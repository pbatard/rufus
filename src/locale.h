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

#include <stdint.h>

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

enum loc_command_type {
	LC_VERSION,
	LC_LOCALE,
	LC_FONT,
	LC_PARENT,
	LC_DIRECTION,
	LC_RESIZE,
	LC_MOVE,
	LC_TEXT
};

typedef struct loc_cmd_struct {
	int command;
	char* text[2];
	int32_t num[2];
} loc_cmd;

typedef struct loc_parse_struct {
	char  c;
	enum  loc_command_type cmd;
	char* arg_type;
} loc_parse;

loc_parse parse_cmd[];
size_t PARSE_CMD_SIZE;

void free_loc_cmd(loc_cmd* lcmd);
BOOL execute_loc_cmd(loc_cmd* lcmd);
