/*
 * timestamp.h
 *
 * Conversion between Windows NT timestamps and UNIX timestamps.
 */

#ifndef _WIMLIB_TIMESTAMP_H
#define _WIMLIB_TIMESTAMP_H

#ifdef HAVE_SYS_TIMES_H
#include <sys/time.h>
#endif
#include <time.h>

#include "wimlib/types.h"

struct wimlib_timespec;

time_t
wim_timestamp_to_time_t(u64 timestamp);

void
wim_timestamp_to_wimlib_timespec(u64 timestamp, struct wimlib_timespec *wts,
				 s32 *high_part_ret);

struct timeval
wim_timestamp_to_timeval(u64 timestamp);

struct timespec
wim_timestamp_to_timespec(u64 timestamp);

u64
time_t_to_wim_timestamp(time_t t);

u64
timeval_to_wim_timestamp(const struct timeval *tv);

u64
timespec_to_wim_timestamp(const struct timespec *ts);

u64
now_as_wim_timestamp(void);

void
wim_timestamp_to_str(u64 timestamp, tchar *buf, size_t len);

#endif /* _WIMLIB_TIMESTAMP_H */
