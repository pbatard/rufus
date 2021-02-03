/*
 * Rufus: The Reliable USB Formatting Utility
 * Standard User I/O Routines (logging, status, error, etc.)
 * Copyright © 2020 Mattiwatti <mattiwatti@gmail.com>
 * Copyright © 2011-2020 Pete Batard <pete@akeo.ie>
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
#ifdef _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <winternl.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "rufus.h"
#include "resource.h"
#include "msapi_utf8.h"
#include "localization.h"

/*
 * Globals
 */
HWND hStatus;
size_t ubuffer_pos = 0;
char ubuffer[UBUFFER_SIZE];	// Buffer for ubpushf() messages we don't log right away
static HANDLE print_mutex = NULL;

void _uprintf(const char *format, ...)
{
	static char buf[4096];
	char* p = buf;
	wchar_t* wbuf;
	va_list args;
	int n;

	if (print_mutex == NULL)
		print_mutex = CreateMutex(NULL, FALSE, NULL);
	if (WaitForSingleObject(print_mutex, INFINITE) != WAIT_OBJECT_0)
		return;

	va_start(args, format);
	n = safe_vsnprintf(p, sizeof(buf)-3, format, args); // buf-3 is room for CR/LF/NUL
	va_end(args);

	p += (n < 0)?sizeof(buf)-3:n;

	while((p>buf) && (isspaceU(p[-1])))
		*--p = '\0';

	*p++ = '\r';
	*p++ = '\n';
	*p   = '\0';

	// Yay, Windows 10 *FINALLY* added actual Unicode support for OutputDebugStringW()!
	wbuf = utf8_to_wchar(buf);
	// Send output to Windows debug facility
	OutputDebugStringW(wbuf);
	if ((hLog != NULL) && (hLog != INVALID_HANDLE_VALUE)) {
		// Send output to our log Window
		Edit_SetSel(hLog, MAX_LOG_SIZE, MAX_LOG_SIZE);
		Edit_ReplaceSel(hLog, wbuf);
		// Make sure the message scrolls into view
		// (Or see code commented in LogProc:WM_SHOWWINDOW for a less forceful scroll)
		Edit_Scroll(hLog, Edit_GetLineCount(hLog), 0);
	}
	free(wbuf);

	ReleaseMutex(print_mutex);
}

void _uprintfs(const char* str)
{
	wchar_t* wstr;
	wstr = utf8_to_wchar(str);
	OutputDebugStringW(wstr);
	if ((hLog != NULL) && (hLog != INVALID_HANDLE_VALUE)) {
		Edit_SetSel(hLog, MAX_LOG_SIZE, MAX_LOG_SIZE);
		Edit_ReplaceSel(hLog, wstr);
		Edit_Scroll(hLog, Edit_GetLineCount(hLog), 0);
	}
	free(wstr);
}

uint32_t read_file(const char* path, uint8_t** buf)
{
	FILE* fd = fopenU(path, "rb");
	if (fd == NULL) {
		uprintf("Error: Can't open file '%s'", path);
		return 0;
	}

	fseek(fd, 0L, SEEK_END);
	uint32_t size = (uint32_t)ftell(fd);
	fseek(fd, 0L, SEEK_SET);

	*buf = malloc(size);
	if (*buf == NULL) {
		uprintf("Error: Can't allocate %d bytes buffer for file '%s'", size, path);
		size = 0;
		goto out;
	}
	if (fread(*buf, 1, size, fd) != size) {
		uprintf("Error: Can't read '%s'", path);
		size = 0;
	}

out:
	fclose(fd);
	if (size == 0) {
		free(*buf);
		*buf = NULL;
	}
	return size;
}

uint32_t write_file(const char* path, const uint8_t* buf, const uint32_t size)
{
	uint32_t written;
	FILE* fd = fopenU(path, "wb");
	if (fd == NULL) {
		uprintf("Error: Can't create '%s'", path);
		return 0;
	}
	written = (uint32_t)fwrite(buf, 1, size, fd);
	if (written != size)
		uprintf("Error: Can't write '%s'", path);
	fclose(fd);
	return written;
}

// Prints a bitstring of a number of any size, with or without leading zeroes.
// See also the printbits() and printbitslz() helper macros in rufus.h
char *_printbits(size_t const size, void const * const ptr, int leading_zeroes)
{
	// sizeof(uintmax_t) so that we have enough space to store whatever is thrown at us
	static char str[sizeof(uintmax_t) * 8 + 3];
	size_t i;
	uint8_t* b = (uint8_t*)ptr;
	uintmax_t mask, lzmask = 0, val = 0;

	// Little endian, the SCOURGE of any rational computing
	for (i = 0; i < size; i++)
		val |= ((uintmax_t)b[i]) << (8 * i);

	str[0] = '0';
	str[1] = 'b';
	if (leading_zeroes)
		lzmask = 1ULL << (size * 8 - 1);
	for (i = 2, mask = 1ULL << (sizeof(uintmax_t) * 8 - 1); mask != 0; mask >>= 1) {
		if ((i > 2) || (lzmask & mask))
			str[i++] = (val & mask) ? '1' : '0';
		else if (val & mask)
			str[i++] = '1';
	}
	str[i] = '\0';
	return str;
}

// Display an hex dump of buffer 'buf'
void DumpBufferHex(void *buf, size_t size)
{
	unsigned char* buffer = (unsigned char*)buf;
	size_t i, j, k;
	char line[80] = "";

	for (i=0; i<size; i+=16) {
		if (i!=0)
			uprintf("%s\n", line);
		line[0] = 0;
		sprintf(&line[strlen(line)], "  %08x  ", (unsigned int)i);
		for(j=0,k=0; k<16; j++,k++) {
			if (i+j < size) {
				sprintf(&line[strlen(line)], "%02x", buffer[i+j]);
			} else {
				sprintf(&line[strlen(line)], "  ");
			}
			sprintf(&line[strlen(line)], " ");
		}
		sprintf(&line[strlen(line)], " ");
		for(j=0,k=0; k<16; j++,k++) {
			if (i+j < size) {
				if ((buffer[i+j] < 32) || (buffer[i+j] > 126)) {
					sprintf(&line[strlen(line)], ".");
				} else {
					sprintf(&line[strlen(line)], "%c", buffer[i+j]);
				}
			}
		}
	}
	uprintf("%s\n", line);
}

// Count on Microsoft to add a new API while not bothering updating the existing error facilities,
// so that the new error messages have to be handled manually. Now, since I don't have all day:
// 1. Copy text from https://docs.microsoft.com/en-us/openspecs/windows_protocols/ms-vds/5102cc53-3143-4268-ba4c-6ea39e999ab4
// 2. awk '{l[NR%7]=$0} {if (NR%7==0) printf "\tcase %s:\t// %s\n\t\treturn \"%s\";\n", l[1], l[3], l[6]}' vds.txt
// 3. Filter out the crap we don't need.
static const char *GetVdsError(DWORD error_code)
{
	switch (error_code) {
	case 0x80042400:	// VDS_E_NOT_SUPPORTED
		return "The operation is not supported by the object.";
	case 0x80042401:	// VDS_E_INITIALIZED_FAILED
		return "VDS or the provider failed to initialize.";
	case 0x80042402:	// VDS_E_INITIALIZE_NOT_CALLED
		return "VDS did not call the hardware provider's initialization method.";
	case 0x80042403:	// VDS_E_ALREADY_REGISTERED
		return "The provider is already registered.";
	case 0x80042404:	// VDS_E_ANOTHER_CALL_IN_PROGRESS
		return "A concurrent second call is made on an object before the first call is completed.";
	case 0x80042405:	// VDS_E_OBJECT_NOT_FOUND
		return "The specified object was not found.";
	case 0x80042406:	// VDS_E_INVALID_SPACE
		return "The specified space is neither free nor valid.";
	case 0x80042407:	// VDS_E_PARTITION_LIMIT_REACHED
		return "No more partitions can be created on the specified disk.";
	case 0x80042408:	// VDS_E_PARTITION_NOT_EMPTY
		return "The extended partition is not empty.";
	case 0x80042409:	// VDS_E_OPERATION_PENDING
		return "The operation is still in progress.";
	case 0x8004240A:	// VDS_E_OPERATION_DENIED
		return "The operation is not permitted on the specified disk, partition, or volume.";
	case 0x8004240B:	// VDS_E_OBJECT_DELETED
		return "The object no longer exists.";
	case 0x8004240C:	// VDS_E_CANCEL_TOO_LATE
		return "The operation can no longer be canceled.";
	case 0x8004240D:	// VDS_E_OPERATION_CANCELED
		return "The operation has already been canceled.";
	case 0x8004240E:	// VDS_E_CANNOT_EXTEND
		return "The file system does not support extending this volume.";
	case 0x8004240F:	// VDS_E_NOT_ENOUGH_SPACE
		return "There is not enough space to complete the operation.";
	case 0x80042410:	// VDS_E_NOT_ENOUGH_DRIVE
		return "There are not enough free disk drives in the subsystem to complete the operation.";
	case 0x80042411:	// VDS_E_BAD_COOKIE
		return "The cookie was not found.";
	case 0x80042412:	// VDS_E_NO_MEDIA
		return "There is no removable media in the drive.";
	case 0x80042413:	// VDS_E_DEVICE_IN_USE
		return "The device is currently in use.";
	case 0x80042414:	// VDS_E_DISK_NOT_EMPTY
		return "The disk contains partitions or volumes.";
	case 0x80042415:	// VDS_E_INVALID_OPERATION
		return "The specified operation is not valid.";
	case 0x80042416:	// VDS_E_PATH_NOT_FOUND
		return "The specified path was not found.";
	case 0x80042417:	// VDS_E_DISK_NOT_INITIALIZED
		return "The specified disk has not been initialized.";
	case 0x80042418:	// VDS_E_NOT_AN_UNALLOCATED_DISK
		return "The specified disk is not an unallocated disk.";
	case 0x80042419:	// VDS_E_UNRECOVERABLE_ERROR
		return "An unrecoverable error occurred. The service MUST shut down.";
	case 0x0004241A:	// VDS_S_DISK_PARTIALLY_CLEANED
		return "The clean operation was not a full clean or was canceled before it could be completed.";
	case 0x8004241B:	// VDS_E_DMADMIN_SERVICE_CONNECTION_FAILED
		return "The provider failed to connect to the LDMA service.";
	case 0x8004241C:	// VDS_E_PROVIDER_INITIALIZATION_FAILED
		return "The provider failed to initialize.";
	case 0x8004241D:	// VDS_E_OBJECT_EXISTS
		return "The object already exists.";
	case 0x8004241E:	// VDS_E_NO_DISKS_FOUND
		return "No disks were found on the target machine.";
	case 0x8004241F:	// VDS_E_PROVIDER_CACHE_CORRUPT
		return "The cache for a provider is corrupt.";
	case 0x80042420:	// VDS_E_DMADMIN_METHOD_CALL_FAILED
		return "A method call to the LDMA service failed.";
	case 0x00042421:	// VDS_S_PROVIDER_ERROR_LOADING_CACHE
		return "The provider encountered errors while loading the cache.";
	case 0x80042422:	// VDS_E_PROVIDER_VOL_DEVICE_NAME_NOT_FOUND
		return "The device form of the volume pathname could not be retrieved.";
	case 0x80042423:	// VDS_E_PROVIDER_VOL_OPEN
		return "Failed to open the volume device";
	case 0x80042424:	// VDS_E_DMADMIN_CORRUPT_NOTIFICATION
		return "A corrupt notification was sent from the LDMA service.";
	case 0x80042425:	// VDS_E_INCOMPATIBLE_FILE_SYSTEM
		return "The file system is incompatible with the specified operation.";
	case 0x80042426:	// VDS_E_INCOMPATIBLE_MEDIA
		return "The media is incompatible with the specified operation.";
	case 0x80042427:	// VDS_E_ACCESS_DENIED
		return "Access is denied. A VDS operation MUST run elevated.";
	case 0x80042428:	// VDS_E_MEDIA_WRITE_PROTECTED
		return "The media is write-protected.";
	case 0x80042429:	// VDS_E_BAD_LABEL
		return "The volume label is not valid.";
	case 0x8004242A:	// VDS_E_CANT_QUICK_FORMAT
		return "The volume cannot be quick-formatted.";
	case 0x8004242B:	// VDS_E_IO_ERROR
		return "An I/O error occurred during the operation.";
	case 0x8004242C:	// VDS_E_VOLUME_TOO_SMALL
		return "The volume size is too small.";
	case 0x8004242D:	// VDS_E_VOLUME_TOO_BIG
		return "The volume size is too large.";
	case 0x8004242E:	// VDS_E_CLUSTER_SIZE_TOO_SMALL
		return "The cluster size is too small.";
	case 0x8004242F:	// VDS_E_CLUSTER_SIZE_TOO_BIG
		return "The cluster size is too large.";
	case 0x80042430:	// VDS_E_CLUSTER_COUNT_BEYOND_32BITS
		return "The number of clusters is too large to be represented as a 32-bit integer.";
	case 0x80042431:	// VDS_E_OBJECT_STATUS_FAILED
		return "The component that the object represents has failed.";
	case 0x80042432:	// VDS_E_VOLUME_INCOMPLETE
		return "The volume is incomplete.";
	case 0x80042433:	// VDS_E_EXTENT_SIZE_LESS_THAN_MIN
		return "The specified extent size is too small.";
	case 0x00042434:	// VDS_S_UPDATE_BOOTFILE_FAILED
		return "The operation was successful, but VDS failed to update the boot options.";
	case 0x00042436:	// VDS_S_BOOT_PARTITION_NUMBER_CHANGE
	case 0x80042436:	// VDS_E_BOOT_PARTITION_NUMBER_CHANGE
		return "The boot partition's partition number will change as a result of the operation.";
	case 0x80042437:	// VDS_E_NO_FREE_SPACE
		return "The specified disk does not have enough free space to complete the operation.";
	case 0x80042438:	// VDS_E_ACTIVE_PARTITION
		return "An active partition was detected on the selected disk.";
	case 0x80042439:	// VDS_E_PARTITION_OF_UNKNOWN_TYPE
		return "The partition information cannot be read.";
	case 0x8004243A:	// VDS_E_LEGACY_VOLUME_FORMAT
		return "A partition with an unknown type was detected on the specified disk.";
	case 0x8004243C:	// VDS_E_MIGRATE_OPEN_VOLUME
		return "A volume on the specified disk could not be opened.";
	case 0x8004243D:	// VDS_E_VOLUME_NOT_ONLINE
		return "The volume is not online.";
	case 0x8004243E:	// VDS_E_VOLUME_NOT_HEALTHY
		return "The volume is failing or has failed.";
	case 0x8004243F:	// VDS_E_VOLUME_SPANS_DISKS
		return "The volume spans multiple disks.";
	case 0x80042440:	// VDS_E_REQUIRES_CONTIGUOUS_DISK_SPACE
		return "The volume does not consist of a single disk extent.";
	case 0x80042441:	// VDS_E_BAD_PROVIDER_DATA
		return "A provider returned bad data.";
	case 0x80042442:	// VDS_E_PROVIDER_FAILURE
		return "A provider failed to complete an operation.";
	case 0x00042443:	// VDS_S_VOLUME_COMPRESS_FAILED
		return "The file system was formatted successfully but could not be compressed.";
	case 0x80042444:	// VDS_E_PACK_OFFLINE
		return "The pack is offline.";
	case 0x80042445:	// VDS_E_VOLUME_NOT_A_MIRROR
		return "The volume is not a mirror.";
	case 0x80042446:	// VDS_E_NO_EXTENTS_FOR_VOLUME
		return "No extents were found for the volume.";
	case 0x80042447:	// VDS_E_DISK_NOT_LOADED_TO_CACHE
		return "The migrated disk failed to load to the cache.";
	case 0x80042448:	// VDS_E_INTERNAL_ERROR
		return "VDS encountered an internal error.";
	case 0x8004244A:	// VDS_E_PROVIDER_TYPE_NOT_SUPPORTED
		return "The method call is not supported for the specified provider type.";
	case 0x8004244B:	// VDS_E_DISK_NOT_ONLINE
		return "One or more of the specified disks are not online.";
	case 0x8004244C:	// VDS_E_DISK_IN_USE_BY_VOLUME
		return "One or more extents of the disk are already being used by the volume.";
	case 0x0004244D:	// VDS_S_IN_PROGRESS
		return "The asynchronous operation is in progress.";
	case 0x8004244E:	// VDS_E_ASYNC_OBJECT_FAILURE
		return "Failure initializing the asynchronous object.";
	case 0x8004244F:	// VDS_E_VOLUME_NOT_MOUNTED
		return "The volume is not mounted.";
	case 0x80042450:	// VDS_E_PACK_NOT_FOUND
		return "The pack was not found.";
	case 0x80042453:	// VDS_E_OBJECT_OUT_OF_SYNC
		return "The reference to the object might be stale.";
	case 0x80042454:	// VDS_E_MISSING_DISK
		return "The specified disk could not be found.";
	case 0x80042455:	// VDS_E_DISK_PNP_REG_CORRUPT
		return "The provider's list of PnP registered disks has become corrupted.";
	case 0x80042457:	// VDS_E_NO_DRIVELETTER_FLAG
		return "The provider does not support the VDS_VF_NO DRIVELETTER volume flag.";
	case 0x80042459:	// VDS_E_REVERT_ON_CLOSE_SET
		return "Some volume flags are already set.";
	case 0x0004245B:	// VDS_S_UNABLE_TO_GET_GPT_ATTRIBUTES
		return "Unable to retrieve the GPT attributes for this volume.";
	case 0x8004245C:	// VDS_E_VOLUME_TEMPORARILY_DISMOUNTED
		return "The volume is already dismounted temporarily.";
	case 0x8004245D:	// VDS_E_VOLUME_PERMANENTLY_DISMOUNTED
		return "The volume is already permanently dismounted.";
	case 0x8004245E:	// VDS_E_VOLUME_HAS_PATH
		return "The volume cannot be dismounted permanently because it still has an access path.";
	case 0x8004245F:	// VDS_E_TIMEOUT
		return "The operation timed out.";
	case 0x80042461:	// VDS_E_LDM_TIMEOUT
		return "The operation timed out in the LDMA service. Retry the operation.";
	case 0x80042462:	// VDS_E_REVERT_ON_CLOSE_MISMATCH
		return "The flags to be cleared do not match the flags that were set previously.";
	case 0x80042463:	// VDS_E_RETRY
		return "The operation failed. Retry the operation.";
	case 0x80042464:	// VDS_E_ONLINE_PACK_EXISTS
		return "The operation failed, because an online pack object already exists.";
	case 0x80042468:	// VDS_E_MAX_USABLE_MBR
		return "Only the first 2TB are usable on large MBR disks.";
	case 0x80042500:	// VDS_E_NO_SOFTWARE_PROVIDERS_LOADED
		return "There are no software providers loaded.";
	case 0x80042501:	// VDS_E_DISK_NOT_MISSING
		return "The disk is not missing.";
	case 0x80042502:	// VDS_E_NO_VOLUME_LAYOUT
		return "The volume's layout could not be retrieved.";
	case 0x80042503:	// VDS_E_CORRUPT_VOLUME_INFO
		return "The volume's driver information is corrupted.";
	case 0x80042504:	// VDS_E_INVALID_ENUMERATOR
		return "The enumerator is corrupted";
	case 0x80042505:	// VDS_E_DRIVER_INTERNAL_ERROR
		return "An internal error occurred in the volume management driver.";
	case 0x80042507:	// VDS_E_VOLUME_INVALID_NAME
		return "The volume name is not valid.";
	case 0x00042508:	// VDS_S_DISK_IS_MISSING
		return "The disk is missing and not all information could be returned.";
	case 0x80042509:	// VDS_E_CORRUPT_PARTITION_INFO
		return "The disk's partition information is corrupted.";
	case 0x0004250A:	// VDS_S_NONCONFORMANT_PARTITION_INFO
		return "The disk's partition information does not conform to what is expected on a dynamic disk.";
	case 0x8004250B:	// VDS_E_CORRUPT_EXTENT_INFO
		return "The disk's extent information is corrupted.";
	case 0x0004250E:	// VDS_S_SYSTEM_PARTITION
		return "Warning: There was a failure while checking for the system partition.";
	case 0x8004250F:	// VDS_E_BAD_PNP_MESSAGE
		return "The PNP service sent a corrupted notification to the provider.";
	case 0x80042510:	// VDS_E_NO_PNP_DISK_ARRIVE
	case 0x80042511:	// VDS_E_NO_PNP_VOLUME_ARRIVE
		return "No disk/volume arrival notification was received.";
	case 0x80042512:	// VDS_E_NO_PNP_DISK_REMOVE
	case 0x80042513:	// VDS_E_NO_PNP_VOLUME_REMOVE
		return "No disk/volume removal notification was received.";
	case 0x80042514:	// VDS_E_PROVIDER_EXITING
		return "The provider is exiting.";
	case 0x00042517:	// VDS_S_NO_NOTIFICATION
		return "No volume arrival notification was received.";
	case 0x80042519:	// VDS_E_INVALID_DISK
		return "The specified disk is not valid.";
	case 0x8004251A:	// VDS_E_INVALID_PACK
		return "The specified disk pack is not valid.";
	case 0x8004251B:	// VDS_E_VOLUME_ON_DISK
		return "This operation is not allowed on disks with volumes.";
	case 0x8004251C:	// VDS_E_DRIVER_INVALID_PARAM
		return "The driver returned an invalid parameter error.";
	case 0x8004253D:	// VDS_E_DRIVER_OBJECT_NOT_FOUND
		return "The object was not found in the driver cache.";
	case 0x8004253E:	// VDS_E_PARTITION_NOT_CYLINDER_ALIGNED
		return "The disk layout contains partitions which are not cylinder aligned.";
	case 0x8004253F:	// VDS_E_DISK_LAYOUT_PARTITIONS_TOO_SMALL
		return "The disk layout contains partitions which are less than the minimum required size.";
	case 0x80042540:	// VDS_E_DISK_IO_FAILING
		return "The I/O to the disk is failing.";
	case 0x80042543:	// VDS_E_GPT_ATTRIBUTES_INVALID
		return "Invalid GPT attributes were specified.";
	case 0x8004254D:	// VDS_E_UNEXPECTED_DISK_LAYOUT_CHANGE
		return "An unexpected layout change occurred external to the volume manager.";
	case 0x8004254E:	// VDS_E_INVALID_VOLUME_LENGTH
		return "The volume length is invalid.";
	case 0x8004254F:	// VDS_E_VOLUME_LENGTH_NOT_SECTOR_SIZE_MULTIPLE
		return "The volume length is not a multiple of the sector size.";
	case 0x80042550:	// VDS_E_VOLUME_NOT_RETAINED
		return "The volume does not have a retained partition association.";
	case 0x80042551:	// VDS_E_VOLUME_RETAINED
		return "The volume already has a retained partition association.";
	case 0x80042553:	// VDS_E_ALIGN_BEYOND_FIRST_CYLINDER
		return "The specified alignment is beyond the first cylinder.";
	case 0x80042554:	// VDS_E_ALIGN_NOT_SECTOR_SIZE_MULTIPLE
		return "The specified alignment is not a multiple of the sector size.";
	case 0x80042555:	// VDS_E_ALIGN_NOT_ZERO
		return "The specified partition type cannot be created with a non-zero alignment.";
	case 0x80042556:	// VDS_E_CACHE_CORRUPT
		return "The service's cache has become corrupt.";
	case 0x80042557:	// VDS_E_CANNOT_CLEAR_VOLUME_FLAG
		return "The specified volume flag cannot be cleared.";
	case 0x80042558:	// VDS_E_DISK_BEING_CLEANED
		return "The operation is not allowed on a disk that is in the process of being cleaned.";
	case 0x8004255A:	// VDS_E_DISK_REMOVEABLE
		return "The operation is not supported on removable media.";
	case 0x8004255B:	// VDS_E_DISK_REMOVEABLE_NOT_EMPTY
		return "The operation is not supported on a non-empty removable disk.";
	case 0x8004255C:	// VDS_E_DRIVE_LETTER_NOT_FREE
		return "The specified drive letter is not free to be assigned.";
	case 0x8004255E:	// VDS_E_INVALID_DRIVE_LETTER
		return "The specified drive letter is not valid.";
	case 0x8004255F:	// VDS_E_INVALID_DRIVE_LETTER_COUNT
		return "The specified number of drive letters to retrieve is not valid.";
	case 0x80042560:	// VDS_E_INVALID_FS_FLAG
		return "The specified file system flag is not valid.";
	case 0x80042561:	// VDS_E_INVALID_FS_TYPE
		return "The specified file system is not valid.";
	case 0x80042562:	// VDS_E_INVALID_OBJECT_TYPE
		return "The specified object type is not valid.";
	case 0x80042563:	// VDS_E_INVALID_PARTITION_LAYOUT
		return "The specified partition layout is invalid.";
	case 0x80042564:	// VDS_E_INVALID_PARTITION_STYLE
		return "VDS only supports MBR or GPT partition style disks.";
	case 0x80042565:	// VDS_E_INVALID_PARTITION_TYPE
		return "The specified partition type is not valid for this operation.";
	case 0x80042566:	// VDS_E_INVALID_PROVIDER_CLSID
	case 0x80042567:	// VDS_E_INVALID_PROVIDER_ID
	case 0x8004256A:	// VDS_E_INVALID_PROVIDER_VERSION_GUID
		return "A NULL GUID was passed to the provider.";
	case 0x80042568:	// VDS_E_INVALID_PROVIDER_NAME
		return "The specified provider name is invalid.";
	case 0x80042569:	// VDS_E_INVALID_PROVIDER_TYPE
		return "The specified provider type is invalid.";
	case 0x8004256B:	// VDS_E_INVALID_PROVIDER_VERSION_STRING
		return "The specified provider version string is invalid.";
	case 0x8004256C:	// VDS_E_INVALID_QUERY_PROVIDER_FLAG
		return "The specified query provider flag is invalid.";
	case 0x8004256D:	// VDS_E_INVALID_SERVICE_FLAG
		return "The specified service flag is invalid.";
	case 0x8004256E:	// VDS_E_INVALID_VOLUME_FLAG
		return "The specified volume flag is invalid.";
	case 0x8004256F:	// VDS_E_PARTITION_NOT_OEM
		return "The operation is only supported on an OEM, ESP, or unknown partition.";
	case 0x80042570:	// VDS_E_PARTITION_PROTECTED
		return "Cannot delete a protected partition without the force protected parameter set.";
	case 0x80042571:	// VDS_E_PARTITION_STYLE_MISMATCH
		return "The specified partition style is not the same as the disk's partition style.";
	case 0x80042572:	// VDS_E_PROVIDER_INTERNAL_ERROR
		return "An internal error has occurred in the provider.";
	case 0x80042575:	// VDS_E_UNRECOVERABLE_PROVIDER_ERROR
		return "An unrecoverable error occurred in the provider.";
	case 0x80042576:	// VDS_E_VOLUME_HIDDEN
		return "Cannot assign a mount point to a hidden volume.";
	case 0x00042577:	// VDS_S_DISMOUNT_FAILED
	case 0x00042578:	// VDS_S_REMOUNT_FAILED
		return "Failed to dismount/remount the volume after setting the volume flags.";
	case 0x80042579:	// VDS_E_FLAG_ALREADY_SET
		return "Cannot set the specified flag as revert-on-close because it is already set.";
	case 0x8004257B:	// VDS_E_DISTINCT_VOLUME
		return "The input volume id cannot be the id of the volume that is the target of the operation.";
	case 0x00042583:	// VDS_S_FS_LOCK
		return "Failed to obtain a file system lock.";
	case 0x80042584:	// VDS_E_READONLY
		return "The volume is read only.";
	case 0x80042585:	// VDS_E_INVALID_VOLUME_TYPE
		return "The volume type is invalid for this operation.";
	case 0x80042588:	// VDS_E_VOLUME_MIRRORED
		return "This operation is not supported on a mirrored volume.";
	case 0x80042589:	// VDS_E_VOLUME_SIMPLE_SPANNED
		return "The operation is only supported on simple or spanned volumes.";
	case 0x8004258C:	// VDS_E_PARTITION_MSR
	case 0x8004258D:	// VDS_E_PARTITION_LDM
		return "The operation is not supported on this type of partitions.";
	case 0x0004258E:	// VDS_S_WINPE_BOOTENTRY
		return "The boot entries cannot be updated automatically on WinPE.";
	case 0x8004258F:	// VDS_E_ALIGN_NOT_A_POWER_OF_TWO
		return "The alignment is not a power of two.";
	case 0x80042590:	// VDS_E_ALIGN_IS_ZERO
		return "The alignment is zero.";
	case 0x80042593:	// VDS_E_FS_NOT_DETERMINED
		return "The default file system could not be determined.";
	case 0x80042595:	// VDS_E_DISK_NOT_OFFLINE
		return "This disk is already online.";
	case 0x80042596:	// VDS_E_FAILED_TO_ONLINE_DISK
		return "The online operation failed.";
	case 0x80042597:	// VDS_E_FAILED_TO_OFFLINE_DISK
		return "The offline operation failed.";
	case 0x80042598:	// VDS_E_BAD_REVISION_NUMBER
		return "The operation could not be completed because the specified revision number is not supported.";
	case 0x00042700:	// VDS_S_NAME_TRUNCATED
		return "The name was set successfully but had to be truncated.";
	case 0x80042701:	// VDS_E_NAME_NOT_UNIQUE
		return "The specified name is not unique.";
	case 0x8004270F:	// VDS_E_NO_DISK_PATHNAME
		return "The disk's path could not be retrieved. Some operations on the disk might fail.";
	case 0x80042711:	// VDS_E_NO_VOLUME_PATHNAME
		return "The path could not be retrieved for one or more volumes.";
	case 0x80042712:	// VDS_E_PROVIDER_CACHE_OUTOFSYNC
		return "The provider's cache is not in sync with the driver cache.";
	case 0x80042713:	// VDS_E_NO_IMPORT_TARGET
		return "No import target was set for the subsystem.";
	case 0x00042714:	// VDS_S_ALREADY_EXISTS
		return "The object already exists.";
	case 0x00042715:	// VDS_S_PROPERTIES_INCOMPLETE
		return "Some, but not all, of the properties were successfully retrieved.";
	case 0x80042803:	// VDS_E_UNABLE_TO_FIND_BOOT_DISK
		return "Volume disk extent information could not be retrieved for the boot volume.";
	case 0x80042807:	// VDS_E_BOOT_DISK
		return "Disk attributes cannot be changed on the boot disk.";
	case 0x00042808:	// VDS_S_DISK_MOUNT_FAILED
	case 0x00042809:	// VDS_S_DISK_DISMOUNT_FAILED
		return "One or more of the volumes on the disk could not be mounted/dismounted.";
	case 0x8004280A:	// VDS_E_DISK_IS_OFFLINE
	case 0x8004280B:	// VDS_E_DISK_IS_READ_ONLY
		return "The operation cannot be performed on a disk that is offline or read-only.";
	case 0x8004280C:	// VDS_E_PAGEFILE_DISK
	case 0x8004280D:	// VDS_E_HIBERNATION_FILE_DISK
	case 0x8004280E:	// VDS_E_CRASHDUMP_DISK
		return "The operation cannot be performed on a disk that contains a pagefile, hibernation or crashdump volume.";
	case 0x8004280F:	// VDS_E_UNABLE_TO_FIND_SYSTEM_DISK
		return "A system error occurred while retrieving the system disk information.";
	case 0x80042810:	// VDS_E_INCORRECT_SYSTEM_VOLUME_EXTENT_INFO
		return "Multiple disk extents reported for the system volume - system error.";
	case 0x80042811:	// VDS_E_SYSTEM_DISK
		return "Disk attributes cannot be changed on the current system disk or BIOS disk 0.";
	case 0x80042823:	// VDS_E_SECTOR_SIZE_ERROR
		return "The sector size MUST be non-zero, a power of 2, and less than the maximum sector size.";
	case 0x80042907:	// VDS_E_SUBSYSTEM_ID_IS_NULL
		return "The provider returned a NULL subsystem identification string.";
	case 0x8004290C:	// VDS_E_REBOOT_REQUIRED
		return "A reboot is required before any further operations are initiated.";
	case 0x8004290D:	// VDS_E_VOLUME_GUID_PATHNAME_NOT_ALLOWED
		return "Volume GUID pathnames are not valid input to this method.";
	case 0x8004290E:	// VDS_E_BOOT_PAGEFILE_DRIVE_LETTER
		return "Assigning or removing drive letters on the current boot or pagefile volume is not allowed.";
	case 0x8004290F:	// VDS_E_DELETE_WITH_CRITICAL
		return "Delete is not allowed on a critical volume.";
	case 0x80042910:	// VDS_E_CLEAN_WITH_DATA
	case 0x80042911:	// VDS_E_CLEAN_WITH_OEM
		return "The FORCE parameter MUST be set to TRUE in order to clean a disk that contains a data or OEM volume.";
	case 0x80042912:	// VDS_E_CLEAN_WITH_CRITICAL
		return "Clean is not allowed on a critical disk.";
	case 0x80042913:	// VDS_E_FORMAT_CRITICAL
		return "Format is not allowed on a critical volume.";
	case 0x80042914:	// VDS_E_NTFS_FORMAT_NOT_SUPPORTED
	case 0x80042915:	// VDS_E_FAT32_FORMAT_NOT_SUPPORTED
	case 0x80042916:	// VDS_E_FAT_FORMAT_NOT_SUPPORTED
		return "The requested file system format is not supported on this volume.";
	case 0x80042917:	// VDS_E_FORMAT_NOT_SUPPORTED
		return "The volume is not formattable.";
	case 0x80042918:	// VDS_E_COMPRESSION_NOT_SUPPORTED
		return "The specified file system does not support compression.";
	default:
		return NULL;
	}
}

// Convert a windows error to human readable string
const char *WindowsErrorString(void)
{
	static char err_string[256] = { 0 };

	DWORD size, presize;
	DWORD error_code, format_error;

	error_code = GetLastError();
	// Check for VDS error codes
	if ((SCODE_FACILITY(error_code) == FACILITY_ITF) && (GetVdsError(error_code) != NULL)) {
		static_sprintf(err_string, "[0x%08lX] %s", error_code, GetVdsError(error_code));
		return err_string;
	}

	static_sprintf(err_string, "[0x%08lX] ", error_code);
	presize = (DWORD)strlen(err_string);

	size = FormatMessageU(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
		HRESULT_CODE(error_code), MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
		&err_string[presize], sizeof(err_string)-(DWORD)strlen(err_string), NULL);
	if (size == 0) {
		format_error = GetLastError();
		if ((format_error) && (format_error != 0x13D))		// 0x13D, decode error, is returned for unknown codes
			static_sprintf(err_string, "Windows error code 0x%08lX (FormatMessage error code 0x%08lX)",
				error_code, format_error);
		else
			static_sprintf(err_string, "Unknown error 0x%08lX", error_code);
	} else {
		// Microsoft may suffix CRLF to error messages, which we need to remove...
		assert(presize > 2);
		size += presize - 2;
		// Cannot underflow if the above assert passed since our first char is neither of the following
		while ((err_string[size] == 0x0D) || (err_string[size] == 0x0A) || (err_string[size] == 0x20))
			err_string[size--] = 0;
	}

	SetLastError(error_code);	// Make sure we don't change the errorcode on exit
	return err_string;
}

char* GuidToString(const GUID* guid)
{
	static char guid_string[MAX_GUID_STRING_LENGTH];

	if (guid == NULL) return NULL;
	sprintf(guid_string, "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
		(uint32_t)guid->Data1, guid->Data2, guid->Data3,
		guid->Data4[0], guid->Data4[1], guid->Data4[2], guid->Data4[3],
		guid->Data4[4], guid->Data4[5], guid->Data4[6], guid->Data4[7]);
	return guid_string;
}

GUID* StringToGuid(const char* str)
{
	static GUID guid;

	if (str == NULL) return NULL;
	if (sscanf(str, "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
		(uint32_t*)&guid.Data1, (uint32_t*)&guid.Data2, (uint32_t*)&guid.Data3,
		(uint32_t*)&guid.Data4[0], (uint32_t*)&guid.Data4[1], (uint32_t*)&guid.Data4[2],
		(uint32_t*)&guid.Data4[3], (uint32_t*)&guid.Data4[4], (uint32_t*)&guid.Data4[5],
		(uint32_t*)&guid.Data4[6], (uint32_t*)&guid.Data4[7]) != 11)
		return NULL;
	return &guid;
}

// Find upper power of 2
static __inline uint16_t upo2(uint16_t v)
{
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v++;
	return v;
}

// Convert a size to human readable
char* SizeToHumanReadable(uint64_t size, BOOL copy_to_log, BOOL fake_units)
{
	int suffix;
	static char str_size[32];
	const char* dir = ((right_to_left_mode) && (!copy_to_log)) ? LEFT_TO_RIGHT_MARK : "";
	double hr_size = (double)size;
	double t;
	uint16_t i_size;
	char **_msg_table = copy_to_log?default_msg_table:msg_table;
	const double divider = fake_units?1000.0:1024.0;

	for (suffix=0; suffix<MAX_SIZE_SUFFIXES-1; suffix++) {
		if (hr_size < divider)
			break;
		hr_size /= divider;
	}
	if (suffix == 0) {
		static_sprintf(str_size, "%s%d%s %s", dir, (int)hr_size, dir, _msg_table[MSG_020-MSG_000]);
	} else if (fake_units) {
		if (hr_size < 8) {
			static_sprintf(str_size, (fabs((hr_size*10.0)-(floor(hr_size + 0.5)*10.0)) < 0.5)?"%0.0f%s":"%0.1f%s",
				hr_size, _msg_table[MSG_020+suffix-MSG_000]);
		} else {
			t = (double)upo2((uint16_t)hr_size);
			i_size = (uint16_t)((fabs(1.0f-(hr_size / t)) < 0.05f)?t:hr_size);
			static_sprintf(str_size, "%s%d%s %s", dir, i_size, dir, _msg_table[MSG_020+suffix-MSG_000]);
		}
	} else {
		static_sprintf(str_size, (hr_size * 10.0 - (floor(hr_size) * 10.0)) < 0.5?
			"%s%0.0f%s %s":"%s%0.1f%s %s", dir, hr_size, dir, _msg_table[MSG_020+suffix-MSG_000]);
	}
	return str_size;
}

// Convert a YYYYMMDDHHMMSS UTC timestamp to a more human readable version
char* TimestampToHumanReadable(uint64_t ts)
{
	uint64_t rem = ts, divisor = 10000000000ULL;
	uint16_t data[6];
	int i;
	static char str[64];

	for (i = 0; i < 6; i++) {
		data[i] = (uint16_t) ((divisor == 0)?rem:(rem / divisor));
		rem %= divisor;
		divisor /= 100ULL;
	}
	static_sprintf(str, "%04d.%02d.%02d %02d:%02d:%02d (UTC)", data[0], data[1], data[2], data[3], data[4], data[5]);
	return str;
}

// Convert custom error code to messages
const char* _StrError(DWORD error_code)
{
	if ( (!IS_ERROR(error_code)) || (SCODE_CODE(error_code) == ERROR_SUCCESS)) {
		return lmprintf(MSG_050);
	}
	if (SCODE_FACILITY(error_code) != FACILITY_STORAGE) {
		SetLastError(error_code);
		return WindowsErrorString();
	}
	switch (SCODE_CODE(error_code)) {
	case ERROR_GEN_FAILURE:
		return lmprintf(MSG_051);
	case ERROR_INCOMPATIBLE_FS:
		return lmprintf(MSG_052);
	case ERROR_ACCESS_DENIED:
		return lmprintf(MSG_053);
	case ERROR_WRITE_PROTECT:
		return lmprintf(MSG_054);
	case ERROR_DEVICE_IN_USE:
		return lmprintf(MSG_055);
	case ERROR_CANT_QUICK_FORMAT:
		return lmprintf(MSG_056);
	case ERROR_LABEL_TOO_LONG:
		return lmprintf(MSG_057);
	case ERROR_INVALID_HANDLE:
		return lmprintf(MSG_058);
	case ERROR_INVALID_CLUSTER_SIZE:
		return lmprintf(MSG_059);
	case ERROR_INVALID_VOLUME_SIZE:
		return lmprintf(MSG_060);
	case ERROR_NO_MEDIA_IN_DRIVE:
		return lmprintf(MSG_061);
	case ERROR_NOT_SUPPORTED:
		return lmprintf(MSG_062);
	case ERROR_NOT_ENOUGH_MEMORY:
		return lmprintf(MSG_063);
	case ERROR_READ_FAULT:
		return lmprintf(MSG_064);
	case ERROR_WRITE_FAULT:
		return lmprintf(MSG_065);
	case ERROR_INSTALL_FAILURE:
		return lmprintf(MSG_066);
	case ERROR_OPEN_FAILED:
		return lmprintf(MSG_067);
	case ERROR_PARTITION_FAILURE:
		return lmprintf(MSG_068);
	case ERROR_CANNOT_COPY:
		return lmprintf(MSG_069);
	case ERROR_CANCELLED:
		return lmprintf(MSG_070);
	case ERROR_CANT_START_THREAD:
		return lmprintf(MSG_071);
	case ERROR_BADBLOCKS_FAILURE:
		return lmprintf(MSG_072);
	case ERROR_ISO_SCAN:
		return lmprintf(MSG_073);
	case ERROR_ISO_EXTRACT:
		return lmprintf(MSG_074);
	case ERROR_CANT_REMOUNT_VOLUME:
		return lmprintf(MSG_075);
	case ERROR_CANT_PATCH:
		return lmprintf(MSG_076);
	case ERROR_CANT_ASSIGN_LETTER:
		return lmprintf(MSG_077);
	case ERROR_CANT_MOUNT_VOLUME:
		return lmprintf(MSG_078);
	case ERROR_NOT_READY:
		return lmprintf(MSG_079);
	case ERROR_BAD_SIGNATURE:
		return lmprintf(MSG_172);
	case ERROR_CANT_DOWNLOAD:
		return lmprintf(MSG_242);
	default:
		SetLastError(error_code);
		return WindowsErrorString();
	}
}

const char* StrError(DWORD error_code, BOOL use_default_locale)
{
	const char* ret;
	if (use_default_locale)
		toggle_default_locale();
	ret = _StrError(error_code);
	if (use_default_locale)
		toggle_default_locale();
	return ret;
}

// A WriteFile() equivalent, with up to nNumRetries write attempts on error.
BOOL WriteFileWithRetry(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite,
	LPDWORD lpNumberOfBytesWritten, DWORD nNumRetries)
{
	DWORD nTry;
	BOOL readFilePointer;
	LARGE_INTEGER liFilePointer, liZero = { { 0,0 } };

	// Need to get the current file pointer in case we need to retry
	readFilePointer = SetFilePointerEx(hFile, liZero, &liFilePointer, FILE_CURRENT);
	if (!readFilePointer)
		uprintf("Warning: Could not read file pointer %s", WindowsErrorString());

	if (nNumRetries == 0)
		nNumRetries = 1;
	for (nTry = 1; nTry <= nNumRetries; nTry++) {
		// Need to rewind our file position on retry - if we can't even do that, just give up
		if ((nTry > 1) && (!SetFilePointerEx(hFile, liFilePointer, NULL, FILE_BEGIN))) {
			uprintf("Could not set file pointer - Aborting");
			break;
		}
		if (WriteFile(hFile, lpBuffer, nNumberOfBytesToWrite, lpNumberOfBytesWritten, NULL)) {
			LastWriteError = 0;
			if (nNumberOfBytesToWrite == *lpNumberOfBytesWritten)
				return TRUE;
			// Some large drives return 0, even though all the data was written - See github #787 */
			if (large_drive && (*lpNumberOfBytesWritten == 0)) {
				uprintf("Warning: Possible short write");
				return TRUE;
			}
			uprintf("Wrote %d bytes but requested %d", *lpNumberOfBytesWritten, nNumberOfBytesToWrite);
		} else {
			uprintf("Write error %s", WindowsErrorString());
			LastWriteError = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|GetLastError();
		}
		// If we can't reposition for the next run, just abort
		if (!readFilePointer)
			break;
		if (nTry < nNumRetries) {
			uprintf("Retrying in %d seconds...", WRITE_TIMEOUT / 1000);
			// Don't sit idly but use the downtime to check for conflicting processes...
			Sleep(CheckDriveAccess(WRITE_TIMEOUT, FALSE));
		}
	}
	if (SCODE_CODE(GetLastError()) == ERROR_SUCCESS)
		SetLastError(ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_WRITE_FAULT);
	return FALSE;
}

// A WaitForSingleObject() equivalent that doesn't block Windows messages
// This is needed, for instance, if you are waiting for a thread that may issue uprintf's
DWORD WaitForSingleObjectWithMessages(HANDLE hHandle, DWORD dwMilliseconds)
{
	uint64_t CurTime, EndTime = GetTickCount64() + dwMilliseconds;
	DWORD res;
	MSG msg;

	do {
		// Read all of the messages in this next loop, removing each message as we read it.
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			if ((msg.message == WM_QUIT) || (msg.message == WM_CLOSE)) {
				SetLastError(ERROR_CANCELLED);
				return WAIT_FAILED;
			} else {
				DispatchMessage(&msg);
			}
		}

		// Wait for any message sent or posted to this queue or for the handle to signaled.
		res = MsgWaitForMultipleObjects(1, &hHandle, FALSE, dwMilliseconds, QS_ALLINPUT);

		if (dwMilliseconds != INFINITE) {
			CurTime = GetTickCount64();
			// Account for the case where we may reach the timeout condition while
			// processing timestamps
			if (CurTime < EndTime)
				dwMilliseconds = (DWORD) (EndTime - CurTime);
			else
				res = WAIT_TIMEOUT;
		}
	} while (res == (WAIT_OBJECT_0 + 1));

	return res;
}

#define STATUS_SUCCESS					((NTSTATUS)0x00000000L)
#define STATUS_NOT_IMPLEMENTED			((NTSTATUS)0xC0000002L)
#define FILE_ATTRIBUTE_VALID_FLAGS		0x00007FB7
#define NtCurrentPeb()					(NtCurrentTeb()->ProcessEnvironmentBlock)
#define RtlGetProcessHeap()				(NtCurrentPeb()->Reserved4[1]) // NtCurrentPeb()->ProcessHeap, mangled due to deficiencies in winternl.h

PF_TYPE_DECL(NTAPI, NTSTATUS, NtCreateFile, (PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG));
PF_TYPE_DECL(NTAPI, BOOLEAN, RtlDosPathNameToNtPathNameW, (PCWSTR, PUNICODE_STRING, PWSTR*, PVOID));
PF_TYPE_DECL(NTAPI, BOOLEAN, RtlFreeHeap, (PVOID, ULONG, PVOID));
PF_TYPE_DECL(NTAPI, VOID, RtlSetLastWin32ErrorAndNtStatusFromNtStatus, (NTSTATUS));

HANDLE CreatePreallocatedFile(const char* lpFileName, DWORD dwDesiredAccess,
	DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
	DWORD dwFlagsAndAttributes, LONGLONG fileSize)
{
	HANDLE fileHandle = INVALID_HANDLE_VALUE;
	OBJECT_ATTRIBUTES objectAttributes;
	IO_STATUS_BLOCK ioStatusBlock;
	UNICODE_STRING ntPath;
	ULONG fileAttributes, flags = 0;
	LARGE_INTEGER allocationSize;
	NTSTATUS status = STATUS_SUCCESS;

	PF_INIT_OR_SET_STATUS(NtCreateFile, Ntdll);
	PF_INIT_OR_SET_STATUS(RtlDosPathNameToNtPathNameW, Ntdll);
	PF_INIT_OR_SET_STATUS(RtlFreeHeap, Ntdll);
	PF_INIT_OR_SET_STATUS(RtlSetLastWin32ErrorAndNtStatusFromNtStatus, Ntdll);

	if (!NT_SUCCESS(status)) {
		return CreateFileU(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
			dwCreationDisposition, dwFlagsAndAttributes, NULL);
	}

	wconvert(lpFileName);

	// Determine creation disposition and flags
	switch (dwCreationDisposition) {
	case CREATE_NEW:
		dwCreationDisposition = FILE_CREATE;
		break;
	case CREATE_ALWAYS:
		dwCreationDisposition = FILE_OVERWRITE_IF;
		break;
	case OPEN_EXISTING:
		dwCreationDisposition = FILE_OPEN;
		break;
	case OPEN_ALWAYS:
		dwCreationDisposition = FILE_OPEN_IF;
		break;
	case TRUNCATE_EXISTING:
		dwCreationDisposition = FILE_OVERWRITE;
		break;
	default:
		SetLastError(ERROR_INVALID_PARAMETER);
		return INVALID_HANDLE_VALUE;
	}

	if ((dwFlagsAndAttributes & FILE_FLAG_OVERLAPPED) == 0)
		flags |= FILE_SYNCHRONOUS_IO_NONALERT;

	if ((dwFlagsAndAttributes & FILE_FLAG_WRITE_THROUGH) != 0)
		flags |= FILE_WRITE_THROUGH;

	if ((dwFlagsAndAttributes & FILE_FLAG_NO_BUFFERING) != 0)
		flags |= FILE_NO_INTERMEDIATE_BUFFERING;

	if ((dwFlagsAndAttributes & FILE_FLAG_RANDOM_ACCESS) != 0)
		flags |= FILE_RANDOM_ACCESS;

	if ((dwFlagsAndAttributes & FILE_FLAG_SEQUENTIAL_SCAN) != 0)
		flags |= FILE_SEQUENTIAL_ONLY;

	if ((dwFlagsAndAttributes & FILE_FLAG_DELETE_ON_CLOSE) != 0) {
		flags |= FILE_DELETE_ON_CLOSE;
		dwDesiredAccess |= DELETE;
	}

	if ((dwFlagsAndAttributes & FILE_FLAG_BACKUP_SEMANTICS) != 0) {
		if ((dwDesiredAccess & GENERIC_ALL) != 0)
			flags |= (FILE_OPEN_FOR_BACKUP_INTENT | FILE_OPEN_REMOTE_INSTANCE);
		else {
			if ((dwDesiredAccess & GENERIC_READ) != 0)
				flags |= FILE_OPEN_FOR_BACKUP_INTENT;

			if ((dwDesiredAccess & GENERIC_WRITE) != 0)
				flags |= FILE_OPEN_REMOTE_INSTANCE;
		}
	} else {
		flags |= FILE_NON_DIRECTORY_FILE;
	}

	if ((dwFlagsAndAttributes & FILE_FLAG_OPEN_REPARSE_POINT) != 0)
		flags |= FILE_OPEN_REPARSE_POINT;

	if ((dwFlagsAndAttributes & FILE_FLAG_OPEN_NO_RECALL) != 0)
		flags |= FILE_OPEN_NO_RECALL;

	fileAttributes = dwFlagsAndAttributes & (FILE_ATTRIBUTE_VALID_FLAGS & ~FILE_ATTRIBUTE_DIRECTORY);

	dwDesiredAccess |= (SYNCHRONIZE | FILE_READ_ATTRIBUTES);

	// Convert DOS path to NT format
	if (!pfRtlDosPathNameToNtPathNameW(wlpFileName, &ntPath, NULL, NULL)) {
		wfree(lpFileName);
		SetLastError(ERROR_FILE_NOT_FOUND);
		return INVALID_HANDLE_VALUE;
	}

	InitializeObjectAttributes(&objectAttributes, &ntPath, 0, NULL, NULL);

	if (lpSecurityAttributes != NULL) {
		if (lpSecurityAttributes->bInheritHandle)
			objectAttributes.Attributes |= OBJ_INHERIT;
		objectAttributes.SecurityDescriptor = lpSecurityAttributes->lpSecurityDescriptor;
	}

	if ((dwFlagsAndAttributes & FILE_FLAG_POSIX_SEMANTICS) == 0)
		objectAttributes.Attributes |= OBJ_CASE_INSENSITIVE;

	allocationSize.QuadPart = fileSize;

	// Call NtCreateFile
	status = pfNtCreateFile(&fileHandle, dwDesiredAccess, &objectAttributes, &ioStatusBlock,
		&allocationSize, fileAttributes, dwShareMode, dwCreationDisposition, flags, NULL, 0);

	pfRtlFreeHeap(RtlGetProcessHeap(), 0, ntPath.Buffer);
	wfree(lpFileName);
	pfRtlSetLastWin32ErrorAndNtStatusFromNtStatus(status);

	return fileHandle;
}
