/*
 * Rufus: The Reliable USB Formatting Utility
 * Elementary Unicode compliant find/replace parser
 * Copyright © 2012-2013 Pete Batard <pete@akeo.ie>
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

/* Memory leaks detection - define _CRTDBG_MAP_ALLOC as preprocessor macro */
#ifdef _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include <malloc.h>
#include <io.h>
#include <fcntl.h>

#include "rufus.h"
#include "msapi_utf8.h"


// What we need for localization
// # Comment
// v 1 1                    // UI target version (major, minor)
// p IDD_DIALOG             // parent dialog for the following
// d 1                      // set text direction 0: left to right, 1 right to left
// f "MS Dialog" 12         // set font and font size
// r IDD_DIALOG +30 +30     // resize dialog (delta_w, delta_h)
// m IDC_START  -10 0       // move control (delta_x, delta_w)
// r IDC_START  0 +1        // resize control
// t IDC_START  "Demarrer"  // Change control text
// t IDC_LONG_CONTROL "Some text here"
//  "some continued text there"
// all parsed commands return: cmd, control_id, text, num1, num2

// TODO: display control name on mouseover
// Link to http://www.resedit.net/

typedef struct localization_command {
	int command;
	wchar_t* text1;
	wchar_t* text2;
	int32_t num1;
	int32_t num2;
} loc_cmd;

int  loc_line_nr = 0;
char loc_filename[32];
#define luprintf(msg) uprintf("%s(%d): " msg "\n", loc_filename, loc_line_nr);

// Parse localization command arguments and fill a localization command structure
static void get_loc_data_args(char* arg_type, wchar_t* wline) {
	const wchar_t wspace[] = L" \t";
	size_t i, r, arg_index;
	char str[1024];	// Fot testing only
	long n;
	wchar_t *endptr, *expected_endptr;

	i = 0;
	for (arg_index=0; arg_type[arg_index] != 0; arg_index++) {
		// Skip leading spaces
		i += wcsspn(&wline[i], wspace);
		r = i;
		switch(arg_type[arg_index]) {
		case 's':	// quoted string
			// search leading quote
			if (wline[i++] != L'"') {
				luprintf("missing leading quote");
				return;
			}
			r = i;
			// locate ending quote
			while ((wline[i] != 0) && (wline[i] != L'"') || ((wline[i] == L'"') && (wline[i-1] == L'\\'))) {
				if ((wline[i] == L'"') && (wline[i-1] == L'\\')) {
					wcscpy_s(&wline[i-1], i+wcslen(&wline[i]), &wline[i]);
				} else {
					i++;
				}
			}
			if (wline[i] == 0) {
				luprintf("missing ending quote");
				return;
			}
			wline[i++] = 0;
			wchar_to_utf8_no_alloc(&wline[r], str, sizeof(str));
			uprintf("Got string: '%s'\n", str);
			break;
		case 'w':	// single word
			while ((wline[i] != 0) && (wline[i] != wspace[0]) && (wline[i] != wspace[1]))
				i++;
			if (wline[i] != 0)
				wline[i++] = 0;
			wchar_to_utf8_no_alloc(&wline[r], str, sizeof(str));
			uprintf("Got word: %s\n", str);
			break;
		case 'i':	// integer
			while ((wline[i] != 0) && (wline[i] != wspace[0]) && (wline[i] != wspace[1]))
				i++;
			expected_endptr = &wline[i];
			if (wline[i] != 0)
				wline[i++] = 0;
			n = wcstol(&wline[r], &endptr, 10);
			if (endptr != expected_endptr) {
				luprintf("could not read integer data");
				return;
			} else {
				uprintf("Got integer: %d\n", n);
			}
			break;
		default:
			uprintf("localization: unhandled arg_type '%c'\n", arg_type[arg_index]);
			break;
		}
	}
}

// Parse an UTF-16 localization command line
static void* get_loc_data_line(wchar_t* wline)
{
	const wchar_t wspace[] = L" \t";
	size_t i;
	wchar_t t;
	BOOLEAN quoteth = FALSE;
	char line[8192];

	if ((wline == NULL) || (wline[0] == 0))
		return NULL;

	i = 0;

	// Skip leading spaces
	i += wcsspn(&wline[i], wspace);

	// Read token (NUL character will be read if EOL)
	t = wline[i++];
	switch (t) {
	case 't':
		get_loc_data_args("ws", &wline[i]);
		return NULL;
	case 'f':
		get_loc_data_args("si", &wline[i]);
		return NULL;
	case 'r':
	case 'm':
		get_loc_data_args("wii", &wline[i]);
		return NULL;
	case 'v':
		get_loc_data_args("ii", &wline[i]);
		return NULL;
	case '#':	// comment
		return NULL;
	default:
		wchar_to_utf8_no_alloc(wline, line, sizeof(line));
		luprintf("syntax error");
		return NULL;
	}
}

static __inline void *_reallocf(void *ptr, size_t size)
{
	void *ret = realloc(ptr, size);
	if (!ret)
		free(ptr);
	return ret;
}

// Parse a Rufus localization command file (ANSI, UTF-8 or UTF-16)
char* get_loc_data_file(const char* filename)
{
	wchar_t *wdata= NULL, *wfilename = NULL, *wbuf = NULL;
	size_t wbufsize = 1024;	// size in wchar_t
	FILE* fd = NULL;
	char *ret = NULL;
	size_t i = 0;
	int r, line_nr_incr = 1;
	wchar_t wc = 0, last_wc;
	BOOL eol = FALSE;

	if ((filename == NULL) || (filename[0] == 0))
		return NULL;

	loc_line_nr = 0;
	safe_strcpy(loc_filename, sizeof(loc_filename), filename);
	wfilename = utf8_to_wchar(filename);
	if (wfilename == NULL) {
		uprintf("localization: could not convert '%s' filename to UTF-16\n", filename);
		goto out;
	}
	fd = _wfopen(wfilename, L"r, ccs=UNICODE");
	if (fd == NULL) goto out;

	wbuf = (wchar_t*) malloc(wbufsize*sizeof(wchar_t));
	if (wbuf == NULL) {
		uprintf("localization: could not allocate line buffer\n");
		goto out;
	}

	do {	// custom readline handling for string collation, realloc, line number handling, etc.
		last_wc = wc;
		wc = getwc(fd);
		switch(wc) {
		case WEOF:
			wbuf[i] = 0;
			get_loc_data_line(wbuf);
			goto out;
		case 0x0D:
		case 0x0A:
			// Process line numbers
			if ((last_wc != 0x0D) && (last_wc != 0x0A)) {
				if (eol) {
					line_nr_incr++;
				} else {
					loc_line_nr += line_nr_incr;
					line_nr_incr = 1;
				}
			}
			wbuf[i] = 0;
			if (!eol) {
				// Strip trailing spaces (for string collation)
				for (r = ((int)i)-1; (r>0) && ((wbuf[r]==0x20)||(wbuf[r]==0x09)); r--);
				if (r < 0)
					r = 0;
				eol = TRUE;
			}
			break;
		case 0x20:
		case 0x09:
			if (!eol) {
				wbuf[i++] = wc;
			}
			break;
		default:
			// Collate multiline strings
			if ((eol) && (wc == L'"') && (wbuf[r] == L'"')) {
				i = r;
				eol = FALSE;
				break;
			}
			if (eol) {
				get_loc_data_line(wbuf);
				eol = FALSE;
				i = 0;
				r = 0;
			}
			wbuf[i++] = wc;
			break;
		}
		if (i >= wbufsize-1) {
			wbufsize *= 2;
			if (wbufsize > 32768) {
				uprintf("localization: requested line buffer is larger than 32K!\n");
				goto out;
			}
			wbuf = (wchar_t*) _reallocf(wbuf, wbufsize*sizeof(wchar_t));
			if (wbuf == NULL) {
				uprintf("localization: could not grow line buffer\n");
				goto out;
			}
		}
	} while(1);

out:
	if (fd != NULL)
		fclose(fd);
	safe_free(wfilename);
	safe_free(wbuf);
	return ret;
}


// Parse a line of UTF-16 text and return the data if it matches the 'token'
// The parsed line is of the form: [ ]token[ ]=[ ]["]data["][ ] and is 
// modified by the parser
static wchar_t* get_token_data_line(const wchar_t* wtoken, wchar_t* wline)
{
	const wchar_t wspace[] = L" \t";	// The only whitespaces we recognize as such
	size_t i, r;
	BOOLEAN quoteth = FALSE;

	if ((wtoken == NULL) || (wline == NULL) || (wline[0] == 0))
		return NULL;

	i = 0;

	// Skip leading spaces
	i += wcsspn(&wline[i], wspace);

	// Our token should begin a line
	if (_wcsnicmp(&wline[i], wtoken, wcslen(wtoken)) != 0)
		return NULL;

	// Token was found, move past token
	i += wcslen(wtoken);

	// Skip spaces
	i += wcsspn(&wline[i], wspace);

	// Check for an equal sign
	if (wline[i] != L'=') 
		return NULL;
	i++;

	// Skip spaces after equal sign
	i += wcsspn(&wline[i], wspace);

	// eliminate leading quote, if it exists
	if (wline[i] == L'"') {
		quoteth = TRUE;
		i++;
	}

	// Keep the starting pos of our data
	r = i;

	// locate end of string or quote
	while ( (wline[i] != 0) && ((wline[i] != L'"') || ((wline[i] == L'"') && (!quoteth))) )
		i++;
	wline[i--] = 0;

	// Eliminate trailing EOL characters
	while ((i>=r) && ((wline[i] == L'\r') || (wline[i] == L'\n')))
		wline[i--] = 0;

	return (wline[r] == 0)?NULL:&wline[r];
}

// Parse a file (ANSI or UTF-8 or UTF-16) and return the data for the first occurence of 'token'
// The returned string is UTF-8 and MUST be freed by the caller
char* get_token_data_file(const char* token, const char* filename)
{
	wchar_t *wtoken = NULL, *wdata= NULL, *wfilename = NULL;
	wchar_t buf[1024];
	FILE* fd = NULL;
	char *ret = NULL;

	if ((filename == NULL) || (token == NULL))
		return NULL;
	if ((filename[0] == 0) || (token[0] == 0))
		return NULL;

	wfilename = utf8_to_wchar(filename);
	if (wfilename == NULL) {
		uprintf("Could not convert '%s' to UTF-16\n", filename);
		goto out;
	}
	wtoken = utf8_to_wchar(token);
	if (wfilename == NULL) {
		uprintf("Could not convert '%s' to UTF-16\n", token);
		goto out;
	}
	fd = _wfopen(wfilename, L"r, ccs=UNICODE");
	if (fd == NULL) goto out;

	// Process individual lines. NUL is always appended.
	// Ideally, we'd check that our buffer fits the line
	while (fgetws(buf, ARRAYSIZE(buf), fd) != NULL) {
		wdata = get_token_data_line(wtoken, buf);
		if (wdata != NULL) {
			ret = wchar_to_utf8(wdata);
			break;
		}
	}

out:
	if (fd != NULL)
		fclose(fd);
	safe_free(wfilename);
	safe_free(wtoken);
	return ret;
}

// Parse a buffer (ANSI or UTF-8) and return the data for the 'n'th occurence of 'token'
// The returned string is UTF-8 and MUST be freed by the caller
char* get_token_data_buffer(const char* token, unsigned int n, const char* buffer, size_t buffer_size)
{
	unsigned int j, curly_count;
	wchar_t *wtoken = NULL, *wdata = NULL, *wbuffer = NULL, *wline = NULL;
	size_t i;
	BOOL done = FALSE;
	char* ret = NULL;

	// We're handling remote data => better safe than sorry
	if ((token == NULL) || (buffer == NULL) || (buffer_size <= 4) || (buffer_size > 65536))
		goto out;

	// Ensure that our buffer is NUL terminated
	if (buffer[buffer_size-1] != 0)
		goto out;

	wbuffer = utf8_to_wchar(buffer);
	wtoken = utf8_to_wchar(token);
	if ((wbuffer == NULL) || (wtoken == NULL))
		goto out;

	// Process individual lines (or multiple lines when between {}, for RTF)
	for (i=0,j=0,done=FALSE; (j!=n)&&(!done); ) {
		wline = &wbuffer[i];

		for(curly_count=0;((curly_count>0)||((wbuffer[i]!=L'\n')&&(wbuffer[i]!=L'\r')))&&(wbuffer[i]!=0);i++) {
			if (wbuffer[i] == L'{') curly_count++;
			if (wbuffer[i] == L'}') curly_count--;
		}
		if (wbuffer[i]==0) {
			done = TRUE;
		} else {
			wbuffer[i++] = 0;
		}
		wdata = get_token_data_line(wtoken, wline);
		if (wdata != NULL) {
			j++;
		}
	}
out:
	if (wdata != NULL)
		ret = wchar_to_utf8(wdata);
	safe_free(wbuffer);
	safe_free(wtoken);
	return ret;
}

static __inline char* get_sanitized_token_data_buffer(const char* token, unsigned int n, const char* buffer, size_t buffer_size)
{
	size_t i;
	char* data = get_token_data_buffer(token, n, buffer, buffer_size);
	if (data != NULL) {
		for (i=0; i<safe_strlen(data); i++) {
			if ((data[i] == '\\') && (data[i+1] == 'n')) {
				data[i] = '\r';
				data[i+1] = '\n';
			}
		}
	}
	return data;
}

// Parse an update data file and populates a rufus_update structure.
// NB: since this is remote data, and we're running elevated, it *IS* considered
// potentially malicious, even if it comes from a supposedly trusted server.
// len should be the size of the buffer, including the zero terminator
void parse_update(char* buf, size_t len)
{
	size_t i;
	char *data = NULL, *token;
	char allowed_rtf_chars[] = "abcdefghijklmnopqrstuvwxyz|~-_:*'";
	char allowed_std_chars[] = "\r\n ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!\"$%^&+=<>(){}[].,;#@/?";

	// strchr includes the NUL terminator in the search, so take care of backslash before NUL
	if ((buf == NULL) || (len < 2) || (len > 65536) || (buf[len-1] != 0) || (buf[len-2] == '\\'))
		return;
	// Sanitize the data - Not a silver bullet, but it helps
	len = safe_strlen(buf)+1;	// Someone may be inserting NULs
	for (i=0; i<len-1; i++) {
		// Check for valid RTF sequences as well as allowed chars if not RTF
		if (buf[i] == '\\') {
			// NB: we have a zero terminator, so we can afford a +1 without overflow
			if (strchr(allowed_rtf_chars, buf[i+1]) == NULL) {
				buf[i] = ' ';
			}
		} else if ((strchr(allowed_rtf_chars, buf[i]) == NULL) && (strchr(allowed_std_chars, buf[i]) == NULL)) {
			buf[i] = ' ';
		}
	}

	for (i=0; i<4; i++)
		update.version[i] = 0;
	update.platform_min[0] = 5;
	update.platform_min[1] = 2;	// XP or later
	safe_free(update.download_url);
	safe_free(update.release_notes);
	if ((data = get_sanitized_token_data_buffer("version", 1, buf, len)) != NULL) {
		for (i=0; (i<4) && ((token = strtok((i==0)?data:NULL, ".")) != NULL); i++) {
			update.version[i] = (uint16_t)atoi(token);
		}
		safe_free(data);
	}
	if ((data = get_sanitized_token_data_buffer("platform_min", 1, buf, len)) != NULL) {
		for (i=0; (i<2) && ((token = strtok((i==0)?data:NULL, ".")) != NULL); i++) {
			update.platform_min[i] = (uint32_t)atoi(token);
		}
		safe_free(data);
	}
	update.download_url = get_sanitized_token_data_buffer("download_url", 1, buf, len);
	update.release_notes = get_sanitized_token_data_buffer("release_notes", 1, buf, len);
}

// Insert entry 'data' under section 'section' of a config file
// Section must include the relevant delimitors (eg '[', ']') if needed
char* insert_section_data(const char* filename, const char* section, const char* data, BOOL dos2unix)
{
	const wchar_t* outmode[] = { L"w", L"w, ccs=UTF-8", L"w, ccs=UTF-16LE" };
	wchar_t *wsection = NULL, *wfilename = NULL, *wtmpname = NULL, *wdata = NULL, bom = 0;
	wchar_t wspace[] = L" \t";
	wchar_t buf[1024];
	FILE *fd_in = NULL, *fd_out = NULL;
	size_t i, size;
	int mode;
	char *ret = NULL, tmp[2];

	if ((filename == NULL) || (section == NULL) || (data == NULL))
		return NULL;
	if ((filename[0] == 0) || (section[0] == 0) || (data[0] == 0))
		return NULL;

	wfilename = utf8_to_wchar(filename);
	if (wfilename == NULL) {
		uprintf("Could not convert '%s' to UTF-16\n", filename);
		goto out;
	}
	wsection = utf8_to_wchar(section);
	if (wfilename == NULL) {
		uprintf("Could not convert '%s' to UTF-16\n", section);
		goto out;
	}
	wdata = utf8_to_wchar(data);
	if (wdata == NULL) {
		uprintf("Could not convert '%s' to UTF-16\n", data);
		goto out;
	}

	fd_in = _wfopen(wfilename, L"r, ccs=UNICODE");
	if (fd_in == NULL) {
		uprintf("Could not open file '%s'\n", filename);
		goto out;
	}
	// Check the input file's BOM and create an output file with the same
	fread(&bom, sizeof(bom), 1, fd_in);
	switch(bom) {
	case 0xFEFF:
		mode = 2;	// UTF-16 (LE)
		break;
	case 0xBBEF:	// Yeah, the UTF-8 BOM is really 0xEF,0xBB,0xBF, but
		mode = 1;	// find me a non UTF-8 file that actually begins with "ï»"
		break;
	default:
		mode = 0;	// ANSI
		break;
	}
	fseek(fd_in, 0, SEEK_SET);
//	uprintf("'%s' was detected as %s\n", filename, 
//		(mode==0)?"ANSI/UTF8 (no BOM)":((mode==1)?"UTF8 (with BOM)":"UTF16 (with BOM"));


	wtmpname = (wchar_t*)calloc(wcslen(wfilename)+2, sizeof(wchar_t));
	if (wtmpname == NULL) {
		uprintf("Could not allocate space for temporary output name\n");
		goto out;
	}
	wcscpy(wtmpname, wfilename);
	wtmpname[wcslen(wtmpname)] = '~';

	fd_out = _wfopen(wtmpname, outmode[mode]);
	if (fd_out == NULL) {
		uprintf("Could not open temporary output file %s~\n", filename);
		goto out;
	}

	// Process individual lines. NUL is always appended.
	while (fgetws(buf, ARRAYSIZE(buf), fd_in) != NULL) {

		i = 0;

		// Skip leading spaces
		i += wcsspn(&buf[i], wspace);

		// Our token should begin a line
		if (_wcsnicmp(&buf[i], wsection, wcslen(wsection)) != 0) {
			fputws(buf, fd_out);
			continue;
		}

		// Section was found, output it
		fputws(buf, fd_out);
		// Now output the new data
		fwprintf(fd_out, L"%s\n", wdata);
		ret = (char*)data;
	}

out:
	if (fd_in != NULL) fclose(fd_in);
	if (fd_out != NULL) fclose(fd_out);

	// If an insertion occured, delete existing file and use the new one
	if (ret != NULL) {
		// We're in Windows text mode => Remove CRs if requested
		fd_in = _wfopen(wtmpname, L"rb");
		fd_out = _wfopen(wfilename, L"wb");
		// Don't check fds
		if ((fd_in != NULL) && (fd_out != NULL)) {
			size = (mode==2)?2:1;
			while(fread(tmp, size, 1, fd_in) == 1) {
				if ((!dos2unix) || (tmp[0] != 0x0D))
					fwrite(tmp, size, 1, fd_out);
			}
			fclose(fd_in);
			fclose(fd_out);
		} else {
			uprintf("Could not write %s - original file has been left unmodifiedn", filename);
			ret = NULL;
			if (fd_in != NULL) fclose(fd_in);
			if (fd_out != NULL) fclose(fd_out);
		}
	} 
	_wunlink(wtmpname);
	safe_free(wfilename);
	safe_free(wtmpname);
	safe_free(wsection);
	safe_free(wdata);

	return ret;
}

// Search for a specific 'src' substring data for all occurences of 'token', and replace
// it with 'rep'. File can be ANSI or UNICODE and is overwritten. Parameters are UTF-8.
// The parsed line is of the form: [ ]token[ ]data
// Returns a pointer to rep if replacement occured, NULL otherwise
char* replace_in_token_data(const char* filename, const char* token, const char* src, const char* rep, BOOL dos2unix)
{
	const wchar_t* outmode[] = { L"w", L"w, ccs=UTF-8", L"w, ccs=UTF-16LE" };
	wchar_t *wtoken = NULL, *wfilename = NULL, *wtmpname = NULL, *wsrc = NULL, *wrep = NULL, bom = 0;
	wchar_t wspace[] = L" \t";
	wchar_t buf[1024], *torep;
	FILE *fd_in = NULL, *fd_out = NULL;
	size_t i, size;
	int mode;
	char *ret = NULL, tmp[2];

	if ((filename == NULL) || (token == NULL) || (src == NULL) || (rep == NULL))
		return NULL;
	if ((filename[0] == 0) || (token[0] == 0) || (src[0] == 0) || (rep[0] == 0))
		return NULL;
	if (strcmp(src, rep) == 0)	// No need for processing is source is same as replacement
		return NULL;

	wfilename = utf8_to_wchar(filename);
	if (wfilename == NULL) {
		uprintf("Could not convert '%s' to UTF-16\n", filename);
		goto out;
	}
	wtoken = utf8_to_wchar(token);
	if (wfilename == NULL) {
		uprintf("Could not convert '%s' to UTF-16\n", token);
		goto out;
	}
	wsrc = utf8_to_wchar(src);
	if (wsrc == NULL) {
		uprintf("Could not convert '%s' to UTF-16\n", src);
		goto out;
	}
	wrep = utf8_to_wchar(rep);
	if (wsrc == NULL) {
		uprintf("Could not convert '%s' to UTF-16\n", rep);
		goto out;
	}

	fd_in = _wfopen(wfilename, L"r, ccs=UNICODE");
	if (fd_in == NULL) {
		uprintf("Could not open file '%s'\n", filename);
		goto out;
	}
	// Check the input file's BOM and create an output file with the same
	fread(&bom, sizeof(bom), 1, fd_in);
	switch(bom) {
	case 0xFEFF:
		mode = 2;	// UTF-16 (LE)
		break;
	case 0xBBEF:	// Yeah, the UTF-8 BOM is really 0xEF,0xBB,0xBF, but
		mode = 1;	// find me a non UTF-8 file that actually begins with "ï»"
		break;
	default:
		mode = 0;	// ANSI
		break;
	}
	fseek(fd_in, 0, SEEK_SET);
//	uprintf("'%s' was detected as %s\n", filename, 
//		(mode==0)?"ANSI/UTF8 (no BOM)":((mode==1)?"UTF8 (with BOM)":"UTF16 (with BOM"));


	wtmpname = (wchar_t*)calloc(wcslen(wfilename)+2, sizeof(wchar_t));
	if (wtmpname == NULL) {
		uprintf("Could not allocate space for temporary output name\n");
		goto out;
	}
	wcscpy(wtmpname, wfilename);
	wtmpname[wcslen(wtmpname)] = '~';

	fd_out = _wfopen(wtmpname, outmode[mode]);
	if (fd_out == NULL) {
		uprintf("Could not open temporary output file %s~\n", filename);
		goto out;
	}

	// Process individual lines. NUL is always appended.
	while (fgetws(buf, ARRAYSIZE(buf), fd_in) != NULL) {

		i = 0;

		// Skip leading spaces
		i += wcsspn(&buf[i], wspace);

		// Our token should begin a line
		if (_wcsnicmp(&buf[i], wtoken, wcslen(wtoken)) != 0) {
			fputws(buf, fd_out);
			continue;
		}

		// Token was found, move past token
		i += strlen(token);

		// Skip spaces
		i += wcsspn(&buf[i], wspace);

		torep = wcsstr(&buf[i], wsrc);
		if (torep == NULL) {
			fputws(buf, fd_out);
			continue;
		}

		i = (torep-buf) + wcslen(wsrc);
		*torep = 0;
		fwprintf(fd_out, L"%s%s%s", buf, wrep, &buf[i]);
		ret = (char*)rep;
	}

out:
	if (fd_in != NULL) fclose(fd_in);
	if (fd_out != NULL) fclose(fd_out);

	// If a replacement occured, delete existing file and use the new one
	if (ret != NULL) {
		// We're in Windows text mode => Remove CRs if requested
		fd_in = _wfopen(wtmpname, L"rb");
		fd_out = _wfopen(wfilename, L"wb");
		// Don't check fds
		if ((fd_in != NULL) && (fd_out != NULL)) {
			size = (mode==2)?2:1;
			while(fread(tmp, size, 1, fd_in) == 1) {
				if ((!dos2unix) || (tmp[0] != 0x0D))
					fwrite(tmp, size, 1, fd_out);
			}
			fclose(fd_in);
			fclose(fd_out);
		} else {
			uprintf("Could not write %s - original file has been left unmodified.\n", filename);
			ret = NULL;
			if (fd_in != NULL) fclose(fd_in);
			if (fd_out != NULL) fclose(fd_out);
		}
	}
	_wunlink(wtmpname);
	safe_free(wfilename);
	safe_free(wtmpname);
	safe_free(wtoken);
	safe_free(wsrc);
	safe_free(wrep);

	return ret;
}
