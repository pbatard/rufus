/* ----------------------------------------------------------------------- *
 *
 *   Copyright 2004-2008 H. Peter Anvin - All Rights Reserved
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 53 Temple Place Ste 330,
 *   Boston MA 02111-1307, USA; either version 2 of the License, or
 *   (at your option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/*
 * libfat.h
 *
 * Headers for the libfat library
 */

#ifndef LIBFAT_H
#define LIBFAT_H

#include <stddef.h>
#include <inttypes.h>

#define LIBFAT_SECTOR_SHIFT	9
#define LIBFAT_SECTOR_SIZE	512
#define LIBFAT_SECTOR_MASK	511

typedef uint64_t libfat_sector_t;
struct libfat_filesystem;

struct libfat_direntry {
    libfat_sector_t sector;
    int offset;
    unsigned char entry[32];
};

/*
 * Open the filesystem.  The readfunc is the function to read
 * sectors, in the format:
 * int readfunc(intptr_t readptr, void *buf, size_t secsize,
 *              libfat_sector_t secno)
 *
 * ... where readptr is a private argument.
 *
 * A return value of != secsize is treated as error.
 */
struct libfat_filesystem
    *libfat_open(int (*readfunc) (intptr_t, void *, size_t, libfat_sector_t),
		 intptr_t readptr);

void libfat_close(struct libfat_filesystem *);

/*
 * Convert a cluster number (or 0 for the root directory) to a
 * sector number.  Return -1 on failure.
 */
libfat_sector_t libfat_clustertosector(const struct libfat_filesystem *fs,
				       int32_t cluster);

/*
 * Get the next sector of either the root directory or a FAT chain.
 * Returns 0 on end of file and -1 on error.
 */
libfat_sector_t libfat_nextsector(struct libfat_filesystem *fs,
				  libfat_sector_t s);

/*
 * Flush all cached sectors for this filesystem.
 */
void libfat_flush(struct libfat_filesystem *fs);

/*
 * Get a pointer to a specific sector.
 */
void *libfat_get_sector(struct libfat_filesystem *fs, libfat_sector_t n);

/*
 * Search a FAT directory for a particular pre-mangled filename.
 * Copies the directory entry into direntry and returns 0 if found.
 */
int32_t libfat_searchdir(struct libfat_filesystem *fs, int32_t dirclust,
			 const void *name, struct libfat_direntry *direntry);

#endif /* LIBFAT_H */
