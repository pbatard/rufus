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

static void log_handler(cdio_log_level_t level, const char* message)
{
	uprintf("cdio %d message: %s\n", level, message);
}

static void print_file_info(const udf_dirent_t *p_udf_dirent, const char* psz_dirname)
{
	time_t mod_time = udf_get_modification_time(p_udf_dirent);
	char psz_mode[11] = "invalid";
	const char *psz_fname = psz_dirname?psz_dirname:udf_get_filename(p_udf_dirent);

	/* Print directory attributes*/
	uprintf("%s %4d %lu %s %s", udf_mode_string(udf_get_posix_filemode(p_udf_dirent), psz_mode),
		udf_get_link_count(p_udf_dirent), (long unsigned int)udf_get_file_length(p_udf_dirent),
		(*psz_fname?psz_fname:"/"), ctime(&mod_time));
}

static void list_files(udf_t *p_udf, udf_dirent_t *p_udf_dirent, const char *psz_path)
{
	if (!p_udf_dirent)
		return;
	print_file_info(p_udf_dirent, psz_path);
	while (udf_readdir(p_udf_dirent)) {
		if (udf_is_dir(p_udf_dirent)) {
			udf_dirent_t *p_udf_dirent2 = udf_opendir(p_udf_dirent);
			if (p_udf_dirent2) {
				const char *psz_dirname = udf_get_filename(p_udf_dirent);
				const unsigned int i_newlen = 2 + strlen(psz_path) + strlen(psz_dirname);
				char* psz_newpath = (char*)calloc(sizeof(char), i_newlen);
				_snprintf(psz_newpath, i_newlen, "%s%s/", psz_path, psz_dirname);
				list_files(p_udf, p_udf_dirent2, psz_newpath);
				free(psz_newpath);
			}
		} else {
			print_file_info(p_udf_dirent, NULL);
		}
	}
}

BOOL ExtractISO(const char* src_iso, const char* dest_dir)
{
	BOOL r = FALSE;
	CdioList_t* p_entlist;
	CdioListNode_t* p_entnode;
	iso9660_t* p_iso = NULL;
	udf_t* p_udf = NULL; 
	udf_dirent_t* p_udf_root;
//	udf_dirent_t* p_udf_file = NULL;
	const char *psz_path="/";
	char *psz_str = NULL;
	char vol_id[UDF_VOLID_SIZE] = "";
	char volset_id[UDF_VOLSET_ID_SIZE+1] = "";

	cdio_log_set_handler(log_handler);

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
	list_files(p_udf, p_udf_root, "");

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

	p_entlist = iso9660_ifs_readdir(p_iso, psz_path);

	/* Iterate over the list of nodes that iso9660_ifs_readdir gives  */
	if (p_entlist) {
		_CDIO_LIST_FOREACH (p_entnode, p_entlist) {
			char filename[4096];
			iso9660_stat_t *p_statbuf = (iso9660_stat_t*) _cdio_list_node_data(p_entnode);
			iso9660_name_translate(p_statbuf->filename, filename);
			uprintf("%s [LSN %6d] %8u %s%s\n", _STAT_DIR == p_statbuf->type ? "d" : "-",
				p_statbuf->lsn, p_statbuf->size, psz_path, filename);
		}
		_cdio_list_free(p_entlist, true);
	} else {
		uprintf("Could not open ISO directory!\n");
	}
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
//	ExtractISO("D:\\src\\libcdio\\test\\udf102.iso", NULL);
	ExtractISO("D:\\Incoming\\en_windows_7_ultimate_with_sp1_x64_dvd_618240.iso", NULL);

	while(getchar() != 0x0a);

#ifdef _CRTDBG_MAP_ALLOC
	_CrtDumpMemoryLeaks();
#endif

	exit(0);
}
#endif
