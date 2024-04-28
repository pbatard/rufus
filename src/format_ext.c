/*
 * Rufus: The Reliable USB Formatting Utility
 * extfs formatting
 * Copyright © 2019-2024 Pete Batard <pete@akeo.ie>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "rufus.h"
#include "file.h"
#include "drive.h"
#include "format.h"
#include "missing.h"
#include "resource.h"
#include "msapi_utf8.h"
#include "localization.h"
#include "ext2fs/ext2fs.h"

extern const char* FileSystemLabel[FS_MAX];
extern io_manager nt_io_manager;
extern DWORD ext2_last_winerror(DWORD default_error);
static float ext2_percent_start = 0.0f, ext2_percent_share = 0.5f;
const float ext2_max_marker = 80.0f;

typedef struct {
	uint64_t max_size;
	uint32_t block_size;
	uint32_t inode_size;
	uint32_t inode_ratio;
} ext2fs_default_t;

const char* error_message(errcode_t error_code)
{
	static char error_string[256];

	switch (error_code) {
	case EXT2_ET_MAGIC_EXT2FS_FILSYS:
	case EXT2_ET_MAGIC_BADBLOCKS_LIST:
	case EXT2_ET_MAGIC_BADBLOCKS_ITERATE:
	case EXT2_ET_MAGIC_INODE_SCAN:
	case EXT2_ET_MAGIC_IO_CHANNEL:
	case EXT2_ET_MAGIC_IO_MANAGER:
	case EXT2_ET_MAGIC_BLOCK_BITMAP:
	case EXT2_ET_MAGIC_INODE_BITMAP:
	case EXT2_ET_MAGIC_GENERIC_BITMAP:
	case EXT2_ET_MAGIC_ICOUNT:
	case EXT2_ET_MAGIC_EXTENT_HANDLE:
	case EXT2_ET_BAD_MAGIC:
		return "Bad magic";
	case EXT2_ET_RO_FILSYS:
		return "Read-only file system";
	case EXT2_ET_GDESC_BAD_BLOCK_MAP:
	case EXT2_ET_GDESC_BAD_INODE_MAP:
	case EXT2_ET_GDESC_BAD_INODE_TABLE:
		return "Bad map or table";
	case EXT2_ET_UNEXPECTED_BLOCK_SIZE:
		return "Unexpected block size";
	case EXT2_ET_DIR_CORRUPTED:
		return "Corrupted entry";
	case EXT2_ET_GDESC_READ:
	case EXT2_ET_GDESC_WRITE:
	case EXT2_ET_INODE_BITMAP_WRITE:
	case EXT2_ET_INODE_BITMAP_READ:
	case EXT2_ET_BLOCK_BITMAP_WRITE:
	case EXT2_ET_BLOCK_BITMAP_READ:
	case EXT2_ET_INODE_TABLE_WRITE:
	case EXT2_ET_INODE_TABLE_READ:
	case EXT2_ET_NEXT_INODE_READ:
	case EXT2_ET_SHORT_READ:
	case EXT2_ET_SHORT_WRITE:
		return "read/write error";
	case EXT2_ET_DIR_NO_SPACE:
		return "no space left";
	case EXT2_ET_TOOSMALL:
		return "Too small";
	case EXT2_ET_BAD_DEVICE_NAME:
		return "Bad device name";
	case EXT2_ET_MISSING_INODE_TABLE:
		return "Missing inode table";
	case EXT2_ET_CORRUPT_SUPERBLOCK:
		return "Superblock is corrupted";
	case EXT2_ET_CALLBACK_NOTHANDLED:
		return "Unhandled callback";
	case EXT2_ET_BAD_BLOCK_IN_INODE_TABLE:
		return "Bad block in inode table";
	case EXT2_ET_UNSUPP_FEATURE:
	case EXT2_ET_RO_UNSUPP_FEATURE:
	case EXT2_ET_UNIMPLEMENTED:
		return "Unsupported feature";
	case EXT2_ET_LLSEEK_FAILED:
		return "Seek failed";
	case EXT2_ET_NO_MEMORY:
	case EXT2_ET_BLOCK_ALLOC_FAIL:
	case EXT2_ET_INODE_ALLOC_FAIL:
		return "Out of memory";
	case EXT2_ET_INVALID_ARGUMENT:
		return "Invalid argument";
	case EXT2_ET_NO_DIRECTORY:
		return "No directory";
	case EXT2_ET_FILE_NOT_FOUND:
		return "File not found";
	case EXT2_ET_FILE_RO:
		return "File is read-only";
	case EXT2_ET_DIR_EXISTS:
		return "Directory already exists";
	case EXT2_ET_CANCEL_REQUESTED:
		return "Cancel requested";
	case EXT2_ET_FILE_TOO_BIG:
		return "File too big";
	case EXT2_ET_JOURNAL_NOT_BLOCK:
	case EXT2_ET_NO_JOURNAL_SB:
		return "No journal superblock";
	case EXT2_ET_JOURNAL_TOO_SMALL:
		return "Journal too small";
	case EXT2_ET_NO_JOURNAL:
		return "No journal";
	case EXT2_ET_TOO_MANY_INODES:
		return "Too many inodes";
	case EXT2_ET_NO_CURRENT_NODE:
		return "No current node";
	case EXT2_ET_OP_NOT_SUPPORTED:
		return "Operation not supported";
	case EXT2_ET_IO_CHANNEL_NO_SUPPORT_64:
		return "I/O Channel does not support 64-bit operation";
	case EXT2_ET_BAD_DESC_SIZE:
		return "Bad descriptor size";
	case EXT2_ET_INODE_CSUM_INVALID:
	case EXT2_ET_INODE_BITMAP_CSUM_INVALID:
	case EXT2_ET_EXTENT_CSUM_INVALID:
	case EXT2_ET_DIR_CSUM_INVALID:
	case EXT2_ET_EXT_ATTR_CSUM_INVALID:
	case EXT2_ET_SB_CSUM_INVALID:
	case EXT2_ET_BLOCK_BITMAP_CSUM_INVALID:
	case EXT2_ET_MMP_CSUM_INVALID:
		return "Invalid checksum";
	case EXT2_ET_UNKNOWN_CSUM:
		return "Unknown checksum";
	case EXT2_ET_FILE_EXISTS:
		return "File exists";
	case EXT2_ET_INODE_IS_GARBAGE:
		return "Inode is garbage";
	case EXT2_ET_JOURNAL_FLAGS_WRONG:
		return "Wrong journal flags";
	case EXT2_ET_FILESYSTEM_CORRUPTED:
		return "File system is corrupted";
	case EXT2_ET_BAD_CRC:
		return "Bad CRC";
	case EXT2_ET_CORRUPT_JOURNAL_SB:
		return "Journal Superblock is corrupted";
	case EXT2_ET_INODE_CORRUPTED:
	case EXT2_ET_EA_INODE_CORRUPTED:
		return "Inode is corrupted";
	case EXT2_ET_NO_GDESC:
		return "Group descriptors not loaded";
	default:
		if ((error_code > EXT2_ET_BASE) && error_code < (EXT2_ET_BASE + 1000)) {
			static_sprintf(error_string, "Unknown ext2fs error %ld (EXT2_ET_BASE + %ld)", error_code, error_code - EXT2_ET_BASE);
		} else {
			SetLastError((ErrorStatus == 0) ? RUFUS_ERROR(error_code & 0xFFFF) : ErrorStatus);
			static_sprintf(error_string, "%s", WindowsErrorString());
		}
		return error_string;
	}
}

errcode_t ext2fs_print_progress(int64_t cur_value, int64_t max_value)
{
	static int64_t last_value = -1;
	if (max_value == 0)
		return 0;
	UpdateProgressWithInfo(OP_FORMAT, MSG_217, (uint64_t)((ext2_percent_start * max_value) + (ext2_percent_share * cur_value)), max_value);
	cur_value = (int64_t)(((float)cur_value / (float)max_value) * min(ext2_max_marker, (float)max_value));
	if (cur_value != last_value) {
		last_value = cur_value;
		uprintfs("+");
	}
	return IS_ERROR(ErrorStatus) ? EXT2_ET_CANCEL_REQUESTED : 0;
}

const char* GetExtFsLabel(DWORD DriveIndex, uint64_t PartitionOffset)
{
	static char label[EXT2_LABEL_LEN + 1];
	errcode_t r;
	ext2_filsys ext2fs = NULL;
	io_manager manager = nt_io_manager;
	char* volume_name = GetExtPartitionName(DriveIndex, PartitionOffset);

	if (volume_name == NULL)
		return NULL;
	r = ext2fs_open(volume_name, EXT2_FLAG_SKIP_MMP, 0, 0, manager, &ext2fs);
	free(volume_name);
	if (r == 0) {
		assert(ext2fs != NULL);
		strncpy(label, ext2fs->super->s_volume_name, EXT2_LABEL_LEN);
		label[EXT2_LABEL_LEN] = 0;
	}
	if (ext2fs != NULL)
		ext2fs_close(ext2fs);
	return (r == 0) ? label : NULL;
}

#define TEST_IMG_PATH               "\\??\\C:\\tmp\\disk.img"
#define TEST_IMG_SIZE               4000		// Size in MB
#define SET_EXT2_FORMAT_ERROR(x)    if (!IS_ERROR(ErrorStatus)) ErrorStatus = ext2_last_winerror(x)

BOOL FormatExtFs(DWORD DriveIndex, uint64_t PartitionOffset, DWORD BlockSize, LPCSTR FSName, LPCSTR Label, DWORD Flags)
{
	// Mostly taken from mke2fs.conf
	const float reserve_ratio = 0.05f;
	const ext2fs_default_t ext2fs_default[5] = {
		{ 3 * MB, 1024, 128, 3},	// "floppy"
		{ 512 * MB, 1024, 128, 2},	// "small"
		{ 4 * GB, 4096, 256, 2},	// "default"
		{ 16 * GB, 4096, 256, 3},	// "big"
		{ 1024 * TB, 4096, 256, 4}	// "huge"
	};

	BOOL ret = FALSE;
	char* volume_name = NULL;
	int i, count;
	struct ext2_super_block features = { 0 };
	io_manager manager = nt_io_manager;
	blk_t journal_size;
	blk64_t size = 0, cur;
	ext2_filsys ext2fs = NULL;
	errcode_t r;
	uint8_t* buf = NULL;

#if defined(RUFUS_TEST)
	// Create a disk image file to test
	uint8_t zb[1024];
	HANDLE h;
	DWORD dwSize;
	HCRYPTPROV hCryptProv = 0;
	volume_name = strdup(TEST_IMG_PATH);
	uprintf("Creating '%s'...", volume_name);
	if (!CryptAcquireContext(&hCryptProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT) || !CryptGenRandom(hCryptProv, sizeof(zb), zb)) {
		uprintf("Failed to randomize buffer - filling with constant value");
		memset(zb, rand(), sizeof(zb));
	}
	CryptReleaseContext(hCryptProv, 0);
	h = CreateFileU(volume_name, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	for (i = 0; i < TEST_IMG_SIZE * sizeof(zb); i++) {
		if (!WriteFile(h, zb, sizeof(zb), &dwSize, NULL) || (dwSize != sizeof(zb))) {
			uprintf("Write error: %s", WindowsErrorString());
			break;
		}
	}
	CloseHandle(h);
#else
	volume_name = GetExtPartitionName(DriveIndex, PartitionOffset);
#endif
	if ((volume_name == NULL) | (strlen(FSName) != 4) || (strncmp(FSName, "ext", 3) != 0)) {
		ErrorStatus = RUFUS_ERROR(ERROR_INVALID_PARAMETER);
		goto out;
	}
	if (strchr(volume_name, ' ') != NULL)
		uprintf("Notice: Using physical device to access partition data");

	if ((strcmp(FSName, FileSystemLabel[FS_EXT2]) != 0) && (strcmp(FSName, FileSystemLabel[FS_EXT3]) != 0)) {
		if (strcmp(FSName, FileSystemLabel[FS_EXT4]) == 0)
			uprintf("ext4 file system is not supported, defaulting to ext3");
		else
			uprintf("Invalid ext file system version requested, defaulting to ext3");
		FSName = FileSystemLabel[FS_EXT3];
	}

	PrintInfoDebug(0, MSG_222, FSName);
	UpdateProgressWithInfoInit(NULL, TRUE);

	// Figure out the volume size and block size
	r = ext2fs_get_device_size2(volume_name, KB, &size);
	if ((r != 0) || (size == 0)) {
		SET_EXT2_FORMAT_ERROR(ERROR_READ_FAULT);
		uprintf("Could not read device size: %s", error_message(r));
		goto out;
	}
	size *= KB;
	for (i = 0; i < ARRAYSIZE(ext2fs_default); i++) {
		if (size < ext2fs_default[i].max_size)
			break;
	}
	assert(i < ARRAYSIZE(ext2fs_default));
	if ((BlockSize == 0) || (BlockSize < EXT2_MIN_BLOCK_SIZE))
		BlockSize = ext2fs_default[i].block_size;
	assert(IS_POWER_OF_2(BlockSize));
	for (features.s_log_block_size = 0; EXT2_BLOCK_SIZE_BITS(&features) <= EXT2_MAX_BLOCK_LOG_SIZE; features.s_log_block_size++) {
		if (EXT2_BLOCK_SIZE(&features) == BlockSize)
			break;
	}
	assert(EXT2_BLOCK_SIZE_BITS(&features) <= EXT2_MAX_BLOCK_LOG_SIZE);
	features.s_log_cluster_size = features.s_log_block_size;
	size /= BlockSize;

	// ext2 and ext3 have a can only accommodate up to Blocksize * 2^32 sized volumes
	if (((strcmp(FSName, FileSystemLabel[FS_EXT2]) == 0) || (strcmp(FSName, FileSystemLabel[FS_EXT3]) == 0)) &&
		(size >= 0x100000000ULL)) {
		SET_EXT2_FORMAT_ERROR(ERROR_INVALID_VOLUME_SIZE);
		uprintf("Volume size is too large for ext2 or ext3");
		goto out;
	}

	// Set the blocks, reserved blocks and inodes
	ext2fs_blocks_count_set(&features, size);
	ext2fs_r_blocks_count_set(&features, (blk64_t)(reserve_ratio * size));
	features.s_rev_level = 1;
	features.s_inode_size = ext2fs_default[i].inode_size;
	features.s_inodes_count = ((ext2fs_blocks_count(&features) >> ext2fs_default[i].inode_ratio) > UINT32_MAX) ?
		UINT32_MAX : (uint32_t)(ext2fs_blocks_count(&features) >> ext2fs_default[i].inode_ratio);
	uprintf("%d possible inodes out of %lld blocks (block size = %d)", features.s_inodes_count, size, EXT2_BLOCK_SIZE(&features));
	uprintf("%lld blocks (%0.1f%%) reserved for the super user", ext2fs_r_blocks_count(&features), reserve_ratio * 100.0f);

	// Set features
	ext2fs_set_feature_dir_index(&features);
	ext2fs_set_feature_filetype(&features);
	ext2fs_set_feature_large_file(&features);
	ext2fs_set_feature_sparse_super(&features);
	ext2fs_set_feature_xattr(&features);
	if (FSName[3] != '2')
		ext2fs_set_feature_journal(&features);
	features.s_default_mount_opts = EXT2_DEFM_XATTR_USER | EXT2_DEFM_ACL;

	// Now that we have set our base features, initialize a virtual superblock
	r = ext2fs_initialize(volume_name, EXT2_FLAG_EXCLUSIVE | EXT2_FLAG_64BITS, &features, manager, &ext2fs);
	if (r != 0) {
		SET_EXT2_FORMAT_ERROR(ERROR_INVALID_DATA);
		uprintf("Could not initialize %s features: %s", FSName, error_message(r));
		goto out;
	}

	// Zero 16 blocks of data from the start of our volume
	buf = calloc(16, ext2fs->io->block_size);
	assert(buf != NULL);
	r = io_channel_write_blk64(ext2fs->io, 0, 16, buf);
	safe_free(buf);
	if (r != 0) {
		SET_EXT2_FORMAT_ERROR(ERROR_WRITE_FAULT);
		uprintf("Could not zero %s superblock area: %s", FSName, error_message(r));
		goto out;
	}

	// Finish setting up the file system
	IGNORE_RETVAL(CoCreateGuid((GUID*)ext2fs->super->s_uuid));
	ext2fs_init_csum_seed(ext2fs);
	ext2fs->super->s_def_hash_version = EXT2_HASH_HALF_MD4;
	IGNORE_RETVAL(CoCreateGuid((GUID*)ext2fs->super->s_hash_seed));
	ext2fs->super->s_max_mnt_count = -1;
	ext2fs->super->s_creator_os = EXT2_OS_WINDOWS;
	ext2fs->super->s_errors = EXT2_ERRORS_CONTINUE;
	if (Label != NULL)
		static_strcpy(ext2fs->super->s_volume_name, Label);

	r = ext2fs_allocate_tables(ext2fs);
	if (r != 0) {
		SET_EXT2_FORMAT_ERROR(ERROR_INVALID_DATA);
		uprintf("Could not allocate %s tables: %s", FSName, error_message(r));
		goto out;
	}
	r = ext2fs_convert_subcluster_bitmap(ext2fs, &ext2fs->block_map);
	if (r != 0) {
		uprintf("Could not set %s cluster bitmap: %s", FSName, error_message(r));
		goto out;
	}

	ext2_percent_start = 0.0f;
	ext2_percent_share = (FSName[3] == '2') ? 1.0f : 0.5f;
	uprintf("Creating %d inode sets: [1 marker = %0.1f set(s)]", ext2fs->group_desc_count,
		max((float)ext2fs->group_desc_count / ext2_max_marker, 1.0f));
	for (i = 0; i < (int)ext2fs->group_desc_count; i++) {
		if (ext2fs_print_progress((int64_t)i, (int64_t)ext2fs->group_desc_count))
			goto out;
		cur = ext2fs_inode_table_loc(ext2fs, i);
		count = ext2fs_div_ceil((ext2fs->super->s_inodes_per_group - ext2fs_bg_itable_unused(ext2fs, i))
			* EXT2_INODE_SIZE(ext2fs->super), EXT2_BLOCK_SIZE(ext2fs->super));
		r = ext2fs_zero_blocks2(ext2fs, cur, count, &cur, &count);
		if (r != 0) {
			SET_EXT2_FORMAT_ERROR(ERROR_WRITE_FAULT);
			uprintf("\r\nCould not zero inode set at position %llu (%d blocks): %s", cur, count, error_message(r));
			goto out;
		}
	}
	uprintfs("\r\n");

	// Create root and lost+found dirs
	r = ext2fs_mkdir(ext2fs, EXT2_ROOT_INO, EXT2_ROOT_INO, 0);
	if (r != 0) {
		SET_EXT2_FORMAT_ERROR(ERROR_FILE_CORRUPT);
		uprintf("Failed to create %s root dir: %s", FSName, error_message(r));
		goto out;
	}
	ext2fs->umask = 077;
	r = ext2fs_mkdir(ext2fs, EXT2_ROOT_INO, 0, "lost+found");
	if (r != 0) {
		SET_EXT2_FORMAT_ERROR(ERROR_FILE_CORRUPT);
		uprintf("Failed to create %s 'lost+found' dir: %s", FSName, error_message(r));
		goto out;
	}

	// Create bitmaps
	for (i = EXT2_ROOT_INO + 1; i < (int)EXT2_FIRST_INODE(ext2fs->super); i++)
		ext2fs_inode_alloc_stats(ext2fs, i, 1);
	ext2fs_mark_ib_dirty(ext2fs);

	r = ext2fs_mark_inode_bitmap2(ext2fs->inode_map, EXT2_BAD_INO);
	if (r != 0) {
		SET_EXT2_FORMAT_ERROR(ERROR_WRITE_FAULT);
		uprintf("Could not set inode bitmaps: %s", error_message(r));
		goto out;
	}
	ext2fs_inode_alloc_stats(ext2fs, EXT2_BAD_INO, 1);
	r = ext2fs_update_bb_inode(ext2fs, NULL);
	if (r != 0) {
		SET_EXT2_FORMAT_ERROR(ERROR_WRITE_FAULT);
		uprintf("Could not set inode stats: %s", error_message(r));
		goto out;
	}

	if (FSName[3] != '2') {
		// Create the journal
		ext2_percent_start = 0.5f;
		journal_size = ext2fs_default_journal_size(ext2fs_blocks_count(ext2fs->super));
		journal_size /= 2;	// That journal init is really killing us!
		uprintf("Creating %d journal blocks: [1 marker = %0.1f block(s)]", journal_size,
			max((float)journal_size / ext2_max_marker, 1.0f));
		// Even with EXT2_MKJOURNAL_LAZYINIT, this call is absolutely dreadful in terms of speed...
		r = ext2fs_add_journal_inode(ext2fs, journal_size, EXT2_MKJOURNAL_NO_MNT_CHECK | ((Flags & FP_QUICK) ? EXT2_MKJOURNAL_LAZYINIT : 0));
		uprintfs("\r\n");
		if (r != 0) {
			SET_EXT2_FORMAT_ERROR(ERROR_WRITE_FAULT);
			uprintf("Could not create %s journal: %s", FSName, error_message(r));
			goto out;
		}
	}

	// Create a 'persistence.conf' file if required
	if (Flags & FP_CREATE_PERSISTENCE_CONF) {
		// You *do* want the LF at the end of the "/ union" line, else Debian Live bails out...
		const char* name = "persistence.conf", data[] = "/ union\n";
		int written = 0, fsize = sizeof(data) - 1;
		ext2_file_t ext2fd;
		ext2_ino_t inode_id;
		time_t ctime = time(NULL);
		struct ext2_inode inode = { 0 };
		// Don't care about the Y2K38 problem of ext2/ext3 for a 'persistence.conf' timestamp
		if (ctime > UINT32_MAX)
			ctime = UINT32_MAX;
		inode.i_mode = 0100644;
		inode.i_links_count = 1;
		// coverity[store_truncates_time_t]
		inode.i_atime = (uint32_t)ctime;
		// coverity[store_truncates_time_t]
		inode.i_ctime = (uint32_t)ctime;
		// coverity[store_truncates_time_t]
		inode.i_mtime = (uint32_t)ctime;
		inode.i_size = fsize;

		ext2fs_namei(ext2fs, EXT2_ROOT_INO, EXT2_ROOT_INO, name, &inode_id);
		ext2fs_new_inode(ext2fs, EXT2_ROOT_INO, 010755, 0, &inode_id);
		ext2fs_link(ext2fs, EXT2_ROOT_INO, name, inode_id, EXT2_FT_REG_FILE);
		ext2fs_inode_alloc_stats(ext2fs, inode_id, 1);
		ext2fs_write_new_inode(ext2fs, inode_id, &inode);
		ext2fs_file_open(ext2fs, inode_id, EXT2_FILE_WRITE, &ext2fd);
		if ((ext2fs_file_write(ext2fd, data, fsize, &written) != 0) || (written != fsize))
			uprintf("Error: Could not create '%s' file", name);
		else
			uprintf("Created '%s' file", name);
		ext2fs_file_close(ext2fd);
	}

	// Finally we can call close() to get the file system gets created
	r = ext2fs_close(ext2fs);
	if (r == 0) {
		// Make sure ext2fs isn't freed twice
		ext2fs = NULL;
	} else {
		SET_EXT2_FORMAT_ERROR(ERROR_WRITE_FAULT);
		uprintf("Could not create %s volume: %s", FSName, error_message(r));
		goto out;
	}
	UpdateProgressWithInfo(OP_FORMAT, MSG_217, 100, 100);
	ret = TRUE;

out:
	free(volume_name);
	ext2fs_free(ext2fs);
	free(buf);
	return ret;
}
