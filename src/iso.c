/*
 * Rufus: The Reliable USB Formatting Utility
 * ISO file extraction
 * Copyright © 2011-2014 Pete Batard <pete@akeo.ie>
 * Based on libcdio's iso & udf samples:
 * Copyright © 2003-2012 Rocky Bernstein <rocky@gnu.org>
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

#include <cdio/cdio.h>
#include <cdio/logging.h>
#include <cdio/iso9660.h>
#include <cdio/udf.h>

#include "rufus.h"
#include "msapi_utf8.h"
#include "resource.h"
#include "localization.h"

// How often should we update the progress bar (in 2K blocks) as updating
// the progress bar for every block will bring extraction to a crawl
#define PROGRESS_THRESHOLD        128
#define FOUR_GIGABYTES            4294967296LL

// Needed for UDF ISO access
CdIo_t* cdio_open (const char* psz_source, driver_id_t driver_id) {return NULL;}
void cdio_destroy (CdIo_t* p_cdio) {}

RUFUS_ISO_REPORT iso_report;
int64_t iso_blocking_status = -1;
BOOL enable_iso = TRUE, enable_joliet = TRUE, enable_rockridge = TRUE, has_ldlinux_c32;
#define ISO_BLOCKING(x) do {x; iso_blocking_status++; } while(0)
static const char* psz_extract_dir;
static const char* bootmgr_efi_name = "bootmgr.efi";
static const char* grldr_name = "grldr";
static const char* ldlinux_name = "ldlinux.sys";
static const char* ldlinux_c32 = "ldlinux.c32";
static const char* efi_dirname = "/efi/boot";
static const char* grub_dirname = "/boot/grub";   // NB: We don't support nonstandard config dir such as AROS' "/boot/pc/grub/"
static const char* syslinux_cfg[] = { "isolinux.cfg", "syslinux.cfg", "extlinux.conf"};
static const char dot_isolinux_bin[] = ".\\isolinux.bin";
static const char* isolinux_bin = &dot_isolinux_bin[2];
static const char* pe_dirname[] = { "/i386", "/minint" };
static const char* pe_file[] = { "ntdetect.com", "setupldr.bin", "txtsetup.sif" };
static const char* reactos_name = "setupldr.sys"; // TODO: freeldr.sys doesn't seem to work
static const char* kolibri_name = "kolibri.img";
static const char* autorun_name = "autorun.inf";
static const char* stupid_antivirus = "  NOTE: This is usually caused by a poorly designed security solution. "
	"See http://rufus.akeo.ie/compatibility.\r\n  This file will be skipped for now, but you should really "
	"look into using a *SMARTER* antivirus solution.";
const char* old_c32_name[NB_OLD_C32] = OLD_C32_NAMES;
static const int64_t old_c32_threshold[NB_OLD_C32] = OLD_C32_THRESHOLD;
static uint8_t i_joliet_level = 0;
static uint64_t total_blocks, nb_blocks;
static BOOL scan_only = FALSE;
static StrArray config_path, isolinux_path;

// Ensure filenames do not contain invalid FAT32 or NTFS characters
static __inline char* sanitize_filename(char* filename, BOOL* is_identical)
{
	size_t i, j;
	char* ret = NULL;
	char unauthorized[] = {'<', '>', ':', '|', '*', '?'};

	*is_identical = TRUE;
	ret = safe_strdup(filename);
	if (ret == NULL) {
		uprintf("Couldn't allocate string for sanitized path");
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
		// TODO: use a registry key to enable libcdio debug?
		return;
	default:
		uprintf("libcdio: %s\n", message);
	}
}

/*
 * Scan and set ISO properties
 * Returns true if the the current file does not need to be processed further
 */
static BOOL check_iso_props(const char* psz_dirname, BOOL* is_syslinux_cfg, BOOL* is_old_c32, 
	int64_t i_file_length, const char* psz_basename, const char* psz_fullpath)
{
	size_t i, j;

	// Check for an isolinux/syslinux config file anywhere
	*is_syslinux_cfg = FALSE;
	for (i=0; i<ARRAYSIZE(syslinux_cfg); i++) {
		if (safe_stricmp(psz_basename, syslinux_cfg[i]) == 0) {
			*is_syslinux_cfg = TRUE;
			if ((scan_only) && (i == 1) && (safe_stricmp(psz_dirname, efi_dirname) == 0))
				iso_report.has_efi_syslinux = TRUE;
		}
	}

	// Check for a syslinux v5.0+ file anywhere
	if (safe_stricmp(psz_basename, ldlinux_c32) == 0) {
		has_ldlinux_c32 = TRUE;
	}

	// Check for an old incompatible c32 file anywhere
	for (i=0; i<NB_OLD_C32; i++) {
		is_old_c32[i] = FALSE;
		if ((safe_stricmp(psz_basename, old_c32_name[i]) == 0) && (i_file_length <= old_c32_threshold[i]))
			is_old_c32[i] = TRUE;
	}

	if (scan_only) {
		// Check for various files in root (psz_dirname = "")
		if (*psz_dirname == 0) {
			if (safe_strnicmp(psz_basename, bootmgr_efi_name, safe_strlen(bootmgr_efi_name)-5) == 0) {
				iso_report.has_bootmgr = TRUE;
			}
			if (safe_stricmp(psz_basename, grldr_name) == 0) {
				iso_report.has_grub4dos = TRUE;
			}
			if (safe_stricmp(psz_basename, kolibri_name) == 0) {
				iso_report.has_kolibrios = TRUE;
			}
			if (safe_stricmp(psz_basename, bootmgr_efi_name) == 0) {
				iso_report.has_win7_efi = TRUE;
			}
		}

		// Check for ReactOS' setupldr.sys anywhere
		if ((iso_report.reactos_path[0] == 0) && (safe_stricmp(psz_basename, reactos_name) == 0))
			safe_strcpy(iso_report.reactos_path, sizeof(iso_report.reactos_path), psz_fullpath);

		// Check for the EFI boot directory
		if (safe_stricmp(psz_dirname, efi_dirname) == 0)
			iso_report.has_efi = TRUE;

		// Check for the Grub boot directory
		// TODO: If there is a need to point to a different config file, as we do with Syslinux,
		// see http://www.gnu.org/software/grub/manual/grub.html#Embedded-configuration
		// However, this allegedly requires normal.mod (105 KB!) to be in core.img...
		if (safe_stricmp(psz_dirname, grub_dirname) == 0)
			iso_report.has_grub2 = TRUE;

		// Check for PE (XP) specific files in "/i386" or "/minint"
		for (i=0; i<ARRAYSIZE(pe_dirname); i++)
			if (safe_stricmp(psz_dirname, pe_dirname[i]) == 0)
				for (j=0; j<ARRAYSIZE(pe_file); j++)
					if (safe_stricmp(psz_basename, pe_file[j]) == 0)
						iso_report.winpe |= (1<<i)<<(ARRAYSIZE(pe_dirname)*j);

		if (*is_syslinux_cfg) {
			// Maintain a list of all the isolinux/syslinux configs identified so far
			StrArrayAdd(&config_path, psz_fullpath);
		}
		if (safe_stricmp(psz_basename, isolinux_bin) == 0) {
			// Maintain a list of all the isolinux.bin files found
			StrArrayAdd(&isolinux_path, psz_fullpath);
		}

		for (i=0; i<NB_OLD_C32; i++) {
			if (is_old_c32[i])
				iso_report.has_old_c32[i] = TRUE;
		}
		if (i_file_length >= FOUR_GIGABYTES)
			iso_report.has_4GB_file = TRUE;
		// Compute projected size needed
		total_blocks += i_file_length/UDF_BLOCKSIZE;
		// NB: ISO_BLOCKSIZE = UDF_BLOCKSIZE
		if ((i_file_length != 0) && (i_file_length%ISO_BLOCKSIZE == 0))	// 
			total_blocks++;
		return TRUE;
	}
	// In case there's an ldlinux.sys on the ISO, prevent it from overwriting ours
	if ((*psz_dirname == 0) && (safe_strcmp(psz_basename, ldlinux_name) == 0)) {
		uprintf("skipping % file from ISO image\n", ldlinux_name);
		return TRUE;
	}
	return FALSE;
}

static void fix_syslinux(const char* psz_fullpath, const char* psz_path, const char* psz_basename)
{
	size_t i, nul_pos;
	char *iso_label, *usb_label, *src, *dst;

	nul_pos = safe_strlen(psz_fullpath);
	src = safe_strdup(psz_fullpath);
	if (src == NULL)
		return;
	for (i=0; i<nul_pos; i++)
		if (src[i] == '/') src[i] = '\\';

	// Workaround for isolinux config files requiring an ISO label for kernel
	// append that may be different from our USB label. Oh, and these labels
	// must have spaces converted to \x20.
	iso_label = replace_char(iso_report.label, ' ', "\\x20");
	usb_label = replace_char(iso_report.usb_label, ' ', "\\x20");
	if (replace_in_token_data(src, "append", iso_label, usb_label, TRUE) != NULL)
		uprintf("  Patched %s: '%s' -> '%s'\n", src, iso_label, usb_label);
	free(iso_label);
	free(usb_label);
	// Fix dual BIOS + EFI support for tails and other ISOs
	if ( (safe_stricmp(psz_path, efi_dirname) == 0) && (safe_stricmp(psz_basename, syslinux_cfg[0]) == 0) &&
			(!iso_report.has_efi_syslinux) && (dst = safe_strdup(src)) ) {
		dst[nul_pos-12] = 's'; dst[nul_pos-11] = 'y'; dst[nul_pos-10] = 's';
		CopyFileA(src, dst, TRUE);
		uprintf("Duplicated %s to %s\n", src, dst);
		free(dst);
	}
	free(src);
}

static void print_extracted_file(char* psz_fullpath, int64_t i_file_length)
{
	size_t i, nul_pos;

	// Replace slashes with backslashes and append the size to the path for UI display
	nul_pos = safe_strlen(psz_fullpath);
	for (i=0; i<nul_pos; i++)
		if (psz_fullpath[i] == '/') psz_fullpath[i] = '\\';
	safe_sprintf(&psz_fullpath[nul_pos], 24, " (%s)", SizeToHumanReadable(i_file_length, TRUE, FALSE));
	uprintf("Extracting: %s\n", psz_fullpath);
	safe_sprintf(&psz_fullpath[nul_pos], 24, " (%s)", SizeToHumanReadable(i_file_length, FALSE, FALSE));
	SetWindowTextU(hISOFileName, psz_fullpath);
	// ISO9660 cannot handle backslashes
	for (i=0; i<nul_pos; i++)
		if (psz_fullpath[i] == '\\') psz_fullpath[i] = '/';
	// Remove the appended size for extraction
	psz_fullpath[nul_pos] = 0;
}

// Returns 0 on success, nonzero on error
static int udf_extract_files(udf_t *p_udf, udf_dirent_t *p_udf_dirent, const char *psz_path)
{
	HANDLE file_handle = NULL;
	DWORD buf_size, wr_size, err;
	BOOL r, is_syslinux_cfg, is_old_c32[NB_OLD_C32], is_identical;
	int i_length;
	size_t i;
	char tmp[128], *psz_fullpath = NULL, *psz_sanpath = NULL;
	const char* psz_basename;
	udf_dirent_t *p_udf_dirent2;
	uint8_t buf[UDF_BLOCKSIZE];
	int64_t i_read, i_file_length;

	if ((p_udf_dirent == NULL) || (psz_path == NULL))
		return 1;

	while ((p_udf_dirent = udf_readdir(p_udf_dirent)) != NULL) {
		if (FormatStatus) goto out;
		psz_basename = udf_get_filename(p_udf_dirent);
		if (strlen(psz_basename) == 0)
			continue;
		i_length = (int)(3 + strlen(psz_path) + strlen(psz_basename) + strlen(psz_extract_dir) + 24);
		psz_fullpath = (char*)calloc(sizeof(char), i_length);
		if (psz_fullpath == NULL) {
			uprintf("Error allocating file name\n");
			goto out;
		}
		i_length = _snprintf(psz_fullpath, i_length, "%s%s/%s", psz_extract_dir, psz_path, psz_basename);
		if (i_length < 0) {
			goto out;
		}
		if (udf_is_dir(p_udf_dirent)) {
			if (!scan_only) {
				psz_sanpath = sanitize_filename(psz_fullpath, &is_identical);
				IGNORE_RETVAL(_mkdirU(psz_sanpath));
				safe_free(psz_sanpath);
			}
			p_udf_dirent2 = udf_opendir(p_udf_dirent);
			if (p_udf_dirent2 != NULL) {
				if (udf_extract_files(p_udf, p_udf_dirent2, &psz_fullpath[strlen(psz_extract_dir)]))
					goto out;
			}
		} else {
			i_file_length = udf_get_file_length(p_udf_dirent);
			if (check_iso_props(psz_path, &is_syslinux_cfg, is_old_c32, i_file_length, psz_basename, psz_fullpath)) {
				safe_free(psz_fullpath);
				continue;
			}
			print_extracted_file(psz_fullpath, i_file_length);
			for (i=0; i<NB_OLD_C32; i++) {
				if (is_old_c32[i] && use_own_c32[i]) {
					static_sprintf(tmp, "%s/syslinux-%s/%s", FILES_DIR, embedded_sl_version_str[0], old_c32_name[i]);
					if (CopyFileA(tmp, psz_fullpath, FALSE)) {
						uprintf("  Replaced with local version\n");
						break;
					}
					uprintf("  Could not replace file: %s\n", WindowsErrorString());
				}
			}
			if (i < NB_OLD_C32)
				continue;
			psz_sanpath = sanitize_filename(psz_fullpath, &is_identical);
			if (!is_identical)
				uprintf("  File name sanitized to '%s'\n", psz_sanpath);
			file_handle = CreateFileU(psz_sanpath, GENERIC_READ | GENERIC_WRITE,
				FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			if (file_handle == INVALID_HANDLE_VALUE) {
				err = GetLastError();
				uprintf("  Unable to create file: %s\n", WindowsErrorString());
				if ((err == ERROR_ACCESS_DENIED) && (safe_strcmp(&psz_sanpath[3], autorun_name) == 0))
					uprintf(stupid_antivirus);
				else
					goto out;
			} else while (i_file_length > 0) {
				if (FormatStatus) goto out;
				memset(buf, 0, UDF_BLOCKSIZE);
				i_read = udf_read_block(p_udf_dirent, buf, 1);
				if (i_read < 0) {
					uprintf("  Error reading UDF file %s\n", &psz_fullpath[strlen(psz_extract_dir)]);
					goto out;
				}
				buf_size = (DWORD)MIN(i_file_length, i_read);
				for (i=0; i<WRITE_RETRIES; i++) {
					ISO_BLOCKING(r = WriteFile(file_handle, buf, buf_size, &wr_size, NULL));
					if ((!r) || (buf_size != wr_size)) {
						uprintf("  Error writing file: %s", WindowsErrorString());
						if (i < WRITE_RETRIES-1)
							uprintf("  RETRYING...\n");
					} else {
						break;
					}
				}
				if (i >= WRITE_RETRIES) goto out;
				i_file_length -= i_read;
				if (nb_blocks++ % PROGRESS_THRESHOLD == 0) {
					SendMessage(hISOProgressBar, PBM_SETPOS, (WPARAM)((MAX_PROGRESS*nb_blocks)/total_blocks), 0);
					UpdateProgress(OP_DOS, 100.0f*nb_blocks/total_blocks);
				}
			}
			// If you have a fast USB 3.0 device, the default Windows buffering does an
			// excellent job at compensating for our small blocks read/writes to max out the
			// device's bandwidth.
			// The drawback however is with cancellation. With a large file, CloseHandle()
			// may take forever to complete and is not interruptible. We try to detect this.
			ISO_BLOCKING(safe_closehandle(file_handle));
			if (is_syslinux_cfg)
				fix_syslinux(psz_sanpath, psz_path, psz_basename);
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
	BOOL s, is_syslinux_cfg, is_old_c32[NB_OLD_C32], is_symlink, is_identical;
	int i_length, r = 1;
	char tmp[128], psz_fullpath[MAX_PATH], *psz_basename, *psz_sanpath;
	const char *psz_iso_name = &psz_fullpath[strlen(psz_extract_dir)];
	unsigned char buf[ISO_BLOCKSIZE];
	CdioListNode_t* p_entnode;
	iso9660_stat_t *p_statbuf;
	CdioList_t* p_entlist;
	size_t i, j;
	lsn_t lsn;
	int64_t i_file_length;

	if ((p_iso == NULL) || (psz_path == NULL))
		return 1;

	i_length = _snprintf(psz_fullpath, sizeof(psz_fullpath), "%s%s/", psz_extract_dir, psz_path);
	if (i_length < 0)
		return 1;
	psz_basename = &psz_fullpath[i_length];

	p_entlist = iso9660_ifs_readdir(p_iso, psz_path);
	if (!p_entlist) {
		uprintf("Could not access directory %s\n", psz_path);
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
			safe_strcpy(psz_basename, sizeof(psz_fullpath)-i_length-1, p_statbuf->filename);
			if (safe_strlen(p_statbuf->filename) > 64)
				iso_report.has_long_filename = TRUE;
			// libcdio has a memleak for Rock Ridge symlinks. It doesn't look like there's an easy fix there as
			// a generic list that's unaware of RR extensions is being used, so we prevent that memleak ourselves
			is_symlink = (p_statbuf->rr.psz_symlink != NULL);
			if (is_symlink)
				iso_report.has_symlinks = TRUE;
			if (scan_only)
				safe_free(p_statbuf->rr.psz_symlink);
		} else {
			iso9660_name_translate_ext(p_statbuf->filename, psz_basename, i_joliet_level);
		}
		if (p_statbuf->type == _STAT_DIR) {
			if (!scan_only) {
				psz_sanpath = sanitize_filename(psz_fullpath, &is_identical);
				IGNORE_RETVAL(_mkdirU(psz_sanpath));
				safe_free(psz_sanpath);
			}
			if (iso_extract_files(p_iso, psz_iso_name))
				goto out;
		} else {
			i_file_length = p_statbuf->size;
			if (check_iso_props(psz_path, &is_syslinux_cfg, is_old_c32, i_file_length, psz_basename, psz_fullpath)) {
				continue;
			}
			print_extracted_file(psz_fullpath, i_file_length);
			for (i=0; i<NB_OLD_C32; i++) {
				if (is_old_c32[i] && use_own_c32[i]) {
					static_sprintf(tmp, "%s/syslinux-%s/%s", FILES_DIR, embedded_sl_version_str[0], old_c32_name[i]);
					if (CopyFileA(tmp, psz_fullpath, FALSE)) {
						uprintf("  Replaced with local version\n");
						break;
					}
					uprintf("  Could not replace file: %s\n", WindowsErrorString());
				}
			}
			if (i < NB_OLD_C32)
				continue;
			psz_sanpath = sanitize_filename(psz_fullpath, &is_identical);
			if (!is_identical)
				uprintf("  File name sanitized to '%s'\n", psz_sanpath);
			if (is_symlink) {
				if (i_file_length == 0)
					uprintf("  Ignoring Rock Ridge symbolic link to '%s'\n", p_statbuf->rr.psz_symlink);
				safe_free(p_statbuf->rr.psz_symlink);
			}
			file_handle = CreateFileU(psz_sanpath, GENERIC_READ | GENERIC_WRITE,
				FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			if (file_handle == INVALID_HANDLE_VALUE) {
				err = GetLastError();
				uprintf("  Unable to create file: %s\n", WindowsErrorString());
				if ((err == ERROR_ACCESS_DENIED) && (safe_strcmp(&psz_sanpath[3], autorun_name) == 0))
					uprintf(stupid_antivirus);
				else
					goto out;
			} else for (i=0; i_file_length>0; i++) {
				if (FormatStatus) goto out;
				memset(buf, 0, ISO_BLOCKSIZE);
				lsn = p_statbuf->lsn + (lsn_t)i;
				if (iso9660_iso_seek_read(p_iso, buf, lsn, 1) != ISO_BLOCKSIZE) {
					uprintf("  Error reading ISO9660 file %s at LSN %lu\n",
						psz_iso_name, (long unsigned int)lsn);
					goto out;
				}
				buf_size = (DWORD)MIN(i_file_length, ISO_BLOCKSIZE);
				for (j=0; j<WRITE_RETRIES; j++) {
					ISO_BLOCKING(s = WriteFile(file_handle, buf, buf_size, &wr_size, NULL));
					if ((!s) || (buf_size != wr_size)) {
						uprintf("  Error writing file: %s", WindowsErrorString());
						if (j < WRITE_RETRIES-1)
							uprintf("  RETRYING...\n");
					} else {
						break;
					}
				}
				if (j >= WRITE_RETRIES) goto out;
				i_file_length -= ISO_BLOCKSIZE;
				if (nb_blocks++ % PROGRESS_THRESHOLD == 0) {
					SendMessage(hISOProgressBar, PBM_SETPOS, (WPARAM)((MAX_PROGRESS*nb_blocks)/total_blocks), 0);
					UpdateProgress(OP_DOS, 100.0f*nb_blocks/total_blocks);
				}
			}
			ISO_BLOCKING(safe_closehandle(file_handle));
			if (is_syslinux_cfg)
				fix_syslinux(psz_sanpath, psz_path, psz_basename);
			safe_free(psz_sanpath);
		}
	}
	r = 0;

out:
	ISO_BLOCKING(safe_closehandle(file_handle));
	_cdio_list_free(p_entlist, true);
	return r;
}

BOOL ExtractISO(const char* src_iso, const char* dest_dir, BOOL scan)
{
	size_t i, size;
	int j;
	uint16_t sl_version;
	FILE* fd;
	int r = 1;
	iso9660_t* p_iso = NULL;
	udf_t* p_udf = NULL; 
	udf_dirent_t* p_udf_root;
	LONG progress_style;
	char *tmp, *buf, *ext;
	char path[MAX_PATH];
	const char* basedir[] = { "i386", "minint" };
	const char* tmp_sif = ".\\txtsetup.sif~";
	iso_extension_mask_t iso_extension_mask = ISO_EXTENSION_ALL;

	if ((!enable_iso) || (src_iso == NULL) || (dest_dir == NULL))
		return FALSE;

	scan_only = scan;
	cdio_log_set_handler(log_handler);
	psz_extract_dir = dest_dir;
	progress_style = GetWindowLong(hISOProgressBar, GWL_STYLE);
	if (scan_only) {
		total_blocks = 0;
		memset(&iso_report, 0, sizeof(iso_report));
		has_ldlinux_c32 = FALSE;
		// String array of all isolinux/syslinux locations
		StrArrayCreate(&config_path, 8);
		StrArrayCreate(&isolinux_path, 8);
		// Change the Window title and static text
		SetWindowTextU(hISOProgressDlg, lmprintf(MSG_202));
		SetWindowTextU(hISOFileName, lmprintf(MSG_202));
		// Change progress style to marquee for scanning
		SetWindowLong(hISOProgressBar, GWL_STYLE, progress_style | PBS_MARQUEE);
		SendMessage(hISOProgressBar, PBM_SETMARQUEE, TRUE, 0);
	} else {
		uprintf("Extracting files...\n");
		IGNORE_RETVAL(_chdirU(app_dir));
		SetWindowTextU(hISOProgressDlg, lmprintf(MSG_231));
		if (total_blocks == 0) {
			uprintf("Error: ISO has not been properly scanned.\n");
			FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_ISO_SCAN);
			goto out;
		}
		nb_blocks = 0;
		iso_blocking_status = 0;
		SetWindowLong(hISOProgressBar, GWL_STYLE, progress_style & (~PBS_MARQUEE));
		SendMessage(hISOProgressBar, PBM_SETPOS, 0, 0);
	}
	SendMessage(hISOProgressDlg, UM_PROGRESS_INIT, 0, 0);

	/* First try to open as UDF - fallback to ISO if it failed */
	p_udf = udf_open(src_iso);
	if (p_udf == NULL)
		goto try_iso;
	uprintf("Disc image is an UDF image\n");

	p_udf_root = udf_get_root(p_udf, true, 0);
	if (p_udf_root == NULL) {
		uprintf("Couldn't locate UDF root directory\n");
		goto out;
	}
	if (scan_only) {
		if (udf_get_logical_volume_id(p_udf, iso_report.label, sizeof(iso_report.label)) <= 0)
			iso_report.label[0] = 0;
	}
	r = udf_extract_files(p_udf, p_udf_root, "");
	goto out;

try_iso:
	// Perform our first scan with Joliet disabled (if Rock Ridge is enabled), so that we can find if
	// there exists a Rock Ridge file with a name > 64 chars or if there are symlinks. If that is the
	// case then we also disable Joliet during the extract phase.
	if ((!enable_joliet) || (enable_rockridge && (scan_only || iso_report.has_long_filename || iso_report.has_symlinks))) {
		iso_extension_mask &= ~ISO_EXTENSION_JOLIET;
	}
	if (!enable_rockridge) {
		iso_extension_mask &= ~ISO_EXTENSION_ROCK_RIDGE;
	}

	p_iso = iso9660_open_ext(src_iso, iso_extension_mask);
	if (p_iso == NULL) {
		uprintf("Unable to open '%s' as an ISO image.\n", src_iso);
		r = 1;
		goto out;
	}
	uprintf("Disc image is an ISO9660 image\n");
	i_joliet_level = iso9660_ifs_get_joliet_level(p_iso);
	if (scan_only) {
		if (iso9660_ifs_get_volume_id(p_iso, &tmp)) {
			safe_strcpy(iso_report.label, sizeof(iso_report.label), tmp);
			safe_free(tmp);
		} else
			iso_report.label[0] = 0;
	} else {
		if (iso_extension_mask & (ISO_EXTENSION_JOLIET|ISO_EXTENSION_ROCK_RIDGE))
			uprintf("This image will be extracted using %s extensions (if present)", 
				(iso_extension_mask & ISO_EXTENSION_JOLIET)?"Joliet":"Rock Ridge");
		else
			uprintf("This image will not be extracted using any ISO extensions");
	}
	r = iso_extract_files(p_iso, "");

out:
	iso_blocking_status = -1;
	if (scan_only) {
		// Remove trailing spaces from the label
		for (j=(int)safe_strlen(iso_report.label)-1; ((j>=0)&&(isspaceU(iso_report.label[j]))); j--)
			iso_report.label[j] = 0;
		// We use the fact that UDF_BLOCKSIZE and ISO_BLOCKSIZE are the same here
		iso_report.projected_size = total_blocks * ISO_BLOCKSIZE;
		// We will link the existing isolinux.cfg from a syslinux.cfg we create
		// If multiple config files exist, choose the one with the shortest path
		// (so that a '/syslinux.cfg' is preferred over a '/isolinux/isolinux.cfg')
		if (!IsStrArrayEmpty(config_path)) {
			safe_strcpy(iso_report.cfg_path, sizeof(iso_report.cfg_path), config_path.String[0]);
			for (i=1; i<config_path.Index; i++) {
				// Tails uses an '/EFI/BOOT/isolinux.cfg' along with a '/isolinux/isolinux.cfg'
				// which are the exact same length. However, only the /isolinux one will work,
				// so for now, at equal length, always pick the latest.
				// We may have to revisit this and prefer a path that contains '/isolinux' if
				// this hack is not enough for other images.
				if (safe_strlen(iso_report.cfg_path) >= safe_strlen(config_path.String[i]))
					safe_strcpy(iso_report.cfg_path, sizeof(iso_report.cfg_path), config_path.String[i]);
			}
			uprintf("Will use %s for Syslinux\n", iso_report.cfg_path);
			// Extract all of the isolinux.bin files we found to identify their versions
			for (i=0; i<isolinux_path.Index; i++) {
				size = (size_t)ExtractISOFile(src_iso, isolinux_path.String[i], dot_isolinux_bin, FILE_ATTRIBUTE_NORMAL);
				if (size == 0) {
					uprintf("Could not access %s\n", isolinux_path.String[i]);
				} else {
					buf = (char*)calloc(size, 1);
					if (buf == NULL) break;
					fd = fopen(dot_isolinux_bin, "rb");
					if (fd == NULL) {
						free(buf);
						continue;
					}
					fread(buf, 1, size, fd);
					fclose(fd);
					sl_version = GetSyslinuxVersion(buf, size, &ext);
					if (iso_report.sl_version == 0) {
						safe_strcpy(iso_report.sl_version_ext, sizeof(iso_report.sl_version_ext), ext);
						iso_report.sl_version = sl_version;
						j = (int)i;
					} else if ((iso_report.sl_version != sl_version) || (safe_strcmp(iso_report.sl_version_ext, ext) != 0)) {
						uprintf("Found conflicting %s versions:\n  '%s' (%d.%02d%s) vs '%s' (%d.%02d%s)\n", isolinux_bin,
							isolinux_path.String[j], SL_MAJOR(iso_report.sl_version), SL_MINOR(iso_report.sl_version),
							iso_report.sl_version_ext, isolinux_path.String[i], SL_MAJOR(sl_version), SL_MINOR(sl_version), ext);
					}
					free(buf);
					_unlink(dot_isolinux_bin);
				}
			}
			if (iso_report.sl_version != 0) {
				static_sprintf(iso_report.sl_version_str, "%d.%02d",
					SL_MAJOR(iso_report.sl_version), SL_MINOR(iso_report.sl_version));
				uprintf("Detected Isolinux version: %s%s (from '%s')",
					iso_report.sl_version_str, iso_report.sl_version_ext, isolinux_path.String[j]);
				if ( (has_ldlinux_c32 && (SL_MAJOR(iso_report.sl_version) < 5))
				  || (!has_ldlinux_c32 && (SL_MAJOR(iso_report.sl_version) >= 5)) )
					uprintf("Warning: Conflict between Isolinux version and the presence of ldlinux.c32...\n");
			} else {
				// Couldn't find a version from isolinux.bin. Force set to the versions we embed
				iso_report.sl_version = embedded_sl_version[has_ldlinux_c32?1:0];
				static_sprintf(iso_report.sl_version_str, "%d.%02d",
					SL_MAJOR(iso_report.sl_version), SL_MINOR(iso_report.sl_version));
				uprintf("Warning: Could not detect Isolinux version - Forcing to %s (embedded)",
					iso_report.sl_version_str);
			}
		}
		if (IS_WINPE(iso_report.winpe)) {
			// In case we have a WinPE 1.x based iso, we extract and parse txtsetup.sif
			// during scan, to see if /minint was provided for OsLoadOptions, as it decides
			// whether we should use 0x80 or 0x81 as the disk ID in the MBR
			safe_sprintf(path, sizeof(path), "/%s/txtsetup.sif", 
				basedir[((iso_report.winpe&WINPE_I386) == WINPE_I386)?0:1]);
			ExtractISOFile(src_iso, path, tmp_sif, FILE_ATTRIBUTE_NORMAL);
			tmp = get_token_data_file("OsLoadOptions", tmp_sif);
			if (tmp != NULL) {
				for (i=0; i<strlen(tmp); i++)
					tmp[i] = (char)tolower(tmp[i]);
				uprintf("Checking txtsetup.sif:\n  OsLoadOptions = %s\n", tmp);
				iso_report.uses_minint = (strstr(tmp, "/minint") != NULL);
			}
			_unlink(tmp_sif);
			safe_free(tmp);
		}
		StrArrayDestroy(&config_path);
		StrArrayDestroy(&isolinux_path);
	} else if (HAS_SYSLINUX(iso_report)) {
		safe_sprintf(path, sizeof(path), "%s\\syslinux.cfg", dest_dir);
		// Create a /syslinux.cfg (if none exists) that points to the existing isolinux cfg
		fd = fopen(path, "r");
		if (fd == NULL) {
			fd = fopen(path, "w");	// No "/syslinux.cfg" => create a new one
			if (fd == NULL) {
				uprintf("Unable to create %s - booting from USB will not work\n", path);
				r = 1;
			} else {
				fprintf(fd, "DEFAULT loadconfig\n\nLABEL loadconfig\n  CONFIG %s\n", iso_report.cfg_path);
				for (i=safe_strlen(iso_report.cfg_path); (i>0)&&(iso_report.cfg_path[i]!='/'); i--);
				if (i>0) {
					iso_report.cfg_path[i] = 0;
					fprintf(fd, "  APPEND %s/\n", iso_report.cfg_path);
					iso_report.cfg_path[i] = '/';
				}
				uprintf("Created: %s\n", path);
			}
		}
		if (fd != NULL)
			fclose(fd);
	}
	SendMessage(hISOProgressDlg, UM_PROGRESS_EXIT, 0, 0);
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
	size_t i;
	ssize_t read_size;
	int64_t file_length, r = 0;
	char buf[UDF_BLOCKSIZE];
	DWORD buf_size, wr_size;
	BOOL s;
	iso9660_t* p_iso = NULL;
	udf_t* p_udf = NULL; 
	udf_dirent_t *p_udf_root = NULL, *p_udf_file = NULL;
	iso9660_stat_t *p_statbuf = NULL;
	lsn_t lsn;
	HANDLE file_handle = INVALID_HANDLE_VALUE;

	file_handle = CreateFileU(dest_file, GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, attributes, NULL);
	if (file_handle == INVALID_HANDLE_VALUE) {
		uprintf("  Unable to create file %s: %s\n", dest_file, WindowsErrorString());
		goto out;
	}

	/* First try to open as UDF - fallback to ISO if it failed */
	p_udf = udf_open(iso);
	if (p_udf == NULL)
		goto try_iso;

	p_udf_root = udf_get_root(p_udf, true, 0);
	if (p_udf_root == NULL) {
		uprintf("Couldn't locate UDF root directory\n");
		goto out;
	}
	p_udf_file = udf_fopen(p_udf_root, iso_file);
	if (!p_udf_file) {
		uprintf("Couldn't locate file %s in ISO image\n", iso_file);
		goto out;
	}
	file_length = udf_get_file_length(p_udf_file);
	while (file_length > 0) {
		memset(buf, 0, UDF_BLOCKSIZE);
		read_size = udf_read_block(p_udf_file, buf, 1);
		if (read_size < 0) {
			uprintf("Error reading UDF file %s\n", iso_file);
			goto out;
		}
		buf_size = (DWORD)MIN(file_length, read_size);
		s = WriteFile(file_handle, buf, buf_size, &wr_size, NULL);
		if ((!s) || (buf_size != wr_size)) {
			uprintf("  Error writing file %s: %s\n", dest_file, WindowsErrorString());
			goto out;
		}
		file_length -= read_size;
		r += read_size;
	}
	goto out;

try_iso:
	p_iso = iso9660_open(iso);
	if (p_iso == NULL) {
		uprintf("Unable to open image '%s'.\n", iso);
		goto out;
	}

	p_statbuf = iso9660_ifs_stat_translate(p_iso, iso_file);
	if (p_statbuf == NULL) {
		uprintf("Could not get ISO-9660 file information for file %s\n", iso_file);
		goto out;
	}

	file_length = p_statbuf->size;
	for (i = 0; file_length > 0; i++) {
		memset(buf, 0, ISO_BLOCKSIZE);
		lsn = p_statbuf->lsn + (lsn_t)i;
		if (iso9660_iso_seek_read(p_iso, buf, lsn, 1) != ISO_BLOCKSIZE) {
			uprintf("  Error reading ISO9660 file %s at LSN %lu\n", iso_file, (long unsigned int)lsn);
			goto out;
		}
		buf_size = (DWORD)MIN(file_length, ISO_BLOCKSIZE);
		s = WriteFile(file_handle, buf, buf_size, &wr_size, NULL);
		if ((!s) || (buf_size != wr_size)) {
			uprintf("  Error writing file %s: %s\n", dest_file, WindowsErrorString());
			goto out;
		}
		file_length -= ISO_BLOCKSIZE;
		r += ISO_BLOCKSIZE;
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
