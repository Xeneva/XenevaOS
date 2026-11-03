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

#ifndef __PTHREAD_H__
#define __PTHREAD_H__

#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include <_xeneva.h>

#ifdef __cplusplus
XE_EXTERN {
#endif

	/*
	 * Threads are native Xeneva user threads: the kernel creates them,
	 * waits for their termination and reaps them.  The pthread handle adds
	 * the userspace bookkeeping the raw syscalls do not carry (join retval,
	 * detached state and TLS keys).
	 */
	typedef struct {
		uint32_t id;
		void* data;
	} pthread_t;

	typedef struct {
		size_t stack_size;
		int detachstate;
	} pthread_attr_t;

#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1

#define PTHREAD_STACK_MIN 16384

	/*
	 * Mutexes live in userspace.  lock is the futex word:
	 *   0 free, 1 locked without waiters, 2 locked with waiters.
	 */
	typedef struct {
		volatile int lock;
		volatile int owner;
		unsigned int count;
		int type;
	} pthread_mutex_t;

	typedef struct {
		int type;
	} pthread_mutexattr_t;

#define PTHREAD_MUTEX_NORMAL 0
#define PTHREAD_MUTEX_RECURSIVE 1
#define PTHREAD_MUTEX_ERRORCHECK 2
#define PTHREAD_MUTEX_DEFAULT PTHREAD_MUTEX_NORMAL

#define PTHREAD_MUTEX_INITIALIZER { 0, 0, 0, PTHREAD_MUTEX_NORMAL }

	/*
	 * Each waiter owns a private futex word, so one signal wakes exactly
	 * one waiter instead of every waiter observing a shared sequence
	 * change and returning spuriously.
	 */
	typedef struct pthread_cond_waiter {
		struct pthread_cond_waiter* next;
		uint32_t word;
	} pthread_cond_waiter;

	typedef struct {
		volatile int lock;
		pthread_cond_waiter* head;
		pthread_cond_waiter* tail;
	} pthread_cond_t;

	typedef struct {
		int pshared;
	} pthread_condattr_t;

#define PTHREAD_COND_INITIALIZER { 0, 0, 0 }

	/*
	 * state > 0 is the reader count, -1 is a writer, 0 is free.
	 */
	typedef struct {
		volatile int state;
		volatile int writers;
		int owner;
	} pthread_rwlock_t;

	typedef struct {
		int pshared;
	} pthread_rwlockattr_t;

#define PTHREAD_RWLOCK_INITIALIZER { 0, 0, 0 }

	typedef int pthread_once_t;

#define PTHREAD_ONCE_INIT 0

	typedef unsigned int pthread_key_t;

#define PTHREAD_KEYS_MAX 128
#define PTHREAD_DESTRUCTOR_ITERATIONS 4

	XE_LIB int pthread_create(
		pthread_t* thread, const pthread_attr_t* attr, void* (*start_routine)(void*), void* arg);
	XE_LIB void pthread_exit(void* value);
	XE_LIB int pthread_join(pthread_t thread, void** retval);
	XE_LIB int pthread_detach(pthread_t thread);
	XE_LIB pthread_t pthread_self(void);
	XE_LIB int pthread_equal(pthread_t t1, pthread_t t2);
	XE_LIB int pthread_kill(pthread_t thread, int sig);

	XE_LIB int gettid(void);

	XE_LIB void pthread_cleanup_push(void (*routine)(void*), void* arg);
	XE_LIB void pthread_cleanup_pop(int execute);

	XE_LIB int pthread_mutex_lock(pthread_mutex_t* mutex);
	XE_LIB int pthread_mutex_trylock(pthread_mutex_t* mutex);
	XE_LIB int pthread_mutex_timedlock(pthread_mutex_t* mutex, const struct timespec* abstime);
	XE_LIB int pthread_mutex_unlock(pthread_mutex_t* mutex);
	XE_LIB int pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr);
	XE_LIB int pthread_mutex_destroy(pthread_mutex_t* mutex);

	XE_LIB int pthread_mutexattr_init(pthread_mutexattr_t* attr);
	XE_LIB int pthread_mutexattr_destroy(pthread_mutexattr_t* attr);
	XE_LIB int pthread_mutexattr_settype(pthread_mutexattr_t* attr, int type);
	XE_LIB int pthread_mutexattr_gettype(const pthread_mutexattr_t* attr, int* type);

	XE_LIB int pthread_cond_init(pthread_cond_t* cond, const pthread_condattr_t* attr);
	XE_LIB int pthread_cond_destroy(pthread_cond_t* cond);
	XE_LIB int pthread_cond_signal(pthread_cond_t* cond);
	XE_LIB int pthread_cond_broadcast(pthread_cond_t* cond);
	XE_LIB int pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex);
	XE_LIB int pthread_cond_timedwait(pthread_cond_t* cond, pthread_mutex_t* mutex,
									  const struct timespec* abstime);

	XE_LIB int pthread_condattr_init(pthread_condattr_t* attr);
	XE_LIB int pthread_condattr_destroy(pthread_condattr_t* attr);

	XE_LIB int pthread_attr_init(pthread_attr_t* attr);
	XE_LIB int pthread_attr_destroy(pthread_attr_t* attr);
	XE_LIB int pthread_attr_setdetachstate(pthread_attr_t* attr, int state);
	XE_LIB int pthread_attr_getdetachstate(const pthread_attr_t* attr, int* state);
	XE_LIB int pthread_attr_setstacksize(pthread_attr_t* attr, size_t size);

	XE_LIB int pthread_rwlock_init(pthread_rwlock_t* lock, const pthread_rwlockattr_t* attr);
	XE_LIB int pthread_rwlock_destroy(pthread_rwlock_t* lock);
	XE_LIB int pthread_rwlock_rdlock(pthread_rwlock_t* lock);
	XE_LIB int pthread_rwlock_tryrdlock(pthread_rwlock_t* lock);
	XE_LIB int pthread_rwlock_wrlock(pthread_rwlock_t* lock);
	XE_LIB int pthread_rwlock_trywrlock(pthread_rwlock_t* lock);
	XE_LIB int pthread_rwlock_unlock(pthread_rwlock_t* lock);
	XE_LIB int pthread_rwlockattr_init(pthread_rwlockattr_t* attr);
	XE_LIB int pthread_rwlockattr_destroy(pthread_rwlockattr_t* attr);

	XE_LIB int pthread_once(pthread_once_t* once_control, void (*init_routine)(void));

	XE_LIB int pthread_key_create(pthread_key_t* key, void (*destructor)(void*));
	XE_LIB int pthread_key_delete(pthread_key_t key);
	XE_LIB void* pthread_getspecific(pthread_key_t key);
	XE_LIB int pthread_setspecific(pthread_key_t key, const void* value);

#ifdef __cplusplus
}
#endif

#endif
