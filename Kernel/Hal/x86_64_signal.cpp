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

#include <Hal/x86_64_signal.h>
#include <Hal/x86_64_gdt.h>
#include <Mm/kmalloc.h>
#include <Mm/vmmngr.h>
#include <Mm/pmmngr.h>
#include <string.h>
#include <aucon.h>
#include <Hal/serial.h>
#include <_null.h>

extern "C" void SigRet();
extern "C" bool _signal_debug = false;

/*
 * AuAllocateSignal -- allocate a new signal to the
 * destination thread
 * @param dest_thread -- destination thread
 * @param signum -- signal number
 */
void AuAllocateSignal(AuThread* dest_thread, int signum) {
	if (dest_thread->signalQueue)
		return;
	Signal *signal = (Signal*)kmalloc(sizeof(Signal));
	memset(signal, 0, sizeof(Signal));
	signal->signum = signum;
	signal->signalStack = (x86_64_cpu_regs_t*)kmalloc(sizeof(x86_64_cpu_regs_t));
	signal->signalState = (AuThreadFrame*)kmalloc(sizeof(AuThreadFrame));
	SignalQueue* queue = (SignalQueue*)kmalloc(sizeof(SignalQueue));
	memset(queue, 0, sizeof(SignalQueue));
	queue->Signal = signal;
	queue->link = dest_thread->signalQueue;
	dest_thread->signalQueue = queue;
	dest_thread->pendingSigCount += 1;
	signal->threadState = dest_thread->state;

}

/*
 * AuCheckSignal -- checks for pending signal
 * @param curr_thr -- pointer to thread structure
 * @param frame -- interrupt stack frame
 */
bool AuCheckSignal(AuThread* curr_thr, interrupt_stack_frame *frame) {
	if (!curr_thr->signalQueue)
		return false;
	if (curr_thr->pendingSigCount > 0 && frame->cs == SEGVAL(GDT_ENTRY_USER_CODE,3))
		return true;
	return false;
}

/*
 * AuGetSignal -- returns a signal from the queue
 * if there is present one
 * @param curr_thr -- Pointer to thread structure
 */
Signal *AuGetSignal(AuThread* curr_thr) {
	if (!curr_thr->signalQueue)
		return NULL;
	Signal* sig;
	SignalQueue* temp;
	temp = curr_thr->signalQueue;
	curr_thr->signalQueue = curr_thr->signalQueue->link;
	temp->link = NULL;
	sig = (Signal*)temp->Signal;
	kfree(temp);
	curr_thr->pendingSigCount--;

	curr_thr->returnableSignal = sig;
	if (!sig)
		return NULL;
	return sig;
}

/*
 * AuPrepareSignal -- prepare a thread to enter a signal handler
 * @param thr -- Pointer to the thread
 * @param frame -- interrupt stack frame
 * @param signal -- pointer to signal
 */
void AuPrepareSignal(AuThread* thr, interrupt_stack_frame* frame, Signal* signal) {
	x86_64_cpu_regs_t* ctx = (x86_64_cpu_regs_t*)(thr->frame.kern_esp - sizeof(x86_64_cpu_regs_t));
	memcpy(signal->signalStack, ctx, sizeof(x86_64_cpu_regs_t));
	memcpy(signal->signalState, &thr->frame, sizeof(AuThreadFrame));
	uint64_t rsp_val = (uint64_t)frame->rsp;
	rsp_val -= 8;
	rsp_val &= 0xFFFFFFFFFFFFFFF0;
	uint64_t* rsp_ = (uint64_t*)rsp_val;
	for (int i = 0; i < 2; i++)
		AuMapPage((uint64_t)AuPmmngrAlloc(), 0x700000 + i * 4096, X86_64_PAGING_USER);
	memcpy((void*)0x700000, &SigRet, 8192);
	*rsp_ = 0x700000;

	thr->frame.rbp = (uint64_t)rsp_;
	thr->frame.rcx = signal->signum;
	thr->frame.rip = (uint64_t)thr->singals[signal->signum];
	thr->frame.rsp = (uint64_t)frame->rsp;
	thr->frame.rflags = 0x286;
	
	frame->rsp = (uint64_t)rsp_;
	frame->rip = thr->frame.rip;
	frame->rflags = 0x286;
	frame->cs = SEGVAL(GDT_ENTRY_USER_CODE, 3);
	frame->ss = SEGVAL(GDT_ENTRY_USER_DATA, 3);
	thr->returnableSignal = signal;
}

/*
 * AuSendSignal -- send a signal to a specific thread
 * @param tid -- thread id
 * @param signo -- signal number
 */
void AuSendSignal(uint16_t tid, int signo) {
	AuThread* thr = AuThreadFindByID(tid);
	bool blocked = false;
	if (!thr){
		thr = AuThreadFindByIDBlockList(tid);
		if (thr)
			blocked = true;
	}
		
	if (!thr)
		return;

	AuAllocateSignal(thr, signo);

	/* unblock the thread for signal handling */
	if (blocked) {
		AuUnblockThread(thr);
	}

	if (thr->state == THREAD_STATE_SLEEP) {
		thr->quanta = 0;
		thr->state = THREAD_STATE_READY;
	}
}


/*
 * AuSignalRemoveAll -- remove all signal forcefully
 * @param thr -- thread to look for signals
 */
void AuSignalRemoveAll(AuThread* thr) {
	if (thr->pendingSigCount < 0)
		return;

	while (thr->pendingSigCount) {
		Signal * sig = AuGetSignal(thr);
		if (!sig)
			break;  //there might be bug in pendingSigCount
		kfree(sig->signalStack);
		kfree(sig->signalState);
		kfree(sig);
		thr->pendingSigCount--;
	}
}

extern "C" void AuSignalDebug(uint64_t rcx) {
	if (_signal_debug) {
		SeTextOut("signal ret stack -> %x \r\n", rcx);
	}
}