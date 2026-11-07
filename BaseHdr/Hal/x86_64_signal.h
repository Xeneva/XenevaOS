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

#ifndef __X86_64_SIGNAL_H__
#define __X86_64_SIGNAL_H__

#include <Hal/x86_64_cpu.h>
#include <Hal/x86_64_sched.h>



#pragma pack(push,1)
typedef struct _signal_ {
	int signum;
	x86_64_cpu_regs_t *signalStack;
	AuThreadFrame* signalState;
	uint8_t threadState;
	bool returnSyscall;
}Signal;
#pragma pack(pop)

/*
* AuAllocateSignal -- allocate a new signal to the
* destination thread
* @param dest_thread -- destination thread
* @param signum -- signal number
*/
extern void AuAllocateSignal(AuThread* dest_thread, int signum);

/*
* AuCheckSignal -- checks for pending signal
* @param curr_thr -- pointer to thread structure
* @param frame -- interrupt stack frame
*/
extern bool AuCheckSignal(AuThread* curr_thr, interrupt_stack_frame *frame);

/*
* AuGetSignal -- returns a signal from the queue
* if there is present one
* @param curr_thr -- Pointer to thread structure
*/
extern Signal *AuGetSignal(AuThread* curr_thr);

/*
* AuPrepareSignal -- prepare a thread to enter a signal handler
* @param thr -- Pointer to the thread
* @param frame -- interrupt stack frame
* @param signal -- pointer to signal
*/
extern void AuPrepareSignal(AuThread* thr, interrupt_stack_frame* frame, Signal* signal);

/*
* AuSendSignal -- send a signal to a specific thread
* @param tid -- thread id
* @param signo -- signal number
*/
extern void AuSendSignal(uint16_t tid, int signo);

/*
* AuSignalRemoveAll -- remove all signal forcefully
* @param thr -- thread to look for signals
*/
extern void AuSignalRemoveAll(AuThread* thr);
#endif