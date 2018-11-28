/*
 * Rufus: The Reliable USB Formatting Utility
 * Drive access function calls
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
#include <stdint.h>
#include <winioctl.h>   // for DISK_GEOMETRY
#include <winternl.h>

#pragma once

#define RUFUS_EXTRA_PARTITION_TYPE          0xea
#define MOUNTMGRCONTROLTYPE                 ((ULONG)'m')
#define MOUNTMGR_DOS_DEVICE_NAME            "\\\\.\\MountPointManager"
#define IOCTL_MOUNTMGR_QUERY_AUTO_MOUNT     \
	CTL_CODE(MOUNTMGRCONTROLTYPE, 15, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MOUNTMGR_SET_AUTO_MOUNT       \
	CTL_CODE(MOUNTMGRCONTROLTYPE, 16, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define XP_MSR                              0x01
#define XP_EFI                              0x02
#define XP_UEFI_NTFS                        0x04
#define XP_COMPAT                           0x08

#define FILE_FLOPPY_DISKETTE                0x00000004

#if !defined(__MINGW32__)
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
} FS_INFORMATION_CLASS, *PFS_INFORMATION_CLASS;
#endif

/* We need a redef of these MS structure */
typedef struct {
	DWORD DeviceType;
	ULONG DeviceNumber;
	ULONG PartitionNumber;
} STORAGE_DEVICE_NUMBER_REDEF;

typedef struct {
	DWORD NumberOfDiskExtents;
	// The one from MS uses ANYSIZE_ARRAY, which can lead to all kind of problems
	DISK_EXTENT Extents[8];
} VOLUME_DISK_EXTENTS_REDEF;

#if !defined(__MINGW32__)
typedef struct _FILE_FS_DEVICE_INFORMATION {
	DEVICE_TYPE DeviceType;
	ULONG Characteristics;
} FILE_FS_DEVICE_INFORMATION, *PFILE_FS_DEVICE_INFORMATION;
#else
/* MinGW is currently missing all the VDS COM stuff */
#include <vds.h>
typedef interface IVdsServiceLoader IVdsServiceLoader;
typedef interface IVdsService IVdsService;
typedef interface IVdsProvider IVdsProvider;
typedef interface IVdsSwProvider IVdsSwProvider;
typedef interface IEnumVdsObject IEnumVdsObject;
typedef interface IVdsPack IVdsPack;
typedef interface IVdsDisk IVdsDisk;
typedef interface IVdsAdvancedDisk IVdsAdvancedDisk;
typedef interface IVdsAdviseSink IVdsAdviseSink;
typedef interface IVdsAsync IVdsAsync;

typedef struct IVdsServiceLoaderVtbl {
	HRESULT(STDMETHODCALLTYPE *QueryInterface)(__RPC__in IVdsServiceLoader *This, __RPC__in REFIID riid, _COM_Outptr_ void **ppvObject);
	ULONG(STDMETHODCALLTYPE *AddRef)(__RPC__in IVdsServiceLoader *This);
	ULONG(STDMETHODCALLTYPE *Release)(__RPC__in IVdsServiceLoader *This);
	HRESULT(STDMETHODCALLTYPE *LoadService)(__RPC__in IVdsServiceLoader *This, __RPC__in_opt_string LPWSTR pwszMachineName, __RPC__deref_out_opt IVdsService **ppService);
} IVdsServiceLoaderVtbl;
interface IVdsServiceLoader {
	CONST_VTBL struct IVdsServiceLoaderVtbl *lpVtbl;
};

typedef struct IVdsServiceVtbl {
	HRESULT(STDMETHODCALLTYPE *QueryInterface)(__RPC__in IVdsService *This, __RPC__in REFIID riid, _COM_Outptr_ void **ppvObject);
	ULONG(STDMETHODCALLTYPE *AddRef)(__RPC__in IVdsService *This);
	ULONG(STDMETHODCALLTYPE *Release)(__RPC__in IVdsService *This);
	HRESULT(STDMETHODCALLTYPE *IsServiceReady)(__RPC__in IVdsService *This);
	HRESULT(STDMETHODCALLTYPE *WaitForServiceReady)(__RPC__in IVdsService *This);
	HRESULT(STDMETHODCALLTYPE *GetProperties)(__RPC__in IVdsService *This, __RPC__out VDS_SERVICE_PROP *pServiceProp);
	HRESULT(STDMETHODCALLTYPE *QueryProviders)(__RPC__in IVdsService *This, DWORD masks, __RPC__deref_out_opt IEnumVdsObject **ppEnum);
	HRESULT(STDMETHODCALLTYPE *QueryMaskedDisks)(__RPC__in IVdsService *This, __RPC__deref_out_opt IEnumVdsObject **ppEnum);
	HRESULT(STDMETHODCALLTYPE *QueryUnallocatedDisks)(__RPC__in IVdsService *This, __RPC__deref_out_opt IEnumVdsObject **ppEnum);
	HRESULT(STDMETHODCALLTYPE *GetObject)(__RPC__in IVdsService *This, VDS_OBJECT_ID ObjectId, VDS_OBJECT_TYPE type, __RPC__deref_out_opt IUnknown **ppObjectUnk);
	HRESULT(STDMETHODCALLTYPE *QueryDriveLetters)(__RPC__in IVdsService *This, WCHAR wcFirstLetter, DWORD count, __RPC__out_ecount_full(count) VDS_DRIVE_LETTER_PROP *pDriveLetterPropArray);
	HRESULT(STDMETHODCALLTYPE *QueryFileSystemTypes)(__RPC__in IVdsService *This, __RPC__deref_out_ecount_full_opt(*plNumberOfFileSystems) VDS_FILE_SYSTEM_TYPE_PROP **ppFileSystemTypeProps, __RPC__out LONG *plNumberOfFileSystems);
	HRESULT(STDMETHODCALLTYPE *Reenumerate)(__RPC__in IVdsService *This);
	HRESULT(STDMETHODCALLTYPE *Refresh)(__RPC__in IVdsService *This);
	HRESULT(STDMETHODCALLTYPE *CleanupObsoleteMountPoints)(__RPC__in IVdsService *This);
	HRESULT(STDMETHODCALLTYPE *Advise)(__RPC__in IVdsService *This, __RPC__in_opt IVdsAdviseSink *pSink, __RPC__out DWORD *pdwCookie);
	HRESULT(STDMETHODCALLTYPE *Unadvise)(__RPC__in IVdsService *This, DWORD dwCookie);
	HRESULT(STDMETHODCALLTYPE *Reboot)(__RPC__in IVdsService *This);
	HRESULT(STDMETHODCALLTYPE *SetFlags)(__RPC__in IVdsService *This, ULONG ulFlags);
	HRESULT(STDMETHODCALLTYPE *ClearFlags)(__RPC__in IVdsService *This, ULONG ulFlags);
} IVdsServiceVtbl;
interface IVdsService {
	CONST_VTBL struct IVdsServiceVtbl *lpVtbl;
};

typedef struct IEnumVdsObjectVtbl {
	HRESULT(STDMETHODCALLTYPE *QueryInterface)(__RPC__in IEnumVdsObject *This, __RPC__in REFIID riid, _COM_Outptr_ void **ppvObject);
	ULONG(STDMETHODCALLTYPE *AddRef)(__RPC__in IEnumVdsObject *This);
	ULONG(STDMETHODCALLTYPE *Release)(__RPC__in IEnumVdsObject *This);
	HRESULT(STDMETHODCALLTYPE *Next)(__RPC__in IEnumVdsObject *This, ULONG celt, __RPC__out_ecount_part(celt, *pcFetched) IUnknown **ppObjectArray, __RPC__out ULONG *pcFetched);
	HRESULT(STDMETHODCALLTYPE *Skip)(__RPC__in IEnumVdsObject *This, ULONG celt);
	HRESULT(STDMETHODCALLTYPE *Reset)(__RPC__in IEnumVdsObject *This);
	HRESULT(STDMETHODCALLTYPE *Clone)(__RPC__in IEnumVdsObject *This, __RPC__deref_out_opt IEnumVdsObject **ppEnum);
} IEnumVdsObjectVtbl;
interface IEnumVdsObject {
	CONST_VTBL struct IEnumVdsObjectVtbl *lpVtbl;
};

typedef struct IVdsProviderVtbl {
	HRESULT(STDMETHODCALLTYPE *QueryInterface)(__RPC__in IVdsProvider *This, __RPC__in REFIID riid, _COM_Outptr_ void **ppvObject);
	ULONG(STDMETHODCALLTYPE *AddRef)(__RPC__in IVdsProvider *This);
	ULONG(STDMETHODCALLTYPE *Release)(__RPC__in IVdsProvider *This);
	HRESULT(STDMETHODCALLTYPE *GetProperties)(__RPC__in IVdsProvider *This, __RPC__out VDS_PROVIDER_PROP *pProviderProp);
} IVdsProviderVtbl;
interface IVdsProvider {
	CONST_VTBL struct IVdsProviderVtbl *lpVtbl;
};

typedef struct IVdsSwProviderVtbl {
	HRESULT(STDMETHODCALLTYPE *QueryInterface)(__RPC__in IVdsSwProvider *This, __RPC__in REFIID riid, _COM_Outptr_ void **ppvObject);
	ULONG(STDMETHODCALLTYPE *AddRef)(__RPC__in IVdsSwProvider *This);
	ULONG(STDMETHODCALLTYPE *Release)(__RPC__in IVdsSwProvider *This);
	HRESULT(STDMETHODCALLTYPE *QueryPacks)(__RPC__in IVdsSwProvider *This, __RPC__deref_out_opt IEnumVdsObject **ppEnum);
	HRESULT(STDMETHODCALLTYPE *CreatePack)(__RPC__in IVdsSwProvider *This, __RPC__deref_out_opt IVdsPack **ppPack);
} IVdsSwProviderVtbl;
interface IVdsSwProvider {
	CONST_VTBL struct IVdsSwProviderVtbl *lpVtbl;
};

typedef struct IVdsPackVtbl {
	HRESULT(STDMETHODCALLTYPE *QueryInterface)(__RPC__in IVdsPack *This, __RPC__in REFIID riid, _COM_Outptr_ void **ppvObject);
	ULONG(STDMETHODCALLTYPE *AddRef)(__RPC__in IVdsPack *This);
	ULONG(STDMETHODCALLTYPE *Release)(__RPC__in IVdsPack *This);
	HRESULT(STDMETHODCALLTYPE *GetProperties)(__RPC__in IVdsPack *This, __RPC__out VDS_PACK_PROP *pPackProp);
	HRESULT(STDMETHODCALLTYPE *GetProvider)(__RPC__in IVdsPack *This, __RPC__deref_out_opt IVdsProvider **ppProvider);
	HRESULT(STDMETHODCALLTYPE *QueryVolumes)(__RPC__in IVdsPack *This, __RPC__deref_out_opt IEnumVdsObject **ppEnum);
	HRESULT(STDMETHODCALLTYPE *QueryDisks)(__RPC__in IVdsPack *This, __RPC__deref_out_opt IEnumVdsObject **ppEnum);
	HRESULT(STDMETHODCALLTYPE *CreateVolume)(__RPC__in IVdsPack *This, VDS_VOLUME_TYPE type, __RPC__in_ecount_full(lNumberOfDisks) VDS_INPUT_DISK *pInputDiskArray, LONG lNumberOfDisks, ULONG ulStripeSize, __RPC__deref_out_opt IVdsAsync **ppAsync);
	HRESULT(STDMETHODCALLTYPE *AddDisk)(__RPC__in IVdsPack *This, VDS_OBJECT_ID DiskId, VDS_PARTITION_STYLE PartitionStyle, BOOL bAsHotSpare);
	HRESULT(STDMETHODCALLTYPE *MigrateDisks)(__RPC__in IVdsPack *This, __RPC__in_ecount_full(lNumberOfDisks) VDS_OBJECT_ID *pDiskArray, LONG lNumberOfDisks, VDS_OBJECT_ID TargetPack, BOOL bForce, BOOL bQueryOnly, __RPC__out_ecount_full(lNumberOfDisks) HRESULT *pResults, __RPC__out BOOL *pbRebootNeeded);
	HRESULT(STDMETHODCALLTYPE *ReplaceDisk)(__RPC__in IVdsPack *This, VDS_OBJECT_ID OldDiskId, VDS_OBJECT_ID NewDiskId, __RPC__deref_out_opt IVdsAsync **ppAsync);
	HRESULT(STDMETHODCALLTYPE *RemoveMissingDisk)(__RPC__in IVdsPack *This, VDS_OBJECT_ID DiskId);
	HRESULT(STDMETHODCALLTYPE *Recover)(__RPC__in IVdsPack *This, __RPC__deref_out_opt IVdsAsync **ppAsync);
} IVdsPackVtbl;
interface IVdsPack {
	CONST_VTBL struct IVdsPackVtbl *lpVtbl;
};

typedef struct IVdsDiskVtbl {
	HRESULT(STDMETHODCALLTYPE *QueryInterface)(__RPC__in IVdsDisk *This, __RPC__in REFIID riid, _COM_Outptr_ void **ppvObject);
	ULONG(STDMETHODCALLTYPE *AddRef)(__RPC__in IVdsDisk *This);
	ULONG(STDMETHODCALLTYPE *Release)(__RPC__in IVdsDisk *This);
	HRESULT(STDMETHODCALLTYPE *GetProperties)(__RPC__in IVdsDisk *This, __RPC__out VDS_DISK_PROP *pDiskProperties);
	HRESULT(STDMETHODCALLTYPE *GetPack)(__RPC__in IVdsDisk *This, __RPC__deref_out_opt IVdsPack **ppPack);
	HRESULT(STDMETHODCALLTYPE *GetIdentificationData)(__RPC__in IVdsDisk *This, __RPC__out VDS_LUN_INFORMATION *pLunInfo);
	HRESULT(STDMETHODCALLTYPE *QueryExtents)(__RPC__in IVdsDisk *This, __RPC__deref_out_ecount_full_opt(*plNumberOfExtents) VDS_DISK_EXTENT **ppExtentArray, __RPC__out LONG *plNumberOfExtents);
	HRESULT(STDMETHODCALLTYPE *ConvertStyle)(__RPC__in IVdsDisk *This, VDS_PARTITION_STYLE NewStyle);
	HRESULT(STDMETHODCALLTYPE *SetFlags)(__RPC__in IVdsDisk *This, ULONG ulFlags);
	HRESULT(STDMETHODCALLTYPE *ClearFlags)(__RPC__in IVdsDisk *This, ULONG ulFlags);
} IVdsDiskVtbl;
interface IVdsDisk {
	CONST_VTBL struct IVdsDiskVtbl *lpVtbl;
};

typedef struct IVdsAdvancedDiskVtbl {
	HRESULT(STDMETHODCALLTYPE *QueryInterface)(__RPC__in IVdsAdvancedDisk *This, __RPC__in REFIID riid, _COM_Outptr_ void **ppvObject);
	ULONG(STDMETHODCALLTYPE *AddRef)(__RPC__in IVdsAdvancedDisk *This);
	ULONG(STDMETHODCALLTYPE *Release)(__RPC__in IVdsAdvancedDisk *This);
	HRESULT(STDMETHODCALLTYPE *GetPartitionProperties)(__RPC__in IVdsAdvancedDisk *This, ULONGLONG ullOffset, __RPC__out VDS_PARTITION_PROP *pPartitionProp);
	HRESULT(STDMETHODCALLTYPE *QueryPartitions)(__RPC__in IVdsAdvancedDisk *This, __RPC__deref_out_ecount_full_opt(*plNumberOfPartitions) VDS_PARTITION_PROP **ppPartitionPropArray, __RPC__out LONG *plNumberOfPartitions);
	HRESULT(STDMETHODCALLTYPE *CreatePartition)(__RPC__in IVdsAdvancedDisk *This, ULONGLONG ullOffset, ULONGLONG ullSize, __RPC__in CREATE_PARTITION_PARAMETERS *para, __RPC__deref_out_opt IVdsAsync **ppAsync);
	HRESULT(STDMETHODCALLTYPE *DeletePartition)(__RPC__in IVdsAdvancedDisk *This, ULONGLONG ullOffset, BOOL bForce, BOOL bForceProtected);
	HRESULT(STDMETHODCALLTYPE *ChangeAttributes)(__RPC__in IVdsAdvancedDisk *This, ULONGLONG ullOffset, __RPC__in CHANGE_ATTRIBUTES_PARAMETERS *para);
	HRESULT(STDMETHODCALLTYPE *AssignDriveLetter)(__RPC__in IVdsAdvancedDisk *This, ULONGLONG ullOffset, WCHAR wcLetter);
	HRESULT(STDMETHODCALLTYPE *DeleteDriveLetter)(__RPC__in IVdsAdvancedDisk *This, ULONGLONG ullOffset, WCHAR wcLetter);
	HRESULT(STDMETHODCALLTYPE *GetDriveLetter)(__RPC__in IVdsAdvancedDisk *This, ULONGLONG ullOffset, __RPC__out WCHAR *pwcLetter);
	HRESULT(STDMETHODCALLTYPE *FormatPartition)(__RPC__in IVdsAdvancedDisk *This, ULONGLONG ullOffset, VDS_FILE_SYSTEM_TYPE type, __RPC__in_string LPWSTR pwszLabel, DWORD dwUnitAllocationSize, BOOL bForce, BOOL bQuickFormat, BOOL bEnableCompression, __RPC__deref_out_opt IVdsAsync **ppAsync);
	HRESULT(STDMETHODCALLTYPE *Clean)(__RPC__in IVdsAdvancedDisk *This, BOOL bForce, BOOL bForceOEM, BOOL bFullClean, __RPC__deref_out_opt IVdsAsync **ppAsync);
} IVdsAdvancedDiskVtbl;
interface IVdsAdvancedDisk {
	CONST_VTBL struct IVdsAdvancedDiskVtbl *lpVtbl;
};

#define IVdsServiceLoader_LoadService(This,pwszMachineName,ppService) (This)->lpVtbl->LoadService(This,pwszMachineName,ppService)
#define IVdsServiceLoader_Release(This) (This)->lpVtbl->Release(This)
#define IVdsService_QueryProviders(This,masks,ppEnum) (This)->lpVtbl->QueryProviders(This,masks,ppEnum)
#define IVdsSwProvider_QueryInterface(This,riid,ppvObject) (This)->lpVtbl->QueryInterface(This,riid,ppvObject)
#define IVdsProvider_Release(This) (This)->lpVtbl->Release(This)
#define IVdsSwProvider_QueryPacks(This,ppEnum) (This)->lpVtbl->QueryPacks(This,ppEnum)
#define IVdsSwProvider_Release(This) (This)->lpVtbl->Release(This)
#define IVdsPack_QueryDisks(This,ppEnum) (This)->lpVtbl->QueryDisks(This,ppEnum)
#define IVdsDisk_GetProperties(This,pDiskProperties) (This)->lpVtbl->GetProperties(This,pDiskProperties)
#define IVdsDisk_Release(This) (This)->lpVtbl->Release(This)
#define IVdsDisk_QueryInterface(This,riid,ppvObject) (This)->lpVtbl->QueryInterface(This,riid,ppvObject)
#define IVdsAdvancedDisk_QueryPartitions(This,ppPartitionPropArray,plNumberOfPartitions) (This)->lpVtbl->QueryPartitions(This,ppPartitionPropArray,plNumberOfPartitions)
#define IVdsAdvancedDisk_DeletePartition(This,ullOffset,bForce,bForceProtected) (This)->lpVtbl->DeletePartition(This,ullOffset,bForce,bForceProtected)
#define IVdsAdvancedDisk_Release(This) (This)->lpVtbl->Release(This)
#define IEnumVdsObject_Next(This,celt,ppObjectArray,pcFetched) (This)->lpVtbl->Next(This,celt,ppObjectArray,pcFetched)
#endif

static __inline BOOL UnlockDrive(HANDLE hDrive) {
	DWORD size;
	return DeviceIoControl(hDrive, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &size, NULL);
}
#define safe_unlockclose(h) do {if ((h != INVALID_HANDLE_VALUE) && (h != NULL)) {UnlockDrive(h); CloseHandle(h); h = INVALID_HANDLE_VALUE;}} while(0)

/* Current drive info */
typedef struct {
	LONGLONG DiskSize;
	DWORD DeviceNumber;
	DWORD SectorsPerTrack;
	DWORD SectorSize;
	DWORD FirstDataSector;
	MEDIA_TYPE MediaType;
	int PartitionStyle;
	int nPartitions;	// number of partitions we actually care about
	int FSType;
	char proposed_label[16];
	BOOL has_protective_mbr;
	BOOL has_mbr_uefi_marker;
	struct {
		ULONG Allowed;
		ULONG Default;
	} ClusterSize[FS_MAX];
} RUFUS_DRIVE_INFO;
extern RUFUS_DRIVE_INFO SelectedDrive;

BOOL SetAutoMount(BOOL enable);
BOOL GetAutoMount(BOOL* enabled);
char* GetPhysicalName(DWORD DriveIndex);
BOOL DeletePartitions(DWORD DriveIndex);
HANDLE GetPhysicalHandle(DWORD DriveIndex, BOOL bLockDrive, BOOL bWriteAccess, BOOL bWriteShare);
char* GetLogicalName(DWORD DriveIndex, BOOL bKeepTrailingBackslash, BOOL bSilent);
BOOL WaitForLogical(DWORD DriveIndex);
HANDLE GetLogicalHandle(DWORD DriveIndex, BOOL bLockDrive, BOOL bWriteAccess, BOOL bWriteShare);
int GetDriveNumber(HANDLE hDrive, char* path);
BOOL GetDriveLetters(DWORD DriveIndex, char* drive_letters);
UINT GetDriveTypeFromIndex(DWORD DriveIndex);
char GetUnusedDriveLetter(void);
BOOL GetDriveLabel(DWORD DriveIndex, char* letter, char** label);
uint64_t GetDriveSize(DWORD DriveIndex);
BOOL IsMediaPresent(DWORD DriveIndex);
BOOL AnalyzeMBR(HANDLE hPhysicalDrive, const char* TargetName, BOOL bSilent);
BOOL AnalyzePBR(HANDLE hLogicalVolume);
BOOL GetDrivePartitionData(DWORD DriveIndex, char* FileSystemName, DWORD FileSystemNameSize, BOOL bSilent);
BOOL UnmountVolume(HANDLE hDrive);
BOOL MountVolume(char* drive_name, char *drive_guid);
BOOL AltUnmountVolume(const char* drive_name);
char* AltMountVolume(const char* drive_name, uint8_t part_nr);
BOOL RemountVolume(char* drive_name);
BOOL CreatePartition(HANDLE hDrive, int partition_style, int file_system, BOOL mbr_uefi_marker, uint8_t extra_partitions);
BOOL InitializeDisk(HANDLE hDrive);
BOOL RefreshDriveLayout(HANDLE hDrive);
const char* GetPartitionType(BYTE Type);
