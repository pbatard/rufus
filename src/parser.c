/*
 * Rufus: The Reliable USB Formatting Utility
 * Elementary Unicode compliant find/replace parser
 * Copyright © 2012-2018 Pete Batard <pete@akeo.ie>
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
#include "missing.h"
#include "msapi_utf8.h"
#include "localization.h"

static const char space[] = " \t";
static const wchar_t wspace[] = L" \t";
static const char* conversion_error = "Could not convert '%s' to UTF-16";

const struct {char c; int flag;} attr_parse[] = {
	{ 'r', LOC_RIGHT_TO_LEFT },
};

/*
 * Fill a localization command buffer by parsing the line arguments
 * The command is allocated and must be freed (by calling free_loc_cmd)
 */
static loc_cmd* get_loc_cmd(char c, char* line) {
	size_t i, j, k, l, r, ti = 0, ii = 0;
	char *endptr, *expected_endptr, *token;
	loc_cmd* lcmd = NULL;

	for (j=0; j<ARRAYSIZE(parse_cmd); j++) {
		if (c == parse_cmd[j].c)
			break;
	}
	if (j >= ARRAYSIZE(parse_cmd)) {
		luprint("unknown command");
		return NULL;
	}

	lcmd = (loc_cmd*)calloc(sizeof(loc_cmd), 1);
	if (lcmd == NULL) {
		luprint("could not allocate command");
		return NULL;
	}
	lcmd->command = parse_cmd[j].cmd;
	lcmd->ctrl_id = (lcmd->command <= LC_TEXT)?-1:0;
	lcmd->line_nr = (uint16_t)loc_line_nr;

	i = 0;
	for (k = 0; parse_cmd[j].arg_type[k] != 0; k++) {
		// Skip leading spaces
		i += strspn(&line[i], space);
		r = i;
		if (line[i] == 0) {
			luprintf("missing parameter for command '%c'", parse_cmd[j].c);
			goto err;
		}
		switch(parse_cmd[j].arg_type[k]) {
		case 's':	// quoted string
			// search leading quote
			if (line[i++] != '"') {
				luprint("no start quote");
				goto err;
			}
			r = i;
			// locate ending quote
			while ((line[i] != 0) && ((line[i] != '"') || ((line[i] == '"') && (line[i-1] == '\\')))) {
				if ((line[i] == '"') && (line[i-1] == '\\')) {
					strcpy(&line[i-1], &line[i]);
				} else {
					i++;
				}
			}
			if (line[i] == 0) {
				luprint("no end quote");
				goto err;
			}
			line[i++] = 0;
			lcmd->txt[ti++] = safe_strdup(&line[r]);
			break;
		case 'c':	// control ID (single word)
			while ((line[i] != 0) && (line[i] != space[0]) && (line[i] != space[1]))
				i++;
			if (line[i] != 0)
				line[i++] = 0;
			lcmd->txt[ti++] = safe_strdup(&line[r]);
			break;
		case 'i':	// 32 bit signed integer
			// allow commas or dots between values
			if ((line[i] == ',') || (line[i] == '.')) {
				i += strspn(&line[i+1], space);
				r = i;
			}
			while ((line[i] != 0) && (line[i] != space[0]) && (line[i] != space[1])
				&& (line[i] != ',') && (line[i] != '.'))
				i++;
			expected_endptr = &line[i];
			if (line[i] != 0)
				line[i++] = 0;
			lcmd->num[ii++] = (int32_t)strtol(&line[r], &endptr, 0);
			if (endptr != expected_endptr) {
				luprint("invalid integer");
				goto err;
			}
			break;
		case 'u':	// comma or dot separated list of unsigned integers (to end of line)
			// count the number of commas
			lcmd->unum_size = 1;
			for (l=i; line[l] != 0; l++) {
				if ((line[l] == '.') || (line[l] == ','))
					lcmd->unum_size++;
			}
			lcmd->unum = (uint32_t*)malloc(lcmd->unum_size * sizeof(uint32_t));
			if (lcmd->unum == NULL) {
				luprint("could not allocate memory");
				goto err;
			}
			token = strtok(&line[i], ".,");
			for (l=0; (l<lcmd->unum_size) && (token != NULL); l++) {
				lcmd->unum[l] = (int32_t)strtol(token, &endptr, 0);
				token = strtok(NULL, ".,");
			}
			if ((token != NULL) || (l != lcmd->unum_size)) {
				luprint("internal error (unexpected number of numeric values)");
				goto err;
			}
			break;
		default:
			uprintf("localization: unhandled arg_type '%c'\n", parse_cmd[j].arg_type[k]);
			goto err;
		}
	}

	return lcmd;

err:
	free_loc_cmd(lcmd);
	return NULL;
}

/*
 * Parse an UTF-8 localization command line
 */
static void get_loc_data_line(char* line)
{
	size_t i;
	loc_cmd* lcmd = NULL;
	char t;

	if ((line == NULL) || (line[0] == 0))
		return;

	// Skip leading spaces
	i = strspn(line, space);

	// Read token (NUL character will be read if EOL)
	t = line[i++];
	if (t == '#')	// Comment
		return;
	if ((t == 0) || ((line[i] != space[0]) && (line[i] != space[1]))) {
		luprintf("syntax error: '%s'", line);
		return;
	}

	lcmd = get_loc_cmd(t, &line[i]);

	if ((lcmd != NULL) && (lcmd->command != LC_LOCALE))
		// TODO: check return value?
		dispatch_loc_cmd(lcmd);
	else
		free_loc_cmd(lcmd);
}

/*
 * Open a localization file and store its file name, with special case
 * when dealing with the embedded loc file.
 */
FILE* open_loc_file(const char* filename)
{
	FILE* fd = NULL;
	wchar_t *wfilename = NULL;
	const char* tmp_ext = ".tmp";

	if (filename == NULL)
		return NULL;

	if (loc_filename != embedded_loc_filename) {
		safe_free(loc_filename);
	}
	if (safe_strcmp(tmp_ext, &filename[safe_strlen(filename)-4]) == 0) {
		loc_filename = embedded_loc_filename;
	} else {
		loc_filename = safe_strdup(filename);
	}
	wfilename = utf8_to_wchar(filename);
	if (wfilename == NULL) {
		uprintf(conversion_error, filename);
		goto out;
	}
	fd = _wfopen(wfilename, L"rb");
	if (fd == NULL) {
		uprintf("localization: could not open '%s'\n", filename);
	}

out:
	safe_free(wfilename);
	return fd;
}

/*
 * Parse a localization file, to construct the list of available locales.
 * The locale file must be UTF-8 with NO BOM.
 */
BOOL get_supported_locales(const char* filename)
{
	FILE* fd = NULL;
	BOOL r = FALSE;
	char line[1024];
	size_t i, j, k;
	loc_cmd *lcmd = NULL, *last_lcmd = NULL;
	long end_of_block;
	int version_line_nr = 0;
	uint32_t loc_base_major = -1, loc_base_minor = -1;

	fd = open_loc_file(filename);
	if (fd == NULL)
		goto out;

	// Check that the file doesn't contain a BOM and was saved in DOS mode
	i = fread(line, 1, sizeof(line), fd);
	if (i < sizeof(line)) {
		uprintf("Invalid loc file: the file is too small!");
		goto out;
	}
	if (((uint8_t)line[0]) > 0x80) {
		uprintf("Invalid loc file: the file should not have a BOM (Byte Order Mark)");
		goto out;
	}
	for (i=0; i<sizeof(line)-1; i++)
		if ((((uint8_t)line[i]) == 0x0D) && (((uint8_t)line[i+1]) == 0x0A)) break;
	if (i >= sizeof(line)-1) {
		uprintf("Invalid loc file: the file MUST be saved in DOS mode (CR/LF)");
		goto out;
	}
	fseek(fd, 0, SEEK_SET);

	loc_line_nr = 0;
	line[0] = 0;
	free_locale_list();
	do {
		// adjust the last block
		end_of_block = ftell(fd);
		if (fgets(line, sizeof(line), fd) == NULL)
			break;
		loc_line_nr++;
		// Skip leading spaces
		i = strspn(line, space);
		if ((line[i] != 'l') && (line[i] != 'v') && (line[i] != 'a'))
			continue;
		// line[i] is not NUL so i+1 is safe to access
		lcmd = get_loc_cmd(line[i], &line[i+1]);
		if ((lcmd == NULL) || ((lcmd->command != LC_LOCALE) && (lcmd->command != LC_VERSION) && (lcmd->command != LC_ATTRIBUTES))) {
			free_loc_cmd(lcmd);
			continue;
		}
		switch (lcmd->command) {
		case LC_LOCALE:
			// we use num[0] and num[1] as block delimiter index for this locale in the file
			if (last_lcmd != NULL) {
				if (version_line_nr == 0) {
					uprintf("localization: no compatible version was found - this locale will be ignored\n");
					list_del(&last_lcmd->list);
					free_loc_cmd(last_lcmd);
				} else {
					last_lcmd->num[1] = (int32_t)end_of_block;
				}
			}
			lcmd->num[0] = (int32_t)ftell(fd);
			// Add our locale command to the locale list
			list_add_tail(&lcmd->list, &locale_list);
			uprintf("localization: found locale '%s'\n", lcmd->txt[0]);
			last_lcmd = lcmd;
			version_line_nr = 0;
			break;
		case LC_ATTRIBUTES:
			if (last_lcmd == NULL) {
				luprint("[a]ttributes cannot precede [l]ocale");
			} else for(j=0; lcmd->txt[0][j] != 0; j++) {
				for (k=0; k<ARRAYSIZE(attr_parse); k++) {
					if (attr_parse[k].c == lcmd->txt[0][j]) {
						// Repurpose ctrl_id as an attributes mask
						last_lcmd->ctrl_id |= attr_parse[k].flag;
						break;
					}
				}
				if (k >= ARRAYSIZE(attr_parse))
					luprintf("unknown attribute '%c' - ignored", lcmd->txt[0][j]);
			}
			free_loc_cmd(lcmd);
			break;
		case LC_VERSION:
			if (version_line_nr != 0) {
				luprintf("[v]ersion was already provided at line %d", version_line_nr);
			} else if (lcmd->unum_size != 2) {
				luprint("[v]ersion format is invalid");
			} else if (last_lcmd == NULL) {
				luprint("[v]ersion cannot precede [l]ocale");
			} else if (loc_base_major == -1) {
				// We use the first version from our loc file (usually en-US) as our base
				// as it should always be the most up to date.
				loc_base_major = lcmd->unum[0];
				loc_base_minor = lcmd->unum[1];
				version_line_nr = loc_line_nr;
			} else {
				if ((lcmd->unum[0] < loc_base_major) || ((lcmd->unum[0] == loc_base_major) && (lcmd->unum[1] < loc_base_minor))) {
					last_lcmd->ctrl_id |= LOC_NEEDS_UPDATE;
					luprintf("the version of this translation is older than the base one and may result in some messages not being properly translated.\n"
						"If you are the translator, please update your translation with the changes that intervened between v%d.%d and v%d.%d.\n"
						"See https://github.com/pbatard/rufus/blob/master/res/loc/ChangeLog.txt",
						lcmd->unum[0], lcmd->unum[1], loc_base_major, loc_base_minor);
				}
				version_line_nr = loc_line_nr;
			}
			free_loc_cmd(lcmd);
			break;
		}
	} while (1);
	if (last_lcmd != NULL) {
		if (version_line_nr == 0) {
			uprintf("localization: no compatible version was found - this locale will be ignored\n");
			list_del(&last_lcmd->list);
			free_loc_cmd(last_lcmd);
		} else {
			last_lcmd->num[1] = (int32_t)ftell(fd);
		}
	}
	r = !list_empty(&locale_list);
	if (r == FALSE)
		uprintf("localization: '%s' contains no valid locale sections\n", filename);

out:
	if (fd != NULL)
		fclose(fd);
	return r;
}

/*
 * Parse a locale section in a localization file (UTF-8, no BOM)
 * NB: this call is reentrant for the "base" command support
 * TODO: Working on memory rather than on file would improve performance
 */
BOOL get_loc_data_file(const char* filename, loc_cmd* lcmd)
{
	size_t bufsize = 1024;
	static FILE* fd = NULL;
	static BOOL populate_default = FALSE;
	char *buf = NULL;
	size_t i = 0;
	int r = 0, line_nr_incr = 1;
	int c = 0, eol_char = 0;
	int start_line, old_loc_line_nr = 0;
	BOOL ret = FALSE, eol = FALSE, escape_sequence = FALSE, reentrant = (fd != NULL);
	long offset, cur_offset = -1, end_offset;
	// The default locale is always the first one
	loc_cmd* default_locale = list_entry(locale_list.next, loc_cmd, list);

	if ((lcmd == NULL) || (default_locale == NULL)) {
		uprintf("localization: no %slocale", (default_locale == NULL)?"default ":" ");
		goto out;
	}

	if (msg_table == NULL) {
		// Initialize the default message table (usually en-US)
		msg_table = default_msg_table;
		uprintf("localization: initializing default message table");
		populate_default = TRUE;
		get_loc_data_file(filename, default_locale);
		populate_default = FALSE;
	}

	if (reentrant) {
		// Called, from a 'b' command - no need to reopen the file,
		// just save the current offset and current line number
		cur_offset = ftell(fd);
		old_loc_line_nr = loc_line_nr;
	} else {
		if ((filename == NULL) || (filename[0] == 0))
			return FALSE;
		if (!populate_default) {
			if (lcmd == default_locale) {
				// The default locale has already been populated => nothing to do
				msg_table = default_msg_table;
				return TRUE;
			}
			msg_table = current_msg_table;
		}
		free_dialog_list();
		fd = open_loc_file(filename);
		if (fd == NULL)
			goto out;
	}

	offset = (long)lcmd->num[0];
	end_offset = (long)lcmd->num[1];
	start_line = lcmd->line_nr;
	loc_line_nr = start_line;
	buf = (char*) malloc(bufsize);
	if (buf == NULL) {
		uprintf("localization: could not allocate line buffer\n");
		goto out;
	}

	if (fseek(fd, offset, SEEK_SET) != 0) {
		uprintf("localization: could not rewind\n");
		goto out;
	}

	do {	// custom readline handling for string collation, realloc, line numbers, etc.
		c = getc(fd);
		switch(c) {
		case EOF:
			buf[i] = 0;
			if (!eol)
				loc_line_nr += line_nr_incr;
			get_loc_data_line(buf);
			break;
		case '\r':
		case '\n':
			if (escape_sequence) {
				escape_sequence = FALSE;
				break;
			}
			// This assumes that the EOL sequence is always the same throughout the file
			if (eol_char == 0)
				eol_char = c;
			if (c == eol_char) {
				if (eol) {
					line_nr_incr++;
				} else {
					loc_line_nr += line_nr_incr;
					line_nr_incr = 1;
				}
			}
			buf[i] = 0;
			if (!eol) {
				// Strip trailing spaces (for string collation)
				for (r = ((int)i)-1; (r>0) && ((buf[r]==space[0])||(buf[r]==space[1])); r--);
				if (r < 0)
					r = 0;
				eol = TRUE;
			}
			break;
		case ' ':
		case '\t':
			if (escape_sequence) {
				escape_sequence = FALSE;
				break;
			}
			if (!eol) {
				buf[i++] = (char)c;
			}
			break;
		case '\\':
			if (!escape_sequence) {
				escape_sequence = TRUE;
				break;
			}
			// fall through on escape sequence
		default:
			if (escape_sequence) {
				switch (c) {
				case 'n':	// \n -> CRLF
					buf[i++] = '\r';
					buf[i++] = '\n';
					break;
				case '"':	// \" carried as is
					buf[i++] = '\\';
					buf[i++] = '"';
					break;
				case '\\':
					buf[i++] = '\\';
					break;
				default:	// ignore any other escape sequence
					break;
				}
				escape_sequence = FALSE;
			} else {
				// Collate multiline strings
				if ((eol) && (c == '"') && (buf[r] == '"')) {
					i = r;
					eol = FALSE;
					break;
				}
				if (eol) {
					get_loc_data_line(buf);
					eol = FALSE;
					i = 0;
					r = 0;
				}
				buf[i++] = (char)c;
			}
			break;
		}
		if ((c == EOF) || (ftell(fd) > end_offset))
			break;
		// Have at least 2 chars extra, for \r\n sequences
		if (i >= bufsize-2) {
			bufsize *= 2;
			if (bufsize > 32768) {
				uprintf("localization: requested line buffer is larger than 32K!\n");
				goto out;
			}
			buf = (char*) _reallocf(buf, bufsize);
			if (buf == NULL) {
				uprintf("localization: could not grow line buffer\n");
				goto out;
			}
		}
	} while(1);
	ret = TRUE;

out:
	// Don't close on a reentrant call
	if (reentrant) {
		if ((cur_offset < 0) || (fseek(fd, cur_offset, SEEK_SET) != 0)) {
			uprintf("localization: unable to reset reentrant position\n");
			ret = FALSE;
		}
		loc_line_nr = old_loc_line_nr;
	} else if (fd != NULL) {
		fclose(fd);
		fd = NULL;
	}
	safe_free(buf);
	return ret;
}


/*
 * Parse a line of UTF-16 text and return the data if it matches the 'token'
 * The parsed line is of the form: [ ][<][ ]token[ ][=|>][ ]["]data["][ ][<] and is
 * modified by the parser
 */
static wchar_t* get_token_data_line(const wchar_t* wtoken, wchar_t* wline)
{
	size_t i, r;
	BOOLEAN quoteth = FALSE;
	BOOLEAN xml = FALSE;

	if ((wtoken == NULL) || (wline == NULL) || (wline[0] == 0))
		return NULL;

	i = 0;

	// Skip leading spaces and opening '<'
	i += wcsspn(&wline[i], wspace);
	if (wline[i] == L'<')
		i++;
	i += wcsspn(&wline[i], wspace);

	// Our token should begin a line
	if (_wcsnicmp(&wline[i], wtoken, wcslen(wtoken)) != 0)
		return NULL;

	// Token was found, move past token
	i += wcslen(wtoken);

	// Skip spaces
	i += wcsspn(&wline[i], wspace);

	// Check for '=' or '>' sign
	if (wline[i] == L'>')
		xml = TRUE;
	else if (wline[i] != L'=')
		return NULL;
	i++;

	// Skip spaces
	i += wcsspn(&wline[i], wspace);

	// eliminate leading quote, if it exists
	if (wline[i] == L'"') {
		quoteth = TRUE;
		i++;
	}

	// Keep the starting pos of our data
	r = i;

	// locate end of string or quote
	while ( (wline[i] != 0) && (((wline[i] != L'"') && (wline[i] != L'<')) || ((wline[i] == L'"') && (!quoteth)) || ((wline[i] == L'<') && (!xml))) )
		i++;
	wline[i--] = 0;

	// Eliminate trailing EOL characters
	while ((i>=r) && ((wline[i] == L'\r') || (wline[i] == L'\n')))
		wline[i--] = 0;

	return (wline[r] == 0)?NULL:&wline[r];
}

/*
 * Parse a file (ANSI or UTF-8 or UTF-16) and return the data for the 'index'th occurrence of 'token'
 * The returned string is UTF-8 and MUST be freed by the caller
 */
char* get_token_data_file_indexed(const char* token, const char* filename, int index)
{
	int i = 0;
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
		uprintf(conversion_error, filename);
		goto out;
	}
	wtoken = utf8_to_wchar(token);
	if (wfilename == NULL) {
		uprintf(conversion_error, token);
		goto out;
	}
	fd = _wfopen(wfilename, L"r, ccs=UNICODE");
	if (fd == NULL) goto out;

	// Process individual lines. NUL is always appended.
	// Ideally, we'd check that our buffer fits the line
	while (fgetws(buf, ARRAYSIZE(buf), fd) != NULL) {
		wdata = get_token_data_line(wtoken, buf);
		if ((wdata != NULL) && (++i == index)) {
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

/*
 * replace or add 'data' for token 'token' in config file 'filename'
 */
char* set_token_data_file(const char* token, const char* data, const char* filename)
{
	const wchar_t* outmode[] = { L"w", L"w, ccs=UTF-8", L"w, ccs=UTF-16LE" };
	wchar_t *wtoken = NULL, *wfilename = NULL, *wtmpname = NULL, *wdata = NULL, bom = 0;
	wchar_t buf[1024];
	FILE *fd_in = NULL, *fd_out = NULL;
	size_t i, size;
	int mode = 0;
	char *ret = NULL, tmp[2];

	if ((filename == NULL) || (token == NULL) || (data == NULL))
		return NULL;
	if ((filename[0] == 0) || (token[0] == 0) || (data[0] == 0))
		return NULL;

	wfilename = utf8_to_wchar(filename);
	if (wfilename == NULL) {
		uprintf(conversion_error, filename);
		goto out;
	}
	wtoken = utf8_to_wchar(token);
	if (wfilename == NULL) {
		uprintf(conversion_error, token);
		goto out;
	}
	wdata = utf8_to_wchar(data);
	if (wdata == NULL) {
		uprintf(conversion_error, data);
		goto out;
	}

	fd_in = _wfopen(wfilename, L"r, ccs=UNICODE");
	if (fd_in == NULL) {
		uprintf("Could not open file '%s'\n", filename);
		goto out;
	}
	// Check the input file's BOM and create an output file with the same
	if (fread(&bom, sizeof(bom), 1, fd_in) == 1) {
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
	}

	wtmpname = (wchar_t*)calloc(wcslen(wfilename)+2, sizeof(wchar_t));
	if (wtmpname == NULL) {
		uprintf("Could not allocate space for temporary output name\n");
		goto out;
	}
	wcscpy(wtmpname, wfilename);
	wtmpname[wcslen(wtmpname)] = '~';

	fd_out = _wfopen(wtmpname, outmode[mode]);
	if (fd_out == NULL) {
		uprintf("Could not open temporary output file '%s~'\n", filename);
		goto out;
	}

	// Process individual lines. NUL is always appended.
	while (fgetws(buf, ARRAYSIZE(buf), fd_in) != NULL) {

		i = 0;

		// Skip leading spaces
		i += wcsspn(&buf[i], wspace);

		// Ignore comments or section headers
		if ((buf[i] == ';') || (buf[i] == '[')) {
			fputws(buf, fd_out);
			continue;
		}

		// Our token should begin a line
		if (_wcsnicmp(&buf[i], wtoken, wcslen(wtoken)) != 0) {
			fputws(buf, fd_out);
			continue;
		}

		// Token was found, move past token
		i += wcslen(wtoken);

		// Skip spaces
		i += wcsspn(&buf[i], wspace);

		// Check for an equal sign
		if (buf[i] != L'=') {
			fputws(buf, fd_out);
			continue;
		}
		i++;

		// Skip spaces after equal sign
		i += wcsspn(&buf[i], wspace);

		// Output the token
		buf[i] = 0;
		fputws(buf, fd_out);

		// Now output the new data
		fwprintf(fd_out, L"%s\n", wdata);
		ret = (char*)data;
	}

	if (ret == NULL) {
		// Didn't find an existing token => append it
		fwprintf(fd_out, L"%s = %s\n", wtoken, wdata);
		ret = (char*)data;
	}

out:
	if (fd_in != NULL) fclose(fd_in);
	if (fd_out != NULL) fclose(fd_out);

	// If an insertion occurred, delete existing file and use the new one
	if (ret != NULL) {
		// We're in Windows text mode => Remove CRs if requested
		fd_in = _wfopen(wtmpname, L"rb");
		fd_out = _wfopen(wfilename, L"wb");
		// Don't check fds
		if ((fd_in != NULL) && (fd_out != NULL)) {
			size = (mode==2)?2:1;
			while(fread(tmp, size, 1, fd_in) == 1)
				fwrite(tmp, size, 1, fd_out);
			fclose(fd_in);
			fclose(fd_out);
		} else {
			uprintf("Could not write '%s' - original file has been left unmodified\n", filename);
			ret = NULL;
			if (fd_in != NULL) fclose(fd_in);
			if (fd_out != NULL) fclose(fd_out);
		}
	}
	if (wtmpname != NULL)
		_wunlink(wtmpname);
	safe_free(wfilename);
	safe_free(wtmpname);
	safe_free(wtoken);
	safe_free(wdata);

	return ret;
}

/*
 * Parse a buffer (ANSI or UTF-8) and return the data for the 'n'th occurrence of 'token'
 * The returned string is UTF-8 and MUST be freed by the caller
 */
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
		for (i=0; i<strlen(data); i++) {
			if ((data[i] == '\\') && (data[i+1] == 'n')) {
				data[i] = '\r';
				data[i+1] = '\n';
			}
		}
	}
	return data;
}

/*
 * Parse an update data file and populates a rufus_update structure.
 * NB: since this is remote data, and we're running elevated, it *IS* considered
 * potentially malicious, even if it comes from a supposedly trusted server.
 * len should be the size of the buffer, including the zero terminator
 */
void parse_update(char* buf, size_t len)
{
	size_t i;
	char *data = NULL, *token;
	char allowed_rtf_chars[] = "abcdefghijklmnopqrstuvwxyz|~-_:*'";
	char allowed_std_chars[] = "\r\n ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!\"$%^&+=<>(){}[].,;#@/?";
	char download_url_name[24];
	char *arch_names[CPU_ARCH_MAX] = { "x86", "x64", "arm", "arm64", "none" };

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

	for (i=0; i<3; i++)
		update.version[i] = 0;
	update.platform_min[0] = 5;
	update.platform_min[1] = 2;	// XP or later
	safe_free(update.download_url);
	safe_free(update.release_notes);
	if ((data = get_sanitized_token_data_buffer("version", 1, buf, len)) != NULL) {
		for (i=0; (i<3) && ((token = strtok((i==0)?data:NULL, ".")) != NULL); i++) {
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
	static_sprintf(download_url_name, "download_url_%s", arch_names[GetCpuArch()]);
	update.download_url = get_sanitized_token_data_buffer(download_url_name, 1, buf, len);
	if (update.download_url == NULL)
		update.download_url = get_sanitized_token_data_buffer("download_url", 1, buf, len);
	update.release_notes = get_sanitized_token_data_buffer("release_notes", 1, buf, len);
}

/*
 * Insert entry 'data' under section 'section' of a config file
 * Section must include the relevant delimiters (eg '[', ']') if needed
 */
char* insert_section_data(const char* filename, const char* section, const char* data, BOOL dos2unix)
{
	const wchar_t* outmode[] = { L"w", L"w, ccs=UTF-8", L"w, ccs=UTF-16LE" };
	wchar_t *wsection = NULL, *wfilename = NULL, *wtmpname = NULL, *wdata = NULL, bom = 0;
	wchar_t buf[1024];
	FILE *fd_in = NULL, *fd_out = NULL;
	size_t i, size;
	int mode = 0;
	char *ret = NULL, tmp[2];

	if ((filename == NULL) || (section == NULL) || (data == NULL))
		return NULL;
	if ((filename[0] == 0) || (section[0] == 0) || (data[0] == 0))
		return NULL;

	wfilename = utf8_to_wchar(filename);
	if (wfilename == NULL) {
		uprintf(conversion_error, filename);
		goto out;
	}
	wsection = utf8_to_wchar(section);
	if (wfilename == NULL) {
		uprintf(conversion_error, section);
		goto out;
	}
	wdata = utf8_to_wchar(data);
	if (wdata == NULL) {
		uprintf(conversion_error, data);
		goto out;
	}

	fd_in = _wfopen(wfilename, L"r, ccs=UNICODE");
	if (fd_in == NULL) {
		uprintf("Could not open file '%s'\n", filename);
		goto out;
	}
	// Check the input file's BOM and create an output file with the same
	if (fread(&bom, sizeof(bom), 1, fd_in) != 1) {
		uprintf("Could not read file '%s'\n", filename);
		goto out;
	}
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
//	duprintf("'%s' was detected as %s\n", filename,
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
		uprintf("Could not open temporary output file '%s~'\n", filename);
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

	// If an insertion occurred, delete existing file and use the new one
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
			uprintf("Could not write '%s' - original file has been left unmodified\n", filename);
			ret = NULL;
			if (fd_in != NULL) fclose(fd_in);
			if (fd_out != NULL) fclose(fd_out);
		}
	}
	if (wtmpname != NULL)
		_wunlink(wtmpname);
	safe_free(wfilename);
	safe_free(wtmpname);
	safe_free(wsection);
	safe_free(wdata);

	return ret;
}

/*
 * Search for a specific 'src' substring data for all occurrences of 'token', and replace
 * it with 'rep'. File can be ANSI or UNICODE and is overwritten. Parameters are UTF-8.
 * The parsed line is of the form: [ ]token[ ]data
 * Returns a pointer to rep if replacement occurred, NULL otherwise
 */
char* replace_in_token_data(const char* filename, const char* token, const char* src, const char* rep, BOOL dos2unix)
{
	const wchar_t* outmode[] = { L"w", L"w, ccs=UTF-8", L"w, ccs=UTF-16LE" };
	wchar_t *wtoken = NULL, *wfilename = NULL, *wtmpname = NULL, *wsrc = NULL, *wrep = NULL, bom = 0;
	wchar_t buf[1024], *torep;
	FILE *fd_in = NULL, *fd_out = NULL;
	size_t i, size;
	int mode = 0;
	char *ret = NULL, tmp[2];

	if ((filename == NULL) || (token == NULL) || (src == NULL) || (rep == NULL))
		return NULL;
	if ((filename[0] == 0) || (token[0] == 0) || (src[0] == 0) || (rep[0] == 0))
		return NULL;
	if (strcmp(src, rep) == 0)	// No need for processing is source is same as replacement
		return NULL;

	wfilename = utf8_to_wchar(filename);
	if (wfilename == NULL) {
		uprintf(conversion_error, filename);
		goto out;
	}
	wtoken = utf8_to_wchar(token);
	if (wfilename == NULL) {
		uprintf(conversion_error, token);
		goto out;
	}
	wsrc = utf8_to_wchar(src);
	if (wsrc == NULL) {
		uprintf(conversion_error, src);
		goto out;
	}
	wrep = utf8_to_wchar(rep);
	if (wsrc == NULL) {
		uprintf(conversion_error, rep);
		goto out;
	}

	fd_in = _wfopen(wfilename, L"r, ccs=UNICODE");
	if (fd_in == NULL) {
		uprintf("Could not open file '%s'\n", filename);
		goto out;
	}
	// Check the input file's BOM and create an output file with the same
	if (fread(&bom, sizeof(bom), 1, fd_in) != 1) {
		if (!feof(fd_in))
			uprintf("Could not read file '%s'\n", filename);
		goto out;
	}
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
//	duprintf("'%s' was detected as %s\n", filename,
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
		uprintf("Could not open temporary output file '%s~'\n", filename);
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
		i += wcslen(wtoken);

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

	// If a replacement occurred, delete existing file and use the new one
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
			uprintf("Could not write '%s' - original file has been left unmodified.\n", filename);
			ret = NULL;
			if (fd_in != NULL) fclose(fd_in);
			if (fd_out != NULL) fclose(fd_out);
		}
	}
	if (wtmpname != NULL)
		_wunlink(wtmpname);
	safe_free(wfilename);
	safe_free(wtmpname);
	safe_free(wtoken);
	safe_free(wsrc);
	safe_free(wrep);

	return ret;
}

/*
 * Replace all 'c' characters in string 'src' with the substring 'rep'
 * The returned string is allocated and must be freed by the caller.
 */
char* replace_char(const char* src, const char c, const char* rep)
{
	size_t i, j, k, count=0, str_len = safe_strlen(src), rep_len = safe_strlen(rep);
	char* res;

	if ((src == NULL) || (rep == NULL))
		return NULL;
	for (i=0; i<str_len; i++) {
		if (src[i] == c)
			count++;
	}
	res = (char*)malloc(str_len + count*rep_len + 1);
	if (res == NULL)
		return NULL;
	for (i=0,j=0; i<str_len; i++) {
		if (src[i] == c) {
			for(k=0; k<rep_len; k++)
				res[j++] = rep[k];
		} else {
// Since the VS Code Analysis tool is dumb...
#if defined(_MSC_VER)
#pragma warning(suppress: 6386)
#endif
			res[j++] = src[i];
		}
	}
	res[j] = 0;
	return res;
}

/*
 * Internal recursive call for get_data_from_asn1(). Returns FALSE on error, TRUE otherwise.
 */
static BOOL get_data_from_asn1_internal(const uint8_t* buf, size_t buf_len, const void* oid,
			size_t oid_len, uint8_t asn1_type, void** data, size_t* data_len, BOOL* matched)
{
	size_t pos = 0, len, len_len, i;
	uint8_t tag;
	BOOL is_sequence, is_universal_tag;

	while (pos < buf_len) {
		is_sequence = buf[pos] & 0x20;
		is_universal_tag = ((buf[pos] & 0xC0) == 0x00);
		tag = buf[pos++] & 0x1F;
		if (tag == 0x1F) {
			uprintf("get_data_from_asn1: Long form tags are unsupported");
			return FALSE;
		}

		// Compute the length
		len = 0;
		len_len = 1;
		if ((is_universal_tag) && (tag == 0x05)) {	// ignore "NULL" tag
			pos++;
		} else {
			if (buf[pos] & 0x80) {
				len_len = buf[pos++] & 0x7F;
				// The data we're dealing with is not expected to ever be larger than 64K
				if (len_len > 2) {
					uprintf("get_data_from_asn1: Length fields larger than 2 bytes are unsupported");
					return FALSE;
				}
				for (i = 0; i < len_len; i++) {
					len <<= 8;
					len += buf[pos++];
				}
			} else {
				len = buf[pos++];
			}

			if (len > buf_len - pos) {
				uprintf("get_data_from_asn1: Overflow error (computed length %d is larger than remaining data)", len);
				return FALSE;
			}
		}

		if (len != 0) {
			if (is_sequence) {
				if (!get_data_from_asn1_internal(&buf[pos], len, oid, oid_len, asn1_type, data, data_len, matched))
					return FALSE;	// error
				if (*data != NULL)
					return TRUE;
			} else if (is_universal_tag) {	// Only process tags that belong to the UNIVERSAL class
				// NB: 0x06 = "OID" tag
				if ((!*matched) && (tag == 0x06) && (len == oid_len) && (memcmp(&buf[pos], oid, oid_len) == 0)) {
					*matched = TRUE;
				} else if ((*matched) && (tag == asn1_type)) {
					*data_len = len;
					*data = (void*)&buf[pos];
					return TRUE;
				}
			}
			pos += len;
		}
	};

	return TRUE;
}

/*
 * Helper functions to convert an OID string to an OID byte array
 * Taken from from openpgp-oid.c
 */
static size_t make_flagged_int(unsigned long value, uint8_t *buf, size_t buf_len)
{
	BOOL more = FALSE;
	int shift;

	for (shift = 28; shift > 0; shift -= 7) {
		if (more || value >= ((unsigned long)1 << shift)) {
			buf[buf_len++] = (uint8_t) (0x80 | (value >> shift));
			value -= (value >> shift) << shift;
			more = TRUE;
		}
	}
	buf[buf_len++] = (uint8_t) value;
	return buf_len;
}

/*
 * Convert OID string 'oid_str' to an OID byte array of size 'ret_len'
 * The returned array must be freed by the caller.
 */
static uint8_t* oid_from_str(const char* oid_str, size_t* ret_len)
{
	uint8_t* oid = NULL;
	unsigned long val1 = 0, val;
	const char *endp;
	int arcno = 0;
	size_t oid_len = 0;

	if ((oid_str == NULL) || (oid_str[0] == 0))
		return NULL;

	// We can safely assume that the encoded OID is shorter than the string.
	oid = malloc(1 + strlen(oid_str) + 2);
	if (oid == NULL)
		return NULL;

	do {
		arcno++;
		val = strtoul(oid_str, (char**)&endp, 10);
		if (!isdigit(*oid_str) || !(*endp == '.' || !*endp))
			goto err;
		if (*endp == '.')
			oid_str = endp + 1;

		if (arcno == 1) {
			if (val > 2)
				break; // Not allowed, error caught below.
			val1 = val;
		} else if (arcno == 2) {
			// Need to combine the first two arcs in one byte.
			if (val1 < 2) {
				if (val > 39)
					goto err;
				oid[oid_len++] = (uint8_t)(val1 * 40 + val);
			} else {
				val += 80;
				oid_len = make_flagged_int(val, oid, oid_len);
			}
		} else {
			oid_len = make_flagged_int(val, oid, oid_len);
		}
	} while (*endp == '.');

	// It is not possible to encode only the first arc.
	if (arcno == 1 || oid_len < 2 || oid_len > 254)
		goto err;

	*ret_len = oid_len;
	return oid;

err:
	free(oid);
	return NULL;
}

/*
 * Parse an ASN.1 binary buffer and return a pointer to the first instance of OID data of type 'asn1_type',
 * matching the OID 'oid_str' (expressed as an OID string). If successful, the length or the returned data
 * is placed in 'data_len'. Note: Only the UNIVERSAL class is supported for 'asn1_type' (other classes are
 * ignored). If 'oid_str' is NULL or empty, the first data element of type 'asn1_type' is returned.
 */
void* get_data_from_asn1(const uint8_t* buf, size_t buf_len, const char* oid_str, uint8_t asn1_type, size_t* data_len)
{
	void* data = NULL;
	uint8_t* oid = NULL;
	size_t oid_len = 0;
	BOOL matched = ((oid_str == NULL) || (oid_str[0] == 0));

	if (buf_len >= 65536) {
		uprintf("get_data_from_asn1: Buffers larger than 64KB are not supported");
		return NULL;
	}

	if (!matched) {
		// We have an OID string to convert
		oid = oid_from_str(oid_str, &oid_len);
		if (oid == NULL) {
			uprintf("get_data_from_asn1: Could not convert OID string '%s'", oid_str);
			return NULL;
		}
	}

	// No need to check for the return value as data is always NULL on error
	get_data_from_asn1_internal(buf, buf_len, oid, oid_len, asn1_type, &data, data_len, &matched);
	free(oid);
	return data;
}
