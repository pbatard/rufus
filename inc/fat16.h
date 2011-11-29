#ifndef FAT16_H
#define FAT16_H

#include <stdio.h>

/* returns TRUE if the file has a FAT16 file system, otherwise FALSE.
   The file position will change when this function is called! */
int is_fat_16_fs(FILE *fp);

/* returns TRUE if the file has a FAT16 boot record, otherwise FALSE.
   The file position will change when this function is called! */
int is_fat_16_br(FILE *fp);

/* returns TRUE if the file has an exact match ot the FAT16 boot record this
   program would create, otherwise FALSE.
   The file position will change when this function is called! */
int entire_fat_16_br_matches(FILE *fp);

/* Writes a FAT16 boot record to a file, returns TRUE on success, otherwise
   FALSE */
int write_fat_16_br(FILE *fp, int bKeepLabel);

#endif
