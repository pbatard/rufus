#ifndef FAT32FD_H
#define FAT32FD_H

#include <stdio.h>

/* returns TRUE if the file has an exact match ot the FAT32 boot record this
   program would create for FreeDOS, otherwise FALSE.
   The file position will change when this function is called! */
int entire_fat_32_fd_br_matches(FILE *fp);

/* Writes a FAT32 FreeDOS boot record to a file, returns TRUE on success,
   otherwise FALSE */
int write_fat_32_fd_br(FILE *fp, int bKeepLabel);

#endif
