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
#include <malloc.h>
#include <io.h>
#include <direct.h>
#ifndef ISO_TEST
#include "rufus.h"
#else
#define uprintf(...) printf(__VA_ARGS__)
#endif

#include <cdio/cdio.h>
#include <cdio/logging.h>
#include <cdio/iso9660.h>
#include <cdio/udf.h>

#define print_vd_info(title, fn)			\
	if (fn(p_iso, &psz_str)) {				\
		uprintf(title ": %s\n", psz_str);	\
	}										\
	free(psz_str);							\
	psz_str = NULL;

/* Needed for UDF ISO access */
CdIo_t* cdio_open (const char *psz_source, driver_id_t driver_id) {return NULL;}
void cdio_destroy (CdIo_t *p_cdio) {}

const char* extract_dir = "D:/tmp/iso";

// TODO: Unicode support, timestamp preservation

static void udf_print_file_info(const udf_dirent_t *p_udf_dirent, const char* psz_dirname)
{
	time_t mod_time = udf_get_modification_time(p_udf_dirent);
	char psz_mode[11] = "invalid";
	const char *psz_fname = psz_dirname?psz_dirname:udf_get_filename(p_udf_dirent);

	/* Print directory attributes*/
	uprintf("%s %4d %lu %s %s", udf_mode_string(udf_get_posix_filemode(p_udf_dirent), psz_mode),
		udf_get_link_count(p_udf_dirent), (long unsigned int)udf_get_file_length(p_udf_dirent),
		(*psz_fname?psz_fname:"/"), ctime(&mod_time));
}

static void udf_list_files(udf_t *p_udf, udf_dirent_t *p_udf_dirent, const char *psz_path)
{
	FILE *fd = NULL;
	int len;
	char* fullpath;
	const char* basename;
	udf_dirent_t *p_udf_dirent2;
	uint8_t buf[UDF_BLOCKSIZE];
	int64_t i_read, file_len;

	if ((p_udf_dirent == NULL) || (psz_path == NULL))
		return;

	while (udf_readdir(p_udf_dirent)) {
		basename = udf_get_filename(p_udf_dirent);
		len = 3 + strlen(psz_path) + strlen(basename) + strlen(extract_dir);
		fullpath = (char*)calloc(sizeof(char), len);
		len = _snprintf(fullpath, len, "%s%s/%s", extract_dir, psz_path, basename);
		if (len < 0) {
			goto err;
		}
uprintf("FULLPATH: %s\n", fullpath);
		if (udf_is_dir(p_udf_dirent)) {
			_mkdir(fullpath);
			p_udf_dirent2 = udf_opendir(p_udf_dirent);
			if (p_udf_dirent2 != NULL) {
				udf_list_files(p_udf, p_udf_dirent2, &fullpath[strlen(extract_dir)]);
			}
		} else {
			fd = fopen(fullpath, "wb");
			if (fd == NULL) {
				uprintf("Unable to create file %s\n", fullpath);
				goto err;
			}
			file_len = udf_get_file_length(p_udf_dirent);
			while (file_len > 0) {
				i_read = udf_read_block(p_udf_dirent, buf, 1);
				if (i_read < 0) {
					uprintf("Error reading UDF file %s\n", &fullpath[strlen(extract_dir)]);
					goto err;
				}
				fwrite(buf, (size_t)min(file_len, i_read), 1, fd);
				if (ferror(fd)) {
					uprintf("Error writing file %s\n", fullpath);
					goto err;
				}
				file_len -= i_read;
			}
			fclose(fd);
			fd = NULL;
		}
		free(fullpath);
	}
	return;

err:
	if (fd != NULL)
		fclose(fd);
	free(fullpath);
}

static void iso_list_files(iso9660_t* p_iso, const char *psz_path)
{
	FILE *fd = NULL;
	int len;
	char filename[4096], *basename;
	const char* iso_filename = &filename[strlen(extract_dir)];
	unsigned char buf[ISO_BLOCKSIZE];
	CdioListNode_t* p_entnode;
	iso9660_stat_t *p_statbuf;
	CdioList_t* p_entlist;
	size_t i;
	lsn_t lsn;
	int64_t file_len;

	if ((p_iso == NULL) || (psz_path == NULL))
		return;

	len = _snprintf(filename, sizeof(filename), "%s%s/", extract_dir, psz_path);
	if (len < 0)
		return;
	basename = &filename[len];

	p_entlist = iso9660_ifs_readdir(p_iso, psz_path);
	if (!p_entlist)
		return;

	_CDIO_LIST_FOREACH (p_entnode, p_entlist) {
		p_statbuf = (iso9660_stat_t*) _cdio_list_node_data(p_entnode);
		if ( (strcmp(p_statbuf->filename, ".") == 0)
		  || (strcmp(p_statbuf->filename, "..") == 0) )
			continue;	// Eliminate . and .. entries
		iso9660_name_translate(p_statbuf->filename, basename);
		uprintf("%s [LSN %6d] %8u %s\n", (p_statbuf->type == _STAT_DIR)?"d":"-",
			p_statbuf->lsn, p_statbuf->size, filename);
		if (p_statbuf->type == _STAT_DIR) {
			_mkdir(filename);
			iso_list_files(p_iso, iso_filename);
		} else {
			fd = fopen(filename, "wb");
			if (fd == NULL) {
				uprintf("Unable to create file %s\n", filename);
				goto out;
			}
			file_len = p_statbuf->size;
			for (i = 0; file_len > 0 ; i++) {
				memset (buf, 0, ISO_BLOCKSIZE);
				lsn = p_statbuf->lsn + i;
				if (iso9660_iso_seek_read (p_iso, buf, lsn, 1) != ISO_BLOCKSIZE) {
					uprintf("Error reading ISO 9660 file %s at LSN %lu\n",
						iso_filename, (long unsigned int)lsn);
					goto out;
				}
				fwrite (buf, (size_t)min(file_len, ISO_BLOCKSIZE), 1, fd);
				if (ferror(fd)) {
					uprintf("Error writing file %s\n", filename);
					goto out;
				}
				file_len -= ISO_BLOCKSIZE;
			}
			fclose(fd);
			fd = NULL;
		}
	}
out:
	if (fd != NULL)
		fclose(fd);
	_cdio_list_free(p_entlist, true);
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
	p_udf = udf_open(src_iso);
	if (p_udf == NULL) {
		uprintf("Unable to open UDF image '%s'.\n", src_iso);
		goto try_iso;
	}
	p_udf_root = udf_get_root(p_udf, true, 0);
	if (p_udf_root == NULL) {
		uprintf("Couldn't locate UDF root directory\n");
		goto out;
	}
	vol_id[0] = 0; volset_id[0] = 0;
	if (udf_get_volume_id(p_udf, vol_id, sizeof(vol_id)) > 0)
		uprintf("volume id: %s\n", vol_id);
	if (udf_get_volume_id(p_udf, volset_id, sizeof(volset_id)) >0 ) {
		volset_id[UDF_VOLSET_ID_SIZE]='\0';
		uprintf("volume set id: %s\n", volset_id);
	}
	uprintf("partition number: %d\n", udf_get_part_number(p_udf));
	udf_list_files(p_udf, p_udf_root, "");

	r = TRUE;
	goto out;

try_iso:
	p_iso = iso9660_open(src_iso);
	if (p_iso == NULL) {
		uprintf("Unable to open ISO image '%s'.\n", src_iso);
		goto out;
	}

	/* Show basic CD info from the Primary Volume Descriptor. */
	print_vd_info("Application", iso9660_ifs_get_application_id);
	print_vd_info("Preparer   ", iso9660_ifs_get_preparer_id);
	print_vd_info("Publisher  ", iso9660_ifs_get_publisher_id);
	print_vd_info("System     ", iso9660_ifs_get_system_id);
	print_vd_info("Volume     ", iso9660_ifs_get_volume_id);
	print_vd_info("Volume Set ", iso9660_ifs_get_volumeset_id);

	iso_list_files(p_iso, "");
	r = TRUE;

#if 0
	iso9660_stat_t *statbuf;
	FILE* outfd;
	uint32_t i;
	char file_name[] = "TEST.TST";
	char buf[ISO_BLOCKSIZE];

	statbuf = iso9660_ifs_stat_translate(p_iso, file_name);
	if (statbuf == NULL) {
		uprintf("Could not get ISO-9660 file information for file %s.\n", file_name);
		goto out2;
	}

	if (!(outfd = fopen(file_name, "wb"))) {
		uprintf("Could not open %s for writing\n", file_name);
		goto out2;
	}

	/* Copy the blocks from the ISO-9660 filesystem to the local filesystem. */
	for (i=0; i<statbuf->size; i+=ISO_BLOCKSIZE) {
		memset (buf, 0, ISO_BLOCKSIZE);
		if (iso9660_iso_seek_read(p_iso, buf, statbuf->lsn + (i / ISO_BLOCKSIZE), 1) != ISO_BLOCKSIZE) {
			uprintf("Error reading ISO 9660 file at lsn %lu\n", (long unsigned int) statbuf->lsn + (i / ISO_BLOCKSIZE));
			goto out3;
		}

		fwrite(buf, ISO_BLOCKSIZE, 1, outfd);
		if (ferror (outfd)) {
			uprintf("Error writing file\n");
			goto out3;
		}
	}

	fflush(outfd);

	/* Make sure the file size has the exact same byte size. Without the
	 * truncate below, the file will a multiple of ISO_BLOCKSIZE. */
	if (_chsize(_fileno(outfd), statbuf->size) != 0) {
		uprintf("Error adjusting file size\n");
		goto out3;
	}

	r = TRUE;

out3:
	fclose(outfd);
out2:
#endif
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
	ExtractISO("D:\\Incoming\\en_windows_7_ultimate_with_sp1_x64_dvd_618240.iso", NULL);
//	ExtractISO("D:\\Incoming\\Windows 8 Preview\\WindowsDeveloperPreview-64bit-English-Developer.iso", NULL);

#ifdef _CRTDBG_MAP_ALLOC
	_CrtDumpMemoryLeaks();
#endif

	while(getchar() != 0x0a);
	exit(0);
}
#endif
