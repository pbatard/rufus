#ifndef _WIMLIB_PROGRESS_H
#define _WIMLIB_PROGRESS_H

#include "wimlib.h"
#include "wimlib/paths.h"
#include "wimlib/types.h"

/* If specified, call the user-provided progress function and check its result.
 */
static inline int
call_progress(wimlib_progress_func_t progfunc,
	      enum wimlib_progress_msg msg,
	      union wimlib_progress_info *info,
	      void *progctx)
{
	if (progfunc) {
		enum wimlib_progress_status status;

		status = (*progfunc)(msg, info, progctx);

		switch (status) {
		case WIMLIB_PROGRESS_STATUS_CONTINUE:
			return 0;
		case WIMLIB_PROGRESS_STATUS_ABORT:
			return WIMLIB_ERR_ABORTED_BY_PROGRESS;
		default:
			return WIMLIB_ERR_UNKNOWN_PROGRESS_STATUS;
		}
	}
	return 0;
}

int
report_error(wimlib_progress_func_t progfunc,
	     void *progctx, int error_code, const tchar *path);

/* Rate-limiting of byte-count based progress messages: update *next_progress_p
 * to the value that completed_bytes needs to reach before the next progress
 * message will be sent.  */
static inline void
set_next_progress(u64 completed_bytes, u64 total_bytes, u64 *next_progress_p)
{
	if (*next_progress_p < total_bytes) {
		/*
		 * Send the next message as soon as:
		 *	- another 1/1000 of the total has been processed;
		 *	- OR another 256 MiB has been processed;
		 *	- OR all bytes have been processed.
		 */
		*next_progress_p = min(min(completed_bytes + total_bytes / 1000,
					   completed_bytes + (1UL << 28)),
				       total_bytes);
	} else {
		/* Last message has been sent.  */
		*next_progress_p = ~0;
	}
}

/* Windows: temporarily remove the stream name from the path  */
static inline tchar *
progress_get_streamless_path(const tchar *path)
{
	tchar *cookie = NULL;
#ifdef _WIN32
	cookie = (wchar_t *)path_stream_name(path);
	if (cookie)
		*--cookie = L'\0'; /* Overwrite the colon  */
#endif
	return cookie;
}

/* Windows: temporarily replace \??\ with \\?\ (to make an NT namespace path
 * into a Win32 namespace path)  */
static inline tchar *
progress_get_win32_path(const tchar *path)
{
#ifdef _WIN32
	if (path != NULL && !wcsncmp(path, L"\\??\\", 4)) {
		((wchar_t *)path)[1] = L'\\';
		return (wchar_t *)&path[1];
	}
#endif
	return NULL;
}

/* Windows: restore the NT namespace path  */
static inline void
progress_put_win32_path(tchar *cookie)
{
#ifdef _WIN32
	if (cookie)
		*cookie = L'?';
#endif
}

/* Windows: restore the stream name part of the path  */
static inline void
progress_put_streamless_path(tchar *cookie)
{
#ifdef _WIN32
	if (cookie)
		*cookie = L':';
#endif
}

#endif /* _WIMLIB_PROGRESS_H */
