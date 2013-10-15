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
#include <stddef.h>

#include "rufus.h"
#include "resource.h"
#include "msapi_utf8.h"
#include "localization.h"
#include "localization_data.h"

/* 
 * List of supported locale commands, with their parameter syntax:
 *   c control ID (no space, no quotes)
 *   s: quoted string
 *   i: 32 bit signed integer
 *   u: 32 bit unsigned CSV list
 * Remember to update the size of the array in localization.h when adding/removing elements
 */
const loc_parse parse_cmd[9] = {
	// Translation name and Windows LCIDs it should apply to
	{ 'l', LC_LOCALE, "ssu" },	// l "en_US" "English (US)" 0x0009,0x1009
	// Base translation to add on top of (eg. "English (UK)" can be used to build on top of "English (US)"
	{ 'b', LC_BASE, "s" },		// b "en_US"
	// Version to use for the localization commandset and API
	{ 'v', LC_VERSION, "ii" },	// v 1.0				// TODO: NOT IMPLEMENTED YET
	// Translate the text control associated with an ID
	{ 't', LC_TEXT, "cs" },		// t IDC_CONTROL "Translation"
	// Set the section/dialog to which the next commands should apply
	{ 'g', LC_GROUP, "c" },		// g IDD_DIALOG
	// Resize a dialog (dx dy pixel increment)
	{ 's', LC_SIZE, "cii" },	// s IDC_CONTROL +10 +10
	// Move a dialog (dx dy pixed displacement)
	{ 'm', LC_MOVE, "cii" },	// m IDC_CONTROL -5 0
	// Set the font to use for the text controls that follow
	// Use f "Default" 0 to reset the font
	{ 'f', LC_FONT, "si" },		// f "MS Dialog" 10
	// Set the direction to use for the text controls that follow
	// 0 = Left to right, 1 = Right to left
	{ 'd', LC_DIRECTION, "i" },	// d 1					// TODO: NOT IMPLEMENTED YET
};

/* Globals */
int    loc_line_nr;
struct list_head locale_list = {NULL, NULL};
char   *loc_filename = NULL, *embedded_loc_filename = "[embedded] rufus.loc";

/*
 * Add a localization command to a dialog/section
 */
void add_dialog_command(int index, loc_cmd* lcmd)
{
	if ((lcmd == NULL) || (index < 0) || (index >= ARRAYSIZE(loc_dlg))) {
		uprintf("add_dialog_command: invalid parameter\n");
		return;
	}
	list_add(&lcmd->list, &loc_dlg[index].list);
}

void free_loc_cmd(loc_cmd* lcmd)
{
	if (lcmd == NULL)
		return;
	safe_free(lcmd->txt[0]);
	safe_free(lcmd->txt[1]);
	safe_free(lcmd->unum);
	free(lcmd);
}

void free_dialog_list(void)
{
	size_t i = 0;
	loc_cmd *lcmd, *next;

	for (i=0; i<ARRAYSIZE(loc_dlg); i++) {
		if (list_empty(&loc_dlg[i].list))
			continue;
		list_for_each_entry_safe(lcmd, next, &loc_dlg[i].list, loc_cmd, list) {
			list_del(&lcmd->list);
			free_loc_cmd(lcmd);
		}
	}
}

void free_locale_list(void)
{
	loc_cmd *lcmd, *next;

	list_for_each_entry_safe(lcmd, next, &locale_list, loc_cmd, list) {
		list_del(&lcmd->list);
		free_loc_cmd(lcmd);
	}
}

/*
 * Init/destroy our various localization lists
 */
void init_localization(void) {
	size_t i;
	for (i=0; i<ARRAYSIZE(loc_dlg); i++)
		list_init(&loc_dlg[i].list);
	list_init(&locale_list);
}

void exit_localization(void) {
	free_dialog_list();
	free_locale_list();
}

/*
 * Validate and store localization command data
 *
 * TODO: Do we need to store a revert for every action we execute here,
 * or do we want to reinstantiate the dialogs?
 */
BOOL dispatch_loc_cmd(loc_cmd* lcmd)
{
	size_t i;
	static int dlg_index = 0;
	loc_cmd* base_locale = NULL;

	if (lcmd == NULL)
		return FALSE;

	if (lcmd->command <= LC_TEXT) {
		// Any command up to LC_TEXT takes a control ID in text[0]
		for (i=0; i<ARRAYSIZE(control_id); i++) {
			if (safe_strcmp(lcmd->txt[0], control_id[i].name) == 0) {
				lcmd->ctrl_id = control_id[i].id;
				break;
			}
		}
		if (lcmd->ctrl_id < 0) {
			luprintf("unknown control '%s'\n", lcmd->txt[0]);
			goto err;
		}
	}

	switch(lcmd->command) {
	// NB: For commands that take an ID, ctrl_id is always a valid index at this stage
	case LC_TEXT:
	case LC_MOVE:
	case LC_SIZE:
		add_dialog_command(dlg_index, lcmd);
		break;
	case LC_GROUP:
		if ((lcmd->ctrl_id-IDD_DIALOG) > ARRAYSIZE(loc_dlg)) {
			luprintf("'%s' is not a group ID\n", lcmd->txt[0]);
			goto err;
		}
		dlg_index = lcmd->ctrl_id - IDD_DIALOG;
		free_loc_cmd(lcmd);
		break;
	case LC_VERSION:
		luprintf("GOT VERSION: %d.%d\n", lcmd->num[0], lcmd->num[1]);
		free_loc_cmd(lcmd);
		break;
	case LC_BASE:
		uprintf("localization: using locale base '%s'", lcmd->txt[0]);
		base_locale = get_locale_from_name(lcmd->txt[0]);
		get_loc_data_file(NULL, (long)base_locale->num[0], (long)base_locale->num[1], base_locale->line_nr);
		free_loc_cmd(lcmd);
		break;
	default:
		free_loc_cmd(lcmd);
		break;
	}
	return TRUE;

err:
	free_loc_cmd(lcmd);
	return FALSE;
}

/*
 * Apply stored localization commands to a specific dialog
 * If hDlg is NULL, apply the commands against an active Window
 * TODO: if dlg_id is <0, apply all
 */
void apply_localization(int dlg_id, HWND hDlg)
{
	loc_cmd* lcmd;
	HWND hCtrl = NULL;
	int id_start = IDD_DIALOG, id_end = IDD_DIALOG + ARRAYSIZE(loc_dlg);
	LONG_PTR style;
	BOOL left_to_right = FALSE;

	if ((dlg_id >= id_start) && (dlg_id < id_end)) {
		// If we have a valid dialog_id, just process that one dialog
		id_start = dlg_id;
		id_end = dlg_id + 1;
		if (hDlg != NULL) {
			loc_dlg[dlg_id-IDD_DIALOG].hDlg = hDlg;
		}
	}

	for (dlg_id = id_start; dlg_id < id_end; dlg_id++) {
		hDlg = loc_dlg[dlg_id-IDD_DIALOG].hDlg;
		if ((!IsWindow(hDlg)) || (list_empty(&loc_dlg[dlg_id-IDD_DIALOG].list)))
			continue;

		// TODO: storing the messages in an array indexed on the message ID - 3000 would be faster
		list_for_each_entry(lcmd, &loc_dlg[dlg_id-IDD_DIALOG].list, loc_cmd, list) {
			if (lcmd->command <= LC_TEXT) { // TODO: should always be the case
				if (lcmd->ctrl_id == dlg_id) {
					if ((dlg_id == IDD_DIALOG) && (lcmd->txt[1] != NULL) && (lcmd->txt[1][0] != 0)) {
						loc_line_nr = lcmd->line_nr;
						luprint("operation forbidden (main dialog title cannot be changed)");
						continue;
					}
					hCtrl = hDlg;
				} else {
					hCtrl = GetDlgItem(hDlg, lcmd->ctrl_id);
				}
				if (hCtrl == NULL) {
					loc_line_nr = lcmd->line_nr;
					luprintf("control '%s' is not part of dialog '%s'\n",
						lcmd->txt[0], control_id[dlg_id-IDD_DIALOG].name);
				}
			}

			switch(lcmd->command) {
			// NB: For commands that take an ID, ctrl_id is always a valid index at this stage
			case LC_TEXT:
				if (hCtrl != NULL) {
					if ((lcmd->txt[1] != NULL) && (lcmd->txt[1][0] != 0))
						SetWindowTextU(hCtrl, lcmd->txt[1]);
					if (left_to_right) {
						style = GetWindowLongPtr(hCtrl, GWL_EXSTYLE);
						style |= WS_EX_LAYOUTRTL; // WS_EX_RIGHT | WS_EX_RTLREADING
						SetWindowLongPtr(hCtrl, GWL_EXSTYLE, style);
						InvalidateRect(hCtrl, NULL, TRUE);
					}
				}
				break;
			case LC_MOVE:
				if (hCtrl != NULL) {
					ResizeMoveCtrl(hDlg, hCtrl, lcmd->num[0], lcmd->num[1], 0, 0);
				}
				break;
			case LC_SIZE:
				if (hCtrl != NULL) {
					ResizeMoveCtrl(hDlg, hCtrl, 0, 0, lcmd->num[0], lcmd->num[1]);
				}
				break;
			}
		}
	}
}

/*
 * This function should be called when a localized dialog is destroyed
 * NB: we can't use isWindow() against our existing HWND to avoid this call
 * as handles are recycled.
 */
void reset_localization(int dlg_id)
{
	loc_dlg[dlg_id-IDD_DIALOG].hDlg = NULL;
}

/*
 * Produce a formatted localized message.
 * Like printf, this call takes a variable number of argument, and uses
 * the message ID to identify the formatted message to use.
 * Uses a rolling list of buffers to allow concurrency
 * TODO: use dynamic realloc'd buffer in case LOC_MESSAGE_SIZE is not enough
 */
char* lmprintf(int msg_id, ...)
{
	static int buf_id = 0;
	static char buf[LOC_MESSAGE_NB][LOC_MESSAGE_SIZE];
	char *format = NULL;
	va_list args;
	loc_cmd* lcmd;
	buf_id %= LOC_MESSAGE_NB;
	buf[buf_id][0] = 0;
	list_for_each_entry(lcmd, &loc_dlg[IDD_MESSAGES-IDD_DIALOG].list, loc_cmd, list) {
		if ((lcmd->command == LC_TEXT) && (lcmd->ctrl_id == msg_id) && (lcmd->txt[1] != NULL)) {
			format = lcmd->txt[1];
		}
	}

	if (format == NULL) {
		safe_sprintf(buf[buf_id], LOC_MESSAGE_SIZE-1, "MSG_%03d UNTRANSLATED", msg_id - MSG_000);
	} else {
		va_start(args, msg_id);
		safe_vsnprintf(buf[buf_id], LOC_MESSAGE_SIZE-1, format, args);
		va_end(args);
		buf[buf_id][LOC_MESSAGE_SIZE-1] = '\0';
	}
	return buf[buf_id++];
}

/*
 * These 2 functions are used to set the current locale
 */
loc_cmd* get_locale_from_lcid(int lcid)
{
	loc_cmd* lcmd = NULL;
	int i;

	if (list_empty(&locale_list)) {
		uprintf("localization: the locale list is empty!\n");
		return NULL;
	}

	list_for_each_entry(lcmd, &locale_list, loc_cmd, list) {
		for (i=0; i<lcmd->unum_size; i++) {
			if (lcmd->unum[i] == lcid) {
				return lcmd;
			}
		}
	}

	lcmd = list_entry(locale_list.next, loc_cmd, list);
	// If we couldn't find a supported locale, just pick the first one (usually English)
	uprintf("localization: could not find locale for LCID: 0x%04X. Will default to '%s'\n", lcid, lcmd->txt[0]);
	return lcmd;
}

loc_cmd* get_locale_from_name(char* locale_name)
{
	loc_cmd* lcmd = NULL;

	if (list_empty(&locale_list)) {
		uprintf("localization: the locale list is empty!\n");
		return NULL;
	}

	list_for_each_entry(lcmd, &locale_list, loc_cmd, list) {
		if (safe_strcmp(lcmd->txt[0], locale_name) == 0)
			return lcmd;
	}

	lcmd = list_entry(locale_list.next, loc_cmd, list);
	uprintf("localization: could not find locale for name '%s'. Will default to '%s'\n", locale_name, lcmd->txt[0]);
	return lcmd;
}
