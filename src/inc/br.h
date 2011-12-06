#ifndef BR_H
#define BR_H

#include <stdio.h>

/* returns TRUE if the file has a boot record, otherwise FALSE.
   The file position will change when this function is called! */
int is_br(FILE *fp);

/* returns TRUE if the file has a LILO boot record, otherwise FALSE.
   The file position will change when this function is called! */
int is_lilo_br(FILE *fp);

/* returns TRUE if the file has a Microsoft dos master boot record, otherwise
   FALSE.The file position will change when this function is called! */
int is_dos_mbr(FILE *fp);

/* returns TRUE if the file has a Microsoft dos master boot record with the
   undocumented F2 instruction, otherwise FALSE. The file position will change
   when this function is called! */
int is_dos_f2_mbr(FILE *fp);

/* returns TRUE if the file has a Microsoft 95b master boot record, otherwise
   FALSE.The file position will change when this function is called! */
int is_95b_mbr(FILE *fp);

/* returns TRUE if the file has a Microsoft 2000 master boot record, otherwise
   FALSE.The file position will change when this function is called! */
int is_2000_mbr(FILE *fp);

/* returns TRUE if the file has a Microsoft Vista master boot record, otherwise
   FALSE.The file position will change when this function is called! */
int is_vista_mbr(FILE *fp);

/* returns TRUE if the file has a Microsoft 7 master boot record, otherwise
   FALSE.The file position will change when this function is called! */
int is_win7_mbr(FILE *fp);

/* returns TRUE if the file has a syslinux master boot record, otherwise
   FALSE.The file position will change when this function is called! */
int is_syslinux_mbr(FILE *fp);

/* returns TRUE if the file has a zeroed master boot record, otherwise
   FALSE.The file position will change when this function is called! */
int is_zero_mbr(FILE *fp);

/* Writes a dos master boot record to a file, returns TRUE on success, otherwise
   FALSE */
int write_dos_mbr(FILE *fp);

/* Writes a 95b master boot record to a file, returns TRUE on success, otherwise
   FALSE */
int write_95b_mbr(FILE *fp);

/* Writes a 2000 master boot record to a file, returns TRUE on success, otherwise
   FALSE */
int write_2000_mbr(FILE *fp);

/* Writes a Vista master boot record to a file, returns TRUE on success, otherwise
   FALSE */
int write_vista_mbr(FILE *fp);

/* Writes a 7 master boot record to a file, returns TRUE on success, otherwise
   FALSE */
int write_win7_mbr(FILE *fp);

/* Writes a syslinux master boot record to a file, returns TRUE on success, otherwise
   FALSE */
int write_syslinux_mbr(FILE *fp);

/* Writes an empty (zeroed) master boot record to a file, returns TRUE on success, otherwise
   FALSE */
int write_zero_mbr(FILE *fp);

/* Completely clears (zeroes) master boot record, returns TRUE on success, otherwise
   FALSE */
int clear_mbr(FILE *fp);

#endif
