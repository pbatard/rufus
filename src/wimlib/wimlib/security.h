#ifndef _WIMLIB_SECURITY_H
#define _WIMLIB_SECURITY_H

#include "wimlib/types.h"

struct wim_security_data;
struct avl_tree_node;

/* Map from SHA1 message digests of security descriptors to security IDs, which
 * are themselves indices into the table of security descriptors in the 'struct
 * wim_security_data'. */
struct wim_sd_set {
	struct wim_security_data *sd;
	struct avl_tree_node *root;
	s32 orig_num_entries;
};

/* Table of security descriptors for a WIM image. */
struct wim_security_data {
	/* The total length of the security data, in bytes.  If there are no
	 * security descriptors, this field, when read from the on-disk metadata
	 * resource, may be either 8 (which is correct) or 0 (which is
	 * interpreted as 8). */
	u32 total_length;

	/* The number of security descriptors in the array @descriptors. */
	u32 num_entries;

	/* Array of sizes of the descriptors, in bytes, in the array
	 * @descriptors. */
	u64 *sizes;

	/* Array of pointers to the security descriptors in the
	 * SECURITY_DESCRIPTOR_RELATIVE format. */
	u8 **descriptors;
};

void
rollback_new_security_descriptors(struct wim_sd_set *sd_set);

void
destroy_sd_set(struct wim_sd_set *sd_set);

s32
sd_set_add_sd(struct wim_sd_set *sd_set, const char descriptor[],
	      size_t size);

int
init_sd_set(struct wim_sd_set *sd_set, struct wim_security_data *sd);

struct wim_security_data *
new_wim_security_data(void);

int
read_wim_security_data(const u8 *buf, size_t buf_len,
		       struct wim_security_data **sd_ret);

u8 *
write_wim_security_data(const struct wim_security_data * restrict sd,
			u8 * restrict p);

void
print_wim_security_data(const struct wim_security_data *sd);

void
free_wim_security_data(struct wim_security_data *sd);

#endif /* _WIMLIB_SECURITY_H */
