/*
 * inode_fixup.c
 */

/*
 * Copyright (C) 2012, 2013, 2014 Eric Biggers
 *
 * This file is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option) any
 * later version.
 *
 * This file is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this file; if not, see https://www.gnu.org/licenses/.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "wimlib/dentry.h"
#include "wimlib/error.h"
#include "wimlib/inode.h"
#include "wimlib/inode_table.h"

struct inode_fixup_params {
	struct wim_inode_table inode_table;
	unsigned long num_dir_hard_links;
	unsigned long num_inconsistent_inodes;
};

#define MAX_DIR_HARD_LINK_WARNINGS 8

static bool
inodes_consistent(const struct wim_inode *inode_1,
		  const struct wim_inode *inode_2)
{
	/* This certainly isn't the only thing we need to check to make sure the
	 * inodes are consistent.  However, this seems to be the only thing that
	 * the MS implementation checks when working around its own bug.
	 *
	 * (Tested: If two dentries share the same hard link group ID, Windows
	 * 8.1 DISM will link them if they have the same unnamed stream hash,
	 * even if the dentries provide different timestamps, attributes,
	 * alternate data streams, and security IDs!  And the one that gets used
	 * will change if you merely swap the filenames.  But if you use
	 * different unnamed stream hashes with everything else the same, it
	 * doesn't link the dentries.)
	 *
	 * For non-buggy WIMs this function will always return true.  */
	return hashes_equal(inode_get_hash_of_unnamed_data_stream(inode_1),
			    inode_get_hash_of_unnamed_data_stream(inode_2));
}

static int
inode_table_insert(struct wim_dentry *dentry, void *_params)
{
	struct inode_fixup_params *params = _params;
	struct wim_inode_table *table = &params->inode_table;
	struct wim_inode *d_inode = dentry->d_inode;
	size_t pos;
	struct wim_inode *inode;

	if (d_inode->i_ino == 0) {
		hlist_add_head(&d_inode->i_hlist_node, &table->extra_inodes);
		return 0;
	}

	/* Try adding this dentry to an existing inode.  */
	pos = hash_inode(table, d_inode->i_ino, 0);
	hlist_for_each_entry(inode, &table->array[pos], i_hlist_node) {
		if (inode->i_ino != d_inode->i_ino) {
			continue;
		}
		if (unlikely(!inodes_consistent(inode, d_inode))) {
			params->num_inconsistent_inodes++;
			continue;
		}
		if (unlikely((d_inode->i_attributes & FILE_ATTRIBUTE_DIRECTORY) ||
			     (inode->i_attributes & FILE_ATTRIBUTE_DIRECTORY)))
		{
			params->num_dir_hard_links++;
			if (params->num_dir_hard_links <=
			    MAX_DIR_HARD_LINK_WARNINGS)
			{
				WARNING("Unsupported directory hard link "
					"\"%"TS"\" <=> \"%"TS"\"",
					dentry_full_path(dentry),
					inode_any_full_path(inode));
			} else if (params->num_dir_hard_links ==
				   MAX_DIR_HARD_LINK_WARNINGS + 1)
			{
				WARNING("Suppressing additional warnings about "
					"directory hard links...");
			}
			continue;
		}
		/* Transfer this dentry to the existing inode.  */
		d_disassociate(dentry);
		d_associate(dentry, inode);
		return 0;
	}

	/* Keep this dentry's inode.  */
	hlist_add_head(&d_inode->i_hlist_node, &table->array[pos]);
	if (++table->filled > table->capacity)
		enlarge_inode_table(table);
	return 0;
}

static void
hlist_move_all(struct hlist_head *src, struct hlist_head *dest)
{
	struct hlist_node *node;

	while ((node = src->first) != NULL) {
		hlist_del(node);
		hlist_add_head(node, dest);
	}
}

/* Move the inodes from the 'struct wim_inode_table' to the 'inode_list'.  */
static void
build_inode_list(struct wim_inode_table *inode_table,
		 struct hlist_head *inode_list)
{
	hlist_move_all(&inode_table->extra_inodes, inode_list);
	for (size_t i = 0; i < inode_table->capacity; i++)
		hlist_move_all(&inode_table->array[i], inode_list);
}

/* Re-assign inode numbers to the inodes in the list.  */
static void
reassign_inode_numbers(struct hlist_head *inode_list)
{
	struct wim_inode *inode;
	u64 cur_ino = 1;

	hlist_for_each_entry(inode, inode_list, i_hlist_node)
		inode->i_ino = cur_ino++;
}

/*
 * Given a WIM image's tree of dentries such that each dentry initially
 * has a unique inode associated with it, determine the actual
 * dentry/inode information.  Following this, a single inode may be named
 * by more than one dentry (usually called a hard link).
 *
 * The 'hard_link_group_id' field of the on-disk WIM dentry, which we
 * have read into 'i_ino' of each dentry's initial inode, determines
 * which dentries share the same inode.  Ideally, dentries share the same
 * inode if and only if they have the same value in this field.  However,
 * exceptions apply:
 *
 * - If 'hard_link_group_id' is 0, the corresponding dentry is the sole
 *   name for its inode.
 * - Due to bugs in the Microsoft implementation, dentries with different
 *   'hard_link_group_id' fields may, in fact, need to be interpreted as
 *   naming different inodes.  This seems to mostly affect images in
 *   install.wim for Windows 7.  I try to work around this in the same way
 *   the Microsoft implementation works around this.
 *
 * Returns 0 or WIMLIB_ERR_NOMEM.  On success, the resulting inodes will be
 * appended to the @inode_list, and they will have consistent numbers in their
 * i_ino fields.
 */
int
dentry_tree_fix_inodes(struct wim_dentry *root, struct hlist_head *inode_list)
{
	struct inode_fixup_params params;
	int ret;

	/* We use a hash table to map inode numbers to inodes.  */

	ret = init_inode_table(&params.inode_table, 64);
	if (ret)
		return ret;

	params.num_dir_hard_links = 0;
	params.num_inconsistent_inodes = 0;

	for_dentry_in_tree(root, inode_table_insert, &params);

	/* Generate the resulting list of inodes, and if needed reassign
	 * the inode numbers.  */
	build_inode_list(&params.inode_table, inode_list);
	destroy_inode_table(&params.inode_table);

	if (unlikely(params.num_dir_hard_links))
		WARNING("Ignoring %lu directory hard links",
			params.num_dir_hard_links);

	if (unlikely(params.num_inconsistent_inodes ||
		     params.num_dir_hard_links))
		reassign_inode_numbers(inode_list);
	return 0;
}
