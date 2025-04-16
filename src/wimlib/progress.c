/*
 * progress.c
 */

/*
 * Copyright (C) 2014 Eric Biggers
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

#include "wimlib/progress.h"

int
report_error(wimlib_progress_func_t progfunc,
	     void *progctx, int error_code, const tchar *path)
{
	int ret;
	union wimlib_progress_info progress;
	tchar *cookie;

	if (error_code == WIMLIB_ERR_SUCCESS ||
	    error_code == WIMLIB_ERR_ABORTED_BY_PROGRESS ||
	    error_code == WIMLIB_ERR_UNKNOWN_PROGRESS_STATUS)
		return error_code;

	progress.handle_error.path = path;
	progress.handle_error.error_code = error_code;
	progress.handle_error.will_ignore = false;

	cookie = progress_get_win32_path(path);

	ret = call_progress(progfunc, WIMLIB_PROGRESS_MSG_HANDLE_ERROR,
			    &progress, progctx);

	progress_put_win32_path(cookie);

	if (ret)
		return ret;

	if (!progress.handle_error.will_ignore)
		return error_code;

	return 0;
}
