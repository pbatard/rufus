/******************************************************************************
 * decompress_vtsi.c
 *
 * Copyright (c) 2021, longpanda <admin@ventoy.net>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 *
 */

#include "libbb.h"
#include "bb_archive.h"

/*
 * Structure of a Ventoy Sparse Image (VTSI) file:
 *
 +---------------------------------
 + sector 0 ~ sector N-1
 +     data area
 +---------------------------------
 + sector N ~ sector M - 1
 +     segment[0]
 +     segment[1]
 +     segment[2]
 +      .....
 +     (may be some align data (segment data aligned with 512) ...)
 +---------------------------------
 + sector M
 +     footer
 +---------------------------------
 *
 * All the integers are in little endian
 * The sector size is fixed 512 for ventoy image file.
 *
 */

#define VTSI_MAGIC 0x0000594F544E4556ULL	// "VENTOY\0\0"

#pragma pack(1)

typedef struct {
	uint64_t disk_start_sector;
	uint64_t sector_num;
	uint64_t data_offset;
} VTSI_SEGMENT;

typedef struct {
	uint64_t magic;
	uint16_t version_major;
	uint16_t version_minor;
	uint64_t disk_size;
	uint32_t disk_signature;
	uint32_t foot_chksum;

	uint32_t segment_num;
	uint32_t segment_chksum;
	uint64_t segment_offset;

	uint8_t  reserved[512 - 44];
} VTSI_FOOTER;

#pragma pack()

extern int __static_assert__[sizeof(VTSI_FOOTER) == 512 ? 1 : -1];

#define MAX_READ_BUF	(8 * 1024 * 1024)

static int check_vtsi_footer(VTSI_FOOTER* footer)
{
	int valid = 0;
	uint32_t i, oldsum, calcsum;

	if (footer->magic != VTSI_MAGIC)
		bb_error_msg_and_err("invalid vtsi magic 0x%llX", footer->magic);

	/* check footer checksum */
	oldsum = footer->foot_chksum;
	footer->foot_chksum = 0;
	for (i = 0, calcsum = 0; i < sizeof(VTSI_FOOTER); i++)
		calcsum += *((uint8_t*)footer + i);
	calcsum = ~calcsum;

	if (calcsum != oldsum)
		bb_error_msg_and_err("invalid vtsi footer chksum 0x%X 0x%X", calcsum, oldsum);

	/* check version (if the major doesn't match, we consider that a breaking change occurred) */
	if (footer->version_major != 1)
		bb_error_msg_and_err("unsupported vtsi version %d.%d", footer->version_major, footer->version_minor);

	valid = 1;
err:
	return valid;
}

static int check_vtsi_segment(VTSI_FOOTER* footer, VTSI_SEGMENT* segment)
{
	int valid = 0;
	uint32_t i, oldsum, calcsum;

	/* check segment checksum */
	oldsum = footer->segment_chksum;

	for (i = 0, calcsum = 0; i < sizeof(VTSI_SEGMENT) * footer->segment_num; i++)
		calcsum += *((uint8_t*)segment + i);
	calcsum = ~calcsum;

	if (calcsum != oldsum)
		bb_error_msg_and_err("invalid vtsi segment chksum 0x%X 0x%X", calcsum, oldsum);

	valid = 1;
err:
	return valid;
}

IF_DESKTOP(long long) int FAST_FUNC unpack_vtsi_stream(transformer_state_t* xstate)
{
	IF_DESKTOP(long long) int n = -EFAULT;
	long long tot = 0;
	off_t src_size;
	int src_fd = 0;
	size_t wsize = 0;
	ssize_t retval = 0;
	uint64_t seg = 0;
	int64_t datalen = 0;
	uint64_t phy_offset = 0;
	size_t max_buflen = MAX_READ_BUF;
	uint8_t* buf = NULL;
	VTSI_SEGMENT* segment = NULL;
	VTSI_SEGMENT* cur_seg = NULL;
	VTSI_FOOTER footer;

	if (xstate->dst_dir)
		bb_error_msg_and_err("decompress to dir is not supported");

	src_fd = xstate->src_fd;
	src_size = lseek(src_fd, 0, SEEK_END);
	lseek(src_fd, src_size - sizeof(VTSI_FOOTER), SEEK_SET);

	safe_read(src_fd, &footer, sizeof(footer));
	if (!check_vtsi_footer(&footer))
		goto err;

	if (xstate->mem_output_size_max == 512)
		max_buflen = 1024;

	segment = xmalloc(footer.segment_num * sizeof(VTSI_SEGMENT) + max_buflen);
	if (!segment)
		bb_error_msg_and_err("Failed to alloc segment buffer %u", footer.segment_num);

	buf = (uint8_t*)segment + footer.segment_num * sizeof(VTSI_SEGMENT);

	lseek(src_fd, footer.segment_offset, SEEK_SET);
	safe_read(src_fd, segment, footer.segment_num * sizeof(VTSI_SEGMENT));

	if (!check_vtsi_segment(&footer, segment))
		goto err;

	/* read data */
	lseek(src_fd, 0, SEEK_SET);
	for (seg = 0; seg < footer.segment_num; seg++) {
		cur_seg = segment + seg;
		datalen = (int64_t)cur_seg->sector_num * 512;
		phy_offset = cur_seg->disk_start_sector * 512;

		if (xstate->mem_output_size_max == 0 && xstate->dst_fd >= 0)
			lseek(xstate->dst_fd, phy_offset, SEEK_SET);

		while (datalen > 0) {
			wsize = MIN((size_t)datalen, max_buflen);
			safe_read(src_fd, buf, (unsigned int)wsize);

			retval = transformer_write(xstate, buf, wsize);
			if (retval != (ssize_t)wsize) {
				n = (retval == -ENOSPC) ? xstate->mem_output_size_max : -1;
				goto err;
			}

			tot += retval;
			datalen -= wsize;
		}
	}

	n = tot;

err:
	if (segment)
		free(segment);

	return n;
}
