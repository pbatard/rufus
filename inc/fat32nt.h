#ifndef FAT32NT_H
#define FAT32NT_H

#include <stdio.h>

/* returns TRUE if the file has an exact match ot the FAT32 boot record this
   program would create for NT, otherwise FALSE.
   The file position will change when this function is called! */
int entire_fat_32_nt_br_matches(FILE *fp);

/* Writes a FAT32 NT boot record to a file, returns TRUE on success, otherwise
   FALSE */
int write_fat_32_nt_br(FILE *fp, int bKeepLabel);

#endif
