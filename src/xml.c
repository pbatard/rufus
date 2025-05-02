/* ezxml.c
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

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#ifndef EZXML_NOMMAP
#include <sys/mman.h>
#endif // EZXML_NOMMAP
#include <sys/stat.h>
#include "ezxml.h"

#define EZXML_WS   "\t\r\n "  // whitespace
#define EZXML_ERRL 128        // maximum error string length

typedef struct ezxml_root *ezxml_root_t;
struct ezxml_root {       // additional data for the root tag
    struct ezxml xml;     // is a super-struct built on top of ezxml struct
    ezxml_t cur;          // current xml tree insertion point
    char *m;              // original xml string
    size_t len;           // length of allocated memory for mmap, -1 for malloc
    char *u;              // UTF-8 conversion of string if original was UTF-16
    char *s;              // start of work area
    char *e;              // end of work area
    char **ent;           // general entities (ampersand sequences)
    char ***attr;         // default attributes
    char ***pi;           // processing instructions
    short standalone;     // non-zero if <?xml standalone="yes"?>
    char err[EZXML_ERRL]; // error string
};

char *EZXML_NIL[] = { NULL }; // empty, null terminated array of strings

// returns the first child tag with the given name or NULL if not found
ezxml_t ezxml_child(ezxml_t xml, const char *name)
{
    xml = (xml) ? xml->child : NULL;
    while (xml && strcmp(name, xml->name)) xml = xml->sibling;
    return xml;
}

// returns the Nth tag with the same name in the same subsection or NULL if not
// found
ezxml_t ezxml_idx(ezxml_t xml, int idx)
{
    for (; xml && idx; idx--) xml = xml->next;
    return xml;
}

// returns the value of the requested tag attribute or NULL if not found
const char *ezxml_attr(ezxml_t xml, const char *attr)
{
    int i = 0, j = 1;
    ezxml_root_t root = (ezxml_root_t)xml;

    if (! xml || ! xml->attr) return NULL;
    while (xml->attr[i] && strcmp(attr, xml->attr[i])) i += 2;
    if (xml->attr[i]) return xml->attr[i + 1]; // found attribute

    while (root->xml.parent) root = (ezxml_root_t)root->xml.parent; // root tag
    for (i = 0; root->attr[i] && strcmp(xml->name, root->attr[i][0]); i++);
    if (! root->attr[i]) return NULL; // no matching default attributes
    while (root->attr[i][j] && strcmp(attr, root->attr[i][j])) j += 3;
    return (root->attr[i][j]) ? root->attr[i][j + 1] : NULL; // found default
}

// same as ezxml_get but takes an already initialized va_list
ezxml_t ezxml_vget(ezxml_t xml, va_list ap)
{
    char *name = va_arg(ap, char *);
    int idx = -1;

    if (name && *name) {
        idx = va_arg(ap, int);    
        xml = ezxml_child(xml, name);
    }
    return (idx < 0) ? xml : ezxml_vget(ezxml_idx(xml, idx), ap);
}

// Traverses the xml tree to retrieve a specific subtag. Takes a variable
// length list of tag names and indexes. The argument list must be terminated
// by either an index of -1 or an empty string tag name. Example: 
// title = ezxml_get(library, "shelf", 0, "book", 2, "title", -1);
// This retrieves the title of the 3rd book on the 1st shelf of library.
// Returns NULL if not found.
ezxml_t ezxml_get(ezxml_t xml, ...)
{
    va_list ap;
    ezxml_t r;

    va_start(ap, xml);
    r = ezxml_vget(xml, ap);
    va_end(ap);
    return r;
}

// returns a null terminated array of processing instructions for the given
// target
const char **ezxml_pi(ezxml_t xml, const char *target)
{
    ezxml_root_t root = (ezxml_root_t)xml;
    int i = 0;

    if (! root) return (const char **)EZXML_NIL;
    while (root->xml.parent) root = (ezxml_root_t)root->xml.parent; // root tag
    while (root->pi[i] && strcmp(target, root->pi[i][0])) i++; // find target
    return (const char **)((root->pi[i]) ? root->pi[i] + 1 : EZXML_NIL);
}

// set an error string and return root
ezxml_t ezxml_err(ezxml_root_t root, char *s, const char *err, ...)
{
    va_list ap;
    int line = 1;
    char *t, fmt[EZXML_ERRL];
    
    for (t = root->s; t < s; t++) if (*t == '\n') line++;
    snprintf(fmt, EZXML_ERRL, "[error near line %d]: %s", line, err);

    va_start(ap, err);
    vsnprintf(root->err, EZXML_ERRL, fmt, ap);
    va_end(ap);

    return &root->xml;
}

// Recursively decodes entity and character references and normalizes new lines
// ent is a null terminated array of alternating entity names and values. set t
// to '&' for general entity decoding, '%' for parameter entity decoding, 'c'
// for cdata sections, ' ' for attribute normalization, or '*' for non-cdata
// attribute normalization. Returns s, or if the decoded string is longer than
// s, returns a malloced string that must be freed.
char *ezxml_decode(char *s, char **ent, char t)
{
    char *e, *r = s, *m = s;
    long b, c, d, l;

    for (; *s; s++) { // normalize line endings
        while (*s == '\r') {
            *(s++) = '\n';
            if (*s == '\n') memmove(s, (s + 1), strlen(s));
        }
    }
    
    for (s = r; ; ) {
        while (*s && *s != '&' && (*s != '%' || t != '%') && !isspace(*s)) s++;

        if (! *s) break;
        else if (t != 'c' && ! strncmp(s, "&#", 2)) { // character reference
            if (s[2] == 'x') c = strtol(s + 3, &e, 16); // base 16
            else c = strtol(s + 2, &e, 10); // base 10
            if (! c || *e != ';') { s++; continue; } // not a character ref

            if (c < 0x80) *(s++) = c; // US-ASCII subset
            else { // multi-byte UTF-8 sequence
                for (b = 0, d = c; d; d /= 2) b++; // number of bits in c
                b = (b - 2) / 5; // number of bytes in payload
                *(s++) = (0xFF << (7 - b)) | (c >> (6 * b)); // head
                while (b) *(s++) = 0x80 | ((c >> (6 * --b)) & 0x3F); // payload
            }

            memmove(s, strchr(s, ';') + 1, strlen(strchr(s, ';')));
        }
        else if ((*s == '&' && (t == '&' || t == ' ' || t == '*')) ||
                 (*s == '%' && t == '%')) { // entity reference
            for (b = 0; ent[b] && strncmp(s + 1, ent[b], strlen(ent[b]));
                 b += 2); // find entity in entity list

            if (ent[b++]) { // found a match
                if ((c = strlen(ent[b])) - 1 > (e = strchr(s, ';')) - s) {
                    l = (d = (s - r)) + c + strlen(e); // new length
                    r = (r == m) ? strcpy(malloc(l), r) : realloc(r, l);
                    e = strchr((s = r + d), ';'); // fix up pointers
                }

                memmove(s + c, e + 1, strlen(e)); // shift rest of string
                strncpy(s, ent[b], c); // copy in replacement text
            }
            else s++; // not a known entity
        }
        else if ((t == ' ' || t == '*') && isspace(*s)) *(s++) = ' ';
        else s++; // no decoding needed
    }

    if (t == '*') { // normalize spaces for non-cdata attributes
        for (s = r; *s; s++) {
            if ((l = strspn(s, " "))) memmove(s, s + l, strlen(s + l) + 1);
            while (*s && *s != ' ') s++;
        }
        if (--s >= r && *s == ' ') *s = '\0'; // trim any trailing space
    }
    return r;
}

// called when parser finds start of new tag
void ezxml_open_tag(ezxml_root_t root, char *name, char **attr)
{
    ezxml_t xml = root->cur;
    
    if (xml->name) xml = ezxml_add_child(xml, name, strlen(xml->txt));
    else xml->name = name; // first open tag

    xml->attr = attr;
    root->cur = xml; // update tag insertion point
}

// called when parser finds character content between open and closing tag
void ezxml_char_content(ezxml_root_t root, char *s, size_t len, char t)
{
    ezxml_t xml = root->cur;
    char *m = s;
    size_t l;

    if (! xml || ! xml->name || ! len) return; // sanity check

    s[len] = '\0'; // null terminate text (calling functions anticipate this)
    len = strlen(s = ezxml_decode(s, root->ent, t)) + 1;

    if (! *(xml->txt)) xml->txt = s; // initial character content
    else { // allocate our own memory and make a copy
        xml->txt = (xml->flags & EZXML_TXTM) // allocate some space
                   ? realloc(xml->txt, (l = strlen(xml->txt)) + len)
                   : strcpy(malloc((l = strlen(xml->txt)) + len), xml->txt);
        strcpy(xml->txt + l, s); // add new char content
        if (s != m) free(s); // free s if it was malloced by ezxml_decode()
    }

    if (xml->txt != m) ezxml_set_flag(xml, EZXML_TXTM);
}

// called when parser finds closing tag
ezxml_t ezxml_close_tag(ezxml_root_t root, char *name, char *s)
{
    if (! root->cur || ! root->cur->name || strcmp(name, root->cur->name))
        return ezxml_err(root, s, "unexpected closing tag </%s>", name);

    root->cur = root->cur->parent;
    return NULL;
}

// checks for circular entity references, returns non-zero if no circular
// references are found, zero otherwise
int ezxml_ent_ok(char *name, char *s, char **ent)
{
    int i;

    for (; ; s++) {
        while (*s && *s != '&') s++; // find next entity reference
        if (! *s) return 1;
        if (! strncmp(s + 1, name, strlen(name))) return 0; // circular ref.
        for (i = 0; ent[i] && strncmp(ent[i], s + 1, strlen(ent[i])); i += 2);
        if (ent[i] && ! ezxml_ent_ok(name, ent[i + 1], ent)) return 0;
    }
}

// called when the parser finds a processing instruction
void ezxml_proc_inst(ezxml_root_t root, char *s, size_t len)
{
    int i = 0, j = 1;
    char *target = s;

    s[len] = '\0'; // null terminate instruction
    if (*(s += strcspn(s, EZXML_WS))) {
        *s = '\0'; // null terminate target
        s += strspn(s + 1, EZXML_WS) + 1; // skip whitespace after target
    }

    if (! strcmp(target, "xml")) { // <?xml ... ?>
        if ((s = strstr(s, "standalone")) && ! strncmp(s + strspn(s + 10,
            EZXML_WS "='\"") + 10, "yes", 3)) root->standalone = 1;
        return;
    }

    if (! root->pi[0]) *(root->pi = malloc(sizeof(char **))) = NULL; //first pi

    while (root->pi[i] && strcmp(target, root->pi[i][0])) i++; // find target
    if (! root->pi[i]) { // new target
        root->pi = realloc(root->pi, sizeof(char **) * (i + 2));
        root->pi[i] = malloc(sizeof(char *) * 3);
        root->pi[i][0] = target;
        root->pi[i][1] = (char *)(root->pi[i + 1] = NULL); // terminate pi list
        root->pi[i][2] = strdup(""); // empty document position list
    }

    while (root->pi[i][j]) j++; // find end of instruction list for this target
    root->pi[i] = realloc(root->pi[i], sizeof(char *) * (j + 3));
    root->pi[i][j + 2] = realloc(root->pi[i][j + 1], j + 1);
    strcpy(root->pi[i][j + 2] + j - 1, (root->xml.name) ? ">" : "<");
    root->pi[i][j + 1] = NULL; // null terminate pi list for this target
    root->pi[i][j] = s; // set instruction
}

// called when the parser finds an internal doctype subset
short ezxml_internal_dtd(ezxml_root_t root, char *s, size_t len)
{
    char q, *c, *t, *n = NULL, *v, **ent, **pe;
    int i, j;
    
    pe = memcpy(malloc(sizeof(EZXML_NIL)), EZXML_NIL, sizeof(EZXML_NIL));

    for (s[len] = '\0'; s; ) {
        while (*s && *s != '<' && *s != '%') s++; // find next declaration

        if (! *s) break;
        else if (! strncmp(s, "<!ENTITY", 8)) { // parse entity definitions
            c = s += strspn(s + 8, EZXML_WS) + 8; // skip white space separator
            n = s + strspn(s, EZXML_WS "%"); // find name
            *(s = n + strcspn(n, EZXML_WS)) = ';'; // append ; to name

            v = s + strspn(s + 1, EZXML_WS) + 1; // find value
            if ((q = *(v++)) != '"' && q != '\'') { // skip externals
                s = strchr(s, '>');
                continue;
            }

            for (i = 0, ent = (*c == '%') ? pe : root->ent; ent[i]; i++);
            ent = realloc(ent, (i + 3) * sizeof(char *)); // space for next ent
            if (*c == '%') pe = ent;
            else root->ent = ent;

            *(++s) = '\0'; // null terminate name
            if ((s = strchr(v, q))) *(s++) = '\0'; // null terminate value
            ent[i + 1] = ezxml_decode(v, pe, '%'); // set value
            ent[i + 2] = NULL; // null terminate entity list
            if (! ezxml_ent_ok(n, ent[i + 1], ent)) { // circular reference
                if (ent[i + 1] != v) free(ent[i + 1]);
                ezxml_err(root, v, "circular entity declaration &%s", n);
                break;
            }
            else ent[i] = n; // set entity name
        }
        else if (! strncmp(s, "<!ATTLIST", 9)) { // parse default attributes
            t = s + strspn(s + 9, EZXML_WS) + 9; // skip whitespace separator
            if (! *t) { ezxml_err(root, t, "unclosed <!ATTLIST"); break; }
            if (*(s = t + strcspn(t, EZXML_WS ">")) == '>') continue;
            else *s = '\0'; // null terminate tag name
            for (i = 0; root->attr[i] && strcmp(n, root->attr[i][0]); i++);

            while (*(n = ++s + strspn(s, EZXML_WS)) && *n != '>') {
                if (*(s = n + strcspn(n, EZXML_WS))) *s = '\0'; // attr name
                else { ezxml_err(root, t, "malformed <!ATTLIST"); break; }

                s += strspn(s + 1, EZXML_WS) + 1; // find next token
                c = (strncmp(s, "CDATA", 5)) ? "*" : " "; // is it cdata?
                if (! strncmp(s, "NOTATION", 8))
                    s += strspn(s + 8, EZXML_WS) + 8;
                s = (*s == '(') ? strchr(s, ')') : s + strcspn(s, EZXML_WS);
                if (! s) { ezxml_err(root, t, "malformed <!ATTLIST"); break; }

                s += strspn(s, EZXML_WS ")"); // skip white space separator
                if (! strncmp(s, "#FIXED", 6))
                    s += strspn(s + 6, EZXML_WS) + 6;
                if (*s == '#') { // no default value
                    s += strcspn(s, EZXML_WS ">") - 1;
                    if (*c == ' ') continue; // cdata is default, nothing to do
                    v = NULL;
                }
                else if ((*s == '"' || *s == '\'')  &&  // default value
                         (s = strchr(v = s + 1, *s))) *s = '\0';
                else { ezxml_err(root, t, "malformed <!ATTLIST"); break; }

                if (! root->attr[i]) { // new tag name
                    root->attr = (! i) ? malloc(2 * sizeof(char **))
                                       : realloc(root->attr,
                                                 (i + 2) * sizeof(char **));
                    root->attr[i] = malloc(2 * sizeof(char *));
                    root->attr[i][0] = t; // set tag name
                    root->attr[i][1] = (char *)(root->attr[i + 1] = NULL);
                }

                for (j = 1; root->attr[i][j]; j += 3); // find end of list
                root->attr[i] = realloc(root->attr[i],
                                        (j + 4) * sizeof(char *));

                root->attr[i][j + 3] = NULL; // null terminate list
                root->attr[i][j + 2] = c; // is it cdata?
                root->attr[i][j + 1] = (v) ? ezxml_decode(v, root->ent, *c)
                                           : NULL;
                root->attr[i][j] = n; // attribute name 
            }
        }
        else if (! strncmp(s, "<!--", 4)) s = strstr(s + 4, "-->"); // comments
        else if (! strncmp(s, "<?", 2)) { // processing instructions
            if ((s = strstr(c = s + 2, "?>")))
                ezxml_proc_inst(root, c, s++ - c);
        }
        else if (*s == '<') s = strchr(s, '>'); // skip other declarations
        else if (*(s++) == '%' && ! root->standalone) break;
    }

    free(pe);
    return ! *root->err;
}

// Converts a UTF-16 string to UTF-8. Returns a new string that must be freed
// or NULL if no conversion was needed.
char *ezxml_str2utf8(char **s, size_t *len)
{
    char *u;
    size_t l = 0, sl, max = *len;
    long c, d;
    int b, be = (**s == '\xFE') ? 1 : (**s == '\xFF') ? 0 : -1;

    if (be == -1) return NULL; // not UTF-16

    u = malloc(max);
    for (sl = 2; sl < *len - 1; sl += 2) {
        c = (be) ? (((*s)[sl] & 0xFF) << 8) | ((*s)[sl + 1] & 0xFF)  //UTF-16BE
                 : (((*s)[sl + 1] & 0xFF) << 8) | ((*s)[sl] & 0xFF); //UTF-16LE
        if (c >= 0xD800 && c <= 0xDFFF && (sl += 2) < *len - 1) { // high-half
            d = (be) ? (((*s)[sl] & 0xFF) << 8) | ((*s)[sl + 1] & 0xFF)
                     : (((*s)[sl + 1] & 0xFF) << 8) | ((*s)[sl] & 0xFF);
            c = (((c & 0x3FF) << 10) | (d & 0x3FF)) + 0x10000;
        }

        while (l + 6 > max) u = realloc(u, max += EZXML_BUFSIZE);
        if (c < 0x80) u[l++] = c; // US-ASCII subset
        else { // multi-byte UTF-8 sequence
            for (b = 0, d = c; d; d /= 2) b++; // bits in c
            b = (b - 2) / 5; // bytes in payload
            u[l++] = (0xFF << (7 - b)) | (c >> (6 * b)); // head
            while (b) u[l++] = 0x80 | ((c >> (6 * --b)) & 0x3F); // payload
        }
    }
    return *s = realloc(u, *len = l);
}

// frees a tag attribute list
void ezxml_free_attr(char **attr) {
    int i = 0;
    char *m;
    
    if (! attr || attr == EZXML_NIL) return; // nothing to free
    while (attr[i]) i += 2; // find end of attribute list
    m = attr[i + 1]; // list of which names and values are malloced
    for (i = 0; m[i]; i++) {
        if (m[i] & EZXML_NAMEM) free(attr[i * 2]);
        if (m[i] & EZXML_TXTM) free(attr[(i * 2) + 1]);
    }
    free(m);
    free(attr);
}

// parse the given xml string and return an ezxml structure
ezxml_t ezxml_parse_str(char *s, size_t len)
{
    ezxml_root_t root = (ezxml_root_t)ezxml_new(NULL);
    char q, e, *d, **attr, **a = NULL; // initialize a to avoid compile warning
    int l, i, j;

    root->m = s;
    if (! len) return ezxml_err(root, NULL, "root tag missing");
    root->u = ezxml_str2utf8(&s, &len); // convert utf-16 to utf-8
    root->e = (root->s = s) + len; // record start and end of work area
    
    e = s[len - 1]; // save end char
    s[len - 1] = '\0'; // turn end char into null terminator

    while (*s && *s != '<') s++; // find first tag
    if (! *s) return ezxml_err(root, s, "root tag missing");

    for (; ; ) {
        attr = (char **)EZXML_NIL;
        d = ++s;
        
        if (isalpha(*s) || *s == '_' || *s == ':' || *s < '\0') { // new tag
            if (! root->cur)
                return ezxml_err(root, d, "markup outside of root element");

            s += strcspn(s, EZXML_WS "/>");
            while (isspace(*s)) *(s++) = '\0'; // null terminate tag name
  
            if (*s && *s != '/' && *s != '>') // find tag in default attr list
                for (i = 0; (a = root->attr[i]) && strcmp(a[0], d); i++);

            for (l = 0; *s && *s != '/' && *s != '>'; l += 2) { // new attrib
                attr = (l) ? realloc(attr, (l + 4) * sizeof(char *))
                           : malloc(4 * sizeof(char *)); // allocate space
                attr[l + 3] = (l) ? realloc(attr[l + 1], (l / 2) + 2)
                                  : malloc(2); // mem for list of maloced vals
                strcpy(attr[l + 3] + (l / 2), " "); // value is not malloced
                attr[l + 2] = NULL; // null terminate list
                attr[l + 1] = ""; // temporary attribute value
                attr[l] = s; // set attribute name

                s += strcspn(s, EZXML_WS "=/>");
                if (*s == '=' || isspace(*s)) { 
                    *(s++) = '\0'; // null terminate tag attribute name
                    q = *(s += strspn(s, EZXML_WS "="));
                    if (q == '"' || q == '\'') { // attribute value
                        attr[l + 1] = ++s;
                        while (*s && *s != q) s++;
                        if (*s) *(s++) = '\0'; // null terminate attribute val
                        else {
                            ezxml_free_attr(attr);
                            return ezxml_err(root, d, "missing %c", q);
                        }

                        for (j = 1; a && a[j] && strcmp(a[j], attr[l]); j +=3);
                        attr[l + 1] = ezxml_decode(attr[l + 1], root->ent, (a
                                                   && a[j]) ? *a[j + 2] : ' ');
                        if (attr[l + 1] < d || attr[l + 1] > s)
                            attr[l + 3][l / 2] = EZXML_TXTM; // value malloced
                    }
                }
                while (isspace(*s)) s++;
            }

            if (*s == '/') { // self closing tag
                *(s++) = '\0';
                if ((*s && *s != '>') || (! *s && e != '>')) {
                    if (l) ezxml_free_attr(attr);
                    return ezxml_err(root, d, "missing >");
                }
                ezxml_open_tag(root, d, attr);
                ezxml_close_tag(root, d, s);
            }
            else if ((q = *s) == '>' || (! *s && e == '>')) { // open tag
                *s = '\0'; // temporarily null terminate tag name
                ezxml_open_tag(root, d, attr);
                *s = q;
            }
            else {
                if (l) ezxml_free_attr(attr);
                return ezxml_err(root, d, "missing >"); 
            }
        }
        else if (*s == '/') { // close tag
            s += strcspn(d = s + 1, EZXML_WS ">") + 1;
            if (! (q = *s) && e != '>') return ezxml_err(root, d, "missing >");
            *s = '\0'; // temporarily null terminate tag name
            if (ezxml_close_tag(root, d, s)) return &root->xml;
            if (isspace(*s = q)) s += strspn(s, EZXML_WS);
        }
        else if (! strncmp(s, "!--", 3)) { // xml comment
            if (! (s = strstr(s + 3, "--")) || (*(s += 2) != '>' && *s) ||
                (! *s && e != '>')) return ezxml_err(root, d, "unclosed <!--");
        }
        else if (! strncmp(s, "![CDATA[", 8)) { // cdata
            if ((s = strstr(s, "]]>")))
                ezxml_char_content(root, d + 8, (s += 2) - d - 10, 'c');
            else return ezxml_err(root, d, "unclosed <![CDATA[");
        }
        else if (! strncmp(s, "!DOCTYPE", 8)) { // dtd
            for (l = 0; *s && ((! l && *s != '>') || (l && (*s != ']' || 
                 *(s + strspn(s + 1, EZXML_WS) + 1) != '>')));
                 l = (*s == '[') ? 1 : l) s += strcspn(s + 1, "[]>") + 1;
            if (! *s && e != '>')
                return ezxml_err(root, d, "unclosed <!DOCTYPE");
            d = (l) ? strchr(d, '[') + 1 : d;
            if (l && ! ezxml_internal_dtd(root, d, s++ - d)) return &root->xml;
        }
        else if (*s == '?') { // <?...?> processing instructions
            do { s = strchr(s, '?'); } while (s && *(++s) && *s != '>');
            if (! s || (! *s && e != '>')) 
                return ezxml_err(root, d, "unclosed <?");
            else ezxml_proc_inst(root, d + 1, s - d - 2);
        }
        else return ezxml_err(root, d, "unexpected <");
        
        if (! s || ! *s) break;
        *s = '\0';
        d = ++s;
        if (*s && *s != '<') { // tag character content
            while (*s && *s != '<') s++;
            if (*s) ezxml_char_content(root, d, s - d, '&');
            else break;
        }
        else if (! *s) break;
    }

    if (! root->cur) return &root->xml;
    else if (! root->cur->name) return ezxml_err(root, d, "root tag missing");
    else return ezxml_err(root, d, "unclosed tag <%s>", root->cur->name);
}

// Wrapper for ezxml_parse_str() that accepts a file stream. Reads the entire
// stream into memory and then parses it. For xml files, use ezxml_parse_file()
// or ezxml_parse_fd()
ezxml_t ezxml_parse_fp(FILE *fp)
{
    ezxml_root_t root;
    size_t l, len = 0;
    char *s;

    if (! (s = malloc(EZXML_BUFSIZE))) return NULL;
    do {
        len += (l = fread((s + len), 1, EZXML_BUFSIZE, fp));
        if (l == EZXML_BUFSIZE) s = realloc(s, len + EZXML_BUFSIZE);
    } while (s && l == EZXML_BUFSIZE);

    if (! s) return NULL;
    root = (ezxml_root_t)ezxml_parse_str(s, len);
    root->len = -1; // so we know to free s in ezxml_free()
    return &root->xml;
}

// A wrapper for ezxml_parse_str() that accepts a file descriptor. First
// attempts to mem map the file. Failing that, reads the file into memory.
// Returns NULL on failure.
ezxml_t ezxml_parse_fd(int fd)
{
    ezxml_root_t root;
    struct stat st;
    size_t l;
    void *m;

    if (fd < 0) return NULL;
    fstat(fd, &st);

#ifndef EZXML_NOMMAP
    l = (st.st_size + sysconf(_SC_PAGESIZE) - 1) & ~(sysconf(_SC_PAGESIZE) -1);
    if ((m = mmap(NULL, l, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0)) !=
        MAP_FAILED) {
        madvise(m, l, MADV_SEQUENTIAL); // optimize for sequential access
        root = (ezxml_root_t)ezxml_parse_str(m, st.st_size);
        madvise(m, root->len = l, MADV_NORMAL); // put it back to normal
    }
    else { // mmap failed, read file into memory
#endif // EZXML_NOMMAP
        l = read(fd, m = malloc(st.st_size), st.st_size);
        root = (ezxml_root_t)ezxml_parse_str(m, l);
        root->len = -1; // so we know to free s in ezxml_free()
#ifndef EZXML_NOMMAP
    }
#endif // EZXML_NOMMAP
    return &root->xml;
}

// a wrapper for ezxml_parse_fd that accepts a file name
ezxml_t ezxml_parse_file(const char *file)
{
    int fd = open(file, O_RDONLY, 0);
    ezxml_t xml = ezxml_parse_fd(fd);
    
    if (fd >= 0) close(fd);
    return xml;
}

// Encodes ampersand sequences appending the results to *dst, reallocating *dst
// if length excedes max. a is non-zero for attribute encoding. Returns *dst
char *ezxml_ampencode(const char *s, size_t len, char **dst, size_t *dlen,
                      size_t *max, short a)
{
    const char *e;
    
    for (e = s + len; s != e; s++) {
        while (*dlen + 10 > *max) *dst = realloc(*dst, *max += EZXML_BUFSIZE);

        switch (*s) {
        case '\0': return *dst;
        case '&': *dlen += sprintf(*dst + *dlen, "&amp;"); break;
        case '<': *dlen += sprintf(*dst + *dlen, "&lt;"); break;
        case '>': *dlen += sprintf(*dst + *dlen, "&gt;"); break;
        case '"': *dlen += sprintf(*dst + *dlen, (a) ? "&quot;" : "\""); break;
        case '\n': *dlen += sprintf(*dst + *dlen, (a) ? "&#xA;" : "\n"); break;
        case '\t': *dlen += sprintf(*dst + *dlen, (a) ? "&#x9;" : "\t"); break;
        case '\r': *dlen += sprintf(*dst + *dlen, "&#xD;"); break;
        default: (*dst)[(*dlen)++] = *s;
        }
    }
    return *dst;
}

// Recursively converts each tag to xml appending it to *s. Reallocates *s if
// its length excedes max. start is the location of the previous tag in the
// parent tag's character content. Returns *s.
char *ezxml_toxml_r(ezxml_t xml, char **s, size_t *len, size_t *max,
                    size_t start, char ***attr)
{
    int i, j;
    char *txt = (xml->parent) ? xml->parent->txt : "";
    size_t off = 0;

    // parent character content up to this tag
    *s = ezxml_ampencode(txt + start, xml->off - start, s, len, max, 0);

    while (*len + strlen(xml->name) + 4 > *max) // reallocate s
        *s = realloc(*s, *max += EZXML_BUFSIZE);

    *len += sprintf(*s + *len, "<%s", xml->name); // open tag
    for (i = 0; xml->attr[i]; i += 2) { // tag attributes
        if (ezxml_attr(xml, xml->attr[i]) != xml->attr[i + 1]) continue;
        while (*len + strlen(xml->attr[i]) + 7 > *max) // reallocate s
            *s = realloc(*s, *max += EZXML_BUFSIZE);

        *len += sprintf(*s + *len, " %s=\"", xml->attr[i]);
        ezxml_ampencode(xml->attr[i + 1], -1, s, len, max, 1);
        *len += sprintf(*s + *len, "\"");
    }

    for (i = 0; attr[i] && strcmp(attr[i][0], xml->name); i++);
    for (j = 1; attr[i] && attr[i][j]; j += 3) { // default attributes
        if (! attr[i][j + 1] || ezxml_attr(xml, attr[i][j]) != attr[i][j + 1])
            continue; // skip duplicates and non-values
        while (*len + strlen(attr[i][j]) + 7 > *max) // reallocate s
            *s = realloc(*s, *max += EZXML_BUFSIZE);

        *len += sprintf(*s + *len, " %s=\"", attr[i][j]);
        ezxml_ampencode(attr[i][j + 1], -1, s, len, max, 1);
        *len += sprintf(*s + *len, "\"");
    }
    *len += sprintf(*s + *len, ">");

    *s = (xml->child) ? ezxml_toxml_r(xml->child, s, len, max, 0, attr) //child
                      : ezxml_ampencode(xml->txt, -1, s, len, max, 0);  //data
    
    while (*len + strlen(xml->name) + 4 > *max) // reallocate s
        *s = realloc(*s, *max += EZXML_BUFSIZE);

    *len += sprintf(*s + *len, "</%s>", xml->name); // close tag

    while (txt[off] && off < xml->off) off++; // make sure off is within bounds
    return (xml->ordered) ? ezxml_toxml_r(xml->ordered, s, len, max, off, attr)
                          : ezxml_ampencode(txt + off, -1, s, len, max, 0);
}

// Converts an ezxml structure back to xml. Returns a string of xml data that
// must be freed.
char *ezxml_toxml(ezxml_t xml)
{
    ezxml_t p = (xml) ? xml->parent : NULL, o = (xml) ? xml->ordered : NULL;
    ezxml_root_t root = (ezxml_root_t)xml;
    size_t len = 0, max = EZXML_BUFSIZE;
    char *s = strcpy(malloc(max), ""), *t, *n;
    int i, j, k;

    if (! xml || ! xml->name) return realloc(s, len + 1);
    while (root->xml.parent) root = (ezxml_root_t)root->xml.parent; // root tag

    for (i = 0; ! p && root->pi[i]; i++) { // pre-root processing instructions
        for (k = 2; root->pi[i][k - 1]; k++);
        for (j = 1; (n = root->pi[i][j]); j++) {
            if (root->pi[i][k][j - 1] == '>') continue; // not pre-root
            while (len + strlen(t = root->pi[i][0]) + strlen(n) + 7 > max)
                s = realloc(s, max += EZXML_BUFSIZE);
            len += sprintf(s + len, "<?%s%s%s?>\n", t, *n ? " " : "", n);
        }
    }

    xml->parent = xml->ordered = NULL;
    s = ezxml_toxml_r(xml, &s, &len, &max, 0, root->attr);
    xml->parent = p;
    xml->ordered = o;

    for (i = 0; ! p && root->pi[i]; i++) { // post-root processing instructions
        for (k = 2; root->pi[i][k - 1]; k++);
        for (j = 1; (n = root->pi[i][j]); j++) {
            if (root->pi[i][k][j - 1] == '<') continue; // not post-root
            while (len + strlen(t = root->pi[i][0]) + strlen(n) + 7 > max)
                s = realloc(s, max += EZXML_BUFSIZE);
            len += sprintf(s + len, "\n<?%s%s%s?>", t, *n ? " " : "", n);
        }
    }
    return realloc(s, len + 1);
}

// free the memory allocated for the ezxml structure
void ezxml_free(ezxml_t xml)
{
    ezxml_root_t root = (ezxml_root_t)xml;
    int i, j;
    char **a, *s;

    if (! xml) return;
    ezxml_free(xml->child);
    ezxml_free(xml->ordered);

    if (! xml->parent) { // free root tag allocations
        for (i = 10; root->ent[i]; i += 2) // 0 - 9 are default entites (<>&"')
            if ((s = root->ent[i + 1]) < root->s || s > root->e) free(s);
        free(root->ent); // free list of general entities

        for (i = 0; (a = root->attr[i]); i++) {
            for (j = 1; a[j++]; j += 2) // free malloced attribute values
                if (a[j] && (a[j] < root->s || a[j] > root->e)) free(a[j]);
            free(a);
        }
        if (root->attr[0]) free(root->attr); // free default attribute list

        for (i = 0; root->pi[i]; i++) {
            for (j = 1; root->pi[i][j]; j++);
            free(root->pi[i][j + 1]);
            free(root->pi[i]);
        }            
        if (root->pi[0]) free(root->pi); // free processing instructions

        if (root->len == -1) free(root->m); // malloced xml data
#ifndef EZXML_NOMMAP
        else if (root->len) munmap(root->m, root->len); // mem mapped xml data
#endif // EZXML_NOMMAP
        if (root->u) free(root->u); // utf8 conversion
    }

    ezxml_free_attr(xml->attr); // tag attributes
    if ((xml->flags & EZXML_TXTM)) free(xml->txt); // character content
    if ((xml->flags & EZXML_NAMEM)) free(xml->name); // tag name
    free(xml);
}

// return parser error message or empty string if none
const char *ezxml_error(ezxml_t xml)
{
    while (xml && xml->parent) xml = xml->parent; // find root tag
    return (xml) ? ((ezxml_root_t)xml)->err : "";
}

// returns a new empty ezxml structure with the given root tag name
ezxml_t ezxml_new(const char *name)
{
    static char *ent[] = { "lt;", "&#60;", "gt;", "&#62;", "quot;", "&#34;",
                           "apos;", "&#39;", "amp;", "&#38;", NULL };
    ezxml_root_t root = (ezxml_root_t)memset(malloc(sizeof(struct ezxml_root)), 
                                             '\0', sizeof(struct ezxml_root));
    root->xml.name = (char *)name;
    root->cur = &root->xml;
    strcpy(root->err, root->xml.txt = "");
    root->ent = memcpy(malloc(sizeof(ent)), ent, sizeof(ent));
    root->attr = root->pi = (char ***)(root->xml.attr = EZXML_NIL);
    return &root->xml;
}

// inserts an existing tag into an ezxml structure
ezxml_t ezxml_insert(ezxml_t xml, ezxml_t dest, size_t off)
{
    ezxml_t cur, prev, head;

    xml->next = xml->sibling = xml->ordered = NULL;
    xml->off = off;
    xml->parent = dest;

    if ((head = dest->child)) { // already have sub tags
        if (head->off <= off) { // not first subtag
            for (cur = head; cur->ordered && cur->ordered->off <= off;
                 cur = cur->ordered);
            xml->ordered = cur->ordered;
            cur->ordered = xml;
        }
        else { // first subtag
            xml->ordered = head;
            dest->child = xml;
        }

        for (cur = head, prev = NULL; cur && strcmp(cur->name, xml->name);
             prev = cur, cur = cur->sibling); // find tag type
        if (cur && cur->off <= off) { // not first of type
            while (cur->next && cur->next->off <= off) cur = cur->next;
            xml->next = cur->next;
            cur->next = xml;
        }
        else { // first tag of this type
            if (prev && cur) prev->sibling = cur->sibling; // remove old first
            xml->next = cur; // old first tag is now next
            for (cur = head, prev = NULL; cur && cur->off <= off;
                 prev = cur, cur = cur->sibling); // new sibling insert point
            xml->sibling = cur;
            if (prev) prev->sibling = xml;
        }
    }
    else dest->child = xml; // only sub tag

    return xml;
}

// Adds a child tag. off is the offset of the child tag relative to the start
// of the parent tag's character content. Returns the child tag.
ezxml_t ezxml_add_child(ezxml_t xml, const char *name, size_t off)
{
    ezxml_t child;

    if (! xml) return NULL;
    child = (ezxml_t)memset(malloc(sizeof(struct ezxml)), '\0',
                            sizeof(struct ezxml));
    child->name = (char *)name;
    child->attr = EZXML_NIL;
    child->txt = "";

    return ezxml_insert(child, xml, off);
}

// sets the character content for the given tag and returns the tag
ezxml_t ezxml_set_txt(ezxml_t xml, const char *txt)
{
    if (! xml) return NULL;
    if (xml->flags & EZXML_TXTM) free(xml->txt); // existing txt was malloced
    xml->flags &= ~EZXML_TXTM;
    xml->txt = (char *)txt;
    return xml;
}

// Sets the given tag attribute or adds a new attribute if not found. A value
// of NULL will remove the specified attribute. Returns the tag given.
ezxml_t ezxml_set_attr(ezxml_t xml, const char *name, const char *value)
{
    int l = 0, c;

    if (! xml) return NULL;
    while (xml->attr[l] && strcmp(xml->attr[l], name)) l += 2;
    if (! xml->attr[l]) { // not found, add as new attribute
        if (! value) return xml; // nothing to do
        if (xml->attr == EZXML_NIL) { // first attribute
            xml->attr = malloc(4 * sizeof(char *));
            xml->attr[1] = strdup(""); // empty list of malloced names/vals
        }
        else xml->attr = realloc(xml->attr, (l + 4) * sizeof(char *));

        xml->attr[l] = (char *)name; // set attribute name
        xml->attr[l + 2] = NULL; // null terminate attribute list
        xml->attr[l + 3] = realloc(xml->attr[l + 1],
                                   (c = strlen(xml->attr[l + 1])) + 2);
        strcpy(xml->attr[l + 3] + c, " "); // set name/value as not malloced
        if (xml->flags & EZXML_DUP) xml->attr[l + 3][c] = EZXML_NAMEM;
    }
    else if (xml->flags & EZXML_DUP) free((char *)name); // name was strduped

    for (c = l; xml->attr[c]; c += 2); // find end of attribute list
    if (xml->attr[c + 1][l / 2] & EZXML_TXTM) free(xml->attr[l + 1]); //old val
    if (xml->flags & EZXML_DUP) xml->attr[c + 1][l / 2] |= EZXML_TXTM;
    else xml->attr[c + 1][l / 2] &= ~EZXML_TXTM;

    if (value) xml->attr[l + 1] = (char *)value; // set attribute value
    else { // remove attribute
        if (xml->attr[c + 1][l / 2] & EZXML_NAMEM) free(xml->attr[l]);
        memmove(xml->attr + l, xml->attr + l + 2, (c - l + 2) * sizeof(char*));
        xml->attr = realloc(xml->attr, (c + 2) * sizeof(char *));
        memmove(xml->attr[c + 1] + (l / 2), xml->attr[c + 1] + (l / 2) + 1,
                (c / 2) - (l / 2)); // fix list of which name/vals are malloced
    }
    xml->flags &= ~EZXML_DUP; // clear strdup() flag
    return xml;
}

// sets a flag for the given tag and returns the tag
ezxml_t ezxml_set_flag(ezxml_t xml, short flag)
{
    if (xml) xml->flags |= flag;
    return xml;
}

// removes a tag along with its subtags without freeing its memory
ezxml_t ezxml_cut(ezxml_t xml)
{
    ezxml_t cur;

    if (! xml) return NULL; // nothing to do
    if (xml->next) xml->next->sibling = xml->sibling; // patch sibling list

    if (xml->parent) { // not root tag
        cur = xml->parent->child; // find head of subtag list
        if (cur == xml) xml->parent->child = xml->ordered; // first subtag
        else { // not first subtag
            while (cur->ordered != xml) cur = cur->ordered;
            cur->ordered = cur->ordered->ordered; // patch ordered list

            cur = xml->parent->child; // go back to head of subtag list
            if (strcmp(cur->name, xml->name)) { // not in first sibling list
                while (strcmp(cur->sibling->name, xml->name))
                    cur = cur->sibling;
                if (cur->sibling == xml) { // first of a sibling list
                    cur->sibling = (xml->next) ? xml->next
                                               : cur->sibling->sibling;
                }
                else cur = cur->sibling; // not first of a sibling list
            }

            while (cur->next && cur->next != xml) cur = cur->next;
            if (cur->next) cur->next = cur->next->next; // patch next list
        }        
    }
    xml->ordered = xml->sibling = xml->next = NULL;
    return xml;
}

#ifdef EZXML_TEST // test harness
int main(int argc, char **argv)
{
    ezxml_t xml;
    char *s;
    int i;

    if (argc != 2) return fprintf(stderr, "usage: %s xmlfile\n", argv[0]);

    xml = ezxml_parse_file(argv[1]);
    printf("%s\n", (s = ezxml_toxml(xml)));
    free(s);
    i = fprintf(stderr, "%s", ezxml_error(xml));
    ezxml_free(xml);
    return (i) ? 1 : 0;
}
#endif // EZXML_TEST
