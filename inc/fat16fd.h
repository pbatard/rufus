#ifndef FAT16FD_H
#define FAT16FD_H

#include <stdio.h>

/* returns TRUE if the file has an exact match ot the FAT16 boot record this
   program would create for FreeDOS, otherwise FALSE.
   The file position will change when this function is called! */
int entire_fat_16_fd_br_matches(FILE *fp);

/* Writes a FAT16 FreeDOS boot record to a file, returns TRUE on success,
   otherwise FALSE */
int write_fat_16_fd_br(FILE *fp, int bKeepLabel);

#endif
