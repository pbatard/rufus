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

// Number of concurrent localization messages. Must be a power of 2.
#define LOC_MESSAGE_NB    8
#define LOC_MESSAGE_SIZE  2048

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
#define list_for_each_entry(pos, head, type, member)			\
	for (pos = list_entry((head)->next, type, member);			\
		 &pos->member != (head);								\
		 pos = list_entry(pos->member.next, type, member))


#define list_for_each_entry_safe(pos, n, head, type, member)	\
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

// Commands that take a control ID *MUST* be at the top
// The last command with a control ID *MUST* be LC_TEXT
enum loc_command_type {
	LC_GROUP,
	LC_MOVE,
	LC_SIZE,
	LC_TEXT,	// Delimits commands that take a Control ID and commands that don't
	LC_VERSION,
	LC_LOCALE,
	LC_BASE,
	LC_FONT,
	LC_DIRECTION,
};

typedef struct loc_cmd_struct {
	uint8_t		command;
	uint8_t		unum_size;
	uint16_t	line_nr;
	int			ctrl_id;
	int32_t		num[2];
	uint32_t*	unum;
	char*		txt[2];
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

extern const loc_parse parse_cmd[9];
extern struct list_head locale_list;
int loc_line_nr;
char *loc_filename, *embedded_loc_filename;

void free_loc_cmd(loc_cmd* lcmd);
BOOL dispatch_loc_cmd(loc_cmd* lcmd);
void init_localization(void);
void exit_localization(void);
void apply_localization(int dlg_id, HWND hDlg);
void reset_localization(int dlg_id);
void free_dialog_list(void);
char* lmprintf(int msg_id, ...);
BOOL get_supported_locales(const char* filename);
char* get_loc_data_file(const char* filename, long offset, long end_offset, int start_line);
void free_locale_list(void);
loc_cmd* get_locale_from_lcid(int lcid);
loc_cmd* get_locale_from_name(char* locale_name);
