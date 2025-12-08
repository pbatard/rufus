/*
 * win32_common.h - common header for Windows-specific files.  This always
 * should be included first.
 */

#ifndef _WIMLIB_WIN32_COMMON_H
#define _WIMLIB_WIN32_COMMON_H

#define UMDF_USING_NTSTATUS
#include <ntstatus.h>
#include <windows.h>
#include <winternl.h>
#include <unistd.h>

#ifdef ERROR
#  undef ERROR
#endif
#include "wimlib/win32.h"

/* ntdll definitions */

#ifdef _MSC_VER
#define FILE_OPENED 0x00000001

#ifndef FILE_SHARE_VALID_FLAGS
#define FILE_SHARE_VALID_FLAGS 0x00000007
#endif

typedef struct _FILE_NAME_INFORMATION {
    ULONG FileNameLength;
    WCHAR FileName[1];
} FILE_NAME_INFORMATION;

typedef struct _FILE_BASIC_INFORMATION {
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    ULONG         FileAttributes;
} FILE_BASIC_INFORMATION;

typedef struct _FILE_DISPOSITION_INFORMATION {
    BOOLEAN DoDeleteFile;
} FILE_DISPOSITION_INFORMATION;

typedef struct _FILE_LINK_INFORMATION {
    union {
        BOOLEAN ReplaceIfExists;
        ULONG Flags;
    } DUMMYUNIONNAME;
    HANDLE RootDirectory;
    ULONG FileNameLength;
    WCHAR FileName[1];
} FILE_LINK_INFORMATION, *PFILE_LINK_INFORMATION;

typedef struct _FILE_ALLOCATION_INFORMATION {
    LARGE_INTEGER AllocationSize;
} FILE_ALLOCATION_INFORMATION;

typedef struct _FILE_END_OF_FILE_INFORMATION {
    LARGE_INTEGER EndOfFile;
} FILE_END_OF_FILE_INFORMATION;

typedef struct _FILE_FULL_EA_INFORMATION {
    ULONG  NextEntryOffset;
    UCHAR  Flags;
    UCHAR  EaNameLength;
    USHORT EaValueLength;
    CHAR   EaName[1];
} FILE_FULL_EA_INFORMATION;

typedef struct _FILE_INTERNAL_INFORMATION {
    LARGE_INTEGER IndexNumber;
} FILE_INTERNAL_INFORMATION;

typedef struct _FILE_NAMES_INFORMATION {
    ULONG NextEntryOffset;
    ULONG FileIndex;
    ULONG FileNameLength;
    WCHAR FileName[1];
} FILE_NAMES_INFORMATION;

typedef struct _FILE_FS_VOLUME_INFORMATION {
    LARGE_INTEGER VolumeCreationTime;
    ULONG         VolumeSerialNumber;
    ULONG         VolumeLabelLength;
    BOOLEAN       SupportsObjects;
    WCHAR         VolumeLabel[1];
} FILE_FS_VOLUME_INFORMATION;

typedef struct _FILE_STANDARD_INFORMATION {
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER EndOfFile;
    ULONG NumberOfLinks;
    BOOLEAN DeletePending;
    BOOLEAN Directory;
} FILE_STANDARD_INFORMATION;

typedef struct _FILE_EA_INFORMATION {
    ULONG EaSize;
} FILE_EA_INFORMATION;

typedef struct _FILE_ACCESS_INFORMATION {
    ACCESS_MASK AccessFlags;
} FILE_ACCESS_INFORMATION;

typedef struct _FILE_POSITION_INFORMATION {
    LARGE_INTEGER CurrentByteOffset;
} FILE_POSITION_INFORMATION;

typedef struct _FILE_MODE_INFORMATION {
    ULONG Mode;
} FILE_MODE_INFORMATION;

typedef struct _FILE_ALIGNMENT_INFORMATION {
    ULONG AlignmentRequirement;
} FILE_ALIGNMENT_INFORMATION;

typedef struct _FILE_STREAM_INFORMATION {
    ULONG         NextEntryOffset;
    ULONG         StreamNameLength;
    LARGE_INTEGER StreamSize;
    LARGE_INTEGER StreamAllocationSize;
    WCHAR         StreamName[1];
} FILE_STREAM_INFORMATION;

typedef struct _FILE_ALL_INFORMATION {
    FILE_BASIC_INFORMATION BasicInformation;
    FILE_STANDARD_INFORMATION StandardInformation;
    FILE_INTERNAL_INFORMATION InternalInformation;
    FILE_EA_INFORMATION EaInformation;
    FILE_ACCESS_INFORMATION AccessInformation;
    FILE_POSITION_INFORMATION PositionInformation;
    FILE_MODE_INFORMATION ModeInformation;
    FILE_ALIGNMENT_INFORMATION AlignmentInformation;
    FILE_NAME_INFORMATION NameInformation;
} FILE_ALL_INFORMATION;

typedef struct _FILE_FS_ATTRIBUTE_INFORMATION {
    ULONG FileSystemAttributes;
    ULONG MaximumComponentNameLength;
    ULONG FileSystemNameLength;
    WCHAR FileSystemName[1];
} FILE_FS_ATTRIBUTE_INFORMATION;

typedef enum {
    FileDirectoryInformationAlt = 1,
    FileFullDirectoryInformation = 2,
    FileBothDirectoryInformation = 3,
    FileBasicInformation = 4,
    FileStandardInformation = 5,
    FileInternalInformation = 6,
    FileEaInformation = 7,
    FileAccessInformation = 8,
    FileNameInformation = 9,
    FileRenameInformation = 10,
    FileLinkInformation = 11,
    FileNamesInformation = 12,
    FileDispositionInformation = 13,
    FilePositionInformation = 14,
    FileFullEaInformation = 15,
    FileModeInformation = 16,
    FileAlignmentInformation = 17,
    FileAllInformation = 18,
    FileAllocationInformation = 19,
    FileEndOfFileInformation = 20,
    FileAlternateNameInformation = 21,
    FileStreamInformation = 22,
    FilePipeInformation = 23,
    FilePipeLocalInformation = 24,
    FilePipeRemoteInformation = 25,
    FileMailslotQueryInformation = 26,
    FileMailslotSetInformation = 27,
    FileCompressionInformation = 28,
    FileObjectIdInformation = 29,
    FileCompletionInformation = 30,
    FileMoveClusterInformation = 31,
    FileQuotaInformation = 32,
    FileReparsePointInformation = 33,
    FileNetworkOpenInformation = 34,
    FileAttributeTagInformation = 35,
    FileTrackingInformation = 36,
    FileIdBothDirectoryInformation = 37,
    FileIdFullDirectoryInformation = 38,
    FileValidDataLengthInformation = 39,
    FileShortNameInformation = 40,
    FileIoCompletionNotificationInformation = 41,
    FileIoStatusBlockRangeInformation = 42,
    FileIoPriorityHintInformation = 43,
    FileSfioReserveInformation = 44,
    FileSfioVolumeInformation = 45,
    FileHardLinkInformation = 46,
    FileProcessIdsUsingFileInformation = 47,
    FileNormalizedNameInformation = 48,
    FileNetworkPhysicalNameInformation = 49,
    FileIdGlobalTxDirectoryInformation = 50,
    FileIsRemoteDeviceInformation = 51,
    FileUnusedInformation = 52,
    FileNumaNodeInformation = 53,
    FileStandardLinkInformation = 54,
    FileRemoteProtocolInformation = 55,
    FileRenameInformationBypassAccessCheck = 56,
    FileLinkInformationBypassAccessCheck = 57,
    FileVolumeNameInformation = 58,
    FileIdInformation = 59,
    FileIdExtdDirectoryInformation = 60,
    FileReplaceCompletionInformation = 61,
    FileHardLinkFullIdInformation = 62,
    FileIdExtdBothDirectoryInformation = 63,
    FileDispositionInformationEx = 64,
    FileRenameInformationEx = 65,
    FileRenameInformationExBypassAccessCheck = 66,
    FileDesiredStorageClassInformation = 67,
    FileStatInformation = 68,
    FileMemoryPartitionInformation = 69,
    FileStatLxInformation = 70,
    FileCaseSensitiveInformation = 71,
    FileLinkInformationEx = 72,
    FileLinkInformationExBypassAccessCheck = 73,
    FileStorageReserveIdInformation = 74,
    FileCaseSensitiveInformationForceAccessCheck = 75,
    FileKnownFolderInformation = 76,
    FileStatBasicInformation = 77,
    FileId64ExtdDirectoryInformation = 78,
    FileId64ExtdBothDirectoryInformation = 79,
    FileIdAllExtdDirectoryInformation = 80,
    FileIdAllExtdBothDirectoryInformation = 81,
    FileStreamReservationInformation,
    FileMupProviderInfo,
    FileMaximumInformation
} FILE_INFORMATION_CLASS_ALT;

typedef enum _FSINFOCLASS {
    FileFsVolumeInformation = 1,
    FileFsLabelInformation,
    FileFsSizeInformation,
    FileFsDeviceInformation,
    FileFsAttributeInformation,
    FileFsControlInformation,
    FileFsFullSizeInformation,
    FileFsObjectIdInformation,
    FileFsDriverPathInformation,
    FileFsVolumeFlagsInformation,
    FileFsMaximumInformation
} FS_INFORMATION_CLASS;
#endif

typedef struct _RTLP_CURDIR_REF {
	LONG RefCount;
	HANDLE Handle;
} RTLP_CURDIR_REF, *PRTLP_CURDIR_REF;

typedef struct _RTL_RELATIVE_NAME_U {
	UNICODE_STRING RelativeName;
	HANDLE ContainingDirectory;
	PRTLP_CURDIR_REF CurDirRef;
} RTL_RELATIVE_NAME_U, *PRTL_RELATIVE_NAME_U;

#ifndef FSCTL_SET_PERSISTENT_VOLUME_STATE
#define FSCTL_SET_PERSISTENT_VOLUME_STATE \
	CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 142, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define PERSISTENT_VOLUME_STATE_SHORT_NAME_CREATION_DISABLED 0x00000001

typedef struct _FILE_FS_PERSISTENT_VOLUME_INFORMATION {
	ULONG VolumeFlags;
	ULONG FlagMask;
	ULONG Version;
	ULONG Reserved;
} FILE_FS_PERSISTENT_VOLUME_INFORMATION, *PFILE_FS_PERSISTENT_VOLUME_INFORMATION;
#endif /* FSCTL_SET_PERSISTENT_VOLUME_STATE */

/* ntdll functions  */

NTSTATUS
NTAPI
NtReadFile(IN HANDLE FileHandle,
           IN HANDLE Event OPTIONAL,
           IN PIO_APC_ROUTINE ApcRoutine OPTIONAL,
           IN PVOID ApcContext OPTIONAL,
           OUT PIO_STATUS_BLOCK IoStatusBlock,
           OUT PVOID Buffer,
           IN ULONG Length,
           IN PLARGE_INTEGER ByteOffset OPTIONAL,
           IN PULONG Key OPTIONAL);

NTSTATUS
NTAPI
NtWriteFile(IN HANDLE FileHandle,
            IN HANDLE Event OPTIONAL,
            IN PIO_APC_ROUTINE ApcRoutine OPTIONAL,
            IN PVOID ApcContext OPTIONAL,
            OUT PIO_STATUS_BLOCK IoStatusBlock,
            IN PVOID Buffer,
            IN ULONG Length,
            IN PLARGE_INTEGER ByteOffset OPTIONAL,
            IN PULONG Key OPTIONAL);

NTSTATUS
NTAPI
NtQueryDirectoryFile(IN HANDLE FileHandle,
                     IN HANDLE EventHandle OPTIONAL,
                     IN PIO_APC_ROUTINE ApcRoutine OPTIONAL,
                     IN PVOID ApcContext OPTIONAL,
                     OUT PIO_STATUS_BLOCK IoStatusBlock,
                     OUT PVOID FileInformation,
                     IN ULONG Length,
                     IN FILE_INFORMATION_CLASS FileInformationClass,
                     IN BOOLEAN ReturnSingleEntry,
                     IN PUNICODE_STRING FileName OPTIONAL,
                     IN BOOLEAN RestartScan);

NTSTATUS
NTAPI
NtQueryInformationFile(IN HANDLE FileHandle,
                       OUT PIO_STATUS_BLOCK IoStatusBlock,
                       OUT PVOID FileInformation,
                       IN ULONG Length,
                       IN FILE_INFORMATION_CLASS FileInformationClass);

NTSTATUS
NTAPI
NtQueryVolumeInformationFile(IN HANDLE FileHandle,
                             OUT PIO_STATUS_BLOCK IoStatusBlock,
                             OUT PVOID FsInformation,
                             IN ULONG Length,
                             IN FS_INFORMATION_CLASS FsInformationClass);

NTSTATUS
NTAPI
NtQuerySecurityObject(IN HANDLE Handle,
                      IN SECURITY_INFORMATION SecurityInformation,
                      OUT PSECURITY_DESCRIPTOR SecurityDescriptor,
                      IN ULONG Length,
                      OUT PULONG ResultLength);

NTSTATUS
NTAPI
NtSetSecurityObject(IN HANDLE Handle,
                    IN SECURITY_INFORMATION SecurityInformation,
                    IN PSECURITY_DESCRIPTOR SecurityDescriptor);

NTSTATUS
NTAPI
NtSetInformationFile(IN HANDLE FileHandle,
                     OUT PIO_STATUS_BLOCK IoStatusBlock,
                     IN PVOID FileInformation,
                     IN ULONG Length,
                     IN FILE_INFORMATION_CLASS FileInformationClass);

NTSTATUS
NTAPI
NtOpenSymbolicLinkObject(PHANDLE LinkHandle,
			 ACCESS_MASK DesiredAccess,
			 POBJECT_ATTRIBUTES ObjectAttributes);

NTSTATUS
NTAPI
NtQueryEaFile(IN HANDLE FileHandle,
	      OUT PIO_STATUS_BLOCK IoStatusBlock,
	      OUT PVOID Buffer,
	      IN ULONG Length,
	      IN BOOLEAN ReturnSingleEntry,
	      IN PVOID EaList OPTIONAL,
	      IN ULONG EaListLength,
	      IN PULONG EaIndex OPTIONAL,
	      IN BOOLEAN RestartScan);

NTSTATUS
NTAPI
NtSetEaFile(IN HANDLE FileHandle,
	    OUT PIO_STATUS_BLOCK IoStatusBlock,
	    OUT PVOID Buffer,
	    IN ULONG Length);

NTSTATUS
NTAPI
NtFsControlFile(IN HANDLE FileHandle,
	    IN HANDLE Event,
	    IN PIO_APC_ROUTINE  ApcRoutine,
	    IN PVOID ApcContext,
	    OUT PIO_STATUS_BLOCK IoStatusBlock,
	    IN ULONG FsControlCode,
	    IN PVOID InputBuffer,
	    IN ULONG InputBufferLength,
	    OUT PVOID OutputBuffer,
	    IN ULONG OutputBufferLength);

/* Dynamically loaded ntdll functions */

extern NTSTATUS (WINAPI *func_RtlDosPathNameToNtPathName_U_WithStatus)
		(IN PCWSTR DosName,
		 OUT PUNICODE_STRING NtName,
		 OUT PCWSTR *PartName,
		 OUT PRTL_RELATIVE_NAME_U RelativeName);

extern NTSTATUS (WINAPI *func_RtlCreateSystemVolumeInformationFolder)
			(PCUNICODE_STRING VolumeRootPath);

/* Other utility functions */

int
win32_path_to_nt_path(const wchar_t *win32_path, UNICODE_STRING *nt_path);

int
win32_get_drive_path(const wchar_t *file_path, wchar_t drive_path[7]);

bool
win32_try_to_attach_wof(const wchar_t *drive);

void __attribute__((cold))
win32_warning(DWORD err, const wchar_t *format, ...);

void __attribute__((cold))
win32_error(DWORD err, const wchar_t *format, ...);

void __attribute__((cold))
winnt_warning(NTSTATUS status, const wchar_t *format, ...);

void __attribute__((cold))
winnt_error(NTSTATUS status, const wchar_t *format, ...);

NTSTATUS
winnt_fsctl(HANDLE h, u32 code, const void *in, u32 in_size,
	    void *out, u32 out_size_avail, u32 *actual_out_size_ret);

#endif /* _WIMLIB_WIN32_COMMON_H */
