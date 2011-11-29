#ifndef FAT32_H
#define FAT32_H

#include <stdio.h>

/* returns TRUE if the file has a FAT32 file system, otherwise FALSE.
   The file position will change when this function is called! */
int is_fat_32_fs(FILE *fp);

/* returns TRUE if the file has a FAT32 DOS boot record, otherwise FALSE.
   The file position will change when this function is called! */
int is_fat_32_br(FILE *fp);

/* returns TRUE if the file has an exact match of the FAT32 DOS boot record
   this program would create, otherwise FALSE.
   The file position will change when this function is called! */
int entire_fat_32_br_matches(FILE *fp);

/* Writes a FAT32 DOS boot record to a file, returns TRUE on success, otherwise
   FALSE */
int write_fat_32_br(FILE *fp, int bKeepLabel);

#endif
