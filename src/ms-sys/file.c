/******************************************************************
    Copyright (C) 2009  Henrik Carlqvist
    Modified for Rufus/Windows (C) 2011-2016  Pete Batard

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
******************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../rufus.h"
#include "file.h"

extern unsigned long ulBytesPerSector;

/* Returns the number of bytes written or -1 on error */
int64_t write_sectors(HANDLE hDrive, uint64_t SectorSize,
                      uint64_t StartSector, uint64_t nSectors,
                      const void *pBuf)
{
   LARGE_INTEGER ptr;
   DWORD Size;

   if((nSectors*SectorSize) > 0xFFFFFFFFUL)
   {
      uprintf("write_sectors: nSectors x SectorSize is too big\n");
      return -1;
   }
   Size = (DWORD)(nSectors*SectorSize);

   ptr.QuadPart = StartSector*SectorSize;
   if(!SetFilePointerEx(hDrive, ptr, NULL, FILE_BEGIN))
   {
      uprintf("write_sectors: Could not access sector 0x%08" PRIx64 " - %s\n", StartSector, WindowsErrorString());
      return -1;
   }

   if(!WriteFile(hDrive, pBuf, Size, &Size, NULL))
   {
      uprintf("write_sectors: Write error %s\n", WindowsErrorString());
      uprintf("  StartSector: 0x%08" PRIx64 ", nSectors: 0x%" PRIx64 ", SectorSize: 0x%" PRIx64 "\n", StartSector, nSectors, SectorSize);
      return -1;
   }
   if (Size != nSectors*SectorSize)
   {
      /* Some large drives return 0, even though all the data was written - See github #787 */
      if (large_drive && Size == 0) {
         uprintf("Warning: Possible short write\n");
         return 0;
      }
      uprintf("write_sectors:write error\n");
      uprintf("  Wrote: %d, Expected: %" PRIu64 "\n", Size, nSectors*SectorSize);
      uprintf("  StartSector: 0x%08" PRIx64 ", nSectors: 0x%" PRIx64 ", SectorSize: 0x%" PRIx64 "\n", StartSector, nSectors, SectorSize);
      return -1;
   }

   return (int64_t)Size;
}

/* Returns the number of bytes read or -1 on error */
int64_t read_sectors(HANDLE hDrive, uint64_t SectorSize,
                     uint64_t StartSector, uint64_t nSectors,
                     void *pBuf)
{
   LARGE_INTEGER ptr;
   DWORD Size;

   if((nSectors*SectorSize) > 0xFFFFFFFFUL)
   {
      uprintf("read_sectors: nSectors x SectorSize is too big\n");
      return -1;
   }
   Size = (DWORD)(nSectors*SectorSize);

   ptr.QuadPart = StartSector*SectorSize;
   if(!SetFilePointerEx(hDrive, ptr, NULL, FILE_BEGIN))
   {
      uprintf("read_sectors: Could not access sector 0x%08" PRIx64 " - %s\n", StartSector, WindowsErrorString());
      return -1;
   }

   if((!ReadFile(hDrive, pBuf, Size, &Size, NULL)) || (Size != nSectors*SectorSize))
   {
      uprintf("read_sectors: Read error %s\n", (GetLastError()!=ERROR_SUCCESS)?WindowsErrorString():"");
      uprintf("  Read: %d, Expected: %" PRIu64 "\n",  Size, nSectors*SectorSize);
      uprintf("  StartSector: 0x%08" PRIx64 ", nSectors: 0x%" PRIx64 ", SectorSize: 0x%" PRIx64 "\n", StartSector, nSectors, SectorSize);
   }

   return (int64_t)Size;
}

/*
* The following calls use a hijacked fp on Windows that contains:
* fp->_handle: a Windows handle
* fp->_offset: a file offset
*/

int contains_data(FILE *fp, uint64_t Position,
	const void *pData, uint64_t Len)
{
   int r = 0;
   unsigned char *aucBuf = _mm_malloc(MAX_DATA_LEN, 16);

   if(aucBuf == NULL)
      return 0;

   if(!read_data(fp, Position, aucBuf, Len))
      goto out;

   if(memcmp(pData, aucBuf, (size_t)Len))
      goto out;

   r = 1;

out:
   _mm_free(aucBuf);
   return r;
} /* contains_data */

int read_data(FILE *fp, uint64_t Position,
              void *pData, uint64_t Len)
{
   int r = 0;
   unsigned char *aucBuf = _mm_malloc(MAX_DATA_LEN, 16);
   FAKE_FD* fd = (FAKE_FD*)fp;
   HANDLE hDrive = (HANDLE)fd->_handle;
   uint64_t StartSector, EndSector, NumSectors;

   if (aucBuf == NULL)
      return 0;

   Position += fd->_offset;

   StartSector = Position/ulBytesPerSector;
   EndSector   = (Position+Len+ulBytesPerSector -1)/ulBytesPerSector;
   NumSectors  = (size_t)(EndSector - StartSector);

   if((NumSectors*ulBytesPerSector) > MAX_DATA_LEN)
   {
      uprintf("read_data: Please increase MAX_DATA_LEN in file.h\n");
      goto out;
   }

   if(Len > 0xFFFFFFFFUL)
   {
      uprintf("read_data: Len is too big\n");
      goto out;
   }

   if(read_sectors(hDrive, ulBytesPerSector, StartSector,
                     NumSectors, aucBuf) <= 0)
   goto out;

   memcpy(pData, &aucBuf[Position - StartSector*ulBytesPerSector], (size_t)Len);

   r = 1;

out:
   _mm_free(aucBuf);
   return r;
}  /* read_data */

/* May read/write the same sector many times, but compatible with existing ms-sys */
int write_data(FILE *fp, uint64_t Position,
               const void *pData, uint64_t Len)
{
   int r = 0;
   /* Windows' WriteFile() may require a buffer that is aligned to the sector size */
   /* TODO: We may need to increase the alignment if we get report of issues on 4K */
   unsigned char *aucBuf = _mm_malloc(MAX_DATA_LEN, 512);
   FAKE_FD* fd = (FAKE_FD*)fp;
   HANDLE hDrive = (HANDLE)fd->_handle;
   uint64_t StartSector, EndSector, NumSectors;

   if (aucBuf == NULL)
      return 0;

   Position += fd->_offset;

   StartSector = Position/ulBytesPerSector;
   EndSector   = (Position+Len+ulBytesPerSector-1)/ulBytesPerSector;
   NumSectors  = EndSector - StartSector;

   if((NumSectors*ulBytesPerSector) > MAX_DATA_LEN)
   {
      uprintf("write_data: Please increase MAX_DATA_LEN in file.h\n");
      goto out;
   }

   if(Len > 0xFFFFFFFFUL)
   {
      uprintf("write_data: Len is too big\n");
      goto out;
   }

   /* Data to write may not be aligned on a sector boundary => read into a sector buffer first */
   if(read_sectors(hDrive, ulBytesPerSector, StartSector,
                     NumSectors, aucBuf) <= 0)
      goto out;

   if(!memcpy(&aucBuf[Position - StartSector*ulBytesPerSector], pData, (size_t)Len))
      goto out;

   if(write_sectors(hDrive, ulBytesPerSector, StartSector,
                     NumSectors, aucBuf) <= 0)
      goto out;

   r = 1;

out:
   _mm_free(aucBuf);
   return r;
} /* write_data */
