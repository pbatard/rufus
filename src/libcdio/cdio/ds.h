/*
    Copyright (C) 2005, 2008, 2017 Rocky Bernstein <rocky@gnu.org>
    Copyright (C) 2000, 2004 Herbert Valerio Riedel <hvr@gnu.org>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/** \file ds.h
 *  \brief  The top-level header for list-related data structures.

    Note: this header will is slated to get removed and libcdio will use
    glib.h routines instead.
*/


#ifndef CDIO_DS_H_
#define CDIO_DS_H_

#include <cdio/types.h>

/** opaque types... */
typedef struct _CdioList CdioList_t;
typedef struct _CdioListNode CdioListNode_t;

typedef int (*_cdio_list_cmp_func_t) (void *p_data1, void *p_data2);
typedef int (*_cdio_list_iterfunc_t) (void *p_data, void *p_user_data);

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** methods */
CdioList_t *_cdio_list_new (void);

void _cdio_list_free (CdioList_t *p_list, int free_data, CdioDataFree_t free_fn);

unsigned _cdio_list_length (const CdioList_t *list);

void _cdio_list_prepend (CdioList_t *p_list, void *p_data);

void _cdio_list_append (CdioList_t *p_list, void *p_data);

void _cdio_list_foreach (CdioList_t *p_list, _cdio_list_iterfunc_t func,
                         void *p_user_data);

CdioListNode_t *_cdio_list_find (CdioList_t *p_list,
                                 _cdio_list_iterfunc_t cmp_func,
                                 void *p_user_data);

#define _CDIO_LIST_FOREACH(node, list) \
 for (node = _cdio_list_begin (list); node; node = _cdio_list_node_next (node))

/** node operations */

CdioListNode_t *_cdio_list_begin (const CdioList_t *p_list);

CdioListNode_t *_cdio_list_end (CdioList_t *p_list);

CdioListNode_t *_cdio_list_node_next (CdioListNode_t *p_node);

  void _cdio_list_node_free (CdioListNode_t *p_node, int i_free_data,
                             CdioDataFree_t free_fn);

void *_cdio_list_node_data (CdioListNode_t *p_node);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CDIO_DS_H_ */

/*
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
