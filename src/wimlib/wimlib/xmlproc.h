#ifndef _WIMLIB_XMLPROC_H
#define _WIMLIB_XMLPROC_H

#include "wimlib/list.h"
#include "wimlib/types.h"

/*****************************************************************************/

enum xml_node_type {
	XML_ELEMENT_NODE,
	XML_TEXT_NODE,
	XML_ATTRIBUTE_NODE,
};

struct xml_node {
	enum xml_node_type type;	/* type of node */
	tchar *name;			/* name of ELEMENT or ATTRIBUTE */
	tchar *value;			/* value of TEXT or ATTRIBUTE */
	struct xml_node *parent;	/* parent, or NULL if none */
	struct list_head children;	/* children; only used for ELEMENT */
	struct list_head sibling_link;
};

/* Iterate through the children of an xml_node.  Does nothing if passed NULL. */
#define xml_node_for_each_child(parent, child) \
	if (parent) list_for_each_entry(child, &(parent)->children, sibling_link)

static inline bool
xml_node_is_element(const struct xml_node *node, const tchar *name)
{
	return node->type == XML_ELEMENT_NODE && !tstrcmp(node->name, name);
}

struct xml_node *
xml_new_element(struct xml_node *parent, const tchar *name);

struct xml_node *
xml_new_element_with_text(struct xml_node *parent, const tchar *name,
			  const tchar *text);

void
xml_add_child(struct xml_node *parent, struct xml_node *child);

void
xml_unlink_node(struct xml_node *node);

void
xml_free_node(struct xml_node *node);

const tchar *
xml_element_get_text(const struct xml_node *element);

int
xml_element_set_text(struct xml_node *element, const tchar *text);

struct xml_node *
xml_get_attrib(const struct xml_node *element, const tchar *name);

int
xml_set_attrib(struct xml_node *element, const tchar *name, const tchar *value);

void
xml_replace_child(struct xml_node *parent, struct xml_node *replacement);

struct xml_node *
xml_clone_tree(struct xml_node *orig);

bool
xml_legal_path(const tchar *name);

bool
xml_legal_value(const tchar *value);

/*****************************************************************************/

int
xml_parse_document(const tchar *raw_doc, struct xml_node **doc_ret);

/*****************************************************************************/

struct xml_out_buf {
	tchar *buf;
	size_t count;
	size_t capacity;
	bool oom;
};

int
xml_write_document(struct xml_node *doc, struct xml_out_buf *buf);

#endif /* _WIMLIB_XMLPROC_H */
