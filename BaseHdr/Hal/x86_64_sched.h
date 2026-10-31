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

#ifndef __X86_64_SCHED_H__
#define __X86_64_SCHED_H__

#include <stdint.h>
#include <aurora.h>
#include <Ipc/signal.h>

#define  THREAD_STATE_READY     1
#define  THREAD_STATE_BLOCKED   3
#define  THREAD_STATE_SLEEP     4
#define  THREAD_STATE_KILLABLE  5

//! Thread levels =========================================================
//! THREAD_LEVEL_KERNEL -- This bit is set when the thread given is kernel mode
//! THREAD_LEVEL_USER -- This bit is set when the thread given is user mode

#define  THREAD_LEVEL_KERNEL (1<<0)
#define  THREAD_LEVEL_USER (1<<1)
#define  THREAD_LEVEL_SUBTHREAD (1<<2)
#define  THREAD_LEVEL_MAIN_THREAD (1<<3)


typedef struct _frame_ {
	uint64_t ss;       //0x00
	uint64_t rsp;      //0x08
	uint64_t rflags;   //0x10
	uint64_t cs;       //0x18
	uint64_t rip;      //0x20

	uint64_t rax;      //0x28
	uint64_t rbx;      //0x30
	uint64_t rcx;      //0x38
	uint64_t rdx;      //0x40
	uint64_t rsi;       //0x48
	uint64_t rdi;       //0x50
	uint64_t rbp;       //0x58
	uint64_t r8;        //0x60
	uint64_t r9;        //0x68
	uint64_t r10;        //0x70
	uint64_t r11;        //0x78
	uint64_t r12;        //0x80
	uint64_t r13;        //0x88
	uint64_t r14;        //0x90
	uint64_t r15;        //0x98
	uint64_t ds;         //0xA0
	uint64_t es;         //0xA8
	uint64_t fs;         //0xB0
	uint64_t gs;         //0xB8
	uint64_t cr3;        //0xC0     [0x08]
	uint64_t kern_esp;   //0xC8     [0x10]
	bool user_;          //0xD0
}AuThreadFrame;


#pragma pack(push,1)
typedef struct _syscall_param_ {
	int64_t param1;    //0xE0
	int64_t param2;    //0xE8
	int64_t param3;    //0xF0
	int64_t param4;    //0xF8
	int64_t param5;    //0x100
	int64_t param6;    //0x108
}AuSyscallParam;
#pragma pack(pop)


#pragma pack(push,1)
/* AuUserEntry structure */
typedef struct _uentry_ {
	uint64_t entrypoint;
	uint64_t rsp;
	uint64_t cs;
	uint64_t ss;
	int num_args;
	uint64_t argvaddr;
	char** argvs;
	uint64_t stackBase;
}AuUserEntry;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _signal_queue_ {
	void* Signal;
	_signal_queue_* link;
}SignalQueue;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _au_thread_ {
	AuThreadFrame frame;
	uint64_t user_stack;  //0xD1
	/* syscall_func_ptr for multiprocessor
	 * purpose */
	AuSyscallParam syscall_param;  //0xE0
	uint8_t *fx_state;
	uint32_t mxcsr;
	char name[16];
	uint8_t state;
	uint16_t id;
	uint64_t quanta;
	uint64_t endTick;
	uint8_t priviledge;
	AuSigHandler singals[39];
	SignalQueue* signalQueue;
	uint8_t pendingSigCount;
	void* returnableSignal;
	/* if this thread is backend of
	 * any user thread, uentry should
	 * be filled */
	AuUserEntry *uentry;
	void* procSlot;
	_au_thread_ *next;
	_au_thread_ *prev;
}AuThread;
#pragma pack(pop)


/*
* AuSchedulerStart -- start the scheduler service
*/
extern void AuSchedulerStart();

/*
* AuSchedulerInitialise -- initialise the core scheduler
*/
extern void AuSchedulerInitialise();

/*
* AuSchedulerInitAp -- pin the idle thread to per cpu
* current thread pointer
*/
extern void AuSchedulerInitAp();

/**
* ! Creates a kernel mode thread
*  @param entry -- Entry point address
*  @param stack -- Stack address
*  @param cr3 -- the top most page map level address
*  @param name -- name of the thread
*  @param priority -- (currently unused) thread's priority
**/
AU_EXTERN AU_EXPORT AuThread* AuCreateKthread(void(*entry) (uint64_t), uint64_t stack, uint64_t cr3, char *name);


/*
* AuGetCurrentThread -- gets the running thread
* from per_cpu_data, currently done only non-multi
* processing
*/
AU_EXTERN AU_EXPORT AuThread* AuGetCurrentThread();

/*
* AuKThreadCopy -- copies the context of dest
* thread to source thread
* @param dest -- destination thread
* @param src -- source thread
*/
extern void AuKThreadCopy(AuThread* dest, AuThread* src);

/*
* AuBlockThread -- blocks a thread and insert it to
* block list
*/
AU_EXTERN AU_EXPORT void AuBlockThread(AuThread *thread);

/*
* AuSleepThread -- sleeps a thread
*/
AU_EXTERN AU_EXPORT void AuSleepThread(AuThread *thread, uint64_t ms);

/*
* AuUnblockThread -- unblocks a thread and insert it to
* ready list
* @param t -- pointer to thread
*/
AU_EXTERN AU_EXPORT void AuUnblockThread(AuThread *t);

/*
* AuThreadMoveToTrash -- move given thread to
* trash
* @param t -- Thread to move to trash
*/
extern void AuThreadMoveToTrash(AuThread* t);

/*
* AuThreadCleanTrash -- removes a particular thread
* from trash list
* @param t -- > thread to remove
*/
extern void AuThreadCleanTrash(AuThread* t);

/*
* AuThreadFindByID -- finds a thread by its id from
* ready queue
* @param id -- id of the thread
*/
AU_EXTERN AU_EXPORT AuThread* AuThreadFindByID(uint16_t id);

/*
* AuThreadFindByIDBlockList -- finds a thread by its id from
* the block queue
* @param id -- id of the thread
*/
AU_EXTERN AU_EXPORT AuThread* AuThreadFindByIDBlockList(uint16_t id);

/*
* AuForceScheduler -- force the scheduler
* to switch next thread
*/
AU_EXTERN AU_EXPORT void AuForceScheduler();

AU_EXTERN AU_EXPORT bool AuIsSchedulerInitialised();

/*
* AuGetSystemTimerTick -- returns the
* current timer tick value
*/
AU_EXTERN AU_EXPORT uint64_t AuGetSystemTimerTick();
#endif