/*
 * unaligned.h - inline functions for unaligned memory accesses
 *
 * Copyright 2022 Eric Biggers
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _WIMLIB_UNALIGNED_H
#define _WIMLIB_UNALIGNED_H

#include <string.h>

#include "wimlib/compiler.h"
#include "wimlib/endianness.h"
#include "wimlib/types.h"

#define DEFINE_UNALIGNED_TYPE(type)				\
static forceinline type						\
load_##type##_unaligned(const void *p)				\
{								\
	type v;							\
	memcpy(&v, p, sizeof(v));				\
	return v;						\
}								\
								\
static forceinline void						\
store_##type##_unaligned(type v, void *p)			\
{								\
	memcpy(p, &v, sizeof(v));				\
}

DEFINE_UNALIGNED_TYPE(u16);
DEFINE_UNALIGNED_TYPE(u32);
DEFINE_UNALIGNED_TYPE(u64);
DEFINE_UNALIGNED_TYPE(le16);
DEFINE_UNALIGNED_TYPE(le32);
DEFINE_UNALIGNED_TYPE(le64);
DEFINE_UNALIGNED_TYPE(be16);
DEFINE_UNALIGNED_TYPE(be32);
DEFINE_UNALIGNED_TYPE(be64);
DEFINE_UNALIGNED_TYPE(size_t);
DEFINE_UNALIGNED_TYPE(machine_word_t);

#define load_word_unaligned	load_machine_word_t_unaligned
#define store_word_unaligned	store_machine_word_t_unaligned

static forceinline u16
get_unaligned_le16(const u8 *p)
{
	if (UNALIGNED_ACCESS_IS_FAST)
		return le16_to_cpu(load_le16_unaligned(p));
	else
		return ((u16)p[1] << 8) | p[0];
}

static forceinline u32
get_unaligned_le32(const u8 *p)
{
	if (UNALIGNED_ACCESS_IS_FAST)
		return le32_to_cpu(load_le32_unaligned(p));
	else
		return ((u32)p[3] << 24) | ((u32)p[2] << 16) |
			((u32)p[1] << 8) | p[0];
}

static forceinline u32
get_unaligned_be32(const u8 *p)
{
	if (UNALIGNED_ACCESS_IS_FAST)
		return be32_to_cpu(load_be32_unaligned(p));
	else
		return ((u32)p[0] << 24) | ((u32)p[1] << 16) |
			((u32)p[2] << 8) | p[3];
}

static forceinline void
put_unaligned_le16(u16 v, u8 *p)
{
	if (UNALIGNED_ACCESS_IS_FAST) {
		store_le16_unaligned(cpu_to_le16(v), p);
	} else {
		p[0] = (u8)(v >> 0);
		p[1] = (u8)(v >> 8);
	}
}

static forceinline void
put_unaligned_le32(u32 v, u8 *p)
{
	if (UNALIGNED_ACCESS_IS_FAST) {
		store_le32_unaligned(cpu_to_le32(v), p);
	} else {
		p[0] = (u8)(v >> 0);
		p[1] = (u8)(v >> 8);
		p[2] = (u8)(v >> 16);
		p[3] = (u8)(v >> 24);
	}
}

static forceinline void
put_unaligned_be32(u32 v, u8 *p)
{
	if (UNALIGNED_ACCESS_IS_FAST) {
		store_be32_unaligned(cpu_to_be32(v), p);
	} else {
		p[0] = (u8)(v >> 24);
		p[1] = (u8)(v >> 16);
		p[2] = (u8)(v >> 8);
		p[3] = (u8)(v >> 0);
	}
}

#endif /* _WIMLIB_UNALIGNED_H */
