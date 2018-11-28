/*
 * Rufus: The Reliable USB Formatting Utility
 * Device detection and enumeration
 * Copyright © 2014-2018 Pete Batard <pete@akeo.ie>
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

/* Memory leaks detection - define _CRTDBG_MAP_ALLOC as preprocessor macro */
#ifdef _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <commctrl.h>
#include <setupapi.h>
#include <assert.h>

#include "rufus.h"
#include "missing.h"
#include "resource.h"
#include "msapi_utf8.h"
#include "localization.h"

#include "drive.h"
#include "dev.h"

extern StrArray DriveID, DriveLabel, DriveHub;
extern uint32_t DrivePort[MAX_DRIVES];
extern BOOL enable_HDDs, use_fake_units, enable_vmdk, usb_debug, list_non_usb_removable_drives;

/*
 * Get the VID, PID and current device speed
 */
static BOOL GetUSBProperties(char* parent_path, char* device_id, usb_device_props* props)
{
	BOOL r = FALSE;
	CONFIGRET cr;
	HANDLE handle = INVALID_HANDLE_VALUE;
	DWORD size;
	DEVINST device_inst;
	USB_NODE_CONNECTION_INFORMATION_EX conn_info;
	USB_NODE_CONNECTION_INFORMATION_EX_V2 conn_info_v2;
	PF_INIT(CM_Get_DevNode_Registry_PropertyA, Cfgmgr32);

	if ((parent_path == NULL) || (device_id == NULL) || (props == NULL) ||
		(pfCM_Get_DevNode_Registry_PropertyA == NULL)) {
		goto out;
	}

	cr = CM_Locate_DevNodeA(&device_inst, device_id, 0);
	if (cr != CR_SUCCESS) {
		uprintf("Could not get device instance handle for '%s': CR error %d", device_id, cr);
		goto out;
	}

	props->port = 0;
	size = sizeof(props->port);
	cr = pfCM_Get_DevNode_Registry_PropertyA(device_inst, CM_DRP_ADDRESS, NULL, (PVOID)&props->port, &size, 0);
	if (cr != CR_SUCCESS) {
		uprintf("Could not get port for '%s': CR error %d", device_id, cr);
		goto out;
	}

	handle = CreateFileA(parent_path, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
	if (handle == INVALID_HANDLE_VALUE) {
		uprintf("Could not open hub %s: %s", parent_path, WindowsErrorString());
		goto out;
	}
	size = sizeof(conn_info);
	memset(&conn_info, 0, size);
	conn_info.ConnectionIndex = (ULONG)props->port;
	// coverity[tainted_data_argument]
	if (!DeviceIoControl(handle, IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX, &conn_info, size, &conn_info, size, &size, NULL)) {
		uprintf("Could not get node connection information for '%s': %s", device_id, WindowsErrorString());
		goto out;
	}

	// Some poorly written proprietary Windows 7 USB 3.0 controller drivers (<cough>ASMedia<cough>)
	// have a screwed up implementation of IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX that succeeds
	// but returns zeroed data => Add a workaround so that we don't lose our VID:PID...
	if ((conn_info.DeviceDescriptor.idVendor != 0) || (conn_info.DeviceDescriptor.idProduct != 0)) {
		props->vid = conn_info.DeviceDescriptor.idVendor;
		props->pid = conn_info.DeviceDescriptor.idProduct;
		props->speed = conn_info.Speed + 1;
		r = TRUE;
	}

	// In their great wisdom, Microsoft decided to BREAK the USB speed report between Windows 7 and Windows 8
	if (nWindowsVersion >= WINDOWS_8) {
		size = sizeof(conn_info_v2);
		memset(&conn_info_v2, 0, size);
		conn_info_v2.ConnectionIndex = (ULONG)props->port;
		conn_info_v2.Length = size;
		conn_info_v2.SupportedUsbProtocols.Usb300 = 1;
		if (!DeviceIoControl(handle, IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2, &conn_info_v2, size, &conn_info_v2, size, &size, NULL)) {
			uprintf("Could not get node connection information (V2) for device '%s': %s", device_id, WindowsErrorString());
		} else if (conn_info_v2.Flags.DeviceIsOperatingAtSuperSpeedOrHigher) {
			props->speed = USB_SPEED_SUPER_OR_LATER;
		} else if (conn_info_v2.Flags.DeviceIsSuperSpeedCapableOrHigher) {
			props->is_LowerSpeed = TRUE;
		}
	}

out:
	safe_closehandle(handle);
	return r;
}

/*
 * Cycle port (reset) the selected device
 */
BOOL ResetDevice(int index)
{
	static uint64_t LastReset = 0;
	BOOL r = FALSE;
	HANDLE handle = INVALID_HANDLE_VALUE;
	DWORD size;
	USB_CYCLE_PORT_PARAMS cycle_port;

	// Wait at least 10 secs between resets
	if (GetTickCount64() < LastReset + 10000ULL) {
		uprintf("You must wait at least 10 seconds before trying to reset a device");
		return FALSE;
	}

	if (DriveHub.String[index] == NULL) {
		uprintf("The device you are trying to reset does not appear to be a USB device...");
		return FALSE;
	}

	LastReset = GetTickCount64();

	handle = CreateFileA(DriveHub.String[index], GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
	if (handle == INVALID_HANDLE_VALUE) {
		uprintf("Could not open %s: %s", DriveHub.String[index], WindowsErrorString());
		goto out;
	}

	size = sizeof(cycle_port);
	memset(&cycle_port, 0, size);
	cycle_port.ConnectionIndex = DrivePort[index];
	uprintf("Cycling port %d (reset) on %s", DrivePort[index], DriveHub.String[index]);
	// As per https://msdn.microsoft.com/en-us/library/windows/hardware/ff537340.aspx
	// IOCTL_USB_HUB_CYCLE_PORT is not supported on Windows 7, Windows Vista, and Windows Server 2008
	if (!DeviceIoControl(handle, IOCTL_USB_HUB_CYCLE_PORT, &cycle_port, size, &cycle_port, size, &size, NULL)) {
		uprintf("  Failed to cycle port: %s", WindowsErrorString());
		goto out;
	}
	uprintf("Please wait for the device to re-appear...");
	r = TRUE;

out:
	safe_closehandle(handle);
	return r;
}

static __inline BOOL IsVHD(const char* buffer)
{
	int i;
	// List of the Hardware IDs of the VHD devices we know
	const char* vhd_name[] = {
		"Arsenal_________Virtual_",
		"KernSafeVirtual_________",
		"Msft____Virtual_Disk____",
		"VMware__VMware_Virtual_S"	// Enabled through a cheat mode, as this lists primary disks on VMWare instances
	};

	for (i = 0; i < (int)(ARRAYSIZE(vhd_name)-(enable_vmdk?0:1)); i++)
		if (safe_strstr(buffer, vhd_name[i]) != NULL)
			return TRUE;
	return FALSE;
}

static __inline BOOL IsRemovable(const char* buffer)
{
	switch (*((DWORD*)buffer)) {
	case CM_REMOVAL_POLICY_EXPECT_SURPRISE_REMOVAL:
	case CM_REMOVAL_POLICY_EXPECT_ORDERLY_REMOVAL:
		return TRUE;
	default:
		return FALSE;
	}
}

static __inline void ToUpper(char* str)
{
	size_t i;
	for (i = 0; i < safe_strlen(str); i++)
		str[i] = (char)toupper(str[i]);
}

BOOL GetOpticalMedia(IMG_SAVE* img_save)
{
	static char str[MAX_PATH];
	static char label[33];
	int k;
	BYTE geometry[256], *buffer = NULL;
	PDISK_GEOMETRY_EX DiskGeometry = (PDISK_GEOMETRY_EX)(void*)geometry;
	DWORD i, j, size, datatype;
	HDEVINFO dev_info = NULL;
	SP_DEVINFO_DATA dev_info_data;
	SP_DEVICE_INTERFACE_DATA devint_data;
	PSP_DEVICE_INTERFACE_DETAIL_DATA_A devint_detail_data;
	HANDLE hDrive = INVALID_HANDLE_VALUE;
	LARGE_INTEGER li;

	dev_info = SetupDiGetClassDevsA(&_GUID_DEVINTERFACE_CDROM, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (dev_info == INVALID_HANDLE_VALUE) {
		uprintf("SetupDiGetClassDevs (Interface) failed: %s\n", WindowsErrorString());
		return FALSE;
	}
	dev_info_data.cbSize = sizeof(dev_info_data);
	for (i = 0; SetupDiEnumDeviceInfo(dev_info, i, &dev_info_data); i++) {
		memset(str, 0, sizeof(str));
		if (!SetupDiGetDeviceRegistryPropertyU(dev_info, &dev_info_data, SPDRP_FRIENDLYNAME,
			&datatype, (LPBYTE)str, sizeof(str), &size)) {
			uprintf("SetupDiGetDeviceRegistryProperty (Friendly Name) failed: %s\n", WindowsErrorString());
			static_strcpy(str, "Generic Optical Drive");
		}
		uprintf("Found '%s' optical device", str);
		devint_data.cbSize = sizeof(devint_data);
		devint_detail_data = NULL;
		for (j = 0; ; j++) {
			safe_closehandle(hDrive);
			safe_free(devint_detail_data);
			safe_free(buffer);

			if (!SetupDiEnumDeviceInterfaces(dev_info, &dev_info_data, &_GUID_DEVINTERFACE_CDROM, j, &devint_data)) {
				if (GetLastError() != ERROR_NO_MORE_ITEMS) {
					uprintf("SetupDiEnumDeviceInterfaces failed: %s\n", WindowsErrorString());
				}
				break;
			}

			if (!SetupDiGetDeviceInterfaceDetailA(dev_info, &devint_data, NULL, 0, &size, NULL)) {
				if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
					devint_detail_data = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)calloc(1, size);
					if (devint_detail_data == NULL) {
						uprintf("Unable to allocate data for SP_DEVICE_INTERFACE_DETAIL_DATA\n");
						continue;
					}
					devint_detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
				} else {
					uprintf("SetupDiGetDeviceInterfaceDetail (dummy) failed: %s\n", WindowsErrorString());
					continue;
				}
			}
			if (devint_detail_data == NULL) {
				uprintf("SetupDiGetDeviceInterfaceDetail (dummy) - no data was allocated\n");
				continue;
			}
			if (!SetupDiGetDeviceInterfaceDetailA(dev_info, &devint_data, devint_detail_data, size, &size, NULL)) {
				uprintf("SetupDiGetDeviceInterfaceDetail (actual) failed: %s\n", WindowsErrorString());
				continue;
			}

			// Get the size of the inserted media (if any)
			hDrive = CreateFileA(devint_detail_data->DevicePath, GENERIC_READ,
				FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
			if (hDrive == INVALID_HANDLE_VALUE)
				continue;
			if (!DeviceIoControl(hDrive, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
				NULL, 0, geometry, sizeof(geometry), &size, NULL))
				continue;
			// Rewritable media usually has a one sector
			if (DiskGeometry->DiskSize.QuadPart <= 4096)
				continue;
			// Read the label directly, since it's a massive PITA to get it from Windows
			li.QuadPart = 0x8000LL;
			buffer = malloc(2048);
			if ((buffer != NULL) && (SetFilePointerEx(hDrive, li, NULL, FILE_BEGIN)) &&
				ReadFile(hDrive, buffer, 2048, &size, NULL) && (size == 2048)) {
				memcpy(label, &buffer[0x28], sizeof(label) - 1);
				label[sizeof(label) - 1] = 0;
				for (k = (int)strlen(label) - 1; (k >= 0) && (label[k] == 0x20); k--)
					label[k] = 0;
				img_save->Label = label;
			}
			static_strcpy(str, devint_detail_data->DevicePath);
			img_save->DevicePath = str;
			img_save->DeviceSize = DiskGeometry->DiskSize.QuadPart;
			safe_closehandle(hDrive);
			safe_free(devint_detail_data);
			safe_free(buffer);
			return TRUE;
		}
	}
	return FALSE;
}

/* For debugging user reports of HDDs vs UFDs */
//#define FORCED_DEVICE
#ifdef FORCED_DEVICE
#define FORCED_VID 0x067B
#define FORCED_PID 0x2731
#define FORCED_NAME "SD Card Reader USB Device"
#endif

/*
 * Refresh the list of USB devices
 */
BOOL GetDevices(DWORD devnum)
{
	// List of USB storage drivers we know - list may be incomplete!
	const char* usbstor_name[] = {
		// Standard MS USB storage driver
		"USBSTOR",
		// USB card readers, with proprietary drivers (Realtek,etc...)
		// Mostly "guessed" from http://www.carrona.org/dvrref.php
		"RTSUER", "CMIUCR", "EUCR",
		// UASP Drivers *MUST* be listed after this, starting with "UASPSTOR"
		// (which is Microsoft's native UASP driver for Windows 8 and later)
		// as we use "UASPSTOR" as a delimiter
		"UASPSTOR", "VUSBSTOR", "ETRONSTOR", "ASUSSTPT"
	};
	// These are the generic (non USB) storage enumerators we also test
	const char* genstor_name[] = {
		// Generic storage drivers (Careful now!)
		"SCSI", // "STORAGE",	// "STORAGE" is used by 'Storage Spaces" and stuff => DANGEROUS!
		// Non-USB card reader drivers - This list *MUST* start with "SD" (delimiter)
		// See http://itdoc.hitachi.co.jp/manuals/3021/30213B5200e/DMDS0094.HTM
		// Also  http://www.carrona.org/dvrref.php. NB: These should be reported
		// as enumerators by Rufus when Enum Debug is enabled
		"SD", "PCISTOR", "RTSOR", "JMCR", "JMCF", "RIMMPTSK", "RIMSPTSK", "RIXDPTSK",
		"TI21SONY", "ESD7SK", "ESM7SK", "O2MD", "O2SD", "VIACR"
	};
	// Oh, and we also have card devices (e.g. 'SCSI\DiskO2Micro_SD_...') under the SCSI enumerator...
	const char* scsi_disk_prefix = "SCSI\\Disk";
	const char* scsi_card_name[] = {
		"_SD_", "_SDHC_", "_MMC_", "_MS_", "_MSPro_", "_xDPicture_", "_O2Media_"
	};
	const char* usb_speed_name[USB_SPEED_MAX] = { "USB", "USB 1.0", "USB 1.1", "USB 2.0", "USB 3.0" };
	// Hash table and String Array used to match a Device ID with the parent hub's Device Interface Path
	htab_table htab_devid = HTAB_EMPTY;
	StrArray dev_if_path;
	char letter_name[] = " (?:)";
	char drive_name[] = "?:\\";
	char uefi_togo_check[] = "?:\\EFI\\Rufus\\ntfs_x64.efi";
	char scsi_card_name_copy[16];
	BOOL r = FALSE, found = FALSE, post_backslash;
	HDEVINFO dev_info = NULL;
	SP_DEVINFO_DATA dev_info_data;
	SP_DEVICE_INTERFACE_DATA devint_data;
	PSP_DEVICE_INTERFACE_DETAIL_DATA_A devint_detail_data;
	DEVINST parent_inst, grandparent_inst, device_inst;
	DWORD size, i, j, k, l, datatype, drive_index;
	DWORD uasp_start = ARRAYSIZE(usbstor_name), card_start = ARRAYSIZE(genstor_name);
	ULONG list_size[ARRAYSIZE(usbstor_name)] = { 0 }, list_start[ARRAYSIZE(usbstor_name)] = { 0 }, full_list_size, ulFlags;
	HANDLE hDrive;
	LONG maxwidth = 0;
	int s, score, drive_number, remove_drive;
	char drive_letters[27], *device_id, *devid_list = NULL, entry_msg[128];
	char *p, *label, *entry, buffer[MAX_PATH], str[MAX_PATH], *method_str, *hub_path;
	usb_device_props props;

	IGNORE_RETVAL(ComboBox_ResetContent(hDeviceList));
	StrArrayClear(&DriveID);
	StrArrayClear(&DriveLabel);
	StrArrayClear(&DriveHub);
	StrArrayCreate(&dev_if_path, 128);
	// Add a dummy for string index zero, as this is what non matching hashes will point to
	StrArrayAdd(&dev_if_path, "", TRUE);

	device_id = (char*)malloc(MAX_PATH);
	if (device_id == NULL)
		goto out;

	// Build a hash table associating a CM Device ID of an USB device with the SetupDI Device Interface Path
	// of its parent hub - this is needed to retrieve the device speed
	dev_info = SetupDiGetClassDevsA(&_GUID_DEVINTERFACE_USB_HUB, NULL, NULL, DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
	if (dev_info != INVALID_HANDLE_VALUE) {
		if (htab_create(DEVID_HTAB_SIZE, &htab_devid)) {
			dev_info_data.cbSize = sizeof(dev_info_data);
			for (i=0; SetupDiEnumDeviceInfo(dev_info, i, &dev_info_data); i++) {
				uuprintf("Processing Hub %d:", i + 1);
				devint_detail_data = NULL;
				devint_data.cbSize = sizeof(devint_data);
				// Only care about the first interface (MemberIndex 0)
				if ( (SetupDiEnumDeviceInterfaces(dev_info, &dev_info_data, &_GUID_DEVINTERFACE_USB_HUB, 0, &devint_data))
				  && (!SetupDiGetDeviceInterfaceDetailA(dev_info, &devint_data, NULL, 0, &size, NULL))
				  && (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
				  && ((devint_detail_data = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)calloc(1, size)) != NULL) ) {
					devint_detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
					if (SetupDiGetDeviceInterfaceDetailA(dev_info, &devint_data, devint_detail_data, size, &size, NULL)) {

						// Find the Device IDs for all the children of this hub
						if (CM_Get_Child(&device_inst, dev_info_data.DevInst, 0) == CR_SUCCESS) {
							device_id[0] = 0;
							s = StrArrayAdd(&dev_if_path, devint_detail_data->DevicePath, TRUE);
							uuprintf("  Hub[%d] = '%s'", s, devint_detail_data->DevicePath);
							if ((s>= 0) && (CM_Get_Device_IDA(device_inst, device_id, MAX_PATH, 0) == CR_SUCCESS)) {
								ToUpper(device_id);
								if ((k = htab_hash(device_id, &htab_devid)) != 0) {
									htab_devid.table[k].data = (void*)(uintptr_t)s;
								}
								uuprintf("  Found ID[%03d]: %s", k, device_id);
								while (CM_Get_Sibling(&device_inst, device_inst, 0) == CR_SUCCESS) {
									device_id[0] = 0;
									if (CM_Get_Device_IDA(device_inst, device_id, MAX_PATH, 0) == CR_SUCCESS) {
										ToUpper(device_id);
										if ((k = htab_hash(device_id, &htab_devid)) != 0) {
											htab_devid.table[k].data = (void*)(uintptr_t)s;
										}
										uuprintf("  Found ID[%03d]: %s", k, device_id);
									}
								}
							}
						}
					}
					free(devint_detail_data);
				}
			}
		}
		SetupDiDestroyDeviceInfoList(dev_info);
	}
	free(device_id);

	// Build a single list of Device IDs from all the storage enumerators we know of
	full_list_size = 0;
	ulFlags = CM_GETIDLIST_FILTER_SERVICE | CM_GETIDLIST_FILTER_PRESENT;
	for (s=0; s<ARRAYSIZE(usbstor_name); s++) {
		// Get a list of device IDs for all USB storage devices
		// This will be used to find if a device is UASP
		// Also compute the uasp_start index
		if (strcmp(usbstor_name[s], "UASPSTOR") == 0)
			uasp_start = s;
		if (CM_Get_Device_ID_List_SizeA(&list_size[s], usbstor_name[s], ulFlags) != CR_SUCCESS)
			list_size[s] = 0;
		if (list_size[s] != 0)
			full_list_size += list_size[s]-1;	// remove extra NUL terminator
	}
	// Compute the card_start index
	for (s=0; s<ARRAYSIZE(genstor_name); s++) {
		if (strcmp(genstor_name[s], "SD") == 0)
			card_start = s;
	}

	// Better safe than sorry. And yeah, we could have used arrays of
	// arrays to avoid this, but it's more readable this way.
	assert((uasp_start > 0) && (uasp_start < ARRAYSIZE(usbstor_name)));
	assert((card_start > 0) && (card_start < ARRAYSIZE(genstor_name)));

	devid_list = NULL;
	if (full_list_size != 0) {
		full_list_size += 1;	// add extra NUL terminator
		devid_list = (char*)malloc(full_list_size);
		if (devid_list == NULL) {
			uprintf("Could not allocate Device ID list\n");
			goto out;
		}
		for (s=0, i=0; s<ARRAYSIZE(usbstor_name); s++) {
			list_start[s] = i;
			if (list_size[s] > 1) {
				if (CM_Get_Device_ID_ListA(usbstor_name[s], &devid_list[i], list_size[s], ulFlags) != CR_SUCCESS)
					continue;
				if (usb_debug) {
					uprintf("Processing IDs belonging to '%s':", usbstor_name[s]);
					for (device_id = &devid_list[i]; *device_id != 0; device_id += strlen(device_id) + 1)
						uprintf("  %s", device_id);
				}
				// The list_size is sometimes larger than required thus we need to find the real end
				for (i += list_size[s]; i > 2; i--) {
					if ((devid_list[i-2] != '\0') && (devid_list[i-1] == '\0') && (devid_list[i] == '\0'))
						break;
				}
			}
		}
	}

	// Now use SetupDi to enumerate all our disk storage devices
	dev_info = SetupDiGetClassDevsA(&_GUID_DEVINTERFACE_DISK, NULL, NULL, DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
	if (dev_info == INVALID_HANDLE_VALUE) {
		uprintf("SetupDiGetClassDevs (Interface) failed: %s\n", WindowsErrorString());
		goto out;
	}
	dev_info_data.cbSize = sizeof(dev_info_data);
	for (i=0; SetupDiEnumDeviceInfo(dev_info, i, &dev_info_data); i++) {
		memset(buffer, 0, sizeof(buffer));
		memset(&props, 0, sizeof(props));
		method_str = "";
		hub_path = NULL;
		if (!SetupDiGetDeviceRegistryPropertyA(dev_info, &dev_info_data, SPDRP_ENUMERATOR_NAME,
				&datatype, (LPBYTE)buffer, sizeof(buffer), &size)) {
			uprintf("SetupDiGetDeviceRegistryProperty (Enumerator Name) failed: %s\n", WindowsErrorString());
			continue;
		}

		for (j = 0; j < ARRAYSIZE(usbstor_name); j++) {
			if (safe_stricmp(buffer, usbstor_name[0]) == 0) {
				props.is_USB = TRUE;
				if ((j != 0) && (j < uasp_start))
					props.is_CARD = TRUE;
				break;
			}
		}

		// UASP drives are listed under SCSI, and we also have non USB card readers to populate
		for (j = 0; j < ARRAYSIZE(genstor_name); j++) {
			if (safe_stricmp(buffer, genstor_name[j]) == 0) {
				props.is_SCSI = TRUE;
				if (j >= card_start)
					props.is_CARD = TRUE;
				break;
			}
		}

		uuprintf("Processing '%s' device:", buffer);
		if ((!props.is_USB) && (!props.is_SCSI)) {
			uuprintf("  Disabled by policy");
			continue;
		}

		// We can't use the friendly name to find if a drive is a VHD, as friendly name string gets translated
		// according to your locale, so we poke the Hardware ID
		memset(buffer, 0, sizeof(buffer));
		props.is_VHD = SetupDiGetDeviceRegistryPropertyA(dev_info, &dev_info_data, SPDRP_HARDWAREID,
			&datatype, (LPBYTE)buffer, sizeof(buffer), &size) && IsVHD(buffer);
		// Additional detection for SCSI card readers
		if ((!props.is_CARD) && (safe_strnicmp(buffer, scsi_disk_prefix, sizeof(scsi_disk_prefix)-1) == 0)) {
			for (j = 0; j < ARRAYSIZE(scsi_card_name); j++) {
				static_strcpy(scsi_card_name_copy, scsi_card_name[j]);
				if (safe_strstr(buffer, scsi_card_name_copy) != NULL) {
					props.is_CARD = TRUE;
					break;
				}
				// Also test for "_SD&" instead of "_SD_" and so on to allow for devices like
				// "SCSI\DiskRicoh_Storage_SD&REV_3.0" to be detected.
				scsi_card_name_copy[strlen(scsi_card_name_copy) - 1] = '&';
				if (safe_strstr(buffer, scsi_card_name_copy) != NULL) {
					props.is_CARD = TRUE;
					break;
				}
			}
		}
		uuprintf("  Hardware ID: '%s'", buffer);

		memset(buffer, 0, sizeof(buffer));
		props.is_Removable = SetupDiGetDeviceRegistryPropertyA(dev_info, &dev_info_data, SPDRP_REMOVAL_POLICY,
			&datatype, (LPBYTE)buffer, sizeof(buffer), &size) && IsRemovable(buffer);

		memset(buffer, 0, sizeof(buffer));
		if (!SetupDiGetDeviceRegistryPropertyU(dev_info, &dev_info_data, SPDRP_FRIENDLYNAME,
				&datatype, (LPBYTE)buffer, sizeof(buffer), &size)) {
			uprintf("SetupDiGetDeviceRegistryProperty (Friendly Name) failed: %s\n", WindowsErrorString());
			// We can afford a failure on this call - just replace the name with "USB Storage Device (Generic)"
			static_strcpy(buffer, lmprintf(MSG_045));
		} else if ((!props.is_VHD) && (devid_list != NULL)) {
			// Get the properties of the device. We could avoid doing this lookup every time by keeping
			// a lookup table, but there shouldn't be that many USB storage devices connected...
			// NB: Each of these Device IDs should have a child, from which we get the Device Instance match.
			for (device_id = devid_list; *device_id != 0; device_id += strlen(device_id) + 1) {
				if (CM_Locate_DevNodeA(&parent_inst, device_id, 0) != CR_SUCCESS) {
					uuprintf("Could not locate device node for '%s'", device_id);
					continue;
				}
				if (CM_Get_Child(&device_inst, parent_inst, 0) != CR_SUCCESS) {
					uuprintf("Could not get children of '%s'", device_id);
					continue;
				}
				if (device_inst != dev_info_data.DevInst) {
					// Try the siblings
					while (CM_Get_Sibling(&device_inst, device_inst, 0) == CR_SUCCESS) {
						if (device_inst == dev_info_data.DevInst) {
							uuprintf("NOTE: Matched instance from sibling for '%s'", device_id);
							break;
						}
					}
					if (device_inst != dev_info_data.DevInst)
						continue;
				}
				post_backslash = FALSE;
				method_str = "";

				// If we're not dealing with the USBSTOR part of our list, then this is an UASP device
				props.is_UASP = ((((uintptr_t)device_id)+2) >= ((uintptr_t)devid_list)+list_start[uasp_start]);
				// Now get the properties of the device, and its Device ID, which we need to populate the properties
				ToUpper(device_id);
				j = htab_hash(device_id, &htab_devid);
				uuprintf("  Matched with ID[%03d]: %s", j, device_id);

				// Try to parse the current device_id string for VID:PID
				// We'll use that if we can't get anything better
				for (k = 0, l = 0; (k<strlen(device_id)) && (l<2); k++) {
					// The ID is in the form USB_VENDOR_BUSID\VID_xxxx&PID_xxxx\...
					if (device_id[k] == '\\')
						post_backslash = TRUE;
					if (!post_backslash)
						continue;
					if (device_id[k] == '_') {
						props.pid = (uint16_t)strtoul(&device_id[k + 1], NULL, 16);
						if (l++ == 0)
							props.vid = props.pid;
					}
				}
				if (props.vid != 0)
					method_str = "[ID]";

				// If the hash didn't match a populated string in dev_if_path[] (htab_devid.table[j].data > 0),
				// we might have an extra vendor driver in between (e.g. "ASUS USB 3.0 Boost Storage Driver"
				// for UASP devices in ASUS "Turbo Mode" or "Apple Mobile Device USB Driver" for iPods)
				// so try to see if we can match the grandparent.
				if ( ((uintptr_t)htab_devid.table[j].data == 0)
					&& (CM_Get_Parent(&grandparent_inst, parent_inst, 0) == CR_SUCCESS)
					&& (CM_Get_Device_IDA(grandparent_inst, str, MAX_PATH, 0) == CR_SUCCESS) ) {
					device_id = str;
					method_str = "[GP]";
					ToUpper(device_id);
					j = htab_hash(device_id, &htab_devid);
					uuprintf("  Matched with (GP) ID[%03d]: %s", j, device_id);
				}
				if ((uintptr_t)htab_devid.table[j].data > 0) {
					uuprintf("  Matched with Hub[%d]: '%s'", (uintptr_t)htab_devid.table[j].data,
							dev_if_path.String[(uintptr_t)htab_devid.table[j].data]);
					if (GetUSBProperties(dev_if_path.String[(uintptr_t)htab_devid.table[j].data], device_id, &props)) {
						method_str = "";
						hub_path = dev_if_path.String[(uintptr_t)htab_devid.table[j].data];
					}
#ifdef FORCED_DEVICE
					props.vid = FORCED_VID;
					props.pid = FORCED_PID;
					static_strcpy(buffer, FORCED_NAME);
#endif
				}
				break;
			}
		}
		if (props.is_VHD) {
			uprintf("Found VHD device '%s'", buffer);
		} else if ((props.is_CARD) && ((!props.is_USB) || ((props.vid == 0) && (props.pid == 0)))) {
			uprintf("Found card reader device '%s'", buffer);
		} else if ((!props.is_USB) && (!props.is_UASP) && (props.is_Removable)) {
			if (!list_non_usb_removable_drives) {
				uprintf("Found non-USB removable device '%s' => Eliminated", buffer);
				uuprintf("If you *REALLY* need, you can enable listing of this device with <Ctrl><Alt><F>");
				continue;
			}
			uprintf("Found non-USB removable device '%s'", buffer);
		} else {
			if ((props.vid == 0) && (props.pid == 0)) {
				if (!props.is_USB) {
					// If we have a non removable SCSI drive and couldn't get a VID:PID,
					// we are most likely dealing with a system drive => eliminate it!
					uuprintf("Found non-USB non-removable device '%s' => Eliminated", buffer);
					continue;
				}
				static_strcpy(str, "????:????");	// Couldn't figure VID:PID
			} else {
				static_sprintf(str, "%04X:%04X", props.vid, props.pid);
			}
			if (props.speed >= USB_SPEED_MAX)
				props.speed = 0;
			uprintf("Found %s%s%s device '%s' (%s) %s\n", props.is_UASP?"UAS (":"",
				usb_speed_name[props.speed], props.is_UASP?")":"", buffer, str, method_str);
			if (props.is_LowerSpeed)
				uprintf("NOTE: This device is an USB 3.0 device operating at lower speed...");
		}
		devint_data.cbSize = sizeof(devint_data);
		hDrive = INVALID_HANDLE_VALUE;
		devint_detail_data = NULL;
		for (j=0; ;j++) {
			safe_closehandle(hDrive);
			safe_free(devint_detail_data);

			if (!SetupDiEnumDeviceInterfaces(dev_info, &dev_info_data, &_GUID_DEVINTERFACE_DISK, j, &devint_data)) {
				if(GetLastError() != ERROR_NO_MORE_ITEMS) {
					uprintf("SetupDiEnumDeviceInterfaces failed: %s\n", WindowsErrorString());
				} else {
					uprintf("A device was eliminated because it didn't report itself as a disk\n");
				}
				break;
			}

			if (!SetupDiGetDeviceInterfaceDetailA(dev_info, &devint_data, NULL, 0, &size, NULL)) {
				if(GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
					devint_detail_data = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)calloc(1, size);
					if (devint_detail_data == NULL) {
						uprintf("Unable to allocate data for SP_DEVICE_INTERFACE_DETAIL_DATA\n");
						continue;
					}
					devint_detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
				} else {
					uprintf("SetupDiGetDeviceInterfaceDetail (dummy) failed: %s\n", WindowsErrorString());
					continue;
				}
			}
			if (devint_detail_data == NULL) {
				uprintf("SetupDiGetDeviceInterfaceDetail (dummy) - no data was allocated\n");
				continue;
			}
			if(!SetupDiGetDeviceInterfaceDetailA(dev_info, &devint_data, devint_detail_data, size, &size, NULL)) {
				uprintf("SetupDiGetDeviceInterfaceDetail (actual) failed: %s\n", WindowsErrorString());
				continue;
			}

			hDrive = CreateFileA(devint_detail_data->DevicePath, GENERIC_READ|GENERIC_WRITE,
				FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			if(hDrive == INVALID_HANDLE_VALUE) {
				uprintf("Could not open '%s': %s\n", devint_detail_data->DevicePath, WindowsErrorString());
				continue;
			}

			drive_number = GetDriveNumber(hDrive, devint_detail_data->DevicePath);
			if (drive_number < 0)
				continue;

			drive_index = drive_number + DRIVE_INDEX_MIN;
			if (!IsMediaPresent(drive_index)) {
				uprintf("Device eliminated because it appears to contain no media\n");
				safe_closehandle(hDrive);
				safe_free(devint_detail_data);
				break;
			}
			if (GetDriveSize(drive_index) < (MIN_DRIVE_SIZE*MB)) {
				uprintf("Device eliminated because it is smaller than %d MB\n", MIN_DRIVE_SIZE);
				safe_closehandle(hDrive);
				safe_free(devint_detail_data);
				break;
			}

			if (GetDriveLabel(drive_index, drive_letters, &label)) {
				if ((props.is_SCSI) && (!props.is_UASP) && (!props.is_VHD)) {
					if (!props.is_Removable) {
						// Non removables should have been eliminated above, but since we
						// are potentially dealing with system drives, better safe than sorry
						safe_closehandle(hDrive);
						safe_free(devint_detail_data);
						break;
					}
					if (!list_non_usb_removable_drives) {
						// Go over the mounted partitions and find if GetDriveType() says they are
						// removable. If they are not removable, don't allow the drive to be listed
						for (p = drive_letters; *p; p++) {
							drive_name[0] = *p;
							if (GetDriveTypeA(drive_name) != DRIVE_REMOVABLE)
								break;
						}
						if (*p) {
							uprintf("Device eliminated because it contains a mounted partition that is set as non-removable");
							safe_closehandle(hDrive);
							safe_free(devint_detail_data);
							break;
						}
					}
				}
				if ((!enable_HDDs) && (!props.is_VHD) && (!props.is_CARD) &&
					((score = IsHDD(drive_index, (uint16_t)props.vid, (uint16_t)props.pid, buffer)) > 0)) {
					uprintf("Device eliminated because it was detected as a Hard Drive (score %d > 0)", score);
					if (!list_non_usb_removable_drives)
						uprintf("If this device is not a Hard Drive, please e-mail the author of this application");
					uprintf("NOTE: You can enable the listing of Hard Drives under 'advanced drive properties'");
					safe_closehandle(hDrive);
					safe_free(devint_detail_data);
					break;
				}

				// The empty string is returned for drives that don't have any volumes assigned
				if (drive_letters[0] == 0) {
					entry = lmprintf(MSG_046, label, drive_number,
						SizeToHumanReadable(GetDriveSize(drive_index), FALSE, use_fake_units));
				} else {
					// Find the UEFI:TOGO partition(s) (and eliminate them form our listing)
					for (k=0; drive_letters[k]; k++) {
						uefi_togo_check[0] = drive_letters[k];
						if (PathFileExistsA(uefi_togo_check)) {
							for (l=k; drive_letters[l]; l++)
								drive_letters[l] = drive_letters[l+1];
							k--;
						}
					}
					// We have multiple volumes assigned to the same device (multiple partitions)
					// If that is the case, use "Multiple Volumes" instead of the label
					static_strcpy(entry_msg, (((drive_letters[0] != 0) && (drive_letters[1] != 0))?
						lmprintf(MSG_047):label));
					for (k=0, remove_drive=0; drive_letters[k] && (!remove_drive); k++) {
						// Append all the drive letters we detected
						letter_name[2] = drive_letters[k];
						if (right_to_left_mode)
							static_strcat(entry_msg, RIGHT_TO_LEFT_MARK);
						static_strcat(entry_msg, letter_name);
						if (drive_letters[k] == (PathGetDriveNumberU(app_dir) + 'A'))
							remove_drive = 1;
						if (drive_letters[k] == (PathGetDriveNumberU(system_dir) + 'A'))
							remove_drive = 2;
					}
					// Make sure that we don't list any drive that should not be listed
					if (remove_drive) {
						uprintf("Removing %C: from the list: This is the %s!", drive_letters[--k],
							(remove_drive==1)?"disk from which " APPLICATION_NAME " is running":"system disk");
						safe_closehandle(hDrive);
						safe_free(devint_detail_data);
						break;
					}
					safe_sprintf(&entry_msg[strlen(entry_msg)], sizeof(entry_msg) - strlen(entry_msg),
						"%s [%s]", (right_to_left_mode)?RIGHT_TO_LEFT_MARK:"", SizeToHumanReadable(GetDriveSize(drive_index), FALSE, use_fake_units));
					entry = entry_msg;
				}

				// Must ensure that the combo box is UNSORTED for indexes to be the same
				StrArrayAdd(&DriveID, buffer, TRUE);
				StrArrayAdd(&DriveLabel, label, TRUE);
				if ((hub_path != NULL) && (StrArrayAdd(&DriveHub, hub_path, TRUE) >= 0))
					DrivePort[DriveHub.Index - 1] = props.port;

				IGNORE_RETVAL(ComboBox_SetItemData(hDeviceList, ComboBox_AddStringU(hDeviceList, entry), drive_index));
				maxwidth = max(maxwidth, GetEntryWidth(hDeviceList, entry));
				safe_closehandle(hDrive);
				safe_free(devint_detail_data);
				break;
			}
		}
	}
	SetupDiDestroyDeviceInfoList(dev_info);

	// Adjust the Dropdown width to the maximum text size
	SendMessage(hDeviceList, CB_SETDROPPEDWIDTH, (WPARAM)maxwidth, 0);

	if (devnum >= DRIVE_INDEX_MIN) {
		for (i=0; i<ComboBox_GetCount(hDeviceList); i++) {
			if ((DWORD)ComboBox_GetItemData(hDeviceList, i) == devnum) {
				found = TRUE;
				break;
			}
		}
	}
	if (!found)
		i = 0;
	IGNORE_RETVAL(ComboBox_SetCurSel(hDeviceList, i));
	SendMessage(hMainDialog, WM_COMMAND, (CBN_SELCHANGE<<16) | IDC_DEVICE, 0);
	r = TRUE;

out:
	// Set 'Start' as the selected button, so that tab selection works
	SendMessage(hMainDialog, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hMainDialog, IDC_START), TRUE);
	safe_free(devid_list);
	StrArrayDestroy(&dev_if_path);
	htab_destroy(&htab_devid);
	return r;
}
