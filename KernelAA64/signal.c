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

#include <signal.h>
#include <Drivers/uart.h>
#include <Hal/AA64/sched.h>
#include <Mm/vmmngr.h>
#include <Mm/pmmngr.h>
#include <string.h>
#if defined(__GNUC__) || defined(__clang__)
#ifndef __cplusplus
#include <stdbool.h>
#endif
#endif

extern void aa64_signal_return();
/**
 * @brief AuAllocSignal -- allocate a signal for destination
 * thread
 * @param thr -- destination thread
 * @param signum -- signal number
 */
int AuAllocSignal(AA64Thread* thr, int signum) {
	if (!thr)
		return 1;

	if (signum <= 0)
		return 1;

	if (signum > SIGTTOU)
		return 1;

	/* check if all tokens are sold out !!*/

	/** let's go with standard posix signal **/
	thr->sig_pending |= (1UL << signum);

	AuThreadMakeReady(thr);
	return 0;
}

static inline int ctzl(uint64_t x) {
	if (x == 0)
		return 64;
	int n = 0;
	while (!(x & 1UL)) {
		x >>= 1;
		n++;
	}
	return n;
}
/**
 * @brief AuSignalDispatch -- dispatch a signal
 * @param thr -- pointer to thread
 */
int AuSignalDispatch(AA64Thread* thr) {
	//lock here
	int result = 0;
	uint64_t deliverable = thr->sig_pending;
	if (deliverable) {
		int signo = ctzl(deliverable);
		thr->sig_pending &= ~(1UL << signo);
		result = signo;
	}
	//unlock here
	//UARTDebugOut("SignalDispatch sig number : %d \r\n", result);
	return result;
}

/**
 * @brief AuSignalDeliver -- deliver the current thread's
 * signal
 * @param current_thread -- pointer to thread
 */
bool AuSignalDeliver(AA64Thread* current_thread) {
	int signo;
	while ((signo = AuSignalDispatch(current_thread)) != 0) {
		if (signo <= 0 || signo >= 32 || !current_thread->sigs[signo])
			continue;
		AA64Registers* regs_ = (AA64Registers*)current_thread->sp;
		memcpy(&current_thread->signal.regs, regs_, sizeof(AA64Registers));
		current_thread->signal.elr_el1 = current_thread->elr_el1;
		regs_->x0 = signo;
		current_thread->elr_el1 = (uint64_t)current_thread->sigs[signo];
		regs_->x30 = current_thread->signal.sigret_address;
		return true;
	}
	return false;
}

/**
 * @brief AuSignalInitializeTrampoline -- installs 
 * return trampoline code to threads sig return address
 * @param t -- pointer to thread struct
 */
void AuSignalInitializeTrampoline(AA64Thread* t) {
	uint64_t* phys = (uint64_t*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	memcpy(phys, &aa64_signal_return, PAGE_SIZE);
	AuMapPageEx((uint64_t*)t->pml,
				V2P((uint64_t)phys),
				0xD0000000,
				PTE_USER_EXECUTABLE | PTE_NORMAL_MEM | PTE_AP_RW_USER);
	t->signal.sigret_address = 0xD0000000;
}
