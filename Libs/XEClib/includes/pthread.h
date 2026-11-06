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
#include <_xeneva.h>

#ifdef __cplusplus
XE_EXTERN {
#endif

	typedef struct {
		uint32_t id;
		void* data;
	} pthread_t;
	typedef unsigned int pthread_attr_t;

	typedef struct {
		int volatile atomic_lock;
		int volatile reads;
		int writerPid;
	} pthread_rwlock_t;

	XE_LIB int pthread_create(
		pthread_t * thread, pthread_attr_t * attr, void* (*start_routine)(void*), void* arg);
	XE_LIB void pthread_exit(void* value);
	XE_LIB int pthread_kill(pthread_t thread, int sig);

	XE_LIB int gettid();

	XE_LIB void pthread_cleanup_push(void (*routine)(void*), void* arg);
	XE_LIB void pthread_cleanup_pop(int execute);

	typedef int volatile pthread_mutex_t;
	typedef int pthread_mutexattr_t;

	XE_LIB int pthread_join(pthread_t thread, void** retval);

#define PTHREAD_MUTEX_INITIALIZER 0

	XE_LIB int pthread_mutex_lock(pthread_mutex_t * mutex);
	XE_LIB int pthread_mutex_trylock(pthread_mutex_t * mutex);
	XE_LIB int pthread_mutex_unlock(pthread_mutex_t * mutex);
	XE_LIB int pthread_mutex_init(pthread_mutex_t * mutex, const pthread_mutexattr_t* attr);
	XE_LIB int pthread_mutex_destroy(pthread_mutex_t * mutex);

	XE_LIB int pthread_attr_init(pthread_attr_t * attr);
	XE_LIB int pthread_attr_destroy(pthread_attr_t * attr);

	XE_LIB int pthread_rwlock_init(pthread_rwlock_t * lock, void* args);
	XE_LIB int pthread_rwlock_wrlock(pthread_rwlock_t * lock);
	XE_LIB int pthread_rwlock_rdlock(pthread_rwlock_t * lock);
	XE_LIB int pthread_rwlock_unlock(pthread_rwlock_t * lock);
	XE_LIB int pthread_rwlock_destroy(pthread_rwlock_t * lock);

#ifdef __cplusplus
}
#endif

#endif