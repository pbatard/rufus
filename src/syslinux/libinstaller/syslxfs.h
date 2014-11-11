/*
 *   Copyright 2011-2012 Paulo Alcantara <pcacjr@zytor.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 53 Temple Place Ste 330,
 *   Boston MA 02111-1307, USA; either version 2 of the License, or
 *   (at your option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

#ifndef _SYSLXFS_H_
#define _SYSLXFS_H_

/* Global fs_type for handling fat, ntfs, ext2/3/4, btrfs, xfs and ufs1/2 */
enum filesystem {
    NONE,
    EXT2,
    BTRFS,
    VFAT,
    NTFS,
    XFS,
    UFS1,
    UFS2,
};

//extern int fs_type;

#endif /* _SYSLXFS_H_ */
