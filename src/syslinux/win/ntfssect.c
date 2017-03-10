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

/****
 * ntfssect.c
 *
 * Fetch NTFS file cluster & sector information via Windows
 *
 * With special thanks to Mark Roddy for his article:
 *   http://www.wd-3.com/archive/luserland.htm
 */

#include <windows.h>
#include <winioctl.h>
#include <stddef.h>
#include <string.h>

#include "ntfssect.h"

/*** Macros */
#define M_ERR(msg) (NtfsSectLastErrorMessage = (msg))

/*** Function declarations */
static DWORD NtfsSectGetVolumeHandle(
    CHAR * VolumeName,
    S_NTFSSECT_VOLINFO * VolumeInfo
  );
static DWORD NtfsSectGetVolumePartitionLba(S_NTFSSECT_VOLINFO * VolumeInfo);

/*** Objects */
CHAR * NtfsSectLastErrorMessage;

/*** Function definitions */
DWORD M_NTFSSECT_API NtfsSectGetFileVcnExtent(
    HANDLE File,
    LARGE_INTEGER * Vcn,
    S_NTFSSECT_EXTENT * Extent
  ) {
    BOOL bad;
    DWORD output_size, rc;
    STARTING_VCN_INPUT_BUFFER input;
    RETRIEVAL_POINTERS_BUFFER output;

    bad = (
        File == INVALID_HANDLE_VALUE ||
        !Vcn ||
        Vcn->QuadPart < 0 ||
        !Extent
      );
    if (bad)
      return ERROR_INVALID_PARAMETER;

    input.StartingVcn = *Vcn;
    DeviceIoControl(
        File,
        FSCTL_GET_RETRIEVAL_POINTERS,
        &input,
        sizeof input,
        &output,
        sizeof output,
        &output_size,
        NULL
      );
    rc = GetLastError();
    switch (rc) {
        case NO_ERROR:
        case ERROR_MORE_DATA:
          Extent->FirstVcn = output.StartingVcn;
          Extent->NextVcn = output.Extents[0].NextVcn;
          Extent->FirstLcn = output.Extents[0].Lcn;
          return ERROR_SUCCESS;

        case ERROR_HANDLE_EOF:
          break;

        default:
          M_ERR("NtfsSectGetFileVcnExtent(): Unknown status!");
      }

    return rc;
  }

/* Internal use only */
static DWORD NtfsSectGetVolumeHandle(
    CHAR * VolumeName,
    S_NTFSSECT_VOLINFO * VolumeInfo
  ) {
    #define M_VOL_PREFIX "\\\\.\\"
    CHAR volname[sizeof M_VOL_PREFIX - 1 + MAX_PATH + 1] = M_VOL_PREFIX;
    CHAR * const volname_short = volname + sizeof M_VOL_PREFIX - 1;
    CHAR * c;
    DWORD rc;

    /* Prefix "\\.\" onto the passed volume name */
    strcpy(volname + sizeof M_VOL_PREFIX - 1, VolumeName);

    /* Find the last non-null character */
    for (c = volname_short; *c; ++c)
      ;

    /* Remove trailing back-slash */
    if (c[-1] == '\\')
      c[-1] = 0;

    /* Open the volume */
    VolumeInfo->Handle = CreateFileA(
        volname,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
      );
    rc = GetLastError();
    if (VolumeInfo->Handle == INVALID_HANDLE_VALUE) {
        M_ERR("Unable to open volume handle!");
        goto err_handle;
      }

    return ERROR_SUCCESS;

    CloseHandle(VolumeInfo->Handle);
    err_handle:

    return rc;
  }

DWORD M_NTFSSECT_API NtfsSectGetVolumeInfo(
    CHAR * VolumeName,
    S_NTFSSECT_VOLINFO * VolumeInfo
  ) {
    S_NTFSSECT_XPFUNCS xp_funcs;
    DWORD rc, free_clusts, total_clusts;
    BOOL ok;

    if (!VolumeName || !VolumeInfo)
      return ERROR_INVALID_PARAMETER;

    /* Only create a handle if it's not already been set */
    if ((VolumeInfo->Handle == NULL) || (VolumeInfo->Handle == INVALID_HANDLE_VALUE)) {
      rc = NtfsSectGetVolumeHandle(VolumeName, VolumeInfo);
      if (rc != ERROR_SUCCESS)
        goto err_handle;
      if ((VolumeInfo->Handle == NULL) || (VolumeInfo->Handle == INVALID_HANDLE_VALUE))
        return ERROR_INVALID_HANDLE;
    }

    rc = NtfsSectLoadXpFuncs(&xp_funcs);
    if (rc != ERROR_SUCCESS)
      goto err_xp_funcs;

    ok = xp_funcs.GetDiskFreeSpace(
        VolumeName,
        &VolumeInfo->SectorsPerCluster,
        &VolumeInfo->BytesPerSector,
        &free_clusts,
        &total_clusts
      );
    rc = GetLastError();
    if (!ok) {
        M_ERR("GetDiskFreeSpace() failed!");
        goto err_freespace;
      }

    rc = NtfsSectGetVolumePartitionLba(VolumeInfo);
    if (rc != ERROR_SUCCESS)
      goto err_lba;

    VolumeInfo->Size = sizeof *VolumeInfo;
    rc = ERROR_SUCCESS;

    err_lba:

    err_freespace:

    NtfsSectUnloadXpFuncs(&xp_funcs);
    err_xp_funcs:

    if (rc != ERROR_SUCCESS) {
        CloseHandle(VolumeInfo->Handle);
        VolumeInfo->Handle = INVALID_HANDLE_VALUE;
      }
    err_handle:

    return rc;
  }

DWORD M_NTFSSECT_API NtfsSectGetVolumeInfoFromFileName(
    CHAR * FileName,
    S_NTFSSECT_VOLINFO * VolumeInfo
  ) {
    S_NTFSSECT_XPFUNCS xp_funcs;
    DWORD rc;
    CHAR volname[MAX_PATH + 1];
    BOOL ok;

    if (!FileName || !VolumeInfo)
      return ERROR_INVALID_PARAMETER;

    rc = NtfsSectLoadXpFuncs(&xp_funcs);
    if (rc != ERROR_SUCCESS) {
        goto err_xp_funcs;
      }

    ok = xp_funcs.GetVolumePathName(
        FileName,
        volname,
        sizeof volname
      );
    rc = GetLastError();
    if (!ok) {
        M_ERR("GetVolumePathName() failed!");
        goto err_volname;
      }

    rc = NtfsSectGetVolumeInfo(volname, VolumeInfo);

    err_volname:

    NtfsSectUnloadXpFuncs(&xp_funcs);
    err_xp_funcs:

    return rc;
  }

/* Internal use only */
static DWORD NtfsSectGetVolumePartitionLba(S_NTFSSECT_VOLINFO * VolumeInfo) {
    BOOL ok;
    VOLUME_DISK_EXTENTS vol_disk_extents;
    DWORD output_size, rc;

    ok = DeviceIoControl(
        VolumeInfo->Handle,
        IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
        NULL,
        0,
        &vol_disk_extents,
        sizeof vol_disk_extents,
        &output_size,
        NULL
      );
    rc = GetLastError();
    if (!ok) {
        M_ERR("Couldn't fetch volume disk extent(s)!");
        goto err_vol_disk_extents;
      }

    if (vol_disk_extents.NumberOfDiskExtents != 1) {
        M_ERR("Unsupported number of volume disk extents!");
        goto err_num_of_extents;
      }

    VolumeInfo->PartitionLba.QuadPart = (
        vol_disk_extents.Extents[0].StartingOffset.QuadPart /
        VolumeInfo->BytesPerSector
      );

    return ERROR_SUCCESS;

    err_num_of_extents:

    err_vol_disk_extents:

    return rc;
  }

DWORD M_NTFSSECT_API NtfsSectLcnToLba(
    const S_NTFSSECT_VOLINFO * VolumeInfo,
    const LARGE_INTEGER * Lcn,
    LARGE_INTEGER * Lba
  ) {
    BOOL bad;
    bad = (
        !VolumeInfo ||
        !VolumeInfo->BytesPerSector ||
        !VolumeInfo->SectorsPerCluster ||
        !Lcn ||
        Lcn->QuadPart < 0 ||
        !Lba
      );
    if (bad)
      return ERROR_INVALID_PARAMETER;

    Lba->QuadPart = (
        VolumeInfo->PartitionLba.QuadPart +
        Lcn->QuadPart *
        VolumeInfo->SectorsPerCluster
      );
    return ERROR_SUCCESS;
  }

DWORD M_NTFSSECT_API NtfsSectLoadXpFuncs(S_NTFSSECT_XPFUNCS * XpFuncs) {
    DWORD rc;

    if (!XpFuncs)
      return ERROR_INVALID_PARAMETER;

    XpFuncs->Size = sizeof *XpFuncs;

    XpFuncs->Kernel32 = LoadLibraryA("kernel32.dll");
    rc = GetLastError();
    if (!XpFuncs->Kernel32) {
        M_ERR("KERNEL32.DLL not found!");
        goto err;
      }

    XpFuncs->GetVolumePathName = (F_KERNEL32_GETVOLUMEPATHNAME *) (
        GetProcAddress(
            XpFuncs->Kernel32,
            "GetVolumePathNameA"
          )
      );
    rc = GetLastError();
    if (!XpFuncs->GetVolumePathName) {
        M_ERR("GetVolumePathName() not found in KERNEL32.DLL!");
        goto err;
      }

    XpFuncs->GetDiskFreeSpace = (F_KERNEL32_GETDISKFREESPACE *) (
        GetProcAddress(
            XpFuncs->Kernel32,
            "GetDiskFreeSpaceA"
          )
      );
    rc = GetLastError();
    if (!XpFuncs->GetDiskFreeSpace) {
        M_ERR("GetDiskFreeSpace() not found in KERNEL32.DLL!");
        goto err;
      }

    return ERROR_SUCCESS;

    err:
    NtfsSectUnloadXpFuncs(XpFuncs);
    return rc;
  }

VOID M_NTFSSECT_API NtfsSectUnloadXpFuncs(S_NTFSSECT_XPFUNCS * XpFuncs) {
    if (!XpFuncs)
      return;

    XpFuncs->GetDiskFreeSpace = NULL;
    XpFuncs->GetVolumePathName = NULL;
    if (XpFuncs->Kernel32)
      FreeLibrary(XpFuncs->Kernel32);
    XpFuncs->Kernel32 = NULL;
    XpFuncs->Size = 0;
    return;
  }

