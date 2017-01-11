/*
 * unzip implementation for Bled/busybox
 *
 * Copyright © 2015 Pete Batard <pete@akeo.ie>
 * Based on mini unzip implementation for busybox © Ed Clark
 * Loosely based on original busybox unzip applet © Laurence Anderson.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */

/* The following are not currently supported:
 * - Store (uncompressed)
 * - Zip64
 */

#include "libbb.h"
#include "bb_archive.h"

enum {
#if BB_BIG_ENDIAN
	ZIP_FILEHEADER_MAGIC = 0x504b0304,
	ZIP_CDF_MAGIC        = 0x504b0102, /* central directory's file header */
	ZIP_CDE_MAGIC        = 0x504b0506, /* "end of central directory" record */
	ZIP_DD_MAGIC         = 0x504b0708,
#else
	ZIP_FILEHEADER_MAGIC = 0x04034b50,
	ZIP_CDF_MAGIC        = 0x02014b50,
	ZIP_CDE_MAGIC        = 0x06054b50,
	ZIP_DD_MAGIC         = 0x08074b50,
#endif
};

#define ZIP_HEADER_LEN 26

PRAGMA_BEGIN_PACKED
typedef union {
	uint8_t raw[ZIP_HEADER_LEN];
	struct {
		uint16_t version;               /* 0-1 */
		uint16_t zip_flags;             /* 2-3 */
		uint16_t method;                /* 4-5 */
		uint16_t modtime;               /* 6-7 */
		uint16_t moddate;               /* 8-9 */
		uint32_t crc32;                 /* 10-13 */
		uint32_t cmpsize;               /* 14-17 */
		uint32_t ucmpsize;              /* 18-21 */
		uint16_t filename_len;          /* 22-23 */
		uint16_t extra_len;             /* 24-25 */
	} PACKED formatted;
} PACKED zip_header_t;
PRAGMA_END_PACKED

/* Check the offset of the last element, not the length.  This leniency
 * allows for poor packing, whereby the overall struct may be too long,
 * even though the elements are all in the right place.
 */
struct BUG_zip_header_must_be_26_bytes {
	char BUG_zip_header_must_be_26_bytes[
		offsetof(zip_header_t, formatted.extra_len) + 2
			== ZIP_HEADER_LEN ? 1 : -1];
};

#define FIX_ENDIANNESS_ZIP(zip_header) do { \
	(zip_header).formatted.version      = SWAP_LE16((zip_header).formatted.version     ); \
	(zip_header).formatted.method       = SWAP_LE16((zip_header).formatted.method      ); \
	(zip_header).formatted.modtime      = SWAP_LE16((zip_header).formatted.modtime     ); \
	(zip_header).formatted.moddate      = SWAP_LE16((zip_header).formatted.moddate     ); \
	(zip_header).formatted.crc32        = SWAP_LE32((zip_header).formatted.crc32       ); \
	(zip_header).formatted.cmpsize      = SWAP_LE32((zip_header).formatted.cmpsize     ); \
	(zip_header).formatted.ucmpsize     = SWAP_LE32((zip_header).formatted.ucmpsize    ); \
	(zip_header).formatted.filename_len = SWAP_LE16((zip_header).formatted.filename_len); \
	(zip_header).formatted.extra_len    = SWAP_LE16((zip_header).formatted.extra_len   ); \
} while (0)

#define CDF_HEADER_LEN 42

PRAGMA_BEGIN_PACKED
typedef union {
	uint8_t raw[CDF_HEADER_LEN];
	struct {
		/* uint32_t signature; 50 4b 01 02 */
		uint16_t version_made_by;       /* 0-1 */
		uint16_t version_needed;        /* 2-3 */
		uint16_t cdf_flags;             /* 4-5 */
		uint16_t method;                /* 6-7 */
		uint16_t mtime;                 /* 8-9 */
		uint16_t mdate;                 /* 10-11 */
		uint32_t crc32;                 /* 12-15 */
		uint32_t cmpsize;               /* 16-19 */
		uint32_t ucmpsize;              /* 20-23 */
		uint16_t file_name_length;      /* 24-25 */
		uint16_t extra_field_length;    /* 26-27 */
		uint16_t file_comment_length;   /* 28-29 */
		uint16_t disk_number_start;     /* 30-31 */
		uint16_t internal_file_attributes; /* 32-33 */
		uint32_t external_file_attributes; /* 34-37 */
		uint32_t relative_offset_of_local_header; /* 38-41 */
	} PACKED formatted;
} cdf_header_t;
PRAGMA_END_PACKED

struct BUG_cdf_header_must_be_42_bytes {
	char BUG_cdf_header_must_be_42_bytes[
		offsetof(cdf_header_t, formatted.relative_offset_of_local_header) + 4
			== CDF_HEADER_LEN ? 1 : -1];
};

#define FIX_ENDIANNESS_CDF(cdf_header) do { \
	(cdf_header).formatted.crc32        = SWAP_LE32((cdf_header).formatted.crc32       ); \
	(cdf_header).formatted.cmpsize      = SWAP_LE32((cdf_header).formatted.cmpsize     ); \
	(cdf_header).formatted.ucmpsize     = SWAP_LE32((cdf_header).formatted.ucmpsize    ); \
	(cdf_header).formatted.file_name_length = SWAP_LE16((cdf_header).formatted.file_name_length); \
	(cdf_header).formatted.extra_field_length = SWAP_LE16((cdf_header).formatted.extra_field_length); \
	(cdf_header).formatted.file_comment_length = SWAP_LE16((cdf_header).formatted.file_comment_length); \
	IF_DESKTOP( \
	(cdf_header).formatted.version_made_by = SWAP_LE16((cdf_header).formatted.version_made_by); \
	(cdf_header).formatted.external_file_attributes = SWAP_LE32((cdf_header).formatted.external_file_attributes); \
	) \
} while (0)

#define CDE_HEADER_LEN 16

PRAGMA_BEGIN_PACKED
typedef union {
	uint8_t raw[CDE_HEADER_LEN];
	struct {
		/* uint32_t signature; 50 4b 05 06 */
		uint16_t this_disk_no;
		uint16_t disk_with_cdf_no;
		uint16_t cdf_entries_on_this_disk;
		uint16_t cdf_entries_total;
		uint32_t cdf_size;
		uint32_t cdf_offset;
		/* uint16_t file_comment_length; */
		/* .ZIP file comment (variable size) */
	} PACKED formatted;
} cde_header_t;
PRAGMA_END_PACKED

struct BUG_cde_header_must_be_16_bytes {
	char BUG_cde_header_must_be_16_bytes[
		sizeof(cde_header_t) == CDE_HEADER_LEN ? 1 : -1];
};

#define FIX_ENDIANNESS_CDE(cde_header) do { \
	(cde_header).formatted.cdf_offset = SWAP_LE32((cde_header).formatted.cdf_offset); \
} while (0)


#if ENABLE_DESKTOP

/* Seen in the wild:
 * Self-extracting PRO2K3XP_32.exe contains 19078464 byte zip archive,
 * where CDE was nearly 48 kbytes before EOF.
 * (Surprisingly, it also apparently has *another* CDE structure
 * closer to the end, with bogus cdf_offset).
 * To make extraction work, bumped PEEK_FROM_END from 16k to 64k.
 */
#define PEEK_FROM_END (64*1024)

/* This value means that we failed to find CDF */
#define BAD_CDF_OFFSET ((uint32_t)0xffffffff)

/* NB: does not preserve file position! */
static uint32_t find_cdf_offset(void)
{
	cde_header_t cde_header;
	unsigned char *p;
	off_t end;
	unsigned char *buf = xzalloc(PEEK_FROM_END);

	end = xlseek(zip_fd, 0, SEEK_END);
	end -= PEEK_FROM_END;
	if (end < 0)
		end = 0;
	xlseek(zip_fd, end, SEEK_SET);
	full_read(zip_fd, buf, PEEK_FROM_END);

	cde_header.formatted.cdf_offset = BAD_CDF_OFFSET;
	p = buf;
	while (p <= buf + PEEK_FROM_END - CDE_HEADER_LEN - 4) {
		if (*p != 'P') {
			p++;
			continue;
		}
		if (*++p != 'K')
			continue;
		if (*++p != 5)
			continue;
		if (*++p != 6)
			continue;
		/* we found CDE! */
		memcpy(cde_header.raw, p + 1, CDE_HEADER_LEN);
		FIX_ENDIANNESS_CDE(cde_header);
		/*
		 * I've seen .ZIP files with seemingly valid CDEs
		 * where cdf_offset points past EOF - ??
		 * Ignore such CDEs:
		 */
		if (cde_header.formatted.cdf_offset < end + (p - buf))
			break;
		cde_header.formatted.cdf_offset = BAD_CDF_OFFSET;
	}
	free(buf);
	return cde_header.formatted.cdf_offset;
};

static uint32_t read_next_cdf(uint32_t cdf_offset, cdf_header_t *cdf_ptr)
{
	off_t org;

	org = xlseek(zip_fd, 0, SEEK_CUR);

	if (!cdf_offset)
		cdf_offset = find_cdf_offset();

	if (cdf_offset != BAD_CDF_OFFSET) {
		xlseek(zip_fd, cdf_offset + 4, SEEK_SET);
		xread(zip_fd, cdf_ptr->raw, CDF_HEADER_LEN);
		FIX_ENDIANNESS_CDF(*cdf_ptr);
		cdf_offset += 4 + CDF_HEADER_LEN
			+ cdf_ptr->formatted.file_name_length
			+ cdf_ptr->formatted.extra_field_length
			+ cdf_ptr->formatted.file_comment_length;
	}

	xlseek(zip_fd, org, SEEK_SET);
	return cdf_offset;
};
#endif

static void unzip_skip(int zip_fd, off_t skip)
{
	if (skip != 0)
		if (lseek(zip_fd, skip, SEEK_CUR) == (off_t)-1)
			bb_copyfd_exact_size(zip_fd, -1, skip);
}

IF_DESKTOP(long long) int FAST_FUNC unpack_zip_stream(transformer_state_t *xstate)
{
	IF_DESKTOP(long long) int n = -EFAULT;
	zip_header_t zip_header;
	char *filename = NULL;
#if ENABLE_DESKTOP
	uint32_t cdf_offset = 0;
#endif

	while (1) {
		uint32_t magic;
		/* Check magic number */
		safe_read(xstate->src_fd, &magic, 4);
		/* Central directory? It's at the end, so exit */
		if (magic == ZIP_CDF_MAGIC)
			break;
#if ENABLE_DESKTOP
		/* Data descriptor? It was a streaming file, go on */
		if (magic == ZIP_DD_MAGIC) {
			/* skip over duplicate crc32, cmpsize and ucmpsize */
			unzip_skip(xstate->src_fd, 3 * 4);
			continue;
		}
#endif
		if (magic != ZIP_FILEHEADER_MAGIC)
			bb_error_msg_and_err("invalid zip magic %08X", (int)magic);

		/* Read the file header */
		safe_read(xstate->src_fd, zip_header.raw, ZIP_HEADER_LEN);
		FIX_ENDIANNESS_ZIP(zip_header);
		if (zip_header.formatted.method != 8) {
			bb_error_msg_and_err("zip method method %d is not supported", zip_header.formatted.method);
		}
#if !ENABLE_DESKTOP
		if (zip_header.formatted.zip_flags & SWAP_LE16(0x0009)) {
			bb_error_msg_and_err("zip flags 1 and 8 are not supported");
		}
#else
		if (zip_header.formatted.zip_flags & SWAP_LE16(0x0001)) {
			/* 0x0001 - encrypted */
			bb_error_msg_and_die("zip flag 1 (encryption) is not supported");
		}

		if (cdf_offset != BAD_CDF_OFFSET) {
			cdf_header_t cdf_header;
			cdf_offset = read_next_cdf(cdf_offset, &cdf_header);
			/*
				* Note: cdf_offset can become BAD_CDF_OFFSET after the above call.
				*/
			if (zip_header.formatted.zip_flags & SWAP_LE16(0x0008)) {
				/* 0x0008 - streaming. [u]cmpsize can be reliably gotten
					* only from Central Directory. See unzip_doc.txt
					*/
				zip_header.formatted.crc32    = cdf_header.formatted.crc32;
				zip_header.formatted.cmpsize  = cdf_header.formatted.cmpsize;
				zip_header.formatted.ucmpsize = cdf_header.formatted.ucmpsize;
			}
			if ((cdf_header.formatted.version_made_by >> 8) == 3) {
				/* This archive is created on Unix */
				dir_mode = file_mode = (cdf_header.formatted.external_file_attributes >> 16);
			}
		}
		if (cdf_offset == BAD_CDF_OFFSET
			&& (zip_header.formatted.zip_flags & SWAP_LE16(0x0008))
		) {
			/* If it's a streaming zip, we _require_ CDF */
			bb_error_msg_and_die("can't find file table");
		}
#endif

		/* Read filename */
		filename = xzalloc(zip_header.formatted.filename_len + 1);
		safe_read(xstate->src_fd, filename, zip_header.formatted.filename_len);
		bb_printf("Processing archive file '%s'", filename);
		free(filename);

		/* Skip extra header bytes */
		unzip_skip(xstate->src_fd, zip_header.formatted.extra_len);

		/* Method 8 - inflate */
		xstate->bytes_in = zip_header.formatted.cmpsize;
		n = inflate_unzip(xstate);

		/* Validate decompression */
		if (n >= 0) {
			if (zip_header.formatted.ucmpsize != xstate->bytes_out)
				bb_error_msg_and_err("bad length");
			else if (zip_header.formatted.crc32 != (xstate->crc32 ^ 0xffffffffL))
				bb_error_msg_and_err("crc error");
		} else if (n != -ENOSPC) {
			bb_error_msg_and_err("inflate error");
		}
	}

err:
	if (n > 0)
		return xstate->bytes_out;
	else if (n == -ENOSPC)
		return xstate->mem_output_size_max;
	else
		return n;
}
