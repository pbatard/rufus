/*
 * badblocks.c		- Bad blocks checker
 *
 * Copyright (C) 1992, 1993, 1994  Remy Card <card@masi.ibp.fr>
 *                                 Laboratoire MASI, Institut Blaise Pascal
 *                                 Universite Pierre et Marie Curie (Paris VI)
 *
 * Copyright 1995, 1996, 1997, 1998, 1999 by Theodore Ts'o
 * Copyright 1999 by David Beattie
 * Copyright 2011-2012 by Pete Batard
 *
 * This file is based on the minix file system programs fsck and mkfs
 * written and copyrighted by Linus Torvalds <Linus.Torvalds@cs.helsinki.fi>
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#include <windows.h>

#ifndef __u32
#define __u32  UINT32
#endif
typedef UINT32 blk_t;
typedef DWORD  errcode_t;

typedef struct ext2_struct_u32_list         *ext2_badblocks_list;
typedef struct ext2_struct_u32_iterate      *ext2_badblocks_iterate;
typedef struct ext2_struct_u32_list         *ext2_u32_list;
typedef struct ext2_struct_u32_iterate      *ext2_u32_iterate;

#define EXT2_ET_NO_MEMORY                   (ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_NOT_ENOUGH_MEMORY)
#define EXT2_ET_MAGIC_BADBLOCKS_LIST        (ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_OBJECT_IN_LIST)
#define EXT2_ET_MAGIC_BADBLOCKS_ITERATE     (ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_INVALID_BLOCK)

#define EXT2_CHECK_MAGIC(struct, code) \
	if ((struct)->magic != (code)) return (code)
#define EXT2_BAD_BLOCKS_THRESHOLD           256
#define EXT2_BLOCKS_AT_ONCE                 64
#define EXT2_SYS_PAGE_SIZE                  4096

enum error_types { READ_ERROR, WRITE_ERROR, CORRUPTION_ERROR };
enum op_type { OP_READ, OP_WRITE };

/*
 * Badblocks report
 */
typedef struct {
	blk_t bb_count;
	blk_t num_read_errors;
	blk_t num_write_errors;
	blk_t num_corruption_errors;
} badblocks_report;

/*
 * Shared prototypes
 */
BOOL BadBlocks(HANDLE hPhysicalDrive, ULONGLONG disk_size, int block_size,
	int test_type, badblocks_report *report, FILE* fd);
