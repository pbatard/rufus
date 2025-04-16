#ifndef _WIMLIB_CASE_H
#define _WIMLIB_CASE_H

#include <stdbool.h>

/* Note: the NTFS-3G headers define CASE_SENSITIVE, hence the WIMLIB prefix.  */
typedef enum {
	/* Use either case-sensitive or case-insensitive search, depending on
	 * the variable @default_ignore_case.  */
	WIMLIB_CASE_PLATFORM_DEFAULT = 0,

	/* Use case-sensitive search.  */
	WIMLIB_CASE_SENSITIVE = 1,

	/* Use case-insensitive search.  */
	WIMLIB_CASE_INSENSITIVE = 2,
} CASE_SENSITIVITY_TYPE;

extern bool default_ignore_case;

static inline bool
will_ignore_case(CASE_SENSITIVITY_TYPE case_type)
{
	if (case_type == WIMLIB_CASE_SENSITIVE)
		return false;
	if (case_type == WIMLIB_CASE_INSENSITIVE)
		return true;

	return default_ignore_case;
}

#endif /* _WIMLIB_CASE_H  */
