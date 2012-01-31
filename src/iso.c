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

#ifndef MIN
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif

#define print_vd_info(title, fn)      \
  if (fn(p_iso, &psz_str)) {          \
    uprintf(title ": %s\n", psz_str); \
  }                                   \
  free(psz_str);                      \
  psz_str = NULL;

/* Needed for UDF ISO access */
// TODO: should be able to elmininate those with an alternate approach
CdIo_t* cdio_open (const char *psz_source, driver_id_t driver_id) {return NULL;}
void cdio_destroy (CdIo_t *p_cdio) {}

const char *psz_extract_dir = "D:/tmp/iso";

// TODO: Unicode support, progress computation, timestamp preservation

static int udf_extract_files(udf_t *p_udf, udf_dirent_t *p_udf_dirent, const char *psz_path)
{
	FILE *fd = NULL;
	int i_length;
	char* psz_fullpath;
	const char* psz_basename;
	udf_dirent_t *p_udf_dirent2;
	uint8_t buf[UDF_BLOCKSIZE];
	int64_t i_read, i_file_length;

	if ((p_udf_dirent == NULL) || (psz_path == NULL))
		return 1;

	while (udf_readdir(p_udf_dirent)) {
		psz_basename = udf_get_filename(p_udf_dirent);
		i_length = (int)(3 + strlen(psz_path) + strlen(psz_basename) + strlen(psz_extract_dir));
		psz_fullpath = (char*)calloc(sizeof(char), i_length);
		if (psz_fullpath == NULL) {
			uprintf("Error allocating file name\n");
			goto out;
		}
		i_length = _snprintf(psz_fullpath, i_length, "%s%s/%s", psz_extract_dir, psz_path, psz_basename);
		if (i_length < 0) {
			goto out;
		}
		uprintf("Extracting: %s\n", psz_fullpath);
		if (udf_is_dir(p_udf_dirent)) {
			_mkdir(psz_fullpath);
			p_udf_dirent2 = udf_opendir(p_udf_dirent);
			if (p_udf_dirent2 != NULL) {
				if (udf_extract_files(p_udf, p_udf_dirent2, &psz_fullpath[strlen(psz_extract_dir)]))
					goto out;
			}
		} else {
			fd = fopen(psz_fullpath, "wb");
			if (fd == NULL) {
				uprintf("  Unable to create file\n");
				goto out;
			}
			i_file_length = udf_get_file_length(p_udf_dirent);
			while (i_file_length > 0) {
				memset(buf, 0, UDF_BLOCKSIZE);
				i_read = udf_read_block(p_udf_dirent, buf, 1);
				if (i_read < 0) {
					uprintf("  Error reading UDF file %s\n", &psz_fullpath[strlen(psz_extract_dir)]);
					goto out;
				}
				fwrite(buf, (size_t)MIN(i_file_length, i_read), 1, fd);
				if (ferror(fd)) {
					uprintf("  Error writing file\n");
					goto out;
				}
				i_file_length -= i_read;
			}
			fclose(fd);
			fd = NULL;
		}
		free(psz_fullpath);
	}
	return 0;

out:
	if (fd != NULL)
		fclose(fd);
	free(psz_fullpath);
	return 1;
}

static int iso_extract_files(iso9660_t* p_iso, const char *psz_path)
{
	FILE *fd = NULL;
	int i_length, r = 1;
	char psz_fullpath[4096], *psz_basename;
	const char *psz_iso_name = &psz_fullpath[strlen(psz_extract_dir)];
	unsigned char buf[ISO_BLOCKSIZE];
	CdioListNode_t* p_entnode;
	iso9660_stat_t *p_statbuf;
	CdioList_t* p_entlist;
	size_t i;
	lsn_t lsn;
	int64_t i_file_length;

	if ((p_iso == NULL) || (psz_path == NULL))
		return 1;

	i_length = _snprintf(psz_fullpath, sizeof(psz_fullpath), "%s%s/", psz_extract_dir, psz_path);
	if (i_length < 0)
		return 1;
	psz_basename = &psz_fullpath[i_length];

	p_entlist = iso9660_ifs_readdir(p_iso, psz_path);
	if (!p_entlist)
		return 1;

	_CDIO_LIST_FOREACH (p_entnode, p_entlist) {
		p_statbuf = (iso9660_stat_t*) _cdio_list_node_data(p_entnode);
		/* Eliminate . and .. entries */
		if ( (strcmp(p_statbuf->filename, ".") == 0)
			|| (strcmp(p_statbuf->filename, "..") == 0) )
			continue;
		iso9660_name_translate(p_statbuf->filename, psz_basename);
		if (p_statbuf->type == _STAT_DIR) {
			_mkdir(psz_fullpath);
			if (iso_extract_files(p_iso, psz_iso_name))
				goto out;
		} else {
			uprintf("Extracting: %s\n", psz_fullpath);
			fd = fopen(psz_fullpath, "wb");
			if (fd == NULL) {
				uprintf("  Unable to create file\n");
				goto out;
			}
			i_file_length = p_statbuf->size;
			for (i = 0; i_file_length > 0; i++) {
				memset(buf, 0, ISO_BLOCKSIZE);
				lsn = p_statbuf->lsn + (lsn_t)i;
				if (iso9660_iso_seek_read(p_iso, buf, lsn, 1) != ISO_BLOCKSIZE) {
					uprintf("  Error reading ISO9660 file %s at LSN %lu\n",
						psz_iso_name, (long unsigned int)lsn);
					goto out;
				}
				fwrite(buf, (size_t)MIN(i_file_length, ISO_BLOCKSIZE), 1, fd);
				if (ferror(fd)) {
					uprintf("  Error writing file\n");
					goto out;
				}
				i_file_length -= ISO_BLOCKSIZE;
			}
			fclose(fd);
			fd = NULL;
		}
	}
	r = 0;

out:
	if (fd != NULL)
		fclose(fd);
	_cdio_list_free(p_entlist, true);
	return r;
}

BOOL ExtractISO(const char* src_iso, const char* dest_dir)
{
	BOOL r = FALSE;
	iso9660_t* p_iso = NULL;
	udf_t* p_udf = NULL; 
	udf_dirent_t* p_udf_root;
	char *psz_str = NULL;
	char vol_id[UDF_VOLID_SIZE] = "";
	char volset_id[UDF_VOLSET_ID_SIZE+1] = "";

	cdio_loglevel_default = CDIO_LOG_DEBUG;

	/* First try to open as UDF - fallback to ISO if it failed */
	p_udf = udf_open(src_iso);
	if (p_udf == NULL)
		goto try_iso;

	p_udf_root = udf_get_root(p_udf, true, 0);
	if (p_udf_root == NULL) {
		uprintf("Couldn't locate UDF root directory\n");
		goto out;
	}
	vol_id[0] = 0; volset_id[0] = 0;

	/* Show basic UDF Volume info */
	if (udf_get_volume_id(p_udf, vol_id, sizeof(vol_id)) > 0)
		uprintf("Volume id: %s\n", vol_id);
	if (udf_get_volume_id(p_udf, volset_id, sizeof(volset_id)) >0 ) {
		volset_id[UDF_VOLSET_ID_SIZE]='\0';
		uprintf("Volume set id: %s\n", volset_id);
	}
	uprintf("Partition number: %d\n", udf_get_part_number(p_udf));

	/* Recursively extract files */
	r = udf_extract_files(p_udf, p_udf_root, "");

	goto out;

try_iso:
	p_iso = iso9660_open(src_iso);
	if (p_iso == NULL) {
		uprintf("Unable to open image '%s'.\n", src_iso);
		goto out;
	}

	/* Show basic ISO9660 info from the Primary Volume Descriptor. */
	print_vd_info("Application", iso9660_ifs_get_application_id);
	print_vd_info("Preparer   ", iso9660_ifs_get_preparer_id);
	print_vd_info("Publisher  ", iso9660_ifs_get_publisher_id);
	print_vd_info("System     ", iso9660_ifs_get_system_id);
	print_vd_info("Volume     ", iso9660_ifs_get_volume_id);
	print_vd_info("Volume Set ", iso9660_ifs_get_volumeset_id);

	r = iso_extract_files(p_iso, "");

out:
	if (p_iso != NULL)
		iso9660_close(p_iso);
	if (p_udf != NULL)
		udf_close(p_udf);

	return r;
}

#ifdef ISO_TEST
int main(int argc, char** argv)
{
//	ExtractISO("D:\\Incoming\\GRMSDKX_EN_DVD.iso", NULL);
//	ExtractISO("D:\\fd11src.iso", NULL);
//	ExtractISO("D:\\Incoming\\en_windows_driver_kit_3790.iso", NULL);
//	ExtractISO("D:\\Incoming\\en_windows_7_ultimate_with_sp1_x64_dvd_618240.iso", NULL);
	ExtractISO("D:\\Incoming\\Windows 8 Preview\\WindowsDeveloperPreview-64bit-English-Developer.iso", NULL);

#ifdef _CRTDBG_MAP_ALLOC
	_CrtDumpMemoryLeaks();
#endif

	while(getchar() != 0x0a);
	exit(0);
}
#endif
