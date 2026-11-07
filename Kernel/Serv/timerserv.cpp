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
#include <Hal/x86_64_hal.h>
#include <Hal/x86_64_sched.h>
#include <Hal/hal.h>
#include <time.h>
#include <aucon.h>

/*
 * CreateTimer -- create timer service
 * @param threadID -- current thread id
 * @param maxTickLimit -- maximum tick limit
 * @param updatemode -- Timer update mode
 */
int CreateTimer(int threadID,int maxTickLimit,uint8_t updatemode) {
	x64_cli();
	AuTimerCreate(threadID, maxTickLimit, updatemode);
	return 0;
}

/*
 * StartTimer -- starts the timer
 */
int StartTimer(int threadID) {
	x64_cli();
	AuTimerStart(threadID);
	return 0;
}

/*
 * StopTimer -- stop the timer
 */
int StopTimer(int threadID) {
	x64_cli();
	AuTimerStop(threadID);
	return 0;
}

/*
 * DestroyTimer -- remove the timer
 * @param threadID -- currently running thread
 * id
 */
int DestroyTimer(int threadID) {
	x64_cli();
	AuTimerDestroy(threadID);
	return 1;
}

/*
* GetTimeOfDay -- returns the time format
* in unix format
* @param ptr -- Pointer to timeval
* structure
*/
int GetTimeOfDay(void* ptr) {
	x64_cli();
	if (!ptr)
		return -1;
	timeval *val = (timeval*)ptr;
	AuGetTimeOfTheDay(val);
	return 0;
}