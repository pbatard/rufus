/* ezxml.h
 *
 * Copyright 2004-2006 Aaron Voisine <aaron@voisine.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _EZXML_H
#define _EZXML_H

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EZXML_BUFSIZE 1024 // size of internal memory buffers
#define EZXML_NAMEM   0x80 // name is malloced
#define EZXML_TXTM    0x40 // txt is malloced
#define EZXML_DUP     0x20 // attribute name and value are strduped

typedef struct ezxml *ezxml_t;
struct ezxml {
    char *name;      // tag name
    char **attr;     // tag attributes { name, value, name, value, ... NULL }
    char *txt;       // tag character content, empty string if none
    size_t off;      // tag offset from start of parent tag character content
    ezxml_t next;    // next tag with same name in this section at this depth
    ezxml_t sibling; // next tag with different name in same section and depth
    ezxml_t ordered; // next tag, same section and depth, in original order
    ezxml_t child;   // head of sub tag list, NULL if none
    ezxml_t parent;  // parent tag, NULL if current tag is root tag
    short flags;     // additional information
};

// Given a string of xml data and its length, parses it and creates an ezxml
// structure. For efficiency, modifies the data by adding null terminators
// and decoding ampersand sequences. If you don't want this, copy the data and
// pass in the copy. Returns NULL on failure.
ezxml_t ezxml_parse_str(char *s, size_t len);

// A wrapper for ezxml_parse_str() that accepts a file descriptor. First
// attempts to mem map the file. Failing that, reads the file into memory.
// Returns NULL on failure.
ezxml_t ezxml_parse_fd(int fd);

// a wrapper for ezxml_parse_fd() that accepts a file name
ezxml_t ezxml_parse_file(const char *file);
    
// Wrapper for ezxml_parse_str() that accepts a file stream. Reads the entire
// stream into memory and then parses it. For xml files, use ezxml_parse_file()
// or ezxml_parse_fd()
ezxml_t ezxml_parse_fp(FILE *fp);

// returns the first child tag (one level deeper) with the given name or NULL
// if not found
ezxml_t ezxml_child(ezxml_t xml, const char *name);

// returns the next tag of the same name in the same section and depth or NULL
// if not found
#define ezxml_next(xml) ((xml) ? xml->next : NULL)

// Returns the Nth tag with the same name in the same section at the same depth
// or NULL if not found. An index of 0 returns the tag given.
ezxml_t ezxml_idx(ezxml_t xml, int idx);

// returns the name of the given tag
#define ezxml_name(xml) ((xml) ? xml->name : NULL)

// returns the given tag's character content or empty string if none
#define ezxml_txt(xml) ((xml) ? xml->txt : "")

// returns the value of the requested tag attribute, or NULL if not found
const char *ezxml_attr(ezxml_t xml, const char *attr);

// Traverses the ezxml sturcture to retrieve a specific subtag. Takes a
// variable length list of tag names and indexes. The argument list must be
// terminated by either an index of -1 or an empty string tag name. Example: 
// title = ezxml_get(library, "shelf", 0, "book", 2, "title", -1);
// This retrieves the title of the 3rd book on the 1st shelf of library.
// Returns NULL if not found.
ezxml_t ezxml_get(ezxml_t xml, ...);

// Converts an ezxml structure back to xml. Returns a string of xml data that
// must be freed.
char *ezxml_toxml(ezxml_t xml);

// returns a NULL terminated array of processing instructions for the given
// target
const char **ezxml_pi(ezxml_t xml, const char *target);

// frees the memory allocated for an ezxml structure
void ezxml_free(ezxml_t xml);
    
// returns parser error message or empty string if none
const char *ezxml_error(ezxml_t xml);

// returns a new empty ezxml structure with the given root tag name
ezxml_t ezxml_new(const char *name);

// wrapper for ezxml_new() that strdup()s name
#define ezxml_new_d(name) ezxml_set_flag(ezxml_new(strdup(name)), EZXML_NAMEM)

// Adds a child tag. off is the offset of the child tag relative to the start
// of the parent tag's character content. Returns the child tag.
ezxml_t ezxml_add_child(ezxml_t xml, const char *name, size_t off);

// wrapper for ezxml_add_child() that strdup()s name
#define ezxml_add_child_d(xml, name, off) \
    ezxml_set_flag(ezxml_add_child(xml, strdup(name), off), EZXML_NAMEM)

// sets the character content for the given tag and returns the tag
ezxml_t ezxml_set_txt(ezxml_t xml, const char *txt);

// wrapper for ezxml_set_txt() that strdup()s txt
#define ezxml_set_txt_d(xml, txt) \
    ezxml_set_flag(ezxml_set_txt(xml, strdup(txt)), EZXML_TXTM)

// Sets the given tag attribute or adds a new attribute if not found. A value
// of NULL will remove the specified attribute. Returns the tag given.
ezxml_t ezxml_set_attr(ezxml_t xml, const char *name, const char *value);

// Wrapper for ezxml_set_attr() that strdup()s name/value. Value cannot be NULL
#define ezxml_set_attr_d(xml, name, value) \
    ezxml_set_attr(ezxml_set_flag(xml, EZXML_DUP), strdup(name), strdup(value))

// sets a flag for the given tag and returns the tag
ezxml_t ezxml_set_flag(ezxml_t xml, short flag);

// removes a tag along with its subtags without freeing its memory
ezxml_t ezxml_cut(ezxml_t xml);

// inserts an existing tag into an ezxml structure
ezxml_t ezxml_insert(ezxml_t xml, ezxml_t dest, size_t off);

// Moves an existing tag to become a subtag of dest at the given offset from
// the start of dest's character content. Returns the moved tag.
#define ezxml_move(xml, dest, off) ezxml_insert(ezxml_cut(xml), dest, off)

// removes a tag along with all its subtags
#define ezxml_remove(xml) ezxml_free(ezxml_cut(xml))

#ifdef __cplusplus
}
#endif

#endif // _EZXML_H
