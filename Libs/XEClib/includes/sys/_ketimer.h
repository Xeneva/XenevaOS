/**
* @file _ketimer.h
* 
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

#ifndef __KE_TIMER_H__
#define __KE_TIMER_H__

#include <_xeneva.h>

#ifdef __cplusplus
XE_EXTERN {
#endif

#define _KE_TIMER_UNDIFINED_MAXCOUNT -1

#define _KE_TIMER_UPDATE_ORDER_SECOND	1
#define _KE_TIMER_UPDATE_ORDER_MINUTE	2
#define _KE_TIMER_UPDATE_ORDER_HOUR		3
#define _KE_TIMER_UPDATE_ORDER_INTERVAL 4

#define TIMER_MESSAGE_CODE 8

	/**
	* @brief _KeCreateTimer -- create timer service
	* @param threadID -- current thread id
	* @param maxTickLimit -- maximum tick limit
	* @param updatemode -- Timer update mode
	*/
	XE_LIB int _KeCreateTimer(int threadID, int maxLimit, uint8_t updateMode);

	/**
	* @brief _KeStartTimer -- starts the timer
	*/
	XE_LIB int _KeStartTimer(int threadID);

	/**
	* @brief _KeStopTimer-- stop the timer
	*/
	XE_LIB int _KeStopTimer(int threadID);

	/**
	* @brief _KeDestroyTimer -- remove the timer
	* @param threadID -- current thread id
	*/
	XE_LIB int _KeDestroyTimer(int threadID);

	/**
	 * @brief _KeSetAlarm -- set one-shot alarm
	 * @param seconds -- second to consider
	 */
	XE_LIB int _KeSetAlarm(unsigned long long seconds);

	XE_LIB int _KeSetITimer(int which, const void*, const void*);

	XE_LIB int _KeGetITimer(int which, const void*);

#ifdef __cplusplus
}
#endif

#endif