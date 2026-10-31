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

#include <Sync/waitq.h>
#include <_null.h>

/**
 * @brief AuWaitQueueInitialize -- initialize a queue
 * @param queue -- Pointer to the allocated queue
 */
void AuWaitQueueInitialize(AuWaitQueue* queue) {
	if (!queue)
		return;
	queue->head = NULL;
}

void AuWaitQueueAdd(AuWaitQueue* q, AuWaitQEntry* e) {
	if (!q || !e)
		return;

	e->prev = NULL;
	e->queue = q;
	e->next = q->head;
	if (q->head)
		q->head->prev = e;
	q->head = e;
}

void AuWaitQueueRemove(AuWaitQEntry* e) {
	if (!e)
		return;
	//mask interrupts
	AuWaitQueue* q = e->queue;
	if (q) {
		if (e->prev)
			e->prev->next = e->next;
		else
			q->head = e->next;
		if (e->next)
			e->next->prev = e->prev;
		e->next = e->prev = NULL;
		e->queue = NULL;
	}
	//unmask interrupts
}

void AuWaitQueue_Wake(AuWaitQueue* q) {
	if (!q)
		return;
	//mask interrupts
	AuWaitQEntry* e = q->head;
	while (e) {
		AuWaitQEntry* next = e->next;
		if (e->thread) {
			/* poll(2) waiters are woken through the poll-aware path;
			 * blocking socket calls park in a plain timed sleep. */
			if (e->thread->blocked_on_poll)
				AuThreadWakePoller(e->thread);
			else
				AuThreadMakeReady(e->thread);
		}
		e = next;
	}
	//unmask interrupts
}

void AuWaitQueue_DetachAll(AuWaitQueue* q) {
	if (!q)
		return;
	//mark interrupt disabled
	AuWaitQueue_Wake(q);
	AuWaitQEntry* e = q->head;
	while (e) {
		AuWaitQEntry* n = e->next;
		e->next = e->prev = NULL;
		e = n;
	}
	q->head = NULL;
	//unmask interrupts
}

void AuWaitQ_DestroyChain(AuWaitQEntry* chain) {
	while (chain) {
		AuWaitQEntry* n = chain->owner_next;
		AuWaitQueueRemove(chain);
		kfree(chain);
		chain = n;
	}
}
