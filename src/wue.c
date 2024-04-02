/*
 * Rufus: The Reliable USB Formatting Utility
 * Windows User Experience
 * Copyright © 2022-2024 Pete Batard <pete@akeo.ie>
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
#include <assert.h>

#include "rufus.h"
#include "vhd.h"
#include "drive.h"
#include "format.h"
#include "missing.h"
#include "resource.h"
#include "registry.h"
#include "msapi_utf8.h"
#include "localization.h"

 /* Memory leaks detection - define _CRTDBG_MAP_ALLOC as preprocessor macro */
#ifdef _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

const char* bypass_name[] = { "BypassTPMCheck", "BypassSecureBootCheck", "BypassRAMCheck" };

int unattend_xml_flags = 0, wintogo_index = -1, wininst_index = 0;
int unattend_xml_mask = UNATTEND_DEFAULT_SELECTION_MASK;
char *unattend_xml_path = NULL, unattend_username[MAX_USERNAME_LENGTH];
BOOL is_bootloader_revoked = FALSE;

extern uint32_t wim_nb_files, wim_proc_files, wim_extra_files;
extern BOOL validate_md5sum;
extern uint64_t md5sum_totalbytes;
extern StrArray modified_files;

/// <summary>
/// Create an installation answer file containing the sections specified by the flags.
/// </summary>
/// <param name="arch">The processor architecture of the Windows image being used.</param>
/// <param name="flags">A bitmask representing the sections to enable.
/// See "Windows User Experience flags and masks" from rufus.h</param>
/// <returns>The path of a newly created answer file on success or NULL on error.</returns>
char* CreateUnattendXml(int arch, int flags)
{
	static char path[MAX_PATH];
	FILE* fd;
	int i, order;
	const char* xml_arch_names[5] = { "x86", "amd64", "arm", "arm64" };
	unattend_xml_flags = flags;
	if (arch < ARCH_X86_32 || arch > ARCH_ARM_64 || flags == 0) {
		uprintf("Note: No Windows User Experience options selected");
		return NULL;
	}
	arch--;
	// coverity[swapped_arguments]
	if (GetTempFileNameU(temp_dir, APPLICATION_NAME, 0, path) == 0)
		return NULL;
	fd = fopen(path, "w");
	if (fd == NULL)
		return NULL;

	uprintf("Selected Windows User Experience options:");
	fprintf(fd, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
	fprintf(fd, "<unattend xmlns=\"urn:schemas-microsoft-com:unattend\">\n");

	// This part produces the unbecoming display of a command prompt window during initial setup as well
	// as alters the layout and options of the initial Windows installer screens, which may scare users.
	// So, in format.c, we'll try to insert the registry keys directly and drop this section. However,
	// because Microsoft prevents Store apps from editing an offline registry, we do need this fallback.
	if (flags & UNATTEND_WINPE_SETUP_MASK) {
		order = 1;
		fprintf(fd, "  <settings pass=\"windowsPE\">\n");
		fprintf(fd, "    <component name=\"Microsoft-Windows-Setup\" processorArchitecture=\"%s\" language=\"neutral\" "
			"xmlns:wcm=\"http://schemas.microsoft.com/WMIConfig/2002/State\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
			"publicKeyToken=\"31bf3856ad364e35\" versionScope=\"nonSxS\">\n", xml_arch_names[arch]);
		// WinPE will complain if we don't provide a product key. *Any* product key. This is soooo idiotic...
		fprintf(fd, "      <UserData>\n");
		fprintf(fd, "        <ProductKey>\n");
		fprintf(fd, "          <Key />\n");
		fprintf(fd, "        </ProductKey>\n");
		fprintf(fd, "      </UserData>\n");
		if (flags & UNATTEND_SECUREBOOT_TPM_MINRAM) {
			uprintf("• Bypass SB/TPM/RAM");
			fprintf(fd, "      <RunSynchronous>\n");
			for (i = 0; i < ARRAYSIZE(bypass_name); i++) {
				fprintf(fd, "        <RunSynchronousCommand wcm:action=\"add\">\n");
				fprintf(fd, "          <Order>%d</Order>\n", order++);
				fprintf(fd, "          <Path>reg add HKLM\\SYSTEM\\Setup\\LabConfig /v %s /t REG_DWORD /d 1 /f</Path>\n", bypass_name[i]);
				fprintf(fd, "        </RunSynchronousCommand>\n");
			}
			fprintf(fd, "      </RunSynchronous>\n");
		}
		fprintf(fd, "    </component>\n");
		fprintf(fd, "  </settings>\n");
	}

	if (flags & UNATTEND_SPECIALIZE_DEPLOYMENT_MASK) {
		order = 1;
		fprintf(fd, "  <settings pass=\"specialize\">\n");
		fprintf(fd, "    <component name=\"Microsoft-Windows-Deployment\" processorArchitecture=\"%s\" language=\"neutral\" "
			"xmlns:wcm=\"http://schemas.microsoft.com/WMIConfig/2002/State\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
			"publicKeyToken=\"31bf3856ad364e35\" versionScope=\"nonSxS\">\n", xml_arch_names[arch]);
		fprintf(fd, "      <RunSynchronous>\n");
		// This part was picked from https://github.com/AveYo/MediaCreationTool.bat/blob/main/bypass11/AutoUnattend.xml
		// NB: This is INCOMPATIBLE with S-Mode below
		if (flags & UNATTEND_NO_ONLINE_ACCOUNT) {
			uprintf("• Bypass online account requirement");
			fprintf(fd, "        <RunSynchronousCommand wcm:action=\"add\">\n");
			fprintf(fd, "          <Order>%d</Order>\n", order++);
			fprintf(fd, "          <Path>reg add HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\OOBE /v BypassNRO /t REG_DWORD /d 1 /f</Path>\n");
			fprintf(fd, "        </RunSynchronousCommand>\n");
		}
		fprintf(fd, "      </RunSynchronous>\n");
		fprintf(fd, "    </component>\n");
		fprintf(fd, "  </settings>\n");
	}

	if (flags & UNATTEND_OOBE_MASK) {
		order = 1;
		fprintf(fd, "  <settings pass=\"oobeSystem\">\n");
		if (flags & UNATTEND_OOBE_SHELL_SETUP_MASK) {
			fprintf(fd, "    <component name=\"Microsoft-Windows-Shell-Setup\" processorArchitecture=\"%s\" language=\"neutral\" "
				"xmlns:wcm=\"http://schemas.microsoft.com/WMIConfig/2002/State\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
				"publicKeyToken=\"31bf3856ad364e35\" versionScope=\"nonSxS\">\n", xml_arch_names[arch]);
			// https://docs.microsoft.com/en-us/windows-hardware/customize/desktop/unattend/microsoft-windows-shell-setup-oobe-protectyourpc
			// It is really super insidous of Microsoft to call this option "ProtectYourPC", when it's really only about
			// data collection. But of course, if it was called "AllowDataCollection", everyone would turn it off...
			if (flags & UNATTEND_NO_DATA_COLLECTION) {
				uprintf("• Disable data collection");
				fprintf(fd, "      <OOBE>\n");
				fprintf(fd, "        <ProtectYourPC>3</ProtectYourPC>\n");
				fprintf(fd, "      </OOBE>\n");
			}
			if (flags & UNATTEND_SET_USER) {
				if ((unattend_username[0] == 0) || (stricmp(unattend_username, "Administrator") == 0) ||
					(stricmp(unattend_username, "Guest") == 0)) {
					uprintf("WARNING: '%s' is not allowed as local account name - Option ignored", unattend_username);
				} else {
					uprintf("• Use '%s' for local account name", unattend_username);
					// If we create a local account in unattend.xml, then we can get Windows 11
					// 22H2 to skip MSA even if the network is connected during installation.
					fprintf(fd, "      <UserAccounts>\n");
					fprintf(fd, "        <LocalAccounts>\n");
					fprintf(fd, "          <LocalAccount wcm:action=\"add\">\n");
					fprintf(fd, "            <Name>%s</Name>\n", unattend_username);
					fprintf(fd, "            <DisplayName>%s</DisplayName>\n", unattend_username);
					fprintf(fd, "            <Group>Administrators;Power Users</Group>\n");
					// Sets an empty password for the account (which, in Microsoft's convoluted ways,
					// needs to be initialized to the Base64 encoded UTF-16 string "Password").
					// The use of an empty password has both the advantage of not having to ask users
					// to type in a password in Rufus (which they might be weary of) as well as allowing
					// automated logon during setup.
					fprintf(fd, "            <Password>\n");
					fprintf(fd, "              <Value>UABhAHMAcwB3AG8AcgBkAA==</Value>\n");
					fprintf(fd, "              <PlainText>false</PlainText>\n");
					fprintf(fd, "            </Password>\n");
					fprintf(fd, "          </LocalAccount>\n");
					fprintf(fd, "        </LocalAccounts>\n");
					fprintf(fd, "      </UserAccounts>\n");
					// Since we set a blank password, we'll ask the user to change it at next logon.
					fprintf(fd, "      <FirstLogonCommands>\n");
					fprintf(fd, "        <SynchronousCommand wcm:action=\"add\">\n");
					fprintf(fd, "          <Order>%d</Order>\n", order++);
					fprintf(fd, "          <CommandLine>net user &quot;%s&quot; /logonpasswordchg:yes</CommandLine>\n", unattend_username);
					fprintf(fd, "        </SynchronousCommand>\n");
					fprintf(fd, "      </FirstLogonCommands>\n");
				}
			}
			fprintf(fd, "    </component>\n");
		}
		if (flags & UNATTEND_OOBE_INTERNATIONAL_MASK) {
			uprintf("• Use the same regional options as this user's");
			fprintf(fd, "    <component name=\"Microsoft-Windows-International-Core\" processorArchitecture=\"%s\" language=\"neutral\" "
				"xmlns:wcm=\"http://schemas.microsoft.com/WMIConfig/2002/State\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
				"publicKeyToken=\"31bf3856ad364e35\" versionScope=\"nonSxS\">\n", xml_arch_names[arch]);
			// What a frigging mess retreiving and trying to match the various locales
			// Microsoft has made. And, *NO*, the new User Language Settings have not
			// improved things in the slightest. They made it much worse for developers!
			fprintf(fd, "      <InputLocale>%s</InputLocale>\n",
				ReadRegistryKeyStr(REGKEY_HKCU, "Keyboard Layout\\Preload\\1"));
			fprintf(fd, "      <SystemLocale>%s</SystemLocale>\n", ToLocaleName(GetSystemDefaultLCID()));
			fprintf(fd, "      <UserLocale>%s</UserLocale>\n", ToLocaleName(GetUserDefaultLCID()));
			fprintf(fd, "      <UILanguage>%s</UILanguage>\n", ToLocaleName(GetUserDefaultUILanguage()));
			fprintf(fd, "      <UILanguageFallback>%s</UILanguageFallback>\n",
				// NB: Officially, this is a REG_MULTI_SZ string
				ReadRegistryKeyStr(REGKEY_HKLM, "SYSTEM\\CurrentControlSet\\Control\\Nls\\Language\\InstallLanguageFallback"));
			fprintf(fd, "    </component>\n");
		}
		if (flags & UNATTEND_DISABLE_BITLOCKER) {
			uprintf("• Disable bitlocker");
			fprintf(fd, "    <component name=\"Microsoft-Windows-SecureStartup-FilterDriver\" processorArchitecture=\"%s\" language=\"neutral\" "
				"xmlns:wcm=\"http://schemas.microsoft.com/WMIConfig/2002/State\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
				"publicKeyToken=\"31bf3856ad364e35\" versionScope=\"nonSxS\">\n", xml_arch_names[arch]);
			fprintf(fd, "      <PreventDeviceEncryption>true</PreventDeviceEncryption>\n");
			fprintf(fd, "    </component>\n");
			fprintf(fd, "    <component name=\"Microsoft-Windows-EnhancedStorage-Adm\" processorArchitecture=\"%s\" language=\"neutral\" "
				"xmlns:wcm=\"http://schemas.microsoft.com/WMIConfig/2002/State\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
				"publicKeyToken=\"31bf3856ad364e35\" versionScope=\"nonSxS\">\n", xml_arch_names[arch]);
			fprintf(fd, "      <TCGSecurityActivationDisabled>1</TCGSecurityActivationDisabled>\n");
			fprintf(fd, "    </component>\n");
		}
		fprintf(fd, "  </settings>\n");
	}

	if (flags & UNATTEND_OFFLINE_SERVICING_MASK) {
		fprintf(fd, "  <settings pass=\"offlineServicing\">\n");
		if (flags & UNATTEND_OFFLINE_INTERNAL_DRIVES) {
			uprintf("• Set internal drives offline");
			fprintf(fd, "    <component name=\"Microsoft-Windows-PartitionManager\" processorArchitecture=\"%s\" language=\"neutral\" "
				"xmlns:wcm=\"http://schemas.microsoft.com/WMIConfig/2002/State\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
				"publicKeyToken=\"31bf3856ad364e35\" versionScope=\"nonSxS\">\n", xml_arch_names[arch]);
			fprintf(fd, "      <SanPolicy>4</SanPolicy>\n");
			fprintf(fd, "    </component>\n");
		}
		if (flags & UNATTEND_FORCE_S_MODE) {
			uprintf("• Enforce S Mode");
			fprintf(fd, "    <component name=\"Microsoft-Windows-CodeIntegrity\" processorArchitecture=\"%s\" language=\"neutral\" "
				"xmlns:wcm=\"http://schemas.microsoft.com/WMIConfig/2002/State\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
				"publicKeyToken=\"31bf3856ad364e35\" versionScope=\"nonSxS\">\n", xml_arch_names[arch]);
			fprintf(fd, "      <SkuPolicyRequired>1</SkuPolicyRequired>\n");
			fprintf(fd, "    </component>\n");
		}
		fprintf(fd, "  </settings>\n");
	}

	fprintf(fd, "</unattend>\n");
	fclose(fd);
	return path;
}

/// <summary>
/// Setup and patch WinPE for Windows XP bootable USBs.
/// </summary>
/// <param name="drive_letter">The letter identifying the target drive.</param>
/// <returns>TRUE on success, FALSE on error.</returns>
BOOL SetupWinPE(char drive_letter)
{
	char src[64], dst[32];
	const char* basedir[3] = { "i386", "amd64", "minint" };
	const char* patch_str_org[2] = { "\\minint\\txtsetup.sif", "\\minint\\system32\\" };
	const char* patch_str_rep[2][2] = { { "\\i386\\txtsetup.sif", "\\i386\\system32\\" } ,
										{ "\\amd64\\txtsetup.sif", "\\amd64\\system32\\" } };
	const char* setupsrcdev = "SetupSourceDevice = \"\\device\\harddisk1\\partition1\"";
	const char* win_nt_bt_org = "$win_nt$.~bt";
	const char* rdisk_zero = "rdisk(0)";
	const LARGE_INTEGER liZero = { {0, 0} };
	HANDLE handle = INVALID_HANDLE_VALUE;
	DWORD i, j, size, read_size, index = 0;
	BOOL r = FALSE;
	char* buffer = NULL;

	if ((img_report.winpe & WINPE_AMD64) == WINPE_AMD64)
		index = 1;
	else if ((img_report.winpe & WINPE_MININT) == WINPE_MININT)
		index = 2;
	// Copy of ntdetect.com in root
	static_sprintf(src, "%c:\\%s\\ntdetect.com", toupper(drive_letter), basedir[2 * (index / 2)]);
	static_sprintf(dst, "%c:\\ntdetect.com", toupper(drive_letter));
	CopyFileA(src, dst, TRUE);
	if (!img_report.uses_minint) {
		// Create a copy of txtsetup.sif, as we want to keep the i386/amd64 files unmodified
		static_sprintf(src, "%c:\\%s\\txtsetup.sif", toupper(drive_letter), basedir[index]);
		static_sprintf(dst, "%c:\\txtsetup.sif", toupper(drive_letter));
		if (!CopyFileA(src, dst, TRUE)) {
			uprintf("Did not copy %s as %s: %s\n", src, dst, WindowsErrorString());
		}
		if (insert_section_data(dst, "[SetupData]", setupsrcdev, FALSE) == NULL) {
			uprintf("Failed to add SetupSourceDevice in %s\n", dst);
			goto out;
		}
		uprintf("Successfully added '%s' to %s\n", setupsrcdev, dst);
	}

	static_sprintf(src, "%c:\\%s\\setupldr.bin", toupper(drive_letter), basedir[2 * (index / 2)]);
	static_sprintf(dst, "%c:\\BOOTMGR", toupper(drive_letter));
	if (!CopyFileA(src, dst, TRUE)) {
		uprintf("Did not copy %s as %s: %s\n", src, dst, WindowsErrorString());
	}

	// \minint with /minint option doesn't require further processing => return true
	// \minint and no \i386 without /minint is unclear => return error
	if (img_report.winpe & WINPE_MININT) {
		if (img_report.uses_minint) {
			uprintf("Detected \\minint directory with /minint option: nothing to patch\n");
			r = TRUE;
		} else if (!(img_report.winpe & (WINPE_I386 | WINPE_AMD64))) {
			uprintf("Detected \\minint directory only but no /minint option: not sure what to do\n");
		}
		goto out;
	}

	// At this stage we only handle \i386
	handle = CreateFileA(dst, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (handle == INVALID_HANDLE_VALUE) {
		uprintf("Could not open %s for patching: %s\n", dst, WindowsErrorString());
		goto out;
	}
	size = GetFileSize(handle, NULL);
	if (size == INVALID_FILE_SIZE) {
		uprintf("Could not get size for file %s: %s\n", dst, WindowsErrorString());
		goto out;
	}
	buffer = (char*)malloc(size);
	if (buffer == NULL)
		goto out;
	if ((!ReadFile(handle, buffer, size, &read_size, NULL)) || (size != read_size)) {
		uprintf("Could not read file %s: %s\n", dst, WindowsErrorString());
		goto out;
	}
	if (!SetFilePointerEx(handle, liZero, NULL, FILE_BEGIN)) {
		uprintf("Could not rewind file %s: %s\n", dst, WindowsErrorString());
		goto out;
	}

	// Patch setupldr.bin
	uprintf("Patching file %s\n", dst);
	// Remove CRC check for 32 bit part of setupldr.bin from Win2k3
	if ((size > 0x2061) && (buffer[0x2060] == 0x74) && (buffer[0x2061] == 0x03)) {
		buffer[0x2060] = 0xeb;
		buffer[0x2061] = 0x1a;
		uprintf("  0x00002060: 0x74 0x03 -> 0xEB 0x1A (disable Win2k3 CRC check)\n");
	}
	for (i = 1; i < size - 32; i++) {
		for (j = 0; j < ARRAYSIZE(patch_str_org); j++) {
			if (safe_strnicmp(&buffer[i], patch_str_org[j], strlen(patch_str_org[j]) - 1) == 0) {
				assert(index < 2);
				uprintf("  0x%08X: '%s' -> '%s'\n", i, &buffer[i], patch_str_rep[index][j]);
				strcpy(&buffer[i], patch_str_rep[index][j]);
				i += (DWORD)max(strlen(patch_str_org[j]), strlen(patch_str_rep[index][j]));	// in case org is a substring of rep
			}
		}
	}

	if (!img_report.uses_minint) {
		// Additional setupldr.bin/bootmgr patching
		for (i = 0; i < size - 32; i++) {
			// rdisk(0) -> rdisk(#) disk masquerading
			// NB: only the first one seems to be needed
			if (safe_strnicmp(&buffer[i], rdisk_zero, strlen(rdisk_zero) - 1) == 0) {
				buffer[i + 6] = 0x31;
				uprintf("  0x%08X: '%s' -> 'rdisk(%c)'\n", i, rdisk_zero, buffer[i + 6]);
			}
			// $WIN_NT$_~BT -> i386/amd64
			if (safe_strnicmp(&buffer[i], win_nt_bt_org, strlen(win_nt_bt_org) - 1) == 0) {
				uprintf("  0x%08X: '%s' -> '%s%s'\n", i, &buffer[i], basedir[index], &buffer[i + strlen(win_nt_bt_org)]);
				strcpy(&buffer[i], basedir[index]);
				// This ensures that we keep the terminator backslash
				buffer[i + strlen(basedir[index])] = buffer[i + strlen(win_nt_bt_org)];
				buffer[i + strlen(basedir[index]) + 1] = 0;
			}
		}
	}

	if (!WriteFileWithRetry(handle, buffer, size, NULL, WRITE_RETRIES)) {
		uprintf("Could not write patched file: %s\n", WindowsErrorString());
		goto out;
	}
	r = TRUE;

out:
	safe_closehandle(handle);
	safe_free(buffer);
	return r;
}

/// <summary>
/// Populate the img_report Window version from an install[.wim|.esd] XML index
/// </summary>
/// <param name="xml_file">The path of the extracted index XML.</param>
/// <param name="index">The index of the occurrence to look for.</param>
static void PopulateWindowsVersionFromXml(const char* xml_file, int index)
{
	char* val;

	val = get_token_data_file_indexed("MAJOR", xml_file, index);
	img_report.win_version.major = (uint16_t)safe_atoi(val);
	free(val);
	val = get_token_data_file_indexed("MINOR", xml_file, index);
	img_report.win_version.minor = (uint16_t)safe_atoi(val);
	free(val);
	val = get_token_data_file_indexed("BUILD", xml_file, index);
	img_report.win_version.build = (uint16_t)safe_atoi(val);
	free(val);
	val = get_token_data_file_indexed("SPBUILD", xml_file, index);
	img_report.win_version.revision = (uint16_t)safe_atoi(val);
	free(val);
	// Adjust versions so that we produce a more accurate report in the log
	// (and yeah, I know we won't properly report Server, but I don't care)
	if (img_report.win_version.major <= 5) {
		// Don't want to support XP or earlier
		img_report.win_version.major = 0;
		img_report.win_version.minor = 0;
	} else if (img_report.win_version.major == 6) {
		// Don't want to support Vista
		if (img_report.win_version.minor == 0) {
			img_report.win_version.major = 0;
		} else if (img_report.win_version.minor == 1) {
			img_report.win_version.major = 7;
			img_report.win_version.minor = 0;
		} else if (img_report.win_version.minor == 2) {
			img_report.win_version.major = 8;
			img_report.win_version.minor = 0;
		} else if (img_report.win_version.minor == 3) {
			img_report.win_version.major = 8;
			img_report.win_version.minor = 1;
		} else if (img_report.win_version.minor == 4) {
			img_report.win_version.major = 10;
			img_report.win_version.minor = 0;
		}
	} else if (img_report.win_version.major == 10) {
		if (img_report.win_version.build > 20000)
			img_report.win_version.major = 11;
	}
}

/// <summary>
/// Populate the img_report Window version from an an install[.wim|.esd], mounting the
/// ISO if needed. Requires Windows 8 or later.
/// </summary>
/// <param name="">(none)</param>
/// <returns>TRUE on success, FALSE if we couldn't populate the version.</returns>
BOOL PopulateWindowsVersion(void)
{
	char *mounted_iso, mounted_image_path[128];
	char xml_file[MAX_PATH] = "";

	memset(&img_report.win_version, 0, sizeof(img_report.win_version));

	if ((WindowsVersion.Version < WINDOWS_8) || ((WimExtractCheck(TRUE) & 4) == 0))
		return FALSE;

	// If we're not using a straight install.wim, we need to mount the ISO to access it
	if (!img_report.is_windows_img) {
		mounted_iso = VhdMountImage(image_path);
		if (mounted_iso == NULL) {
			uprintf("Could not mount Windows ISO for build number detection");
			return FALSE;
		}
		static_sprintf(mounted_image_path, "%s%s", mounted_iso, &img_report.wininst_path[0][2]);
	}

	// Now take a look at the XML file in install.wim to list our versions
	if ((GetTempFileNameU(temp_dir, APPLICATION_NAME, 0, xml_file) == 0) || (xml_file[0] == 0)) {
		// Last ditch effort to get a tmp file - just extract it to the current directory
		static_strcpy(xml_file, ".\\RufVXml.tmp");
	}
	// GetTempFileName() may leave a file behind
	DeleteFileU(xml_file);

	// Must use the Windows WIM API as 7z messes up the XML
	if (!WimExtractFile_API(img_report.is_windows_img ? image_path : mounted_image_path,
		0, "[1].xml", xml_file, TRUE)) {
		uprintf("Could not acquire WIM index");
		goto out;
	}

	PopulateWindowsVersionFromXml(xml_file, 1);

out:
	DeleteFileU(xml_file);
	if (!img_report.is_windows_img)
		VhdUnmountImage();

	return ((img_report.win_version.major != 0) && (img_report.win_version.build != 0));
}

// Copy this system's SkuSiPolicy.p7b to the target drive so that UEFI bootloaders
// revoked by Windows through WDAC policy do get flagged as revoked.
BOOL CopySKUSiPolicy(const char* drive_name)
{
	BOOL r = FALSE;
	char src[MAX_PATH], dst[MAX_PATH];
	struct __stat64 stat64 = { 0 };

	// Only copy SkuPolicy if we warned about the bootloader being revoked.
	if ((target_type != TT_UEFI) || !IS_WINDOWS_1X(img_report) ||
		(pe256ssp_size == 0) || !is_bootloader_revoked)
		return r;

	static_sprintf(src, "%s\\SecureBootUpdates\\SKUSiPolicy.p7b", system_dir);
	static_sprintf(dst, "%s\\EFI\\Microsoft\\Boot\\SKUSiPolicy.p7b", drive_name);
	if ((_stat64U(dst, &stat64) != 0) && (_stat64U(src, &stat64) == 0)) {
		uprintf("Copying: %s (%s) (from %s)", dst, SizeToHumanReadable(stat64.st_size, FALSE, FALSE), src);
		r = CopyFileU(src, dst, TRUE);
		if (!r)
			uprintf("  Error writing file: %s", WindowsErrorString());
	}

	return r;
}

/// <summary>
/// Checks which versions of Windows are available in an install image
/// to set our extraction index. Asks the user to select one if needed.
/// </summary>
/// <param name="">(none)</param>
/// <returns>-2 on user cancel, -1 on other error, >=0 on success.</returns>
int SetWinToGoIndex(void)
{
	char* mounted_iso, mounted_image_path[128];
	char xml_file[MAX_PATH] = "";
	char* install_names[MAX_WININST];
	StrArray version_name, version_index;
	int i;
	BOOL bNonStandard = FALSE;

	// Sanity checks
	wintogo_index = -1;
	wininst_index = 0;
	if ((WindowsVersion.Version < WINDOWS_8) || ((WimExtractCheck(FALSE) & 4) == 0) ||
		(ComboBox_GetCurItemData(hFileSystem) != FS_NTFS)) {
		return -1;
	}

	// If we have multiple windows install images, ask the user the one to use
	if (img_report.wininst_index > 1) {
		for (i = 0; i < img_report.wininst_index; i++)
			install_names[i] = &img_report.wininst_path[i][2];
		wininst_index = _log2(SelectionDialog(lmprintf(MSG_130), lmprintf(MSG_131), install_names, img_report.wininst_index));
		if (wininst_index < 0)
			return -2;
		if (wininst_index >= MAX_WININST)
			wininst_index = 0;
	}

	// If we're not using a straight install.wim, we need to mount the ISO to access it
	if (!img_report.is_windows_img) {
		mounted_iso = VhdMountImage(image_path);
		if (mounted_iso == NULL) {
			uprintf("Could not mount ISO for Windows To Go selection");
			return -1;
		}
		static_sprintf(mounted_image_path, "%s%s", mounted_iso, &img_report.wininst_path[wininst_index][2]);
	}

	// Now take a look at the XML file in install.wim to list our versions
	if ((GetTempFileNameU(temp_dir, APPLICATION_NAME, 0, xml_file) == 0) || (xml_file[0] == 0)) {
		// Last ditch effort to get a tmp file - just extract it to the current directory
		static_strcpy(xml_file, ".\\RufVXml.tmp");
	}
	// GetTempFileName() may leave a file behind
	DeleteFileU(xml_file);

	// Must use the Windows WIM API as 7z messes up the XML
	if (!WimExtractFile_API(img_report.is_windows_img ? image_path : mounted_image_path,
		0, "[1].xml", xml_file, FALSE)) {
		uprintf("Could not acquire WIM index");
		goto out;
	}

	StrArrayCreate(&version_name, 16);
	StrArrayCreate(&version_index, 16);
	for (i = 0; StrArrayAdd(&version_index, get_token_data_file_indexed("IMAGE INDEX", xml_file, i + 1), FALSE) >= 0; i++) {
		// Some people are apparently creating *unofficial* Windows ISOs that don't have DISPLAYNAME elements.
		// If we are parsing such an ISO, try to fall back to using DESCRIPTION. Of course, since we don't use
		// a formal XML parser, if an ISO mixes entries with both DISPLAYNAME and DESCRIPTION and others with
		// only DESCRIPTION, the version names we report will be wrong.
		// But hey, there's only so far I'm willing to go to help people who, not content to have demonstrated
		// their utter ignorance on development matters, are also trying to lecture experienced developers
		// about specific "noob mistakes"... that don't exist in the code they are trying to criticize.
		if (StrArrayAdd(&version_name, get_token_data_file_indexed("DISPLAYNAME", xml_file, i + 1), FALSE) < 0) {
			bNonStandard = TRUE;
			if (StrArrayAdd(&version_name, get_token_data_file_indexed("DESCRIPTION", xml_file, i + 1), FALSE) < 0) {
				uprintf("Warning: Could not find a description for image index %d", i + 1);
				StrArrayAdd(&version_name, "Unknown Windows Version", TRUE);
			}
		}
	}
	if (bNonStandard)
		uprintf("Warning: Nonstandard Windows image (missing <DISPLAYNAME> entries)");

	if (i > 1)
		// NB: _log2 returns -2 if SelectionDialog() returns negative (user cancelled)
		i = _log2(SelectionDialog(lmprintf(MSG_291), lmprintf(MSG_292), version_name.String, i)) + 1;
	if (i < 0)
		wintogo_index = -2;	// Cancelled by the user
	else if (i == 0)
		wintogo_index = 1;
	else
		wintogo_index = atoi(version_index.String[i - 1]);
	if (i > 0) {
		// re-populate the version data from the selected XML index
		PopulateWindowsVersionFromXml(xml_file, i);
		// If we couldn't obtain the major and build, we have a problem
		if (img_report.win_version.major == 0 || img_report.win_version.build == 0)
			uprintf("Warning: Could not obtain version information from XML index (Nonstandard Windows image?)");
		uprintf("Will use '%s' (Build: %d, Index %s) for Windows To Go",
			version_name.String[i - 1], img_report.win_version.build, version_index.String[i - 1]);
		// Need Windows 10 Creator Update or later for boot on REMOVABLE to work
		if ((img_report.win_version.build < 15000) && (SelectedDrive.MediaType != FixedMedia)) {
			if (MessageBoxExU(hMainDialog, lmprintf(MSG_098), lmprintf(MSG_190),
				MB_YESNO | MB_ICONWARNING | MB_IS_RTL, selected_langid) != IDYES)
				wintogo_index = -2;
		}
		// Display a notice about WppRecorder.sys for 1809 ISOs
		if (img_report.win_version.build == 17763) {
			notification_info more_info;
			more_info.id = MORE_INFO_URL;
			more_info.url = WPPRECORDER_MORE_INFO_URL;
			Notification(MSG_INFO, NULL, &more_info, lmprintf(MSG_128, "Windows To Go"), lmprintf(MSG_133));
		}
	}
	StrArrayDestroy(&version_name);
	StrArrayDestroy(&version_index);

out:
	DeleteFileU(xml_file);
	if (!img_report.is_windows_img)
		VhdUnmountImage();
	return wintogo_index;
}

/// <summary>
/// Setup a Windows To Go drive according to the official Microsoft instructions detailed at:
/// https://docs.microsoft.com/en-us/previous-versions/windows/it-pro/windows-8.1-and-8/jj721578(v=ws.11).
/// Note that as opposed to the technet guide above we use bcdedit rather than 'unattend.xml'
/// to disable the recovery environment.
/// </summary>
/// <param name="DriveIndex">The Rufus drive index for the target media.</param>
/// <param name="drive_name">The path of the target media.</param>
/// <param name="use_esp">Whether to create an ESP on the target media.</param>
/// <returns>TRUE on success, FALSE on error.</returns>
BOOL SetupWinToGo(DWORD DriveIndex, const char* drive_name, BOOL use_esp)
{
	char *mounted_iso, *ms_efi = NULL, mounted_image_path[128], cmd[MAX_PATH];
	ULONG cluster_size;

	uprintf("Windows To Go mode selected");
	// Additional sanity checks
	if ((use_esp) && (SelectedDrive.MediaType != FixedMedia) && (WindowsVersion.BuildNumber < 15000)) {
		ErrorStatus = RUFUS_ERROR(ERROR_NOT_SUPPORTED);
		return FALSE;
	}

	if (!img_report.is_windows_img) {
		mounted_iso = VhdMountImage(image_path);
		if (mounted_iso == NULL) {
			uprintf("Could not mount ISO for Windows To Go installation");
			ErrorStatus = RUFUS_ERROR(APPERR(ERROR_ISO_EXTRACT));
			return FALSE;
		}
		static_sprintf(mounted_image_path, "%s%s", mounted_iso, &img_report.wininst_path[wininst_index][2]);
		uprintf("Mounted ISO as '%s'", mounted_iso);
	}

	// Now we use the WIM API to apply that image
	if (!WimApplyImage(img_report.is_windows_img ? image_path : mounted_image_path, wintogo_index, drive_name)) {
		uprintf("Failed to apply Windows To Go image");
		if (!IS_ERROR(ErrorStatus))
			ErrorStatus = RUFUS_ERROR(APPERR(ERROR_ISO_EXTRACT));
		if (!img_report.is_windows_img)
			VhdUnmountImage();
		return FALSE;
	}
	if (!img_report.is_windows_img)
		VhdUnmountImage();

	if (use_esp) {
		uprintf("Setting up EFI System Partition");
		// According to Ubuntu (https://bugs.launchpad.net/ubuntu/+source/partman-efi/+bug/811485) you want to use FAT32.
		// However, you have to be careful that the cluster size needs to be greater or equal to the sector size, which
		// in turn has an impact on the minimum EFI partition size we can create (see ms_efi_size_MB in drive.c)
		if (SelectedDrive.SectorSize <= 1024)
			cluster_size = 1024;
		else if (SelectedDrive.SectorSize <= 4096)
			cluster_size = 4096;
		else	// Go for broke
			cluster_size = (ULONG)SelectedDrive.SectorSize;
		// Boy do you *NOT* want to specify a label here, and spend HOURS figuring out why your EFI partition cannot boot...
		// Also, we use the Large FAT32 facility Microsoft APIs are *UTTERLY USELESS* for achieving what we want:
		// VDS cannot list ESP volumes (talk about allegedly improving on the old disk and volume APIs, only to
		// completely neuter it) and IVdsDiskPartitionMF::FormatPartitionEx(), which is what you are supposed to
		// use for ESPs, explicitly states: "This method cannot be used to format removable media."
		if (!FormatPartition(DriveIndex, SelectedDrive.Partition[partition_index[PI_ESP]].Offset, cluster_size, FS_FAT32, "",
			FP_QUICK | FP_FORCE | FP_LARGE_FAT32 | FP_NO_BOOT)) {
			uprintf("Could not format EFI System Partition");
			return FALSE;
		}
		Sleep(200);
		// Need to have the ESP mounted to invoke bcdboot
		ms_efi = AltMountVolume(DriveIndex, SelectedDrive.Partition[partition_index[PI_ESP]].Offset, FALSE);
		if (ms_efi == NULL) {
			ErrorStatus = RUFUS_ERROR(APPERR(ERROR_CANT_ASSIGN_LETTER));
			return FALSE;
		}
	}

	// We invoke the 'bcdboot' command from the host, as the one from the drive produces problems (#558)
	// and of course, we couldn't invoke an ARM64 'bcdboot' binary on an x86 host anyway...
	// Also, since Rufus should (usually) be running as a 32 bit app, on 64 bit systems, we need to use
	// 'C:\Windows\Sysnative' and not 'C:\Windows\System32' to invoke bcdboot, as 'C:\Windows\System32'
	// will get converted to 'C:\Windows\SysWOW64' behind the scenes, and there is no bcdboot.exe there.
	uprintf("Enabling boot using command:");
	static_sprintf(cmd, "%s\\bcdboot.exe %s\\Windows /v /f %s /s %s", sysnative_dir, drive_name,
		HAS_BOOTMGR_BIOS(img_report) ? (HAS_BOOTMGR_EFI(img_report) ? "ALL" : "BIOS") : "UEFI",
		(use_esp) ? ms_efi : drive_name);
	// I don't believe we can ever have a stray '%' in cmd, but just in case...
	assert(strchr(cmd, '%') == NULL);
	uprintf(cmd);
	if (RunCommand(cmd, sysnative_dir, usb_debug) != 0) {
		// Try to continue... but report a failure
		uprintf("Failed to enable boot");
		ErrorStatus = RUFUS_ERROR(APPERR(ERROR_ISO_EXTRACT));
	}

	CopySKUSiPolicy((use_esp) ? ms_efi : drive_name);

	UpdateProgressWithInfo(OP_FILE_COPY, MSG_267, wim_proc_files + 2 * wim_extra_files, wim_nb_files);

	// Setting internal drives offline for Windows To Go is crucial if, for instance, you are using ReFS
	// on Windows 10 (therefore ReFS v3.4) and don't want a Windows 11 To Go boot to automatically
	// "upgrade" the ReFS version on all drives to v3.7, thereby preventing you from being able to mount
	// those volumes back on Windows 10 ever again. Yes, I have been stung by this Microsoft bullshit!
	// See: https://gist.github.com/0xbadfca11/da0598e47dd643d933dc#Mountability
	if (unattend_xml_flags & UNATTEND_OFFLINE_INTERNAL_DRIVES) {
		uprintf("Setting the target's internal drives offline using command:");
		// This applies the "offlineServicing" section of the unattend.xml (while ignoring the other sections)
		static_sprintf(cmd, "dism /Image:%s\\ /Apply-Unattend:%s", drive_name, unattend_xml_path);
		uprintf(cmd);
		RunCommand(cmd, NULL, usb_debug);
	}

	uprintf("Disabling use of the Windows Recovery Environment using command:");
	static_sprintf(cmd, "%s\\bcdedit.exe /store %s\\EFI\\Microsoft\\Boot\\BCD /set {default} recoveryenabled no",
		sysnative_dir, (use_esp) ? ms_efi : drive_name);
	assert(strchr(cmd, '%') == NULL);
	uprintf(cmd);
	RunCommand(cmd, sysnative_dir, usb_debug);

	UpdateProgressWithInfo(OP_FILE_COPY, MSG_267, wim_nb_files, wim_nb_files);

	if (use_esp) {
		Sleep(200);
		AltUnmountVolume(ms_efi, FALSE);
	}

	return TRUE;
}

/// <summary>
/// Add unattend.xml to 'sources\boot.wim' (install) or 'Windows\Panther\' (Windows To Go).
/// </summary>
/// <param name="drive_letter">The letter of the drive where the \sources\boot.wim image resides.</param>
/// <param name="flags">A bitmap of unattend flags to apply.</param>
/// <returns>TRUE on success, FALSE on error.</returns>
BOOL ApplyWindowsCustomization(char drive_letter, int flags)
// NB: Work with a copy of unattend_xml_flags as a paremeter since we will modify it.
{
	BOOL r = FALSE, is_hive_mounted = FALSE;
	int i, wim_index = 2;
	const char* offline_hive_name = "RUFUS_OFFLINE_HIVE";
	char boot_wim_path[] = "?:\\sources\\boot.wim", key_path[64];
	char appraiserres_dll_src[] = "?:\\sources\\appraiserres.dll";
	char appraiserres_dll_dst[] = "?:\\sources\\appraiserres.bak";
	char *mount_path = NULL, path[MAX_PATH];
	HKEY hKey = NULL, hSubKey = NULL;
	LSTATUS status;
	DWORD dwDisp, dwVal = 1;

	assert(unattend_xml_path != NULL);
	uprintf("Applying Windows customization:");
	PrintStatus(0, MSG_326);
	if (flags & UNATTEND_WINDOWS_TO_GO) {
		static_sprintf(path, "%c:\\Windows\\Panther", drive_letter);
		if (!CreateDirectoryA(path, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
			uprintf("Could not create '%s' : %s", path, WindowsErrorString());
			goto out;
		}
		static_sprintf(path, "%c:\\Windows\\Panther\\unattend.xml", drive_letter);
		if (!CopyFileA(unattend_xml_path, path, TRUE)) {
			uprintf("Could not create '%s' : %s", path, WindowsErrorString());
			goto out;
		}
		uprintf("Added '%s'", path);
	} else {
		boot_wim_path[0] = drive_letter;
		if (flags & UNATTEND_WINPE_SETUP_MASK) {
			// Create a backup of sources\appraiserres.dll and then create an empty file to
			// allow in-place upgrades without TPM/SB. Note that we need to create an empty,
			// appraiserres.dll otherwise setup.exe extracts its own.
			appraiserres_dll_src[0] = drive_letter;
			appraiserres_dll_dst[0] = drive_letter;
			if (!MoveFileExU(appraiserres_dll_src, appraiserres_dll_dst, MOVEFILE_REPLACE_EXISTING)
				&& GetLastError() != ERROR_FILE_NOT_FOUND) {
				uprintf("Could not rename '%s': %s", appraiserres_dll_src, WindowsErrorString());
			} else {
				if (GetLastError() == ERROR_SUCCESS)
					uprintf("Renamed '%s' → '%s'", appraiserres_dll_src, appraiserres_dll_dst);
				CloseHandle(CreateFileU(appraiserres_dll_src, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
					NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL));
				uprintf("Created '%s' placeholder", appraiserres_dll_src);
				if (validate_md5sum) {
					md5sum_totalbytes -= _filesizeU(appraiserres_dll_dst);
					StrArrayAdd(&modified_files, appraiserres_dll_src, TRUE);
				}
			}
		}

		UpdateProgressWithInfoForce(OP_PATCH, MSG_325, 0, PATCH_PROGRESS_TOTAL);
		// We only need to mount boot.wim if we have windowsPE data to deal with. If
		// not, we can just copy our unattend.xml in \sources\$OEM$\$$\Panther\.
		if (flags & UNATTEND_WINPE_SETUP_MASK) {
			if (validate_md5sum)
				md5sum_totalbytes -= _filesizeU(boot_wim_path);
			uprintf("Mounting '%s[%d]'...", boot_wim_path, wim_index);
			// Some "unofficial" ISOs have a modified boot.wim that doesn't have Windows Setup at index 2...
			if (!WimIsValidIndex(boot_wim_path, wim_index)) {
				uprintf("WARNING: This image appears to be an UNOFFICIAL Windows ISO!");
				uprintf("Rufus recommends that you only use OFFICIAL retail Microsoft Windows images, such as");
				uprintf("the ones that can be downloaded through the download facility of this application.");
				wim_index = 1;
			}
			mount_path = WimMountImage(boot_wim_path, wim_index);
			if (mount_path == NULL)
				goto out;
		}

		if (flags & UNATTEND_SECUREBOOT_TPM_MINRAM) {
			// Try to create the registry keys directly, and fallback to using unattend
			// if that fails (which the Windows Store version is expected to do).
			static_sprintf(path, "%s\\Windows\\System32\\config\\SYSTEM", mount_path);
			if (!MountRegistryHive(HKEY_LOCAL_MACHINE, offline_hive_name, path)) {
				uprintf("Falling back to creating the registry keys through unattend.xml");
				goto copy_unattend;
			}
			UpdateProgressWithInfoForce(OP_PATCH, MSG_325, 101, PATCH_PROGRESS_TOTAL);
			is_hive_mounted = TRUE;

			static_sprintf(key_path, "%s\\Setup", offline_hive_name);
			status = RegOpenKeyExA(HKEY_LOCAL_MACHINE, key_path, 0, KEY_READ | KEY_CREATE_SUB_KEY, &hKey);
			if (status != ERROR_SUCCESS) {
				SetLastError(status);
				uprintf("Could not open 'HKLM\\SYSTEM\\Setup' registry key: %s", WindowsErrorString());
				goto copy_unattend;
			}

			status = RegCreateKeyExA(hKey, "LabConfig", 0, NULL, 0,
				KEY_SET_VALUE | KEY_QUERY_VALUE | KEY_CREATE_SUB_KEY, NULL, &hSubKey, &dwDisp);
			if (status != ERROR_SUCCESS) {
				SetLastError(status);
				uprintf("Could not create 'HKLM\\SYSTEM\\Setup\\LabConfig' registry key: %s", WindowsErrorString());
				goto copy_unattend;
			}

			for (i = 0; i < ARRAYSIZE(bypass_name); i++) {
				status = RegSetValueExA(hSubKey, bypass_name[i], 0, REG_DWORD, (LPBYTE)&dwVal, sizeof(DWORD));
				if (status != ERROR_SUCCESS) {
					SetLastError(status);
					uprintf("Could not set 'HKLM\\SYSTEM\\Setup\\LabConfig\\%s' registry key: %s",
						bypass_name[i], WindowsErrorString());
					goto copy_unattend;
				}
				uprintf("Created 'HKLM\\SYSTEM\\Setup\\LabConfig\\%s' registry key", bypass_name[i]);
			}
			// We were successfull in creating the keys so disable the windowsPE section from unattend.xml
			// We do this by replacing '<settings pass="windowsPE">' with '<settings pass="disabled">'
			// (provided that the registry key creation was the only item for this pass)
			if ((flags & UNATTEND_WINPE_SETUP_MASK) == UNATTEND_SECUREBOOT_TPM_MINRAM) {
				if (replace_in_token_data(unattend_xml_path, "<settings", "windowsPE", "disabled", FALSE) == NULL)
					uprintf("Warning: Could not disable 'windowsPE' pass from unattend.xml");
				// Remove the flags, since we accomplished the registry creation outside of unattend.
				flags &= ~UNATTEND_SECUREBOOT_TPM_MINRAM;
			} else {
				// TODO: If we add other tasks besides LabConfig reg keys, we'll need to figure out how
				// to comment out the <RunSynchronous> entries from windowsPE (and only windowsPE).
				assert(FALSE);
			}
			UpdateProgressWithInfoForce(OP_PATCH, MSG_325, 102, PATCH_PROGRESS_TOTAL);
		}

	copy_unattend:
		if (flags & UNATTEND_WINPE_SETUP_MASK) {
			// If we have a windowsPE section, copy the answer files to the root of boot.wim as
			// Autounattend.xml. This also results in that file being automatically copied over
			// to %WINDIR%\Panther\unattend.xml for later passes processing.
			assert(mount_path != NULL);
			static_sprintf(path, "%s\\Autounattend.xml", mount_path);
			if (!CopyFileU(unattend_xml_path, path, TRUE)) {
				uprintf("Could not create boot.wim 'Autounattend.xml': %s", WindowsErrorString());
				goto out;
			}
			uprintf("Added 'Autounattend.xml' to '%s'", boot_wim_path);
		} else {
			// If there is no windowsPE section in our unattend, then copying it as Autounattend.xml on
			// the root of boot.wim will not work as Windows Setup does *NOT* carry Autounattend.xml into
			// %WINDIR%\Panther\unattend.xml then (See: https://github.com/pbatard/rufus/issues/1981).
			// So instead, copy it to \sources\$OEM$\$$\Panther\unattend.xml on the media, as the content
			// of \sources\$OEM$\$$\* will get copied into %WINDIR%\ during the file copy phase.
			static_sprintf(path, "%c:\\sources\\$OEM$\\$$\\Panther", drive_letter);
			i = SHCreateDirectoryExA(NULL, path, NULL);
			if (i != ERROR_SUCCESS) {
				SetLastError(i);
				uprintf("Error: Could not create directory '%s': %s", path, WindowsErrorString());
				goto out;
			}
			static_sprintf(path, "%c:\\sources\\$OEM$\\$$\\Panther\\unattend.xml", drive_letter);
			if (!CopyFileU(unattend_xml_path, path, TRUE)) {
				uprintf("Could not create '%s': %s", path, WindowsErrorString());
				goto out;
			}
			uprintf("Created '%s'", path);
		}
		UpdateProgressWithInfoForce(OP_PATCH, MSG_325, 103, PATCH_PROGRESS_TOTAL);
	}
	r = TRUE;

out:
	if (hSubKey != NULL)
		RegCloseKey(hSubKey);
	if (hKey != NULL)
		RegCloseKey(hKey);
	if (is_hive_mounted) {
		UnmountRegistryHive(HKEY_LOCAL_MACHINE, offline_hive_name);
		UpdateProgressWithInfoForce(OP_PATCH, MSG_325, 104, PATCH_PROGRESS_TOTAL);
	}
	if (mount_path) {
		uprintf("Unmounting '%s[%d]'...", boot_wim_path, wim_index);
		WimUnmountImage(boot_wim_path, wim_index, TRUE);
		if (validate_md5sum) {
			md5sum_totalbytes += _filesizeU(boot_wim_path);
			StrArrayAdd(&modified_files, boot_wim_path, TRUE);
		}
		UpdateProgressWithInfo(OP_PATCH, MSG_325, PATCH_PROGRESS_TOTAL, PATCH_PROGRESS_TOTAL);
	}
	free(mount_path);
	return r;
}
