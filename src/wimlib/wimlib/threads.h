#ifndef _WIMLIB_THREADS_H
#define _WIMLIB_THREADS_H

#include <stdbool.h>

#ifdef _WIN32

struct thread {
	void *win32_thread;
	void *(*thrproc)(void *);
	void *arg;
};

struct mutex { void *win32_crit; };
#define MUTEX_INITIALIZER { NULL }

struct condvar { void *win32_cond; };

#else /* _WIN32 */

#include <pthread.h>

struct thread { pthread_t pthread; };

struct mutex { pthread_mutex_t pthread_mutex; };
#define MUTEX_INITIALIZER { PTHREAD_MUTEX_INITIALIZER }

struct condvar { pthread_cond_t pthread_cond; };

#endif /* !_WIN32 */

bool thread_create(struct thread *t, void *(*thrproc)(void *), void *arg);
void thread_join(struct thread *t);
bool mutex_init(struct mutex *m);
void mutex_destroy(struct mutex *m);
void mutex_lock(struct mutex *m);
void mutex_unlock(struct mutex *m);
bool condvar_init(struct condvar *c);
void condvar_destroy(struct condvar *c);
void condvar_wait(struct condvar *c, struct mutex *m);
void condvar_signal(struct condvar *c);
void condvar_broadcast(struct condvar *c);

#endif /* _WIMLIB_THREADS_H */
