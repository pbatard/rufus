/*
 * unzip implementation for Bled/busybox
 *
 * Copyright © 2015-2023 Pete Batard <pete@akeo.ie>
 * Based on mini unzip implementation for busybox © Ed Clark
 * Loosely based on original busybox unzip applet © Laurence Anderson.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */

#include "libbb.h"
#include "bb_archive.h"

#if 0
# define dbg(...) bb_printf(__VA_ARGS__)
#else
# define dbg(...) ((void)0)
#endif

#define xread safe_read

enum {
#if BB_BIG_ENDIAN
	ZIP_FILEHEADER_MAGIC = 0x504b0304,
	ZIP_CDF_MAGIC        = 0x504b0102, /* CDF item */
	ZIP_CDE_MAGIC        = 0x504b0506, /* End of CDF */
	ZIP64_CDE_MAGIC      = 0x504b0606, /* End of Zip64 CDF */
	ZIP_DD_MAGIC         = 0x504b0708,
#else
	ZIP_FILEHEADER_MAGIC = 0x04034b50,
	ZIP_CDF_MAGIC        = 0x02014b50,
	ZIP_CDE_MAGIC        = 0x06054b50,
	ZIP64_CDE_MAGIC      = 0x06064b50,
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
		uint32_t crc32 PACKED;          /* 10-13 */
		uint32_t cmpsize PACKED;        /* 14-17 */
		uint32_t ucmpsize PACKED;       /* 18-21 */
		uint16_t filename_len;          /* 22-23 */
		uint16_t extra_len;             /* 24-25 */
		/* filename follows (not NUL terminated) */
		/* extra field follows */
		/* data follows */
	} fmt PACKED;
} zip_header_t; /* PACKED - gcc 4.2.1 doesn't like it (spews warning) */
PRAGMA_END_PACKED

#define FIX_ENDIANNESS_ZIP(zip) \
do { if (BB_BIG_ENDIAN) { \
	(zip).fmt.method        = SWAP_LE16((zip).fmt.method      ); \
	(zip).fmt.modtime       = SWAP_LE16((zip).fmt.modtime     ); \
	(zip).fmt.moddate       = SWAP_LE16((zip).fmt.moddate     ); \
	(zip).fmt.crc32         = SWAP_LE32((zip).fmt.crc32       ); \
	(zip).fmt.cmpsize       = SWAP_LE32((zip).fmt.cmpsize     ); \
	(zip).fmt.ucmpsize      = SWAP_LE32((zip).fmt.ucmpsize    ); \
	(zip).fmt.filename_len  = SWAP_LE16((zip).fmt.filename_len); \
	(zip).fmt.extra_len     = SWAP_LE16((zip).fmt.extra_len   ); \
}} while (0)

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
		uint16_t modtime;               /* 8-9 */
		uint16_t moddate;               /* 10-11 */
		uint32_t crc32;                 /* 12-15 */
		uint32_t cmpsize;               /* 16-19 */
		uint32_t ucmpsize;              /* 20-23 */
		uint16_t filename_len;          /* 24-25 */
		uint16_t extra_len;             /* 26-27 */
		uint16_t file_comment_length;   /* 28-29 */
		uint16_t disk_number_start;     /* 30-31 */
		uint16_t internal_attributes;   /* 32-33 */
		uint32_t external_attributes PACKED; /* 34-37 */
		uint32_t relative_offset_of_local_header PACKED; /* 38-41 */
		/* filename follows (not NUL terminated) */
		/* extra field follows */
		/* file comment follows */
	} fmt PACKED;
} cdf_header_t;
PRAGMA_END_PACKED

#define FIX_ENDIANNESS_CDF(cdf) \
do { if (BB_BIG_ENDIAN) { \
	(cdf).fmt.version_made_by = SWAP_LE16((cdf).fmt.version_made_by); \
	(cdf).fmt.version_needed  = SWAP_LE16((cdf).fmt.version_needed ); \
	(cdf).fmt.method          = SWAP_LE16((cdf).fmt.method         ); \
	(cdf).fmt.modtime         = SWAP_LE16((cdf).fmt.modtime        ); \
	(cdf).fmt.moddate         = SWAP_LE16((cdf).fmt.moddate        ); \
	(cdf).fmt.crc32           = SWAP_LE32((cdf).fmt.crc32          ); \
	(cdf).fmt.cmpsize         = SWAP_LE32((cdf).fmt.cmpsize        ); \
	(cdf).fmt.ucmpsize        = SWAP_LE32((cdf).fmt.ucmpsize       ); \
	(cdf).fmt.filename_len    = SWAP_LE16((cdf).fmt.filename_len   ); \
	(cdf).fmt.extra_len       = SWAP_LE16((cdf).fmt.extra_len      ); \
	(cdf).fmt.file_comment_length = SWAP_LE16((cdf).fmt.file_comment_length); \
	(cdf).fmt.external_attributes = SWAP_LE32((cdf).fmt.external_attributes); \
}} while (0)

#define CDE_LEN 16

PRAGMA_BEGIN_PACKED
typedef union {
	uint8_t raw[CDE_LEN];
	struct {
		/* uint32_t signature; 50 4b 05 06 */
		uint16_t this_disk_no;
		uint16_t disk_with_cdf_no;
		uint16_t cdf_entries_on_this_disk;
		uint16_t cdf_entries_total;
		uint32_t cdf_size;
		uint32_t cdf_offset;
		/* uint16_t archive_comment_length; */
		/* archive comment follows */
	} fmt PACKED;
} cde_t;
PRAGMA_END_PACKED

#define FIX_ENDIANNESS_CDE(cde) \
do { if (BB_BIG_ENDIAN) { \
	(cde).fmt.cdf_offset = SWAP_LE32((cde).fmt.cdf_offset); \
}} while (0)

/* Extra records */
#define EXTRA_HEADER_LEN 4

PRAGMA_BEGIN_PACKED
typedef union {
	uint8_t raw[EXTRA_HEADER_LEN];
	struct {
		uint16_t tag;
		uint16_t length;
		/* extra record follows */
	} fmt PACKED;
} extra_header_t;
PRAGMA_END_PACKED

#define FIX_ENDIANNESS_EXTRA(ext) \
do { if (BB_BIG_ENDIAN) { \
	(ext).fmt.tag = SWAP_LE16((ext).fmt.tag); \
	(ext).fmt.length = SWAP_LE32((ext).fmt.length); \
}} while (0)

/* ZIP64 records */
#define ZIP64_LEN 20

PRAGMA_BEGIN_PACKED
typedef union {
	uint8_t raw[ZIP64_LEN];
	struct {
		/* uint16_t tag; 00 01 */
		uint16_t tag;
		uint16_t length;
		uint64_t ucmpsize PACKED;
		uint64_t cmpsize PACKED;
	} fmt PACKED;
} zip64_t;
PRAGMA_END_PACKED

#define FIX_ENDIANNESS_ZIP64(z64) \
do { if (BB_BIG_ENDIAN) { \
	(z64).fmt.cmpsize = SWAP_LE64((z64).fmt.cmpsize); \
	(z64).fmt.ucmpsize = SWAP_LE64((z64).fmt.ucmpsize); \
}} while (0)

#define CDE64_LEN 52

PRAGMA_BEGIN_PACKED
typedef union {
	uint8_t raw[CDE64_LEN];
	struct {
		/* uint32_t signature; 50 4b 06 06 */
		uint64_t size PACKED;
		uint16_t version_made_by;
		uint16_t version_needed;
		uint32_t this_disk_no;
		uint32_t disk_with_cdf_no;
		uint64_t cdf_entries_on_this_disk PACKED;
		uint64_t cdf_entries_total PACKED;
		uint64_t cdf_size PACKED;
		uint64_t cdf_offset PACKED;
		/* archive comment follows */
	} fmt PACKED;
} cde64_t;
PRAGMA_END_PACKED

#define FIX_ENDIANNESS_CDE64(c64) \
do { if (BB_BIG_ENDIAN) { \
	(c64).fmt.size = SWAP_LE64((c64).fmt.size); \
	(c64).fmt.cdf_offset = SWAP_LE64((c64).fmt.cdf_offset); \
}} while (0)

inline void BUG(void) {
	/* Check the offset of the last element, not the length.  This leniency
	 * allows for poor packing, whereby the overall struct may be too long,
	 * even though the elements are all in the right place.
	 */
	BUILD_BUG_ON(
		offsetof(zip_header_t, fmt.extra_len) + 2
			!= ZIP_HEADER_LEN);
	BUILD_BUG_ON(
		offsetof(cdf_header_t, fmt.relative_offset_of_local_header) + 4
			!= CDF_HEADER_LEN);
	BUILD_BUG_ON(sizeof(cde_t) != CDE_LEN);
	BUILD_BUG_ON(sizeof(extra_header_t) != EXTRA_HEADER_LEN);
	BUILD_BUG_ON(sizeof(zip64_t) != ZIP64_LEN);
	BUILD_BUG_ON(sizeof(cde64_t) != CDE64_LEN);
}

/* This value means that we failed to find CDF */
#define BAD_CDF_OFFSET ((uint32_t)0xffffffff)

#if !ENABLE_FEATURE_UNZIP_CDF

# define find_cdf_offset(fd) BAD_CDF_OFFSET

#else
/* Seen in the wild:
 * Self-extracting PRO2K3XP_32.exe contains 19078464 byte zip archive,
 * where CDE was nearly 48 kbytes before EOF.
 * (Surprisingly, it also apparently has *another* CDE structure
 * closer to the end, with bogus cdf_offset).
 * To make extraction work, bumped PEEK_FROM_END from 16k to 64k.
 */
#define PEEK_FROM_END (64*1024)
/* NB: does not preserve file position! */
static uint64_t find_cdf_offset(int fd)
{
	cde_t cde;
	cde64_t cde64;
	unsigned char *buf;
	unsigned char *p, c;
	uint64_t end = 0, found, size;

	size = lseek(fd, 0, SEEK_END);
	if (size == (off_t) -1)
		return BAD_CDF_OFFSET;
	
	if (size > PEEK_FROM_END)
		end = size - PEEK_FROM_END;
	else
		end = 0;

	size = MIN(size, PEEK_FROM_END);

	lseek(fd, end, SEEK_SET);
	buf = xzalloc((size_t)size);
	if (buf == NULL)
		return 0;	
	full_read(fd, buf, (unsigned int)size);

	found = BAD_CDF_OFFSET;
	p = buf;
	while (p <= buf + size - CDE_LEN - 4) {
		if (*p != 'P') {
			p++;
			continue;
		}
		if (*++p != 'K')
			continue;
		c = *++p;
		if (c != 5 && c != 6)
			continue;
		if (*++p != 6)
			continue;
		/* we found CDE! */
		switch (c) {
		case 5: /* 32-bit CDE */
			memcpy(cde.raw, p + 1, CDE_LEN);
			dbg("cde.this_disk_no:%d", cde.fmt.this_disk_no);
			dbg("cde.disk_with_cdf_no:%d", cde.fmt.disk_with_cdf_no);
			dbg("cde.cdf_entries_on_this_disk:%d", cde.fmt.cdf_entries_on_this_disk);
			dbg("cde.cdf_entries_total:%d", cde.fmt.cdf_entries_total);
			dbg("cde.cdf_size:%d", cde.fmt.cdf_size);
			dbg("cde.cdf_offset:%x", cde.fmt.cdf_offset);
			FIX_ENDIANNESS_CDE(cde);
			/*
			 * I've seen .ZIP files with seemingly valid CDEs
			 * where cdf_offset points past EOF - ??
			 * This check ignores such CDEs:
			 */
			if (cde.fmt.cdf_offset != 0xffffffffL &&
				cde.fmt.cdf_offset < end + (p - buf)) {
				found = cde.fmt.cdf_offset;
				dbg("Possible cdf_offset:0x%"OFF_FMT"x at 0x%"OFF_FMT"x",
					found, end + (p - 3 - buf));
				dbg("  cdf_offset+cdf_size:0x%"OFF_FMT"x",
					(found + SWAP_LE32(cde.fmt.cdf_size)));
				/*
				 * We do not "break" here because only the last CDE is valid.
				 * I've seen a .zip archive which contained a .zip file,
				 * uncompressed, and taking the first CDE was using
				 * the CDE inside that file!
				 */
			}
			break;
		case 6: /* 64-bit CDE */
			memcpy(cde64.raw, p + 1, CDE64_LEN);
			FIX_ENDIANNESS_CDE64(cde64);
			dbg("cde64.this_disk_no:%d", cde64.fmt.this_disk_no);
			dbg("cde64.disk_with_cdf_no:%d", cde64.fmt.disk_with_cdf_no);
			dbg("cde64.cdf_entries_on_this_disk:%lld", cde64.fmt.cdf_entries_on_this_disk);
			dbg("cde64.cdf_entries_total:%lld", cde64.fmt.cdf_entries_total);
			dbg("cde64.cdf_size:%lld", cde64.fmt.cdf_size);
			dbg("cde64.cdf_offset:%llx", cde64.fmt.cdf_offset);
			if (cde64.fmt.cdf_offset < end + (p - buf)) {
				found = cde64.fmt.cdf_offset;
				dbg("Possible cdf_offset:0x%"OFF_FMT"x at 0x%"OFF_FMT"x",
					found, end + (p - 3 - buf));
				dbg("  cdf_offset+cdf_size:0x%"OFF_FMT"x",
					(found + SWAP_LE64(cde64.fmt.cdf_size)));
			}
			break;
		}
	}
	free(buf);
	dbg("Found cdf_offset:0x%"OFF_FMT"x", found);
	return found;
};

static uint64_t read_next_cdf(int fd, uint64_t cdf_offset, cdf_header_t *cdf)
{
	uint32_t magic;

	if (cdf_offset == BAD_CDF_OFFSET)
		return cdf_offset;

	dbg("Reading CDF at 0x%"OFF_FMT"x", cdf_offset);
	lseek(fd, cdf_offset, SEEK_SET);
	(void)_read(fd, &magic, 4);
	/* Central Directory End? Assume CDF has ended.
	 * (more correct method is to use cde.cdf_entries_total counter)
	 */
	if (magic == ZIP_CDE_MAGIC) {
		dbg("got ZIP_CDE_MAGIC");
		return 0; /* EOF */
	}
	if (magic == ZIP64_CDE_MAGIC) { /* seen in .zip with >4GB files */
		dbg("got ZIP64_CDE_MAGIC");
		return 0; /* EOF */
	}
	(void)_read(fd, cdf->raw, CDF_HEADER_LEN);

	FIX_ENDIANNESS_CDF(*cdf);
	dbg("  magic:%08x filename_len:%u extra_len:%u file_comment_length:%u",
		magic,
		(unsigned)cdf->fmt.filename_len,
		(unsigned)cdf->fmt.extra_len,
		(unsigned)cdf->fmt.file_comment_length
	);
//TODO: require that magic == ZIP_CDF_MAGIC?

	cdf_offset += 4 + CDF_HEADER_LEN
		+ cdf->fmt.filename_len
		+ cdf->fmt.extra_len
		+ cdf->fmt.file_comment_length;

	dbg("Next cdf_offset 0x%"OFF_FMT"x", cdf_offset);
	return cdf_offset;
};
#endif

static void die_if_bad_fnamesize(unsigned sz)
{
	if (sz > 0xfff) /* more than 4k?! no funny business please */
		bb_simple_error_msg_and_die("bad archive");
}

static void unzip_skip(int fd, off_t skip)
{
	if (skip != 0)
		if (lseek(fd, skip, SEEK_CUR) == (off_t)-1)
			bb_copyfd_exact_size(fd, -1, skip);
}

/* Set the filename and process extra ZIP64 data */
static void unzip_set_xstate(transformer_state_t* xstate, zip_header_t* zip)
{
	uint8_t* buf = NULL;
	uint16_t i = 0;
	extra_header_t* extra;
	zip64_t* zip64;

	/* Set the default sizes for non ZIP64 content */
	xstate->dst_size = zip->fmt.ucmpsize;
	xstate->bytes_in = zip->fmt.cmpsize;

	/* Set the filename */
	die_if_bad_fnamesize(zip->fmt.filename_len);
	xstate->dst_name = xzalloc(zip->fmt.filename_len + 1);
	if (xstate->dst_name == NULL)
		goto err;
	xread(xstate->src_fd, xstate->dst_name, zip->fmt.filename_len);
	xstate->dst_name[zip->fmt.filename_len] = 0;

	/* Read the extra data */
	if (zip->fmt.extra_len) {
		dbg("Reading extra data");
		buf = malloc(zip->fmt.extra_len);
		if (buf == NULL)
			goto err;
		xread(xstate->src_fd, buf, zip->fmt.extra_len);
	}

	/* Process the extra records */
	if (zip->fmt.extra_len < EXTRA_HEADER_LEN + 1)
		goto err;
	for (i = 0; i < zip->fmt.extra_len - EXTRA_HEADER_LEN; ) {
		extra = (extra_header_t*)&buf[i];
		FIX_ENDIANNESS_EXTRA(*extra);
		dbg("  tag:0x%04x len:%u",
			(unsigned)extra->fmt.tag,
			(unsigned)extra->fmt.length
		);
		i += EXTRA_HEADER_LEN + extra->fmt.length;
		/* Process the ZIP64 data */
		if (extra->fmt.tag == SWAP_LE16(0x0001)) {
			zip64 = (zip64_t*)extra;
			FIX_ENDIANNESS_ZIP64(*zip64);
			if ((zip->fmt.cmpsize == 0xffffffffL || zip->fmt.ucmpsize == 0xffffffffL) &&
				EXTRA_HEADER_LEN + zip64->fmt.length >= ZIP64_LEN) {
				dbg("Actual cmpsize:0x%"OFF_FMT"x ucmpsize:0x%"OFF_FMT"x",
					zip64->fmt.cmpsize,
					zip64->fmt.ucmpsize
				);
				xstate->dst_size = zip64->fmt.ucmpsize;
				xstate->bytes_in = zip64->fmt.cmpsize;
			}
		}
	}

err:
	free(buf);
}

static IF_DESKTOP(long long) int
unzip_extract(zip_header_t* zip, transformer_state_t* xstate)
{
	IF_DESKTOP(long long) int n = -EFAULT;

	if (zip->fmt.method == 0) {
		/* Method 0 - stored (not compressed) */
		if (xstate->dst_size) {
			bb_copyfd_exact_size(xstate->src_fd, xstate->dst_fd, xstate->dst_size);
		}
		return xstate->dst_size;
	}

	if (zip->fmt.method == 8) {
		/* Method 8 - inflate */
		n = inflate_unzip(xstate);

		/* Validate decompression */
		if (n >= 0) {
			if (zip->fmt.crc32 != (xstate->crc32 ^ 0xffffffffL)) {
				bb_simple_error_msg_and_die("crc error");
			}
		} else if (n != -ENOSPC) {
			bb_simple_error_msg_and_die("inflate error");
		}
	}
#if ENABLE_FEATURE_UNZIP_BZIP2
	else if (zip->fmt.method == 12) {
		/* Tested. Unpacker reads too much, but we use CDF
		 * and will seek to the correct beginning of next file.
		 */
		xstate->bytes_out = unpack_bz2_stream(xstate);
		if ((int64_t)xstate->bytes_out < 0)
			bb_simple_error_msg_and_die("inflate error");
	}
#endif
#if ENABLE_FEATURE_UNZIP_LZMA
	else if (zip->fmt.method == 14) {
		/* Not tested yet */
		xstate->bytes_out = unpack_lzma_stream(xstate);
		if ((int64_t)xstate->bytes_out < 0)
			bb_simple_error_msg_and_die("inflate error");
	}
#endif
#if ENABLE_FEATURE_UNZIP_XZ
	else if (zip->fmt.method == 95) {
		/* Not tested yet */
		xstate->bytes_out = unpack_xz_stream(xstate);
		if ((int64_t)xstate->bytes_out < 0)
			bb_simple_error_msg_and_die("inflate error");
	}
#endif
	else {
		bb_error_msg_and_die("unsupported method %u", zip->fmt.method);
	}

	/* Validate decompression - size */
	if (n != -ENOSPC && xstate->dst_size != xstate->bytes_out) {
		/* Don't die. Who knows, maybe len calculation
		 * was botched somewhere. After all, crc matched! */
		bb_simple_error_msg("bad length");
	}
	return n;
}


IF_DESKTOP(long long) int FAST_FUNC
unpack_zip_stream(transformer_state_t *xstate)
{
	IF_DESKTOP(long long) int n = -EFAULT;
	bool is_dir = false;
	uint64_t cdf_offset = find_cdf_offset(xstate->src_fd);	/* try to seek to the end, find CDE and CDF start */

	while (1) {
		zip_header_t zip;
		if (!ENABLE_FEATURE_UNZIP_CDF || cdf_offset == BAD_CDF_OFFSET) {
			/* Normally happens when input is unseekable.
			 *
			 * Valid ZIP file has Central Directory at the end
			 * with central directory file headers (CDFs).
			 * After it, there is a Central Directory End structure.
			 * CDFs identify what files are in the ZIP and where
			 * they are located. This allows ZIP readers to load
			 * the list of files without reading the entire ZIP archive.
			 * ZIP files may be appended to, only files specified in
			 * the CD are valid. Scanning for local file headers is
			 * not a correct algorithm.
			 *
			 * We try to do the above, and resort to "linear" reading
			 * of ZIP file only if seek failed or CDE wasn't found.
			 */
			uint32_t magic;

			/* Check magic number */
			xread(xstate->src_fd, &magic, 4);
			/* CDF item? Assume there are no more files, exit */
			if (magic == ZIP_CDF_MAGIC) {
				dbg("got ZIP_CDF_MAGIC");
				break;
			}
			/* Data descriptor? It was a streaming file, go on */
			if (magic == ZIP_DD_MAGIC) {
				dbg("got ZIP_DD_MAGIC");
				/* skip over duplicate crc32, cmpsize and ucmpsize */
				unzip_skip(xstate->src_fd, 3 * 4);
				continue;
			}
			if (magic != ZIP_FILEHEADER_MAGIC)
				bb_error_msg_and_die("invalid zip magic %08X", (int)magic);
			dbg("got ZIP_FILEHEADER_MAGIC");

			xread(xstate->src_fd, zip.raw, ZIP_HEADER_LEN);
			FIX_ENDIANNESS_ZIP(zip);
			if (zip.fmt.zip_flags & SWAP_LE16(0x0008)) {
				bb_error_msg_and_die("zip flag %s is not supported",
					"8 (streaming)");
			}
		}
#if ENABLE_FEATURE_UNZIP_CDF
		else {
			/* cdf_offset is valid (and we know the file is seekable) */
			cdf_header_t cdf;
			cdf_offset = read_next_cdf(xstate->src_fd, cdf_offset, &cdf);
			if (cdf_offset == 0) /* EOF? */
				break;
# if 1
			lseek(xstate->src_fd,
				SWAP_LE32(cdf.fmt.relative_offset_of_local_header) + 4,
				SEEK_SET);
			xread(xstate->src_fd, zip.raw, ZIP_HEADER_LEN);
			FIX_ENDIANNESS_ZIP(zip);
			if (zip.fmt.zip_flags & SWAP_LE16(0x0008)) {
				/* 0x0008 - streaming. [u]cmpsize can be reliably gotten
				 * only from Central Directory.
				 */
				zip.fmt.crc32 = cdf.fmt.crc32;
				zip.fmt.cmpsize = cdf.fmt.cmpsize;
				zip.fmt.ucmpsize = cdf.fmt.ucmpsize;
			}
			/* Check for UNIX/DOS/WIN directory */
			is_dir = cdf.fmt.external_attributes & 0x40000010;
// Seen in some zipfiles: central directory 9 byte extra field contains
// a subfield with ID 0x5455 and 5 data bytes, which is a Unix-style UTC mtime.
// Local header version:
//  u16 0x5455 ("UT")
//  u16 size (1 + 4 * n)
//  u8  flags: bit 0:mtime is present, bit 1:atime is present, bit 2:ctime is present
//  u32 mtime
//  u32 atime
//  u32 ctime
// Central header version:
//  u16 0x5455 ("UT")
//  u16 size (5 (or 1?))
//  u8  flags: bit 0:mtime is present, bit 1:atime is present, bit 2:ctime is present
//  u32 mtime (CDF does not store atime/ctime)
# else
			/* CDF has the same data as local header, no need to read the latter...
			 * ...not really. An archive was seen with cdf.extra_len == 6 but
			 * zip.extra_len == 0.
			 */
			memcpy(&zip.fmt.version,
				&cdf.fmt.version_needed, ZIP_HEADER_LEN);
			xlseek(zip_fd,
				SWAP_LE32(cdf.fmt.relative_offset_of_local_header) + 4 + ZIP_HEADER_LEN,
				SEEK_SET);
# endif
		}
#endif
		if (cdf_offset == BAD_CDF_OFFSET
			&& (zip.fmt.zip_flags & SWAP_LE16(0x0008))
			) {
			/* If it's a streaming zip, we _require_ CDF */
			bb_error_msg_and_die("can't find file table");
		}
		if (zip.fmt.zip_flags & SWAP_LE16(0x0001)) {
			/* 0x0001 - encrypted */
			bb_error_msg_and_die("zip flag %s is not supported",
					"1 (encryption)");
		}
		dbg("File cmpsize:0x%x extra_len:0x%x ucmpsize:0x%x",
			(unsigned)zip.fmt.cmpsize,
			(unsigned)zip.fmt.extra_len,
			(unsigned)zip.fmt.ucmpsize
		);

		/* Sets the file name and set the file sizes using ZIP64 if present */
		unzip_set_xstate(xstate, &zip);

		/* Handle multiple file switching */
		if ((!is_dir) && (xstate->dst_dir != NULL) && 
			(transformer_switch_file(xstate) < 0)) { 
				goto err;
		}

		n = unzip_extract(&zip, xstate);

		/* Only process the first file if not extracting to a dir */
		if (xstate->dst_dir == NULL)
			break;
	}

err:
	if (n > 0)
		return xstate->bytes_out;
	else if (n == -ENOSPC)
		return xstate->mem_output_size_max;
	else
		return n;
}
