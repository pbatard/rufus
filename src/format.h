/*
 * Rufus: The Reliable USB Formatting Utility
 * Formatting function calls
 * Copyright © 2007-2009 Tom Thornhill/Ridgecrop
 * Copyright © 2011-2018 Pete Batard <pete@akeo.ie>
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
#include <windows.h>
#include <winioctl.h>				// for MEDIA_TYPE

#pragma once

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
	FCC_UNKNOWN15,
	FCC_UNKNOWN16,
	FCC_UNKNOWN17,
	FCC_DEVICE_NOT_READY,
	FCC_CHECKDISK_PROGRESS,
	FCC_UNKNOWN1A,
	FCC_UNKNOWN1B,
	FCC_UNKNOWN1C,
	FCC_UNKNOWN1D,
	FCC_UNKNOWN1E,
	FCC_UNKNOWN1F,
	FCC_READ_ONLY_MODE,
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

/* Mostly from http://doxygen.reactos.org/df/d85/fmifs_8h_source.html */
typedef LONG (WINAPI *Chkdsk_t)(
	WCHAR*               DriveRoot,
	WCHAR*               FileSystemTypeName,
	BOOL                 CorrectErrors,
	BOOL                 Verbose,
	BOOL                 CheckOnlyIfDirty,
	BOOL                 ScanDrive,
	VOID*                Unused2,
	VOID*                Unused3,
	FILE_SYSTEM_CALLBACK Callback);

/* http://msdn.microsoft.com/en-us/library/windows/desktop/aa383357.aspx */
typedef enum  {
	FPF_COMPRESSED       = 0x01 
} FILE_SYSTEM_PROP_FLAG;

typedef BOOLEAN (WINAPI* EnableVolumeCompression_t)(
	WCHAR*               DriveRoot,
	ULONG                CompressionFlags	// FILE_SYSTEM_PROP_FLAG
);

/* Large FAT32 */
#pragma pack(push, 1)
typedef struct tagFAT_BOOTSECTOR32
{
	// Common fields.
	BYTE sJmpBoot[3];
	BYTE sOEMName[8];
	WORD wBytsPerSec;
	BYTE bSecPerClus;
	WORD wRsvdSecCnt;
	BYTE bNumFATs;
	WORD wRootEntCnt;
	WORD wTotSec16;           // if zero, use dTotSec32 instead
	BYTE bMedia;
	WORD wFATSz16;
	WORD wSecPerTrk;
	WORD wNumHeads;
	DWORD dHiddSec;
	DWORD dTotSec32;
	// Fat 32/16 only
	DWORD dFATSz32;
	WORD wExtFlags;
	WORD wFSVer;
	DWORD dRootClus;
	WORD wFSInfo;
	WORD wBkBootSec;
	BYTE Reserved[12];
	BYTE bDrvNum;
	BYTE Reserved1;
	BYTE bBootSig;           // == 0x29 if next three fields are ok
	DWORD dBS_VolID;
	BYTE sVolLab[11];
	BYTE sBS_FilSysType[8];
} FAT_BOOTSECTOR32;

typedef struct {
	DWORD dLeadSig;         // 0x41615252
	BYTE sReserved1[480];   // zeros
	DWORD dStrucSig;        // 0x61417272
	DWORD dFree_Count;      // 0xFFFFFFFF
	DWORD dNxt_Free;        // 0xFFFFFFFF
	BYTE sReserved2[12];    // zeros
	DWORD dTrailSig;        // 0xAA550000
} FAT_FSINFO;
#pragma pack(pop)

#define die(msg, err) do { uprintf(msg); \
	FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|err; \
	goto out; } while(0)
