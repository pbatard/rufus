/*
 * badblocks.c		- Bad blocks checker
 *
 * Copyright (C) 1992, 1993, 1994  Remy Card <card@masi.ibp.fr>
 *                                 Laboratoire MASI, Institut Blaise Pascal
 *                                 Universite Pierre et Marie Curie (Paris VI)
 *
 * Copyright 1995, 1996, 1997, 1998, 1999 by Theodore Ts'o
 * Copyright 1999 by David Beattie
 * Copyright 2011-2018 by Pete Batard
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
#include <stdint.h>

typedef uint64_t blk_t;
typedef DWORD    errcode_t;

typedef struct bb_struct_u64_list         *bb_badblocks_list;
typedef struct bb_struct_u64_iterate      *bb_badblocks_iterate;
typedef struct bb_struct_u64_list         *bb_u64_list;
typedef struct bb_struct_u64_iterate      *bb_u64_iterate;

#define BB_ET_NO_MEMORY                   (ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_NOT_ENOUGH_MEMORY)
#define BB_ET_MAGIC_BADBLOCKS_LIST        (ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_OBJECT_IN_LIST)
#define BB_ET_MAGIC_BADBLOCKS_ITERATE     (ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_INVALID_BLOCK)

#define BB_CHECK_MAGIC(struct, code) \
	if ((struct)->magic != (code)) return (code)
#define BB_BAD_BLOCKS_THRESHOLD           256
#define BB_BLOCKS_AT_ONCE                 64
#define BB_SYS_PAGE_SIZE                  4096

enum error_types { READ_ERROR, WRITE_ERROR, CORRUPTION_ERROR };
enum op_type { OP_READ, OP_WRITE };

/*
 * Badblocks report
 */
typedef struct {
	uint32_t bb_count;
	uint32_t num_read_errors;
	uint32_t num_write_errors;
	uint32_t num_corruption_errors;
} badblocks_report;

/*
 * Shared prototypes
 */
BOOL BadBlocks(HANDLE hPhysicalDrive, ULONGLONG disk_size, int nb_passes,
	int flash_type, badblocks_report *report, FILE* fd);
