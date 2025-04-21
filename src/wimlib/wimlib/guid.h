/*
 * guid.h
 *
 * Utility functions for handling 16-byte globally unique identifiers (GUIDs).
 */

#ifndef _WIMLIB_GUID_H
#define _WIMLIB_GUID_H

#include <string.h>

#include "wimlib/util.h"

#define GUID_SIZE    16

static inline void
copy_guid(u8 dest[GUID_SIZE], const u8 src[GUID_SIZE])
{
	memcpy(dest, src, GUID_SIZE);
}

static inline int
cmp_guids(const u8 guid1[GUID_SIZE], const u8 guid2[GUID_SIZE])
{
	return memcmp(guid1, guid2, GUID_SIZE);
}

static inline bool
guids_equal(const u8 guid1[GUID_SIZE], const u8 guid2[GUID_SIZE])
{
	return !cmp_guids(guid1, guid2);
}

static inline void
generate_guid(u8 guid[GUID_SIZE])
{
	get_random_bytes(guid, GUID_SIZE);
}

#endif /* _WIMLIB_GUID_H */
