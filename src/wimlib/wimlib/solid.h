#ifndef _WIMLIB_SOLID_H
#define _WIMLIB_SOLID_H

struct list_head;

int
sort_blob_list_for_solid_compression(struct list_head *blob_list);

#endif /* _WIMLIB_SOLID_H */
