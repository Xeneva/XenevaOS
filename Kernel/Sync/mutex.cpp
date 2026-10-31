/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2023, Manas Kamal Choudhury
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

#include <Sync/mutex.h>
#include <Hal/x86_64_sched.h>
#include <Hal/x86_64_lowlevel.h>
#include <Hal/x86_64_cpu.h>
#include <Hal/serial.h>
#include <_null.h>
#include <Mm/kmalloc.h>

/*
 * AuCreateMutex -- create a new mutex and return
 */
AU_EXTERN AU_EXPORT AuMutex* AuCreateMutex() {
	AuMutex* mut = (AuMutex*)kmalloc(sizeof(AuMutex));
	mut->lock = AuCreateSpinlock(false);
	mut->owner = NULL;
	mut->waiters = initialize_list();
	return mut;
}

/*
 * AuAcquireMutex -- acuire a mutex lock
 * @param mut -- Pointer to mutex
 */
AU_EXTERN AU_EXPORT int AuAcquireMutex(AuMutex* mut) {
	if (!AuIsSchedulerInitialised())
		return 0;
	if (!mut)
		return 0;
	x64_cli();
	AuAcquireSpinlock(mut->lock);
	AuThread* current_thr = AuGetCurrentThread();
	AuProcess* current_proc = AuProcessFindThread(current_thr);
	while (mut->status) {
		list_add(mut->waiters, current_thr);
		current_proc->state = 0;
		current_proc->state |= PROCESS_STATE_BUSY_WAIT;
		AuBlockThread(current_thr);
		x64_force_sched();
	}

	mut->status = 1;
	mut->owner = current_thr;
	SeTextOut("Mutex acquired \r\n");
	AuReleaseSpinlock(mut->lock);
	return 0;
}

/*
 * AuReleaseMutex -- release a mutex
 * @param pointer to mutex
 */
AU_EXTERN AU_EXPORT int AuReleaseMutex(AuMutex *mutex) {
	if (!AuIsSchedulerInitialised())
		return 0;
	if (!mutex)
		return -1;
	if (mutex->owner != AuGetCurrentThread()) {
		return -1;
	}
	AuAcquireSpinlock(mutex->lock);
	mutex->status = 0;
	mutex->owner = NULL;
	for (int i = 0; i < mutex->waiters->pointer; i++) {
		AuThread* thr_ = (AuThread*)list_remove(mutex->waiters, i);
		AuProcess* current_proc = AuProcessFindThread(thr_);
		if (thr_) {
			current_proc->state = 0;
			current_proc->state |= PROCESS_STATE_READY;
			AuUnblockThread(thr_);
		}
	}
	SeTextOut("Mutex released \r\n");
	AuReleaseSpinlock(mutex->lock);
	return 0;
}

/*
 * AuDeleteMutex -- free up a mutex
 * @param mutex -- Pointer to mutex
 */
AU_EXTERN AU_EXPORT void AuDeleteMutex(AuMutex *mutex) {
	kfree(mutex->waiters);
	kfree(mutex);
}