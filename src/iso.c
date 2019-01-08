/*
 * Rufus: The Reliable USB Formatting Utility
 * ISO file extraction
 * Copyright © 2011-2018 Pete Batard <pete@akeo.ie>
 * Based on libcdio's iso & udf samples:
 * Copyright © 2003-2014 Rocky Bernstein <rocky@gnu.org>
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
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <errno.h>
#include <direct.h>
#include <ctype.h>
#include <virtdisk.h>

#include <cdio/cdio.h>
#include <cdio/logging.h>
#include <cdio/iso9660.h>
#include <cdio/udf.h>

#include "rufus.h"
#include "libfat.h"
#include "missing.h"
#include "resource.h"
#include "msapi_utf8.h"
#include "localization.h"

// How often should we update the progress bar (in 2K blocks) as updating
// the progress bar for every block will bring extraction to a crawl
#define PROGRESS_THRESHOLD        128
#define FOUR_GIGABYTES            4294967296LL

// Needed for UDF ISO access
CdIo_t* cdio_open (const char* psz_source, driver_id_t driver_id) {return NULL;}
void cdio_destroy (CdIo_t* p_cdio) {}

uint32_t GetInstallWimVersion(const char* iso);

typedef struct {
	BOOLEAN is_cfg;
	BOOLEAN is_syslinux_cfg;
	BOOLEAN is_grub_cfg;
	BOOLEAN is_old_c32[NB_OLD_C32];
} EXTRACT_PROPS;

RUFUS_IMG_REPORT img_report;
int64_t iso_blocking_status = -1;
extern BOOL preserve_timestamps;
BOOL enable_iso = TRUE, enable_joliet = TRUE, enable_rockridge = TRUE, has_ldlinux_c32;
#define ISO_BLOCKING(x) do {x; iso_blocking_status++; } while(0)
static const char* psz_extract_dir;
static const char* bootmgr_name = "bootmgr";
static const char* bootmgr_efi_name = "bootmgr.efi";
static const char* grldr_name = "grldr";
static const char* ldlinux_name = "ldlinux.sys";
static const char* ldlinux_c32 = "ldlinux.c32";
static const char* efi_dirname = "/efi/boot";
static const char* efi_bootname[] = { "bootia32.efi", "bootia64.efi", "bootx64.efi", "bootarm.efi", "bootaa64.efi", "bootebc.efi" };
static const char* sources_str = "/sources";
static const char* wininst_name[] = { "install.wim", "install.esd", "install.swm" };
// We only support GRUB/BIOS (x86) that uses a standard config dir (/boot/grub/i386-pc/)
// If the disc was mastered properly, GRUB/EFI will take care of itself
static const char* grub_dirname = "/boot/grub/i386-pc";
static const char* grub_cfg = "grub.cfg";
static const char* syslinux_cfg[] = { "isolinux.cfg", "syslinux.cfg", "extlinux.conf" };
static const char* isolinux_bin[] = { "isolinux.bin", "boot.bin" };
static const char* pe_dirname[] = { "/i386", "/amd64", "/minint" };
static const char* pe_file[] = { "ntdetect.com", "setupldr.bin", "txtsetup.sif" };
static const char* reactos_name = "setupldr.sys"; // TODO: freeldr.sys doesn't seem to work
static const char* kolibri_name = "kolibri.img";
static const char* autorun_name = "autorun.inf";
static const char* casper_name = "CASPER";
static const char* stupid_antivirus = "  NOTE: This is usually caused by a poorly designed security solution. "
	"See https://goo.gl/QTobxX.\r\n  This file will be skipped for now, but you should really "
	"look into using a *SMARTER* antivirus solution.";
const char* old_c32_name[NB_OLD_C32] = OLD_C32_NAMES;
static const int64_t old_c32_threshold[NB_OLD_C32] = OLD_C32_THRESHOLD;
static uint8_t joliet_level = 0;
static uint64_t total_blocks, nb_blocks;
static BOOL scan_only = FALSE;
static StrArray config_path, isolinux_path;

// Ensure filenames do not contain invalid FAT32 or NTFS characters
static __inline char* sanitize_filename(char* filename, BOOL* is_identical)
{
	size_t i, j;
	char* ret = NULL;
	char unauthorized[] = { '*', '?', '<', '>', ':', '|' };

	*is_identical = TRUE;
	ret = safe_strdup(filename);
	if (ret == NULL) {
		uprintf("Could not allocate string for sanitized path");
		return NULL;
	}

	// Must start after the drive part (D:\...) so that we don't eliminate the first column
	for (i=2; i<safe_strlen(ret); i++) {
		for (j=0; j<sizeof(unauthorized); j++) {
			if (ret[i] == unauthorized[j]) {
				ret[i] = '_';
				*is_identical = FALSE;
			}
		}
	}
	return ret;
}

static void log_handler (cdio_log_level_t level, const char *message)
{
	switch(level) {
	case CDIO_LOG_DEBUG:
		// TODO: use a setting to enable libcdio debug?
		return;
	default:
		uprintf("libcdio: %s\n", message);
	}
}

/*
 * Scan and set ISO properties
 * Returns true if the the current file does not need to be processed further
 */
static BOOL check_iso_props(const char* psz_dirname, int64_t file_length, const char* psz_basename,
	const char* psz_fullpath, EXTRACT_PROPS *props)
{
	size_t i, j, len;
	// Check for an isolinux/syslinux config file anywhere
	memset(props, 0, sizeof(EXTRACT_PROPS));
	for (i=0; i<ARRAYSIZE(syslinux_cfg); i++) {
		if (safe_stricmp(psz_basename, syslinux_cfg[i]) == 0) {
			props->is_cfg = TRUE;	// Required for "extlinux.conf"
			props->is_syslinux_cfg = TRUE;
			if ((scan_only) && (i == 1) && (safe_stricmp(psz_dirname, efi_dirname) == 0))
				img_report.has_efi_syslinux = TRUE;
		}
	}

	// Check for an old incompatible c32 file anywhere
	for (i=0; i<NB_OLD_C32; i++) {
		if ((safe_stricmp(psz_basename, old_c32_name[i]) == 0) && (file_length <= old_c32_threshold[i]))
			props->is_old_c32[i] = TRUE;
	}

	// Check for config files that may need patching
	if (!scan_only) {
		len = safe_strlen(psz_basename);
		if ((len >= 4) && safe_stricmp(&psz_basename[len-4], ".cfg") == 0)
			props->is_cfg = TRUE;
	}

	// Check for GRUB artifacts
	if (scan_only) {
		if (safe_stricmp(psz_dirname, grub_dirname) == 0)
			img_report.has_grub2 = TRUE;
	} else if (safe_stricmp(psz_basename, grub_cfg) == 0) {
		props->is_grub_cfg = TRUE;
	}

	if (scan_only) {
		// Check for a syslinux v5.0+ file anywhere
		if (safe_stricmp(psz_basename, ldlinux_c32) == 0) {
			has_ldlinux_c32 = TRUE;
		}

		// Check for various files in root (psz_dirname = "")
		if ((psz_dirname != NULL) && (psz_dirname[0] == 0)) {
			if (safe_stricmp(psz_basename, bootmgr_name) == 0) {
				img_report.has_bootmgr = TRUE;
			}
			if (safe_stricmp(psz_basename, bootmgr_efi_name) == 0) {
				img_report.has_bootmgr_efi = TRUE;
			}
			if (safe_stricmp(psz_basename, grldr_name) == 0) {
				img_report.has_grub4dos = TRUE;
			}
			if (safe_stricmp(psz_basename, kolibri_name) == 0) {
				img_report.has_kolibrios = TRUE;
			}
			if (safe_stricmp(psz_basename, casper_name) == 0) {
				img_report.has_casper = TRUE;
			}
			if (safe_stricmp(psz_basename, bootmgr_efi_name) == 0) {
				img_report.has_efi |= 1;
			}
		}

		// Check for ReactOS' setupldr.sys anywhere
		if ((img_report.reactos_path[0] == 0) && (safe_stricmp(psz_basename, reactos_name) == 0))
			static_strcpy(img_report.reactos_path, psz_fullpath);

		// Check for the first 'efi*.img' we can find (that hopefully contains EFI boot files)
		if (!HAS_EFI_IMG(img_report) && (safe_strlen(psz_basename) >= 7) &&
			(safe_strnicmp(psz_basename, "efi", 3) == 0) &&
			(safe_stricmp(&psz_basename[strlen(psz_basename) - 4], ".img") == 0))
			static_strcpy(img_report.efi_img_path, psz_fullpath);

		// Check for the EFI boot entries
		if (safe_stricmp(psz_dirname, efi_dirname) == 0) {
			for (i=0; i<ARRAYSIZE(efi_bootname); i++)
				if (safe_stricmp(psz_basename, efi_bootname[i]) == 0)
					img_report.has_efi |= (2<<i);	// start at 2 since "bootmgr.efi" is bit 0
		}

		// Check for "install.###" in "###/sources/"
		if (safe_stricmp(&psz_dirname[max(0, safe_strlen(psz_dirname) - strlen(sources_str))], sources_str) == 0) {
			for (i = 0; i < ARRAYSIZE(wininst_name); i++) {
				if (safe_stricmp(psz_basename, wininst_name[i]) == 0) {
					if (img_report.wininst_index < MAX_WININST) {
						static_sprintf(img_report.wininst_path[img_report.wininst_index], "?:%s", psz_fullpath);
						img_report.wininst_index++;
					}
				}
			}
		}

		// Check for PE (XP) specific files in "/i386", "/amd64" or "/minint"
		for (i=0; i<ARRAYSIZE(pe_dirname); i++)
			if (safe_stricmp(psz_dirname, pe_dirname[i]) == 0)
				for (j=0; j<ARRAYSIZE(pe_file); j++)
					if (safe_stricmp(psz_basename, pe_file[j]) == 0)
						img_report.winpe |= (1<<j)<<(ARRAYSIZE(pe_dirname)*i);

		if (props->is_syslinux_cfg) {
			// Maintain a list of all the isolinux/syslinux configs identified so far
			StrArrayAdd(&config_path, psz_fullpath, TRUE);
		}
		for (i=0; i<ARRAYSIZE(isolinux_bin); i++) {
			if (safe_stricmp(psz_basename, isolinux_bin[i]) == 0) {
				// Maintain a list of all the isolinux.bin files found
				StrArrayAdd(&isolinux_path, psz_fullpath, TRUE);
			}
		}

		for (i=0; i<NB_OLD_C32; i++) {
			if (props->is_old_c32[i])
				img_report.has_old_c32[i] = TRUE;
		}
		if (file_length >= FOUR_GIGABYTES)
			img_report.has_4GB_file = TRUE;
		// Compute projected size needed
		total_blocks += file_length / ISO_BLOCKSIZE;
		// NB: ISO_BLOCKSIZE = UDF_BLOCKSIZE
		if ((file_length != 0) && (file_length % ISO_BLOCKSIZE != 0))
			total_blocks++;
		return TRUE;
	}
	// In case there's an ldlinux.sys on the ISO, prevent it from overwriting ours
	if ((psz_dirname != NULL) && (psz_dirname[0] == 0) && (safe_strcmp(psz_basename, ldlinux_name) == 0)) {
		uprintf("skipping % file from ISO image\n", ldlinux_name);
		return TRUE;
	}
	return FALSE;
}

// Apply various workarounds to Linux config files
static void fix_config(const char* psz_fullpath, const char* psz_path, const char* psz_basename, EXTRACT_PROPS* props)
{
	size_t i, nul_pos;
	char *iso_label = NULL, *usb_label = NULL, *src, *dst;

	nul_pos = safe_strlen(psz_fullpath);
	src = safe_strdup(psz_fullpath);
	if (src == NULL)
		return;
	for (i=0; i<nul_pos; i++)
		if (src[i] == '/') src[i] = '\\';

	// Workaround for config files requiring an ISO label for kernel append that may be
	// different from our USB label. Oh, and these labels must have spaces converted to \x20.
	if (props->is_cfg) {
		iso_label = replace_char(img_report.label, ' ', "\\x20");
		usb_label = replace_char(img_report.usb_label, ' ', "\\x20");
		if ((iso_label != NULL) && (usb_label != NULL)) {
			if (replace_in_token_data(src, (props->is_grub_cfg) ? "linuxefi" : "append",
				iso_label, usb_label, TRUE) != NULL)
				uprintf("  Patched %s: '%s' ➔ '%s'\n", src, iso_label, usb_label);
		}
		safe_free(iso_label);
		safe_free(usb_label);
	}

	// Fix dual BIOS + EFI support for tails and other ISOs
	if ( (props->is_syslinux_cfg) && (safe_stricmp(psz_path, efi_dirname) == 0) &&
		 (safe_stricmp(psz_basename, syslinux_cfg[0]) == 0) &&
		 (!img_report.has_efi_syslinux) && (dst = safe_strdup(src)) ) {
		dst[nul_pos-12] = 's'; dst[nul_pos-11] = 'y'; dst[nul_pos-10] = 's';
		CopyFileA(src, dst, TRUE);
		uprintf("Duplicated %s to %s\n", src, dst);
		free(dst);
	}

	// Workaround for FreeNAS
	if (props->is_grub_cfg) {
		iso_label = malloc(MAX_PATH);
		usb_label = malloc(MAX_PATH);
		if ((iso_label != NULL) && (usb_label != NULL)) {
			safe_sprintf(iso_label, MAX_PATH, "cd9660:/dev/iso9660/%s", img_report.label);
			safe_sprintf(usb_label, MAX_PATH, "msdosfs:/dev/msdosfs/%s", img_report.usb_label);
			if (replace_in_token_data(src, "set", iso_label, usb_label, TRUE) != NULL)
				uprintf("  Patched %s: '%s' ➔ '%s'\n", src, iso_label, usb_label);
		}
		safe_free(iso_label);
		safe_free(usb_label);
	}

	free(src);
}

static void print_extracted_file(char* psz_fullpath, int64_t file_length)
{
	size_t i, nul_pos;

	if (psz_fullpath == NULL)
		return;
	// Replace slashes with backslashes and append the size to the path for UI display
	nul_pos = strlen(psz_fullpath);
	for (i=0; i<nul_pos; i++)
		if (psz_fullpath[i] == '/') psz_fullpath[i] = '\\';
	safe_sprintf(&psz_fullpath[nul_pos], 24, " (%s)", SizeToHumanReadable(file_length, TRUE, FALSE));
	uprintf("Extracting: %s\n", psz_fullpath);
	safe_sprintf(&psz_fullpath[nul_pos], 24, " (%s)", SizeToHumanReadable(file_length, FALSE, FALSE));
	PrintStatus(0, MSG_000, psz_fullpath);	// MSG_000 is "%s"
	// ISO9660 cannot handle backslashes
	for (i=0; i<nul_pos; i++)
		if (psz_fullpath[i] == '\\') psz_fullpath[i] = '/';
	// Remove the appended size for extraction
	psz_fullpath[nul_pos] = 0;
}

// Convert from time_t to FILETIME
// Uses 3 static entries so that we can convert 3 concurrent values at the same time
static LPFILETIME __inline to_filetime(time_t t)
{
	static int i = 0;
	static FILETIME ft[3], *r;
	LONGLONG ll = Int32x32To64(t, 10000000) + 116444736000000000;

	r = &ft[i];
	r->dwLowDateTime = (DWORD)ll;
	r->dwHighDateTime = (DWORD)(ll >> 32);
	i = (i + 1) % ARRAYSIZE(ft);
	return r;
}

// Helper function to restore the timestamp on a directory
static void __inline set_directory_timestamp(char* path, LPFILETIME creation, LPFILETIME last_access, LPFILETIME modify)
{
	HANDLE dir_handle = CreateFileU(path, GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if ((dir_handle == INVALID_HANDLE_VALUE) || (!SetFileTime(dir_handle, creation, last_access, modify)))
		uprintf("  Could not set timestamp for directory '%s': %s", path, WindowsErrorString());
	safe_closehandle(dir_handle);
}

// Preallocates the target size of a newly created file in order to prevent fragmentation from repeated writes
static void __inline preallocate_filesize(HANDLE hFile, int64_t file_length)
{
	SetFileInformationByHandle(hFile, FileEndOfFileInfo, &file_length, sizeof(file_length));

	// FileAllocationInfo does not require the size to be a multiple of the cluster size; the FS driver takes care of this.
	SetFileInformationByHandle(hFile, FileAllocationInfo, &file_length, sizeof(file_length));
}

// Returns 0 on success, nonzero on error
static int udf_extract_files(udf_t *p_udf, udf_dirent_t *p_udf_dirent, const char *psz_path)
{
	HANDLE file_handle = NULL;
	DWORD buf_size, wr_size, err;
	EXTRACT_PROPS props;
	BOOL r, is_identical;
	int length;
	size_t i;
	char tmp[128], *psz_fullpath = NULL, *psz_sanpath = NULL;
	const char* psz_basename;
	udf_dirent_t *p_udf_dirent2;
	uint8_t buf[UDF_BLOCKSIZE];
	int64_t read, file_length;

	if ((p_udf_dirent == NULL) || (psz_path == NULL))
		return 1;

	while ((p_udf_dirent = udf_readdir(p_udf_dirent)) != NULL) {
		if (FormatStatus) goto out;
		psz_basename = udf_get_filename(p_udf_dirent);
		if (strlen(psz_basename) == 0)
			continue;
		length = (int)(3 + strlen(psz_path) + strlen(psz_basename) + strlen(psz_extract_dir) + 24);
		psz_fullpath = (char*)calloc(sizeof(char), length);
		if (psz_fullpath == NULL) {
			uprintf("Error allocating file name");
			goto out;
		}
		length = _snprintf(psz_fullpath, length, "%s%s/%s", psz_extract_dir, psz_path, psz_basename);
		if (length < 0) {
			goto out;
		}
		if (udf_is_dir(p_udf_dirent)) {
			if (!scan_only) {
				psz_sanpath = sanitize_filename(psz_fullpath, &is_identical);
				IGNORE_RETVAL(_mkdirU(psz_sanpath));
				if (preserve_timestamps) {
					set_directory_timestamp(psz_sanpath, to_filetime(udf_get_attribute_time(p_udf_dirent)),
						to_filetime(udf_get_access_time(p_udf_dirent)), to_filetime(udf_get_modification_time(p_udf_dirent)));
				}
				safe_free(psz_sanpath);
			}
			p_udf_dirent2 = udf_opendir(p_udf_dirent);
			if (p_udf_dirent2 != NULL) {
				if (udf_extract_files(p_udf, p_udf_dirent2, &psz_fullpath[strlen(psz_extract_dir)]))
					goto out;
			}
		} else {
			file_length = udf_get_file_length(p_udf_dirent);
			if (check_iso_props(psz_path, file_length, psz_basename, psz_fullpath, &props)) {
				safe_free(psz_fullpath);
				continue;
			}
			print_extracted_file(psz_fullpath, file_length);
			for (i=0; i<NB_OLD_C32; i++) {
				if (props.is_old_c32[i] && use_own_c32[i]) {
					static_sprintf(tmp, "%s/syslinux-%s/%s", FILES_DIR, embedded_sl_version_str[0], old_c32_name[i]);
					if (CopyFileU(tmp, psz_fullpath, FALSE)) {
						uprintf("  Replaced with local version %s", IsFileInDB(tmp)?"✓":"✗");
						break;
					}
					uprintf("  Could not replace file: %s", WindowsErrorString());
				}
			}
			if (i < NB_OLD_C32)
				continue;
			psz_sanpath = sanitize_filename(psz_fullpath, &is_identical);
			if (!is_identical)
				uprintf("  File name sanitized to '%s'", psz_sanpath);
			file_handle = CreateFileU(psz_sanpath, GENERIC_READ | GENERIC_WRITE,
				FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			if (file_handle == INVALID_HANDLE_VALUE) {
				err = GetLastError();
				uprintf("  Unable to create file: %s", WindowsErrorString());
				if ((err == ERROR_ACCESS_DENIED) && (safe_strcmp(&psz_sanpath[3], autorun_name) == 0))
					uprintf(stupid_antivirus);
				else
					goto out;
			} else {
				preallocate_filesize(file_handle, file_length);
				while (file_length > 0) {
					if (FormatStatus) goto out;
					memset(buf, 0, UDF_BLOCKSIZE);
					read = udf_read_block(p_udf_dirent, buf, 1);
					if (read < 0) {
						uprintf("  Error reading UDF file %s", &psz_fullpath[strlen(psz_extract_dir)]);
						goto out;
					}
					buf_size = (DWORD)MIN(file_length, read);
					ISO_BLOCKING(r = WriteFileWithRetry(file_handle, buf, buf_size, &wr_size, WRITE_RETRIES));
					if (!r) {
						uprintf("  Error writing file: %s", WindowsErrorString());
						goto out;
					}
					file_length -= read;
					if (nb_blocks++ % PROGRESS_THRESHOLD == 0)
						UpdateProgress(OP_DOS, 100.0f*nb_blocks/total_blocks);
				}
			}
			if ((preserve_timestamps) && (!SetFileTime(file_handle, to_filetime(udf_get_attribute_time(p_udf_dirent)),
				to_filetime(udf_get_access_time(p_udf_dirent)), to_filetime(udf_get_modification_time(p_udf_dirent)))))
				uprintf("  Could not set timestamp: %s", WindowsErrorString());

			// If you have a fast USB 3.0 device, the default Windows buffering does an
			// excellent job at compensating for our small blocks read/writes to max out the
			// device's bandwidth.
			// The drawback however is with cancellation. With a large file, CloseHandle()
			// may take forever to complete and is not interruptible. We try to detect this.
			ISO_BLOCKING(safe_closehandle(file_handle));
			if (props.is_cfg)
				fix_config(psz_sanpath, psz_path, psz_basename, &props);
			safe_free(psz_sanpath);
		}
		safe_free(psz_fullpath);
	}
	return 0;

out:
	if (p_udf_dirent != NULL)
		udf_dirent_free(p_udf_dirent);
	ISO_BLOCKING(safe_closehandle(file_handle));
	safe_free(psz_fullpath);
	return 1;
}

// Returns 0 on success, nonzero on error
static int iso_extract_files(iso9660_t* p_iso, const char *psz_path)
{
	HANDLE file_handle = NULL;
	DWORD buf_size, wr_size, err;
	EXTRACT_PROPS props;
	BOOL is_symlink, is_identical;
	int length, r = 1;
	char tmp[128], psz_fullpath[MAX_PATH], *psz_basename, *psz_sanpath;
	const char *psz_iso_name = &psz_fullpath[strlen(psz_extract_dir)];
	unsigned char buf[ISO_BLOCKSIZE];
	CdioListNode_t* p_entnode;
	iso9660_stat_t *p_statbuf;
	CdioISO9660FileList_t* p_entlist;
	size_t i, j;
	lsn_t lsn;
	int64_t file_length, extent_length;

	if ((p_iso == NULL) || (psz_path == NULL))
		return 1;

	length = _snprintf(psz_fullpath, sizeof(psz_fullpath), "%s%s/", psz_extract_dir, psz_path);
	if (length < 0)
		return 1;
	psz_basename = &psz_fullpath[length];

	p_entlist = iso9660_ifs_readdir(p_iso, psz_path);
	if (!p_entlist) {
		uprintf("Could not access directory %s", psz_path);
		return 1;
	}

	_CDIO_LIST_FOREACH(p_entnode, p_entlist) {
		if (FormatStatus) goto out;
		p_statbuf = (iso9660_stat_t*) _cdio_list_node_data(p_entnode);
		// Eliminate . and .. entries
		if ( (strcmp(p_statbuf->filename, ".") == 0)
			|| (strcmp(p_statbuf->filename, "..") == 0) )
			continue;
		// Rock Ridge requires an exception
		is_symlink = FALSE;
		if ((p_statbuf->rr.b3_rock == yep) && enable_rockridge) {
			safe_strcpy(psz_basename, sizeof(psz_fullpath) - length - 1, p_statbuf->filename);
			if (safe_strlen(p_statbuf->filename) > 64)
				img_report.has_long_filename = TRUE;
			// libcdio has a memleak for Rock Ridge symlinks. It doesn't look like there's an easy fix there as
			// a generic list that's unaware of RR extensions is being used, so we prevent that memleak ourselves
			is_symlink = (p_statbuf->rr.psz_symlink != NULL);
			if (is_symlink)
				img_report.has_symlinks = TRUE;
			if (scan_only)
				safe_free(p_statbuf->rr.psz_symlink);
		} else {
			iso9660_name_translate_ext(p_statbuf->filename, psz_basename, joliet_level);
		}
		if (p_statbuf->type == _STAT_DIR) {
			if (!scan_only) {
				psz_sanpath = sanitize_filename(psz_fullpath, &is_identical);
				IGNORE_RETVAL(_mkdirU(psz_sanpath));
				if (preserve_timestamps) {
					LPFILETIME ft = to_filetime(mktime(&p_statbuf->tm));
					set_directory_timestamp(psz_sanpath, ft, ft, ft);
				}
				safe_free(psz_sanpath);
			}
			if (iso_extract_files(p_iso, psz_iso_name))
				goto out;
		} else {
			file_length = p_statbuf->size;
			if (check_iso_props(psz_path, file_length, psz_basename, psz_fullpath, &props)) {
				continue;
			}
			print_extracted_file(psz_fullpath, file_length);
			for (i=0; i<NB_OLD_C32; i++) {
				if (props.is_old_c32[i] && use_own_c32[i]) {
					static_sprintf(tmp, "%s/syslinux-%s/%s", FILES_DIR, embedded_sl_version_str[0], old_c32_name[i]);
					if (CopyFileU(tmp, psz_fullpath, FALSE)) {
						uprintf("  Replaced with local version %s", IsFileInDB(tmp)?"✓":"✗");
						break;
					}
					uprintf("  Could not replace file: %s", WindowsErrorString());
				}
			}
			if (i < NB_OLD_C32)
				continue;
			psz_sanpath = sanitize_filename(psz_fullpath, &is_identical);
			if (!is_identical)
				uprintf("  File name sanitized to '%s'", psz_sanpath);
			if (is_symlink) {
				if (file_length == 0)
					uprintf("  Ignoring Rock Ridge symbolic link to '%s'", p_statbuf->rr.psz_symlink);
				safe_free(p_statbuf->rr.psz_symlink);
			}
			file_handle = CreateFileU(psz_sanpath, GENERIC_READ | GENERIC_WRITE,
				FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			if (file_handle == INVALID_HANDLE_VALUE) {
				err = GetLastError();
				uprintf("  Unable to create file: %s", WindowsErrorString());
				if ((err == ERROR_ACCESS_DENIED) && (safe_strcmp(&psz_sanpath[3], autorun_name) == 0))
					uprintf(stupid_antivirus);
				else
					goto out;
			} else {
				preallocate_filesize(file_handle, file_length);
				for (j=0; j<p_statbuf->extents; j++) {
					extent_length = p_statbuf->extsize[j];
					for (i=0; extent_length>0; i++) {
						if (FormatStatus) goto out;
						memset(buf, 0, ISO_BLOCKSIZE);
						lsn = p_statbuf->lsn[j] + (lsn_t)i;
						if (iso9660_iso_seek_read(p_iso, buf, lsn, 1) != ISO_BLOCKSIZE) {
							uprintf("  Error reading ISO9660 file %s at LSN %lu",
								psz_iso_name, (long unsigned int)lsn);
							goto out;
						}
						buf_size = (DWORD)MIN(extent_length, ISO_BLOCKSIZE);
						ISO_BLOCKING(r = WriteFileWithRetry(file_handle, buf, buf_size, &wr_size, WRITE_RETRIES));
						if (!r) {
							uprintf("  Error writing file: %s", WindowsErrorString());
							goto out;
						}
						extent_length -= ISO_BLOCKSIZE;
						if (nb_blocks++ % PROGRESS_THRESHOLD == 0)
							UpdateProgress(OP_DOS, 100.0f*nb_blocks/total_blocks);
					}
				}
			}
			if (preserve_timestamps) {
				LPFILETIME ft = to_filetime(mktime(&p_statbuf->tm));
				if (!SetFileTime(file_handle, ft, ft, ft))
					uprintf("  Could not set timestamp: %s", WindowsErrorString());
			}
			ISO_BLOCKING(safe_closehandle(file_handle));
			if (props.is_cfg)
				fix_config(psz_sanpath, psz_path, psz_basename, &props);
			safe_free(psz_sanpath);
		}
	}
	r = 0;

out:
	ISO_BLOCKING(safe_closehandle(file_handle));
	iso9660_filelist_free(p_entlist);
	return r;
}

void GetGrubVersion(char* buf, size_t buf_size)
{
	char *p, unauthorized[] = {'<', '>', ':', '|', '*', '?', '\\', '/'};
	size_t i;
	const char grub_version_str[] = "GRUB  version %s";

	for (i=0; i<buf_size; i++) {
		if (memcmp(&buf[i], grub_version_str, sizeof(grub_version_str)) == 0) {
			static_strcpy(img_report.grub2_version, &buf[i + sizeof(grub_version_str)]);
			break;
		}
	}
	// Sanitize the string
	for (p = &img_report.grub2_version[0]; *p; p++) {
		for (i=0; i<sizeof(unauthorized); i++) {
			if (*p == unauthorized[i])
				*p = '_';
		}
	}
	// <Shakes fist angrily> "KASPERSKYYYYYY!!!..." (https://github.com/pbatard/rufus/issues/467)
	// But seriously, these guys should know better than "security" through obscurity...
	if (img_report.grub2_version[0] == '0')
		img_report.grub2_version[0] = 0;
}

BOOL ExtractISO(const char* src_iso, const char* dest_dir, BOOL scan)
{
	size_t i, j, size, sl_index = 0;
	uint16_t sl_version;
	FILE* fd;
	int r = 1;
	iso9660_t* p_iso = NULL;
	iso9660_pvd_t pvd;
	udf_t* p_udf = NULL;
	udf_dirent_t* p_udf_root;
	char *tmp, *buf, *ext;
	char path[MAX_PATH], path2[16];
	const char* basedir[] = { "i386", "amd64", "minint" };
	const char* tmp_sif = ".\\txtsetup.sif~";
	iso_extension_mask_t iso_extension_mask = ISO_EXTENSION_ALL;
	char* spacing = "  ";

	if ((!enable_iso) || (src_iso == NULL) || (dest_dir == NULL))
		return FALSE;

	scan_only = scan;
	if (!scan_only)
		spacing = "";
	cdio_log_set_handler(log_handler);
	psz_extract_dir = dest_dir;
	// Change progress style to marquee for scanning
	if (scan_only) {
		uprintf("ISO analysis:");
		SendMessage(hMainDialog, UM_PROGRESS_INIT, PBS_MARQUEE, 0);
		total_blocks = 0;
		has_ldlinux_c32 = FALSE;
		// String array of all isolinux/syslinux locations
		StrArrayCreate(&config_path, 8);
		StrArrayCreate(&isolinux_path, 8);
		PrintInfo(0, MSG_202);
	} else {
		uprintf("Extracting files...\n");
		IGNORE_RETVAL(_chdirU(app_dir));
		PrintInfo(0, MSG_231);
		if (total_blocks == 0) {
			uprintf("Error: ISO has not been properly scanned.\n");
			FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_ISO_SCAN);
			goto out;
		}
		nb_blocks = 0;
		iso_blocking_status = 0;
	}

	// First try to open as UDF - fallback to ISO if it failed
	p_udf = udf_open(src_iso);
	if (p_udf == NULL)
		goto try_iso;
	uprintf("%sImage is an UDF image", spacing);

	p_udf_root = udf_get_root(p_udf, true, 0);
	if (p_udf_root == NULL) {
		uprintf("%sCould not locate UDF root directory", spacing);
		goto out;
	}
	if (scan_only) {
		if (udf_get_logical_volume_id(p_udf, img_report.label, sizeof(img_report.label)) <= 0)
			img_report.label[0] = 0;
		// Open the UDF as ISO so that we can perform size checks
		p_iso = iso9660_open(src_iso);
	}
	r = udf_extract_files(p_udf, p_udf_root, "");
	goto out;

try_iso:
	// Perform our first scan with Joliet disabled (if Rock Ridge is enabled), so that we can find if
	// there exists a Rock Ridge file with a name > 64 chars or if there are symlinks. If that is the
	// case then we also disable Joliet during the extract phase.
	if ((!enable_joliet) || (enable_rockridge && (scan_only || img_report.has_long_filename || img_report.has_symlinks))) {
		iso_extension_mask &= ~ISO_EXTENSION_JOLIET;
	}
	if (!enable_rockridge) {
		iso_extension_mask &= ~ISO_EXTENSION_ROCK_RIDGE;
	}

	p_iso = iso9660_open_ext(src_iso, iso_extension_mask);
	if (p_iso == NULL) {
		uprintf("%s'%s' doesn't look like an ISO image", spacing, src_iso);
		r = 1;
		goto out;
	}
	uprintf("%sImage is an ISO9660 image", spacing);
	joliet_level = iso9660_ifs_get_joliet_level(p_iso);
	if (scan_only) {
		if (iso9660_ifs_get_volume_id(p_iso, &tmp)) {
			static_strcpy(img_report.label, tmp);
			safe_free(tmp);
		} else
			img_report.label[0] = 0;
	} else {
		if (iso_extension_mask & (ISO_EXTENSION_JOLIET|ISO_EXTENSION_ROCK_RIDGE))
			uprintf("%sThis image will be extracted using %s extensions (if present)", spacing,
				(iso_extension_mask & ISO_EXTENSION_JOLIET)?"Joliet":"Rock Ridge");
		else
			uprintf("%sThis image will not be extracted using any ISO extensions", spacing);
	}
	r = iso_extract_files(p_iso, "");

out:
	iso_blocking_status = -1;
	if (scan_only) {
		struct __stat64 stat;
		// Find if there is a mismatch between the ISO size, as reported by the PVD, and the actual file size
		if ((iso9660_ifs_read_pvd(p_iso, &pvd)) && (_stat64U(src_iso, &stat) == 0))
			img_report.mismatch_size = (int64_t)(iso9660_get_pvd_space_size(&pvd)) * ISO_BLOCKSIZE - stat.st_size;
		// Remove trailing spaces from the label
		for (j=safe_strlen(img_report.label)-1; ((j>0)&&(isspaceU(img_report.label[j]))); j--)
			img_report.label[j] = 0;
		// We use the fact that UDF_BLOCKSIZE and ISO_BLOCKSIZE are the same here
		img_report.projected_size = total_blocks * ISO_BLOCKSIZE;
		// We will link the existing isolinux.cfg from a syslinux.cfg we create
		// If multiple config files exist, choose the one with the shortest path
		// (so that a '/syslinux.cfg' is preferred over a '/isolinux/isolinux.cfg')
		if (!IsStrArrayEmpty(config_path)) {
			// Set the img_report.cfg_path string to maximum length, so that we don't have to
			// do a special case for StrArray entry 0.
			memset(img_report.cfg_path, '_', sizeof(img_report.cfg_path)-1);
			img_report.cfg_path[sizeof(img_report.cfg_path)-1] = 0;
			for (i=0; i<config_path.Index; i++) {
				// OpenSuse based Live image have a /syslinux.cfg that doesn't work, so we enforce
				// the use of the one in '/boot/[i386|x86_64]/loader/isolinux.cfg' if present.
				// Note that, because the openSuse live script are not designed to handle anything but
				// an ISO9660 filesystem for the live device, this still won't allow for proper boot.
				// See https://github.com/openSUSE/kiwi/issues/354
				if ( (_stricmp(config_path.String[i], "/boot/i386/loader/isolinux.cfg") == 0) ||
					 (_stricmp(config_path.String[i], "/boot/x86_64/loader/isolinux.cfg") == 0)) {
					static_strcpy(img_report.cfg_path, config_path.String[i]);
					img_report.needs_syslinux_overwrite = TRUE;
					break;
				}
				// Tails uses an '/EFI/BOOT/isolinux.cfg' along with a '/isolinux/isolinux.cfg'
				// which are the exact same length. However, only the /isolinux one will work,
				// so for now, at equal length, always pick the latest.
				// We may have to revisit this and prefer a path that contains '/isolinux' if
				// this hack is not enough for other images.
				if (safe_strlen(img_report.cfg_path) >= safe_strlen(config_path.String[i]))
					static_strcpy(img_report.cfg_path, config_path.String[i]);
			}
			uprintf("  Will use '%s' for Syslinux", img_report.cfg_path);
			// Extract all of the isolinux.bin files we found to identify their versions
			for (i=0; i<isolinux_path.Index; i++) {
				char isolinux_tmp[MAX_PATH];
				static_sprintf(isolinux_tmp, "%s\\isolinux.tmp", temp_dir);
				size = (size_t)ExtractISOFile(src_iso, isolinux_path.String[i], isolinux_tmp, FILE_ATTRIBUTE_NORMAL);
				if (size == 0) {
					uprintf("  Could not access %s", isolinux_path.String[i]);
				} else {
					buf = (char*)calloc(size, 1);
					if (buf == NULL) break;
					fd = fopen(isolinux_tmp, "rb");
					if (fd == NULL) {
						free(buf);
						continue;
					}
					fread(buf, 1, size, fd);
					fclose(fd);
					sl_version = GetSyslinuxVersion(buf, size, &ext);
					if (img_report.sl_version == 0) {
						static_strcpy(img_report.sl_version_ext, ext);
						img_report.sl_version = sl_version;
						sl_index = i;
					} else if ((img_report.sl_version != sl_version) || (safe_strcmp(img_report.sl_version_ext, ext) != 0)) {
						uprintf("  Found conflicting isolinux versions:\n  '%s' (%d.%02d%s) vs '%s' (%d.%02d%s)",
							isolinux_path.String[sl_index], SL_MAJOR(img_report.sl_version), SL_MINOR(img_report.sl_version),
							img_report.sl_version_ext, isolinux_path.String[i], SL_MAJOR(sl_version), SL_MINOR(sl_version), ext);
						// Workaround for Antergos and other ISOs, that have multiple Syslinux versions.
						// Where possible, prefer to the one that resides in the same directory as the config file.
						for (j=safe_strlen(img_report.cfg_path); (j>0) && (img_report.cfg_path[j]!='/'); j--);
						if (safe_strnicmp(img_report.cfg_path, isolinux_path.String[i], j) == 0) {
							static_strcpy(img_report.sl_version_ext, ext);
							img_report.sl_version = sl_version;
							sl_index = i;
						}
					}
					free(buf);
				}
				_unlinkU(isolinux_tmp);
			}
			if (img_report.sl_version != 0) {
				static_sprintf(img_report.sl_version_str, "%d.%02d",
					SL_MAJOR(img_report.sl_version), SL_MINOR(img_report.sl_version));
				uprintf("  Detected Syslinux version: %s%s (from '%s')",
					img_report.sl_version_str, img_report.sl_version_ext, isolinux_path.String[sl_index]);
				if ( (has_ldlinux_c32 && (SL_MAJOR(img_report.sl_version) < 5))
				  || (!has_ldlinux_c32 && (SL_MAJOR(img_report.sl_version) >= 5)) )
					uprintf("  Warning: Conflict between Isolinux version and the presence of ldlinux.c32...");
			} else {
				// Couldn't find a version from isolinux.bin. Force set to the versions we embed
				img_report.sl_version = embedded_sl_version[has_ldlinux_c32?1:0];
				static_sprintf(img_report.sl_version_str, "%d.%02d",
					SL_MAJOR(img_report.sl_version), SL_MINOR(img_report.sl_version));
				uprintf("  Warning: Could not detect Isolinux version - Forcing to %s (embedded)",
					img_report.sl_version_str);
			}
		}
		if (!IS_EFI_BOOTABLE(img_report) && HAS_EFI_IMG(img_report) && ExtractEfiImgFiles(NULL)) {
			img_report.has_efi = 0x80;
		}
		if (HAS_WINPE(img_report)) {
			// In case we have a WinPE 1.x based iso, we extract and parse txtsetup.sif
			// during scan, to see if /minint was provided for OsLoadOptions, as it decides
			// whether we should use 0x80 or 0x81 as the disk ID in the MBR
			static_sprintf(path, "/%s/txtsetup.sif",
				basedir[((img_report.winpe&WINPE_I386) == WINPE_I386)?0:((img_report.winpe&WINPE_AMD64) == WINPE_AMD64?1:2)]);
			ExtractISOFile(src_iso, path, tmp_sif, FILE_ATTRIBUTE_NORMAL);
			tmp = get_token_data_file("OsLoadOptions", tmp_sif);
			if (tmp != NULL) {
				for (i=0; i<strlen(tmp); i++)
					tmp[i] = (char)tolower(tmp[i]);
				uprintf("  Checking txtsetup.sif:\n  OsLoadOptions = %s", tmp);
				img_report.uses_minint = (strstr(tmp, "/minint") != NULL);
			}
			_unlinkU(tmp_sif);
			safe_free(tmp);
		}
		if (HAS_WININST(img_report)) {
			img_report.wininst_version = GetInstallWimVersion(src_iso);
		}
		if (img_report.has_grub2) {
			// In case we have a GRUB2 based iso, we extract boot/grub/i386-pc/normal.mod to parse its version
			img_report.grub2_version[0] = 0;
			if ((GetTempPathU(sizeof(path), path) != 0) && (GetTempFileNameU(path, APPLICATION_NAME, 0, path) != 0)) {
				size = (size_t)ExtractISOFile(src_iso, "boot/grub/i386-pc/normal.mod", path, FILE_ATTRIBUTE_NORMAL);
				buf = (char*)calloc(size, 1);
				fd = fopen(path, "rb");
				if ((size == 0) || (buf == NULL) || (fd == NULL)) {
					uprintf("  Could not read Grub version from 'boot/grub/i386-pc/normal.mod'");
				} else {
					fread(buf, 1, size, fd);
					fclose(fd);
					GetGrubVersion(buf, size);
				}
				free(buf);
				_unlinkU(path);
			}
			if (img_report.grub2_version[0] != 0)
				uprintf("  Detected Grub version: %s", img_report.grub2_version);
			else {
				uprintf("  Could not detect Grub version");
				img_report.has_grub2 = FALSE;
			}
		}
		StrArrayDestroy(&config_path);
		StrArrayDestroy(&isolinux_path);
		SendMessage(hMainDialog, UM_PROGRESS_EXIT, 0, 0);
	} else {
		// For Debian live ISOs, that only provide EFI boot files in a FAT efi.img
		if (img_report.has_efi == 0x80)
			ExtractEfiImgFiles(dest_dir);
		if (HAS_SYSLINUX(img_report)) {
			static_sprintf(path, "%s\\syslinux.cfg", dest_dir);
			// Create a /syslinux.cfg (if none exists) that points to the existing isolinux cfg
			fd = fopen(path, "r");
			if (fd != NULL && img_report.needs_syslinux_overwrite) {
				fclose(fd);
				fd = NULL;
				static_sprintf(path2, "%s\\syslinux.org", dest_dir);
				uprintf("Renaming: %s ➔ %s", path, path2);
				IGNORE_RETVAL(rename(path, path2));
			}
			if (fd == NULL) {
				fd = fopen(path, "w");	// No "/syslinux.cfg" => create a new one
				if (fd == NULL) {
					uprintf("Unable to create %s - booting from USB will not work", path);
					r = 1;
				} else {
					fprintf(fd, "DEFAULT loadconfig\n\nLABEL loadconfig\n  CONFIG %s\n", img_report.cfg_path);
					for (i = safe_strlen(img_report.cfg_path); (i > 0) && (img_report.cfg_path[i] != '/'); i--);
					if (i > 0) {
						img_report.cfg_path[i] = 0;
						fprintf(fd, "  APPEND %s/\n", img_report.cfg_path);
						img_report.cfg_path[i] = '/';
					}
					uprintf("Created: %s", path);
				}
			}
			if (fd != NULL)
				fclose(fd);
		}
	}
	if (p_iso != NULL)
		iso9660_close(p_iso);
	if (p_udf != NULL)
		udf_close(p_udf);
	if ((r != 0) && (FormatStatus == 0))
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR((scan_only?ERROR_ISO_SCAN:ERROR_ISO_EXTRACT));
	return (r == 0);
}

int64_t ExtractISOFile(const char* iso, const char* iso_file, const char* dest_file, DWORD attributes)
{
	size_t i, j;
	ssize_t read_size;
	int64_t file_length, extent_length, r = 0;
	char buf[UDF_BLOCKSIZE];
	DWORD buf_size, wr_size;
	iso9660_t* p_iso = NULL;
	udf_t* p_udf = NULL;
	udf_dirent_t *p_udf_root = NULL, *p_udf_file = NULL;
	iso9660_stat_t *p_statbuf = NULL;
	lsn_t lsn;
	HANDLE file_handle = INVALID_HANDLE_VALUE;

	file_handle = CreateFileU(dest_file, GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ, NULL, CREATE_ALWAYS, attributes, NULL);
	if (file_handle == INVALID_HANDLE_VALUE) {
		uprintf("  Could not create file %s: %s", dest_file, WindowsErrorString());
		goto out;
	}

	// First try to open as UDF - fallback to ISO if it failed
	p_udf = udf_open(iso);
	if (p_udf == NULL)
		goto try_iso;

	p_udf_root = udf_get_root(p_udf, true, 0);
	if (p_udf_root == NULL) {
		uprintf("Could not locate UDF root directory");
		goto out;
	}
	p_udf_file = udf_fopen(p_udf_root, iso_file);
	if (!p_udf_file) {
		uprintf("Could not locate file %s in ISO image", iso_file);
		goto out;
	}
	file_length = udf_get_file_length(p_udf_file);
	while (file_length > 0) {
		memset(buf, 0, UDF_BLOCKSIZE);
		read_size = udf_read_block(p_udf_file, buf, 1);
		if (read_size < 0) {
			uprintf("Error reading UDF file %s", iso_file);
			goto out;
		}
		buf_size = (DWORD)MIN(file_length, read_size);
		if (!WriteFileWithRetry(file_handle, buf, buf_size, &wr_size, WRITE_RETRIES)) {
			uprintf("  Error writing file %s: %s", dest_file, WindowsErrorString());
			goto out;
		}
		file_length -= read_size;
		r += read_size;
	}
	goto out;

try_iso:
	p_iso = iso9660_open(iso);
	if (p_iso == NULL) {
		uprintf("Unable to open image '%s'", iso);
		goto out;
	}

	p_statbuf = iso9660_ifs_stat_translate(p_iso, iso_file);
	if (p_statbuf == NULL) {
		uprintf("Could not get ISO-9660 file information for file %s", iso_file);
		goto out;
	}

	for (j = 0; j < p_statbuf->extents; j++) {
		extent_length = p_statbuf->extsize[j];
		for (i = 0; extent_length > 0; i++) {
			memset(buf, 0, ISO_BLOCKSIZE);
			lsn = p_statbuf->lsn[j] + (lsn_t)i;
			if (iso9660_iso_seek_read(p_iso, buf, lsn, 1) != ISO_BLOCKSIZE) {
				uprintf("  Error reading ISO9660 file %s at LSN %lu", iso_file, (long unsigned int)lsn);
				goto out;
			}
			buf_size = (DWORD)MIN(extent_length, ISO_BLOCKSIZE);
			if (!WriteFileWithRetry(file_handle, buf, buf_size, &wr_size, WRITE_RETRIES)) {
				uprintf("  Error writing file %s: %s", dest_file, WindowsErrorString());
				goto out;
			}
			extent_length -= ISO_BLOCKSIZE;
			r += ISO_BLOCKSIZE;
		}
	}

out:
	safe_closehandle(file_handle);
	if (p_statbuf != NULL)
		safe_free(p_statbuf->rr.psz_symlink);
	safe_free(p_statbuf);
	if (p_udf_root != NULL)
		udf_dirent_free(p_udf_root);
	if (p_udf_file != NULL)
		udf_dirent_free(p_udf_file);
	if (p_iso != NULL)
		iso9660_close(p_iso);
	if (p_udf != NULL)
		udf_close(p_udf);
	return r;
}

uint32_t GetInstallWimVersion(const char* iso)
{
	char *wim_path = NULL, *p, buf[UDF_BLOCKSIZE] = { 0 };
	uint32_t* wim_header = (uint32_t*)buf, r = 0xffffffff;
	iso9660_t* p_iso = NULL;
	udf_t* p_udf = NULL;
	udf_dirent_t *p_udf_root = NULL, *p_udf_file = NULL;
	iso9660_stat_t *p_statbuf = NULL;

	wim_path = safe_strdup(&img_report.wininst_path[0][2]);
	if (wim_path == NULL)
		goto out;
	// UDF indiscriminately accepts slash or backslash delimiters,
	// but ISO-9660 requires slash
	for (p = wim_path; *p != 0; p++)
		if (*p == '\\') *p = '/';

	// First try to open as UDF - fallback to ISO if it failed
	p_udf = udf_open(iso);
	if (p_udf == NULL)
		goto try_iso;

	p_udf_root = udf_get_root(p_udf, true, 0);
	if (p_udf_root == NULL) {
		uprintf("Could not locate UDF root directory");
		goto out;
	}
	p_udf_file = udf_fopen(p_udf_root, wim_path);
	if (!p_udf_file) {
		uprintf("Could not locate file %s in ISO image", wim_path);
		goto out;
	}
	if (udf_read_block(p_udf_file, buf, 1) != UDF_BLOCKSIZE) {
		uprintf("Error reading UDF file %s", wim_path);
		goto out;
	}
	r = wim_header[3];
	goto out;

try_iso:
	p_iso = iso9660_open(iso);
	if (p_iso == NULL) {
		uprintf("Could not open image '%s'", iso);
		goto out;
	}
	p_statbuf = iso9660_ifs_stat_translate(p_iso, wim_path);
	if (p_statbuf == NULL) {
		uprintf("Could not get ISO-9660 file information for file %s", wim_path);
		goto out;
	}
	if (iso9660_iso_seek_read(p_iso, buf, p_statbuf->lsn[0], 1) != ISO_BLOCKSIZE) {
		uprintf("Error reading ISO-9660 file %s at LSN %d", wim_path, p_statbuf->lsn[0]);
		goto out;
	}
	r = wim_header[3];

out:
	if (p_statbuf != NULL)
		safe_free(p_statbuf->rr.psz_symlink);
	safe_free(p_statbuf);
	if (p_udf_root != NULL)
		udf_dirent_free(p_udf_root);
	if (p_udf_file != NULL)
		udf_dirent_free(p_udf_file);
	if (p_iso != NULL)
		iso9660_close(p_iso);
	if (p_udf != NULL)
		udf_close(p_udf);
	safe_free(wim_path);
	return bswap_uint32(r);
}

#define ISO_NB_BLOCKS 16
typedef struct {
	iso9660_t*      p_iso;
	lsn_t           lsn;
	libfat_sector_t sec_start;
	// Use a multi block buffer, to improve sector reads
	uint8_t         buf[ISO_BLOCKSIZE * ISO_NB_BLOCKS];
} iso9660_readfat_private;

/*
 * Read sectors from a FAT img file residing on an ISO-9660 filesystem.
 * NB: This assumes that the img file sectors are contiguous on the ISO.
  */
int iso9660_readfat(intptr_t pp, void *buf, size_t secsize, libfat_sector_t sec)
{
	iso9660_readfat_private* p_private = (iso9660_readfat_private*)pp;

	if (sizeof(p_private->buf) % secsize != 0) {
		uprintf("iso9660_readfat: Sector size %d is not a divisor of %d", secsize, sizeof(p_private->buf));
		return 0;
	}

	if ((sec < p_private->sec_start) || (sec >= p_private->sec_start + sizeof(p_private->buf) / secsize)) {
		// Sector being queried is not in our multi block buffer -> Update it
		p_private->sec_start = (((sec * secsize) / ISO_BLOCKSIZE) * ISO_BLOCKSIZE) / secsize;
		if (iso9660_iso_seek_read(p_private->p_iso, p_private->buf,
			p_private->lsn + (lsn_t)((p_private->sec_start * secsize) / ISO_BLOCKSIZE), ISO_NB_BLOCKS)
			!= ISO_NB_BLOCKS * ISO_BLOCKSIZE) {
			uprintf("Error reading ISO-9660 file %s at LSN %lu\n", img_report.efi_img_path,
				(long unsigned int)(p_private->lsn + (p_private->sec_start * secsize) / ISO_BLOCKSIZE));
			return 0;
		}
	}
	memcpy(buf, &p_private->buf[(sec - p_private->sec_start)*secsize], secsize);
	return (int)secsize;
}

/*
 * Extract EFI bootloaders files from an ISO-9660 FAT img file into directory <dir>.
 * If <dir> is NULL, returns TRUE if an EFI bootloader exists in the img.
 * If <dir> is not NULL, returns TRUE if any if the bootloaders was properly written.
 */
BOOL ExtractEfiImgFiles(const char* dir)
{
	BOOL ret = FALSE;
	HANDLE handle;
	DWORD size, file_size, written;
	iso9660_t* p_iso = NULL;
	iso9660_stat_t* p_statbuf = NULL;
	iso9660_readfat_private* p_private = NULL;
	libfat_sector_t s;
	int32_t dc, c;
	struct libfat_filesystem *lf_fs = NULL;
	struct libfat_direntry direntry;
	char name[12] = { 0 };
	char path[64];
	int i, j, k;
	void* buf;

	if ((image_path == NULL) || !HAS_EFI_IMG(img_report))
		return FALSE;

	p_iso = iso9660_open(image_path);
	if (p_iso == NULL) {
		uprintf("Could not open image '%s' as an ISO-9660 file system", image_path);
		goto out;
	}
	p_statbuf = iso9660_ifs_stat_translate(p_iso, img_report.efi_img_path);
	if (p_statbuf == NULL) {
		uprintf("Could not get ISO-9660 file information for file %s\n", img_report.efi_img_path);
		goto out;
	}
	p_private = malloc(sizeof(iso9660_readfat_private));
	if (p_private == NULL)
		goto out;
	p_private->p_iso = p_iso;
	p_private->lsn = p_statbuf->lsn[0];	// Image should be small enough not to use multiextents
	p_private->sec_start = 0;
	// Populate our intial buffer
	if (iso9660_iso_seek_read(p_private->p_iso, p_private->buf, p_private->lsn, ISO_NB_BLOCKS) != ISO_NB_BLOCKS * ISO_BLOCKSIZE) {
		uprintf("Error reading ISO-9660 file %s at LSN %lu\n", img_report.efi_img_path, (long unsigned int)p_private->lsn);
		goto out;
	}
	lf_fs = libfat_open(iso9660_readfat, (intptr_t)p_private);
	if (lf_fs == NULL) {
		uprintf("FAT access error");
		goto out;
	}

	// Navigate to /EFI/BOOT
	if (libfat_searchdir(lf_fs, 0, "EFI        ", &direntry) < 0)
		goto out;
	dc = direntry.entry[26] + (direntry.entry[27] << 8);
	if (libfat_searchdir(lf_fs, dc, "BOOT       ", &direntry) < 0)
		goto out;
	dc = direntry.entry[26] + (direntry.entry[27] << 8);

	for (i = 0; i < ARRAYSIZE(efi_bootname); i++) {
		// Sanity check in case the EFI forum comes up with a 'bootmips64.efi' or something...
		if (strlen(efi_bootname[i]) > 12) {
			uprintf("Internal error: FAT 8.3");
			continue;
		}
		for (j = 0, k = 0; efi_bootname[i][j] != 0; j++) {
			if (efi_bootname[i][j] == '.') {
				while (k < 8)
					name[k++] = ' ';
			} else
				name[k++] = toupper(efi_bootname[i][j]);
		}
		c = libfat_searchdir(lf_fs, dc, name, &direntry);
		if (c > 0) {
			if (dir == NULL) {
				if (!ret)
					uprintf("  Detected EFI bootloader(s) (from '%s'):", img_report.efi_img_path);
				uprintf("  ● '%s'", efi_bootname[i]);
				ret = TRUE;
			} else {
				file_size = direntry.entry[28] + (direntry.entry[29] << 8) + (direntry.entry[30] << 16) +
					(direntry.entry[31] << 24);
				// Sanity check
				if (file_size > 64 * MB) {
					uprintf("Warning: File size is larger than 64 MB => not extracted");
					continue;
				}
				static_sprintf(path, "%s\\efi", dir);
				if (!CreateDirectoryA(path, 0) && (GetLastError() != ERROR_ALREADY_EXISTS)) {
					uprintf("Could not create directory '%s': %s\n", path, WindowsErrorString());
					continue;
				}
				static_strcat(path, "\\boot");
				if (!CreateDirectoryA(path, 0) && (GetLastError() != ERROR_ALREADY_EXISTS)) {
					uprintf("Could not create directory '%s': %s\n", path, WindowsErrorString());
					continue;
				}
				static_strcat(path, "\\");
				static_strcat(path, efi_bootname[i]);
				uprintf("Extracting: %s (from '%s', %s)", path, img_report.efi_img_path,
					SizeToHumanReadable(file_size, FALSE, FALSE));
				handle = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
					NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
				if (handle == INVALID_HANDLE_VALUE) {
					uprintf("Unable to create '%s': %s", path, WindowsErrorString());
					continue;
				}

				written = 0;
				s = libfat_clustertosector(lf_fs, c);
				while ((s != 0) && (s < 0xFFFFFFFFULL) && (written < file_size)) {
					buf = libfat_get_sector(lf_fs, s);
					size = MIN(LIBFAT_SECTOR_SIZE, file_size - written);
					if (!WriteFileWithRetry(handle, buf, size, &size, WRITE_RETRIES) ||
						(size != MIN(LIBFAT_SECTOR_SIZE, file_size - written))) {
						uprintf("Error writing '%s': %s", path, WindowsErrorString());
						CloseHandle(handle);
						continue;
					}
					written += size;
					s = libfat_nextsector(lf_fs, s);
				}
				CloseHandle(handle);
				ret = TRUE;
			}
		}
	}

out:
	if (lf_fs != NULL)
		libfat_close(lf_fs);
	if (p_statbuf != NULL)
		safe_free(p_statbuf->rr.psz_symlink);
	safe_free(p_statbuf);
	safe_free(p_private);
	if (p_iso != NULL)
		iso9660_close(p_iso);
	return ret;
}

// VirtDisk API Prototypes - Only available for Windows 8 or later
PF_TYPE_DECL(WINAPI, DWORD, OpenVirtualDisk, (PVIRTUAL_STORAGE_TYPE, PCWSTR,
	VIRTUAL_DISK_ACCESS_MASK, OPEN_VIRTUAL_DISK_FLAG, POPEN_VIRTUAL_DISK_PARAMETERS, PHANDLE));
PF_TYPE_DECL(WINAPI, DWORD, AttachVirtualDisk, (HANDLE, PSECURITY_DESCRIPTOR,
	ATTACH_VIRTUAL_DISK_FLAG, ULONG, PATTACH_VIRTUAL_DISK_PARAMETERS, LPOVERLAPPED));
PF_TYPE_DECL(WINAPI, DWORD, DetachVirtualDisk, (HANDLE, DETACH_VIRTUAL_DISK_FLAG, ULONG));
PF_TYPE_DECL(WINAPI, DWORD, GetVirtualDiskPhysicalPath, (HANDLE, PULONG, PWSTR));

static char physical_path[128] = "";
static HANDLE mounted_handle = INVALID_HANDLE_VALUE;

char* MountISO(const char* path)
{
	VIRTUAL_STORAGE_TYPE vtype = { 1, VIRTUAL_STORAGE_TYPE_VENDOR_MICROSOFT };
	ATTACH_VIRTUAL_DISK_PARAMETERS vparams = {0};
	DWORD r;
	wchar_t wtmp[128];
	ULONG size = ARRAYSIZE(wtmp);
	wconvert(path);
	char* ret = NULL;

	PF_INIT_OR_OUT(OpenVirtualDisk, VirtDisk);
	PF_INIT_OR_OUT(AttachVirtualDisk, VirtDisk);
	PF_INIT_OR_OUT(GetVirtualDiskPhysicalPath, VirtDisk);

	if ((mounted_handle != NULL) && (mounted_handle != INVALID_HANDLE_VALUE))
		UnMountISO();

	r = pfOpenVirtualDisk(&vtype, wpath, VIRTUAL_DISK_ACCESS_READ | VIRTUAL_DISK_ACCESS_GET_INFO,
		OPEN_VIRTUAL_DISK_FLAG_NONE, NULL, &mounted_handle);
	if (r != ERROR_SUCCESS) {
		SetLastError(r);
		uprintf("Could not open ISO '%s': %s", path, WindowsErrorString());
		goto out;
	}

	vparams.Version = ATTACH_VIRTUAL_DISK_VERSION_1;
	r = pfAttachVirtualDisk(mounted_handle, NULL, ATTACH_VIRTUAL_DISK_FLAG_READ_ONLY |
		ATTACH_VIRTUAL_DISK_FLAG_NO_DRIVE_LETTER, 0, &vparams, NULL);
	if (r != ERROR_SUCCESS) {
		SetLastError(r);
		uprintf("Could not mount ISO '%s': %s", path, WindowsErrorString());
		goto out;
	}

	r = pfGetVirtualDiskPhysicalPath(mounted_handle, &size, wtmp);
	if (r != ERROR_SUCCESS) {
		SetLastError(r);
		uprintf("Could not obtain physical path for mounted ISO '%s': %s", path, WindowsErrorString());
		goto out;
	}
	wchar_to_utf8_no_alloc(wtmp, physical_path, sizeof(physical_path));
	ret = physical_path;

out:
	if (ret == NULL)
		UnMountISO();
	wfree(path);
	return ret;
}

void UnMountISO(void)
{
	PF_INIT_OR_OUT(DetachVirtualDisk, VirtDisk);

	if ((mounted_handle == NULL) || (mounted_handle == INVALID_HANDLE_VALUE))
		goto out;

	pfDetachVirtualDisk(mounted_handle, DETACH_VIRTUAL_DISK_FLAG_NONE, 0);
	safe_closehandle(mounted_handle);
out:
	physical_path[0] = 0;
}
