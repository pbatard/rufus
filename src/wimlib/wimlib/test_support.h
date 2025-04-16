#ifndef _WIMLIB_TEST_SUPPORT_H
#define _WIMLIB_TEST_SUPPORT_H

#ifdef ENABLE_TEST_SUPPORT

#include "wimlib.h"
#include "wimlib/types.h"

#define WIMLIB_ERR_IMAGES_ARE_DIFFERENT			200

#define WIMLIB_ADD_FLAG_GENERATE_TEST_DATA		0x08000000

#define WIMLIB_CMP_FLAG_UNIX_MODE	0x00000001
#define WIMLIB_CMP_FLAG_NTFS_3G_MODE	0x00000002
#define WIMLIB_CMP_FLAG_WINDOWS_MODE	0x00000004
#define WIMLIB_CMP_FLAG_EXT4		0x00000008

WIMLIBAPI void
wimlib_seed_random(u64 seed);

WIMLIBAPI int
wimlib_compare_images(WIMStruct *wim1, int image1,
		      WIMStruct *wim2, int image2, int cmp_flags);

WIMLIBAPI int
wimlib_parse_and_write_xml_doc(const tchar *in, tchar **out_ret);

WIMLIBAPI int
wimlib_utf8_to_utf16le(const char *in, size_t in_nbytes,
		       utf16lechar **out_ret, size_t *out_nbytes_ret);

WIMLIBAPI int
wimlib_utf16le_to_utf8(const utf16lechar *in, size_t in_nbytes,
		       char **out_ret, size_t *out_nbytes_ret);

#endif /* ENABLE_TEST_SUPPORT */

#endif /* _WIMLIB_TEST_SUPPORT_H */
