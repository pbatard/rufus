/*
 * Rufus: The Resourceful USB Formatting Utility
 * Standard I/O Routines (logging, status, etc.)
 * Copyright (c) 2011 Pete Batard <pete@akeo.ie>
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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "msapi_utf8.h"
#include "rufus.h"
#include "resource.h"

/*
 * Globals
 */
HWND hStatus;

#ifdef RUFUS_DEBUG
void _uprintf(const char *format, ...)
{
	char buf[4096], *p = buf;
	va_list args;
	int n;

	va_start(args, format);
	n = safe_vsnprintf(p, sizeof(buf)-3, format, args); // buf-3 is room for CR/LF/NUL
	va_end(args);

	p += (n < 0)?sizeof(buf)-3:n;

	while((p>buf) && (isspace(p[-1])))
		*--p = '\0';

	*p++ = '\r';
	*p++ = '\n';
	*p   = '\0';

	OutputDebugStringA(buf);
}
#endif

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

/*
 * Convert a windows error to human readable string
 * uses retval as errorcode, or, if 0, use GetLastError()
 */
char *WindowsErrorString(void)
{
static char err_string[256];

	DWORD size;
	DWORD error_code, format_error;

	error_code = GetLastError();

	safe_sprintf(err_string, sizeof(err_string), "[%d] ", error_code);

	size = FormatMessageU(FORMAT_MESSAGE_FROM_SYSTEM, NULL, error_code,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), &err_string[strlen(err_string)],
		sizeof(err_string)-(DWORD)strlen(err_string), NULL);
	if (size == 0) {
		format_error = GetLastError();
		if (format_error)
			safe_sprintf(err_string, sizeof(err_string),
				"Windows error code %u (FormatMessage error code %u)", error_code, format_error);
		else
			safe_sprintf(err_string, sizeof(err_string), "Unknown error code %u", error_code);
	}
	return err_string;
}

void PrintStatus(const char *format, ...)
{
	char buf[256], *p = buf;
	va_list args;
	int n;

	va_start(args, format);
	n = safe_vsnprintf(p, sizeof(buf)-1, format, args); // room for NUL
	va_end(args);

	p += (n < 0)?sizeof(buf)-1:n;

	while((p>buf) && (isspace(p[-1])))
		*--p = '\0';

	*p   = '\0';

	SetDlgItemTextU(hMainDialog, IDC_STATUS, buf);
}
