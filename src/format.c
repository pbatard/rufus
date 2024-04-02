/*
 * Rufus: The Reliable USB Formatting Utility
 * Formatting function calls
 * Copyright © 2011-2024 Pete Batard <pete@akeo.ie>
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
#include <windowsx.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <process.h>
#include <stddef.h>
#include <ctype.h>
#include <locale.h>
#include <assert.h>
#if !defined(__MINGW32__)
#include <vds.h>
#endif

#include "rufus.h"
#include "format.h"
#include "missing.h"
#include "resource.h"
#include "settings.h"
#include "winio.h"
#include "msapi_utf8.h"
#include "localization.h"

#include "br.h"
#include "vhd.h"
#include "wue.h"
#include "fat16.h"
#include "fat32.h"
#include "ntfs.h"
#include "partition_info.h"
#include "file.h"
#include "drive.h"
#include "format.h"
#include "badblocks.h"
#include "bled/bled.h"
#include "../res/grub/grub_version.h"

/* Numbers of buffer used for asynchronous DD reads */
#define NUM_BUFFERS 2

/*
 * Globals
 */
const char* FileSystemLabel[FS_MAX] = { "FAT", "FAT32", "NTFS", "UDF", "exFAT", "ReFS", "ext2", "ext3", "ext4" };
DWORD ErrorStatus = 0, LastWriteError = 0;
badblocks_report report = { 0 };
static float format_percent = 0.0f;
static int task_number = 0, actual_fs_type;
static unsigned int sec_buf_pos = 0;
extern const int nb_steps[FS_MAX];
extern const char* md5sum_name[2];
extern uint32_t dur_mins, dur_secs;
extern uint32_t wim_nb_files, wim_proc_files, wim_extra_files;
extern BOOL force_large_fat32, enable_ntfs_compression, lock_drive, zero_drive, fast_zeroing, enable_file_indexing;
extern BOOL write_as_image, use_vds, write_as_esp, is_vds_available, has_ffu_support, use_rufus_mbr;
extern char* archive_path;
uint8_t *grub2_buf = NULL, *sec_buf = NULL;
long grub2_len;

/*
 * Convert the fmifs outputs messages (that use an OEM code page) to UTF-8
 */
static void OutputUTF8Message(const char* src)
{
	int len;
	wchar_t* wdst = NULL;

	if (src == NULL)
		goto out;
	len = (int)safe_strlen(src);
	while ((len > 0) && ((src[len-1] == 0x0A) || (src[len-1] == 0x0D) || (src[len-1] == ' ')))
		len--;
	if (len == 0)
		goto out;

	len = MultiByteToWideChar(CP_OEMCP, 0, src, len, NULL, 0);
	if (len == 0)
		goto out;
	wdst = (wchar_t*)calloc(len+1, sizeof(wchar_t));
	if ((wdst == NULL) || (MultiByteToWideChar(CP_OEMCP, 0, src, len, wdst, len+1) == 0))
		goto out;
	uprintf("%S", wdst);

out:
	safe_free(wdst);
}

/*
 * FormatEx callback. Return FALSE to halt operations
 */
static BOOLEAN __stdcall FormatExCallback(FILE_SYSTEM_CALLBACK_COMMAND Command, DWORD Action, PVOID pData)
{
	char percent_str[8];
	if (IS_ERROR(ErrorStatus))
		return FALSE;

	assert((actual_fs_type >= 0) && (actual_fs_type < FS_MAX));
	if ((actual_fs_type < 0) || (actual_fs_type >= FS_MAX))
		return FALSE;

	switch(Command) {
	case FCC_PROGRESS:
		static_sprintf(percent_str, "%lu%%", *((DWORD*)pData));
		PrintInfo(0, MSG_217, percent_str);
		UpdateProgress(OP_FORMAT, 1.0f * (*((DWORD*)pData)));
		break;
	case FCC_STRUCTURE_PROGRESS:	// No progress on quick format
		if (task_number < nb_steps[actual_fs_type] - 1) {
			if (task_number == 0)
				uprintf("Creating file system...");
			PrintInfo(0, MSG_218, ++task_number, nb_steps[actual_fs_type]);
			format_percent += 100.0f / (1.0f * nb_steps[actual_fs_type]);
			UpdateProgress(OP_CREATE_FS, format_percent);
		}
		break;
	case FCC_DONE:
		PrintInfo(0, MSG_218, nb_steps[actual_fs_type], nb_steps[actual_fs_type]);
		UpdateProgress(OP_CREATE_FS, 100.0f);
		if(*(BOOLEAN*)pData == FALSE) {
			uprintf("Error while formatting");
			ErrorStatus = RUFUS_ERROR(ERROR_GEN_FAILURE);
		}
		break;
	case FCC_DONE_WITH_STRUCTURE:
		break;
	case FCC_INCOMPATIBLE_FILE_SYSTEM:
		uprintf("Incompatible File System");
		ErrorStatus = RUFUS_ERROR(APPERR(ERROR_INCOMPATIBLE_FS));
		break;
	case FCC_ACCESS_DENIED:
		uprintf("Access denied");
		ErrorStatus = RUFUS_ERROR(ERROR_ACCESS_DENIED);
		break;
	case FCC_MEDIA_WRITE_PROTECTED:
		uprintf("Media is write protected");
		ErrorStatus = RUFUS_ERROR(ERROR_WRITE_PROTECT);
		break;
	case FCC_VOLUME_IN_USE:
		uprintf("Volume is in use");
		ErrorStatus = RUFUS_ERROR(ERROR_DEVICE_IN_USE);
		break;
	case FCC_DEVICE_NOT_READY:
		uprintf("The device is not ready");
		ErrorStatus = RUFUS_ERROR(ERROR_NOT_READY);
		break;
	case FCC_CANT_QUICK_FORMAT:
		uprintf("Cannot quick format this volume");
		ErrorStatus = RUFUS_ERROR(APPERR(ERROR_CANT_QUICK_FORMAT));
		break;
	case FCC_BAD_LABEL:
		uprintf("Bad label");
		ErrorStatus = RUFUS_ERROR(ERROR_LABEL_TOO_LONG);
		break;
	case FCC_OUTPUT:
		OutputUTF8Message(((PTEXTOUTPUT)pData)->Output);
		break;
	case FCC_CLUSTER_SIZE_TOO_BIG:
	case FCC_CLUSTER_SIZE_TOO_SMALL:
		uprintf("Unsupported cluster size");
		ErrorStatus = RUFUS_ERROR(APPERR(ERROR_INVALID_CLUSTER_SIZE));
		break;
	case FCC_VOLUME_TOO_BIG:
	case FCC_VOLUME_TOO_SMALL:
		uprintf("Volume is too %s", (Command == FCC_VOLUME_TOO_BIG) ? "big" : "small");
		ErrorStatus = RUFUS_ERROR(APPERR(ERROR_INVALID_VOLUME_SIZE));
		break;
	case FCC_NO_MEDIA_IN_DRIVE:
		uprintf("No media in drive");
		ErrorStatus = RUFUS_ERROR(ERROR_NO_MEDIA_IN_DRIVE);
		break;
	case FCC_ALIGNMENT_VIOLATION:
		uprintf("Partition start offset is not aligned to the cluster size");
		ErrorStatus = RUFUS_ERROR(ERROR_OFFSET_ALIGNMENT_VIOLATION);
		break;
	default:
		uprintf("FormatExCallback: Received unhandled command 0x%02X - aborting", Command);
		ErrorStatus = RUFUS_ERROR(ERROR_NOT_SUPPORTED);
		break;
	}
	return (!IS_ERROR(ErrorStatus));
}

/*
 * Chkdsk callback. Return FALSE to halt operations
 */
static BOOLEAN __stdcall ChkdskCallback(FILE_SYSTEM_CALLBACK_COMMAND Command, DWORD Action, PVOID pData)
{
	DWORD* percent;
	if (IS_ERROR(ErrorStatus))
		return FALSE;

	switch (Command) {
	case FCC_PROGRESS:
	case FCC_CHECKDISK_PROGRESS:
		percent = (DWORD*)pData;
		PrintInfo(0, MSG_219, *percent);
		break;
	case FCC_DONE:
		if (*(BOOLEAN*)pData == FALSE) {
			uprintf("Error while checking disk");
			return FALSE;
		}
		break;
	case FCC_UNKNOWN1A:
	case FCC_DONE_WITH_STRUCTURE:
		// Silence these specific calls
		break;
	case FCC_INCOMPATIBLE_FILE_SYSTEM:
		uprintf("Incompatible File System");
		return FALSE;
	case FCC_ACCESS_DENIED:
		uprintf("Access denied");
		return FALSE;
	case FCC_MEDIA_WRITE_PROTECTED:
		uprintf("Media is write protected");
		return FALSE;
	case FCC_VOLUME_IN_USE:
		uprintf("Volume is in use");
		return FALSE;
	case FCC_OUTPUT:
		OutputUTF8Message(((PTEXTOUTPUT)pData)->Output);
		break;
	case FCC_NO_MEDIA_IN_DRIVE:
		uprintf("No media in drive");
		return FALSE;
	case FCC_READ_ONLY_MODE:
		uprintf("Media has been switched to read-only - Leaving checkdisk");
		break;
	default:
		uprintf("ChkdskExCallback: received unhandled command %X", Command);
		// Assume the command isn't an error
		break;
	}
	return TRUE;
}

/*
 * Converts an UTF-8 label to a valid FAT/NTFS one
 * TODO: Use IVdsService::QueryFileSystemTypes -> VDS_FILE_SYSTEM_TYPE_PROP
 * to get the list of unauthorised and max length for each FS.
 */
static void ToValidLabel(char* Label, BOOL bFAT)
{
	size_t i, j, k;
	BOOL found;
	const WCHAR unauthorized[] = L"*?,;:/\\|+=<>[]\"";
	const WCHAR to_underscore[] = L"\t.";
	char label[16] = { 0 };
	WCHAR *wLabel = utf8_to_wchar(Label);

	if (wLabel == NULL)
		return;

	for (i = 0, k = 0; i < wcslen(wLabel); i++) {
		if (bFAT) {	// NTFS does allows all the FAT unauthorized above
			found = FALSE;
			for (j = 0; j < wcslen(unauthorized); j++) {
				if (wLabel[i] == unauthorized[j]) {
					found = TRUE;
					break;
				}
			}
			// A FAT label that contains extended chars will be rejected
			if (wLabel[i] >= 0x80) {
				wLabel[k++] = L'_';
				found = TRUE;
			}
			if (found)
				continue;
		}
		found = FALSE;
		for (j = 0; j < wcslen(to_underscore); j++) {
			if (wLabel[i] == to_underscore[j]) {
				wLabel[k++] = '_';
				found = TRUE;
				break;
			}
		}
		if (found)
			continue;
		wLabel[k++] = bFAT ? toupper(wLabel[i]) : wLabel[i];
	}
	wLabel[k] = 0;

	if (bFAT) {
		if (wcslen(wLabel) > 11)
			wLabel[11] = 0;
		for (i = 0, j = 0; wLabel[i] != 0 ; i++)
			if (wLabel[i] == '_')
				j++;
		if (i < 2*j) {
			// If the final label is mostly underscore, use an alternate label according to the
			// size (eg: "256 MB", "7.9 GB"). Note that we can't use SelectedDrive.proposed_label
			// here as it may contain localized character for GB or MB, so make sure that the
			// effective label we use is an English one, and also make sure we convert the dot.
			static_sprintf(label, "%s", SizeToHumanReadable(SelectedDrive.DiskSize, TRUE, FALSE));
			for (i = 0; label[i] != 0; i++)
				wLabel[i] = (label[i] == '.') ? '_' : label[i];
			wLabel[i] = 0;
			uprintf("FAT label is mostly underscores. Using '%S' label instead.", wLabel);
		}
	} else if (wcslen(wLabel) > 32) {
		wLabel[32] = 0;
	}

	// Needed for disk by label isolinux.cfg workaround
	wchar_to_utf8_no_alloc(wLabel, img_report.usb_label, sizeof(img_report.usb_label));
	safe_strcpy(Label, strlen(Label) + 1, img_report.usb_label);
	free(wLabel);
}

/*
 * Call on VDS to format a partition
 */
static BOOL FormatNativeVds(DWORD DriveIndex, uint64_t PartitionOffset, DWORD ClusterSize, LPCSTR FSName, LPCSTR Label, DWORD Flags)
{
	BOOL r = FALSE, bFoundVolume = FALSE;
	HRESULT hr;
	ULONG ulFetched;
	IVdsServiceLoader *pLoader;
	IVdsService *pService;
	IEnumVdsObject *pEnum;
	IUnknown *pUnk;
	char* VolumeName = NULL;
	WCHAR *wVolumeName = NULL, *wLabel = utf8_to_wchar(Label), *wFSName = utf8_to_wchar(FSName);

	if ((strcmp(FSName, FileSystemLabel[FS_EXFAT]) == 0) && !((dur_mins == 0) && (dur_secs == 0))) {
		PrintInfo(0, MSG_220, FSName, dur_mins, dur_secs);
	} else {
		PrintInfo(0, MSG_222, FSName);
	}
	uprintf("Formatting to %s (using VDS)", FSName);

	UpdateProgressWithInfoInit(NULL, TRUE);
	VolumeName = GetLogicalName(DriveIndex, PartitionOffset, TRUE, TRUE);
	wVolumeName = utf8_to_wchar(VolumeName);
	if (wVolumeName == NULL) {
		uprintf("Could not read volume name");
		ErrorStatus = RUFUS_ERROR(ERROR_GEN_FAILURE);
		goto out;
	}

	// Initialize COM
	IGNORE_RETVAL(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
	IGNORE_RETVAL(CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_CONNECT,
		RPC_C_IMP_LEVEL_IMPERSONATE, NULL, 0, NULL));

	// Create a VDS Loader Instance
	hr = CoCreateInstance(&CLSID_VdsLoader, NULL, CLSCTX_LOCAL_SERVER | CLSCTX_REMOTE_SERVER,
		&IID_IVdsServiceLoader, (void **)&pLoader);
	if (hr != S_OK) {
		VDS_SET_ERROR(hr);
		uprintf("Could not create VDS Loader Instance: %s", WindowsErrorString());
		goto out;
	}

	// Load the VDS Service
	hr = IVdsServiceLoader_LoadService(pLoader, L"", &pService);
	IVdsServiceLoader_Release(pLoader);
	if (hr != S_OK) {
		VDS_SET_ERROR(hr);
		uprintf("Could not load VDS Service: %s", WindowsErrorString());
		goto out;
	}

	// Wait for the Service to become ready if needed
	hr = IVdsService_WaitForServiceReady(pService);
	if (hr != S_OK) {
		VDS_SET_ERROR(hr);
		uprintf("VDS Service is not ready: %s", WindowsErrorString());
		goto out;
	}

	// Query the VDS Service Providers
	hr = IVdsService_QueryProviders(pService, VDS_QUERY_SOFTWARE_PROVIDERS, &pEnum);
	if (hr != S_OK) {
		VDS_SET_ERROR(hr);
		uprintf("Could not query VDS Service Providers: %s", WindowsErrorString());
		goto out;
	}

	while (IEnumVdsObject_Next(pEnum, 1, &pUnk, &ulFetched) == S_OK) {
		IVdsProvider *pProvider;
		IVdsSwProvider *pSwProvider;
		IEnumVdsObject *pEnumPack;
		IUnknown *pPackUnk;
		CHECK_FOR_USER_CANCEL;

		// Get VDS Provider
		hr = IUnknown_QueryInterface(pUnk, &IID_IVdsProvider, (void **)&pProvider);
		IUnknown_Release(pUnk);
		if (hr != S_OK) {
			VDS_SET_ERROR(hr);
			uprintf("Could not get VDS Provider: %s", WindowsErrorString());
			goto out;
		}

		// Get VDS Software Provider
		hr = IVdsSwProvider_QueryInterface(pProvider, &IID_IVdsSwProvider, (void **)&pSwProvider);
		IVdsProvider_Release(pProvider);
		if (hr != S_OK) {
			VDS_SET_ERROR(hr);
			uprintf("Could not get VDS Software Provider: %s", WindowsErrorString());
			goto out;
		}

		// Get VDS Software Provider Packs
		hr = IVdsSwProvider_QueryPacks(pSwProvider, &pEnumPack);
		IVdsSwProvider_Release(pSwProvider);
		if (hr != S_OK) {
			VDS_SET_ERROR(hr);
			uprintf("Could not get VDS Software Provider Packs: %s", WindowsErrorString());
			goto out;
		}

		// Enumerate Provider Packs
		while (IEnumVdsObject_Next(pEnumPack, 1, &pPackUnk, &ulFetched) == S_OK) {
			IVdsPack *pPack;
			IEnumVdsObject *pEnumVolume;
			IUnknown *pVolumeUnk;
			CHECK_FOR_USER_CANCEL;

			hr = IUnknown_QueryInterface(pPackUnk, &IID_IVdsPack, (void **)&pPack);
			IUnknown_Release(pPackUnk);
			if (hr != S_OK) {
				VDS_SET_ERROR(hr);
				uprintf("Could not query VDS Software Provider Pack: %s", WindowsErrorString());
				goto out;
			}

			// Use the pack interface to access the volumes
			hr = IVdsPack_QueryVolumes(pPack, &pEnumVolume);
			if (hr != S_OK) {
				VDS_SET_ERROR(hr);
				uprintf("Could not query VDS volumes: %s", WindowsErrorString());
				goto out;
			}

			// List volumes
			while (IEnumVdsObject_Next(pEnumVolume, 1, &pVolumeUnk, &ulFetched) == S_OK) {
				BOOL match;
				HRESULT hr2 = E_FAIL;
				VDS_VOLUME_PROP VolumeProps;
				LPWSTR *wszPathArray;
				ULONG ulPercentCompleted, ulNumberOfPaths;
				USHORT usFsVersion = 0;
				IVdsVolume *pVolume;
				IVdsAsync* pAsync;
				IVdsVolumeMF3 *pVolumeMF3;
				CHECK_FOR_USER_CANCEL;

				// Get the volume interface.
				hr = IUnknown_QueryInterface(pVolumeUnk, &IID_IVdsVolume, (void **)&pVolume);
				if (hr != S_OK) {
					VDS_SET_ERROR(hr);
					uprintf("Could not query VDS Volume Interface: %s", WindowsErrorString());
					goto out;
				}

				hr = IVdsVolume_GetProperties(pVolume, &VolumeProps);
				if ((hr != S_OK) && (hr != VDS_S_PROPERTIES_INCOMPLETE)) {
					VDS_SET_ERROR(hr);
					IVdsVolume_Release(pVolume);
					uprintf("Could not query VDS Volume Properties: %s", WindowsErrorString());
					continue;
				}
				CoTaskMemFree(VolumeProps.pwszName);

				// Instantiate the IVdsVolumeMF3 interface for our volume.
				hr = IVdsVolume_QueryInterface(pVolume, &IID_IVdsVolumeMF3, (void **)&pVolumeMF3);
				IVdsVolume_Release(pVolume);
				if (hr != S_OK) {
					VDS_SET_ERROR(hr);
					uprintf("Could not access VDS VolumeMF3 interface: %s", WindowsErrorString());
					continue;
				}

				// Query the volume GUID
				hr = IVdsVolumeMF3_QueryVolumeGuidPathnames(pVolumeMF3, &wszPathArray, &ulNumberOfPaths);
				if (hr != S_OK) {
					VDS_SET_ERROR(hr);
					uprintf("Could not query VDS VolumeGuidPathnames: %s", WindowsErrorString());
					continue;
				}

				if (ulNumberOfPaths > 1)
					uprintf("Notice: Volume %S has more than one GUID...", wszPathArray[0]);

				match = (wcscmp(wVolumeName, wszPathArray[0]) == 0);
				CoTaskMemFree(wszPathArray);
				if (!match)
					continue;

				bFoundVolume = TRUE;
				if (strcmp(Label, FileSystemLabel[FS_UDF]) == 0)
					usFsVersion = ReadSetting32(SETTING_USE_UDF_VERSION);
				if (ClusterSize < 0x200) {
					ClusterSize = 0;
					uprintf("Using default cluster size");
				} else {
					uprintf("Using cluster size: %d bytes", ClusterSize);
				}
				format_percent = 0.0f;
				uprintf("%s format was selected", (Flags & FP_QUICK) ? "Quick" : "Slow");
				if (Flags & FP_COMPRESSION)
					uprintf("NTFS compression is enabled");

				hr = IVdsVolumeMF3_FormatEx2(pVolumeMF3, wFSName, usFsVersion, ClusterSize, wLabel, Flags, &pAsync);
				while (SUCCEEDED(hr)) {
					if (IS_ERROR(ErrorStatus)) {
						IVdsAsync_Cancel(pAsync);
						break;
					}
					hr = IVdsAsync_QueryStatus(pAsync, &hr2, &ulPercentCompleted);
					if (SUCCEEDED(hr)) {
						if (Flags & FP_QUICK) {
							// Progress report on quick format is useless, so we'll just pretend we have 2 tasks
							PrintInfo(0, MSG_218, (ulPercentCompleted < 100) ? 1 : 2, 2);
							UpdateProgress(OP_CREATE_FS, (float)ulPercentCompleted);
						} else {
							UpdateProgressWithInfo(OP_FORMAT, MSG_217, ulPercentCompleted, 100);
						}
						hr = hr2;
						if (hr == S_OK)
							break;
						if (hr == VDS_E_OPERATION_PENDING)
							hr = S_OK;
					}
					Sleep(500);
				}
				if (!SUCCEEDED(hr)) {
					VDS_SET_ERROR(hr);
					uprintf("Could not format drive: %s", WindowsErrorString());
					goto out;
				}

				IVdsAsync_Release(pAsync);
				IVdsVolumeMF3_Release(pVolumeMF3);

				if (!IS_ERROR(ErrorStatus)) {
					uprintf("Format completed.");
					r = TRUE;
				}
				goto out;
			}
		}
	}

out:
	if ((!bFoundVolume) && (ErrorStatus == 0))
		ErrorStatus = RUFUS_ERROR(ERROR_PATH_NOT_FOUND);
	safe_free(VolumeName);
	safe_free(wVolumeName);
	safe_free(wLabel);
	safe_free(wFSName);
	CoUninitialize();
	return r;
}

/*
 * Call on fmifs.dll's FormatEx() to format the drive
 */
static BOOL FormatNative(DWORD DriveIndex, uint64_t PartitionOffset, DWORD ClusterSize, LPCSTR FSName, LPCSTR Label, DWORD Flags)
{
	BOOL r = FALSE;
	PF_DECL(FormatEx);
	PF_DECL(EnableVolumeCompression);
	char *locale, *VolumeName = NULL;
	WCHAR* wVolumeName = NULL, *wLabel = utf8_to_wchar(Label), *wFSName = utf8_to_wchar(FSName);
	size_t i;

	if ((strcmp(FSName, FileSystemLabel[FS_EXFAT]) == 0) && !((dur_mins == 0) && (dur_secs == 0))) {
		PrintInfo(0, MSG_220, FSName, dur_mins, dur_secs);
	} else {
		PrintInfo(0, MSG_222, FSName);
	}
	uprintf("Formatting to %s (using IFS)", FSName);

	VolumeName = GetLogicalName(DriveIndex, PartitionOffset, TRUE, TRUE);
	wVolumeName = utf8_to_wchar(VolumeName);
	if (wVolumeName == NULL) {
		uprintf("Could not read volume name (%s)", VolumeName);
		ErrorStatus = RUFUS_ERROR(ERROR_GEN_FAILURE);
		goto out;
	}
	// Hey, nice consistency here, Microsoft! -  FormatEx() fails if wVolumeName has
	// a trailing backslash, but EnableCompression() fails without...
	wVolumeName[wcslen(wVolumeName)-1] = 0;		// Remove trailing backslash

	// LoadLibrary("fmifs.dll") appears to changes the locale, which can lead to
	// problems with tolower(). Make sure we restore the locale. For more details,
	// see https://sourceforge.net/p/mingw/mailman/message/29269040/
	locale = setlocale(LC_ALL, NULL);
	PF_INIT_OR_OUT(FormatEx, fmifs);
	PF_INIT(EnableVolumeCompression, fmifs);
	setlocale(LC_ALL, locale);

	if (ClusterSize < 0x200) {
		// 0 is FormatEx's value for default, which we need to use for UDF
		ClusterSize = 0;
		uprintf("Using default cluster size");
	} else {
		uprintf("Using cluster size: %d bytes", ClusterSize);
	}
	format_percent = 0.0f;
	task_number = 0;

	uprintf("%s format was selected", (Flags & FP_QUICK) ? "Quick" : "Slow");
	for (i = 0; i < WRITE_RETRIES; i++) {
		pfFormatEx(wVolumeName, SelectedDrive.MediaType, wFSName, wLabel,
			(Flags & FP_QUICK), ClusterSize, FormatExCallback);
		if (!IS_ERROR(ErrorStatus) || (HRESULT_CODE(ErrorStatus) == ERROR_CANCELLED))
			break;
		uprintf("%s - Retrying...", WindowsErrorString());
		Sleep(WRITE_TIMEOUT);
	}
	if (IS_ERROR(ErrorStatus))
		goto out;

	if (Flags & FP_COMPRESSION) {
		wVolumeName[wcslen(wVolumeName)] = '\\';	// Add trailing backslash back again
		if (pfEnableVolumeCompression(wVolumeName, FPF_COMPRESSED)) {
			uprintf("Enabled NTFS compression");
		} else {
			uprintf("Could not enable NTFS compression: %s", WindowsErrorString());
		}
	}

	if (!IS_ERROR(ErrorStatus)) {
		uprintf("Format completed.");
		r = TRUE;
	}

out:
	if (!r && !IS_ERROR(ErrorStatus))
		ErrorStatus = RUFUS_ERROR(SCODE_CODE(GetLastError()));
	safe_free(VolumeName);
	safe_free(wVolumeName);
	safe_free(wLabel);
	safe_free(wFSName);
	return r;
}

BOOL FormatPartition(DWORD DriveIndex, uint64_t PartitionOffset, DWORD UnitAllocationSize, USHORT FSType, LPCSTR Label, DWORD Flags)
{
	if ((DriveIndex < 0x80) || (DriveIndex > 0x100) || (FSType >= FS_MAX) ||
		((UnitAllocationSize != 0) && (!IS_POWER_OF_2(UnitAllocationSize)))) {
		ErrorStatus = RUFUS_ERROR(ERROR_INVALID_PARAMETER);
		return FALSE;
	}
	actual_fs_type = FSType;
	if ((FSType == FS_FAT32) && ((SelectedDrive.DiskSize > LARGE_FAT32_SIZE) || (force_large_fat32) || (Flags & FP_LARGE_FAT32)))
		return FormatLargeFAT32(DriveIndex, PartitionOffset, UnitAllocationSize, FileSystemLabel[FSType], Label, Flags);
	else if (IS_EXT(FSType))
		return FormatExtFs(DriveIndex, PartitionOffset, UnitAllocationSize, FileSystemLabel[FSType], Label, Flags);
	else if (use_vds)
		return FormatNativeVds(DriveIndex, PartitionOffset, UnitAllocationSize, FileSystemLabel[FSType], Label, Flags);
	else
		return FormatNative(DriveIndex, PartitionOffset, UnitAllocationSize, FileSystemLabel[FSType], Label, Flags);
}

/*
 * Call on fmifs.dll's Chkdsk() to fixup the filesystem
 */
static BOOL CheckDisk(char DriveLetter)
{
	BOOL r = FALSE;
	PF_DECL(Chkdsk);
	WCHAR wDriveRoot[] = L"?:\\";
	WCHAR wFSType[32];
	size_t i;

	wDriveRoot[0] = (WCHAR)DriveLetter;
	PrintInfoDebug(0, MSG_223);

	PF_INIT_OR_OUT(Chkdsk, Fmifs);

	GetWindowTextW(hFileSystem, wFSType, ARRAYSIZE(wFSType));
	// We may have a " (Default)" trail
	for (i=0; i<wcslen(wFSType); i++) {
		if (wFSType[i] == ' ') {
			wFSType[i] = 0;
			break;
		}
	}

	pfChkdsk(wDriveRoot, wFSType, FALSE, FALSE, FALSE, FALSE, NULL, NULL, ChkdskCallback);
	if (!IS_ERROR(ErrorStatus)) {
		uprintf("NTFS Fixup completed.");
		r = TRUE;
	}

out:
	return r;
}

static BOOL ClearMBRGPT(HANDLE hPhysicalDrive, LONGLONG DiskSize, DWORD SectorSize, BOOL add1MB)
{
	BOOL r = FALSE;
	LARGE_INTEGER liFilePointer;
	uint64_t num_sectors_to_clear;
	unsigned char* pZeroBuf = NULL;

	PrintInfoDebug(0, MSG_224);
	// http://en.wikipedia.org/wiki/GUID_Partition_Table tells us we should clear 34 sectors at the
	// beginning and 33 at the end. We bump these values to MAX_SECTORS_TO_CLEAR each end to help
	// with reluctant access to large drive.

	// We try to clear at least 1MB + the VBR when Large FAT32 is selected (add1MB), but
	// don't do it otherwise, as it seems unnecessary and may take time for slow drives.
	// Also, for various reasons (one of which being that Windows seems to have issues
	// with GPT drives that contain a lot of small partitions) we try not not to clear
	// sectors further than the lowest partition already residing on the disk.
	num_sectors_to_clear = min(SelectedDrive.FirstDataSector, (DWORD)((add1MB ? 2048 : 0) + MAX_SECTORS_TO_CLEAR));
	// Special case for big floppy disks (FirstDataSector = 0)
	if (num_sectors_to_clear < 4)
		num_sectors_to_clear = (DWORD)((add1MB ? 2048 : 0) + MAX_SECTORS_TO_CLEAR);

	uprintf("Erasing %llu sectors", num_sectors_to_clear);
	pZeroBuf = calloc(SectorSize, (size_t)num_sectors_to_clear);
	if (pZeroBuf == NULL) {
		ErrorStatus = RUFUS_ERROR(ERROR_NOT_ENOUGH_MEMORY);
		goto out;
	}
	liFilePointer.QuadPart = 0ULL;
	if (!SetFilePointerEx(hPhysicalDrive, liFilePointer, &liFilePointer, FILE_BEGIN) || (liFilePointer.QuadPart != 0ULL))
		uprintf("Warning: Could not reset disk position");
	if (!WriteFileWithRetry(hPhysicalDrive, pZeroBuf, (DWORD)(SectorSize * num_sectors_to_clear), NULL, WRITE_RETRIES))
		goto out;
	CHECK_FOR_USER_CANCEL;
	liFilePointer.QuadPart = DiskSize - (LONGLONG)SectorSize * MAX_SECTORS_TO_CLEAR;
	// Windows seems to be an ass about keeping a lock on a backup GPT,
	// so we try to be lenient about not being able to clear it.
	if (SetFilePointerEx(hPhysicalDrive, liFilePointer, &liFilePointer, FILE_BEGIN)) {
		IGNORE_RETVAL(WriteFileWithRetry(hPhysicalDrive, pZeroBuf,
			SectorSize * MAX_SECTORS_TO_CLEAR, NULL, WRITE_RETRIES));
	}
	r = TRUE;

out:
	safe_free(pZeroBuf);
	return r;
}

/*
 * Process the Master Boot Record
 */
static BOOL WriteMBR(HANDLE hPhysicalDrive)
{
	BOOL r = FALSE;
	BOOL needs_masquerading = HAS_WINPE(img_report) && (!img_report.uses_minint);
	uint8_t* buffer = NULL;
	FAKE_FD fake_fd = { 0 };
	FILE* fp = (FILE*)&fake_fd;
	const char* using_msg = "Using %s MBR";

	if (SelectedDrive.SectorSize < 512)
		goto out;

	if (partition_type == PARTITION_STYLE_GPT) {
		// Add a notice with a protective MBR
		fake_fd._handle = (char*)hPhysicalDrive;
		set_bytes_per_sector(SelectedDrive.SectorSize);
		uprintf(using_msg, "Rufus protective");
		r = write_rufus_msg_mbr(fp);
		goto notify;
	}

	// FormatEx rewrites the MBR and removes the LBA attribute of FAT16
	// and FAT32 partitions - we need to correct this in the MBR
	buffer = (uint8_t*)_mm_malloc(SelectedDrive.SectorSize, 16);
	if (buffer == NULL) {
		uprintf("Could not allocate memory for MBR");
		ErrorStatus = RUFUS_ERROR(ERROR_NOT_ENOUGH_MEMORY);
		goto out;
	}

	if (!read_sectors(hPhysicalDrive, SelectedDrive.SectorSize, 0, 1, buffer)) {
		uprintf("Could not read MBR");
		ErrorStatus = RUFUS_ERROR(ERROR_READ_FAULT);
		goto out;
	}

	switch (ComboBox_GetCurItemData(hFileSystem)) {
	case FS_FAT16:
		if (buffer[0x1c2] == 0x0e) {
			uprintf("Partition is already FAT16 LBA...");
		} else if ((buffer[0x1c2] != 0x04) && (buffer[0x1c2] != 0x06)) {
			uprintf("Warning: converting a non FAT16 partition to FAT16 LBA: FS type=0x%02x", buffer[0x1c2]);
		}
		buffer[0x1c2] = 0x0e;
		break;
	case FS_FAT32:
		if (buffer[0x1c2] == 0x0c) {
			uprintf("Partition is already FAT32 LBA...");
		} else if (buffer[0x1c2] != 0x0b) {
			uprintf("Warning: converting a non FAT32 partition to FAT32 LBA: FS type=0x%02x", buffer[0x1c2]);
		}
		buffer[0x1c2] = 0x0c;
		break;
	}
	if ((boot_type != BT_NON_BOOTABLE) && (target_type == TT_BIOS)) {
		// Set first partition bootable or masquerade as second disk
		buffer[0x1be] = needs_masquerading ? 0x81 : 0x80;
		uprintf("Set bootable USB partition as 0x%02X", buffer[0x1be]);
	}

	if (!write_sectors(hPhysicalDrive, SelectedDrive.SectorSize, 0, 1, buffer)) {
		uprintf("Could not write MBR");
		ErrorStatus = RUFUS_ERROR(ERROR_WRITE_FAULT);
		goto out;
	}

	fake_fd._handle = (char*)hPhysicalDrive;
	set_bytes_per_sector(SelectedDrive.SectorSize);

	// What follows is really a case statement with complex conditions listed
	// by order of preference
	if ((boot_type == BT_IMAGE) && HAS_WINDOWS(img_report) && (allow_dual_uefi_bios) && (target_type == TT_BIOS))
		goto windows_mbr;

	// Non bootable or forced UEFI (zeroed MBR)
	if ((boot_type == BT_NON_BOOTABLE) || (target_type == TT_UEFI)) {
		uprintf(using_msg, "Zeroed");
		r = write_zero_mbr(fp);
		goto notify;
	}

	// Syslinux
	if ( (boot_type == BT_SYSLINUX_V4) || (boot_type == BT_SYSLINUX_V6) ||
		 ((boot_type == BT_IMAGE) && HAS_SYSLINUX(img_report)) ) {
		uprintf(using_msg, "Syslinux");
		r = write_syslinux_mbr(fp);
		goto notify;
	}

	// Grub 2.0
	if ( ((boot_type == BT_IMAGE) && (img_report.has_grub2)) || (boot_type == BT_GRUB2) ) {
		uprintf(using_msg, "Grub 2.0");
		r = write_grub2_mbr(fp);
		goto notify;
	}

	// Grub4DOS
	if ( ((boot_type == BT_IMAGE) && (img_report.has_grub4dos)) || (boot_type == BT_GRUB4DOS) ) {
		uprintf(using_msg, "Grub4DOS");
		r = write_grub4dos_mbr(fp);
		goto notify;
	}

	// ReactOS
	if (boot_type == BT_REACTOS) {
		uprintf(using_msg, "ReactOS");
		r = write_reactos_mbr(fp);
		goto notify;
	}

	// KolibriOS
	if ( (boot_type == BT_IMAGE) && HAS_KOLIBRIOS(img_report) && (IS_FAT(fs_type))) {
		uprintf(using_msg, "KolibriOS");
		r = write_kolibrios_mbr(fp);
		goto notify;
	}

	// If everything else failed, fall back to a conventional Windows/Rufus MBR
windows_mbr:
	if (needs_masquerading || use_rufus_mbr) {
		uprintf(using_msg, APPLICATION_NAME);
		r = write_rufus_mbr(fp);
	} else {
		uprintf(using_msg, "Windows 7");
		r = write_win7_mbr(fp);
	}

notify:
	// Tell the system we've updated the disk properties
	if (!DeviceIoControl(hPhysicalDrive, IOCTL_DISK_UPDATE_PROPERTIES, NULL, 0, NULL, 0, NULL, NULL))
		uprintf("Failed to notify system about disk properties update: %s", WindowsErrorString());

out:
	safe_mm_free(buffer);
	return r;
}

/*
 * Write Secondary Boot Record (usually right after the MBR)
 */
static BOOL WriteSBR(HANDLE hPhysicalDrive)
{
	// TODO: Do we need anything special for 4K sectors?
	DWORD size, max_size, br_size = 0x200;
	int r, sub_type = boot_type;
	uint8_t *buf = NULL;
	FAKE_FD fake_fd = { 0 };
	FILE* fp = (FILE*)&fake_fd;

	fake_fd._handle = (char*)hPhysicalDrive;
	set_bytes_per_sector(SelectedDrive.SectorSize);
	// Syslinux has precedence over Grub
	if ((boot_type == BT_IMAGE) && (!HAS_SYSLINUX(img_report))) {
		if (img_report.has_grub4dos)
			sub_type = BT_GRUB4DOS;
		if (img_report.has_grub2)
			sub_type = BT_GRUB2;
	}

	// Use BT_MAX for the protective message
	if ((boot_type != BT_NON_BOOTABLE) && (partition_type == PARTITION_STYLE_GPT))
		sub_type = BT_MAX;

	switch (sub_type) {
	case BT_GRUB4DOS:
		uprintf("Writing Grub4Dos SBR");
		buf = GetResource(hMainInstance, MAKEINTRESOURCEA(IDR_GR_GRUB_GRLDR_MBR), _RT_RCDATA, "grldr.mbr", &size, FALSE);
		if ((buf == NULL) || (size <= br_size)) {
			uprintf("grldr.mbr is either not present or too small");
			return FALSE;
		}
		buf = &buf[br_size];
		size -= br_size;
		break;
	case BT_GRUB2:
		if (grub2_buf != NULL) {
			uprintf("Writing Grub 2.0 SBR (from download) %s",
				IsBufferInDB(grub2_buf, grub2_len)?"✓":"✗");
			buf = grub2_buf;
			size = (DWORD)grub2_len;
		} else {
			uprintf("Writing Grub 2.0 SBR (from embedded)");
			buf = GetResource(hMainInstance, MAKEINTRESOURCEA(IDR_GR_GRUB2_CORE_IMG), _RT_RCDATA, "core.img", &size, FALSE);
			if (buf == NULL) {
				uprintf("Could not access core.img");
				return FALSE;
			}
		}
		break;
	case BT_MAX:
		uprintf("Writing protective message SBR");
		size = 4 * KB;
		br_size = 17 * KB;	// 34 sectors are reserved for protective MBR + primary GPT
		buf = GetResource(hMainInstance, MAKEINTRESOURCEA(IDR_SBR_MSG), _RT_RCDATA, "msg.txt", &size, TRUE);
		if (buf == NULL) {
			uprintf("Could not access message");
			return FALSE;
		}
		break;
	default:
		// No need to write secondary block
		return TRUE;
	}

	// Ensure that we have sufficient space for the SBR
	max_size = (DWORD)SelectedDrive.Partition[0].Offset;
	if (br_size + size > max_size) {
		uprintf("  SBR size is too large - You may need to uncheck 'Add fixes for old BIOSes'.");
		if (sub_type == BT_MAX)
			safe_free(buf);
		return FALSE;
	}

	r = write_data(fp, br_size, buf, (uint64_t)size);
	safe_free(grub2_buf);
	if (sub_type == BT_MAX)
		safe_free(buf);
	return (r != 0);
}

/*
 * Process the Partition Boot Record
 */
static __inline const char* bt_to_name(void) {
	switch (boot_type) {
	case BT_FREEDOS: return "FreeDOS";
	case BT_REACTOS: return "ReactOS";
	default:
		return ((boot_type == BT_IMAGE) && HAS_KOLIBRIOS(img_report)) ? "KolibriOS" : "Standard";
	}
}

BOOL WritePBR(HANDLE hLogicalVolume)
{
	int i;
	FAKE_FD fake_fd = { 0 };
	FILE* fp = (FILE*)&fake_fd;
	const char* using_msg = "Using %s %s partition boot record";

	fake_fd._handle = (char*)hLogicalVolume;
	set_bytes_per_sector(SelectedDrive.SectorSize);

	switch (actual_fs_type) {
	case FS_FAT16:
		uprintf(using_msg, bt_to_name(), "FAT16");
		if (!is_fat_16_fs(fp)) {
			uprintf("New volume does not have a FAT16 boot sector - aborting");
			break;
		}
		uprintf("Confirmed new volume has a FAT16 boot sector");
		if (boot_type == BT_FREEDOS) {
			if (!write_fat_16_fd_br(fp, 0)) break;
		} else if (boot_type == BT_REACTOS) {
			if (!write_fat_16_ros_br(fp, 0)) break;
		} else if ((boot_type == BT_IMAGE) && HAS_KOLIBRIOS(img_report)) {
			uprintf("FAT16 is not supported for KolibriOS"); break;
		} else {
			if (!write_fat_16_br(fp, 0)) break;
		}
		// Disk Drive ID needs to be corrected on XP
		if (!write_partition_physical_disk_drive_id_fat16(fp))
			break;
		return TRUE;
	case FS_FAT32:
		uprintf(using_msg, bt_to_name(), "FAT32");
		for (i = 0; i < 2; i++) {
			if (!is_fat_32_fs(fp)) {
				uprintf("New volume does not have a %s FAT32 boot sector - aborting", i ? "secondary" : "primary");
				break;
			}
			uprintf("Confirmed new volume has a %s FAT32 boot sector", i ? "secondary" : "primary");
			uprintf("Setting %s FAT32 boot sector for boot...", i ? "secondary" : "primary");
			if (boot_type == BT_FREEDOS) {
				if (!write_fat_32_fd_br(fp, 0)) break;
			} else if (boot_type == BT_REACTOS) {
				if (!write_fat_32_ros_br(fp, 0)) break;
			} else if ((boot_type == BT_IMAGE) && HAS_KOLIBRIOS(img_report)) {
				if (!write_fat_32_kos_br(fp, 0)) break;
			} else if ((boot_type == BT_IMAGE) && HAS_BOOTMGR(img_report)) {
				if (!write_fat_32_pe_br(fp, 0)) break;
			} else if ((boot_type == BT_IMAGE) && HAS_WINPE(img_report)) {
				if (!write_fat_32_nt_br(fp, 0)) break;
			} else {
				if (!write_fat_32_br(fp, 0)) break;
			}
			// Disk Drive ID needs to be corrected on XP
			if (!write_partition_physical_disk_drive_id_fat32(fp))
				break;
			fake_fd._offset += 6 * SelectedDrive.SectorSize;
		}
		return TRUE;
	case FS_NTFS:
		uprintf(using_msg, bt_to_name(), "NTFS");
		if (!is_ntfs_fs(fp)) {
			uprintf("New volume does not have an NTFS boot sector - aborting");
			break;
		}
		uprintf("Confirmed new volume has an NTFS boot sector");
		if (!write_ntfs_br(fp)) break;
		// Note: NTFS requires a full remount after writing the PBR. We dismount when we lock
		// and also go through a forced remount, so that shouldn't be an issue.
		// But with NTFS, if you don't remount, you don't boot!
		return TRUE;
	case FS_EXT2:
	case FS_EXT3:
	case FS_EXT4:
		return TRUE;
	default:
		uprintf("Unsupported FS for FS BR processing - aborting");
		break;
	}
	ErrorStatus = RUFUS_ERROR(ERROR_WRITE_FAULT);
	return FALSE;
}

static void update_progress(const uint64_t processed_bytes)
{
	// NB: We don't really care about resetting this value to UINT64_MAX for a new pass.
	static uint64_t last_value = UINT64_MAX;
	uint64_t cur_value;

	UpdateProgressWithInfo(OP_FORMAT, MSG_261, processed_bytes, img_report.image_size);
	cur_value = (processed_bytes * min(80, img_report.image_size)) / img_report.image_size;
	if (cur_value != last_value) {
		last_value = cur_value;
		uprintfs("+");
	}
}

// Some compressed images use streams that aren't multiple of the sector
// size and cause write failures => Use a write override that alleviates
// the problem. See GitHub issue #1422 for details.
static int sector_write(int fd, const void* _buf, unsigned int count)
{
	const uint8_t* buf = (const uint8_t*)_buf;
	unsigned int sec_size = (unsigned int)SelectedDrive.SectorSize;
	int written, fill_size = 0;

	if (sec_size == 0)
		sec_size = 512;

	// If we are on a sector boundary and count is multiple of the
	// sector size, just issue a regular write
	if ((sec_buf_pos == 0) && (count % sec_size == 0))
		return _write(fd, buf, count);

	// If we have an existing partial sector, fill and write it
	if (sec_buf_pos > 0) {
		fill_size = min(sec_size - sec_buf_pos, count);
		memcpy(&sec_buf[sec_buf_pos], buf, fill_size);
		sec_buf_pos += fill_size;
		// If we don't have a full sector just buffer it for next call
		if (sec_buf_pos < sec_size)
			return (int)count;
		sec_buf_pos = 0;
		written = _write(fd, sec_buf, sec_size);
		if (written != sec_size)
			return written;
	}

	// Now write as many full sectors as we can
	uint32_t sec_num = (count - fill_size) / sec_size;
	written = _write(fd, &buf[fill_size], sec_num * sec_size);
	if (written < 0)
		return written;
	else if (written != sec_num * sec_size)
		return fill_size + written;
	sec_buf_pos = count - fill_size - written;
	assert(sec_buf_pos < sec_size);

	// Keep leftover bytes, if any, in the sector buffer
	if (sec_buf_pos != 0)
		memcpy(sec_buf, &buf[fill_size + written], sec_buf_pos);
	return (int)count;
}

/* Write an image file or zero a drive */
static BOOL WriteDrive(HANDLE hPhysicalDrive, BOOL bZeroDrive)
{
	BOOL s, ret = FALSE;
	LARGE_INTEGER li;
	HANDLE hSourceImage = INVALID_HANDLE_VALUE;
	DWORD i, read_size[NUM_BUFFERS], write_size, comp_size, buf_size;
	uint64_t wb, target_size = bZeroDrive ? SelectedDrive.DiskSize : img_report.image_size;
	uint64_t cur_value, last_value = 0;
	int64_t bled_ret;
	uint8_t* buffer = NULL;
	uint32_t zero_data, *cmp_buffer = NULL;
	char* vhd_path = NULL;
	int throttle_fast_zeroing = 0, read_bufnum = 0, proc_bufnum = 1;

	if (SelectedDrive.SectorSize < 512) {
		uprintf("Unexpected sector size (%d) - Aborting", SelectedDrive.SectorSize);
		return FALSE;
	}

	// We poked the MBR and other stuff, so we need to rewind
	li.QuadPart = 0;
	if (!SetFilePointerEx(hPhysicalDrive, li, NULL, FILE_BEGIN))
		uprintf("Warning: Unable to rewind image position - wrong data might be copied!");
	UpdateProgressWithInfoInit(NULL, FALSE);

	if (bZeroDrive) {
		uprintf(fast_zeroing ? "Fast-zeroing drive:" : "Zeroing drive:");
		// Our buffer size must be a multiple of the sector size and *ALIGNED* to the sector size
		buf_size = ((DD_BUFFER_SIZE + SelectedDrive.SectorSize - 1) / SelectedDrive.SectorSize) * SelectedDrive.SectorSize;
		buffer = (uint8_t*)_mm_malloc(buf_size, SelectedDrive.SectorSize);
		if (buffer == NULL) {
			ErrorStatus = RUFUS_ERROR(ERROR_NOT_ENOUGH_MEMORY);
			uprintf("Could not allocate disk zeroing buffer");
			goto out;
		}
		assert((uintptr_t)buffer % SelectedDrive.SectorSize == 0);

		// Clear buffer
		memset(buffer, fast_zeroing ? 0xff : 0x00, buf_size);

		if (fast_zeroing) {
			cmp_buffer = (uint32_t*)_mm_malloc(buf_size, SelectedDrive.SectorSize);
			if (cmp_buffer == NULL) {
				ErrorStatus = RUFUS_ERROR(ERROR_NOT_ENOUGH_MEMORY);
				uprintf("Could not allocate disk comparison buffer");
				goto out;
			}
			assert((uintptr_t)cmp_buffer % SelectedDrive.SectorSize == 0);
		}

		read_size[0] = buf_size;
		for (wb = 0, write_size = 0; wb < target_size; wb += write_size) {
			UpdateProgressWithInfo(OP_FORMAT, fast_zeroing ? MSG_306 : MSG_286, wb, target_size);
			cur_value = (wb * 80) / target_size;
			for (; cur_value > last_value && last_value < 80; last_value++)
				uprintfs("+");
			// Don't overflow our projected size (mostly for VHDs)
			if (wb + read_size[0] > target_size)
				read_size[0] = (DWORD)(target_size - wb);

			// WriteFile fails unless the size is a multiple of sector size
			if (read_size[0] % SelectedDrive.SectorSize != 0)
				read_size[0] = ((read_size[0] + SelectedDrive.SectorSize - 1) / SelectedDrive.SectorSize) * SelectedDrive.SectorSize;

			// Fast-zeroing: Depending on your hardware, reading from flash may be much faster than writing, so
			// we might speed things up by skipping empty blocks, or skipping the write if the data is the same.
			// Notes: A block is declared empty when all bits are either 0 (zeros) or 1 (flash block erased).
			// Also, a back-off strategy is used to limit reading.
			if (throttle_fast_zeroing) {
				throttle_fast_zeroing--;
			} else if (fast_zeroing) {
				CHECK_FOR_USER_CANCEL;

				// Read block and compare against the block that needs to be written
				s = ReadFile(hPhysicalDrive, cmp_buffer, read_size[0], &comp_size, NULL);
				if ((!s) || (comp_size != read_size[0])) {
					uprintf("\r\nRead error: Could not read data for fast zeroing comparison - %s", WindowsErrorString());
					goto out;
				}

				// Check for an empty block by comparing with the first element
				zero_data = cmp_buffer[0];
				// Check all bits are the same
				if ((zero_data == 0) || (zero_data == 0xffffffff)) {
					// Compare the rest of the block against the first element
					for (i = 1; (i < read_size[0] / sizeof(uint32_t)) && (cmp_buffer[i] == zero_data); i++);
					if (i >= read_size[0] / sizeof(uint32_t)) {
						// Block is empty, skip write
						write_size = read_size[0];
						continue;
					}
				}

				// Move the file pointer position back for writing
				li.QuadPart = wb;
				if (!SetFilePointerEx(hPhysicalDrive, li, NULL, FILE_BEGIN)) {
					uprintf("\r\nError: Could not reset position - %s", WindowsErrorString());
					goto out;
				}
				// Throttle read operations
				throttle_fast_zeroing = 15;
			}

			for (i = 1; i <= WRITE_RETRIES; i++) {
				CHECK_FOR_USER_CANCEL;
				s = WriteFile(hPhysicalDrive, buffer, read_size[0], &write_size, NULL);
				if ((s) && (write_size == read_size[0]))
					break;
				if (s)
					uprintf("\r\nWrite error: Wrote %d bytes, expected %d bytes", write_size, read_size[0]);
				else
					uprintf("\r\nWrite error at sector %lld: %s", wb / SelectedDrive.SectorSize, WindowsErrorString());
				if (i < WRITE_RETRIES) {
					li.QuadPart = wb;
					uprintf("Retrying in %d seconds...", WRITE_TIMEOUT / 1000);
					Sleep(WRITE_TIMEOUT);
					if (!SetFilePointerEx(hPhysicalDrive, li, NULL, FILE_BEGIN)) {
						uprintf("Write error: Could not reset position - %s", WindowsErrorString());
						goto out;
					}
				} else {
					ErrorStatus = RUFUS_ERROR(ERROR_WRITE_FAULT);
					goto out;
				}
				Sleep(200);
			}
			if (i > WRITE_RETRIES)
				goto out;
		}
		uprintfs("\r\n");
	} else if (img_report.compression_type != BLED_COMPRESSION_NONE && img_report.compression_type < BLED_COMPRESSION_MAX) {
		uprintf("Writing compressed image:");
		hSourceImage = CreateFileU(image_path, GENERIC_READ, FILE_SHARE_READ, NULL,
			OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
		if (hSourceImage == INVALID_HANDLE_VALUE) {
			uprintf("Could not open image '%s': %s", image_path, WindowsErrorString());
			ErrorStatus = RUFUS_ERROR(ERROR_OPEN_FAILED);
			goto out;
		}
		sec_buf = (uint8_t*)_mm_malloc(SelectedDrive.SectorSize, SelectedDrive.SectorSize);
		if (sec_buf == NULL) {
			ErrorStatus = RUFUS_ERROR(ERROR_NOT_ENOUGH_MEMORY);
			uprintf("Could not allocate disk write buffer");
			goto out;
		}
		assert((uintptr_t)sec_buf % SelectedDrive.SectorSize == 0);
		sec_buf_pos = 0;
		bled_init(256 * KB, uprintf, NULL, sector_write, update_progress, NULL, &ErrorStatus);
		bled_ret = bled_uncompress_with_handles(hSourceImage, hPhysicalDrive, img_report.compression_type);
		bled_exit();
		uprintfs("\r\n");
		if ((bled_ret >= 0) && (sec_buf_pos != 0)) {
			// A disk image that doesn't end up on disk boundary should be a rare
			// enough case, so we dont bother checking the write operation and
			// just issue a notice about it in the log.
			uprintf("Notice: Compressed image data didn't end on block boundary.");
			// Gonna assert that WriteFile() and _write() share the same file offset
			WriteFile(hPhysicalDrive, sec_buf, SelectedDrive.SectorSize, &write_size, NULL);
		}
		safe_mm_free(sec_buf);
		if ((bled_ret < 0) && (SCODE_CODE(ErrorStatus) != ERROR_CANCELLED)) {
			// Unfortunately, different compression backends return different negative error codes
			uprintf("Could not write compressed image: %lld", bled_ret);
			ErrorStatus = RUFUS_ERROR(ERROR_WRITE_FAULT);
			goto out;
		}
	} else {
		assert(img_report.compression_type != IMG_COMPRESSION_FFU);
		// VHD/VHDX require mounting the image first
		if (img_report.compression_type == IMG_COMPRESSION_VHD ||
			img_report.compression_type == IMG_COMPRESSION_VHDX) {
			// Since VHDX images are compressed, we need to obtain the actual size
			vhd_path = VhdMountImageAndGetSize(image_path, &target_size);
			if (vhd_path == NULL || target_size == 0)
				goto out;
		}

		hSourceImage = CreateFileAsync(vhd_path != NULL ? vhd_path : image_path, GENERIC_READ,
			FILE_SHARE_READ, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN);
		if (hSourceImage == NULL) {
			uprintf("Could not open image '%s': %s", image_path, WindowsErrorString());
			ErrorStatus = RUFUS_ERROR(ERROR_OPEN_FAILED);
			goto out;
		}

		// Our buffer size must be a multiple of the sector size and *ALIGNED* to the sector size
		buf_size = ((DD_BUFFER_SIZE + SelectedDrive.SectorSize - 1) / SelectedDrive.SectorSize) * SelectedDrive.SectorSize;
		buffer = (uint8_t*)_mm_malloc(buf_size * NUM_BUFFERS, SelectedDrive.SectorSize);
		if (buffer == NULL) {
			ErrorStatus = RUFUS_ERROR(ERROR_NOT_ENOUGH_MEMORY);
			uprintf("Could not allocate disk write buffer");
			goto out;
		}
		assert((uintptr_t)buffer % SelectedDrive.SectorSize == 0);

		// Start the initial read
		ReadFileAsync(hSourceImage, &buffer[read_bufnum * buf_size], buf_size);

		read_size[proc_bufnum] = 1;	// To avoid early loop exit
		for (wb = 0; read_size[proc_bufnum] != 0; wb += read_size[proc_bufnum]) {
			// 0. Update the progress
			UpdateProgressWithInfo(OP_FORMAT, MSG_261, wb, target_size);
			cur_value = (wb * 80) / target_size;
			for ( ; cur_value > last_value && last_value < 80; last_value++)
				uprintfs("+");

			if (wb >= target_size)
				break;

			// 1. Wait for the current read operation to complete (and update the read size)
			if ((!WaitFileAsync(hSourceImage, DRIVE_ACCESS_TIMEOUT)) ||
				(!GetSizeAsync(hSourceImage, &read_size[read_bufnum]))) {
				uprintf("\r\nRead error: %s", WindowsErrorString());
				ErrorStatus = RUFUS_ERROR(ERROR_READ_FAULT);
				goto out;
			}

			// 2. Update the read size
			// 2a) Don't overflow our projected size (mostly for VHDs)
			if (wb + read_size[read_bufnum] > target_size)
				read_size[read_bufnum] = (DWORD)(target_size - wb);
			// 2b) WriteFile fails unless the size is a multiple of sector size
			if (read_size[read_bufnum] % SelectedDrive.SectorSize != 0)
				read_size[read_bufnum] = ((read_size[read_bufnum] + SelectedDrive.SectorSize - 1) /
					SelectedDrive.SectorSize) * SelectedDrive.SectorSize;

			// 3. Switch to the next reading buffer
			proc_bufnum = read_bufnum;
			read_bufnum = (read_bufnum + 1) % NUM_BUFFERS;

			// 3. Launch the next asynchronous read operation
			ReadFileAsync(hSourceImage, &buffer[read_bufnum * buf_size], buf_size);

			// 4. Synchronously write the current data buffer
			for (i = 1; i <= WRITE_RETRIES; i++) {
				CHECK_FOR_USER_CANCEL;
				s = WriteFile(hPhysicalDrive, &buffer[proc_bufnum * buf_size], read_size[proc_bufnum], &write_size, NULL);
				if ((s) && (write_size == read_size[proc_bufnum]))
					break;
				if (s)
					uprintf("\r\nWrite error: Wrote %d bytes, expected %d bytes", write_size, read_size[proc_bufnum]);
				else
					uprintf("\r\nWrite error at sector %lld: %s", wb / SelectedDrive.SectorSize, WindowsErrorString());
				if (i < WRITE_RETRIES) {
					li.QuadPart = wb;
					uprintf("Retrying in %d seconds...", WRITE_TIMEOUT / 1000);
					Sleep(WRITE_TIMEOUT);
					if (!SetFilePointerEx(hPhysicalDrive, li, NULL, FILE_BEGIN)) {
						uprintf("Write error: Could not reset position - %s", WindowsErrorString());
						goto out;
					}
				} else {
					ErrorStatus = RUFUS_ERROR(ERROR_WRITE_FAULT);
					goto out;
				}
				Sleep(200);
			}
			if (i > WRITE_RETRIES)
				goto out;
		}
		uprintfs("\r\n");
	}
	RefreshDriveLayout(hPhysicalDrive);
	ret = TRUE;
out:
	if (img_report.compression_type != BLED_COMPRESSION_NONE && img_report.compression_type < BLED_COMPRESSION_MAX)
		safe_closehandle(hSourceImage);
	else
		CloseFileAsync(hSourceImage);
	if (vhd_path != NULL)
		VhdUnmountImage();
	safe_mm_free(buffer);
	safe_mm_free(cmp_buffer);
	return ret;
}

/*
 * Standalone thread for the formatting operation
 * According to https://learn.microsoft.com/windows/win32/api/winioctl/ni-winioctl-fsctl_dismount_volume
 * To change a volume file system
 *   Open a volume.
 *   Lock the volume.
 *   Format the volume.
 *   Dismount the volume.
 *   Unlock the volume.
 *   Close the volume handle.
 */
DWORD WINAPI FormatThread(void* param)
{
	int r;
	BOOL ret, use_large_fat32, windows_to_go, actual_lock_drive = lock_drive, write_as_ext = FALSE;
	// Windows 11 and VDS (which I suspect is what fmifs.dll's FormatEx() is now calling behind the scenes)
	// require us to unlock the physical drive to format the drive, else access denied is returned.
	BOOL need_logical = FALSE, must_unlock_physical = (use_vds || WindowsVersion.Version >= WINDOWS_11);
	DWORD cr, DriveIndex = (DWORD)(uintptr_t)param, ClusterSize, Flags;
	HANDLE hPhysicalDrive = INVALID_HANDLE_VALUE;
	HANDLE hLogicalVolume = INVALID_HANDLE_VALUE;
	SYSTEMTIME lt;
	uint8_t *buffer = NULL, extra_partitions = 0;
	char *bb_msg, *volume_name = NULL;
	char drive_name[] = "?:\\";
	char drive_letters[27], fs_name[32], label[64];
	char logfile[MAX_PATH], *userdir;
	char efi_dst[] = "?:\\efi\\boot\\bootx64.efi";
	char kolibri_dst[] = "?:\\MTLD_F32";
	char grub4dos_dst[] = "?:\\grldr";

	use_large_fat32 = (fs_type == FS_FAT32) && ((SelectedDrive.DiskSize > LARGE_FAT32_SIZE) || (force_large_fat32));
	windows_to_go = (image_options & IMOP_WINTOGO) && (boot_type == BT_IMAGE) && HAS_WINTOGO(img_report) &&
		(ComboBox_GetCurItemData(hImageOption) == IMOP_WIN_TO_GO);
	large_drive = (SelectedDrive.DiskSize > (1*TB));
	if (large_drive)
		uprintf("Notice: Large drive detected (may produce short writes)");

	// Find out if we need to add any extra partitions
	extra_partitions = 0;
	if ((boot_type == BT_IMAGE) && !write_as_image && HAS_PERSISTENCE(img_report) && persistence_size)
		extra_partitions |= XP_PERSISTENCE;
	// According to Microsoft, every GPT disk (we RUN Windows from) must have an MSR due to not having hidden sectors
	// https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/windows-and-gpt-faq#disks-that-require-an-msr
	if ((windows_to_go) && (target_type == TT_UEFI) && (partition_type == PARTITION_STYLE_GPT))
		extra_partitions |= XP_ESP | XP_MSR;
	// If we have a bootable image with UEFI bootloaders and the target file system is NTFS or exFAT
	// or the UEFI:NTFS option is selected, we add the UEFI:NTFS partition...
	else if ((((boot_type == BT_IMAGE) && IS_EFI_BOOTABLE(img_report)) && ((fs_type == FS_NTFS) || (fs_type == FS_EXFAT))) ||
			 (boot_type == BT_UEFI_NTFS)) {
		extra_partitions |= XP_UEFI_NTFS;
		// ...but only if we're not dealing with a Windows image in installer mode with target
		//  system set to BIOS and without dual BIOS+UEFI boot enabled.
		if ((boot_type == BT_IMAGE) && HAS_BOOTMGR_BIOS(img_report) && (!windows_to_go) &&
			(target_type == TT_BIOS) && (!allow_dual_uefi_bios))
			extra_partitions &= ~XP_UEFI_NTFS;
	}
	if (IsChecked(IDC_OLD_BIOS_FIXES))
		extra_partitions |= XP_COMPAT;

	// On pre 1703 platforms (and even on later ones), anything with ext2/ext3 doesn't sit
	// too well with Windows. Same with ESPs. Relaxing our locking rules seems to help...
	if ((extra_partitions & (XP_ESP | XP_PERSISTENCE)) || IS_EXT(fs_type))
		actual_lock_drive = FALSE;
	// Windows 11 is a lot more proactive in locking ESPs and MSRs than previous versions
	// were, meaning that we also can't lock the drive without incurring errors...
	if ((WindowsVersion.Version >= WINDOWS_11) && extra_partitions)
		actual_lock_drive = FALSE;
	// Fixed drives + ext2/ext3 don't play nice and require the same handling as ESPs
	write_as_ext = IS_EXT(fs_type) && (GetDriveTypeFromIndex(DriveIndex) == DRIVE_FIXED);

	PrintInfoDebug(0, MSG_225);
	hPhysicalDrive = GetPhysicalHandle(DriveIndex, actual_lock_drive, FALSE, !actual_lock_drive);
	if (hPhysicalDrive == INVALID_HANDLE_VALUE) {
		ErrorStatus = RUFUS_ERROR(ERROR_OPEN_FAILED);
		goto out;
	}

	// At this stage we have both a handle and a lock to the physical drive
	if (!GetDriveLetters(DriveIndex, drive_letters)) {
		uprintf("Failed to get a drive letter");
		ErrorStatus = RUFUS_ERROR(APPERR(ERROR_CANT_ASSIGN_LETTER));
		goto out;
	}

	// Unassign all drives letters
	drive_name[0] = RemoveDriveLetters(DriveIndex, TRUE, FALSE);
	if (drive_name[0] == 0) {
		uprintf("Unable to find a drive letter to use");
		ErrorStatus = RUFUS_ERROR(APPERR(ERROR_CANT_ASSIGN_LETTER));
		goto out;
	}
	uprintf("Will use '%c:' as volume mountpoint", toupper(drive_name[0]));

	// It kind of blows, but we have to relinquish access to the physical drive
	// for VDS to be able to delete the partitions that reside on it...
	safe_unlockclose(hPhysicalDrive);
	PrintInfo(0, MSG_239, lmprintf(MSG_307));
	if (!is_vds_available || !DeletePartition(DriveIndex, 0, TRUE)) {
		uprintf("Warning: Could not delete partition(s): %s", is_vds_available ? WindowsErrorString() : "VDS is not available");
		SetLastError(ErrorStatus);
		ErrorStatus = 0;
		// If we couldn't delete partitions, Windows give us trouble unless we
		// request access to the logical drive. Don't ask me why!
		need_logical = TRUE;
		// Also, since we couldn't clean the disk, we need to disable drive locking
		actual_lock_drive = FALSE;
	}

	// An extra refresh of the (now empty) partition data here appears to be helpful
	GetDrivePartitionData(SelectedDrive.DeviceNumber, fs_name, sizeof(fs_name), TRUE);

	// Now get RW access to the physical drive
	hPhysicalDrive = GetPhysicalHandle(DriveIndex, actual_lock_drive, TRUE, !actual_lock_drive);
	if (hPhysicalDrive == INVALID_HANDLE_VALUE) {
		ErrorStatus = RUFUS_ERROR(ERROR_OPEN_FAILED);
		goto out;
	}
	RefreshDriveLayout(hPhysicalDrive);

	// If we write an image that contains an ESP, Windows forcibly reassigns/removes the target
	// drive, which causes a write error. To work around this, we must lock the logical drive.
	// Also need to lock logical drive if we couldn't delete partitions, to keep Windows happy...
	if (((boot_type == BT_IMAGE) && write_as_image) || (need_logical)) {
		uprintf("Requesting logical volume handle...");
		hLogicalVolume = GetLogicalHandle(DriveIndex, 0, TRUE, FALSE, !actual_lock_drive);
		if (hLogicalVolume == INVALID_HANDLE_VALUE) {
			uprintf("Could not access logical volume");
			ErrorStatus = RUFUS_ERROR(ERROR_OPEN_FAILED);
			goto out;
		// If the call succeeds (and we don't get a NULL logical handle as returned for
		// unpartitioned drives), try to unmount the volume.
		} else if ((hLogicalVolume != NULL) && (!UnmountVolume(hLogicalVolume))) {
			uprintf("Trying to continue regardless...");
		}
	}
	CHECK_FOR_USER_CANCEL;

	if (!zero_drive && !write_as_image) {
		PrintInfoDebug(0, MSG_226);
		AnalyzeMBR(hPhysicalDrive, "Drive", FALSE);
		UpdateProgress(OP_ANALYZE_MBR, -1.0f);
	}

	if (zero_drive) {
		WriteDrive(hPhysicalDrive, TRUE);
		goto out;
	}

	// Zap partition records. This helps prevent access errors.
	// Note, Microsoft's way of cleaning partitions (IOCTL_DISK_CREATE_DISK, which is what we apply
	// in InitializeDisk) is *NOT ENOUGH* to reset a disk and can render it inoperable for partitioning
	// or formatting under Windows. See https://github.com/pbatard/rufus/issues/759 for details.
	if ((boot_type != BT_IMAGE) || (img_report.is_iso && !write_as_image)) {
		if ((!ClearMBRGPT(hPhysicalDrive, SelectedDrive.DiskSize, SelectedDrive.SectorSize, use_large_fat32)) ||
			(!InitializeDisk(hPhysicalDrive))) {
			uprintf("Could not reset partitions");
			ErrorStatus = (LastWriteError != 0) ? LastWriteError : RUFUS_ERROR(ERROR_PARTITION_FAILURE);
			goto out;
		}
	}

	if (IsChecked(IDC_BAD_BLOCKS)) {
		do {
			FILE* log_fd;
			int sel = ComboBox_GetCurSel(hNBPasses);
			// create a log file for bad blocks report. Since %USERPROFILE% may
			// have localized characters, we use the UTF-8 API.
			userdir = getenvU("USERPROFILE");
			static_strcpy(logfile, userdir);
			safe_free(userdir);
			GetLocalTime(&lt);
			safe_sprintf(&logfile[strlen(logfile)], sizeof(logfile) - strlen(logfile) - 1,
				"\\rufus_%04d%02d%02d_%02d%02d%02d.log",
				lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute, lt.wSecond);
			log_fd = fopenU(logfile, "w+");
			if (log_fd == NULL) {
				uprintf("Error: Could not create log file for bad blocks check");
				goto out;
			} else {
				fprintf(log_fd, APPLICATION_NAME " bad blocks check started on: %04d.%02d.%02d %02d:%02d:%02d",
				lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute, lt.wSecond);
				fflush(log_fd);
			}

			if (!BadBlocks(hPhysicalDrive, SelectedDrive.DiskSize, (sel >= 2) ? 4 : sel +1, sel, &report, log_fd)) {
				uprintf("Bad blocks: Check failed.");
				if (!IS_ERROR(ErrorStatus))
					ErrorStatus = RUFUS_ERROR(APPERR(ERROR_BADBLOCKS_FAILURE));
				ClearMBRGPT(hPhysicalDrive, SelectedDrive.DiskSize, SelectedDrive.SectorSize, FALSE);
				fclose(log_fd);
				DeleteFileU(logfile);
				goto out;
			}
			uprintf("Bad Blocks: Check completed, %d bad block%s found. (%d/%d/%d errors)",
				report.bb_count, (report.bb_count==1)?"":"s",
				report.num_read_errors, report.num_write_errors, report.num_corruption_errors);
			r = IDOK;
			if (report.bb_count) {
				bb_msg = lmprintf(MSG_011, report.bb_count, report.num_read_errors, report.num_write_errors,
					report.num_corruption_errors);
				fprintf(log_fd, "%s", bb_msg);
				GetLocalTime(&lt);
				fprintf(log_fd, APPLICATION_NAME " bad blocks check ended on: %04d.%02d.%02d %02d:%02d:%02d",
				lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute, lt.wSecond);
				fclose(log_fd);
				r = MessageBoxExU(hMainDialog, lmprintf(MSG_012, bb_msg, logfile),
					lmprintf(MSG_010), MB_ABORTRETRYIGNORE | MB_ICONWARNING | MB_IS_RTL, selected_langid);
			} else {
				// We didn't get any errors => delete the log file
				fclose(log_fd);
				DeleteFileU(logfile);
			}
		} while (r == IDRETRY);
		if (r == IDABORT) {
			ErrorStatus = RUFUS_ERROR(ERROR_CANCELLED);
			goto out;
		}

		// Especially after destructive badblocks test, you must zero the MBR/GPT completely
		// before repartitioning. Else, all kind of bad things happen.
		if (!ClearMBRGPT(hPhysicalDrive, SelectedDrive.DiskSize, SelectedDrive.SectorSize, use_large_fat32)) {
			uprintf("unable to zero MBR/GPT");
			if (!IS_ERROR(ErrorStatus))
				ErrorStatus = RUFUS_ERROR(ERROR_WRITE_FAULT);
			goto out;
		}
	}

	// Write an image file
	if ((boot_type == BT_IMAGE) && write_as_image) {
		// Special case for FFU images
		if (img_report.compression_type == IMG_COMPRESSION_FFU) {
			char cmd[MAX_PATH + 128], *physical = NULL;
			// Should have been filtered out beforehand
			assert(has_ffu_support);
			safe_unlockclose(hPhysicalDrive);
			physical = GetPhysicalName(SelectedDrive.DeviceNumber);
			static_sprintf(cmd, "dism /Apply-Ffu /ApplyDrive:%s /ImageFile:\"%s\"", physical, image_path);
			safe_free(physical);
			uprintf("Running command: '%s", cmd);
			cr = RunCommandWithProgress(cmd, sysnative_dir, TRUE, MSG_261);
			if (cr != 0 && !IS_ERROR(ErrorStatus)) {
				SetLastError(cr);
				uprintf("Failed to apply FFU image: %s", WindowsErrorString());
				ErrorStatus = RUFUS_ERROR(SCODE_CODE(cr));
			}
		} else {
			WriteDrive(hPhysicalDrive, FALSE);
		}
		goto out;
	}

	UpdateProgress(OP_ZERO_MBR, -1.0f);
	CHECK_FOR_USER_CANCEL;

	if (!CreatePartition(hPhysicalDrive, partition_type, fs_type, (partition_type == PARTITION_STYLE_MBR)
		&& (target_type == TT_UEFI), extra_partitions)) {
		ErrorStatus = (LastWriteError != 0) ? LastWriteError : RUFUS_ERROR(ERROR_PARTITION_FAILURE);
		goto out;
	}
	UpdateProgress(OP_PARTITION, -1.0f);

	// Close the (unmounted) volume before formatting
	if ((hLogicalVolume != NULL) && (hLogicalVolume != INVALID_HANDLE_VALUE)) {
		PrintInfoDebug(0, MSG_227);
		if (!CloseHandle(hLogicalVolume)) {
			hLogicalVolume = INVALID_HANDLE_VALUE;
			uprintf("Could not close volume: %s", WindowsErrorString());
			ErrorStatus = RUFUS_ERROR(ERROR_ACCESS_DENIED);
			goto out;
		}
	}
	hLogicalVolume = INVALID_HANDLE_VALUE;

	if (must_unlock_physical)
		safe_unlockclose(hPhysicalDrive);

	if (use_vds) {
		uprintf("Refreshing drive layout...");
		// Note: This may leave the device disabled on re-plug or reboot
		// so only do this for the experimental VDS path for now...
		cr = CycleDevice(ComboBox_GetCurSel(hDeviceList));
		if (cr == ERROR_DEVICE_REINITIALIZATION_NEEDED) {
			uprintf("Zombie device detected, trying again...");
			Sleep(1000);
			cr = CycleDevice(ComboBox_GetCurSel(hDeviceList));
		}
		if (cr == 0)
			uprintf("Successfully cycled device");
		else
			uprintf("Cycling device failed!");
		RefreshLayout(DriveIndex);
	}

	// Wait for the logical drive we just created to appear
	uprintf("Waiting for logical drive to reappear...");
	Sleep(200);
	if (write_as_esp || write_as_ext) {
		// Can't format ESPs or ext2/ext3 partitions unless we mount them ourselves
		volume_name = AltMountVolume(DriveIndex, SelectedDrive.Partition[partition_index[PI_MAIN]].Offset, FALSE);
		if (volume_name == NULL) {
			ErrorStatus = RUFUS_ERROR(APPERR(ERROR_CANT_ASSIGN_LETTER));
			goto out;
		}
	} else {
		if (!WaitForLogical(DriveIndex, SelectedDrive.Partition[partition_index[PI_MAIN]].Offset)) {
			uprintf("Logical drive was not found - aborting");
			if (!IS_ERROR(ErrorStatus))
				ErrorStatus = RUFUS_ERROR(ERROR_TIMEOUT);
			goto out;
		}
	}
	CHECK_FOR_USER_CANCEL;

	// Format Casper partition if required. Do it before we format anything with
	// a file system that Windows will recognize, to avoid concurrent access.
	if (extra_partitions & XP_PERSISTENCE) {
		uint32_t ext_version = ReadSetting32(SETTING_USE_EXT_VERSION);
		if ((ext_version < 2) || (ext_version > 4))
			ext_version = 3;
		uprintf("Using %s-like method to enable persistence", img_report.uses_casper ? "Ubuntu" : "Debian");
		if (!FormatPartition(DriveIndex, SelectedDrive.Partition[partition_index[PI_CASPER]].Offset, 0, FS_EXT2 + (ext_version - 2),
			img_report.uses_casper ? "casper-rw" : "persistence",
			(img_report.uses_casper ? 0 : FP_CREATE_PERSISTENCE_CONF) |
			(IsChecked(IDC_QUICK_FORMAT) ? FP_QUICK : 0))) {
			if (!IS_ERROR(ErrorStatus))
				ErrorStatus = RUFUS_ERROR(ERROR_WRITE_FAULT);
			goto out;
		}
	}

	GetWindowTextU(hLabel, label, sizeof(label));
	if (fs_type < FS_EXT2)
		ToValidLabel(label, (fs_type == FS_FAT16) || (fs_type == FS_FAT32) || (fs_type == FS_EXFAT));
	ClusterSize = (DWORD)ComboBox_GetCurItemData(hClusterSize);
	if ((ClusterSize < 0x200) || (write_as_esp))
		ClusterSize = 0;	// 0 = default cluster size

	Flags = FP_FORCE;
	if (IsChecked(IDC_QUICK_FORMAT))
		Flags |= FP_QUICK;
	if ((fs_type == FS_NTFS) && (enable_ntfs_compression))
		Flags |= FP_COMPRESSION;
	if (write_as_esp)
		Flags |= FP_LARGE_FAT32;

	ret = FormatPartition(DriveIndex, SelectedDrive.Partition[partition_index[PI_MAIN]].Offset, ClusterSize, fs_type, label, Flags);
	if (!ret) {
		// Error will be set by FormatPartition() in ErrorStatus
		uprintf("Format error: %s", StrError(ErrorStatus, TRUE));
		goto out;
	}

	if (must_unlock_physical) {
		// Get RW access back to the physical drive...
		hPhysicalDrive = GetPhysicalHandle(DriveIndex, actual_lock_drive, TRUE, !actual_lock_drive);
		if (hPhysicalDrive == INVALID_HANDLE_VALUE) {
			ErrorStatus = RUFUS_ERROR(ERROR_OPEN_FAILED);
			goto out;
		}
	}

	// Thanks to Microsoft, we must fix the MBR AFTER the drive has been formatted
	if ((partition_type == PARTITION_STYLE_MBR) || ((boot_type != BT_NON_BOOTABLE) && (partition_type == PARTITION_STYLE_GPT))) {
		PrintInfoDebug(0, MSG_228);	// "Writing master boot record..."
		if ((!WriteMBR(hPhysicalDrive)) || (!WriteSBR(hPhysicalDrive))) {
			if (!IS_ERROR(ErrorStatus))
				ErrorStatus = RUFUS_ERROR(ERROR_WRITE_FAULT);
			goto out;
		}
		UpdateProgress(OP_FIX_MBR, -1.0f);
	}
	Sleep(200);

	if (!write_as_esp && !write_as_ext) {
		WaitForLogical(DriveIndex, 0);
		// Try to continue
		CHECK_FOR_USER_CANCEL;

		volume_name = GetLogicalName(DriveIndex, SelectedDrive.Partition[partition_index[PI_MAIN]].Offset, TRUE, TRUE);
		if (volume_name == NULL) {
			uprintf("Could not get volume name");
			ErrorStatus = RUFUS_ERROR(ERROR_NO_VOLUME_ID);
			goto out;
		}
		uprintf("Found volume %s", volume_name);


		// Windows is really finicky with regards to reassigning drive letters even after
		// we forcibly removed them, so add yet another explicit call to RemoveDriveLetters()
		RemoveDriveLetters(DriveIndex, FALSE, TRUE);
		if (!MountVolume(drive_name, volume_name)) {
			uprintf("Could not remount %s as %c: %s", volume_name, toupper(drive_name[0]), WindowsErrorString());
			ErrorStatus = RUFUS_ERROR(APPERR(ERROR_CANT_MOUNT_VOLUME));
			goto out;
		}
		CHECK_FOR_USER_CANCEL;

		// Disable file indexing, unless it was force-enabled by the user
		if ((!enable_file_indexing) && ((fs_type == FS_NTFS) || (fs_type == FS_UDF) || (fs_type == FS_REFS))) {
			uprintf("Disabling file indexing...");
			if (!SetFileAttributesA(volume_name, FILE_ATTRIBUTE_NOT_CONTENT_INDEXED))
				uprintf("Could not disable file indexing: %s", WindowsErrorString());
		}
	}

	// Refresh the drive label - This is needed as Windows may have altered it from
	// the name we proposed, and we require an exact label, to patch config files.
	if ((fs_type < FS_EXT2) && !GetVolumeInformationU(drive_name, img_report.usb_label,
		ARRAYSIZE(img_report.usb_label), NULL, NULL, NULL, NULL, 0)) {
		uprintf("Warning: Failed to refresh label: %s", WindowsErrorString());
	} else if (IS_EXT(fs_type)) {
		const char* ext_label = GetExtFsLabel(DriveIndex, 0);
		if (ext_label != NULL)
			static_strcpy(img_report.usb_label, label);
	}

	if (boot_type != BT_NON_BOOTABLE) {
		if (boot_type == BT_UEFI_NTFS) {
			// All good
		} else if (target_type == TT_UEFI) {
			// For once, no need to do anything - just check our sanity
			assert((boot_type == BT_IMAGE) && IS_EFI_BOOTABLE(img_report) && (fs_type <= FS_NTFS));
			if ( (boot_type != BT_IMAGE) || !IS_EFI_BOOTABLE(img_report) || (fs_type > FS_NTFS) ) {
				ErrorStatus = RUFUS_ERROR(ERROR_INSTALL_FAILURE);
				goto out;
			}
		} else if ( (boot_type == BT_SYSLINUX_V4) || (boot_type == BT_SYSLINUX_V6) ||
			((boot_type == BT_IMAGE) && (HAS_SYSLINUX(img_report) || HAS_REACTOS(img_report)) &&
				(!HAS_WINDOWS(img_report) || !allow_dual_uefi_bios)) ) {
			if (!InstallSyslinux(DriveIndex, drive_name[0], fs_type)) {
				ErrorStatus = RUFUS_ERROR(ERROR_INSTALL_FAILURE);
				goto out;
			}
		} else {
			// We still have a lock, which we need to modify the volume boot record
			// => no need to reacquire the lock...
			hLogicalVolume = GetLogicalHandle(DriveIndex, SelectedDrive.Partition[partition_index[PI_MAIN]].Offset, FALSE, TRUE, FALSE);
			if ((hLogicalVolume == INVALID_HANDLE_VALUE) || (hLogicalVolume == NULL)) {
				uprintf("Could not re-mount volume for partition boot record access");
				ErrorStatus = RUFUS_ERROR(ERROR_OPEN_FAILED);
				goto out;
			}
			// NB: if you unmount the logical volume here, XP will report error:
			// [0x00000456] The media in the drive may have changed
			PrintInfoDebug(0, MSG_229);
			if (!WritePBR(hLogicalVolume)) {
				if (!IS_ERROR(ErrorStatus))
					ErrorStatus = RUFUS_ERROR(ERROR_WRITE_FAULT);
				goto out;
			}
			// We must close and unlock the volume to write files to it
			safe_unlockclose(hLogicalVolume);
		}
	} else {
		if (IsChecked(IDC_EXTENDED_LABEL))
			SetAutorun(drive_name);
	}
	CHECK_FOR_USER_CANCEL;

	// We issue a complete remount of the filesystem on account of:
	// - Ensuring the file explorer properly detects that the volume was updated
	// - Ensuring that an NTFS system will be reparsed so that it becomes bootable
	if (!RemountVolume(drive_name, FALSE))
		goto out;
	CHECK_FOR_USER_CANCEL;

	if (boot_type != BT_NON_BOOTABLE) {
		if ((boot_type == BT_MSDOS) || (boot_type == BT_FREEDOS)) {
			UpdateProgress(OP_FILE_COPY, -1.0f);
			PrintInfoDebug(0, MSG_230);
			if (!ExtractDOS(drive_name)) {
				if (!IS_ERROR(ErrorStatus))
					ErrorStatus = RUFUS_ERROR(ERROR_CANNOT_COPY);
				goto out;
			}
		} else if (boot_type == BT_GRUB4DOS) {
			grub4dos_dst[0] = drive_name[0];
			IGNORE_RETVAL(_chdirU(app_data_dir));
			uprintf("Installing: %s (Grub4DOS loader) %s", grub4dos_dst,
				IsFileInDB(FILES_DIR "\\grub4dos-" GRUB4DOS_VERSION "\\grldr")?"✓":"✗");
			if (!CopyFileU(FILES_DIR "\\grub4dos-" GRUB4DOS_VERSION "\\grldr", grub4dos_dst, FALSE))
				uprintf("Failed to copy file: %s", WindowsErrorString());
		} else if ((boot_type == BT_IMAGE) && (image_path != NULL) && (img_report.is_iso || img_report.is_windows_img)) {
			UpdateProgress(OP_FILE_COPY, 0.0f);
			drive_name[2] = 0;	// Ensure our drive is something like 'D:'
			if (windows_to_go) {
				PrintInfoDebug(0, MSG_268);
				if (!SetupWinToGo(DriveIndex, drive_name, (extra_partitions & XP_ESP))) {
					if (!IS_ERROR(ErrorStatus))
						ErrorStatus = RUFUS_ERROR(APPERR(ERROR_ISO_EXTRACT));
					goto out;
				}
				if (unattend_xml_path != NULL) {
					if (!ApplyWindowsCustomization(drive_name[0], unattend_xml_flags | UNATTEND_WINDOWS_TO_GO))
						ErrorStatus = RUFUS_ERROR(APPERR(ERROR_CANT_PATCH));
				}
			} else {
				assert(!img_report.is_windows_img);
				if (!ExtractISO(image_path, drive_name, FALSE)) {
					if (!IS_ERROR(ErrorStatus))
						ErrorStatus = RUFUS_ERROR(APPERR(ERROR_ISO_EXTRACT));
					goto out;
				}
				if (HAS_KOLIBRIOS(img_report)) {
					kolibri_dst[0] = drive_name[0];
					uprintf("Installing: %s (KolibriOS loader)", kolibri_dst);
					if (ExtractISOFile(image_path, "HD_Load/USB_Boot/MTLD_F32", kolibri_dst,
						FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM) == 0) {
						uprintf("Warning: loader installation failed - KolibriOS will not boot!");
					}
				}
				// EFI mode selected, with no 'boot###.efi' but Windows 7 x64's 'bootmgr.efi' (bit #0)
				if (((target_type == TT_UEFI) || allow_dual_uefi_bios) && HAS_WIN7_EFI(img_report)) {
					PrintInfo(0, MSG_232, lmprintf(MSG_307));
					uprintf("Win7 EFI boot setup");
					img_report.wininst_path[0][0] = drive_name[0];
					efi_dst[0] = drive_name[0];
					efi_dst[sizeof(efi_dst) - sizeof("\\bootx64.efi")] = 0;
					if (!CreateDirectoryA(efi_dst, 0)) {
						uprintf("Could not create directory '%s': %s", efi_dst, WindowsErrorString());
						ErrorStatus = RUFUS_ERROR(APPERR(ERROR_CANT_PATCH));
					} else {
						efi_dst[sizeof(efi_dst) - sizeof("\\bootx64.efi")] = '\\';
						if (!WimExtractFile(img_report.wininst_path[0], 1, "Windows\\Boot\\EFI\\bootmgfw.efi", efi_dst, FALSE)) {
							uprintf("Failed to setup Win7 EFI boot");
							ErrorStatus = RUFUS_ERROR(APPERR(ERROR_CANT_PATCH));
						}
					}
				}
				CopySKUSiPolicy(drive_name);
				if ( (target_type == TT_BIOS) && HAS_WINPE(img_report) ) {
					// Apply WinPE fixup
					if (!SetupWinPE(drive_name[0]))
						ErrorStatus = RUFUS_ERROR(APPERR(ERROR_CANT_PATCH));
				}
				if (unattend_xml_path != NULL) {
					if (!ApplyWindowsCustomization(drive_name[0], unattend_xml_flags))
						ErrorStatus = RUFUS_ERROR(APPERR(ERROR_CANT_PATCH));
				}
			}
		}

		UpdateProgress(OP_FINALIZE, -1.0f);
		PrintInfoDebug(0, MSG_233);
		if ((boot_type == BT_IMAGE) && (image_path != NULL) && (img_report.is_iso) && (!windows_to_go))
			UpdateMD5Sum(drive_name, md5sum_name[img_report.has_md5sum ? img_report.has_md5sum - 1 : 0]);
		if (IsChecked(IDC_EXTENDED_LABEL))
			SetAutorun(drive_name);
		// Issue another complete remount before we exit, to ensure we're clean
		RemountVolume(drive_name, TRUE);
		// NTFS fixup (WinPE/AIK images don't seem to boot without an extra checkdisk)
		if ((boot_type == BT_IMAGE) && (img_report.is_iso) && (fs_type == FS_NTFS)) {
			// Try to ensure that all messages from Checkdisk will be in English
			if (PRIMARYLANGID(GetThreadUILanguage()) != LANG_ENGLISH) {
				SetThreadUILanguage(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US));
				if (PRIMARYLANGID(GetThreadUILanguage()) != LANG_ENGLISH)
					uprintf("Note: CheckDisk messages may be localized");
			}
			CheckDisk(drive_name[0]);
			UpdateProgress(OP_FINALIZE, -1.0f);
		}
	}

	// Copy any additonal files from an optional zip archive selected by the user
	if (!IS_ERROR(ErrorStatus)) {
		UpdateProgress(OP_EXTRACT_ZIP, 0.0f);
		drive_name[2] = 0;
		if (archive_path != NULL && fs_type < FS_EXT2 && !ExtractZip(archive_path, drive_name) && !IS_ERROR(ErrorStatus))
			uprintf("Warning: Could not copy additional files");
	}

out:
	if ((write_as_esp || write_as_ext) && volume_name != NULL)
		AltUnmountVolume(volume_name, TRUE);
	else
		safe_free(volume_name);
	safe_free(buffer);
	safe_unlockclose(hLogicalVolume);
	safe_unlockclose(hPhysicalDrive);	// This can take a while
	if ((boot_type == BT_IMAGE) && write_as_image) {
		PrintInfo(0, MSG_320, lmprintf(MSG_307));
		Sleep(200);
		VdsRescan(VDS_RESCAN_REFRESH, 0, TRUE);
		// Trying to mount accessible partitions after writing an image leads to the
		// creation of the infamous 'System Volume Information' folder on ESPs, which
		// in turn leads to checksum errors for Ubuntu's boot/grub/efi.img (that maps
		// to the Ubuntu ESP). So we only call the code below if there are no ESPs or
		// if we're running a Ventoy image.
		if ((GetEspOffset(DriveIndex) == 0) || (img_report.compression_type == BLED_COMPRESSION_VTSI)) {
			WaitForLogical(DriveIndex, 0);
			if (GetDrivePartitionData(SelectedDrive.DeviceNumber, fs_name, sizeof(fs_name), TRUE)) {
				volume_name = GetLogicalName(DriveIndex, 0, TRUE, TRUE);
				if ((volume_name != NULL) && (MountVolume(drive_name, volume_name)))
					uprintf("Remounted %s as %c:", volume_name, toupper(drive_name[0]));
			}
		}
	}
	if (IS_ERROR(ErrorStatus)) {
		volume_name = GetLogicalName(DriveIndex, SelectedDrive.Partition[partition_index[PI_MAIN]].Offset, TRUE, TRUE);
		if (volume_name != NULL) {
			if (MountVolume(drive_name, volume_name))
				uprintf("Re-mounted volume as %c: after error", toupper(drive_name[0]));
			free(volume_name);
		}
	}
	PostMessage(hMainDialog, UM_FORMAT_COMPLETED, (WPARAM)TRUE, 0);
	ExitThread(0);
}
