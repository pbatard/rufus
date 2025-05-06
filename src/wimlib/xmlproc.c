/*
 * xmlproc.c
 *
 * A simple XML 1.0 processor.  This handles all XML features that are used in
 * WIM files, plus a bit more for futureproofing.  It omits problematic
 * features, such as expansion of entities other than simple escape sequences.
 */

/*
 * Copyright 2023 Eric Biggers
 *
 * This file is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option) any
 * later version.
 *
 * This file is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this file; if not, see https://www.gnu.org/licenses/.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>

#include "wimlib/assert.h"
#include "wimlib/error.h"
#include "wimlib/test_support.h"
#include "wimlib/util.h"
#include "wimlib/xmlproc.h"

/*----------------------------------------------------------------------------*
 *                         XML node utility functions                         *
 *----------------------------------------------------------------------------*/

static tchar *
tstrdupz(const tchar *str, size_t len)
{
	tchar *new_str = CALLOC(len + 1, sizeof(str[0]));

	if (new_str)
		tmemcpy(new_str, str, len);
	return new_str;
}

static struct xml_node *
xml_new_node(struct xml_node *parent, enum xml_node_type type,
	     const tchar *name, size_t name_len,
	     const tchar *value, size_t value_len)
{
	struct xml_node *node = CALLOC(1, sizeof(*node));

	if (!node)
		return NULL;
	node->type = type;
	INIT_LIST_HEAD(&node->children);
	if (name) {
		node->name = tstrdupz(name, name_len);
		if (!node->name)
			goto oom;
	}
	if (value) {
		node->value = tstrdupz(value, value_len);
		if (!node->value)
			goto oom;
	}
	if (parent)
		xml_add_child(parent, node);
	return node;

oom:
	xml_free_node(node);
	return NULL;
}

/*
 * Create a new ELEMENT node, and if @parent is non-NULL add the new node under
 * @parent which should be another ELEMENT.
 */
struct xml_node *
xml_new_element(struct xml_node *parent, const tchar *name)
{
	return xml_new_node(parent, XML_ELEMENT_NODE, name, tstrlen(name),
			    NULL, 0);
}

/*
 * Create a new ELEMENT node with an attached TEXT node, and if @parent is
 * non-NULL add the new ELEMENT under @parent which should be another ELEMENT.
 */
struct xml_node *
xml_new_element_with_text(struct xml_node *parent, const tchar *name,
			  const tchar *text)
{
	struct xml_node *element = xml_new_element(parent, name);

	if (element && xml_element_set_text(element, text) != 0) {
		xml_free_node(element);
		return NULL;
	}
	return element;
}

/* Append @child to the children list of @parent. */
void
xml_add_child(struct xml_node *parent, struct xml_node *child)
{
	xml_unlink_node(child);	/* Shouldn't be needed, but be safe. */
	child->parent = parent;
	list_add_tail(&child->sibling_link, &parent->children);
}

/* Unlink @node from its parent, if it has one. */
void
xml_unlink_node(struct xml_node *node)
{
	if (node->parent) {
		list_del(&node->sibling_link);
		node->parent = NULL;
	}
}

static void
xml_free_children(struct xml_node *parent)
{
	struct xml_node *child, *tmp;

	list_for_each_entry_safe(child, tmp, &parent->children, sibling_link)
		xml_free_node(child);
}

/* Recursively free @node, first unlinking it if needed.  @node may be NULL. */
void
xml_free_node(struct xml_node *node)
{
	if (node) {
		xml_unlink_node(node);
		xml_free_children(node);
		FREE(node->name);
		FREE(node->value);
		FREE(node);
	}
}

/*
 * Return the text from the first TEXT child node of @element, or NULL if no
 * such node exists.  @element may be NULL.
 */
const tchar *
xml_element_get_text(const struct xml_node *element)
{
	const struct xml_node *child;

	xml_node_for_each_child(element, child)
		if (child->type == XML_TEXT_NODE)
			return child->value;
	return NULL;
}

/*
 * Set the contents of the given @element to the given @text, replacing the
 * entire existing contents if any.
 */
int
xml_element_set_text(struct xml_node *element, const tchar *text)
{
	struct xml_node *text_node = xml_new_node(NULL, XML_TEXT_NODE, NULL, 0,
						  text, tstrlen(text));
	if (!text_node)
		return WIMLIB_ERR_NOMEM;
	xml_free_children(element);
	xml_add_child(element, text_node);
	return 0;
}

static int
xml_element_append_text(struct xml_node *element,
			const tchar *text, size_t text_len)
{
	struct xml_node *last_child;

	if (!list_empty(&element->children) &&
	    (last_child =
	     list_last_entry(&element->children, struct xml_node,
			     sibling_link))->type == XML_TEXT_NODE) {
		/*
		 * The new TEXT would directly follow another TEXT, so simplify
		 * the tree by just appending to the existing TEXT.  (This case
		 * can theoretically be reached via the use of CDATA...)
		 */
		size_t old_len = tstrlen(last_child->value);
		tchar *new_value = CALLOC(old_len + text_len + 1,
					  sizeof(new_value[0]));
		if (!new_value)
			return WIMLIB_ERR_NOMEM;
		tmemcpy(new_value, last_child->value, old_len);
		tmemcpy(&new_value[old_len], text, text_len);
		FREE(last_child->value);
		last_child->value = new_value;
		return 0;
	}
	if (!xml_new_node(element, XML_TEXT_NODE, NULL, 0, text, text_len))
		return WIMLIB_ERR_NOMEM;
	return 0;
}

/* Find the attribute with the given @name on @element. */
struct xml_node *
xml_get_attrib(const struct xml_node *element, const tchar *name)
{
	struct xml_node *child;

	xml_node_for_each_child(element, child) {
		if (child->type == XML_ATTRIBUTE_NODE &&
		    !tstrcmp(child->name, name))
			return child;
	}
	return NULL;
}

/* Set the attribute @name=@value on the given @element. */
int
xml_set_attrib(struct xml_node *element, const tchar *name, const tchar *value)
{
	struct xml_node *attrib = xml_new_node(NULL, XML_ATTRIBUTE_NODE,
					       name, tstrlen(name),
					       value, tstrlen(value));
	if (!attrib)
		return WIMLIB_ERR_NOMEM;
	xml_replace_child(element, attrib);
	return 0;
}

/*
 * Add the ELEMENT or ATTRIBUTE node @replacement under the ELEMENT @parent,
 * replacing any node with the same type and name that already exists.
 */
void
xml_replace_child(struct xml_node *parent, struct xml_node *replacement)
{
	struct xml_node *child;

	xml_unlink_node(replacement); /* Shouldn't be needed, but be safe. */

	xml_node_for_each_child(parent, child) {
		if (child->type == replacement->type &&
		    !tstrcmp(child->name, replacement->name)) {
			list_replace(&child->sibling_link,
				     &replacement->sibling_link);
			replacement->parent = parent;
			child->parent = NULL;
			xml_free_node(child);
			return;
		}
	}
	xml_add_child(parent, replacement);
}

struct xml_node *
xml_clone_tree(struct xml_node *orig)
{
	struct xml_node *clone, *orig_child, *clone_child;

	clone = xml_new_node(NULL, orig->type,
			orig->name, orig->name ? tstrlen(orig->name) : 0,
			orig->value, orig->value ? tstrlen(orig->value) : 0);
	if (!clone)
		return NULL;
	xml_node_for_each_child(orig, orig_child) {
		clone_child = xml_clone_tree(orig_child);
		if (!clone_child)
			goto oom;
		xml_add_child(clone, clone_child);
	}
	return clone;

oom:
	xml_free_node(clone);
	return NULL;
}

/*----------------------------------------------------------------------------*
 *                           XML string validation                            *
 *----------------------------------------------------------------------------*/

/*
 * Functions that check for legal names and values in XML 1.0.  These are
 * currently slightly over-lenient, as they allow everything non-ASCII.  These
 * are also not currently used by the XML parser to reject non-well-formed
 * documents, but rather just by the user of the XML processor (xml.c) in order
 * to avoid introducing illegal names and values into the document.
 */

static inline bool
is_whitespace(tchar c)
{
	return c == ' ' || c == '\n' || c == '\r' || c == '\t';
}

static inline bool
is_name_start_char(tchar c)
{
	return (c & 0x7f) != c /* overly lenient for now */ ||
		(c >= 'A' && c <= 'Z') ||
		(c >= 'a' && c <= 'z') ||
		c == ':' || c == '_';
}

static inline bool
is_name_char(tchar c)
{
	return is_name_start_char(c) ||
		(c >= '0' && c <= '9') || c == '-' || c == '.';
}

/* Allow characters used in element "paths"; see do_xml_path_walk() */
static inline bool
is_path_char(tchar c)
{
	return c == '/' || c == '[' || c == ']';
}

bool
xml_legal_path(const tchar *p)
{
	if (!is_name_start_char(*p) && !is_path_char(*p))
		return false;
	for (p = p + 1; *p; p++) {
		if (!is_name_char(*p) && !is_path_char(*p))
			return false;
	}
	return true;
}

bool
xml_legal_value(const tchar *p)
{
	for (; *p; p++) {
		/* Careful: tchar can be signed. */
		if (*p > 0 && *p < 0x20 && !is_whitespace(*p))
			return false;
	}
	return true;
}

#if TCHAR_IS_UTF16LE
#define BYTE_ORDER_MARK	(tchar[]){ 0xfeff, 0 }
#else
#define BYTE_ORDER_MARK	"\xEF\xBB\xBF"
#endif

/*----------------------------------------------------------------------------*
 *                               XML parsing                                  *
 *----------------------------------------------------------------------------*/

#define CHECK(cond)	if (!(cond)) goto bad

static inline void
skip_whitespace(const tchar **pp)
{
	const tchar *p = *pp;

	while (is_whitespace(*p))
		p++;
	*pp = p;
}

static inline bool
skip_string(const tchar **pp, const tchar *str)
{
	const tchar *p = *pp;
	size_t len = tstrlen(str);

	if (tstrncmp(p, str, len))
		return false;
	*pp = p + len;
	return true;
}

static inline bool
find_and_skip(const tchar **pp, const tchar *str)
{
	const tchar *p = *pp;

	p = tstrstr(p, str);
	if (!p)
		return false;
	*pp = p + tstrlen(str);
	return true;
}

static bool
skip_misc(const tchar **pp)
{
	const tchar *p = *pp, *prev_p;

	do {
		prev_p = p;
		skip_whitespace(&p);
		/* Discard XML declaration and top-level PIs for now. */
		if (skip_string(&p, T("<?")) && !find_and_skip(&p, T("?>")))
			return false;
		/* Discard DOCTYPE declaration for now. */
		if (skip_string(&p, T("<!DOCTYPE")) && !find_and_skip(&p, T(">")))
			return false;
		/* Discard top-level comments for now. */
		if (skip_string(&p, T("<!--")) && !find_and_skip(&p, T("-->")))
			return false;
	} while (p != prev_p);
	*pp = p;
	return true;
}

static inline const tchar *
get_escape_seq(tchar c)
{
	switch (c) {
	case '<':
		return T("&lt;");
	case '>':
		return T("&gt;");
	case '&':
		return T("&amp;");
	case '\'':
		return T("&apos;");
	case '"':
		return T("&quot;");
	}
	return NULL;
}

/* Note: 'str' must be NUL-terminated, but only 'len' chars are used. */
static int
unescape_string(const tchar *str, size_t len, tchar **unescaped_ret)
{
	const tchar *in_p = str;
	tchar *unescaped, *out_p;

	unescaped = CALLOC(len + 1, sizeof(str[0]));
	if (!unescaped)
		return WIMLIB_ERR_NOMEM;
	out_p = unescaped;
	while (in_p < &str[len]) {
		if (*in_p != '&')
			*out_p++ = *in_p++;
		else if (skip_string(&in_p, T("&lt;")))
			*out_p++ = '<';
		else if (skip_string(&in_p, T("&gt;")))
			*out_p++ = '>';
		else if (skip_string(&in_p, T("&amp;")))
			*out_p++ = '&';
		else if (skip_string(&in_p, T("&apos;")))
			*out_p++ = '\'';
		else if (skip_string(&in_p, T("&quot;")))
			*out_p++ = '"';
		else
			goto bad;
	}
	if (in_p > &str[len])
		goto bad;
	*unescaped_ret = unescaped;
	return 0;

bad:
	ERROR("Error unescaping string '%.*"TS"'", (int)len, str);
	FREE(unescaped);
	return WIMLIB_ERR_XML;
}

static int
parse_element(const tchar **pp, struct xml_node *parent, int depth,
	      struct xml_node **node_ret);

static int
parse_contents(const tchar **pp, struct xml_node *element, int depth)
{
	const tchar *p = *pp;
	int ret;

	for (;;) {
		const tchar *raw_text = p;
		tchar *text;

		for (; *p != '<'; p++) {
			if (*p == '\0')
				return WIMLIB_ERR_XML;
		}
		if (p > raw_text) {
			ret = unescape_string(raw_text, p - raw_text, &text);
			if (ret)
				return ret;
			ret = xml_element_append_text(element, text,
						      tstrlen(text));
			FREE(text);
			if (ret)
				return ret;
		}
		if (p[1] == '/') {
			break; /* Reached the end tag of @element */
		} else if (p[1] == '?') {
			/* Discard processing instructions for now. */
			p += 2;
			if (!find_and_skip(&p, T("?>")))
				return WIMLIB_ERR_XML;
			continue;
		} else if (p[1] == '!') {
			if (skip_string(&p, T("<![CDATA["))) {
				raw_text = p;
				if (!find_and_skip(&p, T("]]>")))
					return WIMLIB_ERR_XML;
				ret = xml_element_append_text(element, raw_text,
							      p - 3 - raw_text);
				if (ret)
					return ret;
				continue;
			} else if (skip_string(&p, T("<!--"))) {
				/* Discard comments for now. */
				if (!find_and_skip(&p, T("-->")))
					return WIMLIB_ERR_XML;
				continue;
			}
			return WIMLIB_ERR_XML;
		}
		ret = parse_element(&p, element, depth + 1, NULL);
		if (ret)
			return ret;
	}
	*pp = p;
	return 0;
}

static int
parse_element(const tchar **pp, struct xml_node *parent, int depth,
	      struct xml_node **element_ret)
{
	const tchar *p = *pp;
	struct xml_node *element = NULL;
	const tchar *name_start;
	size_t name_len;
	int ret;

	/* Parse the start tag. */
	CHECK(depth < 50);
	CHECK(*p == '<');
	p++;
	name_start = p;
	while (!is_whitespace(*p) && *p != '>' && *p != '/' && *p != '\0')
		p++;
	name_len = p - name_start;
	CHECK(name_len > 0);
	element = xml_new_node(parent, XML_ELEMENT_NODE, name_start, name_len,
			       NULL, 0);
	if (!element) {
		ret = WIMLIB_ERR_NOMEM;
		goto error;
	}
	/* Parse the attributes list within the start tag. */
	while (is_whitespace(*p)) {
		const tchar *attr_name_start, *attr_value_start;
		size_t attr_name_len, attr_value_len;
		tchar *attr_value;
		tchar quote;

		skip_whitespace(&p);
		if (*p == '/' || *p == '>')
			break;
		attr_name_start = p;
		while (*p != '=' && !is_whitespace(*p) && *p != '\0')
			p++;
		attr_name_len = p - attr_name_start;
		skip_whitespace(&p);
		CHECK(attr_name_len > 0 && *p == '=');
		p++;
		skip_whitespace(&p);
		quote = *p;
		CHECK(quote == '\'' || quote == '"');
		attr_value_start = ++p;
		while (*p != quote && *p != '\0')
			p++;
		CHECK(*p == quote);
		attr_value_len = p - attr_value_start;
		p++;
		ret = unescape_string(attr_value_start, attr_value_len,
				      &attr_value);
		if (ret)
			goto error;
		ret = xml_new_node(element, XML_ATTRIBUTE_NODE,
				   attr_name_start, attr_name_len,
				   attr_value, tstrlen(attr_value))
			? 0 : WIMLIB_ERR_NOMEM;
		FREE(attr_value);
		if (ret)
			goto error;
	}
	if (*p == '/') {
		/* Closing an empty element tag */
		p++;
	} else {
		/* Closing the start tag */
		CHECK(*p == '>');
		p++;
		/* Parse the contents, then the end tag. */
		ret = parse_contents(&p, element, depth);
		if (ret)
			goto error;
		CHECK(*p == '<');
		p++;
		CHECK(*p == '/');
		p++;
		CHECK(!tstrncmp(p, name_start, name_len));
		p += name_len;
		skip_whitespace(&p);
	}
	CHECK(*p == '>');
	p++;
	*pp = p;
	if (element_ret)
		*element_ret = element;
	return 0;

error:
	xml_free_node(element);
	return ret;

bad:
	ret = WIMLIB_ERR_XML;
	goto error;
}

/*
 * Deserialize an XML document and return its root node in @doc_ret.  The
 * document must be given as a NUL-terminated string of 'tchar', i.e. UTF-16LE
 * in Windows builds and UTF-8 everywhere else.
 */
int
xml_parse_document(const tchar *p, struct xml_node **doc_ret)
{
	int ret;
	struct xml_node *doc;

	// Keep static analysers happy since we don't care about returned value.
	(void)skip_string(&p, BYTE_ORDER_MARK);
	if (!skip_misc(&p))
		return WIMLIB_ERR_XML;
	ret = parse_element(&p, NULL, 0, &doc);
	if (ret)
		return ret;
	if (!skip_misc(&p) || *p) {
		xml_free_node(doc);
		return WIMLIB_ERR_XML;
	}
	*doc_ret = doc;
	return 0;
}

/*----------------------------------------------------------------------------*
 *                               XML writing                                  *
 *----------------------------------------------------------------------------*/

static void
xml_write(struct xml_out_buf *buf, const tchar *str, size_t len)
{
	if (buf->count + len + 1 > buf->capacity) {
		size_t new_capacity = max3(buf->count + len + 1,
					   buf->capacity * 2, 4096);
		tchar *new_buf = REALLOC(buf->buf,
					 new_capacity * sizeof(str[0]));
		if (!new_buf) {
			buf->oom = true;
			return;
		}
		buf->buf = new_buf;
		buf->capacity = new_capacity;
	}
	tmemcpy(&buf->buf[buf->count], str, len);
	buf->count += len;
}

static void
xml_puts(struct xml_out_buf *buf, const tchar *str)
{
	xml_write(buf, str, tstrlen(str));
}

static void
xml_escape_and_puts(struct xml_out_buf *buf, const tchar *str)
{
	const tchar *p = str, *saved, *seq = NULL;

	for (;; p++) {
		for (saved = p; *p && (seq = get_escape_seq(*p)) == NULL; p++)
			;
		xml_write(buf, saved, p - saved);
		if (!*p)
			return;
		xml_puts(buf, seq);
	}
}

static void
xml_write_element(struct xml_node *element, struct xml_out_buf *buf)
{
	struct xml_node *child;
	wimlib_assert(element != NULL);
	if (element == NULL)
		return;

	/* Write the start tag. */
	xml_puts(buf, T("<"));
	xml_puts(buf, element->name);
	xml_node_for_each_child(element, child) {
		if (child->type == XML_ATTRIBUTE_NODE) {
			xml_puts(buf, T(" "));
			xml_puts(buf, child->name);
			xml_puts(buf, T("=\""));
			xml_escape_and_puts(buf, child->value);
			xml_puts(buf, T("\""));
		}
	}
	xml_puts(buf, T(">"));

	/* Write the contents. */
	xml_node_for_each_child(element, child) {
		if (child->type == XML_TEXT_NODE)
			xml_escape_and_puts(buf, child->value);
		else if (child->type == XML_ELEMENT_NODE)
			xml_write_element(child, buf);
	}

	/* Write the end tag. */
	xml_puts(buf, T("</"));
	xml_puts(buf, element->name);
	xml_puts(buf, T(">"));
}

/*
 * Serialize the document @doc into @buf as a NUL-terminated string of 'tchar',
 * i.e. UTF-16LE in Windows builds and UTF-8 everywhere else.  A byte order mark
 * (BOM) is included, as this is needed for compatibility with WIMGAPI.
 */
int
xml_write_document(struct xml_node *doc, struct xml_out_buf *buf)
{
	xml_puts(buf, BYTE_ORDER_MARK);
	xml_write_element(doc, buf);
	if (buf->oom)
		return WIMLIB_ERR_NOMEM;
	buf->buf[buf->count] = '\0';
	return 0;
}

/*----------------------------------------------------------------------------*
 *                              Test support                                  *
 *----------------------------------------------------------------------------*/

#ifdef ENABLE_TEST_SUPPORT
WIMLIBAPI int
wimlib_parse_and_write_xml_doc(const tchar *in, tchar **out_ret)
{
	struct xml_node *doc;
	struct xml_out_buf buf = {};
	int ret;

	ret = xml_parse_document(in, &doc);
	if (ret)
		return ret;
	ret = xml_write_document(doc, &buf);
	xml_free_node(doc);
	*out_ret = buf.buf;
	return ret;
}
#endif /* ENABLE_TEST_SUPPORT */
