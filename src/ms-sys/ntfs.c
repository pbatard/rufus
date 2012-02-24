/******************************************************************
    Copyright (C) 2012  Pete Batard <pete@akeo.ie>
    Based on fat16.c Copyright (C) 2009  Henrik Carlqvist

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
******************************************************************/

#include <stdio.h>
#include <string.h>

#include "file.h"
#include "ntfs.h"

int is_ntfs_fs(FILE *fp)
{
   unsigned char aucMagic[] = {'N','T','F','S',' ',' ',' ',' '};

   return contains_data(fp, 0x03, aucMagic, sizeof(aucMagic));
} /* is_ntfs_fs */

int is_ntfs_br(FILE *fp)
{
   /* A "file" is probably some kind of NTFS boot record if it contains the
      magic chars 0x55, 0xAA at positions 0x1FE */
   unsigned char aucRef[] = {0x55, 0xAA};
   unsigned char aucMagic[] = {'N','T','F','S',' ',' ',' ',' '};

   if( ! contains_data(fp, 0x1FE, aucRef, sizeof(aucRef)))
      return 0;
   if( ! contains_data(fp, 0x03, aucMagic, sizeof(aucMagic)))
      return 0;
   return 1;
} /* is_ntfs_br */

int entire_ntfs_br_matches(FILE *fp)
{
   #include "br_ntfs_0x0.h"
   #include "br_ntfs_0x54.h"

   return
      ( contains_data(fp, 0x0, br_ntfs_0x0, sizeof(br_ntfs_0x0)) &&
	/* BIOS Parameter Block might differ between systems */
	contains_data(fp, 0x54, br_ntfs_0x54, sizeof(br_ntfs_0x54)) );
} /* entire_ntfs_br_matches */

int write_ntfs_br(FILE *fp)
{
   #include "br_ntfs_0x0.h"
   #include "br_ntfs_0x54.h"

   return
	 ( write_data(fp, 0x0, br_ntfs_0x0, sizeof(br_ntfs_0x0)) &&
	   /* BIOS Parameter Block should not be overwritten */
	   write_data(fp, 0x54, br_ntfs_0x54, sizeof(br_ntfs_0x54)) );
} /* write_ntsf_br */
