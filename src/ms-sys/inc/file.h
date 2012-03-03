#ifndef FILE_H
#define FILE_H

#include <stdint.h>

/* Max valid value of uiLen for contains_data */
#define MAX_DATA_LEN 8192

/* Checks if a file contains a data pattern of length Len at position
   Position. The file pointer will change when calling this function! */
int contains_data(FILE *fp, uint64_t Position,
                  const void *pData, uint64_t Len);

/* Writes a data pattern of length Len at position Position.
   The file pointer will change when calling this function! */
int write_data(FILE *fp, uint64_t Position,
               const void *pData, uint64_t Len);

/* Writes nSectors of size SectorSize starting at sector StartSector */
int64_t write_sectors(void *hDrive, uint64_t SectorSize,
                      uint64_t StartSector, uint64_t nSectors,
                      const void *pBuf);

/* Reads nSectors of size SectorSize starting at sector StartSector */
int64_t read_sectors(void *hDrive, uint64_t SectorSize,
                     uint64_t StartSector, uint64_t nSectors,
                     void *pBuf);

#endif
