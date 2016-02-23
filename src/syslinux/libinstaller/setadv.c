/* ----------------------------------------------------------------------- *
 *
 *   Copyright 2007-2008 H. Peter Anvin - All Rights Reserved
 *   Copyright 2010 Intel Corporation; author: H. Peter Anvin
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 53 Temple Place Ste 330,
 *   Boston MA 02111-1307, USA; either version 2 of the License, or
 *   (at your option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/*
 * setadv.c
 *
 * (Over)write a data item in the auxiliary data vector.  To
 * delete an item, set its length to zero.
 *
 * Return 0 on success, -1 on error, and set errno.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include "syslxint.h"
#include "syslxcom.h"
#include "syslxfs.h"

unsigned char syslinux_adv[2 * ADV_SIZE];

#define ADV_MAGIC1	0x5a2d2fa5	/* Head signature */
#define ADV_MAGIC2	0xa3041767	/* Total checksum */
#define ADV_MAGIC3	0xdd28bf64	/* Tail signature */

static void cleanup_adv(unsigned char *advbuf)
{
    int i;
    uint32_t csum;

    /* Make sure both copies agree, and update the checksum */
    set_32((uint32_t *) advbuf, ADV_MAGIC1);

    csum = ADV_MAGIC2;
    for (i = 8; i < ADV_SIZE - 4; i += 4)
	csum -= get_32((uint32_t *) (advbuf + i));

    set_32((uint32_t *) (advbuf + 4), csum);
    set_32((uint32_t *) (advbuf + ADV_SIZE - 4), ADV_MAGIC3);

    memcpy(advbuf + ADV_SIZE, advbuf, ADV_SIZE);
}

int syslinux_setadv(int tag, size_t size, const void *data)
{
    uint8_t *p;
    size_t left;
    uint8_t advtmp[ADV_SIZE];

    if ((unsigned)tag - 1 > 254) {
	errno = EINVAL;
	return -1;		/* Impossible tag value */
    }

    if (size > 255) {
	errno = ENOSPC;		/* Max 255 bytes for a data item */
	return -1;
    }

    left = ADV_LEN;
    p = advtmp;
    memcpy(p, syslinux_adv + 2 * 4, left);	/* Make working copy */

    while (left >= 2) {
	uint8_t ptag = p[0];
	size_t plen = p[1] + 2;

	if (ptag == ADV_END)
	    break;

	if (ptag == tag) {
	    /* Found our tag.  Delete it. */

	    if (plen >= left) {
		/* Entire remainder is our tag */
		break;
	    }
	    memmove(p, p + plen, left - plen);
	} else {
	    /* Not our tag */
	    if (plen > left)
		break;		/* Corrupt tag (overrun) - overwrite it */

	    left -= plen;
	    p += plen;
	}
    }

    /* Now (p, left) reflects the position to write in and how much space
       we have for our data. */

    if (size) {
	if (left < size + 2) {
	    errno = ENOSPC;	/* Not enough space for data */
	    return -1;
	}

	*p++ = tag;
	*p++ = size;
	memcpy(p, data, size);
	p += size;
	left -= size + 2;
    }

    memset(p, 0, left);

    /* If we got here, everything went OK, commit the write */
    memcpy(syslinux_adv + 2 * 4, advtmp, ADV_LEN);
    cleanup_adv(syslinux_adv);

    return 0;
}

void syslinux_reset_adv(unsigned char *advbuf)
{
    /* Create an all-zero ADV */
    memset(advbuf + 2 * 4, 0, ADV_LEN);
    cleanup_adv(advbuf);
}

static int adv_consistent(const unsigned char *p)
{
    int i;
    uint32_t csum;

    if (get_32((uint32_t *) p) != ADV_MAGIC1 ||
	get_32((uint32_t *) (p + ADV_SIZE - 4)) != ADV_MAGIC3)
	return 0;

    csum = 0;
    for (i = 4; i < ADV_SIZE - 4; i += 4)
	csum += get_32((uint32_t *) (p + i));

    return csum == ADV_MAGIC2;
}

/*
 * Verify that an in-memory ADV is consistent, making the copies consistent.
 * If neither copy is OK, return -1 and call syslinux_reset_adv().
 */
int syslinux_validate_adv(unsigned char *advbuf)
{
    if (adv_consistent(advbuf + 0 * ADV_SIZE)) {
	memcpy(advbuf + ADV_SIZE, advbuf, ADV_SIZE);
	return 0;
    } else if (adv_consistent(advbuf + 1 * ADV_SIZE)) {
	memcpy(advbuf, advbuf + ADV_SIZE, ADV_SIZE);
	return 0;
    } else {
	syslinux_reset_adv(advbuf);
	return -1;
    }
}
