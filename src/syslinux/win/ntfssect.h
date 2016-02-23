/* -------------------------------------------------------------------------- *
 *
 *   Copyright 2011 Shao Miller - All Rights Reserved
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 53 Temple Place Ste 330,
 *   Boston MA 02111-1307, USA; either version 2 of the License, or
 *   (at your option) any later version; incorporated herein by reference.
 *
 * ------------------------------------------------------------------------- */
#ifndef M_NTFSSECT_H_

/****
 * ntfssect.h
 *
 * Fetch NTFS file cluster & sector information via Windows
 *
 * With special thanks to Mark Roddy for his article:
 *   http://www.wd-3.com/archive/luserland.htm
 */

/*** Macros */
#define M_NTFSSECT_H_
#define M_NTFSSECT_API

/*** Object types */

/* An "extent;" a contiguous range of file data */
typedef struct S_NTFSSECT_EXTENT_ S_NTFSSECT_EXTENT;

/* Volume info relevant to file cluster & sector info */
typedef struct S_NTFSSECT_VOLINFO_ S_NTFSSECT_VOLINFO;

/* Stores function pointers to some Windows functions */
typedef struct S_NTFSSECT_XPFUNCS_ S_NTFSSECT_XPFUNCS;

/*** Function types */

/* The function type for Kernel32.dll's GetDiskFreeSpace() */
typedef BOOL WINAPI F_KERNEL32_GETDISKFREESPACE(
    LPCSTR,
    LPDWORD,
    LPDWORD,
    LPDWORD,
    LPDWORD
  );

/* The function type for Kernel32.dll's GetVolumePathName() */
typedef BOOL WINAPI F_KERNEL32_GETVOLUMEPATHNAME(LPCSTR, LPCSTR, DWORD);

/*** Function declarations */

/**
 * Fetch the extent containing a particular VCN
 *
 * @v File
 * @v Vcn
 * @v Extent
 * @ret DWORD
 */
DWORD M_NTFSSECT_API NtfsSectGetFileVcnExtent(
    HANDLE File,
    LARGE_INTEGER * Vcn,
    S_NTFSSECT_EXTENT * Extent
  );

/**
 * Populate a volume info object
 *
 * @v VolumeName
 * @v VolumeInfo
 * @ret DWORD
 */
DWORD M_NTFSSECT_API NtfsSectGetVolumeInfo(
    CHAR * VolumeName,
    S_NTFSSECT_VOLINFO * VolumeInfo
  );

/**
 * Populate a volume info object
 *
 * @v FileName
 * @v VolumeInfo
 * @ret DWORD
 */
DWORD M_NTFSSECT_API NtfsSectGetVolumeInfoFromFileName(
    CHAR * FileName,
    S_NTFSSECT_VOLINFO * VolumeInfo
  );

/**
 * Convert a volume LCN to an absolute disk LBA
 *
 * @v VolumeInfo
 * @v Lcn
 * @v Lba
 * @ret DWORD
 */
DWORD M_NTFSSECT_API NtfsSectLcnToLba(
    const S_NTFSSECT_VOLINFO * VolumeInfo,
    const LARGE_INTEGER * Lcn,
    LARGE_INTEGER * Lba
  );

/**
 * Load some helper XP functions
 *
 * @v XpFuncs
 * @ret DWORD
 */
DWORD M_NTFSSECT_API NtfsSectLoadXpFuncs(S_NTFSSECT_XPFUNCS * XpFuncs);

/**
 * Unload some helper XP functions
 *
 * @v XpFuncs
 * @ret DWORD
 */
VOID M_NTFSSECT_API NtfsSectUnloadXpFuncs(S_NTFSSECT_XPFUNCS * XpFuncs);

/*** Object declarations */

/**
 * The last error message set by one of our functions.
 * Obviously not per-thread
 */
extern CHAR * NtfsSectLastErrorMessage;

/*** Struct/union definitions */
struct S_NTFSSECT_EXTENT_ {
    LARGE_INTEGER FirstVcn;
    LARGE_INTEGER NextVcn;
    LARGE_INTEGER FirstLcn;
  };

struct S_NTFSSECT_VOLINFO_ {
    DWORD Size;
    HANDLE Handle;
    DWORD BytesPerSector;
    DWORD SectorsPerCluster;
    LARGE_INTEGER PartitionLba;
  };

struct S_NTFSSECT_XPFUNCS_ {
    DWORD Size;
    HMODULE Kernel32;
    F_KERNEL32_GETVOLUMEPATHNAME * GetVolumePathName;
    F_KERNEL32_GETDISKFREESPACE * GetDiskFreeSpace;
  };

#endif /* M_NTFSSECT_H_ */
