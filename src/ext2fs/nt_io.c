/*
 * nt_io.c --- This is the Nt I/O interface to the I/O manager.
 *
 * Implements a one-block write-through cache.
 *
 * Copyright (C) 1993, 1994, 1995 Theodore Ts'o.
 * Copyright (C) 1998 Andrey Shedel <andreys@ns.cr.cyco.com>
 * Copyright (C) 2018-2024 Pete Batard <pete@akeo.ie>
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Library
 * General Public License, version 2.
 * %End-Header%
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <errno.h>
#include <sys/types.h>

#include <windows.h>
#include <winternl.h>
#include <assert.h>

#include "config.h"
#include "ext2fs.h"
#include "rufus.h"
#include "msapi_utf8.h"

extern char* NtStatusError(NTSTATUS Status);
static DWORD LastWinError = 0;

PF_TYPE_DECL(NTAPI, ULONG, RtlNtStatusToDosError, (NTSTATUS));
PF_TYPE_DECL(NTAPI, NTSTATUS, NtClose, (HANDLE));
PF_TYPE_DECL(NTAPI, NTSTATUS, NtOpenFile, (PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, ULONG, ULONG));
PF_TYPE_DECL(NTAPI, NTSTATUS, NtFlushBuffersFile, (HANDLE, PIO_STATUS_BLOCK));
PF_TYPE_DECL(NTAPI, NTSTATUS, NtReadFile, (HANDLE, HANDLE, PIO_APC_ROUTINE, PVOID, PIO_STATUS_BLOCK, PVOID, ULONG, PLARGE_INTEGER, PULONG));
PF_TYPE_DECL(NTAPI, NTSTATUS, NtWriteFile, (HANDLE, HANDLE, PIO_APC_ROUTINE, PVOID, PIO_STATUS_BLOCK, PVOID, ULONG, PLARGE_INTEGER, PULONG));
PF_TYPE_DECL(NTAPI, NTSTATUS, NtDeviceIoControlFile, (HANDLE, HANDLE, PIO_APC_ROUTINE, PVOID, PIO_STATUS_BLOCK, ULONG, PVOID, ULONG, PVOID, ULONG));
PF_TYPE_DECL(NTAPI, NTSTATUS, NtFsControlFile, (HANDLE, HANDLE, PIO_APC_ROUTINE, PVOID, PIO_STATUS_BLOCK, ULONG, PVOID, ULONG, PVOID, ULONG));
PF_TYPE_DECL(NTAPI, NTSTATUS, NtDelayExecution, (BOOLEAN, PLARGE_INTEGER));

#define ARGUMENT_PRESENT(ArgumentPointer)   ((CHAR *)((ULONG_PTR)(ArgumentPointer)) != (CHAR *)(NULL))

#define STATUS_SUCCESS                      ((NTSTATUS)0x00000000L)
#define STATUS_ACCESS_DENIED                ((NTSTATUS)0xC0000022L)
#define STATUS_INVALID_DEVICE_REQUEST       ((NTSTATUS)0xC0000010L)

#define BooleanFlagOn(Flags, SingleFlag)    ((BOOLEAN)((((Flags) & (SingleFlag)) != 0)))

#define EXT2_ET_MAGIC_NT_IO_CHANNEL         0x10ed

// Private data block
typedef struct _NT_PRIVATE_DATA {
    int     magic;
    HANDLE  handle;
    int     flags;
    char*   buffer;
    __u32   buffer_block_number;
    ULONG   buffer_size;
    BOOLEAN read_only;
    BOOLEAN written;
    // Used by Rufus
    __u64   offset;
    __u64   size;
} NT_PRIVATE_DATA, *PNT_PRIVATE_DATA;

//
// Standard interface prototypes
//

static errcode_t nt_open(const char *name, int flags, io_channel *channel);
static errcode_t nt_close(io_channel channel);
static errcode_t nt_set_blksize(io_channel channel, int blksize);
static errcode_t nt_read_blk(io_channel channel, unsigned long block, int count, void *data);
static errcode_t nt_read_blk64(io_channel channel, unsigned long long block, int count, void* data);
static errcode_t nt_write_blk(io_channel channel, unsigned long block, int count, const void *data);
static errcode_t nt_write_blk64(io_channel channel, unsigned long long block, int count, const void* data);
static errcode_t nt_flush(io_channel channel);

struct struct_io_manager struct_nt_manager = {
	.magic		= EXT2_ET_MAGIC_IO_MANAGER,
	.name		= "NT I/O Manager",
	.open		= nt_open,
	.close		= nt_close,
	.set_blksize	= nt_set_blksize,
	.read_blk	= nt_read_blk,
	.read_blk64	= nt_read_blk64,
	.write_blk	= nt_write_blk,
	.write_blk64	= nt_write_blk64,
	.flush		= nt_flush
};

io_manager nt_io_manager = &struct_nt_manager;

// Convert Win32 errors to unix errno
typedef struct {
	ULONG WinError;
	int errnocode;
} ERROR_ENTRY;

static ERROR_ENTRY ErrorTable[] = {
        {  ERROR_INVALID_FUNCTION,       EINVAL    },
        {  ERROR_FILE_NOT_FOUND,         ENOENT    },
        {  ERROR_PATH_NOT_FOUND,         ENOENT    },
        {  ERROR_TOO_MANY_OPEN_FILES,    EMFILE    },
        {  ERROR_ACCESS_DENIED,          EACCES    },
        {  ERROR_INVALID_HANDLE,         EBADF     },
        {  ERROR_ARENA_TRASHED,          ENOMEM    },
        {  ERROR_NOT_ENOUGH_MEMORY,      ENOMEM    },
        {  ERROR_INVALID_BLOCK,          ENOMEM    },
        {  ERROR_BAD_ENVIRONMENT,        E2BIG     },
        {  ERROR_BAD_FORMAT,             ENOEXEC   },
        {  ERROR_INVALID_ACCESS,         EINVAL    },
        {  ERROR_INVALID_DATA,           EINVAL    },
        {  ERROR_INVALID_DRIVE,          ENOENT    },
        {  ERROR_CURRENT_DIRECTORY,      EACCES    },
        {  ERROR_NOT_SAME_DEVICE,        EXDEV     },
        {  ERROR_NO_MORE_FILES,          ENOENT    },
        {  ERROR_LOCK_VIOLATION,         EACCES    },
        {  ERROR_BAD_NETPATH,            ENOENT    },
        {  ERROR_NETWORK_ACCESS_DENIED,  EACCES    },
        {  ERROR_BAD_NET_NAME,           ENOENT    },
        {  ERROR_FILE_EXISTS,            EEXIST    },
        {  ERROR_CANNOT_MAKE,            EACCES    },
        {  ERROR_FAIL_I24,               EACCES    },
        {  ERROR_INVALID_PARAMETER,      EINVAL    },
        {  ERROR_NO_PROC_SLOTS,          EAGAIN    },
        {  ERROR_DRIVE_LOCKED,           EACCES    },
        {  ERROR_BROKEN_PIPE,            EPIPE     },
        {  ERROR_DISK_FULL,              ENOSPC    },
        {  ERROR_INVALID_TARGET_HANDLE,  EBADF     },
        {  ERROR_INVALID_HANDLE,         EINVAL    },
        {  ERROR_WAIT_NO_CHILDREN,       ECHILD    },
        {  ERROR_CHILD_NOT_COMPLETE,     ECHILD    },
        {  ERROR_DIRECT_ACCESS_HANDLE,   EBADF     },
        {  ERROR_NEGATIVE_SEEK,          EINVAL    },
        {  ERROR_SEEK_ON_DEVICE,         EACCES    },
        {  ERROR_DIR_NOT_EMPTY,          ENOTEMPTY },
        {  ERROR_NOT_LOCKED,             EACCES    },
        {  ERROR_BAD_PATHNAME,           ENOENT    },
        {  ERROR_MAX_THRDS_REACHED,      EAGAIN    },
        {  ERROR_LOCK_FAILED,            EACCES    },
        {  ERROR_ALREADY_EXISTS,         EEXIST    },
        {  ERROR_FILENAME_EXCED_RANGE,   ENOENT    },
        {  ERROR_NESTING_NOT_ALLOWED,    EAGAIN    },
        {  ERROR_NOT_ENOUGH_QUOTA,       ENOMEM    }
};

static unsigned _MapDosError(IN ULONG WinError)
{
	int i;

	LastWinError = WinError;
	for (i = 0; i < (sizeof(ErrorTable)/sizeof(ErrorTable[0])); ++i) {
		if (WinError == ErrorTable[i].WinError) {
			return ErrorTable[i].errnocode;
		}
	}

	// Not in table. Check ranges
	if ((WinError >= ERROR_WRITE_PROTECT) && (WinError <= ERROR_SHARING_BUFFER_EXCEEDED))
		return EACCES;
	else if ((WinError >= ERROR_INVALID_STARTING_CODESEG) && (WinError <= ERROR_INFLOOP_IN_RELOC_CHAIN))
		return ENOEXEC;
	else
		return EINVAL;
}

// Map NT status to dos error.
static __inline unsigned _MapNtStatus(IN NTSTATUS Status)
{
	PF_INIT(RtlNtStatusToDosError, Ntdll);
	return (pfRtlNtStatusToDosError == NULL) ? EFAULT: _MapDosError(pfRtlNtStatusToDosError(Status));
}

// Return the last Windows Error
DWORD ext2_last_winerror(DWORD default_error)
{
	return RUFUS_ERROR(LastWinError ? LastWinError : default_error);
}

//
// Helper functions
//
static NTSTATUS _OpenNtName(IN PCSTR Name, IN BOOLEAN Readonly, OUT PHANDLE Handle, OUT PBOOLEAN OpenedReadonly OPTIONAL)
{
	UNICODE_STRING UnicodeString;
	WCHAR Buffer[512];
	NTSTATUS Status = EFAULT;
	OBJECT_ATTRIBUTES ObjectAttributes;
	IO_STATUS_BLOCK IoStatusBlock;
	PF_INIT(NtDelayExecution, Ntdll);
	PF_INIT_OR_OUT(NtOpenFile, Ntdll);

	// Make Unicode name from input string
	utf8_to_wchar_no_alloc(Name, Buffer, ARRAYSIZE(Buffer));
	UnicodeString.Buffer = Buffer;
	UnicodeString.Length = (USHORT) wcslen(Buffer) * 2;
	UnicodeString.MaximumLength = sizeof(Buffer); // in bytes!!!

	// Initialize object
	InitializeObjectAttributes(&ObjectAttributes, &UnicodeString, OBJ_CASE_INSENSITIVE, NULL, NULL);

	// Try to open it in initial mode
	if (ARGUMENT_PRESENT(OpenedReadonly))
		*OpenedReadonly = Readonly;

	Status = pfNtOpenFile(Handle, SYNCHRONIZE | FILE_READ_DATA | (Readonly ? 0 : FILE_WRITE_DATA),
			      &ObjectAttributes, &IoStatusBlock, FILE_SHARE_WRITE | FILE_SHARE_READ,
			      FILE_SYNCHRONOUS_IO_NONALERT);
	if (!NT_SUCCESS(Status)) {
		// Maybe was just mounted? wait 0.5 sec and retry.
		LARGE_INTEGER Interval;
		Interval.QuadPart = -5000000; // 0.5 sec. from now
		pfNtDelayExecution(FALSE, &Interval);

		Status = pfNtOpenFile(Handle, SYNCHRONIZE | FILE_READ_DATA | (Readonly ? 0 : FILE_WRITE_DATA),
				      &ObjectAttributes, &IoStatusBlock, FILE_SHARE_WRITE | FILE_SHARE_READ,
				      FILE_SYNCHRONOUS_IO_NONALERT);

		// Try to satisfy mode
		if ((Status == STATUS_ACCESS_DENIED) && !Readonly) {
			if (ARGUMENT_PRESENT(OpenedReadonly))
				*OpenedReadonly = TRUE;

			Status = pfNtOpenFile(Handle, SYNCHRONIZE | FILE_READ_DATA, &ObjectAttributes,
					      &IoStatusBlock, FILE_SHARE_WRITE | FILE_SHARE_READ,
					      FILE_SYNCHRONOUS_IO_NONALERT);
		}
	}

out:
	return Status;
}

static NTSTATUS _OpenDriveLetter(IN CHAR Letter, IN BOOLEAN ReadOnly, OUT PHANDLE Handle, OUT PBOOLEAN OpenedReadonly OPTIONAL)
{
	CHAR Buffer[100];
	sprintf(Buffer, "\\DosDevices\\%c:", Letter);
	return _OpenNtName(Buffer, ReadOnly, Handle, OpenedReadonly);
}

static __inline NTSTATUS _FlushDrive(IN HANDLE Handle)
{
	IO_STATUS_BLOCK IoStatusBlock;
	PF_INIT(NtFlushBuffersFile, NtDll);
	return (pfNtFlushBuffersFile == NULL) ? STATUS_DLL_NOT_FOUND : pfNtFlushBuffersFile(Handle, &IoStatusBlock);
}


static __inline NTSTATUS _LockDrive(IN HANDLE Handle)
{
	IO_STATUS_BLOCK IoStatusBlock;
	PF_INIT(NtFsControlFile, NtDll);
	return (pfNtFsControlFile == NULL) ? STATUS_DLL_NOT_FOUND : pfNtFsControlFile(Handle, 0, 0, 0, &IoStatusBlock, FSCTL_LOCK_VOLUME, 0, 0, 0, 0);
}


static __inline NTSTATUS _UnlockDrive(IN HANDLE Handle)
{
	IO_STATUS_BLOCK IoStatusBlock;
	PF_INIT(NtFsControlFile, NtDll);
	return (pfNtFsControlFile == NULL) ? STATUS_DLL_NOT_FOUND : pfNtFsControlFile(Handle, 0, 0, 0, &IoStatusBlock, FSCTL_UNLOCK_VOLUME, 0, 0, 0, 0);
}

static __inline NTSTATUS _DismountDrive(IN HANDLE Handle)
{
	IO_STATUS_BLOCK IoStatusBlock;
	PF_INIT(NtFsControlFile, NtDll);
	return (pfNtFsControlFile == NULL) ? STATUS_DLL_NOT_FOUND : pfNtFsControlFile(Handle, 0, 0, 0, &IoStatusBlock, FSCTL_DISMOUNT_VOLUME, 0, 0, 0, 0);
}

static __inline BOOLEAN _IsMounted(IN HANDLE Handle)
{
	IO_STATUS_BLOCK IoStatusBlock;
	PF_INIT(NtFsControlFile, NtDll);
	return (pfNtFsControlFile == NULL) ? FALSE :
		(BOOLEAN)(pfNtFsControlFile(Handle, 0, 0, 0, &IoStatusBlock, FSCTL_IS_VOLUME_MOUNTED, 0, 0, 0, 0) == STATUS_SUCCESS);
}

static __inline NTSTATUS _CloseDisk(IN HANDLE Handle)
{
	PF_INIT(NtClose, Ntdll);
	return (pfNtClose == NULL) ? STATUS_DLL_NOT_FOUND : pfNtClose(Handle);
}

static PCSTR _NormalizeDeviceName(IN PCSTR Device, IN PSTR NormalizedDeviceNameBuffer, OUT __u64 *Offset, OUT __u64 *Size)
{
	*Offset = *Size = 0ULL;
	// Convert non NT paths to NT
	if (Device[0] == '\\') {
		if ((strlen(Device) < 4) || (Device[3] != '\\'))
			return Device;
		// Handle custom paths of the form "<Physical> <Offset> <Size>" used by Rufus to
		// enable multi-partition access on removable devices, for pre 1703 platforms.
		if (sscanf(Device, "%s %I64u %I64u", NormalizedDeviceNameBuffer, Offset, Size) < 1)
			return NULL;
		if ((NormalizedDeviceNameBuffer[1] == '\\') || (NormalizedDeviceNameBuffer[1] == '.'))
			NormalizedDeviceNameBuffer[1] = '?';
		if (NormalizedDeviceNameBuffer[2] == '.')
			NormalizedDeviceNameBuffer[2] = '?';
		return NormalizedDeviceNameBuffer;
	}

	// Don't allow the conversion of non absolute paths.
	// Too easy to get a C:\ drive altered on a mishap otherwise...
	return NULL;
}

static VOID _GetDeviceSize(IN HANDLE h, OUT unsigned __int64 *FsSize)
{
	PARTITION_INFORMATION_EX pi;
	DISK_GEOMETRY_EX gi;
	NTSTATUS Status;
	IO_STATUS_BLOCK IoStatusBlock;
	LARGE_INTEGER li;

	*FsSize = 0;
	PF_INIT(NtDeviceIoControlFile, NtDll);
	if (pfNtDeviceIoControlFile == NULL)
		return;

	RtlZeroMemory(&pi, sizeof(pi));
	Status = pfNtDeviceIoControlFile(h, NULL, NULL, NULL, &IoStatusBlock,
					 IOCTL_DISK_GET_PARTITION_INFO_EX,
					 &pi, sizeof(pi), &pi, sizeof(pi));
	if (NT_SUCCESS(Status)) {
		*FsSize = pi.PartitionLength.QuadPart;
	} else if (Status == STATUS_INVALID_DEVICE_REQUEST) {
		// No partitions: Try a drive geometry request
		RtlZeroMemory(&gi, sizeof(gi));

		Status = pfNtDeviceIoControlFile(h, NULL, NULL, NULL, &IoStatusBlock,
						 IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
						 &gi, sizeof(gi), &gi, sizeof(gi));

		if (NT_SUCCESS(Status))
			*FsSize = gi.DiskSize.QuadPart;
	} else if (Status == STATUS_INVALID_PARAMETER) {
		// Possibly a straight image file
		if (GetFileSizeEx(h, &li))
			*FsSize = li.QuadPart;
	}
}

static BOOLEAN _Ext2OpenDevice(IN PCSTR Name, IN BOOLEAN ReadOnly, OUT PHANDLE Handle,
	OUT __u64 *Offset, OUT __u64 *Size, OUT PBOOLEAN OpenedReadonly OPTIONAL, OUT errcode_t *Errno OPTIONAL)
{
	CHAR NormalizedDeviceName[512];
	NTSTATUS Status;

	LastWinError = 0;
	if (Name == NULL) {
		LastWinError = ERROR_INVALID_PARAMETER;
		if (ARGUMENT_PRESENT(Errno))
			*Errno = ENOENT;
		return FALSE;
	}

	if ((((*Name) | 0x20) >= 'a') && (((*Name) | 0x20) <= 'z') &&
		(':' == *(Name + 1)) && ('\0' == *(Name + 2))) {
		Status = _OpenDriveLetter(*Name, ReadOnly, Handle, OpenedReadonly);
	} else {
		Name = _NormalizeDeviceName(Name, NormalizedDeviceName, Offset, Size);
		if (Name == NULL) {
			LastWinError = ERROR_INVALID_PARAMETER;
			if (ARGUMENT_PRESENT(Errno))
				*Errno = ENOENT;
			return FALSE;
		}

		Status = _OpenNtName(Name, ReadOnly, Handle, OpenedReadonly);
	}

	if (!NT_SUCCESS(Status)) {
		if (ARGUMENT_PRESENT(Errno))
			*Errno = _MapNtStatus(Status);
		return FALSE;
	}

	return TRUE;
}

static BOOLEAN _BlockIo(IN HANDLE Handle, IN LARGE_INTEGER Offset, IN ULONG Bytes, IN OUT PCHAR Buffer, IN BOOLEAN Read, OUT errcode_t *Errno OPTIONAL)
{
	IO_STATUS_BLOCK IoStatusBlock;
	NTSTATUS Status = STATUS_DLL_NOT_FOUND;
	PF_INIT_OR_OUT(NtReadFile, NtDll);
	PF_INIT_OR_OUT(NtWriteFile, NtDll);

	// Should be aligned
	assert((Bytes % 512) == 0);
	assert((Offset.LowPart % 512) == 0);

	LastWinError = 0;
	// Perform io
	if(Read) {
		Status = pfNtReadFile(Handle, NULL, NULL, NULL,
			&IoStatusBlock, Buffer, Bytes, &Offset, NULL);
	} else	{
		Status = pfNtWriteFile(Handle, NULL, NULL, NULL,
			&IoStatusBlock, Buffer, Bytes, &Offset, NULL);
	}

out:
	if (!NT_SUCCESS(Status)) {
		if (ARGUMENT_PRESENT(Errno))
			*Errno = _MapNtStatus(Status);
		return FALSE;
	}

	if (ARGUMENT_PRESENT(Errno))
		*Errno = 0;
	return TRUE;
}

static BOOLEAN _RawWrite(IN HANDLE Handle, IN LARGE_INTEGER Offset, IN ULONG Bytes, OUT const CHAR* Buffer, OUT errcode_t* Errno)
{
	return _BlockIo(Handle, Offset, Bytes, (PCHAR)Buffer, FALSE, Errno);
}

static BOOLEAN _RawRead(IN HANDLE Handle, IN LARGE_INTEGER Offset, IN ULONG Bytes, IN PCHAR Buffer, OUT errcode_t* Errno)
{
	return _BlockIo(Handle, Offset, Bytes, Buffer, TRUE, Errno);
}

static BOOLEAN _SetPartType(IN HANDLE Handle, IN UCHAR Type)
{
	IO_STATUS_BLOCK IoStatusBlock;
	PF_INIT(NtDeviceIoControlFile, NtDll);
	if (pfNtDeviceIoControlFile == NULL)
		return FALSE;
	return NT_SUCCESS(pfNtDeviceIoControlFile(Handle, NULL, NULL, NULL, &IoStatusBlock,
						  IOCTL_DISK_SET_PARTITION_INFO, &Type, sizeof(Type), NULL, 0));
}

//
// Interface functions.
// Is_mounted is set to 1 if the device is mounted, 0 otherwise
//
errcode_t ext2fs_check_if_mounted(const char *file, int *mount_flags)
{
	errcode_t errcode = 0;
	__u64 Offset, Size;
	HANDLE h;
	BOOLEAN Readonly;

	*mount_flags = 0;

	if (!_Ext2OpenDevice(file, TRUE, &h, &Offset, &Size, &Readonly, &errcode))
		return errcode;

	*mount_flags &= _IsMounted(h) ? EXT2_MF_MOUNTED : 0;
	_CloseDisk(h);

	return 0;
}

// Not implemented
errcode_t ext2fs_check_mount_point(const char *file, int *mount_flags, char *mtpt, int mtlen)
{
	return EXT2_ET_OP_NOT_SUPPORTED;
}

// Returns the number of blocks in a partition
// Note: Do *NOT* be tempted to cache the device size according to the NT path as
// different removable devices (e.g. UFD) may be remounted under the same path.
errcode_t ext2fs_get_device_size2(const char *file, int blocksize, blk64_t *retblocks)
{
	errcode_t errcode = 0;
	__u64 Offset, Size = 0;
	HANDLE h;
	BOOLEAN Readonly;

	if (!_Ext2OpenDevice(file, TRUE, &h, &Offset, &Size, &Readonly, &errcode))
		return errcode;

	if (Size == 0LL)
		_GetDeviceSize(h, &Size);
	_CloseDisk(h);

	*retblocks = (blk64_t)(Size / blocksize);
	return 0;
}


//
// Table elements
//
static errcode_t nt_open(const char *name, int flags, io_channel *channel)
{
	io_channel io = NULL;
	PNT_PRIVATE_DATA nt_data = NULL;
	errcode_t errcode = 0;

	if (name == NULL)
		return EXT2_ET_BAD_DEVICE_NAME;

	// Allocate buffers
	io = (io_channel) calloc(1, sizeof(struct struct_io_channel));
	if (io == NULL) {
		errcode = ENOMEM;
		goto out;
	}

	io->name = calloc(strlen(name) + 1, 1);
	if (io->name == NULL) {
		errcode = ENOMEM;
		goto out;
	}

	nt_data = (PNT_PRIVATE_DATA) calloc(1, sizeof(NT_PRIVATE_DATA));
	if (nt_data == NULL) {
		errcode = ENOMEM;
		goto out;
	}

	nt_data->buffer = malloc(EXT2_MIN_BLOCK_SIZE);
	if (nt_data->buffer == NULL) {
		errcode = ENOMEM;
		goto out;
	}

	// Initialize data
	io->magic = EXT2_ET_MAGIC_IO_CHANNEL;
	io->manager = nt_io_manager;
	strcpy(io->name, name);
	io->block_size = EXT2_MIN_BLOCK_SIZE;
	io->refcount = 1;

	nt_data->magic = EXT2_ET_MAGIC_NT_IO_CHANNEL;
	nt_data->buffer_block_number = 0xffffffff;
	nt_data->buffer_size = EXT2_MIN_BLOCK_SIZE;
	io->private_data = nt_data;

	// Open the device
	if (!_Ext2OpenDevice(name, (BOOLEAN)!BooleanFlagOn(flags, EXT2_FLAG_RW), &nt_data->handle,
		&nt_data->offset, &nt_data->size, &nt_data->read_only, &errcode)) {
		if (!errcode)
			errcode = EIO;
		goto out;
	}

	// Done
	*channel = io;

out:
	if (errcode) {
		if (io != NULL) {
			free(io->name);
			free(io);
		}

		if (nt_data != NULL) {
			if (nt_data->handle != NULL) {
				_UnlockDrive(nt_data->handle);
				_CloseDisk(nt_data->handle);
			}
			free(nt_data->buffer);
			free(nt_data);
		}
	}

	return errcode;
}

static errcode_t nt_close(io_channel channel)
{
	PNT_PRIVATE_DATA nt_data = NULL;

	if (channel == NULL)
		return 0;

	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	nt_data = (PNT_PRIVATE_DATA) channel->private_data;
	EXT2_CHECK_MAGIC(nt_data, EXT2_ET_MAGIC_NT_IO_CHANNEL);

	if (--channel->refcount > 0)
		return 0;

	free(channel->name);
	free(channel);

	if (nt_data != NULL) {
		if (nt_data->handle != NULL)
			CloseHandle(nt_data->handle);
		free(nt_data->buffer);
		free(nt_data);
	}

	return 0;
}

static errcode_t nt_set_blksize(io_channel channel, int blksize)
{
	PNT_PRIVATE_DATA nt_data = NULL;

	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	nt_data = (PNT_PRIVATE_DATA) channel->private_data;
	EXT2_CHECK_MAGIC(nt_data, EXT2_ET_MAGIC_NT_IO_CHANNEL);

	if (channel->block_size != blksize) {
		channel->block_size = blksize;

		free(nt_data->buffer);
		nt_data->buffer_block_number = 0xffffffff;
		nt_data->buffer_size = channel->block_size;
		assert((nt_data->buffer_size % 512) == 0);

		nt_data->buffer = malloc(nt_data->buffer_size);
		if (nt_data->buffer == NULL)
			return ENOMEM;
	}

	return 0;
}

static errcode_t nt_read_blk64(io_channel channel, unsigned long long block, int count, void *buf)
{
	PVOID read_buffer;
	ULONG read_size;
	ULONG size;
	LARGE_INTEGER offset;
	PNT_PRIVATE_DATA nt_data = NULL;
	errcode_t errcode = 0;

	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	nt_data = (PNT_PRIVATE_DATA) channel->private_data;
	EXT2_CHECK_MAGIC(nt_data, EXT2_ET_MAGIC_NT_IO_CHANNEL);

	// If it's in the cache, use it!
	if ((count == 1) && (block == nt_data->buffer_block_number) &&
	    (nt_data->buffer_block_number != 0xffffffff)) {
		memcpy(buf, nt_data->buffer, channel->block_size);
		return 0;
	}

	size = (count < 0) ? (ULONG)(-count) : (ULONG)(count * channel->block_size);

	offset.QuadPart = block * channel->block_size + nt_data->offset;

	// If not fit to the block
	if (size <= nt_data->buffer_size) {
		// Update the cache
		nt_data->buffer_block_number = block;
		read_buffer = nt_data->buffer;
		read_size = nt_data->buffer_size;
	} else {
		read_size = size;
		read_buffer = buf;
		assert((read_size % channel->block_size) == 0);
	}

	if (!_RawRead(nt_data->handle, offset, read_size, read_buffer, &errcode)) {
		if (channel->read_error)
			return (channel->read_error)(channel, block, count, buf, size, 0, errcode);
		else
			return errcode;
	}

	if (read_buffer != buf) {
		assert(size <= read_size);
		memcpy(buf, read_buffer, size);
	}

	return 0;
}

static errcode_t nt_read_blk(io_channel channel, unsigned long block, int count, void* buf)
{
	return nt_read_blk64(channel, block, count, buf);
}

static errcode_t nt_write_blk64(io_channel channel, unsigned long long block, int count, const void *buf)
{
	ULONG write_size;
	LARGE_INTEGER offset;
	PNT_PRIVATE_DATA nt_data = NULL;
	errcode_t errcode = 0;

	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	nt_data = (PNT_PRIVATE_DATA) channel->private_data;
	EXT2_CHECK_MAGIC(nt_data, EXT2_ET_MAGIC_NT_IO_CHANNEL);

	if (nt_data->read_only)
		return EACCES;

	if (count == 1) {
		write_size = channel->block_size;
	} else {
		nt_data->buffer_block_number = 0xffffffff;
		if (count < 0)
			write_size = (ULONG)(-count);
		else
			write_size = (ULONG)(count * channel->block_size);
	}


	assert((write_size % 512) == 0);
	offset.QuadPart = block * channel->block_size + nt_data->offset;

	if (!_RawWrite(nt_data->handle, offset, write_size, buf, &errcode)) {
		if (channel->write_error)
			return (channel->write_error)(channel, block, count, buf, write_size, 0, errcode);
		else
			return errcode;
	}


	// Stash a copy.
	if(write_size >= nt_data->buffer_size) {
		nt_data->buffer_block_number = block;
		memcpy(nt_data->buffer, buf, nt_data->buffer_size);
	}

	nt_data->written = TRUE;

	return 0;
}

static errcode_t nt_write_blk(io_channel channel, unsigned long block, int count, const void* buf)
{
	return nt_write_blk64(channel, block, count, buf);
}

static errcode_t nt_flush(io_channel channel)
{
	PNT_PRIVATE_DATA nt_data = NULL;

	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	nt_data = (PNT_PRIVATE_DATA) channel->private_data;
	EXT2_CHECK_MAGIC(nt_data, EXT2_ET_MAGIC_NT_IO_CHANNEL);

	if(nt_data->read_only)
		return 0;


	// Flush file buffers.
	_FlushDrive(nt_data->handle);


	// Test and correct partition type.
	if (nt_data->written)
		_SetPartType(nt_data->handle, 0x83);

	return 0;
}
