#ifndef FAT12_H
#define FAT12_H

/* returns TRUE if the file has a FAT12 file system, otherwise FALSE.
   The file position will change when this function is called! */
int is_fat_12_fs(FILE *fp);

/* returns TRUE if the file has an exact match ot the FAT12 boot record this
   program would create, otherwise FALSE.
   The file position will change when this function is called! */
int entire_fat_12_br_matches(FILE *fp);

/* Writes a FAT12 boot record to a file, returns TRUE on success, otherwise
   FALSE */
int write_fat_12_br(FILE *fp, int bKeepLabel);


#endif
