/*
 * wof.h
 *
 * Definitions for the Windows Overlay Filesystem filter (WOF) ioctls, as well
 * some definitions for associated undocumented data structures.
 *
 * Copyright 2022 Eric Biggers
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _WOF_H_
#define _WOF_H_

#include "wimlib/compiler.h"
#include "wimlib/types.h"

#ifdef _WIN32

#ifndef _MSC_VER
/*
 * The Windows Overlay Filesystem filter (WOF, a.k.a. wof.sys) is a filesystem
 * filter driver, available in Windows 8.1 and later, which allows files to be
 * "externally backed", meaning that their data is stored in another location,
 * possibly in compressed form.
 *
 * WOF implements a plug-in mechanism by which a specific "provider" is
 * responsible for actually externally backing a given file.  The currently
 * known providers are:
 *
 *	- The WIM provider: allows a file to be externally backed by a
 *	  compressed resource in a WIM archive
 *	- The file provider: allows a file to be "externally backed" by a named
 *	  data stream stored with the file itself, where that named data stream
 *	  has the format of a compressed WIM resource
 *
 * For both of these providers, externally backed files are effectively
 * read-only.  If you try to write to such a file, Windows automatically
 * decompresses it and turns it into a regular, non-externally-backed file.
 *
 * WOF provides various ioctls that control its operation.  For example,
 * FSCTL_SET_EXTERNAL_BACKING sets up a file as externally backed.
 *
 * WOF external backings are implemented using reparse points.  One consequence
 * of this is that WOF external backings can only be set on files that do not
 * already have a reparse point set.  Another consequence of this is that it is
 * possible to create a WOF external backing by manually creating the reparse
 * point, although this requires dealing with undocumented data structures and
 * it only works when the WOF driver is not currently attached to the volume.
 *
 * Note that only the unnamed data stream portion of a file can be externally
 * backed.  Other NTFS streams and metadata are not externally backed.
 */


/*----------------------------------------------------------------------------*
 *                          WOF ioctl definitions                             *
 *----------------------------------------------------------------------------*/

#ifndef WOF_CURRENT_VERSION
/* Identifies a file backing provider and the overlay service version it supports.
 * Ref: https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_wof_external_info */
typedef struct _WOF_EXTERNAL_INFO {
#define WOF_CURRENT_VERSION 1
	DWORD Version;
	DWORD Provider;
} WOF_EXTERNAL_INFO, *PWOF_EXTERNAL_INFO;
#endif /* WOF_CURRENT_VERSION */

/* WIM provider ("WIMBoot") */
#ifndef WOF_PROVIDER_WIM
#define WOF_PROVIDER_WIM 1
/*
 * The identifier and status information for the Windows Image File (WIM)
 * external backing provider.
 * Ref: https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_wim_provider_external_info
 */
typedef struct _WIM_PROVIDER_EXTERNAL_INFO {
#define WIM_PROVIDER_CURRENT_VERSION 1
	ULONG         Version;
	ULONG         Flags;
	LARGE_INTEGER DataSourceId;
#define WIM_PROVIDER_HASH_SIZE 20
	UCHAR         ResourceHash[WIM_PROVIDER_HASH_SIZE];
} WIM_PROVIDER_EXTERNAL_INFO, *PWIM_PROVIDER_EXTERNAL_INFO;
#endif /* WOF_PROVIDER_WIM */

/* File provider ("system compression") */
#ifndef WOF_PROVIDER_FILE
#define WOF_PROVIDER_FILE 2
/* Defines metadata specific to files provided by WOF_PROVIDER_FILE.
 * Ref: https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_provider_external_info_v1 */
typedef struct _FILE_PROVIDER_EXTERNAL_INFO_V1 {
#define FILE_PROVIDER_CURRENT_VERSION 1
	DWORD Version;
#define FILE_PROVIDER_COMPRESSION_XPRESS4K	0
#define FILE_PROVIDER_COMPRESSION_LZX		1
#define FILE_PROVIDER_COMPRESSION_XPRESS8K	2
#define FILE_PROVIDER_COMPRESSION_XPRESS16K	3
	DWORD Algorithm;
	DWORD Flags;
} FILE_PROVIDER_EXTERNAL_INFO_V1, *PFILE_PROVIDER_EXTERNAL_INFO_V1;
#endif /* WOF_PROVIDER_FILE */

/* Sets the backing source for a file.
 * Ref: https://docs.microsoft.com/en-us/windows-hardware/drivers/ifs/fsctl-set-external-backing */
#ifndef FSCTL_SET_EXTERNAL_BACKING
#define FSCTL_SET_EXTERNAL_BACKING \
	CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 195, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#endif

/* Gets the backing information for a file from an external backing provider.
 * Ref: https://docs.microsoft.com/en-us/windows-hardware/drivers/ifs/fsctl-get-external-backing */
#ifndef FSCTL_GET_EXTERNAL_BACKING
#define FSCTL_GET_EXTERNAL_BACKING \
	CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 196, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

#ifndef STATUS_OBJECT_NOT_EXTERNALLY_BACKED
#define STATUS_OBJECT_NOT_EXTERNALLY_BACKED	0xC000046D
#endif

/* Removes the association of a file with an external backing provider.
 * Ref: https://docs.microsoft.com/en-us/windows-hardware/drivers/ifs/fsctl-delete-external-backing */
#ifndef FSCTL_DELETE_EXTERNAL_BACKING
#define FSCTL_DELETE_EXTERNAL_BACKING \
	CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 197, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#endif

/* Begins or continues an enumeration of files on a volume that have a backing source.
 * Ref: https://docs.microsoft.com/en-us/windows-hardware/drivers/ifs/fsctl-enum-external-backing */
#ifndef FSCTL_ENUM_EXTERNAL_BACKING
#define FSCTL_ENUM_EXTERNAL_BACKING \
	CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 198, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

/* Enumerates all the data sources from a backing provider for a specified volume.
 * Ref: https://docs.microsoft.com/en-us/windows-hardware/drivers/ifs/fsctl-enum-overlay */
#ifndef FSCTL_ENUM_OVERLAY
#define FSCTL_ENUM_OVERLAY \
	CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 199, METHOD_NEITHER, FILE_ANY_ACCESS)
#endif
typedef struct _WIM_PROVIDER_OVERLAY_ENTRY {
	ULONG NextEntryOffset;
	LARGE_INTEGER DataSourceId;
	GUID WimGuid;
	ULONG WimFileNameOffset;
	ULONG WimType;
	ULONG WimIndex;
	ULONG Flags;
} WIM_PROVIDER_OVERLAY_ENTRY, *PWIM_PROVIDER_OVERLAY_ENTRY;

/* Add a new external backing source to a volume's namespace.
 * Ref: https://docs.microsoft.com/en-us/windows-hardware/drivers/ifs/fsctl-add-overlay */
#ifndef FSCTL_ADD_OVERLAY
#define FSCTL_ADD_OVERLAY \
	CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 204, METHOD_BUFFERED, FILE_WRITE_DATA)
#endif
typedef struct _WIM_PROVIDER_ADD_OVERLAY_INPUT {
#define WIM_BOOT_NOT_OS_WIM	0
#define WIM_BOOT_OS_WIM		1
	ULONG WimType;
	ULONG WimIndex;
	ULONG WimFileNameOffset;
	ULONG WimFileNameLength;
} WIM_PROVIDER_ADD_OVERLAY_INPUT, *PWIM_PROVIDER_ADD_OVERLAY_INPUT;

/* Removes a backing source from a volume.
 * Ref: https://docs.microsoft.com/en-us/windows-hardware/drivers/ifs/fsctl-remove-overlay */
#ifndef FSCTL_REMOVE_OVERLAY
#define FSCTL_REMOVE_OVERLAY \
	CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 205, METHOD_BUFFERED, FILE_WRITE_DATA)
#endif
typedef struct _WIM_PROVIDER_REMOVE_OVERLAY_INPUT {
	LARGE_INTEGER DataSourceId;
} WIM_PROVIDER_REMOVE_OVERLAY_INPUT, *PWIM_PROVIDER_REMOVE_OVERLAY_INPUT;

/* Updates a new data source identifier for a backing source attached to a volume.
 * Ref: https://docs.microsoft.com/en-us/windows-hardware/drivers/ifs/fsctl-update-overlay */
#ifndef FSCTL_UPDATE_OVERLAY
#define FSCTL_UPDATE_OVERLAY \
	CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 206, METHOD_BUFFERED, FILE_WRITE_DATA)
#endif
typedef struct _WIM_PROVIDER_UPDATE_OVERLAY_INPUT {
	LARGE_INTEGER DataSourceId;
	ULONG         WimFileNameOffset;
	ULONG         WimFileNameLength;
} WIM_PROVIDER_UPDATE_OVERLAY_INPUT, *PWIM_PROVIDER_UPDATE_OVERLAY_INPUT;

#endif

/*----------------------------------------------------------------------------*
 *        WOF reparse point and WimOverlay.dat structs (undocumented)         *
 *----------------------------------------------------------------------------*/

/*
 * Format of the WIM provider reparse data.  This is the data which follows the
 * portion of the reparse point common to WOF.  (The common portion consists of
 * a reparse point header where the reparse tag is 0x80000017, then a
 * WOF_EXTERNAL_INFO struct which specifies the provider.)
 *
 * Note that Microsoft does not document any of the reparse point formats for
 * WOF, although they document the structures which must be passed into the
 * ioctls, which are often similar.
 */
PRAGMA_BEGIN_PACKED
struct wim_provider_rpdata {
	/* Set to 2.  Uncertain meaning.  */
	le32 version;

	/* 0 when WIM provider active, otherwise
	 * WIM_PROVIDER_EXTERNAL_FLAG_NOT_ACTIVE or
	 * WIM_PROVIDER_EXTERNAL_FLAG_SUSPENDED.  */
	le32 flags;

	/* Integer ID that identifies the WIM.  */
	le64 data_source_id;

	/* SHA-1 message digest of the file's unnamed data stream.  */
	u8 unnamed_data_stream_hash[20];

	/* SHA-1 message digest of the WIM's blob table as stored on disk.  */
	u8 blob_table_hash[20];

	/* Uncompressed size of the file's unnamed data stream, in bytes.  */
	le64 unnamed_data_stream_size;

	/* Size of the file's unnamed data stream as stored in the WIM file.
	 * If this is the same as unnamed_data_stream_size, then the stream is
	 * uncompressed.  If this is the *not* the same as
	 * unnamed_data_stream_size, then the stream is compressed.  */
	le64 unnamed_data_stream_size_in_wim;

	/* Byte offset of the file's unnamed data stream in the WIM.  */
	le64 unnamed_data_stream_offset_in_wim;
} __attribute__((packed));

/* WIM-specific information about a WIM data source  */
struct WimOverlay_dat_entry_1 {

	/* Identifier for the WIM data source, (normally allocated by
	 * FSCTL_ADD_OVERLAY).  Every 'WimOverlay_dat_entry_1' should have a
	 * different value for this.  */
	le64 data_source_id;

	/* Byte offset, from the beginning of the file, of the corresponding
	 * 'struct WimOverlay_dat_entry_2' for this WIM data source.  */
	le32 entry_2_offset;

	/* Size, in bytes, of the corresponding 'struct WimOverlay_dat_entry_2
	 * for this WIM data source, including wim_file_name and its null
	 * terminator.  */
	le32 entry_2_length;

	/* Type of the WIM file: WIM_BOOT_OS_WIM or WIM_BOOT_NOT_OS_WIM.  */
	le32 wim_type;

	/* Index of the image in the WIM to use??? (This doesn't really make
	 * sense, since WIM files combine file data "blobs" for all images into
	 * a single table.  Set to 1 if unsure...)  */
	le32 wim_index;

	/* GUID of the WIM file (copied from the WIM header, offset +0x18).  */
	u8 guid[16];
} __attribute__((packed));

/*
 * Format of file: "\System Volume Information\WimOverlay.dat"
 *
 * Not documented by Microsoft.
 *
 * The file consists of a 'struct WimOverlay_dat_header' followed by one or more
 * 'struct WimOverlay_dat_entry_1', followed by the same number of 'struct
 * WimOverlay_dat_entry_2'.  Note that 'struct WimOverlay_dat_entry_1' is of
 * fixed length, whereas 'struct WimOverlay_dat_entry_2' is of variable length.
 */
struct WimOverlay_dat_header {
	/* Set to WIMOVERLAY_DAT_MAGIC  */
	le32 magic;
#define WIMOVERLAY_DAT_MAGIC 0x66436F57

	/* Set to 1 (WIM_PROVIDER_CURRENT_VERSION)  */
	le32 wim_provider_version;

	/* Set to 0x00000028  */
	le32 unknown_0x08;

	/* Set to number of WIMs registered (listed in the file)  */
	le32 num_entries;

	/* The next available data source ID.  This is tracked so that data
	 * source IDs are never reused, even if a WIM is unregistered.  */
	le64 next_data_source_id;

	struct WimOverlay_dat_entry_1 entry_1s[];
} __attribute__((packed));

/* Location information about a WIM data source  */
struct WimOverlay_dat_entry_2 {
	/* Set to 0  */
	le32 unknown_0x00;

	/* Set to 0  */
	le32 unknown_0x04;

	/* Size, in bytes, of this 'struct WimOverlay_dat_entry_2', including
	 * wim_file_name and its null terminator.  */
	le32 entry_2_length;

	/* Set to 0  */
	le32 unknown_0x0C;

	/* Set to 5  */
	le32 unknown_0x10;

	struct {
		/* Set to 1  */
		le32 unknown_0x14;

		/* Size of this inner structure, in bytes.  */
		le32 inner_struct_size;

		/* Set to 5  */
		le32 unknown_0x1C;

		/* Set to 6  */
		le32 unknown_0x20;

		/* Set to 0  */
		le32 unknown_0x24;

		/* Set to 0x48  */
		le32 unknown_0x28;

		/* Set to 0  */
		le32 unknown_0x2C;

		/*************************
		 * Partition information
		 ************************/

		/* Partition identifier  */
		union {
			/* (For MBR-formatted disks)  */
			struct {
				/* Offset, in bytes, of the MBR partition, from
				 * the beginning of the disk.  */
				le64 part_start_offset;

				/* Set to 0  */
				le64 padding;
			} mbr;

			/* (For GPT-formatted disks)  */
			struct {
				/* Unique GUID of the GPT partition  */
				u8 part_unique_guid[16];
			} gpt;
		} partition;

		/* Set to 0  */
		le32 unknown_0x40;

		/***********************
		 * Disk information
		 **********************/

		/* 1 for MBR, 0 for GPT  */
		le32 partition_table_type;
	#define WIMOVERLAY_PARTITION_TYPE_MBR 1
	#define WIMOVERLAY_PARTITION_TYPE_GPT 0

		/* Disk identifier  */
		union {
			/* (For MBR-formatted disks)  */
			struct {
				/* 4-byte ID of the MBR disk  */
				le32 disk_id;

				/* Set to 0  */
				le32 padding[3];
			} mbr;

			/* (For GPT-formatted disks)  */
			struct {
				/* GUID of the GPT disk  */
				u8 disk_guid[16];
			} gpt;
		} disk;

		/* Set to 0.  (This is the right size for some sort of optional
		 * GUID...)  */
		le32 unknown_0x58[4];

		/**************************
		 * Location in filesystem
		 *************************/

		/* Null-terminated path to WIM file.  Begins with \ but does
		 * *not* include drive letter!  */
		utf16lechar wim_file_name[];
	} __attribute__((packed));
} __attribute__((packed));
PRAGMA_END_PACKED

static void __attribute__((unused))
wof_check_structs(void)
{
	STATIC_ASSERT(sizeof(struct WimOverlay_dat_header) == 24);
	STATIC_ASSERT(sizeof(struct WimOverlay_dat_entry_1) == 40);
	STATIC_ASSERT(sizeof(struct WimOverlay_dat_entry_2) == 104);
}

#endif /* _WIN32 */

#endif /* _WOF_H_ */
