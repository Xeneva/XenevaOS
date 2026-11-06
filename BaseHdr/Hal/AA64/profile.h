/**
* @file profile.h
*
* BSD 2-Clause License
*
* Copyright (c) 2022-2025, Manas Kamal Choudhury
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

#ifndef __PROFILE_H__
#define __PROFILE_H__

/**
 * use flag __KERNEL_PROFILER_ON__ to turn on profiler on
 * specific code portion
 */

#include <stdint.h>
#include <Drivers/uart.h>

extern uint64_t AuProfileReadCycles();
extern uint64_t AuProfileReadFreq();


#define PROFILE_START(name) \
     uint64_t __start_profile = AuProfileReadCycles();

#define PROFILE_END(name)\
    uint64_t __end_profile = AuProfileReadCycles(); \
    uint64_t diff = __end_profile - __start_profile; \
    uint64_t freq = get_cntfrq_el0(); \
    double sec = (double)diff / freq; \
    double ms = (double)diff * 1000.0 / freq; \
    UARTDebugOut("[AURORA-PROFILE]: %s : time: %f sec, time: %f ms, cpu cycles : %d\r\n", name, sec, ms, (__end_profile - __start_profile));



#endif
