/*
 * Rufus: The Reliable USB Formatting Utility
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

// TODO: publicize the link and license for USB icon:
// http://www.softicons.com/free-icons/computer-icons/icons-unleashed-vol-1-by-pc-unleashed/usb-icon

#include "msapi_utf8.h"
#include "resource.h"
#include "rufus.h"

/*
 * Globals
 */
static HINSTANCE hMainInstance;
static HWND hDialog, hDeviceList, hCapacity, hFileSystem;

#ifdef RUFUS_DEBUG
static void _uprintf(const char *format, ...)
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
#define uprintf(...) _uprintf(__VA_ARGS__)
#else
#define uprintf(...)
#endif

/*
 * Converts a windows error to human readable string
 * uses retval as errorcode, or, if 0, use GetLastError()
 */
static char *WindowsErrorString(DWORD retval)
{
static char err_string[256];

	DWORD size;
	DWORD error_code, format_error;

	error_code = retval?retval:GetLastError();

	safe_sprintf(err_string, sizeof(err_string), "[%d] ", error_code);

	size = FormatMessageU(FORMAT_MESSAGE_FROM_SYSTEM, NULL, error_code,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), &err_string[strlen(err_string)],
		sizeof(err_string)-(DWORD)strlen(err_string), NULL);
	if (size == 0) {
		format_error = GetLastError();
		if (format_error)
			safe_sprintf(err_string, sizeof(err_string),
				"Windows error code %u (FormatMessage error code %u)", error_code, format_error);
		else
			safe_sprintf(err_string, sizeof(err_string), "Unknown error code %u", error_code);
	}
	return err_string;
}

/*
 * Opens a drive return both the handle and the drive letter
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
		uprintf("GetLogicalDriveStrings failed: %s\n", WindowsErrorString(0));
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
			uprintf("Could not open drive %c: %s\n", WindowsErrorString(0));
			continue;
		}
	
		r = DeviceIoControl(*hDrive, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL,
			0, &device_number, sizeof(device_number), &size, NULL);
		if ((!r) || (size <= 0)) {
			uprintf("IOCTL_STORAGE_GET_DEVICE_NUMBER failed: %s\n", WindowsErrorString(0));
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
 * Returns the drive letter and volume label
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

	if (!GetVolumeInformationA(DrivePath, volume_label, sizeof(volume_label), NULL, NULL, NULL, NULL, 0)) {
		uprintf("GetVolumeInformation (Label) failed: %s\n", WindowsErrorString(0));
		return FALSE;
	}
	*label = volume_label;
	*letter = DrivePath[0];

	return TRUE;
}

/*
 * Returns the drive letter and volume label
 */
static BOOL GetDriveInfo(DWORD num, LONGLONG* DriveSize, char* FSType, DWORD FSTypeSize)
{
	BOOL r;
	HANDLE hDrive;
	DWORD size;
	BYTE geometry[128];
	void* disk_geometry = (void*)geometry;
	char DrivePath[] = "#:\\";

	*DriveSize = 0;

	if (!GetDriveHandle(num, &hDrive, DrivePath))
		return FALSE;

	r = DeviceIoControl(hDrive, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, 
			NULL, 0, geometry, sizeof(geometry), &size, NULL );
	if (!r || size <= 0) {
		uprintf("IOCTL_DISK_GET_DRIVE_GEOMETRY_EX failed: %s\n", WindowsErrorString(0));
		safe_closehandle(hDrive);
		return FALSE;
	}
	*DriveSize = ((PDISK_GEOMETRY_EX)(disk_geometry))->DiskSize.QuadPart;

	safe_closehandle(hDrive);

	if (!GetVolumeInformationA(DrivePath, NULL, 0, NULL, NULL, NULL, FSType, FSTypeSize)) {
		uprintf("GetVolumeInformation (Properties) failed: %s\n", WindowsErrorString(0));
		return FALSE;
	}

	return TRUE;
}

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
 * Refreshes the list of USB devices
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
		uprintf("SetupDiGetClassDevs (Interface) failed: %d\n", WindowsErrorString(0));
		return FALSE;
	}

	dev_info_data.cbSize = sizeof(dev_info_data);
	for (i=0; SetupDiEnumDeviceInfo(dev_info, i, &dev_info_data); i++) {
		memset(buffer, 0, sizeof(buffer));
		if (!SetupDiGetDeviceRegistryPropertyA(dev_info, &dev_info_data, SPDRP_ENUMERATOR_NAME,
				&datatype, (LPBYTE)buffer, sizeof(buffer), &size)) {
			uprintf("SetupDiGetDeviceRegistryProperty (Enumerator Name) failed: %d\n", WindowsErrorString(0));
			continue;
		}

		if (safe_strcmp(buffer, usbstor_name) != 0)
			continue;
		memset(buffer, 0, sizeof(buffer));
		if (!SetupDiGetDeviceRegistryPropertyA(dev_info, &dev_info_data, SPDRP_FRIENDLYNAME,
				&datatype, (LPBYTE)buffer, sizeof(buffer), &size)) {
			uprintf("SetupDiGetDeviceRegistryProperty (Friendly Name) failed: %d\n", WindowsErrorString(0));
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
					uprintf("SetupDiEnumDeviceInterfaces failed: %s\n", WindowsErrorString(0));
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
					uprintf("SetupDiGetDeviceInterfaceDetail (dummy) failed: %s\n", WindowsErrorString(0));
					continue;
				}
			}
			if(!SetupDiGetDeviceInterfaceDetailA(dev_info, &devint_data, devint_detail_data, size, &size, NULL)) {
				uprintf("SetupDiGetDeviceInterfaceDetail (actual) failed: %s\n", WindowsErrorString(0));
				continue;
			}

			hDrive = CreateFileA(devint_detail_data->DevicePath, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
			if(hDrive == INVALID_HANDLE_VALUE) {
				uprintf("could not open '%s': %s\n", devint_detail_data->DevicePath, WindowsErrorString(0)); 
				continue;
			}

			memset(&device_number, 0, sizeof(device_number));
			r = DeviceIoControl(hDrive, IOCTL_STORAGE_GET_DEVICE_NUMBER, 
						NULL, 0, &device_number, sizeof(device_number), &size, NULL );
			if (!r || size <= 0) {
				uprintf("IOCTL_STORAGE_GET_DEVICE_NUMBER 2 failed: %s\n", WindowsErrorString(0));
				continue;
			}

			if (GetDriveLabel(device_number.DeviceNumber, &drive_letter, &label)) {
				safe_sprintf(entry, sizeof(entry), "%s (%c:)\n", label, drive_letter);
				IGNORE_RETVAL(ComboBox_SetItemData(hDeviceList, ComboBox_AddStringU(hDeviceList, entry), device_number.DeviceNumber));
			}
		}
	}
	IGNORE_RETVAL(ComboBox_SetCurSel(hDeviceList, 0));
	PostMessage(hDialog, WM_COMMAND, (CBN_SELCHANGE<<16) | IDC_DEVICE, 0);

	return TRUE;
}

// TODO: the device is currently in use by another application (find application a la TGit installer?)

/*
* Main dialog callback
*/
static INT_PTR CALLBACK MainCallback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {

	case WM_DEVICECHANGE:
		GetUSBDevices();
		return (INT_PTR)TRUE;

	case WM_INITDIALOG:
		hDialog = hDlg;
		hDeviceList = GetDlgItem(hDlg, IDC_DEVICE);
		hCapacity = GetDlgItem(hDlg, IDC_CAPACITY);
		hFileSystem = GetDlgItem(hDlg, IDC_FILESYSTEM);
		GetUSBDevices();
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		switch(LOWORD(wParam)) {
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
 * Center a dialog with regards to the main application Window or the desktop
 */
void CenterDialog(HWND hDlg)
{
	POINT Point;
	HWND hParent;
	RECT DialogRect;
	RECT ParentRect;
	int nWidth;
	int nHeight;

	// Get the size of the dialog box.
	GetWindowRect(hDlg, &DialogRect);

	// Get the parent
	hParent = GetParent(hDlg);
	if (hParent == NULL) {
		hParent = GetDesktopWindow();
	}
	GetClientRect(hParent, &ParentRect);

	// Calculate the height and width of the current dialog
	nWidth = DialogRect.right - DialogRect.left;
	nHeight = DialogRect.bottom - DialogRect.top;

	// Find the center point and convert to screen coordinates.
	Point.x = (ParentRect.right - ParentRect.left) / 2;
	Point.y = (ParentRect.bottom - ParentRect.top) / 2;
	ClientToScreen(hParent, &Point);

	// Calculate the new x, y starting point.
	Point.x -= nWidth / 2;
	Point.y -= nHeight / 2 + 35;

	// Move the window.
	MoveWindow(hDlg, Point.x, Point.y, nWidth, nHeight, FALSE);
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
