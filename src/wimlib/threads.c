/*
 * threads.c - Thread, mutex, and condition variable support.  Wraps around
 *             pthreads or Windows native threads.
 */

/*
 * Copyright 2016-2023 Eric Biggers
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

#ifdef _WIN32
#  include "wimlib/win32_common.h"
#else
#  include <errno.h>
#  include <pthread.h>
#endif

#include "wimlib/assert.h"
#include "wimlib/error.h"
#include "wimlib/threads.h"
#include "wimlib/util.h"

#ifdef _WIN32

static DWORD WINAPI
win32_thrproc(LPVOID lpParameter)
{
	struct thread *t = (struct thread *)lpParameter;

	(*t->thrproc)(t->arg);
	return 0;
}

bool thread_create(struct thread *t, void *(*thrproc)(void *), void *arg)
{
	HANDLE h;

	t->thrproc = thrproc;
	t->arg = arg;
	h = CreateThread(NULL, 0, win32_thrproc, (LPVOID)t, 0, NULL);
	if (h == NULL) {
		win32_error(GetLastError(), L"Failed to create thread");
		return false;
	}
	t->win32_thread = (void *)h;
	return true;
}

void thread_join(struct thread *t)
{
	DWORD res = WaitForSingleObject((HANDLE)t->win32_thread, INFINITE);

	wimlib_assert(res == WAIT_OBJECT_0);
}

bool mutex_init(struct mutex *m)
{
	CRITICAL_SECTION *crit = MALLOC(sizeof(*crit));

	if (!crit)
		return false;
	InitializeCriticalSection(crit);
	m->win32_crit = crit;
	return true;
}

void mutex_destroy(struct mutex *m)
{
	DeleteCriticalSection(m->win32_crit);
	FREE(m->win32_crit);
	m->win32_crit = NULL;
}

void mutex_lock(struct mutex *m)
{
	CRITICAL_SECTION *crit = m->win32_crit;

	if (unlikely(!crit)) {
		CRITICAL_SECTION *old;

		crit = MALLOC(sizeof(*crit));
		wimlib_assert(crit != NULL);
		InitializeCriticalSection(crit);
		old = InterlockedCompareExchangePointer(&m->win32_crit, crit,
							NULL);
		if (old) {
			DeleteCriticalSection(crit);
			FREE(crit);
			crit = old;
		}
	}
	EnterCriticalSection(crit);
}

void mutex_unlock(struct mutex *m)
{
	LeaveCriticalSection(m->win32_crit);
}

bool condvar_init(struct condvar *c)
{
	CONDITION_VARIABLE *cond = MALLOC(sizeof(*cond));

	if (!cond)
		return false;
	InitializeConditionVariable(cond);
	c->win32_cond = cond;
	return true;
}

void condvar_destroy(struct condvar *c)
{
	FREE(c->win32_cond);
	c->win32_cond = NULL;
}

void condvar_wait(struct condvar *c, struct mutex *m)
{
	BOOL ok = SleepConditionVariableCS(c->win32_cond, m->win32_crit,
					   INFINITE);
	wimlib_assert(ok);
}

void condvar_signal(struct condvar *c)
{
	WakeConditionVariable(c->win32_cond);
}

void condvar_broadcast(struct condvar *c)
{
	WakeAllConditionVariable(c->win32_cond);
}

#else /* _WIN32 */

bool thread_create(struct thread *t, void *(*thrproc)(void *), void *arg)
{
	int err = pthread_create(&t->pthread, NULL, thrproc, arg);

	if (err) {
		errno = err;
		ERROR_WITH_ERRNO("Failed to create thread");
		return false;
	}
	return true;
}

void thread_join(struct thread *t)
{
	int err = pthread_join(t->pthread, NULL);

	wimlib_assert(err == 0);
}

bool mutex_init(struct mutex *m)
{
	int err = pthread_mutex_init(&m->pthread_mutex, NULL);

	if (err) {
		errno = err;
		ERROR_WITH_ERRNO("Failed to initialize mutex");
		return false;
	}
	return true;
}

void mutex_destroy(struct mutex *m)
{
	int err = pthread_mutex_destroy(&m->pthread_mutex);

	wimlib_assert(err == 0);
}

void mutex_lock(struct mutex *m)
{
	int err = pthread_mutex_lock(&m->pthread_mutex);

	wimlib_assert(err == 0);
}

void mutex_unlock(struct mutex *m)
{
	int err = pthread_mutex_unlock(&m->pthread_mutex);

	wimlib_assert(err == 0);
}

bool condvar_init(struct condvar *c)
{
	int err = pthread_cond_init(&c->pthread_cond, NULL);

	if (err) {
		errno = err;
		ERROR_WITH_ERRNO("Failed to initialize condition variable");
		return false;
	}
	return true;
}

void condvar_destroy(struct condvar *c)
{
	int err = pthread_cond_destroy(&c->pthread_cond);

	wimlib_assert(err == 0);
}

void condvar_wait(struct condvar *c, struct mutex *m)
{
	int err = pthread_cond_wait(&c->pthread_cond, &m->pthread_mutex);

	wimlib_assert(err == 0);
}

void condvar_signal(struct condvar *c)
{
	int err = pthread_cond_signal(&c->pthread_cond);

	wimlib_assert(err == 0);
}

void condvar_broadcast(struct condvar *c)
{
	int err = pthread_cond_broadcast(&c->pthread_cond);

	wimlib_assert(err == 0);
}

#endif /* !_WIN32 */
