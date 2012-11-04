#ifndef NTFS_H
#define NTFS_H

#include <stdio.h>

/* returns TRUE if the file has an NFTS file system, otherwise FALSE.
   The file position will change when this function is called! */
int is_ntfs_fs(FILE *fp);

/* returns TRUE if the file has a NTFS boot record, otherwise FALSE.
   The file position will change when this function is called! */
int is_ntfs_br(FILE *fp);

/* returns TRUE if the file has an exact match of the NTFS boot record
   this program would create, otherwise FALSE.
   The file position will change when this function is called! */
int entire_ntfs_br_matches(FILE *fp);

/* Writes a NTFS boot record to a file, returns TRUE on success, otherwise
   FALSE */
int write_ntfs_br(FILE *fp);

#endif
