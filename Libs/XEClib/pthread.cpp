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
#include <stdint.h>

extern "C" void _PthreadSetParam(uint64_t val);

int pthread_create(pthread_t* thread,
				   pthread_attr_t* attr,
				   void* (*start_routine)(void*),
				   void* arg) {
	_XEThreadEntry ent = (_XEThreadEntry)start_routine;

	/**
	 * TODO: Kernel doesn't have error check on thread creation
	 * whether it succeed or failed, so it gurantees for success
	 * in thread creation so no error returning is branched :)
	 */
	_PthreadSetParam((uint64_t)arg);
	int tid = _KeCreateThread(ent, "pthr");
	thread->data = NULL;
	thread->id = tid;
	_KePrint("pthread id : %d \r\n", thread->id);
	return 0; // haha, always success
}

void pthread_exit(void* val) {
	int thread_id = _KeGetThreadID();
	_KeExitSubThread(thread_id);
}

int pthread_kill(pthread_t* thread, int sig) {
	// per thread signaling, implement baalor korai
	//nai roh kela !!
	if (!thread)
		return 1;
	_KeSendSignalToThread(thread->id, sig);
	return 0;
}

int gettid() {
	return _KeGetThreadID();
}

int pthread_join(pthread_t thread, void** retval) {
	//need to implement in kernel level
	//baalor implement koribo baaki asei,
	//current thread tui wait koribo, tar
	//maal thread tule, maal thread tui
	//nijor bf tur logot lilimaai kori ahi
	//eyar tat ahibo, tar pisot ee nijor kaam
	//resume koribo
	int result = _KeThreadWaitForTermination(thread.id);
	return result;
}

void pthread_cleanup_push(void (*routine)(void*), void* arg) {}

void pthread_cleanup_pop(int execute) {}

int pthread_mutex_lock(pthread_mutex_t* mutex) {
	return 0;
}

int pthread_mutex_trylock(pthread_mutex_t* mutex) {
	return 0;
}

int pthread_mutex_unlock(pthread_mutex_t* mutex) {
	return 0;
}

int pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr) {
	return 0;
}

int pthread_mutex_destroy(pthread_mutex_t* mutex) {
	return 0;
}

int pthread_attr_init(pthread_attr_t* attr) {
	return 0;
}

int pthread_attr_destroy(pthread_attr_t* attr) {
	return 0;
}

int pthread_rwlock_init(pthread_rwlock_t* lock, void* args) {
	return 0;
}

int pthread_rwlock_wrlock(pthread_rwlock_t* lock) {
	return 0;
}

int pthread_rwlock_rdlock(pthread_rwlock_t* lock) {
	return 0;
}

int pthread_rwlock_unlock(pthread_rwlock_t* lock) {
	return 0;
}

int pthread_rwlock_destroy(pthread_rwlock_t* lock) {
	return 0;
}
