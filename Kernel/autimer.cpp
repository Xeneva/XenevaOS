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

#include <autimer.h>
#include <_null.h>
#include <Mm/kmalloc.h>
#include <string.h>
#include <aucon.h>
#include <Ipc/postbox.h>

AuTimer *timerFirst;
AuTimer *timerLast;

/*
* AuTimerDataInitialise -- initialise timer
* data's to default value
*/
void AuTimerDataInitialise() {
	timerFirst = NULL;
	timerLast = NULL;
}

void AuTimerInsert(AuTimer* timer) {
	timer->next = NULL;
	timer->prev = NULL;

	if (timerFirst == NULL){
		timerLast = timer;
		timerFirst = timer;
	}
	else {
		timerLast->next = timer;
		timer->prev = timerLast;
	}

	timerLast = timer;
}

void AuTimerDelete(AuTimer* timer) {
	if (timerFirst == NULL)
		return;

	if (timer == timerFirst){
		timerFirst = timerFirst->next;
	}
	else {
		timer->prev->next = timer->next;
	}

	if (timer == timerLast){
		timerLast = timer->prev;
	}
	else {
		timer->next->prev = timer->prev;
	}
	kfree(timer);
}

/*
* AuTimerCreate -- create a new timer and immediately it will start
*/
void AuTimerCreate(uint16_t thread_id, int maxTickLimit, uint8_t updateOrder){
	AuTimer* timer = (AuTimer*)kmalloc(sizeof(AuTimer));
	memset(timer, 0, sizeof(AuTimer));
	timer->threadId = thread_id;
	timer->maxTick = maxTickLimit;
	timer->updateOrder = updateOrder;
	timer->run = false;
	AuTimerInsert(timer);
}

/*
*AuTimerStart -- starts the timer
* @param threadId -- timer's thread id
*/
void AuTimerStart(uint16_t threadId) {
	for (AuTimer* t = timerFirst; t != NULL; t = t->next) {
		if (t->threadId == threadId) {
			t->run = true;
			break;
		}
	}
}


/*
* AuTimerStop -- stop the timer
* @param threadId -- timer's thread id
*/
void AuTimerStop(uint16_t threadId) {
	for (AuTimer* t = timerFirst; t != NULL; t = t->next) {
		if (t->threadId == threadId){
			t->run = false;
			break;
		}
	}
}

/*
*AuTimerDestroy -- destroys a timer associated
* with given thread id
* @param threadId -- thread's id
*/
void AuTimerDestroy(uint16_t threadID) {
	for (AuTimer* t = timerFirst; t != NULL; t = t->next) {
		if (t->threadId == threadID){
			AuTimerDelete(t);
			break;
		}
	}
}

/*
*AuTimerPostMessage -- post a postevent to
*thread's postbox
* @param t -- pointer to timer
*/
void AuTimerPostMessage(AuTimer* t) {
	PostEvent e;
	memset(&e, 0, sizeof(PostEvent));
	e.type = TIMER_MESSAGE_CODE;
	e.to_id = t->threadId;
	e.dword = t->lastTick;
	e.dword2 = t->tickDifference;
	PostBoxPutEvent(&e);
}

/*
* AuTimerFire-- called by RTC clock interrupt handler
* @param sec -- second
* @param min -- minute
* @param hour -- hour value
*/
void AuTimerFire(int sec, int min, int hour) {
	if (timerFirst) {
		for (AuTimer* t = timerFirst; t != NULL; t = t->next) {
			if (t->run) {
				if (t->updateOrder == TIMER_UPDATE_ORDER_HOUR) {
					if (t->maxTick != -1) {
						if (t->lastTick == t->maxTick)
							t->run = false;
					}
					if (t->lastTick != hour) {
						if (t->maxTick != -1){
							if (t->lastTick == t->maxTick)
								t->run = false;
						}
						AuTimerPostMessage(t);
						t->lastTick = hour;
					}
				}
				else if (t->updateOrder == TIMER_UPDATE_ORDER_MINUTE){
					if (t->maxTick != -1){
						if (t->lastTick == t->maxTick)
							t->run = false;
					}
					if (t->lastTick != min) {
						AuTimerPostMessage(t);
						t->lastTick = min;
					}
				}
				else if (t->updateOrder == TIMER_UPDATE_ORDER_SECOND) {
					if (t->maxTick != -1){
						if (t->lastTick == t->maxTick)
							t->run = false;
					}
					if (t->lastTick != sec){
						AuTimerPostMessage(t);
						t->lastTick = sec;
					}
				}
				else if (t->updateOrder == TIMER_UPDATE_ORDER_INTERVAL) {
					if (t->lastTick >= t->tickDifference) {
						AuTimerPostMessage(t);
						t->lastTick = 0;
					}
					else{
						t->lastTick++;
					}
				}
			}
		}
	}
}