/*
 * Rufus: The Reliable USB Formatting Utility
 * ISO file extraction
 * Copyright (c) 2011-2012 Pete Batard <pete@akeo.ie>
 * Based on libcdio's iso & udf samples:
 * Copyright (c) 2003-2011 Rocky Bernstein <rocky@gnu.org>
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

#include <cdio/cdio.h>
#include <cdio/logging.h>
#include <cdio/iso9660.h>
#include <cdio/udf.h>

#include "rufus.h"
#include "msapi_utf8.h"
#include "resource.h"

#ifndef MIN
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef PBS_MARQUEE
#define PBS_MARQUEE 0x08
#endif
#ifndef PBM_SETMARQUEE
#define PBM_SETMARQUEE (WM_USER+10)
#endif
// How often should we update the progress bar (in 2K blocks) as updating
// the progress bar for every block will bring extraction to a crawl
#define PROGRESS_THRESHOLD        1024
#define FOUR_GIGABYTES            4294967296LL

// Needed for UDF ISO access
CdIo_t* cdio_open (const char *psz_source, driver_id_t driver_id) {return NULL;}
void cdio_destroy (CdIo_t *p_cdio) {}

RUFUS_ISO_REPORT iso_report;
int64_t iso_blocking_status = -1;
#define ISO_BLOCKING(x) do {x; iso_blocking_status++; } while(0)
static const char *psz_extract_dir;
static const char *isolinux_name = "isolinux", *bootmgr_name = "bootmgr";
static uint64_t total_blocks, nb_blocks;
static BOOL scan_only = FALSE;

// TODO: Timestamp & permissions preservation

// Convert a file size to human readable
static __inline char* size_to_hr(int64_t size)
{
	int suffix = 0;
	static char str_size[24];
	const char* sizes[] = { "", "KB", "MB", "GB", "TB" };
	double hr_size = (double)size;
	while ((suffix < ARRAYSIZE(sizes)) && (hr_size >= 1024.0)) {
		hr_size /= 1024.0;
		suffix++;
	}
	if (suffix == 0) {
		safe_sprintf(str_size, sizeof(str_size), " (%d bytes)", (int)hr_size);
	} else {
		safe_sprintf(str_size, sizeof(str_size), " (%0.1f %s)", hr_size, sizes[suffix]);
	}
	return str_size;
}

// Interruptible thread for handle closure on large files
DWORD WINAPI ISOCloseHandleThread(LPVOID param)
{
	CloseHandle((HANDLE)param);
	ExitThread(0);
}

// Returns 0 on success, nonzero on error
static int udf_extract_files(udf_t *p_udf, udf_dirent_t *p_udf_dirent, const char *psz_path)
{
	HANDLE file_handle = NULL;
	DWORD buf_size, wr_size;
	BOOL r;
	int i_length;
	size_t i, nul_pos;
	char* psz_fullpath;
	const char* psz_basename;
	udf_dirent_t *p_udf_dirent2;
	uint8_t buf[UDF_BLOCKSIZE];
	int64_t i_read, i_file_length;

	if ((p_udf_dirent == NULL) || (psz_path == NULL))
		return 1;

	while ((p_udf_dirent = udf_readdir(p_udf_dirent)) != NULL) {
		if (FormatStatus) goto out;
		psz_basename = udf_get_filename(p_udf_dirent);
		i_length = (int)(3 + strlen(psz_path) + strlen(psz_basename) + strlen(psz_extract_dir) + 24);
		psz_fullpath = (char*)calloc(sizeof(char), i_length);
		if (psz_fullpath == NULL) {
			uprintf("Error allocating file name\n");
			goto out;
		}
		i_length = safe_sprintf(psz_fullpath, i_length, "%s%s/%s", psz_extract_dir, psz_path, psz_basename);
		if (i_length < 0) {
			goto out;
		}
		if (udf_is_dir(p_udf_dirent)) {
			if (!scan_only) {
				_mkdir(psz_fullpath);
			} else {
				// Check for an "isolinux\" dir in root (psz_path = "")
				if ((*psz_path == 0) && (safe_strcmp(psz_basename, isolinux_name) == 0))
					iso_report.has_isolinux = TRUE;
			}
			p_udf_dirent2 = udf_opendir(p_udf_dirent);
			if (p_udf_dirent2 != NULL) {
				if (udf_extract_files(p_udf, p_udf_dirent2, &psz_fullpath[strlen(psz_extract_dir)]))
					goto out;
			}
		} else {
			i_file_length = udf_get_file_length(p_udf_dirent);
			if (scan_only) {
				// Check for a "bootmgr" file in root (psz_path = "")
				if ((*psz_path == 0) && (safe_strcmp(psz_basename, bootmgr_name) == 0))
					iso_report.has_bootmgr = TRUE;
				if (i_file_length >= FOUR_GIGABYTES)
					iso_report.has_4GB_file = TRUE;
				total_blocks += i_file_length/UDF_BLOCKSIZE;
				if ((i_file_length != 0) && (i_file_length%UDF_BLOCKSIZE == 0))
					total_blocks++;
				safe_free(psz_fullpath);
				continue;
			}
			// Replace slashes with backslashes and append the size to the path for UI display
			nul_pos = safe_strlen(psz_fullpath);
			for (i=0; i<nul_pos; i++) if (psz_fullpath[i] == '/') psz_fullpath[i] = '\\';
			safe_strcpy(&psz_fullpath[nul_pos], 24, size_to_hr(i_file_length));
			uprintf("Extracting: %s\n", psz_fullpath);
			SetWindowTextU(hISOFileName, psz_fullpath);
			// Remove the appended size for extraction
			psz_fullpath[nul_pos] = 0;
			file_handle = CreateFileU(psz_fullpath, GENERIC_READ | GENERIC_WRITE,
				FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			if (file_handle == INVALID_HANDLE_VALUE) {
				uprintf("  Unable to create file: %s\n", WindowsErrorString());
				goto out;
			}
			while (i_file_length > 0) {
				if (FormatStatus) goto out;
				memset(buf, 0, UDF_BLOCKSIZE);
				i_read = udf_read_block(p_udf_dirent, buf, 1);
				if (i_read < 0) {
					uprintf("  Error reading UDF file %s\n", &psz_fullpath[strlen(psz_extract_dir)]);
					goto out;
				}
				buf_size = (DWORD)MIN(i_file_length, i_read);
				ISO_BLOCKING(r = WriteFile(file_handle, buf, buf_size, &wr_size, NULL));
				if ((!r) || (buf_size != wr_size)) {
					uprintf("  Error writing file: %s\n", WindowsErrorString());
					goto out;
				}
				i_file_length -= i_read;
				if (nb_blocks++ % PROGRESS_THRESHOLD == 0) {
					SendMessage(hISOProgressBar, PBM_SETPOS, (WPARAM)((MAX_PROGRESS*nb_blocks)/total_blocks), 0);
				}
			}
			// If you have a fast USB 3.0 device, the default Windows buffering does an
			// excellent job at compensating for our small blocks read/writes to max out the
			// device's bandwidth.
			// The drawback however is with cancellation. With a large file, CloseHandle()
			// may take forever to complete and is not interruptible. We try to detect this.
			ISO_BLOCKING(safe_closehandle(file_handle));
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
	DWORD buf_size, wr_size;
	BOOL s;
	int i_length, r = 1;
	char psz_fullpath[1024], *psz_basename;
	const char *psz_iso_name = &psz_fullpath[strlen(psz_extract_dir)];
	unsigned char buf[ISO_BLOCKSIZE];
	CdioListNode_t* p_entnode;
	iso9660_stat_t *p_statbuf;
	CdioList_t* p_entlist;
	size_t i, nul_pos;
	lsn_t lsn;
	int64_t i_file_length;

	if ((p_iso == NULL) || (psz_path == NULL))
		return 1;

	i_length = safe_sprintf(psz_fullpath, sizeof(psz_fullpath), "%s%s/", psz_extract_dir, psz_path);
	if (i_length < 0)
		return 1;
	psz_basename = &psz_fullpath[i_length];

	p_entlist = iso9660_ifs_readdir(p_iso, psz_path);
	if (!p_entlist)
		return 1;

	_CDIO_LIST_FOREACH (p_entnode, p_entlist) {
		if (FormatStatus) goto out;
		p_statbuf = (iso9660_stat_t*) _cdio_list_node_data(p_entnode);
		/* Eliminate . and .. entries */
		if ( (strcmp(p_statbuf->filename, ".") == 0)
			|| (strcmp(p_statbuf->filename, "..") == 0) )
			continue;
		iso9660_name_translate(p_statbuf->filename, psz_basename);
		if (p_statbuf->type == _STAT_DIR) {
			if (!scan_only) {
				_mkdir(psz_fullpath);
			} else {
				// Check for an "isolinux\" dir in root (psz_path = "")
				if ((*psz_path == 0) && (safe_strcmp(psz_basename, isolinux_name) == 0))
					iso_report.has_isolinux = TRUE;
			}
			if (iso_extract_files(p_iso, psz_iso_name))
				goto out;
		} else {
			i_file_length = p_statbuf->size;
			if (scan_only) {
				// Check for a "bootmgr" file in root (psz_path = "")
				if ((*psz_path == 0) && (safe_strcmp(psz_basename, bootmgr_name) == 0))
					iso_report.has_bootmgr = TRUE;
				if (i_file_length >= FOUR_GIGABYTES)
					iso_report.has_4GB_file = TRUE;
				total_blocks += i_file_length/ISO_BLOCKSIZE;
				if ((i_file_length != 0) && (i_file_length%ISO_BLOCKSIZE == 0))
					total_blocks++;
				continue;
			}
			// Replace slashes with backslashes and append the size to the path for UI display
			nul_pos = safe_strlen(psz_fullpath);
			for (i=0; i<nul_pos; i++) if (psz_fullpath[i] == '/') psz_fullpath[i] = '\\';
			safe_strcpy(&psz_fullpath[nul_pos], 24, size_to_hr(i_file_length));
			uprintf("Extracting: %s\n", psz_fullpath);
			SetWindowTextU(hISOFileName, psz_fullpath);
			// ISO9660 cannot handle backslashes
			for (i=0; i<nul_pos; i++) if (psz_fullpath[i] == '\\') psz_fullpath[i] = '/';
			psz_fullpath[nul_pos] = 0;
			file_handle = CreateFileU(psz_fullpath, GENERIC_READ | GENERIC_WRITE,
				FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			if (file_handle == INVALID_HANDLE_VALUE) {
				uprintf("  Unable to create file: %s\n", WindowsErrorString());
				goto out;
			}
			for (i = 0; i_file_length > 0; i++) {
				if (FormatStatus) goto out;
				memset(buf, 0, ISO_BLOCKSIZE);
				lsn = p_statbuf->lsn + (lsn_t)i;
				if (iso9660_iso_seek_read(p_iso, buf, lsn, 1) != ISO_BLOCKSIZE) {
					uprintf("  Error reading ISO9660 file %s at LSN %lu\n",
						psz_iso_name, (long unsigned int)lsn);
					goto out;
				}
				buf_size = (DWORD)MIN(i_file_length, ISO_BLOCKSIZE);
				ISO_BLOCKING(s = WriteFile(file_handle, buf, buf_size, &wr_size, NULL));
				if ((!s) || (buf_size != wr_size)) {
					uprintf("  Error writing file: %s\n", WindowsErrorString());
					goto out;
				}
				i_file_length -= ISO_BLOCKSIZE;
				if (nb_blocks++ % PROGRESS_THRESHOLD == 0) {
					SendMessage(hISOProgressBar, PBM_SETPOS, (WPARAM)((MAX_PROGRESS*nb_blocks)/total_blocks), 0);
				}
			}
			ISO_BLOCKING(safe_closehandle(file_handle));
		}
	}
	r = 0;

out:
	ISO_BLOCKING(safe_closehandle(file_handle));
	_cdio_list_free(p_entlist, true);
	return r;
}

BOOL ExtractISO(const char* src_iso, const char* dest_dir, bool scan)
{
	BOOL r = FALSE;
	iso9660_t* p_iso = NULL;
	udf_t* p_udf = NULL; 
	udf_dirent_t* p_udf_root;
	LONG progress_style;
	const char* scan_text = "Scanning ISO image...\n";

	if ((src_iso == NULL) || (dest_dir == NULL))
		return FALSE;

	scan_only = scan;
	cdio_loglevel_default = CDIO_LOG_DEBUG;
	psz_extract_dir = dest_dir;
	progress_style = GetWindowLong(hISOProgressBar, GWL_STYLE);
	if (scan_only) {
		total_blocks = 0;
		iso_report.projected_size = 0;
		iso_report.has_4GB_file = FALSE;
		iso_report.has_bootmgr = FALSE;
		iso_report.has_isolinux = FALSE;
		// Change the Window title and static text
		SetWindowTextU(hISOProgressDlg, scan_text);
		SetWindowTextU(hISOFileName, scan_text);
		// Change progress style to marquee for scanning
		SetWindowLong(hISOProgressBar, GWL_STYLE, progress_style | PBS_MARQUEE);
		SendMessage(hISOProgressBar, PBM_SETMARQUEE, TRUE, 0);
	} else {
		uprintf("Extracting files...\n");
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
	ShowWindow(hISOProgressDlg, SW_SHOW);
	UpdateWindow(hISOProgressDlg);

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
	r = udf_extract_files(p_udf, p_udf_root, "");
	goto out;

try_iso:
	p_iso = iso9660_open(src_iso);
	if (p_iso == NULL) {
		uprintf("Unable to open image '%s'.\n", src_iso);
		goto out;
	}
	uprintf("Disc image is an ISO9660 image\n");
	r = iso_extract_files(p_iso, "");

out:
	iso_blocking_status = -1;
	if (scan_only) {
		// We use the fact that UDF_BLOCKSIZE and ISO_BLOCKSIZE are the same here
		iso_report.projected_size = total_blocks * ISO_BLOCKSIZE;
	}
	SendMessage(hISOProgressDlg, UM_ISO_EXIT, 0, 0);
	if (p_iso != NULL)
		iso9660_close(p_iso);
	if (p_udf != NULL)
		udf_close(p_udf);
	if ((r != 0) && (FormatStatus == 0))
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR((scan_only?ERROR_ISO_SCAN:ERROR_ISO_EXTRACT));
	return (r == 0);
}
