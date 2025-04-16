#ifndef _WIMLIB_PATHLIST_H
#define _WIMLIB_PATHLIST_H

#include "wimlib/types.h"

int
read_path_list_file(const tchar *listfile,
		    tchar ***paths_ret, size_t *num_paths_ret,
		    void **mem_ret);

#endif /* _WIMLIB_PATHLIST_H */
