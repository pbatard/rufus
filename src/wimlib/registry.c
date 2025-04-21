/*
 * registry.c
 *
 * Extract information from Windows NT registry hives.
 */

/*
 * Copyright (C) 2016 Eric Biggers
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

#include <string.h>

#include "wimlib/encoding.h"
#include "wimlib/endianness.h"
#include "wimlib/error.h"
#include "wimlib/registry.h"
#include "wimlib/util.h"

/* Registry hive file header  */
struct regf {
#define REGF_MAGIC		cpu_to_le32(0x66676572)	/* "regf" */
	le32 magic;
	le32 f1[4];
#define REGF_MAJOR		cpu_to_le32(1)
	le32 major_version;
	le32 minor_version;
	le32 f2[2];
	le32 root_key_offset;		/* Offset, in hbin area, to root nk  */
	le32 total_hbin_size;		/* Total size of all hbins  */
	le32 f3[1013];
	u8 hbin_area[0];		/* Start of hbin area  */
} __attribute__((packed));


/* Cell header  */
struct cell {
	/* The cell size in bytes, negated for in-use cells  */
	le32 size;

	/* Magic characters which identify the cell type  */
	le16 magic;
} __attribute__((packed));

/* NK cell - represents a registry key  */
struct nk {
#define NK_MAGIC		cpu_to_le16(0x6B6E)	/* "nk"	*/
	struct cell base;
#define NK_COMPRESSED_NAME	cpu_to_le16(0x0020)
	le16 flags;
	le64 unknown_0x08;
	le32 unknown_0x10;
	le32 parent_offset;
	le32 num_subkeys;
	le32 unknown_0x1C;
	le32 subkey_list_offset;
	le32 unknown_0x24;
	le32 num_values;
	le32 value_list_offset;
	le32 unknown_0x30;
	le32 unknown_0x34;
	le16 unknown_0x38;
	le16 unknown_0x3A;
	le32 unknown_0x3C;
	le32 unknown_0x40;
	le32 unknown_0x44;
	le32 unknown_0x48;
	le16 name_size;
	le16 unknown_0x4E;
	char name[0];
} __attribute__((packed));

/* Subkey list cell.  There are four types.  LF, LH, and LI cells reference
 * subkey NK cells directly, while RI cells reference other subkey lists.  All
 * contain a count followed by that many 32-bit offsets.  But LF and LH cells
 * contain a 32-bit hash along with each offset, while LI and RI cells only
 * contain offsets.  */
struct subkey_list {
#define LF_MAGIC	cpu_to_le16(0x666C)	/* "lf"	*/
#define LH_MAGIC	cpu_to_le16(0x686C)	/* "lh"	*/
#define LI_MAGIC	cpu_to_le16(0x696C)	/* "li" */
#define RI_MAGIC	cpu_to_le16(0x6972)	/* "ri" */
	struct cell base;
	le16 num_offsets;
	le32 elements[0];
} __attribute__((packed));

/* Value list cell - contains a list of value references  */
struct value_list {
	le32 size;
	le32 vk_offsets[0];
} __attribute__((packed));

/* VK cell - contains a value's data, or a reference to it  */
struct vk {
#define VK_MAGIC			cpu_to_le16(0x6B76)
	struct cell base;
	le16 name_size;
	le32 data_size;
	le32 data_offset;
#ifndef _WIN32
#define REG_NONE			cpu_to_le32(0)
#define REG_SZ				cpu_to_le32(1)
#define REG_EXPAND_SZ			cpu_to_le32(2)
#define REG_BINARY			cpu_to_le32(3)
#define REG_DWORD			cpu_to_le32(4)
#define REG_DWORD_LITTLE_ENDIAN		cpu_to_le32(4)
#define REG_DWORD_BIG_ENDIAN		cpu_to_le32(5)
#define REG_LINK			cpu_to_le32(6)
#define REG_MULTI_SZ			cpu_to_le32(7)
#define REG_RESOURCE_LIST		cpu_to_le32(8)
#define REG_FULL_RESOURCE_DESCRIPTOR	cpu_to_le32(9)
#define REG_RESOURCE_REQUIREMENTS_LIST	cpu_to_le32(10)
#define REG_QWORD			cpu_to_le32(11)
#define REG_QWORD_LITTLE_ENDIAN		cpu_to_le32(11)
#endif
	le32 data_type;
#define VK_COMPRESSED_NAME		cpu_to_le16(0x0001)
	le16 flags;
	le16 unknown_0x16;
	char name[0];
};

/* Data cell - contains a value's data  */
struct data_cell {
	le32 size;
	u8 data[0];
};

/* Arbitrary limits for safety  */
#define MAX_VALUES		65536
#define MAX_VALUE_SIZE		1048576
#define MAX_SUBKEYS		65536
#define MAX_SUBKEY_LIST_LEVELS	5
#define MAX_SUBKEY_LISTS	4096

static enum hive_status
translate_wimlib_error(int ret)
{
	if (likely(!ret))
		return HIVE_OK;
	if (ret == WIMLIB_ERR_NOMEM)
		return HIVE_OUT_OF_MEMORY;
	return HIVE_UNSUPPORTED;
}

/* Compare a UTF-16LE name with a key or value name in the registry.  The
 * comparison is case insensitive.  */
static inline bool
names_equal(const utf16lechar *name, size_t name_nchars,
	    const void *disk_name, size_t disk_name_size,
	    bool compressed)
{
	if (compressed) {
		/* ISO-8859-1 (LATIN1) on-disk  */
		const u8 *p = disk_name;
		if (disk_name_size != name_nchars)
			return false;
		for (size_t i = 0; i < name_nchars; i++)
			if (upcase[le16_to_cpu(name[i])] != upcase[p[i]])
				return false;
		return true;
	} else {
		/* UTF-16LE on disk  */
		disk_name_size /= 2;
		if (disk_name_size != name_nchars)
			return false;
		return !cmp_utf16le_strings(name, name_nchars,
					    disk_name, disk_name_size, true);
	}
}

/* Get a pointer to a cell, with alignment and bounds checking.  Returns NULL if
 * the requested information does not specify a properly aligned, sized, and
 * in-use cell.  */
static const void *
get_cell_pointer(const struct regf *regf, le32 offset, size_t wanted_size)
{
	u32 total = le32_to_cpu(regf->total_hbin_size);
	u32 offs = le32_to_cpu(offset);
	const struct cell *cell;
	u32 actual_size;

	if ((offs > total) || (offs & 7) || (wanted_size > total - offs))
		return NULL;

	cell = (const struct cell *)&regf->hbin_area[offs];
	actual_size = -le32_to_cpu(cell->size);
	if (actual_size > INT32_MAX) /* Cell unused, or size was INT32_MIN?  */
		return NULL;
	if (wanted_size > actual_size) /* Cell too small?  */
		return NULL;
	return cell;
}

/* Revalidate the cell with its full length.  Returns true iff the cell is
 * valid.  */
static bool
revalidate_cell(const struct regf *regf, le32 offset, size_t wanted_size)
{
	return get_cell_pointer(regf, offset, wanted_size) != NULL;
}

struct subkey_iteration_stats {

	/* The number of additional levels of descendent subkey lists that may
	 * be visited (currently, i.e. at this point in the iteration) before
	 * our safety limit of MAX_SUBKEY_LIST_LEVELS is reached  */
	u32 levels_remaining;

	/* The number of additional subkey lists that may be visited until our
	 * safety limit of MAX_SUBKEY_LISTS is reached  */
	u32 subkey_lists_remaining;

	/* The number of subkeys remaining to be found.  Since the number of
	 * subkeys is known from the parent nk cell, this should be 0 at the end
	 * of the iteration.  */
	u32 subkeys_remaining;
};

typedef enum hive_status (*subkey_cb_t)(const struct nk *, void *);

static enum hive_status
iterate_subkeys_recursive(const struct regf *regf, le32 subkey_list_offset,
			  subkey_cb_t cb, void *cb_ctx,
			  struct subkey_iteration_stats *stats)
{
	const struct subkey_list *list;
	unsigned num_offsets;
	size_t extra_size;
	unsigned increment;
	size_t i = 0;
	enum hive_status status;

	if (stats->levels_remaining == 0 || stats->subkey_lists_remaining == 0)
		return HIVE_CORRUPT;

	stats->subkey_lists_remaining--;

	list = get_cell_pointer(regf, subkey_list_offset,
				sizeof(struct subkey_list));
	if (!list)
		return HIVE_CORRUPT;

	num_offsets = le16_to_cpu(list->num_offsets);
	extra_size = num_offsets * sizeof(list->elements[0]);
	increment = 1;

	if (list->base.magic == LF_MAGIC || list->base.magic == LH_MAGIC) {
		/* Hashes are included  */
		extra_size *= 2;
		increment = 2;
	}

	if (!revalidate_cell(regf, subkey_list_offset,
			     sizeof(struct subkey_list) + extra_size))
		return HIVE_CORRUPT;

	switch (list->base.magic) {
	case LF_MAGIC:
	case LH_MAGIC:
	case LI_MAGIC:
		/* Children are subkeys  */
		if (stats->subkeys_remaining < num_offsets)
			return HIVE_CORRUPT;
		stats->subkeys_remaining -= num_offsets;
		while (num_offsets--) {
			const struct nk *sub_nk;

			sub_nk = get_cell_pointer(regf, list->elements[i],
						  sizeof(struct nk));
			if (!sub_nk || sub_nk->base.magic != NK_MAGIC)
				return HIVE_CORRUPT;

			if (!revalidate_cell(regf, list->elements[i],
					     sizeof(struct nk) +
						le16_to_cpu(sub_nk->name_size)))
				return HIVE_CORRUPT;

			status = (*cb)(sub_nk, cb_ctx);
			if (status != HIVE_OK)
				return status;
			i += increment;
		}
		return HIVE_OK;
	case RI_MAGIC:
		/* Children are subkey lists  */
		status = HIVE_OK;
		stats->levels_remaining--;
		while (num_offsets--) {
			status = iterate_subkeys_recursive(regf,
						list->elements[i++],
						cb, cb_ctx, stats);
			if (status != HIVE_OK)
				break;
		}
		stats->levels_remaining++;
		return status;
	default:
		return HIVE_UNSUPPORTED;
	}
}

/* Call @cb on each subkey cell of the key @nk.  */
static enum hive_status
iterate_subkeys(const struct regf *regf, const struct nk *nk,
		subkey_cb_t cb, void *cb_ctx)
{
	u32 num_subkeys = le32_to_cpu(nk->num_subkeys);
	struct subkey_iteration_stats stats;
	enum hive_status status;

	if (num_subkeys == 0)
		return HIVE_OK;

	if (num_subkeys > MAX_SUBKEYS)
		return HIVE_CORRUPT;

	stats.levels_remaining = MAX_SUBKEY_LIST_LEVELS;
	stats.subkey_lists_remaining = MAX_SUBKEY_LISTS;
	stats.subkeys_remaining = num_subkeys;

	status = iterate_subkeys_recursive(regf, nk->subkey_list_offset,
					   cb, cb_ctx, &stats);
	if (stats.subkeys_remaining != 0 && status == HIVE_OK)
		status = HIVE_CORRUPT;
	return status;
}

struct lookup_subkey_ctx {
	const utf16lechar *key_name;
	size_t key_name_nchars;
	const struct nk *result;
};

static enum hive_status
lookup_subkey_cb(const struct nk *sub_nk, void *_ctx)
{
	struct lookup_subkey_ctx *ctx = _ctx;

	if (names_equal(ctx->key_name, ctx->key_name_nchars,
			sub_nk->name, le16_to_cpu(sub_nk->name_size),
			(sub_nk->flags & NK_COMPRESSED_NAME) != 0))
	{
		ctx->result = sub_nk;
		return HIVE_ITERATION_STOPPED;
	}

	return HIVE_OK;
}

/*
 * Given a registry key cell @nk, look up the next component of the key
 * *key_namep.  If found, return HIVE_OK, advance *key_namep past the key name
 * component, and return the subkey cell in @sub_nk_ret.  Otherwise, return
 * another HIVE_* error code.
 */
static enum hive_status
lookup_subkey(const struct regf *regf, const utf16lechar **key_namep,
	      const struct nk *nk, const struct nk **sub_nk_ret)
{
	const utf16lechar *key_name = *key_namep;
	size_t key_name_nchars = 0;
	struct lookup_subkey_ctx ctx;
	enum hive_status status;

	while (key_name[key_name_nchars] != cpu_to_le16('\0') &&
	       key_name[key_name_nchars] != cpu_to_le16('\\'))
		key_name_nchars++;

	ctx.key_name = key_name;
	ctx.key_name_nchars = key_name_nchars;
	ctx.result = NULL;

	status = iterate_subkeys(regf, nk, lookup_subkey_cb, &ctx);
	if (!ctx.result) {
		if (status == HIVE_OK)
			status = HIVE_KEY_NOT_FOUND;
		return status;
	}

	key_name += key_name_nchars;
	while (*key_name == cpu_to_le16('\\'))
		key_name++;
	*key_namep = key_name;
	*sub_nk_ret = ctx.result;
	return HIVE_OK;
}

/* Find the nk cell for the key named @key_name in the registry hive @regf.  */
static enum hive_status
lookup_key(const struct regf *regf, const tchar *key_name,
	   const struct nk **nk_ret)
{
	const struct nk *nk;
	enum hive_status status;
	const utf16lechar *key_uname, *key_unamep;

	nk = get_cell_pointer(regf, regf->root_key_offset, sizeof(struct nk));
	if (!nk || nk->base.magic != NK_MAGIC)
		return HIVE_CORRUPT;

	status = translate_wimlib_error(tstr_get_utf16le(key_name, &key_uname));
	if (status != HIVE_OK)
		return status;
	key_unamep = key_uname;
	while (*key_unamep) {
		status = lookup_subkey(regf, &key_unamep, nk, &nk);
		if (status != HIVE_OK)
			goto out;
	}
	*nk_ret = nk;
	status = HIVE_OK;
out:
	tstr_put_utf16le(key_uname);
	return status;
}

/* Find the vk cell for the value named @value_name of the key named @key_name
 * in the registry hive @regf.  */
static enum hive_status
lookup_value(const struct regf *regf, const tchar *key_name,
	     const tchar *value_name, const struct vk **vk_ret)
{
	enum hive_status status;
	const struct nk *nk;
	size_t num_values;
	const struct value_list *value_list;
	const  utf16lechar *value_uname;
	size_t value_uname_nchars;

	/* Look up the nk cell for the key.  */
	status = lookup_key(regf, key_name, &nk);
	if (status != HIVE_OK)
		return status;

	num_values = le32_to_cpu(nk->num_values);

	if (num_values == 0) /* No values?  */
		return HIVE_VALUE_NOT_FOUND;

	if (num_values > MAX_VALUES)
		return HIVE_CORRUPT;

	value_list = get_cell_pointer(regf, nk->value_list_offset,
				      sizeof(struct value_list) +
				      (num_values *
				       sizeof(value_list->vk_offsets[0])));
	if (!value_list)
		return HIVE_CORRUPT;

	/* Look for the value in the value list.  */

	status = translate_wimlib_error(
			tstr_get_utf16le_and_len(value_name, &value_uname,
						 &value_uname_nchars));
	if (status != HIVE_OK)
		return status;
	value_uname_nchars /= 2;

	for (size_t i = 0; i < num_values; i++) {
		const struct vk *vk;
		size_t name_size;

		status = HIVE_CORRUPT;
		vk = get_cell_pointer(regf, value_list->vk_offsets[i],
				      sizeof(struct vk));
		if (!vk || vk->base.magic != VK_MAGIC)
			goto out;

		name_size = le16_to_cpu(vk->name_size);

		if (!revalidate_cell(regf, value_list->vk_offsets[i],
				     sizeof(struct vk) + name_size))
			goto out;

		if (names_equal(value_uname, value_uname_nchars,
				vk->name, name_size,
				(vk->flags & VK_COMPRESSED_NAME) != 0))
		{
			*vk_ret = vk;
			status = HIVE_OK;
			goto out;
		}
	}

	status = HIVE_VALUE_NOT_FOUND;
out:
	tstr_put_utf16le(value_uname);
	return status;
}

/*
 * Retrieve the data of the value named @value_name of the key named @key_name
 * in the registry hive @regf.  If the value was found, return HIVE_OK and
 * return the data, its size, and its type in @data_ret, @data_size_ret, and
 * @data_type_ret.  Otherwise, return another HIVE_* error code.
 */
static enum hive_status
retrieve_value(const struct regf *regf, const tchar *key_name,
	       const tchar *value_name, void **data_ret,
	       size_t *data_size_ret, le32 *data_type_ret)
{
	enum hive_status status;
	const struct vk *vk;
	size_t data_size;
	bool is_inline;
	const void *data;

	/* Find the vk cell.  */
	status = lookup_value(regf, key_name, value_name, &vk);
	if (status != HIVE_OK)
		return status;

	/* Extract the value data from the vk cell (for inline data) or from the
	 * data cell which it references (for non-inline data).  */

	data_size = le32_to_cpu(vk->data_size);

	is_inline = (data_size & 0x80000000);
	data_size &= 0x7FFFFFFF;

	if (data_size > MAX_VALUE_SIZE)
		return HIVE_CORRUPT;

	if (is_inline) {
		if (data_size > 4)
			return HIVE_CORRUPT;
		data = &vk->data_offset;
	} else {
		const struct data_cell *data_cell;

		data_cell = get_cell_pointer(regf, vk->data_offset,
					     sizeof(struct data_cell));
		if (!data_cell)
			return HIVE_CORRUPT;

		if (!revalidate_cell(regf, vk->data_offset,
				     sizeof(struct data_cell) + data_size))
			return HIVE_UNSUPPORTED; /* Possibly a big data cell  */

		data = data_cell->data;
	}

	*data_ret = memdup(data, data_size);
	if (!*data_ret)
		return HIVE_OUT_OF_MEMORY;
	*data_size_ret = data_size;
	*data_type_ret = vk->data_type;
	return HIVE_OK;
}

/* Validate the registry hive file given in memory as @hive_mem and @hive_size.
 * If valid, return HIVE_OK.  If invalid, return another HIVE_* error code.  */
enum hive_status
hive_validate(const void *hive_mem, size_t hive_size)
{
	const struct regf *regf = hive_mem;

	STATIC_ASSERT(sizeof(struct regf) == 4096);

	if (hive_size < sizeof(struct regf))
		return HIVE_CORRUPT;

	if (regf->magic != REGF_MAGIC || regf->major_version != REGF_MAJOR)
		return HIVE_UNSUPPORTED;

	if (le32_to_cpu(regf->total_hbin_size) > hive_size - sizeof(struct regf))
		return HIVE_CORRUPT;

	return HIVE_OK;
}

/* Get a string value from the registry hive file.  */
enum hive_status
hive_get_string(const struct regf *regf, const tchar *key_name,
		const tchar *value_name, tchar **value_ret)
{
	void *data;
	size_t data_size;
	le32 data_type;
	enum hive_status status;

	/* Retrieve the raw value data.  */
	status = retrieve_value(regf, key_name, value_name,
				&data, &data_size, &data_type);
	if (status != HIVE_OK)
		return status;

	/* Interpret the data as a string, when possible.  */
	switch (data_type) {
	case REG_SZ:
	case REG_MULTI_SZ:
		status = translate_wimlib_error(
			utf16le_to_tstr(data, data_size, value_ret, &data_size));
		break;
	default:
		status = HIVE_VALUE_IS_WRONG_TYPE;
		break;
	}
	FREE(data);
	return status;
}

/* Get a number value from the registry hive file.  */
enum hive_status
hive_get_number(const struct regf *regf, const tchar *key_name,
		const tchar *value_name, s64 *value_ret)
{
	void *data;
	size_t data_size;
	le32 data_type;
	enum hive_status status;

	/* Retrieve the raw value data.  */
	status = retrieve_value(regf, key_name, value_name,
				&data, &data_size, &data_type);
	if (status != HIVE_OK)
		return status;

	/* Interpret the data as a number, when possible.  */
	switch (data_type) {
	case REG_DWORD_LITTLE_ENDIAN:
		if (data_size == 4) {
			*value_ret = le32_to_cpu(*(le32 *)data);
			status = HIVE_OK;
		} else {
			status = HIVE_CORRUPT;
		}
		break;
	case REG_DWORD_BIG_ENDIAN:
		if (data_size == 4) {
			*value_ret = be32_to_cpu(*(be32 *)data);
			status = HIVE_OK;
		} else {
			status = HIVE_CORRUPT;
		}
		break;
	case REG_QWORD_LITTLE_ENDIAN:
		if (data_size == 8) {
			*value_ret = le64_to_cpu(*(le64 *)data);
			status = HIVE_OK;
		} else {
			status = HIVE_CORRUPT;
		}
		break;
	default:
		status = HIVE_VALUE_IS_WRONG_TYPE;
		break;
	}

	FREE(data);
	return status;
}

static enum hive_status
append_subkey_name(const struct nk *sub_nk, void *_next_subkey_p)
{
	size_t name_size = le16_to_cpu(sub_nk->name_size);
	tchar *subkey;
	tchar ***next_subkeyp = _next_subkey_p;

	if (sub_nk->flags & NK_COMPRESSED_NAME) {
		subkey = MALLOC((name_size + 1) * sizeof(tchar));
		if (!subkey)
			return HIVE_OUT_OF_MEMORY;
		for (size_t i = 0; i < name_size; i++)
			subkey[i] = sub_nk->name[i];
		subkey[name_size] = '\0';
	} else {
		enum hive_status status;

		status = translate_wimlib_error(
			utf16le_to_tstr((utf16lechar *)sub_nk->name,
					name_size, &subkey, NULL));
		if (status != HIVE_OK)
			return status;
	}

	**next_subkeyp = subkey;
	++*next_subkeyp;
	return HIVE_OK;
}

/* List the subkeys of the specified registry key.  */
enum hive_status
hive_list_subkeys(const struct regf *regf, const tchar *key_name,
		  tchar ***subkeys_ret)
{
	enum hive_status status;
	const struct nk *nk;
	tchar **subkeys;
	tchar **next_subkey;

	status = lookup_key(regf, key_name, &nk);
	if (status != HIVE_OK)
		return status;

	if (le32_to_cpu(nk->num_subkeys) > MAX_SUBKEYS)
		return HIVE_CORRUPT;

	subkeys = CALLOC(le32_to_cpu(nk->num_subkeys) + 1, sizeof(subkeys[0]));
	if (!subkeys)
		return HIVE_OUT_OF_MEMORY;

	next_subkey = subkeys;
	status = iterate_subkeys(regf, nk, append_subkey_name, &next_subkey);
	if (status == HIVE_OK)
		*subkeys_ret = subkeys;
	else
		hive_free_subkeys_list(subkeys);
	return status;
}

void
hive_free_subkeys_list(tchar **subkeys)
{
	for (tchar **p = subkeys; *p; p++)
		FREE(*p);
	FREE(subkeys);
}

const char *
hive_status_to_string(enum hive_status status)
{
	switch (status) {
	case HIVE_OK:
		return "HIVE_OK";
	case HIVE_CORRUPT:
		return "HIVE_CORRUPT";
	case HIVE_UNSUPPORTED:
		return "HIVE_UNSUPPORTED";
	case HIVE_KEY_NOT_FOUND:
		return "HIVE_KEY_NOT_FOUND";
	case HIVE_VALUE_NOT_FOUND:
		return "HIVE_VALUE_NOT_FOUND";
	case HIVE_VALUE_IS_WRONG_TYPE:
		return "HIVE_VALUE_IS_WRONG_TYPE";
	case HIVE_OUT_OF_MEMORY:
		return "HIVE_OUT_OF_MEMORY";
	case HIVE_ITERATION_STOPPED:
		return "HIVE_ITERATION_STOPPED";
	}
	return NULL;
}
