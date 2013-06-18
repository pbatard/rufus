/*
 * Rufus: The Reliable USB Formatting Utility
 * Localization functions, a.k.a. "Everybody is doing it wrong but me!"
 * Copyright © 2013 Pete Batard <pete@akeo.ie>
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

#include "rufus.h"
#include "msapi_utf8.h"
#include "locale.h"

/* s: quoted string, i: 32 bit signed integer, w single word (no space) */
loc_parse parse_cmd[] = {
	{ 'v', LC_VERSION, "ii" },
	{ 'l', LC_LOCALE, "s" },
	{ 'f', LC_FONT, "si" },
	{ 'p', LC_PARENT, "w" },
	{ 'd', LC_DIRECTION, "i" },
	{ 'r', LC_RESIZE, "wii" },
	{ 'm', LC_MOVE, "wii" },
	{ 't', LC_TEXT, "ws" }
};
size_t PARSE_CMD_SIZE = ARRAYSIZE(parse_cmd);

void free_loc_cmd(loc_cmd* lcmd)
{
	if (lcmd == NULL)
		return;
	safe_free(lcmd->text[0]);
	safe_free(lcmd->text[1]);
	free(lcmd);
}

BOOL execute_loc_cmd(loc_cmd* lcmd)
{
	if (lcmd == NULL)
		return FALSE;
	uprintf("cmd #%d: ('%s', '%s') (%d, %d)\n",
		lcmd->command, lcmd->text[0], lcmd->text[1], lcmd->num[0], lcmd->num[1]);
	return TRUE;
}
