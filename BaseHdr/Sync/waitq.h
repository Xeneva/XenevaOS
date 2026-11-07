/**
* BSD 2-Clause License
*
* Copyright (c) 2022 - 2026, Manas Kamal Choudhury
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

#ifndef __WAIT_QUEUE_H__
#define __WAIT_QUEUE_H__

#include <stdint.h>
#include <Hal/AA64/aa64cpu.h>
#include <Hal/AA64/sched.h>

struct _au_wait_queue;

typedef struct _au_waitq_entry_ {
	AA64Thread* thread;
	void* data;
	struct _au_wait_queue* queue;
	struct _au_waitq_entry_* next;
	struct _au_waitq_entry_* prev;
	struct _au_waitq_entry_* owner_next;
} AuWaitQEntry;

typedef struct _au_wait_queue {
	AuWaitQEntry* head;
} AuWaitQueue;

extern void AuWaitQueueInitialize(AuWaitQueue* queue);

extern void AuWaitQueueAdd(AuWaitQueue* q, AuWaitQEntry* e);

extern void AuWaitQueueRemove(AuWaitQEntry* e);

extern void AuWaitQueue_Wake(AuWaitQueue* q);

extern void AuWaitQueue_DetachAll(AuWaitQueue* q);

extern void AuWaitQ_DestroyChain(AuWaitQEntry* chain);

#endif