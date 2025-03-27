/*
* Rufus: The Reliable USB Formatting Utility
* UEFI constants and structs
* Copyright © 2025 Pete Batard <pete@akeo.ie>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <basetsd.h>
#include <guiddef.h>

#pragma pack(1)

typedef struct {
	UINT16  Year;
	UINT8   Month;
	UINT8   Day;
	UINT8   Hour;
	UINT8   Minute;
	UINT8   Second;
	UINT8   Pad1;
	UINT32  Nanosecond;
	INT16   TimeZone;
	UINT8   Daylight;
	UINT8   Pad2;
} EFI_TIME;

typedef struct _WIN_CERTIFICATE {
	UINT32  dwLength;
	UINT16  wRevision;
	UINT16  wCertificateType;
} WIN_CERTIFICATE;

typedef struct _WIN_CERTIFICATE_UEFI_GUID {
	WIN_CERTIFICATE   Hdr;
	GUID              CertType;
	UINT8             CertData[1];
} WIN_CERTIFICATE_UEFI_GUID;

typedef struct {
	EFI_TIME                     TimeStamp;
	WIN_CERTIFICATE_UEFI_GUID    AuthInfo;
} EFI_VARIABLE_AUTHENTICATION_2;

typedef struct {
	GUID        SignatureOwner;
	UINT8       SignatureData[1];
} EFI_SIGNATURE_DATA;

typedef struct {
	GUID        SignatureType;
	UINT32      SignatureListSize;
	UINT32      SignatureHeaderSize;
	UINT32      SignatureSize;
} EFI_SIGNATURE_LIST;

#pragma pack()

const GUID EFI_CERT_SHA256_GUID = { 0xc1c41626, 0x504c, 0x4092, { 0xac, 0xa9, 0x41, 0xf9, 0x36, 0x93, 0x43, 0x28 } };
