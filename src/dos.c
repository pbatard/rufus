/*
 * Rufus: The Reliable USB Formatting Utility
 * DOS boot file extraction, from the FAT12 floppy image in diskcopy.dll
 * (MS WinME DOS) or from the embedded FreeDOS resource files
 * Copyright © 2011-2017 Pete Batard <pete@akeo.ie>
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
#include <stdlib.h>
#include <string.h>

#include "rufus.h"
#include "missing.h"
#include "resource.h"

#include "dos.h"

static BYTE* DiskImage = NULL;
static DWORD DiskImageSize;

/*
 * FAT time conversion, from ReactOS' time.c
 */
#define TICKSPERMIN        600000000
#define TICKSPERSEC        10000000
#define TICKSPERMSEC       10000
#define SECSPERDAY         86400
#define SECSPERHOUR        3600
#define SECSPERMIN         60
#define MINSPERHOUR        60
#define HOURSPERDAY        24
#define EPOCHWEEKDAY       1
#define DAYSPERWEEK        7
#define EPOCHYEAR          1601
#define DAYSPERNORMALYEAR  365
#define DAYSPERLEAPYEAR    366
#define MONSPERYEAR        12

typedef struct _TIME_FIELDS {
	short Year;
	short Month;
	short Day;
	short Hour;
	short Minute;
	short Second;
	short Milliseconds;
	short Weekday;
} TIME_FIELDS, *PTIME_FIELDS;

#define ARGUMENT_PRESENT(ArgumentPointer) \
	((CHAR*)((ULONG_PTR)(ArgumentPointer)) != (CHAR*)NULL)

static const UCHAR MonthLengths[2][MONSPERYEAR] =
{
	{ 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 },
	{ 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }
};

static __inline int IsLeapYear(int Year)
{
	return Year % 4 == 0 && (Year % 100 != 0 || Year % 400 == 0) ? 1 : 0;
}

static int DaysSinceEpoch(int Year)
{
	int Days;
	Year--; /* Don't include a leap day from the current year */
	Days = Year * DAYSPERNORMALYEAR + Year / 4 - Year / 100 + Year / 400;
	Days -= (EPOCHYEAR - 1) * DAYSPERNORMALYEAR + (EPOCHYEAR - 1) / 4 - (EPOCHYEAR - 1) / 100 + (EPOCHYEAR - 1) / 400;
	return Days;
}

static BOOLEAN RtlTimeFieldsToTime(PTIME_FIELDS TimeFields, PLARGE_INTEGER Time)
{
	int CurMonth;
	TIME_FIELDS IntTimeFields;

	memcpy(&IntTimeFields,
		TimeFields,
		sizeof(TIME_FIELDS));

	if (TimeFields->Milliseconds < 0 || TimeFields->Milliseconds > 999 ||
		TimeFields->Second < 0 || TimeFields->Second > 59 ||
		TimeFields->Minute < 0 || TimeFields->Minute > 59 ||
		TimeFields->Hour < 0 || TimeFields->Hour > 23 ||
		TimeFields->Month < 1 || TimeFields->Month > 12 ||
		TimeFields->Day < 1 ||
		TimeFields->Day >
		MonthLengths[IsLeapYear(TimeFields->Year)][TimeFields->Month - 1] ||
		TimeFields->Year < 1601) {
		return FALSE;
	}

	/* Compute the time */
	Time->QuadPart = DaysSinceEpoch(IntTimeFields.Year);
	for (CurMonth = 1; CurMonth < IntTimeFields.Month; CurMonth++) {
		Time->QuadPart += MonthLengths[IsLeapYear(IntTimeFields.Year)][CurMonth - 1];
	}
	Time->QuadPart += IntTimeFields.Day - 1;
	Time->QuadPart *= SECSPERDAY;
	Time->QuadPart += IntTimeFields.Hour * SECSPERHOUR + IntTimeFields.Minute * SECSPERMIN +
		IntTimeFields.Second;
	Time->QuadPart *= TICKSPERSEC;
	Time->QuadPart += IntTimeFields.Milliseconds * TICKSPERMSEC;

	return TRUE;
}

static void FatDateTimeToSystemTime(PLARGE_INTEGER SystemTime, PFAT_DATETIME FatDateTime, UCHAR TenMs OPTIONAL)
{
	TIME_FIELDS TimeFields;

	/* Setup time fields */
	TimeFields.Year = FatDateTime->Date.Year + 1980;
	TimeFields.Month = FatDateTime->Date.Month;
	TimeFields.Day = FatDateTime->Date.Day;
	TimeFields.Hour = FatDateTime->Time.Hour;
	TimeFields.Minute = FatDateTime->Time.Minute;
	TimeFields.Second = (FatDateTime->Time.DoubleSeconds << 1);

	/* Adjust up to 10 milliseconds
	* if the parameter was supplied
	*/
	if (ARGUMENT_PRESENT(TenMs)) {
		TimeFields.Second += TenMs / 100;
		TimeFields.Milliseconds = (TenMs % 100) * 10;
	} else {
		TimeFields.Milliseconds = 0;
	}

	/* Fix seconds value that might get beyond the bound */
	if (TimeFields.Second > 59) TimeFields.Second = 0;

	/* Perform conversion to system time if possible */
	if (!RtlTimeFieldsToTime(&TimeFields, SystemTime)) {
		/* Set to default time if conversion failed */
		SystemTime->QuadPart = 0;
	}
}

/* http://www.multiboot.ru/msdos8.htm & http://en.wikipedia.org/wiki/Windows_Me#Real_mode_DOS
 * COMMAND.COM and IO.SYS from diskcopy.dll are from the WinME crippled version
 * that removed real mode DOS => they must be patched:
 * IO.SYS            000003AA          75 -> EB
 * COMMAND.COM       00006510          75 -> EB
 */
static BOOL Patch_COMMAND_COM(size_t filestart, size_t filesize)
{
	const BYTE expected[8] = { 0x15, 0x80, 0xFA, 0x03, 0x75, 0x10, 0xB8, 0x0E };

	uprintf("Patching COMMAND.COM...\n");
	if (filesize != 93040) {
		uprintf("  unexpected file size\n");
		return FALSE;
	}
	if (memcmp(&DiskImage[filestart+0x650c], expected, sizeof(expected)) != 0) {
		uprintf("  unexpected binary data\n");
		return FALSE;
	}
	DiskImage[filestart+0x6510] = 0xeb;
	return TRUE;
}

static BOOL Patch_IO_SYS(size_t filestart, size_t filesize)
{
	const BYTE expected[8] = { 0xFA, 0x80, 0x75, 0x09, 0x8D, 0xB6, 0x99, 0x00 };

	uprintf("Patching IO.SYS...\n");
	if (filesize != 116736) {
		uprintf("  unexpected file size\n");
		return FALSE;
	}
	if (memcmp(&DiskImage[filestart+0x3a8], expected, sizeof(expected)) != 0) {
		uprintf("  unexpected binary data\n");
		return FALSE;
	}
	DiskImage[filestart+0x3aa] = 0xeb;
	return TRUE;
}

/* Extract the file identified by FAT RootDir index 'entry' to 'path' */
static BOOL ExtractFAT(int entry, const char* path)
{
	HANDLE hFile;
	DWORD Size;
	char filename[MAX_PATH];
	size_t i, pos, fnamepos;
	size_t filestart, filesize;
	FAT_DATETIME LastAccessTime;
	LARGE_INTEGER liCreationTime, liLastAccessTime, liLastWriteTime;
	FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
	PDIR_ENTRY dir_entry = (PDIR_ENTRY)&DiskImage[FAT12_ROOTDIR_OFFSET + entry*FAT_BYTES_PER_DIRENT];

	if ((path == NULL) || ((safe_strlen(path) + 14) > sizeof(filename))) {
		uprintf("invalid path supplied for MS-DOS FAT extraction\n");
		return FALSE;
	}
	strcpy(filename, path);
	pos = strlen(path);
	fnamepos = pos;

	for(i=0; i<8; i++) {
		if (dir_entry->FileName[i] == ' ')
			break;
		filename[pos++] = dir_entry->FileName[i];
	}
	filename[pos++] = '.';
	for (i=8; i<11; i++) {
		if (dir_entry->FileName[i] == ' ')
			break;
		filename[pos++] = dir_entry->FileName[i];
	}
	filename[pos] = 0;
	filestart = (dir_entry->FirstCluster + FAT12_CLUSTER_OFFSET)*FAT12_CLUSTER_SIZE;
	filesize = dir_entry->FileSize;
	if ((filestart + filesize) > DiskImageSize) {
		uprintf("FAT File %s would be out of bounds: %X, %X\n", filename, filestart, filesize);
		uprintf("%X, %X\n", dir_entry->FirstCluster, dir_entry->FileSize);
		return FALSE;
	}

	/* WinME DOS files need to be patched */
	if (strcmp(&filename[fnamepos], "COMMAND.COM") == 0) {
		Patch_COMMAND_COM(filestart, filesize);
	} else if (strcmp(&filename[fnamepos], "IO.SYS") == 0) {
		Patch_IO_SYS(filestart, filesize);
	}

	/* Create a file, using the same attributes as found in the FAT */
	hFile = CreateFileA(filename, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ,
		NULL, CREATE_ALWAYS, dir_entry->Attributes, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		uprintf("Unable to create file '%s': %s.\n", filename, WindowsErrorString());
		return FALSE;
	}

	if (!WriteFileWithRetry(hFile, &DiskImage[filestart], (DWORD)filesize, &Size, WRITE_RETRIES)) {
		uprintf("Could not write file '%s': %s.\n", filename, WindowsErrorString());
		safe_closehandle(hFile);
		return FALSE;
	}

	/* Restore timestamps from FAT */
	FatDateTimeToSystemTime(&liCreationTime, &dir_entry->CreationDateTime, dir_entry->CreationTimeTenMs);
	ftCreationTime.dwHighDateTime = liCreationTime.HighPart;
	ftCreationTime.dwLowDateTime = liCreationTime.LowPart;
	LastAccessTime.Value = 0;
	LastAccessTime.Date = dir_entry->LastAccessDate;
	FatDateTimeToSystemTime(&liLastAccessTime, &LastAccessTime, 0);
	ftLastAccessTime.dwHighDateTime = liLastAccessTime.HighPart;
	ftLastAccessTime.dwLowDateTime = liLastAccessTime.LowPart;
	FatDateTimeToSystemTime(&liLastWriteTime, &dir_entry->LastWriteDateTime, 0);
	ftLastWriteTime.dwHighDateTime = liLastWriteTime.HighPart;
	ftLastWriteTime.dwLowDateTime = liLastWriteTime.LowPart;
	if (!SetFileTime(hFile, &ftCreationTime, &ftLastAccessTime, &ftLastWriteTime)) {
		uprintf("Could not set timestamps: %s\n", WindowsErrorString());
	}

	safe_closehandle(hFile);
	uprintf("Successfully wrote '%s' (%d bytes)\n", filename, filesize);

	return TRUE;
}

/* Extract the MS-DOS files contained in the FAT12 1.4MB floppy
   image included as resource "BINFILE" in diskcopy.dll */
static BOOL ExtractMSDOS(const char* path)
{
	char dllname[MAX_PATH] = "C:\\Windows\\System32";
	int i, j;
	UINT len;
	BOOL r = FALSE;
	HMODULE hDLL = NULL;
	char locale_path[MAX_PATH];
	char* extractlist[] = { "MSDOS   SYS", "COMMAND COM", "IO      SYS", "MODE    COM",
		"KEYB    COM", "KEYBOARDSYS", "KEYBRD2 SYS", "KEYBRD3 SYS", "KEYBRD4 SYS",
		"DISPLAY SYS", "EGA     CPI", "EGA2    CPI", "EGA3    CPI" };

	if (path == NULL)
		return FALSE;

	// Reduce the visible mess by placing all the locale files into a subdir
	static_strcpy(locale_path, path);
	static_strcat(locale_path, "LOCALE\\");
	CreateDirectoryA(locale_path, NULL);

	len = GetSystemDirectoryA(dllname, sizeof(dllname));
	if ((len == 0) || (len >= sizeof(dllname))) {
		uprintf("Unable to get system directory: %s\n", WindowsErrorString());
		goto out;
	}
	static_strcat(dllname, "\\diskcopy.dll");
	hDLL = LoadLibraryA(dllname);
	if (hDLL == NULL) {
		uprintf("Unable to open %s: %s\n", dllname, WindowsErrorString());
		goto out;
	}

	DiskImage = (BYTE*)GetResource(hDLL, MAKEINTRESOURCEA(1), "BINFILE", "disk image", &DiskImageSize, TRUE);
	if (DiskImage == NULL)
		goto out;

	// Sanity check
	if (DiskImageSize < 700*KB) {
		uprintf("MS-DOS disk image is too small (%d bytes)\n", dllname, DiskImageSize);
		goto out;
	}

	for (i=0, r=TRUE; r && i<FAT_FN_DIR_ENTRY_LAST; i++) {
		if (DiskImage[FAT12_ROOTDIR_OFFSET + i*FAT_BYTES_PER_DIRENT] == FAT_DIRENT_DELETED)
			continue;
		for (j=0; r && j<ARRAYSIZE(extractlist); j++) {
			if (memcmp(extractlist[j], &DiskImage[FAT12_ROOTDIR_OFFSET + i*FAT_BYTES_PER_DIRENT], 8+3) == 0) {
				r = ExtractFAT(i, (j<3)?path:locale_path);
				if ((j == 2) || (j == 7) || (j == 12))
					UpdateProgress(OP_DOS, -1.0f);
			}
		}
	}
	if (r)
		r = SetDOSLocale(path, FALSE);

out:
	if (hDLL != NULL)
		FreeLibrary(hDLL);
	safe_free(DiskImage);
	return r;
}

/* Extract the FreeDOS files embedded in the app */
BOOL ExtractFreeDOS(const char* path)
{
	const char* res_name[] = { "COMMAND.COM", "KERNEL.SYS", "DISPLAY.EXE", "KEYB.EXE",
		"MODE.COM", "KEYBOARD.SYS", "KEYBRD2.SYS", "KEYBRD3.SYS", "KEYBRD4.SYS", "EGA.CPX",
		"EGA2.CPX", "EGA3.CPX", "EGA4.CPX", "EGA5.CPX", "EGA6.CPX",
		"EGA7.CPX", "EGA8.CPX", "EGA9.CPX", "EGA10.CPX", "EGA11.CPX",
		"EGA12.CPX", "EGA13.CPX", "EGA14.CPX", "EGA15.CPX", "EGA16.CPX",
		"EGA17.CPX", "EGA18.CPX" };
	const int res_id[ARRAYSIZE(res_name)] = { IDR_FD_COMMAND_COM, IDR_FD_KERNEL_SYS, IDR_FD_DISPLAY_EXE, IDR_FD_KEYB_EXE,
		IDR_FD_MODE_COM, IDR_FD_KB1_SYS, IDR_FD_KB2_SYS, IDR_FD_KB3_SYS, IDR_FD_KB4_SYS, IDR_FD_EGA1_CPX,
		IDR_FD_EGA2_CPX, IDR_FD_EGA3_CPX, IDR_FD_EGA4_CPX, IDR_FD_EGA5_CPX, IDR_FD_EGA6_CPX,
		IDR_FD_EGA7_CPX, IDR_FD_EGA8_CPX, IDR_FD_EGA9_CPX, IDR_FD_EGA10_CPX, IDR_FD_EGA11_CPX,
		IDR_FD_EGA12_CPX, IDR_FD_EGA13_CPX, IDR_FD_EGA14_CPX, IDR_FD_EGA15_CPX, IDR_FD_EGA16_CPX,
		IDR_FD_EGA17_CPX, IDR_FD_EGA18_CPX };
	char filename[MAX_PATH], locale_path[MAX_PATH];
	BYTE* res_data;
	DWORD res_size, Size;
	HANDLE hFile;
	int i;

	if ((path == NULL) || ((safe_strlen(path) + 14) > sizeof(filename))) {
		uprintf("invalid path supplied for FreeDOS extraction\n");
		return FALSE;
	}

	// Reduce the visible mess by placing all the locale files into a subdir
	static_strcpy(locale_path, path);
	static_strcat(locale_path, "LOCALE\\");
	CreateDirectoryA(locale_path, NULL);

	for (i=0; i<ARRAYSIZE(res_name); i++) {
		res_data = (BYTE*)GetResource(hMainInstance, MAKEINTRESOURCEA(res_id[i]), _RT_RCDATA, res_name[i], &res_size, FALSE);

		static_strcpy(filename, ((i<2)?path:locale_path));
		static_strcat(filename, res_name[i]);

		hFile = CreateFileA(filename, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ, NULL,
			CREATE_ALWAYS, (i<2)?(FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM):FILE_ATTRIBUTE_NORMAL, NULL);
		if ((hFile == NULL) || (hFile == INVALID_HANDLE_VALUE)) {
			uprintf("Unable to create file '%s': %s.\n", filename, WindowsErrorString());
			return FALSE;
		}

		if (!WriteFileWithRetry(hFile, res_data, res_size, &Size, WRITE_RETRIES)) {
			uprintf("Could not write file '%s': %s.\n", filename, WindowsErrorString());
			safe_closehandle(hFile);
			return FALSE;
		}

		// We don't restore timestamps for FreeDOS, as there's no metadata in the files,
		// thus we would need to have a separate header with each file's timestamps

		safe_closehandle(hFile);
		uprintf("Successfully wrote '%s' (%d bytes)\n", filename, res_size);

		if ((i == 4) || (i == 10) || (i == 16) || (i == 22) || (i == ARRAYSIZE(res_name)-1))
			UpdateProgress(OP_DOS, -1.0f);
	}

	return SetDOSLocale(path, TRUE);
}

BOOL ExtractDOS(const char* path)
{
	switch(ComboBox_GetItemData(hBootType, ComboBox_GetCurSel(hBootType))) {
	case BT_MSDOS:
		return ExtractMSDOS(path);
	case BT_FREEDOS:
		return ExtractFreeDOS(path);
	}
	return FALSE;
}
