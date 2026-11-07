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

#ifndef __AU_TIMER_H__
#define __AU_TIMER_H__

#include <stdint.h>

#define TIMER_UPDATE_ORDER_SECOND 1
#define TIMER_UPDATE_ORDER_MINUTE 2
#define TIMER_UPDATE_ORDER_HOUR 3
#define TIMER_UPDATE_ORDER_INTERVAL 4

#define TIMER_MESSAGE_CODE 8

#define MAX_TICK_DEFAULT UINT32_MAX

#pragma pack(push,1)
typedef struct _timer_ {
	int lastTick;
	uint8_t updateOrder;
	uint16_t threadId; //registered thread id
	int tickDifference;
	int maxTick;
	bool run;
	_timer_ *next;
	_timer_ *prev;
}AuTimer;
#pragma pack(pop)

/*
* AuTimerDataInitialise -- initialise timer
* data's to default value
*/
extern void AuTimerDataInitialise();

/*
* AuTimerCreate -- create a new timer and immediately it will start
*/
extern void AuTimerCreate(uint16_t thread_id, int maxTickLimit, uint8_t updateOrder);

/*
*AuTimerStart -- starts the timer
* @param threadId -- timer's thread id
*/
extern void AuTimerStart(uint16_t threadId);

/*
* AuTimerStop -- stop the timer
* @param threadId -- timer's thread id
*/
extern void AuTimerStop(uint16_t threadId);

/*
*AuTimerDestroy -- destroys a timer associated
* with given thread id
* @param threadId -- thread's id
*/
extern void AuTimerDestroy(uint16_t threadID);
/*
* AuTimerFire-- called by RTC clock interrupt handler
* @param sec -- second
* @param min -- minute
* @param hour -- hour value
*/
extern void AuTimerFire(int sec, int min, int hour);


#endif