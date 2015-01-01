/*
 * unxz implementation for busybox
 *
 * Copyright © 2014-2015 Pete Batard <pete@akeo.ie>
 * Based on xz-embedded © Lasse Collin <lasse.collin@tukaani.org> - Public Domain
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */

#include "libbb.h"
#include "bb_archive.h"

#define XZ_EXTERN static
// We get XZ_OPTIONS_ERROR in xz_dec_stream if this is not defined
#define XZ_DEC_ANY_CHECK

#include "xz_dec_bcj.c"
#include "xz_dec_lzma2.c"
#include "xz_dec_stream.c"

static void XZ_FUNC xz_crc32_init(void)
{
	if (!global_crc32_table)
		global_crc32_table = crc32_filltable(NULL, 0);
}

static uint32_t XZ_FUNC xz_crc32(const uint8_t *buf, size_t size, uint32_t crc)
{
	// The XZ CRC32 is INVERTED!
	return ~crc32_block_endian0(~crc, buf, size, global_crc32_table);
}

IF_DESKTOP(long long) int FAST_FUNC unpack_xz_stream(transformer_state_t *xstate)
{
	IF_DESKTOP(long long) int n = 0;
	struct xz_buf b;
	struct xz_dec *s;
	enum xz_ret ret = XZ_STREAM_END;
	uint8_t *in = NULL, *out = NULL;
	ssize_t nwrote;

	xz_crc32_init();

	/*
	 * Support up to 64 MiB dictionary. The actually needed memory
	 * is allocated once the headers have been parsed.
	 */
	s = xz_dec_init(XZ_DYNALLOC, 1 << 26);
	if (!s)
		bb_error_msg_and_err("memory allocation error");

	in = xmalloc(BUFSIZ);
	out = xmalloc(BUFSIZ);

	b.in = in;
	b.in_pos = 0;
	b.in_size = 0;
	b.out = out;
	b.out_pos = 0;
	b.out_size = BUFSIZ;

	while (true) {
		if (b.in_pos == b.in_size) {
			b.in_size = safe_read(xstate->src_fd, in, BUFSIZ);
			if ((int)b.in_size < 0)
				bb_error_msg_and_err("read error (errno: %d)", errno);
			b.in_pos = 0;
		}
		ret = xz_dec_run(s, &b);

		if (b.out_pos == BUFSIZ) {
			nwrote = transformer_write(xstate, b.out, b.out_pos);
			if (nwrote == (ssize_t)-1) {
				ret = XZ_DATA_ERROR;
				bb_error_msg_and_err("write error (errno: %d)", errno);
			}
			IF_DESKTOP(n += nwrote;)
			b.out_pos = 0;
		}

		if (ret == XZ_OK)
			continue;

#ifdef XZ_DEC_ANY_CHECK
		if (ret == XZ_UNSUPPORTED_CHECK) {
			bb_error_msg("unsupported check; not verifying file integrity");
			continue;
		}
#endif

		nwrote = transformer_write(xstate, b.out, b.out_pos);
		if (nwrote == (ssize_t)-1) {
			ret = XZ_DATA_ERROR;
			bb_error_msg_and_err("write error (errno: %d)", errno);
		}
		IF_DESKTOP(n += nwrote;)

		switch (ret) {
		case XZ_STREAM_END:
			ret = XZ_OK;
			goto out;

		case XZ_MEM_ERROR:
			bb_error_msg_and_err("memory allocation error");

		case XZ_MEMLIMIT_ERROR:
			bb_error_msg_and_err("memory usage limit error");

		case XZ_FORMAT_ERROR:
			bb_error_msg_and_err("not a .xz file");

		case XZ_OPTIONS_ERROR:
			bb_error_msg_and_err("unsupported XZ header option");

		case XZ_DATA_ERROR:
			bb_error_msg_and_err("corrupted archive");
		case XZ_BUF_ERROR:
			bb_error_msg_and_err("corrupted buffer");

		default:
			bb_error_msg_and_err("XZ decompression bug!");
		}
	}

out:
err:
	xz_dec_end(s);
	free(in);
	free(out);
	return (ret == XZ_OK)?n:-ret;
}
