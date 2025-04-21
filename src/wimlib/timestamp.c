/*
 * timestamp.c
 *
 * Conversion between Windows NT timestamps and UNIX timestamps.
 */

/*
 * Copyright (C) 2012-2017 Eric Biggers
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

#include "wimlib.h" /* for struct wimlib_timespec */
#include "wimlib/timestamp.h"

/*
 * Timestamps in WIM files are Windows NT timestamps, or FILETIMEs: 64-bit
 * values storing the number of 100-nanosecond ticks since January 1, 1601.
 *
 * Note: UNIX timestamps are signed; Windows timestamps are not.  Negative UNIX
 * timestamps represent times before 1970-01-01.  When such a timestamp is
 * converted to a Windows timestamp, we can preserve the correct date provided
 * that it is not also before 1601-01-01.
 */

#define NANOSECONDS_PER_TICK	100
#define TICKS_PER_SECOND	(1000000000 / NANOSECONDS_PER_TICK)
#define TICKS_PER_MICROSECOND	(TICKS_PER_SECOND / 1000000)

/*
 * EPOCH_DISTANCE is the number of seconds separating the Windows NT and UNIX
 * epochs.  This is equal to ((1970-1601)*365+89)*24*60*60.  89 is the number
 * of leap years between 1970 and 1601.
 */
#define EPOCH_DISTANCE		11644473600

/* Windows NT timestamps to UNIX timestamps  */

time_t
wim_timestamp_to_time_t(u64 timestamp)
{
	return (timestamp / TICKS_PER_SECOND) - EPOCH_DISTANCE;
}

void
wim_timestamp_to_wimlib_timespec(u64 timestamp, struct wimlib_timespec *wts,
				 s32 *high_part_ret)
{
	s64 sec = (timestamp / TICKS_PER_SECOND) - EPOCH_DISTANCE;

	wts->tv_sec = sec;
	wts->tv_nsec = (timestamp % TICKS_PER_SECOND) * NANOSECONDS_PER_TICK;

	if (sizeof(wts->tv_sec) == 4)
		*high_part_ret = sec >> 32;
}

#ifdef _WIN32
static void __attribute__((unused))
check_sizeof_time_t(void)
{
	/* Windows builds should always be using 64-bit time_t now. */
	STATIC_ASSERT(sizeof(time_t) == 8);
}
#else
struct timeval
wim_timestamp_to_timeval(u64 timestamp)
{
	return (struct timeval) {
		.tv_sec = wim_timestamp_to_time_t(timestamp),
		.tv_usec = (timestamp % TICKS_PER_SECOND) / TICKS_PER_MICROSECOND,
	};
}

struct timespec
wim_timestamp_to_timespec(u64 timestamp)
{
	return (struct timespec) {
		.tv_sec = wim_timestamp_to_time_t(timestamp),
		.tv_nsec = (timestamp % TICKS_PER_SECOND) * NANOSECONDS_PER_TICK,
	};
}
#endif /* !_WIN32 */

/* UNIX timestamps to Windows NT timestamps  */

u64
time_t_to_wim_timestamp(time_t t)
{
	return ((u64)t + EPOCH_DISTANCE) * TICKS_PER_SECOND;
}

#ifndef _WIN32
u64
timeval_to_wim_timestamp(const struct timeval *tv)
{
	return time_t_to_wim_timestamp(tv->tv_sec) +
		(u32)tv->tv_usec * TICKS_PER_MICROSECOND;
}

u64
timespec_to_wim_timestamp(const struct timespec *ts)
{
	return time_t_to_wim_timestamp(ts->tv_sec) +
		(u32)ts->tv_nsec / NANOSECONDS_PER_TICK;
}

/* Retrieve the current time as a WIM timestamp.  */
u64
now_as_wim_timestamp(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return timeval_to_wim_timestamp(&tv);
}
#endif /* !_WIN32 */

/* Translate a WIM timestamp into a human-readable string.  */
void
wim_timestamp_to_str(u64 timestamp, tchar *buf, size_t len)
{
	struct tm tm = { 0 };
	time_t t = wim_timestamp_to_time_t(timestamp);

#ifdef _WIN32
	gmtime_s(&tm, &t);
#else
	gmtime_r(&t, &tm);
#endif
	tstrftime(buf, len, T("%a %b %d %H:%M:%S %Y UTC"), &tm);
}
