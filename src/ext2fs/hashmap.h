#ifndef HASHMAP_H
# define HASHMAP_H

# include <stdlib.h>
# include <stdint.h>

#ifndef __GNUC_PREREQ
#if defined(__GNUC__) && defined(__GNUC_MINOR__)
#define __GNUC_PREREQ(maj, min) \
	((__GNUC__ << 16) + __GNUC_MINOR__ >= ((maj) << 16) + (min))
#else
#define __GNUC_PREREQ(maj, min) 0
#endif
#endif

struct ext2fs_hashmap {
	uint32_t size;
	uint32_t(*hash)(const void *key, size_t len);
	void(*free)(void*);
	struct ext2fs_hashmap_entry *first;
	struct ext2fs_hashmap_entry *last;
	struct ext2fs_hashmap_entry {
		void *data;
		const void *key;
		size_t key_len;
		struct ext2fs_hashmap_entry *next;
		struct ext2fs_hashmap_entry *list_next;
		struct ext2fs_hashmap_entry *list_prev;
#if __GNUC_PREREQ (4, 8)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
	} *entries[0];
#if __GNUC_PREREQ (4, 8)
#pragma GCC diagnostic pop
#endif
};

struct ext2fs_hashmap *ext2fs_hashmap_create(
				uint32_t(*hash_fct)(const void*, size_t),
				void(*free_fct)(void*), size_t size);
void ext2fs_hashmap_add(struct ext2fs_hashmap *h, void *data, const void *key,
			size_t key_len);
void *ext2fs_hashmap_lookup(struct ext2fs_hashmap *h, const void *key,
			    size_t key_len);
void *ext2fs_hashmap_iter_in_order(struct ext2fs_hashmap *h,
				   struct ext2fs_hashmap_entry **it);
void ext2fs_hashmap_del(struct ext2fs_hashmap *h,
			struct ext2fs_hashmap_entry *e);
void ext2fs_hashmap_free(struct ext2fs_hashmap *h);

uint32_t ext2fs_djb2_hash(const void *str, size_t size);

#endif /* !HASHMAP_H */
