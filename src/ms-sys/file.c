/******************************************************************
    Copyright (C) 2009  Henrik Carlqvist
    Modified for Rufus/Windows (C) 2011-2012  Pete Batard

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

#include "../rufus.h"
#include "file.h"

/* Returns the number of bytes written or -1 on error */
int write_sectors(HANDLE hDrive, size_t SectorSize,
                  size_t StartSector, size_t nSectors,
                  const void *pBuf)
{
   LARGE_INTEGER ptr;
   DWORD Size = (DWORD)(nSectors*SectorSize);

   ptr.QuadPart = StartSector*SectorSize;
   if(!SetFilePointerEx(hDrive, ptr, NULL, FILE_BEGIN))
   {
      uprintf("write_sectors: Could not access sector %d - %s\n", StartSector, WindowsErrorString());
      return -1;
   }

   if((!WriteFile(hDrive, pBuf, Size, &Size, NULL)) || (Size != nSectors*SectorSize))
   {
      uprintf("write_sectors: Write error - %s\n", WindowsErrorString());
      uprintf("  StartSector:%0X, nSectors:%0X, SectorSize:%0X\n", StartSector, nSectors, SectorSize);
      return Size;
   }

   return Size;
}

/* Returns the number of bytes read or -1 on error */
int read_sectors(HANDLE hDrive, size_t SectorSize,
                 size_t StartSector, size_t nSectors,
                 void *pBuf)
{
   LARGE_INTEGER ptr;
   DWORD Size = (DWORD)(nSectors*SectorSize);

   ptr.QuadPart = StartSector*SectorSize;
   if(!SetFilePointerEx(hDrive, ptr, NULL, FILE_BEGIN))
   {
      uprintf("read_sectors: Could not access sector %d - %s\n", StartSector, WindowsErrorString());
      return -1;
   }

   if((!ReadFile(hDrive, pBuf, Size, &Size, NULL)) || (Size != nSectors*SectorSize))
   {
      uprintf("read_sectors: Read error - %s\n", WindowsErrorString());
      uprintf("  StartSector:%0X, nSectors:%0X, SectorSize:%0X\n", StartSector, nSectors, SectorSize);
   }

   return Size;
}

/*
 * The following calls use a bastardized fp on Windows that contains:
 * fp->_ptr: a Windows handle
 * fp->_bufsiz: the sector size
 * fp->_cnt: a file offset
 */
int contains_data(FILE *fp, size_t Position,
                  const void *pData, size_t Len)
{
   unsigned char aucBuf[MAX_DATA_LEN];
   HANDLE hDrive = (HANDLE)fp->_ptr;
   size_t SectorSize = (size_t)fp->_bufsiz;
   size_t StartSector, EndSector, NumSectors;
   Position += (size_t)fp->_cnt;

   StartSector = Position/SectorSize;
   EndSector   = (Position+Len+SectorSize-1)/SectorSize;
   NumSectors  = EndSector - StartSector;

   if((NumSectors*SectorSize) > MAX_DATA_LEN)
   {
      uprintf("Please increase MAX_DATA_LEN in file.h\n");
      return 0;
   }

   if(read_sectors(hDrive, SectorSize, StartSector,
                     NumSectors, aucBuf) <= 0)
      return 0;

   if(memcmp(pData, &aucBuf[Position - StartSector*SectorSize], Len))
      return 0;
   return 1;
} /* contains_data */

/* May read/write the same sector many times, but compatible with existing ms-sys */
int write_data(FILE *fp, size_t Position,
               const void *pData, size_t Len)
{
   unsigned char aucBuf[MAX_DATA_LEN];
   HANDLE hDrive = (HANDLE)fp->_ptr;
   size_t SectorSize = (size_t)fp->_bufsiz;
   size_t StartSector, EndSector, NumSectors;
   Position += (size_t)fp->_cnt;

   StartSector = Position/SectorSize;
   EndSector   = (Position+Len+SectorSize-1)/SectorSize;
   NumSectors  = EndSector - StartSector;

   if((NumSectors*SectorSize) > MAX_DATA_LEN)
   {
      uprintf("Please increase MAX_DATA_LEN in file.h\n");
      return 0;
   }

   /* Data to write may not be aligned on a sector boundary => read into a sector buffer first */
   if(read_sectors(hDrive, SectorSize, StartSector,
                     NumSectors, aucBuf) <= 0)
      return 0;

   if(!memcpy(&aucBuf[Position - StartSector*SectorSize], pData, Len))
      return 0;

   if(write_sectors(hDrive, SectorSize, StartSector,
                     NumSectors, aucBuf) <= 0)
      return 0;
   return 1;
} /* write_data */
