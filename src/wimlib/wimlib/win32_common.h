/*
 * win32_common.h - common header for Windows-specific files.  This always
 * should be included first.
 */

#ifndef _WIMLIB_WIN32_COMMON_H
#define _WIMLIB_WIN32_COMMON_H

#include <ntstatus.h>
#include <windows.h>
#include <winternl.h>

#ifdef ERROR
#  undef ERROR
#endif
#include "wimlib/win32.h"

/* ntdll definitions */

#define FILE_OPENED 0x00000001

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
