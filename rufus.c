/*
 * Rufus: The Resourceful USB Formatting Utility
 * Copyright (c) 2011 Pete Batard <pete@akeo.ie>
 * 
 * Device enumeration based in part on TestUSBDriveEject.cpp by ahmd:
 * http://www.codeguru.com/forum/showpost.php?p=1951973&postcount=7
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
#include <windowsx.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <commctrl.h>
#include <setupapi.h>
#include <winioctl.h>

// http://doc.sch130.nsc.ru/www.sysinternals.com/ntw2k/source/fmifs.shtml
// http://svn.reactos.org/svn/reactos/trunk/reactos/include/reactos/libs/fmifs/
//#include <fmifs.h>
// http://git.kernel.org/?p=fs/ext2/e2fsprogs.git;a=blob;f=misc/badblocks.c

#include "msapi_utf8.h"
#include "resource.h"
#include "rufus.h"

#if !defined(GUID_DEVINTERFACE_DISK)
const GUID GUID_DEVINTERFACE_DISK = { 0x53f56307L, 0xb6bf, 0x11d0, {0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b} };
#endif

extern char *WindowsErrorString(void);

/*
 * Globals
 */
HINSTANCE hMainInstance;
HWND hMainDialog;
char szFolderPath[MAX_PATH];
HWND hStatus;
float fScale = 1.0f;

static HWND hDeviceList, hCapacity, hFileSystem;


#ifdef RUFUS_DEBUG
void _uprintf(const char *format, ...)
{
	char buf[4096], *p = buf;
	va_list args;
	int n;

	va_start(args, format);
	n = safe_vsnprintf(p, sizeof(buf)-3, format, args); // buf-3 is room for CR/LF/NUL
	va_end(args);

	p += (n < 0)?sizeof(buf)-3:n;

	while((p>buf) && (isspace(p[-1])))
		*--p = '\0';

	*p++ = '\r';
	*p++ = '\n';
	*p   = '\0';

	OutputDebugStringA(buf);
}
#endif


/*
 * Convert a partition type to its human readable form
 * http://www.win.tue.nl/~aeb/partitions/partition_types-1.html
 */
static const char* GetPartitionType(BYTE Type)
{
	switch(Type) {
	case 0x00: 
		return "Unused";
	case 0x01:
		return "DOS FAT12";
	case 0x02:
		return "Logical Disk Manager";
	case 0x04:
		return "DOS 3.0+ FAT16";
	case 0x05:
		return "Extended Partition";
	case 0x06:
		return "DOS 3.31+ FAT16 (Huge)";
	case 0x07:
		return "NTFS";
	case 0x0b:
		return "Win95 FAT32";
	case 0x0c:
		return "Win95 FAT32 (LBA)";
	case 0x0e:
		return "Win95 FAT16 (LBA)";
	case 0x0f:
		return "Win95 Extended (LBA)";
	case 0x11:
		return "Hidden DOS FAT12";
	case 0x12:
		return "Diagnostics Partition";
	case 0x14:
		return "Hidden DOS 3.0+ FAT16";
	case 0x16:
		return "Hidden DOS 3.31+ FAT16 (Huge)";
	case 0x17:
		return "Hidden NTFS";
	case 0x1b:
		return "Hidden Win95 FAT32";
	case 0x1c:
		return "Hidden Win95 FAT32 (LBA)";
	case 0x1e:
		return "Hidden Win95 FAT16 (LBA)";
	case 0x1f:
		return "Hidden Win95 Extended (LBA)";
	case 0x27:
		return "Windows Recovery";
	case 0x42:
		return "Windows 2000 Dynamic Extended";
	case 0x63:
		return "Unix System V";
	case 0x82:
		return "Linux Swap";
	case 0x83:
		return "Linux";
	case 0x85:
		return "Linux Extended";
	case 0x8e:
		return "Linux LVM";
	case 0x9f:
	case 0xa6:
	case 0xa9:
		return "BSD";
	case 0xa8:
	case 0xaf:
		return "OS-X";
	case 0xab:
		return "OS-X (Boot)";
	case 0xeb:
		return "BeOS";
	case 0xfa:
		return "Bochs";
	case 0xfb:
	case 0xfc:
		return "VMWare";
	case 0xfd:
		return "Linux RAID";
	default:
		return "Unknown";
	}
}

/*
 * Open a drive - return both the handle and the drive letter
 */
static BOOL GetDriveHandle(DWORD num, HANDLE* hDrive, char* DriveLetter)
{
	BOOL r;
	DWORD size;
	STORAGE_DEVICE_NUMBER_REDEF device_number = {0};
	static char drives[26*4];	/* "D:\", "E:\", etc. */
	char *drive = drives;
	char drive_name[] = "\\\\.\\#:";

	size = GetLogicalDriveStringsA(sizeof(drives), drives);
	if (size == 0) {
		uprintf("GetLogicalDriveStrings failed: %s\n", WindowsErrorString());
		return FALSE;
	}
	if (size > sizeof(drives)) {
		uprintf("GetLogicalDriveStrings: buffer too small (required %d vs %d)\n", size, sizeof(drives));
		return FALSE;
	}

	*hDrive = INVALID_HANDLE_VALUE;
	for ( ;*drive; drive += safe_strlen(drive)+1) {
		if (*drive < 'C') {
			continue;
		}
		safe_sprintf(drive_name, sizeof(drive_name), "\\\\.\\%c:", drive[0]);
		*hDrive = CreateFileA(drive_name, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, 0);
		if (hDrive == INVALID_HANDLE_VALUE) {
			uprintf("Could not open drive %c: %s\n", WindowsErrorString());
			continue;
		}
	
		r = DeviceIoControl(*hDrive, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL,
			0, &device_number, sizeof(device_number), &size, NULL);
		if ((!r) || (size <= 0)) {
			uprintf("IOCTL_STORAGE_GET_DEVICE_NUMBER (GetDriveHandle) failed: %s\n", WindowsErrorString());
			safe_closehandle(*hDrive);
			break;
		}
		if (device_number.DeviceNumber == num)
			break;
	}

	if (DriveLetter != NULL) {
		*DriveLetter = *drive;
	}

	return (*hDrive != INVALID_HANDLE_VALUE);
}

/*
 * Return the drive letter and volume label
 */
static BOOL GetDriveLabel(DWORD num, char* letter, char** label)
{
	HANDLE hDrive;
	char DrivePath[] = "#:\\";
	char volume_label[MAX_PATH+1];

	*label = "NO_LABEL";

	if (!GetDriveHandle(num, &hDrive, DrivePath))
		return FALSE;
	safe_closehandle(hDrive);
	*letter = DrivePath[0];

	if (GetVolumeInformationA(DrivePath, volume_label, sizeof(volume_label), NULL, NULL, NULL, NULL, 0)) {
		*label = volume_label;
	} else {
		uprintf("GetVolumeInformation (Label) failed: %s\n", WindowsErrorString());
	}

	return TRUE;
}

/*
 * Returns the drive properties (size, FS)
 */
static BOOL GetDriveInfo(DWORD num, LONGLONG* DriveSize, char* FSType, DWORD FSTypeSize)
{
	BOOL r;
	HANDLE hDrive;
	DWORD size;
	BYTE geometry[128], layout[1024];
	void* disk_geometry = (void*)geometry;
	void* drive_layout = (void*)layout;
	PDISK_GEOMETRY_EX DiskGeometry = (PDISK_GEOMETRY_EX)disk_geometry;
	PDRIVE_LAYOUT_INFORMATION_EX DriveLayout = (PDRIVE_LAYOUT_INFORMATION_EX)drive_layout;
	char DrivePath[] = "#:\\";
	DWORD i, nb_partitions = 0;

	*DriveSize = 0;

	if (!GetDriveHandle(num, &hDrive, DrivePath))
		return FALSE;

	r = DeviceIoControl(hDrive, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, 
			NULL, 0, geometry, sizeof(geometry), &size, NULL );
	if (!r || size <= 0) {
		uprintf("IOCTL_DISK_GET_DRIVE_GEOMETRY_EX failed: %s\n", WindowsErrorString());
		safe_closehandle(hDrive);
		return FALSE;
	}
	*DriveSize = DiskGeometry->DiskSize.QuadPart;

	r = DeviceIoControl(hDrive, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, 
			NULL, 0, layout, sizeof(layout), &size, NULL );
	if (!r || size <= 0) {
		uprintf("IOCTL_DISK_GET_DRIVE_LAYOUT_EX failed: %s\n", WindowsErrorString());
	} else {
		switch (DriveLayout->PartitionStyle) {
		case PARTITION_STYLE_MBR:
			for (i=0; i<DriveLayout->PartitionCount; i++) {
				if (DriveLayout->PartitionEntry[i].Mbr.PartitionType != PARTITION_ENTRY_UNUSED) {
					uprintf("Partition #%d:\n", ++nb_partitions);
					uprintf("  Type: %s (0x%02X)\n  Boot: %s\n  Recognized: %s\n  Hidden Sectors: %d\n",
						GetPartitionType(DriveLayout->PartitionEntry[i].Mbr.PartitionType),
						DriveLayout->PartitionEntry[i].Mbr.PartitionType,
						DriveLayout->PartitionEntry[i].Mbr.BootIndicator?"Yes":"No",
						DriveLayout->PartitionEntry[i].Mbr.RecognizedPartition?"Yes":"No",
						DriveLayout->PartitionEntry[i].Mbr.HiddenSectors);
				}
			}
			uprintf("Partition type: MBR, NB Partitions: %d\n", nb_partitions);
			break;
		case PARTITION_STYLE_GPT:
			uprintf("Partition type: GPT, NB Partitions: %d\n", DriveLayout->PartitionCount);
			break;
		default:
			uprintf("Partition type: RAW\n");
			break;
		}
	}

	safe_closehandle(hDrive);

	if (!GetVolumeInformationA(DrivePath, NULL, 0, NULL, NULL, NULL, FSType, FSTypeSize)) {
		safe_sprintf(FSType, FSTypeSize, "Non Windows (Please Select)");
		uprintf("GetVolumeInformation (Properties) failed: %s\n", WindowsErrorString());
	}

	return TRUE;
}

/*
 * Populate the UI properties
 */
static BOOL PopulateProperties(int index)
{
	double HumanReadableSize;
	LONGLONG DiskSize;
	DWORD DeviceNumber;
	char capacity[64], FSType[32];
	char* suffix[] = { "KB", "MB", "GB", "TB", "PB"};
	int i;

	IGNORE_RETVAL(ComboBox_ResetContent(hCapacity));
	IGNORE_RETVAL(ComboBox_ResetContent(hFileSystem));
	if (index < 0) {
		return TRUE;
	}

	DeviceNumber = (DWORD)ComboBox_GetItemData(hDeviceList, index);
	if (!GetDriveInfo(DeviceNumber, &DiskSize, FSType, sizeof(FSType)))
		return FALSE;

	HumanReadableSize = (double)DiskSize;
	for (i=0; i<ARRAYSIZE(suffix); i++) {
		HumanReadableSize /= 1024.0;
		if (HumanReadableSize < 512.0) {
			safe_sprintf(capacity, sizeof(capacity), "%0.2f %s", HumanReadableSize, suffix[i]);
			break;
		}
	}
	IGNORE_RETVAL(ComboBox_AddStringU(hCapacity, capacity));
	IGNORE_RETVAL(ComboBox_SetCurSel(hCapacity, 0));
	IGNORE_RETVAL(ComboBox_AddStringU(hFileSystem, FSType));
	IGNORE_RETVAL(ComboBox_SetCurSel(hFileSystem, 0));
	return TRUE;
}

/*
 * Refresh the list of USB devices
 */
static BOOL GetUSBDevices(void)
{
	BOOL r;
	HDEVINFO dev_info = NULL;
	SP_DEVINFO_DATA dev_info_data;
	SP_DEVICE_INTERFACE_DATA devint_data;
	PSP_DEVICE_INTERFACE_DETAIL_DATA_A devint_detail_data;
	STORAGE_DEVICE_NUMBER_REDEF device_number;
	DWORD size, i, j, datatype;
	HANDLE hDrive;
	char drive_letter;
	char *label, entry[MAX_PATH], buffer[MAX_PATH];
	const char* usbstor_name = "USBSTOR";

	IGNORE_RETVAL(ComboBox_ResetContent(hDeviceList));

	dev_info = SetupDiGetClassDevsA(&GUID_DEVINTERFACE_DISK, NULL, NULL, DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
	if (dev_info == INVALID_HANDLE_VALUE) {
		uprintf("SetupDiGetClassDevs (Interface) failed: %d\n", WindowsErrorString());
		return FALSE;
	}

	dev_info_data.cbSize = sizeof(dev_info_data);
	for (i=0; SetupDiEnumDeviceInfo(dev_info, i, &dev_info_data); i++) {
		memset(buffer, 0, sizeof(buffer));
		if (!SetupDiGetDeviceRegistryPropertyA(dev_info, &dev_info_data, SPDRP_ENUMERATOR_NAME,
				&datatype, (LPBYTE)buffer, sizeof(buffer), &size)) {
			uprintf("SetupDiGetDeviceRegistryProperty (Enumerator Name) failed: %d\n", WindowsErrorString());
			continue;
		}

		if (safe_strcmp(buffer, usbstor_name) != 0)
			continue;
		memset(buffer, 0, sizeof(buffer));
		if (!SetupDiGetDeviceRegistryPropertyA(dev_info, &dev_info_data, SPDRP_FRIENDLYNAME,
				&datatype, (LPBYTE)buffer, sizeof(buffer), &size)) {
			uprintf("SetupDiGetDeviceRegistryProperty (Friendly Name) failed: %d\n", WindowsErrorString());
			continue;
		}
		uprintf("found drive '%s'\n", buffer);

		devint_data.cbSize = sizeof(devint_data);
		hDrive = INVALID_HANDLE_VALUE;
		devint_detail_data = NULL;
		for (j=0; ;j++) {
			safe_closehandle(hDrive);
			safe_free(devint_detail_data);

			if (!SetupDiEnumDeviceInterfaces(dev_info, &dev_info_data, &GUID_DEVINTERFACE_DISK, j, &devint_data)) {
				if(GetLastError() != ERROR_NO_MORE_ITEMS) {
					uprintf("SetupDiEnumDeviceInterfaces failed: %s\n", WindowsErrorString());
				}
				break;
			}

			if (!SetupDiGetDeviceInterfaceDetailA(dev_info, &devint_data, NULL, 0, &size, NULL)) {
				if(GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
					devint_detail_data = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)calloc(1, size);
					if (devint_detail_data == NULL) {
						uprintf("unable to allocate data for SP_DEVICE_INTERFACE_DETAIL_DATA\n");
						return FALSE;
					}
					devint_detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
				} else {
					uprintf("SetupDiGetDeviceInterfaceDetail (dummy) failed: %s\n", WindowsErrorString());
					continue;
				}
			}
			if(!SetupDiGetDeviceInterfaceDetailA(dev_info, &devint_data, devint_detail_data, size, &size, NULL)) {
				uprintf("SetupDiGetDeviceInterfaceDetail (actual) failed: %s\n", WindowsErrorString());
				continue;
			}

			hDrive = CreateFileA(devint_detail_data->DevicePath, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
			if(hDrive == INVALID_HANDLE_VALUE) {
				uprintf("could not open '%s': %s\n", devint_detail_data->DevicePath, WindowsErrorString()); 
				continue;
			}

			memset(&device_number, 0, sizeof(device_number));
			r = DeviceIoControl(hDrive, IOCTL_STORAGE_GET_DEVICE_NUMBER, 
						NULL, 0, &device_number, sizeof(device_number), &size, NULL );
			if (!r || size <= 0) {
				uprintf("IOCTL_STORAGE_GET_DEVICE_NUMBER (GetUSBDevices) failed: %s\n", WindowsErrorString());
				continue;
			}

			if (GetDriveLabel(device_number.DeviceNumber, &drive_letter, &label)) {
				safe_sprintf(entry, sizeof(entry), "%s (%c:)\n", label, drive_letter);
				IGNORE_RETVAL(ComboBox_SetItemData(hDeviceList, ComboBox_AddStringU(hDeviceList, entry), device_number.DeviceNumber));
			}
		}
	}
	IGNORE_RETVAL(ComboBox_SetCurSel(hDeviceList, 0));
	PostMessage(hMainDialog, WM_COMMAND, (CBN_SELCHANGE<<16) | IDC_DEVICE, 0);

	return TRUE;
}

// TODO: the device is currently in use by another application (find application a la TGit installer?)

/*
 * Main dialog callback
 */
static INT_PTR CALLBACK MainCallback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	HDC hDC;
	DRAWITEMSTRUCT* pDI;

	switch (message) {

	case WM_DEVICECHANGE:
		GetUSBDevices();
		return (INT_PTR)TRUE;

	case WM_INITDIALOG:
		hMainDialog = hDlg;
		hDeviceList = GetDlgItem(hDlg, IDC_DEVICE);
		hCapacity = GetDlgItem(hDlg, IDC_CAPACITY);
		hFileSystem = GetDlgItem(hDlg, IDC_FILESYSTEM);
		// High DPI scaling
		hDC = GetDC(hDlg);
		fScale = GetDeviceCaps(hDC, LOGPIXELSX) / 96.0f;
		ReleaseDC(hDlg, hDC);
		// Create the status line
		CreateStatusBar();
		// Display the version in the right area of the status bar
		SendMessageA(GetDlgItem(hDlg, IDC_STATUS), SB_SETTEXTA, SBT_OWNERDRAW | 1, (LPARAM)APP_VERSION);
		GetUSBDevices();
		return (INT_PTR)TRUE;

	// Change the colour of the version text in the status bar
	case WM_DRAWITEM:
		if (wParam == IDC_STATUS) {
			pDI = (DRAWITEMSTRUCT*)lParam;
			SetBkMode(pDI->hDC, TRANSPARENT);
			SetTextColor(pDI->hDC, GetSysColor(COLOR_3DSHADOW));
			pDI->rcItem.top += (int)(2.0f * fScale);
			pDI->rcItem.left += (int)(4.0f * fScale);
			DrawTextExA(pDI->hDC, APP_VERSION, -1, &pDI->rcItem, DT_LEFT, NULL);
			return (INT_PTR)TRUE;
		}
		break;

	case WM_COMMAND:
		switch(LOWORD(wParam)) {
		case IDC_ABOUT:
			CreateAboutBox();
			break;
		case IDC_DEVICE:		// dropdown: device description
			switch (HIWORD(wParam)) {
			case CBN_SELCHANGE:
				PopulateProperties(ComboBox_GetCurSel(hDeviceList));
				break;
			}
		break;
		case IDC_CLOSE:
			PostQuitMessage(0);
			break;
		default:
			return (INT_PTR)FALSE;
		}
		return (INT_PTR)TRUE;

	case WM_CLOSE:
		PostQuitMessage(0);
		break;

	}
	return (INT_PTR)FALSE;
}


/*
 * Application Entrypoint
 */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	HANDLE mutex = NULL;
	HWND hDlg = NULL;
	MSG msg;

	uprintf("*** RUFUS INIT ***\n");

	// Prevent 2 applications from running at the same time
	mutex = CreateMutexA(NULL, TRUE, "Global/RUFUS");
	if ((mutex == NULL) || (GetLastError() == ERROR_ALREADY_EXISTS))
	{
		MessageBoxA(NULL, "Another Rufus application is running.\n"
			"Please close the first application before running another one.",
			"Other instance detected", MB_ICONSTOP);
		return 0;
	}

	// Save instance of the application for further reference
	hMainInstance = hInstance;

	// Initialize COM for folder selection
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

	// Create the main Window
	if ( (hDlg = CreateDialogA(hInstance, MAKEINTRESOURCEA(IDD_DIALOG), NULL, MainCallback)) == NULL ) {
		MessageBoxA(NULL, "Could not create Window", "DialogBox failure", MB_ICONSTOP);
		goto out;
	}
	CenterDialog(hDlg);
	ShowWindow(hDlg, SW_SHOWNORMAL);
	UpdateWindow(hDlg);

	// Do our own event processing
	while(GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

out:
	CloseHandle(mutex);
	uprintf("*** RUFUS EXIT ***\n");

	return 0;
}
