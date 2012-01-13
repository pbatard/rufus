/*
 * Rufus: The Reliable USB Formatting Utility
 * ISO file extraction
 * Copyright (c) 2011-2012 Pete Batard <pete@akeo.ie>
 * Based on libcdio's iso-read.c & iso-info.c:
 * Copyright (C) 2004, 2005, 2006, 2008 Rocky Bernstein <rocky@gnu.org>
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
#include <stdio.h>
#include <malloc.h>
#include "rufus.h"

#include <cdio/cdio.h>
#include <cdio/iso9660.h>

#define print_vd_info(title, fn)			\
	if (fn(p_iso, &psz_str)) {				\
		uprintf(title ": %s\n", psz_str);	\
	}										\
	free(psz_str);							\
	psz_str = NULL;

BOOL ExtractISO(const char* src_iso, const char* dest_dir)
{
	BOOL r = FALSE;
	CdioList_t *p_entlist;
	CdioListNode_t *p_entnode;
	iso9660_t *p_iso;
	const char *psz_path="/";
	char *psz_str = NULL;


	p_iso = iso9660_open(src_iso);
	if (p_iso == NULL) {
		uprintf("Unable to open ISO image '%s'.\n", src_iso);
		goto out1;
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
		_cdio_list_free (p_entlist, true);
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
	iso9660_close(p_iso);
out1:

	return r;
}
