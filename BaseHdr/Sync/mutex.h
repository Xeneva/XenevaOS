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

#ifndef __MUTEX_H__
#define __MUTEX_H__

#include <Sync/spinlock.h>
#include <stdint.h>
#include <Hal/x86_64_sched.h>
#include <aurora.h>
#include <list.h>

#pragma pack(push,1)
typedef struct _mutex_ {
	Spinlock* lock;
	volatile uint8_t status;
	AuThread *owner;
	list_t* waiters;
}AuMutex;
#pragma pack(pop)

/*
* AuCreateMutex -- create a new mutex and return
*/
AU_EXTERN AU_EXPORT AuMutex* AuCreateMutex();

/*
* AuAcquireMutex -- acuire a mutex lock
* @param mut -- Pointer to mutex
*/
AU_EXTERN AU_EXPORT int AuAcquireMutex(AuMutex* mut);

/*
* AuReleaseMutex -- release a mutex
* @param pointer to mutex
*/
AU_EXTERN AU_EXPORT int AuReleaseMutex(AuMutex *mutex);

/*
* AuDeleteMutex -- free up a mutex
* @param mutex -- Pointer to mutex
*/
AU_EXTERN AU_EXPORT void AuDeleteMutex(AuMutex *mutex);

#endif