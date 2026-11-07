/**
* BSD 2-Clause License
*
* Copyright (c) 2023-2026, Manas Kamal Choudhury
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

#ifndef __KERNEL_LOG_H__
#define __KERNEL_LOG_H__

#include <stdint.h>
#include <aurora.h>

#define BORDOISILA_DEBUG_LEVEL_COUNT 5
/**
 * Loggin level of bordoisila
 */
typedef enum {
	BORDOISILA_EMERG = 0,
	BORDOISILA_ERROR,
	BORDOISILA_WARN,
	BORDOISILA_INFO,
	BORDOISILA_DEBUG,
}BLogLevel;

typedef void (*BLogSinkFunc)(const char* data, uint32_t len, void* ctx);

/**
 * @brief B_KLogInit -- initialize bordoisila
 * logging system
 */
extern void B_KLogInit();
/**
 * @brief BPrintK -- print formatted string to circular buffer
 * @param level -- logging level
 * @param fmt -- formatted string
 */
AU_EXTERN AU_EXPORT void BPrintK(BLogLevel level, const char* fmt, ...);

/**
 * @brief _BlogBypassAuConsole -- if logging was using
 * framebuffer, bypass it to uart
 */
extern void _BlogBypassAuConsole();

#endif
