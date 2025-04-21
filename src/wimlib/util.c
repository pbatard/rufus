/*
 * util.c - utility functions
 */

/*
 * Copyright 2012-2023 Eric Biggers
 *
 * This file is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option) any
 * later version.
 *
 * This file is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this file; if not, see https://www.gnu.org/licenses/.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_SYS_SYSCTL_H
#  include <sys/types.h>
#  include <sys/sysctl.h>
#endif
#ifdef HAVE_SYS_SYSCALL_H
#  include <sys/syscall.h>
#endif
#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#include "wimlib.h"
#include "wimlib/assert.h"
#include "wimlib/error.h"
#include "wimlib/timestamp.h"
#include "wimlib/util.h"

/*******************
 * Memory allocation
 *******************/

static void *(*wimlib_malloc_func) (size_t)	    = malloc;
static void  (*wimlib_free_func)   (void *)	    = free;
static void *(*wimlib_realloc_func)(void *, size_t) = realloc;

void *
wimlib_malloc(size_t size)
{
	void *ptr;

retry:
	ptr = (*wimlib_malloc_func)(size);
	if (unlikely(!ptr)) {
		if (size == 0) {
			size = 1;
			goto retry;
		}
	}
	return ptr;
}

void
wimlib_free_memory(void *ptr)
{
	(*wimlib_free_func)(ptr);
}

void *
wimlib_realloc(void *ptr, size_t size)
{
	if (size == 0)
		size = 1;
	return (*wimlib_realloc_func)(ptr, size);
}

void *
wimlib_calloc(size_t nmemb, size_t size)
{
	size_t total_size = nmemb * size;
	void *p;

	if (size != 0 && nmemb > SIZE_MAX / size) {
		errno = ENOMEM;
		return NULL;
	}

	p = MALLOC(total_size);
	if (p)
		p = memset(p, 0, total_size);
	return p;
}

char *
wimlib_strdup(const char *str)
{
	return memdup(str, strlen(str) + 1);
}

#ifdef _WIN32
wchar_t *
wimlib_wcsdup(const wchar_t *str)
{
	return memdup(str, (wcslen(str) + 1) * sizeof(wchar_t));
}
#endif

void *
wimlib_aligned_malloc(size_t size, size_t alignment)
{
	wimlib_assert(is_power_of_2(alignment));

	void *ptr = MALLOC(sizeof(void *) + alignment - 1 + size);
	if (ptr) {
		void *orig_ptr = ptr;
		ptr = (void *)ALIGN((uintptr_t)ptr + sizeof(void *), alignment);
		((void **)ptr)[-1] = orig_ptr;
	}
	return ptr;
}

void
wimlib_aligned_free(void *ptr)
{
	if (ptr)
		FREE(((void **)ptr)[-1]);
}

void *
memdup(const void *mem, size_t size)
{
	void *ptr = MALLOC(size);
	if (ptr)
		ptr = memcpy(ptr, mem, size);
	return ptr;
}

/* API function documented in wimlib.h  */
WIMLIBAPI int
wimlib_set_memory_allocator(void *(*malloc_func)(size_t),
			    void (*free_func)(void *),
			    void *(*realloc_func)(void *, size_t))
{
	wimlib_malloc_func  = malloc_func  ? malloc_func  : malloc;
	wimlib_free_func    = free_func    ? free_func    : free;
	wimlib_realloc_func = realloc_func ? realloc_func : realloc;
	return 0;
}

/*******************
 * String utilities
 *******************/

#ifndef HAVE_MEMPCPY
void *mempcpy(void *dst, const void *src, size_t n)
{
	void* ret = memcpy(dst, src, n);
	return ret != NULL ? _PTR(ret + n) : NULL;
}
#endif

/**************************
 * Random number generation
 **************************/

#ifndef _WIN32
/*
 * Generate @n cryptographically secure random bytes (thread-safe)
 *
 * This is the UNIX version.  It uses the Linux getrandom() system call if
 * available; otherwise, it falls back to reading from /dev/urandom.
 */
void
get_random_bytes(void *p, size_t n)
{
	if (n == 0)
		return;
#ifdef __NR_getrandom
	static bool getrandom_unavailable;

	if (getrandom_unavailable)
		goto try_dev_urandom;
	do {
		int res = syscall(__NR_getrandom, p, n, 0);
		if (unlikely(res < 0)) {
			if (errno == ENOSYS) {
				getrandom_unavailable = true;
				goto try_dev_urandom;
			}
			if (errno == EINTR)
				continue;
			ERROR_WITH_ERRNO("getrandom() failed");
			wimlib_assert(0);
			res = 0;
		}
		p += res;
		n -= res;
	} while (n != 0);
	return;

try_dev_urandom:
	;
#endif /* __NR_getrandom */
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0) {
		ERROR_WITH_ERRNO("Unable to open /dev/urandom");
		wimlib_assert(0);
	}
	do {
		int res = read(fd, p, min(n, INT_MAX));
		if (unlikely(res < 0)) {
			if (errno == EINTR)
				continue;
			ERROR_WITH_ERRNO("Error reading from /dev/urandom");
			wimlib_assert(0);
			res = 0;
		}
		p += res;
		n -= res;
	} while (n != 0);
	close(fd);
}
#endif /* !_WIN32 */

/*
 * Generate @n cryptographically secure random alphanumeric characters
 * (thread-safe)
 *
 * This is implemented on top of get_random_bytes().  For efficiency the calls
 * to get_random_bytes() are batched.
 */
void
get_random_alnum_chars(tchar *p, size_t n)
{
	u32 r[64];
	int r_idx = 0;
	int r_end = 0;

	for (; n != 0; p++, n--) {
		tchar x;

		if (r_idx >= r_end) {
			r_idx = 0;
			r_end = min(n, ARRAY_LEN(r));
			get_random_bytes(r, r_end * sizeof(r[0]));
		}

		STATIC_ASSERT(sizeof(r[0]) == sizeof(u32));
		while (unlikely(r[r_idx] >= UINT32_MAX - (UINT32_MAX % 62)))
			get_random_bytes(&r[r_idx], sizeof(r[0]));

		x = r[r_idx++] % 62;

		if (x < 26)
			*p = 'a' + x;
		else if (x < 52)
			*p = 'A' + x - 26;
		else
			*p = '0' + x - 52;
	}
}

/************************
 * System information
 ************************/

#ifndef _WIN32
unsigned
get_available_cpus(void)
{
	long n = sysconf(_SC_NPROCESSORS_ONLN);
	if (n < 1 || n >= UINT_MAX) {
		WARNING("Failed to determine number of processors; assuming 1.");
		return 1;
	}
	return n;
}
#endif /* !_WIN32 */

#ifndef _WIN32
u64
get_available_memory(void)
{
#if defined(_SC_PAGESIZE) && defined(_SC_PHYS_PAGES)
	long page_size = sysconf(_SC_PAGESIZE);
	long num_pages = sysconf(_SC_PHYS_PAGES);
	if (page_size <= 0 || num_pages <= 0)
		goto default_size;
	return ((u64)page_size * (u64)num_pages);
#else
	int mib[2] = {CTL_HW, HW_MEMSIZE};
	u64 memsize;
	size_t len = sizeof(memsize);
	if (sysctl(mib, ARRAY_LEN(mib), &memsize, &len, NULL, 0) < 0 || len != 8)
		goto default_size;
	return memsize;
#endif

default_size:
	WARNING("Failed to determine available memory; assuming 1 GiB");
	return (u64)1 << 30;
}
#endif /* !_WIN32 */
