/*
 * Rufus: The Reliable USB Formatting Utility
 * Formatting function calls
 * Copyright (c) 2011-2012 Pete Batard <pete@akeo.ie>
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
#include <Windows.h>
#include <winioctl.h>				// for MEDIA_TYPE

#pragma once

/*
 * typedefs for the function prototypes. Use the something like:
 *   PF_DECL(FormatEx);
 * which translates to:
 *   FormatEx_t pfFormatEx = NULL;
 * in your code, to declare the entrypoint and then use:
 *   PF_INIT(FormatEx, fmifs);
 * which translates to:
 *   pfFormatEx = (FormatEx_t) GetProcAddress(GetDLLHandle("fmifs"), "FormatEx");
 * to make it accessible.
 */
static __inline HMODULE GetDLLHandle(char* szDLLName)
{
	HMODULE h = NULL;
	if ((h = GetModuleHandleA(szDLLName)) == NULL)
		h = LoadLibraryA(szDLLName);
	return h;
}
#define PF_DECL(proc) proc##_t pf##proc = NULL
#define PF_INIT(proc, dllname) pf##proc = (proc##_t) GetProcAddress(GetDLLHandle(#dllname), #proc)
#define PF_INIT_OR_OUT(proc, dllname) \
	PF_INIT(proc, dllname); if (pf##proc == NULL) { \
	uprintf("unable to access %s DLL: %s", #dllname, \
	WindowsErrorString()); goto out; }

/* Callback command types (some errorcode were filled from HPUSBFW V2.2.3 and their
   designation from msdn.microsoft.com/en-us/library/windows/desktop/aa819439.aspx */
typedef enum {
	FCC_PROGRESS,
	FCC_DONE_WITH_STRUCTURE,
	FCC_UNKNOWN2,
	FCC_INCOMPATIBLE_FILE_SYSTEM,
	FCC_UNKNOWN4,
	FCC_UNKNOWN5,
	FCC_ACCESS_DENIED,
	FCC_MEDIA_WRITE_PROTECTED,
	FCC_VOLUME_IN_USE,
	FCC_CANT_QUICK_FORMAT,
	FCC_UNKNOWNA,
	FCC_DONE,
	FCC_BAD_LABEL,
	FCC_UNKNOWND,
	FCC_OUTPUT,
	FCC_STRUCTURE_PROGRESS,
	FCC_CLUSTER_SIZE_TOO_SMALL,
	FCC_CLUSTER_SIZE_TOO_BIG,
	FCC_VOLUME_TOO_SMALL,
	FCC_VOLUME_TOO_BIG,
	FCC_NO_MEDIA_IN_DRIVE,
} FILE_SYSTEM_CALLBACK_COMMAND;

typedef struct {
	DWORD Lines;
	CHAR* Output;
} TEXTOUTPUT, *PTEXTOUTPUT;

typedef BOOLEAN (__stdcall *FILE_SYSTEM_CALLBACK)(
	FILE_SYSTEM_CALLBACK_COMMAND Command,
	ULONG                        Action,
	PVOID                        pData
);

/* Parameter names aligned to
   http://msdn.microsoft.com/en-us/library/windows/desktop/aa819439.aspx */
typedef VOID (WINAPI *FormatEx_t)(
	WCHAR*               DriveRoot,
	MEDIA_TYPE           MediaType,		// See WinIoCtl.h
	WCHAR*               FileSystemTypeName,
	WCHAR*               Label,
	BOOL                 QuickFormat,
	ULONG                DesiredUnitAllocationSize,
	FILE_SYSTEM_CALLBACK Callback
);

/* http://msdn.microsoft.com/en-us/library/windows/desktop/aa383357.aspx */
typedef enum  {
	FPF_COMPRESSED   = 0x01 
} FILE_SYSTEM_PROP_FLAG;

typedef BOOLEAN (WINAPI* EnableVolumeCompression_t)(
	WCHAR*          DriveRoot,
	ULONG           CompressionFlags	// FILE_SYSTEM_PROP_FLAG
);
