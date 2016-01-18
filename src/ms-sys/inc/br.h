#ifndef BR_H
#define BR_H

#include <stdio.h>
#include <stdint.h>

/* Sets custom number of bytes per sector, default value is 512 */
void set_bytes_per_sector(unsigned long ulValue);

/* Gets Windows Disk Signature from MBR */
uint32_t read_windows_disk_signature(FILE *fp);

/* Sets a new Windows Disk Signature to MBR */
int write_windows_disk_signature(FILE *fp, uint32_t tWDS);

/* Reads copy protect bytes after Windows Disk Signature from MBR */
uint16_t read_mbr_copy_protect_bytes(FILE *fp);
const char *read_mbr_copy_protect_bytes_explained(FILE *fp);

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

/* returns TRUE if the file has a Rufus master boot record, otherwise
   FALSE.The file position will change when this function is called! */
int is_rufus_mbr(FILE *fp);

/* returns TRUE if the file has a ReactOS master boot record, otherwise
   FALSE.The file position will change when this function is called! */
int is_reactos_mbr(FILE *fp);

/* returns TRUE if the file has a Grub4DOS master boot record, otherwise
   FALSE.The file position will change when this function is called! */
int is_grub4dos_mbr(FILE *fp);

/* returns TRUE if the file has a Grub 2.0 master boot record, otherwise
   FALSE.The file position will change when this function is called! */
int is_grub2_mbr(FILE *fp);

/* returns TRUE if the file has a KolibriOS master boot record, otherwise
   FALSE.The file position will change when this function is called! */
int is_kolibrios_mbr(FILE *fp);

/* returns TRUE if the file has a syslinux master boot record, otherwise
   FALSE.The file position will change when this function is called! */
int is_syslinux_mbr(FILE *fp);

/* returns TRUE if the file has a syslinux GPT master boot record, otherwise
   FALSE.The file position will change when this function is called! */
int is_syslinux_gpt_mbr(FILE *fp);

/* returns TRUE if the file has a zeroed master boot record, otherwise
   FALSE.The file position will change when this function is called! */
int is_zero_mbr(FILE *fp);
int is_zero_mbr_not_including_disk_signature_or_copy_protect(FILE *fp);

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

/* Writes a Windows 7 master boot record to a file, returns TRUE on success, otherwise
   FALSE */
int write_win7_mbr(FILE *fp);

/* Writes a Rufus master boot record to a file, returns TRUE on success, otherwise
   FALSE */
int write_rufus_mbr(FILE *fp);

/* Writes a ReactOS master boot record to a file, returns TRUE on success, otherwise
   FALSE */
int write_reactos_mbr(FILE *fp);

/* Writes a Grub4DOS master boot record to a file, returns TRUE on success, otherwise
   FALSE */
int write_grub4dos_mbr(FILE *fp);

/* Writes a Grub 2.0 master boot record to a file, returns TRUE on success, otherwise
   FALSE */
int write_grub2_mbr(FILE *fp);

/* Writes a KolibriOS master boot record to a file, returns TRUE on success, otherwise
   FALSE */
int write_kolibrios_mbr(FILE *fp);

/* Writes a syslinux master boot record to a file, returns TRUE on success, otherwise
   FALSE */
int write_syslinux_mbr(FILE *fp);

/* Writes a syslinux GPT master boot record to a file, returns TRUE on success, otherwise
   FALSE */
int write_syslinux_gpt_mbr(FILE *fp);

/* Writes an empty (zeroed) master boot record to a file, returns TRUE on success, otherwise
   FALSE */
int write_zero_mbr(FILE *fp);

#endif
