/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2026, Manas Kamal Choudhury
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice, this
*    list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following disclaimer in the documentation
*    and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
**/

#include <pthread.h>
#include <sys/_keproc.h>
#include <sys/_ketime.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

/*
 * Native futex opcodes.  These match the kernel's AU_FUTEX_* values.
 */
#define XE_FUTEX_WAIT 0
#define XE_FUTEX_WAKE 1
#define XE_FUTEX_EAGAIN 11
#define XE_FUTEX_ETIMEDOUT 116

#define XE_NSEC_PER_SEC 1000000000LL

/* Native SYS_FUTEX takes a 64-bit timespec.  The PE target's long is only
 * 32 bits, so struct timespec from <time.h> is 8 bytes here and cannot be
 * handed to the kernel directly. */
struct xe_futex_timespec {
	int64_t tv_sec;
	int64_t tv_nsec;
};

/* ------------------------------------------------------------------ */
/* tiny userspace spinlock for the bookkeeping lists                  */
/* ------------------------------------------------------------------ */

static inline void xe_spin_lock(volatile int* lock) {
	while (__atomic_exchange_n(lock, 1, __ATOMIC_ACQUIRE)) {
		while (__atomic_load_n(lock, __ATOMIC_RELAXED))
			__asm__ __volatile__("yield" : : : "memory");
	}
}

static inline void xe_spin_unlock(volatile int* lock) {
	__atomic_store_n(lock, 0, __ATOMIC_RELEASE);
}

/* Relative timeout from an absolute CLOCK_REALTIME timespec. */
static int xe_abs_to_rel(const struct timespec* abs, struct xe_futex_timespec* rel) {
	if (!abs || abs->tv_nsec < 0 || abs->tv_nsec >= XE_NSEC_PER_SEC || abs->tv_sec < 0)
		return EINVAL;

	int64_t now_sec = 0;
	int64_t now_nsec = 0;
	_KeGetWalltime(&now_sec, &now_nsec);

	int64_t sec = (int64_t)abs->tv_sec - now_sec;
	int64_t nsec = (int64_t)abs->tv_nsec - now_nsec;
	if (nsec < 0) {
		nsec += XE_NSEC_PER_SEC;
		sec -= 1;
	}
	if (sec < 0 || (sec == 0 && nsec <= 0))
		return ETIMEDOUT;

	rel->tv_sec = sec;
	rel->tv_nsec = nsec;
	return 0;
}

/* ------------------------------------------------------------------ */
/* userspace thread records                                           */
/* ------------------------------------------------------------------ */

struct pthread_thread {
	uint32_t tid;
	void* (*start)(void*);
	void* arg;
	void* retval;
	volatile int refs;
	volatile int detached;
	volatile int finished;
	int is_pthread;
	void* keys[PTHREAD_KEYS_MAX];
	struct pthread_thread* next;
};

static volatile int g_thread_lock;
static struct pthread_thread* g_threads;

static void thread_unref(struct pthread_thread* t);

static void thread_register(struct pthread_thread* t) {
	xe_spin_lock(&g_thread_lock);
	t->next = g_threads;
	g_threads = t;
	xe_spin_unlock(&g_thread_lock);
}

static struct pthread_thread* thread_find(uint32_t tid) {
	struct pthread_thread* t = NULL;
	xe_spin_lock(&g_thread_lock);
	for (t = g_threads; t != NULL; t = t->next) {
		if (t->tid == tid)
			break;
	}
	xe_spin_unlock(&g_thread_lock);
	return t;
}

static struct pthread_thread* thread_get(uint32_t tid) {
	struct pthread_thread* t = NULL;
	xe_spin_lock(&g_thread_lock);
	for (t = g_threads; t != NULL; t = t->next) {
		if (t->tid == tid)
			break;
	}
	if (!t) {
		t = (struct pthread_thread*)calloc(1, sizeof(struct pthread_thread));
		if (t) {
			t->tid = tid;
			t->refs = 1; /* owned by the thread itself */
			t->is_pthread = 0;
			t->next = g_threads;
			g_threads = t;
		}
	}
	xe_spin_unlock(&g_thread_lock);
	return t;
}

static void thread_unref(struct pthread_thread* t) {
	if (!t)
		return;
	if (__atomic_sub_fetch(&t->refs, 1, __ATOMIC_ACQ_REL) != 0)
		return;

	xe_spin_lock(&g_thread_lock);
	struct pthread_thread** pp = &g_threads;
	while (*pp) {
		if (*pp == t) {
			*pp = t->next;
			break;
		}
		pp = &(*pp)->next;
	}
	xe_spin_unlock(&g_thread_lock);
	free(t);
}

/* ------------------------------------------------------------------ */
/* pthread keys                                                       */
/* ------------------------------------------------------------------ */

static volatile int g_key_lock;
static unsigned int g_key_next;
static void (*g_key_destructors[PTHREAD_KEYS_MAX])(void*);

int pthread_key_create(pthread_key_t* key, void (*destructor)(void*)) {
	if (!key)
		return EINVAL;

	xe_spin_lock(&g_key_lock);
	if (g_key_next >= PTHREAD_KEYS_MAX) {
		xe_spin_unlock(&g_key_lock);
		return EAGAIN;
	}
	unsigned int k = g_key_next++;
	g_key_destructors[k] = destructor;
	xe_spin_unlock(&g_key_lock);

	*key = k;
	return 0;
}

int pthread_key_delete(pthread_key_t key) {
	if (key >= PTHREAD_KEYS_MAX)
		return EINVAL;
	xe_spin_lock(&g_key_lock);
	g_key_destructors[key] = NULL;
	xe_spin_unlock(&g_key_lock);
	return 0;
}

void* pthread_getspecific(pthread_key_t key) {
	if (key >= PTHREAD_KEYS_MAX)
		return NULL;
	struct pthread_thread* t = thread_find((uint32_t)_KeGetThreadID());
	if (!t)
		return NULL;
	return t->keys[key];
}

int pthread_setspecific(pthread_key_t key, const void* value) {
	if (key >= PTHREAD_KEYS_MAX)
		return EINVAL;
	struct pthread_thread* t = thread_get((uint32_t)_KeGetThreadID());
	if (!t)
		return ENOMEM;
	t->keys[key] = (void*)value;
	return 0;
}

static void thread_run_key_destructors(struct pthread_thread* t) {
	for (int round = 0; round < PTHREAD_DESTRUCTOR_ITERATIONS; round++) {
		int ran = 0;
		for (int k = 0; k < PTHREAD_KEYS_MAX; k++) {
			void* value = t->keys[k];
			if (!value)
				continue;

			xe_spin_lock(&g_key_lock);
			void (*destructor)(void*) = g_key_destructors[k];
			xe_spin_unlock(&g_key_lock);

			t->keys[k] = NULL;
			if (destructor) {
				destructor(value);
				ran = 1;
			}
		}
		if (!ran)
			break;
	}
}

/* ------------------------------------------------------------------ */
/* thread lifecycle                                                   */
/* ------------------------------------------------------------------ */

static void* pthread_trampoline(void* arg) {
	struct pthread_thread* t = (struct pthread_thread*)arg;
	t->tid = (uint32_t)_KeGetThreadID();
	void* retval = t->start(t->arg);

	uint32_t tid = t->tid;
	thread_run_key_destructors(t);
	t->retval = retval;
	__atomic_store_n(&t->finished, 1, __ATOMIC_RELEASE);
	/* Drop the thread's own reference.  The creator's reference keeps a
	 * joinable record alive until pthread_join consumes it. */
	thread_unref(t);
	_KeExitSubThread((int)tid);
	for (;;)
		_KePauseThread();
	return NULL;
}

int pthread_create(pthread_t* thread,
				   const pthread_attr_t* attr,
				   void* (*start_routine)(void*),
				   void* arg) {
	if (!thread || !start_routine)
		return EINVAL;

	struct pthread_thread* t =
		(struct pthread_thread*)calloc(1, sizeof(struct pthread_thread));
	if (!t)
		return EAGAIN;

	t->start = start_routine;
	t->arg = arg;
	t->refs = 2; /* creator handle + running thread */
	t->is_pthread = 1;
	thread_register(t);

	int tid = _KeCreateThreadArg((void (*)())pthread_trampoline, (char*)"pthr", t);
	if (tid < 0) {
		t->refs = 1;
		thread_unref(t);
		return EAGAIN;
	}

	t->tid = (uint32_t)tid;
	thread->id = (uint32_t)tid;
	thread->data = t;

	if (attr && attr->detachstate == PTHREAD_CREATE_DETACHED) {
		t->detached = 1;
		thread_unref(t); /* creator gives up its reference */
	}
	return 0;
}

void pthread_exit(void* value) {
	uint32_t tid = (uint32_t)_KeGetThreadID();
	struct pthread_thread* t = thread_find(tid);
	if (!t)
		t = thread_get(tid);

	if (!t || !t->is_pthread) {
		/* No pthread record means this is the process main thread. */
		_KeProcessExit();
		for (;;)
			_KePauseThread();
	}

	thread_run_key_destructors(t);
	t->retval = value;
	__atomic_store_n(&t->finished, 1, __ATOMIC_RELEASE);
	thread_unref(t);
	_KeExitSubThread((int)tid);
	for (;;)
		_KePauseThread();
}

int pthread_join(pthread_t thread, void** retval) {
	struct pthread_thread* t = (struct pthread_thread*)thread.data;
	if (!t || t->tid != thread.id)
		return ESRCH;
	if (t->detached)
		return EINVAL;

	int r = _KeThreadWaitForTermination((int)thread.id);
	if (retval)
		*retval = t->retval;
	if (!__atomic_load_n(&t->finished, __ATOMIC_ACQUIRE) && r != 0)
		return ESRCH;

	thread_unref(t);
	return 0;
}

int pthread_detach(pthread_t thread) {
	struct pthread_thread* t = (struct pthread_thread*)thread.data;
	if (!t || t->tid != thread.id)
		return ESRCH;
	if (t->detached)
		return EINVAL;
	t->detached = 1;
	thread_unref(t);
	return 0;
}

pthread_t pthread_self(void) {
	pthread_t p;
	uint32_t tid = (uint32_t)_KeGetThreadID();
	p.id = tid;
	p.data = thread_get(tid);
	return p;
}

int pthread_equal(pthread_t t1, pthread_t t2) {
	return t1.id == t2.id;
}

int pthread_kill(pthread_t thread, int sig) {
	return _KeSendSignalToThread((int)thread.id, sig) == 0 ? 0 : ESRCH;
}

int gettid(void) {
	return (int)_KeGetThreadID();
}

/* ------------------------------------------------------------------ */
/* mutex                                                              */
/* ------------------------------------------------------------------ */

#define MUTEX_LOCK_WORD(m) ((uint32_t*)(void*)&(m)->lock)

static int mutex_try_acquire(pthread_mutex_t* m) {
	int tid = (int)_KeGetThreadID();
	int type = m->type;
	/* A tid can be 0 (the first user thread), so "owner == tid" is only
	 * meaningful while the lock is actually held. */
	int held = __atomic_load_n(&m->lock, __ATOMIC_ACQUIRE) != 0;

	if (held && type == PTHREAD_MUTEX_RECURSIVE && m->owner == tid) {
		m->count++;
		return 0;
	}
	if (held && type == PTHREAD_MUTEX_ERRORCHECK && m->owner == tid)
		return EDEADLK;

	int expected = 0;
	if (__atomic_compare_exchange_n(&m->lock, &expected, 1, false,
									__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		m->owner = tid;
		if (type == PTHREAD_MUTEX_RECURSIVE)
			m->count = 1;
		return 0;
	}
	return EBUSY;
}

/*
 * Reacquire after a futex wait.  Unlike the fast path, this exchanges the
 * word with 2 and leaves it at 2 when it wins: if another waiter is still
 * asleep, the next unlock must see 2 and wake it.  A plain CAS(0 -> 1) here
 * would erase that bit and strand every waiter but the woken one.
 */
static int mutex_acquire_contended(pthread_mutex_t* m) {
	int tid = (int)_KeGetThreadID();
	int type = m->type;
	int held = __atomic_load_n(&m->lock, __ATOMIC_ACQUIRE) != 0;

	if (held && type == PTHREAD_MUTEX_RECURSIVE && m->owner == tid) {
		m->count++;
		return 0;
	}
	if (held && type == PTHREAD_MUTEX_ERRORCHECK && m->owner == tid)
		return EDEADLK;

	if (__atomic_exchange_n(&m->lock, 2, __ATOMIC_ACQ_REL) == 0) {
		m->owner = tid;
		if (type == PTHREAD_MUTEX_RECURSIVE)
			m->count = 1;
		return 0;
	}
	return EBUSY;
}

static int mutex_lock_common(pthread_mutex_t* m, const struct timespec* abstime) {
	int r = mutex_try_acquire(m);
	if (r != EBUSY)
		return r;

	for (;;) {
		r = mutex_acquire_contended(m);
		if (r != EBUSY)
			return r;

		struct xe_futex_timespec rel;
		const void* timeout = NULL;
		if (abstime) {
			int e = xe_abs_to_rel(abstime, &rel);
			if (e == ETIMEDOUT)
				return ETIMEDOUT;
			if (e != 0)
				return e;
			timeout = &rel;
		}

		int fr = _KeFutex(MUTEX_LOCK_WORD(m), XE_FUTEX_WAIT, 2, timeout, NULL, 0);
		if (fr == -XE_FUTEX_ETIMEDOUT)
			return ETIMEDOUT;
		if (fr != 0 && fr != -XE_FUTEX_EAGAIN)
			return -fr;
	}
}

int pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr) {
	if (!mutex)
		return EINVAL;
	mutex->lock = 0;
	mutex->owner = 0;
	mutex->count = 0;
	mutex->type = attr ? attr->type : PTHREAD_MUTEX_NORMAL;
	return 0;
}

int pthread_mutex_destroy(pthread_mutex_t* mutex) {
	if (!mutex)
		return EINVAL;
	return __atomic_load_n(&mutex->lock, __ATOMIC_ACQUIRE) == 0 ? 0 : EBUSY;
}

int pthread_mutex_lock(pthread_mutex_t* mutex) {
	if (!mutex)
		return EINVAL;
	return mutex_lock_common(mutex, NULL);
}

int pthread_mutex_timedlock(pthread_mutex_t* mutex, const struct timespec* abstime) {
	if (!mutex || !abstime)
		return EINVAL;
	return mutex_lock_common(mutex, abstime);
}

int pthread_mutex_trylock(pthread_mutex_t* mutex) {
	if (!mutex)
		return EINVAL;
	return mutex_try_acquire(mutex);
}

int pthread_mutex_unlock(pthread_mutex_t* mutex) {
	if (!mutex)
		return EINVAL;

	int tid = (int)_KeGetThreadID();
	if (mutex->type == PTHREAD_MUTEX_RECURSIVE) {
		if (mutex->owner != tid)
			return EPERM;
		if (mutex->count > 1) {
			mutex->count--;
			return 0;
		}
	} else if (mutex->type == PTHREAD_MUTEX_ERRORCHECK) {
		if (mutex->owner != tid)
			return EPERM;
	}

	mutex->owner = 0;
	mutex->count = 0;
	int old = __atomic_exchange_n(&mutex->lock, 0, __ATOMIC_ACQ_REL);
	if (old == 2)
		_KeFutex(MUTEX_LOCK_WORD(mutex), XE_FUTEX_WAKE, 1, NULL, NULL, 0);
	return 0;
}

int pthread_mutexattr_init(pthread_mutexattr_t* attr) {
	if (!attr)
		return EINVAL;
	attr->type = PTHREAD_MUTEX_NORMAL;
	return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t* attr) {
	(void)attr;
	return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t* attr, int type) {
	if (!attr)
		return EINVAL;
	if (type != PTHREAD_MUTEX_NORMAL && type != PTHREAD_MUTEX_RECURSIVE &&
		type != PTHREAD_MUTEX_ERRORCHECK)
		return EINVAL;
	attr->type = type;
	return 0;
}

int pthread_mutexattr_gettype(const pthread_mutexattr_t* attr, int* type) {
	if (!attr || !type)
		return EINVAL;
	*type = attr->type;
	return 0;
}

/* ------------------------------------------------------------------ */
/* condition variables                                                */
/* ------------------------------------------------------------------ */

static void cond_lock(pthread_cond_t* c) {
	xe_spin_lock(&c->lock);
}

static void cond_unlock(pthread_cond_t* c) {
	xe_spin_unlock(&c->lock);
}

static void cond_enqueue(pthread_cond_t* c, pthread_cond_waiter* w) {
	w->next = NULL;
	if (c->tail)
		c->tail->next = w;
	else
		c->head = w;
	c->tail = w;
}

static void cond_remove(pthread_cond_t* c, pthread_cond_waiter* w) {
	pthread_cond_waiter* prev = NULL;
	for (pthread_cond_waiter* p = c->head; p != NULL; p = p->next) {
		if (p == w) {
			if (prev)
				prev->next = p->next;
			else
				c->head = p->next;
			if (c->tail == p)
				c->tail = prev;
			p->next = NULL;
			return;
		}
		prev = p;
	}
}

int pthread_cond_init(pthread_cond_t* cond, const pthread_condattr_t* attr) {
	(void)attr;
	if (!cond)
		return EINVAL;
	cond->lock = 0;
	cond->head = NULL;
	cond->tail = NULL;
	return 0;
}

int pthread_cond_destroy(pthread_cond_t* cond) {
	if (!cond)
		return EINVAL;
	cond_lock(cond);
	int busy = cond->head != NULL;
	cond_unlock(cond);
	return busy ? EBUSY : 0;
}

int pthread_cond_signal(pthread_cond_t* cond) {
	if (!cond)
		return EINVAL;

	pthread_cond_waiter* w = NULL;
	cond_lock(cond);
	w = cond->head;
	if (w) {
		cond->head = w->next;
		if (!cond->head)
			cond->tail = NULL;
		w->next = NULL;
		__atomic_store_n(&w->word, 1, __ATOMIC_RELEASE);
	}
	cond_unlock(cond);

	if (w)
		_KeFutex((uint32_t*)&w->word, XE_FUTEX_WAKE, 1, NULL, NULL, 0);
	return 0;
}

int pthread_cond_broadcast(pthread_cond_t* cond) {
	if (!cond)
		return EINVAL;

	cond_lock(cond);
	pthread_cond_waiter* list = cond->head;
	cond->head = NULL;
	cond->tail = NULL;
	for (pthread_cond_waiter* w = list; w != NULL; w = w->next)
		__atomic_store_n(&w->word, 1, __ATOMIC_RELEASE);
	cond_unlock(cond);

	while (list) {
		pthread_cond_waiter* next = list->next;
		_KeFutex((uint32_t*)&list->word, XE_FUTEX_WAKE, 1, NULL, NULL, 0);
		list = next;
	}
	return 0;
}

static int cond_wait_common(pthread_cond_t* cond, pthread_mutex_t* mutex,
							const struct timespec* abstime, int timed) {
	if (!cond || !mutex)
		return EINVAL;

	pthread_cond_waiter w;
	w.next = NULL;
	w.word = 0;

	cond_lock(cond);
	cond_enqueue(cond, &w);
	cond_unlock(cond);

	int r = pthread_mutex_unlock(mutex);
	if (r) {
		cond_lock(cond);
		cond_remove(cond, &w);
		cond_unlock(cond);
		return r;
	}

	/* Recompute the relative deadline on every retry: a spurious wake must
	 * not extend the wait, and EAGAIN means the signal already set the word
	 * (or the kernel rejected the value) and only a recheck is needed. */
	for (;;) {
		if (__atomic_load_n(&w.word, __ATOMIC_ACQUIRE) != 0)
			break;

		struct xe_futex_timespec rel;
		const void* timeout = NULL;
		if (timed) {
			int e = xe_abs_to_rel(abstime, &rel);
			if (e == ETIMEDOUT)
				break;
			if (e != 0) {
				cond_lock(cond);
				cond_remove(cond, &w);
				cond_unlock(cond);
				pthread_mutex_lock(mutex);
				return e;
			}
			timeout = &rel;
		}

		int fr = _KeFutex((uint32_t*)&w.word, XE_FUTEX_WAIT, 0, timeout, NULL, 0);
		if (fr == -XE_FUTEX_ETIMEDOUT)
			break;
		if (fr != 0 && fr != -XE_FUTEX_EAGAIN) {
			cond_lock(cond);
			cond_remove(cond, &w);
			cond_unlock(cond);
			pthread_mutex_lock(mutex);
			return -fr;
		}
	}

	cond_lock(cond);
	cond_remove(cond, &w);
	cond_unlock(cond);

	pthread_mutex_lock(mutex);

	return (timed && __atomic_load_n(&w.word, __ATOMIC_ACQUIRE) == 0) ? ETIMEDOUT : 0;
}

int pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex) {
	return cond_wait_common(cond, mutex, NULL, 0);
}

int pthread_cond_timedwait(pthread_cond_t* cond, pthread_mutex_t* mutex,
						   const struct timespec* abstime) {
	if (!abstime)
		return EINVAL;
	return cond_wait_common(cond, mutex, abstime, 1);
}

int pthread_condattr_init(pthread_condattr_t* attr) {
	if (!attr)
		return EINVAL;
	attr->pshared = 0;
	return 0;
}

int pthread_condattr_destroy(pthread_condattr_t* attr) {
	(void)attr;
	return 0;
}

/* ------------------------------------------------------------------ */
/* attributes                                                         */
/* ------------------------------------------------------------------ */

int pthread_attr_init(pthread_attr_t* attr) {
	if (!attr)
		return EINVAL;
	attr->stack_size = 512 * 1024;
	attr->detachstate = PTHREAD_CREATE_JOINABLE;
	return 0;
}

int pthread_attr_destroy(pthread_attr_t* attr) {
	(void)attr;
	return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t* attr, int state) {
	if (!attr)
		return EINVAL;
	if (state != PTHREAD_CREATE_JOINABLE && state != PTHREAD_CREATE_DETACHED)
		return EINVAL;
	attr->detachstate = state;
	return 0;
}

int pthread_attr_getdetachstate(const pthread_attr_t* attr, int* state) {
	if (!attr || !state)
		return EINVAL;
	*state = attr->detachstate;
	return 0;
}

int pthread_attr_setstacksize(pthread_attr_t* attr, size_t size) {
	if (!attr || size < PTHREAD_STACK_MIN)
		return EINVAL;
	attr->stack_size = size; /* the kernel currently uses its own stack size */
	return 0;
}

/* ------------------------------------------------------------------ */
/* read/write locks                                                   */
/* ------------------------------------------------------------------ */

#define RWLOCK_WORD(l) ((uint32_t*)(void*)&(l)->state)

static int rwlock_rdlock_common(pthread_rwlock_t* lock, int try_only) {
	for (;;) {
		int s = __atomic_load_n(&lock->state, __ATOMIC_ACQUIRE);
		if (s >= 0) {
			int expected = s;
			if (__atomic_compare_exchange_n(&lock->state, &expected, s + 1, false,
											__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
				return 0;
			if (try_only)
				return EBUSY;
			continue;
		}
		if (try_only)
			return EBUSY;
		_KeFutex(RWLOCK_WORD(lock), XE_FUTEX_WAIT, (uint32_t)s, NULL, NULL, 0);
	}
}

int pthread_rwlock_init(pthread_rwlock_t* lock, const pthread_rwlockattr_t* attr) {
	(void)attr;
	if (!lock)
		return EINVAL;
	lock->state = 0;
	lock->writers = 0;
	lock->owner = 0;
	return 0;
}

int pthread_rwlock_destroy(pthread_rwlock_t* lock) {
	if (!lock)
		return EINVAL;
	if (__atomic_load_n(&lock->state, __ATOMIC_ACQUIRE) != 0)
		return EBUSY;
	return __atomic_load_n(&lock->writers, __ATOMIC_ACQUIRE) == 0 ? 0 : EBUSY;
}

int pthread_rwlock_rdlock(pthread_rwlock_t* lock) {
	if (!lock)
		return EINVAL;
	return rwlock_rdlock_common(lock, 0);
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t* lock) {
	if (!lock)
		return EINVAL;
	return rwlock_rdlock_common(lock, 1);
}

int pthread_rwlock_wrlock(pthread_rwlock_t* lock) {
	if (!lock)
		return EINVAL;

	int tid = (int)_KeGetThreadID();
	__atomic_fetch_add(&lock->writers, 1, __ATOMIC_ACQ_REL);
	for (;;) {
		int expected = 0;
		if (__atomic_compare_exchange_n(&lock->state, &expected, -1, false,
										__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
			lock->owner = tid;
			__atomic_fetch_sub(&lock->writers, 1, __ATOMIC_ACQ_REL);
			return 0;
		}
		_KeFutex(RWLOCK_WORD(lock), XE_FUTEX_WAIT, (uint32_t)expected, NULL, NULL, 0);
	}
}

int pthread_rwlock_trywrlock(pthread_rwlock_t* lock) {
	if (!lock)
		return EINVAL;
	if (__atomic_load_n(&lock->writers, __ATOMIC_RELAXED) != 0)
		return EBUSY;
	int expected = 0;
	if (!__atomic_compare_exchange_n(&lock->state, &expected, -1, false,
									 __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return EBUSY;
	lock->owner = (int)_KeGetThreadID();
	return 0;
}

int pthread_rwlock_unlock(pthread_rwlock_t* lock) {
	if (!lock)
		return EINVAL;

	int s = __atomic_load_n(&lock->state, __ATOMIC_ACQUIRE);
	if (s == -1) {
		if (lock->owner != (int)_KeGetThreadID())
			return EPERM;
		lock->owner = 0;
		__atomic_store_n(&lock->state, 0, __ATOMIC_RELEASE);
		_KeFutex(RWLOCK_WORD(lock), XE_FUTEX_WAKE, 0x7fffffff, NULL, NULL, 0);
		return 0;
	}
	if (s <= 0)
		return EPERM;

	int ns = __atomic_sub_fetch(&lock->state, 1, __ATOMIC_ACQ_REL);
	if (ns == 0 && __atomic_load_n(&lock->writers, __ATOMIC_RELAXED) > 0)
		_KeFutex(RWLOCK_WORD(lock), XE_FUTEX_WAKE, 1, NULL, NULL, 0);
	return 0;
}

int pthread_rwlockattr_init(pthread_rwlockattr_t* attr) {
	if (!attr)
		return EINVAL;
	attr->pshared = 0;
	return 0;
}

int pthread_rwlockattr_destroy(pthread_rwlockattr_t* attr) {
	(void)attr;
	return 0;
}

/* ------------------------------------------------------------------ */
/* once                                                               */
/* ------------------------------------------------------------------ */

int pthread_once(pthread_once_t* once_control, void (*init_routine)(void)) {
	if (!once_control || !init_routine)
		return EINVAL;

	for (;;) {
		int state = __atomic_load_n(once_control, __ATOMIC_ACQUIRE);
		if (state == 2)
			return 0;
		if (state == 0) {
			int expected = 0;
			if (__atomic_compare_exchange_n(once_control, &expected, 1, false,
											__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
				init_routine();
				__atomic_store_n(once_control, 2, __ATOMIC_RELEASE);
				_KeFutex((uint32_t*)once_control, XE_FUTEX_WAKE, 0x7fffffff, NULL, NULL, 0);
				return 0;
			}
			continue;
		}
		_KeFutex((uint32_t*)once_control, XE_FUTEX_WAIT, 1, NULL, NULL, 0);
	}
}

/* ------------------------------------------------------------------ */
/* cancellation stubs (no asynchronous cancellation in Xeneva yet)    */
/* ------------------------------------------------------------------ */

void pthread_cleanup_push(void (*routine)(void*), void* arg) {
	(void)routine;
	(void)arg;
}

void pthread_cleanup_pop(int execute) {
	(void)execute;
}
