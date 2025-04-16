#ifndef _WIMLIB_INTEGRITY_H
#define _WIMLIB_INTEGRITY_H

#include <sys/types.h>

#include "wimlib/types.h"

#define WIM_INTEGRITY_OK 0
#define WIM_INTEGRITY_NOT_OK -1
#define WIM_INTEGRITY_NONEXISTENT -2

struct integrity_table;

int
read_integrity_table(WIMStruct *wim, u64 num_checked_bytes,
		     struct integrity_table **table_ret);

#define free_integrity_table(table) FREE(table)

int
write_integrity_table(WIMStruct *wim,
		      off_t new_blob_table_end,
		      off_t old_blob_table_end,
		      struct integrity_table *old_table);

int
check_wim_integrity(WIMStruct *wim);

#endif /* _WIMLIB_INTEGRITY_H */
