/*
  Copyright (C) 2005, 2008, 2011, 2016 Rocky Bernstein <rocky@gnu.org>
  Copyright (C) 2000 Herbert Valerio Riedel <hvr@gnu.org>

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

#ifdef HAVE_CONFIG_H
# include "config.h"
# define __CDIO_CONFIG_H__ 1
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <cdio/ds.h>
#include <cdio/util.h>
#include <cdio/types.h>
#include "cdio_assert.h"

struct _CdioList
{
  unsigned length;

  CdioListNode_t *begin;
  CdioListNode_t *end;
};

struct _CdioListNode
{
  CdioList_t *list;

  CdioListNode_t *next;

  void *data;
};

/* impl */

CdioList_t *
_cdio_list_new (void)
{
  CdioList_t *p_new_obj = calloc (1, sizeof (CdioList_t));

  return p_new_obj;
}

void
_cdio_list_free (CdioList_t *p_list, int free_data, CdioDataFree_t free_fn)
{
  while (_cdio_list_length (p_list))
    _cdio_list_node_free (_cdio_list_begin (p_list), free_data, free_fn);

  free (p_list);
}

unsigned
_cdio_list_length (const CdioList_t *p_list)
{
  cdio_assert (p_list != NULL);

  return p_list->length;
}

void
_cdio_list_prepend (CdioList_t *p_list, void *p_data)
{
  CdioListNode_t *p_new_node;

  cdio_assert (p_list != NULL);

  p_new_node = calloc (1, sizeof (CdioListNode_t));
  cdio_assert (p_new_node != NULL);

  p_new_node->list = p_list;
  p_new_node->next = p_list->begin;
  p_new_node->data = p_data;

  p_list->begin = p_new_node;
  if (p_list->length == 0)
    p_list->end = p_new_node;

  p_list->length++;
}

void
_cdio_list_append (CdioList_t *p_list, void *p_data)
{
  cdio_assert (p_list != NULL);

  if (p_list->length == 0)
    {
      _cdio_list_prepend (p_list, p_data);
    }
  else
    {
      CdioListNode_t *p_new_node = calloc (1, sizeof (CdioListNode_t));
      cdio_assert (p_new_node != NULL);

      p_new_node->list = p_list;
      p_new_node->next = NULL;
      p_new_node->data = p_data;

      p_list->end->next = p_new_node;
      p_list->end = p_new_node;

      p_list->length++;
    }
}

void
_cdio_list_foreach (CdioList_t *p_list, _cdio_list_iterfunc_t func,
                    void *p_user_data)
{
  CdioListNode_t *node;

  cdio_assert (p_list != NULL);
  cdio_assert (func != 0);

  for (node = _cdio_list_begin (p_list);
       node != NULL;
       node = _cdio_list_node_next (node))
    func (_cdio_list_node_data (node), p_user_data);
}

CdioListNode_t *
_cdio_list_find (CdioList_t *p_list, _cdio_list_iterfunc_t cmp_func,
                 void *p_user_data)
{
  CdioListNode_t *p_node;

  cdio_assert (p_list != NULL);
  cdio_assert (cmp_func != 0);

  for (p_node = _cdio_list_begin (p_list);
       p_node != NULL;
       p_node = _cdio_list_node_next (p_node))
    if (cmp_func (_cdio_list_node_data (p_node), p_user_data))
      break;

  return p_node;
}

CdioListNode_t *
_cdio_list_begin (const CdioList_t *p_list)
{
  cdio_assert (p_list != NULL);

  return p_list->begin;
}

CdioListNode_t *
_cdio_list_end (CdioList_t *p_list)
{
  cdio_assert (p_list != NULL);

  return p_list->end;
}

CdioListNode_t *
_cdio_list_node_next (CdioListNode_t *p_node)
{
  if (p_node)
    return p_node->next;

  return NULL;
}

void
_cdio_list_node_free (CdioListNode_t *p_node,
                      int free_data, CdioDataFree_t free_fn)
{
  CdioList_t *p_list;
  CdioListNode_t *prev_node;

  cdio_assert (p_node != NULL);

  p_list = p_node->list;

  cdio_assert (_cdio_list_length (p_list) > 0);

  if (free_data && free_fn)
    free_fn (_cdio_list_node_data (p_node));

  if (_cdio_list_length (p_list) == 1)
    {
      cdio_assert (p_list->begin == p_list->end);

      p_list->end = p_list->begin = NULL;
      p_list->length = 0;
      free (p_node);
      return;
    }

  cdio_assert (p_list->begin != p_list->end);

  if (p_list->begin == p_node)
    {
      p_list->begin = p_node->next;
      free (p_node);
      p_list->length--;
      return;
    }

  for (prev_node = p_list->begin; prev_node->next; prev_node = prev_node->next)
    if (prev_node->next == p_node)
      break;

  cdio_assert (prev_node->next != NULL);

  if (p_list->end == p_node)
    p_list->end = prev_node;

  prev_node->next = p_node->next;

  p_list->length--;

  free (p_node);
}

void *
_cdio_list_node_data (CdioListNode_t *p_node)
{
  if (p_node)
    return p_node->data;

  return NULL;
}

/* eof */


/*
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
