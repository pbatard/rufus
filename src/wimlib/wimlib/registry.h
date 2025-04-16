#ifndef _WIMLIB_REGISTRY_H
#define _WIMLIB_REGISTRY_H

#include "wimlib/types.h"

struct regf;

enum hive_status {
	HIVE_OK,
	HIVE_CORRUPT,
	HIVE_UNSUPPORTED,
	HIVE_KEY_NOT_FOUND,
	HIVE_VALUE_NOT_FOUND,
	HIVE_VALUE_IS_WRONG_TYPE,
	HIVE_OUT_OF_MEMORY,
	HIVE_ITERATION_STOPPED,
};

enum hive_status
hive_validate(const void *hive_mem, size_t hive_size);

enum hive_status
hive_get_string(const struct regf *regf, const tchar *key_name,
		const tchar *value_name, tchar **value_ret);

enum hive_status
hive_get_number(const struct regf *regf, const tchar *key_name,
		const tchar *value_name, s64 *value_ret);

enum hive_status
hive_list_subkeys(const struct regf *regf, const tchar *key_name,
		  tchar ***subkeys_ret);

void
hive_free_subkeys_list(tchar **subkeys);

const char *
hive_status_to_string(enum hive_status status);

#endif /* _WIMLIB_REGISTRY_H */
