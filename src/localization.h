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
#include <stddef.h>

#pragma once

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

// Commands that take a control ID *MUST* be at the top
// The last command with a control ID *MUST* be LC_TEXT

#define luprint(msg) uprintf("%s(%d): " msg "\n", loc_filename, loc_line_nr)
#define luprintf(msg, ...) uprintf("%s(%d): " msg "\n", loc_filename, loc_line_nr, __VA_ARGS__)

/*
 * List handling functions (stolen from libusb)
 * NB: offsetof() requires '#include <stddef.h>'
 */
struct list_head {
	struct list_head *prev, *next;
};

/* Get an entry from the list
 *  ptr - the address of this list_head element in "type"
 *  type - the data type that contains "member"
 *  member - the list_head element in "type"
 */
#define list_entry(ptr, type, member) \
	((type *)((uintptr_t)(ptr) - (uintptr_t)offsetof(type, member)))

/* Get each entry from a list
 *  pos - A structure pointer has a "member" element
 *  head - list head
 *  member - the list_head element in "pos"
 *  type - the type of the first parameter
 */
#define list_for_each_entry(pos, head, member, type)			\
	for (pos = list_entry((head)->next, type, member);			\
		 &pos->member != (head);								\
		 pos = list_entry(pos->member.next, type, member))


#define list_for_each_entry_safe(pos, n, head, member, type)	\
	for (pos = list_entry((head)->next, type, member),			\
		 n = list_entry(pos->member.next, type, member);		\
		 &pos->member != (head);								\
		 pos = n, n = list_entry(n->member.next, type, member))

#define list_empty(entry) ((entry)->next == (entry))

static __inline void list_init(struct list_head *entry)
{
	entry->prev = entry->next = entry;
}

static __inline void list_add(struct list_head *entry, struct list_head *head)
{
	entry->next = head->next;
	entry->prev = head;

	head->next->prev = entry;
	head->next = entry;
}

static __inline void list_add_tail(struct list_head *entry,
	struct list_head *head)
{
	entry->next = head;
	entry->prev = head->prev;

	head->prev->next = entry;
	head->prev = entry;
}

static __inline void list_del(struct list_head *entry)
{
	entry->next->prev = entry->prev;
	entry->prev->next = entry->next;
	entry->next = entry->prev = NULL;
}


enum loc_command_type {
	LC_PARENT,
	LC_MOVE,
	LC_RESIZE,
	LC_TEXT,	// Delimits commands that take a Control ID and commands that don't
	LC_VERSION,
	LC_LOCALE,
	LC_FONT,
	LC_DIRECTION,
};

typedef struct loc_cmd_struct {
	int command;
	int ctrl_id;
	uint32_t line_nr;
	char* text[2];
	int32_t num[2];
	struct list_head list;
} loc_cmd;

typedef struct loc_parse_struct {
	char  c;
	enum  loc_command_type cmd;
	char* arg_type;
} loc_parse;

typedef struct loc_control_id_struct {
	const char* name;
	const int id;
} loc_control_id;

typedef struct loc_dlg_list_struct {
	const int dlg_id;
	HWND hDlg;
	struct list_head list;
} loc_dlg_list;

loc_parse parse_cmd[];
const size_t PARSE_CMD_SIZE;
int loc_line_nr;
char loc_filename[32];

void free_loc_cmd(loc_cmd* lcmd);
BOOL dispatch_loc_cmd(loc_cmd* lcmd);
void init_localization(void);
void apply_localization(int dlg_id, HWND hDlg);
void reset_localization(int dlg_id);
void free_loc_dlg(void);
