#ifndef FILE_H
#define FILE_H

/* Max valid value of uiLen for contains_data */
#define MAX_DATA_LEN 4096

/* Checks if a file contains a data pattern of length uiLen at position
   ulPositoin. The file pointer will change when calling this function! */
int contains_data(FILE *fp, unsigned long ulPosition,
                  const void *pData, unsigned int uiLen);

/* Writes a data pattern of length uiLen at position ulPositoin.
   The file pointer will change when calling this function! */
int write_data(FILE *fp, unsigned long ulPosition,
               const void *pData, unsigned int uiLen);

/* Checks if a file contains a data pattern of length uiLen at position
   ulPositoin. The file pointer will change when calling this function! */
int write_sectors(void *hDrive, size_t SectorSize,
                  size_t StartSector, size_t nSectors,
                  const void *pBuf, size_t BufSize);

int read_sectors(void *hDrive, size_t SectorSize,
                 size_t StartSector, size_t nSectors,
                 void *pBuf, size_t BufSize);

#endif
