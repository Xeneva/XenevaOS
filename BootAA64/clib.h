/**
* BSD 2-Clause License
*
* Copyright (c) 2023-2025, Manas Kamal Choudhury
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

#ifndef __CLIB_H__
#define __CLIB_H__

#include <stdint.h>
//#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void* memset(void* targ, uint8_t val, uint32_t len);
void* memcpy(void* targ, void* src, uint32_t len);
size_t strlen(const char* s);

#ifdef __cplusplus
}
#endif

// extern void memset(void* targ, uint8_t val, uint32_t len);
// extern void memcpy(void* targ, void* src, uint32_t len);
extern wchar_t* wstrchr(wchar_t* s, int c);
extern int wstrlen(wchar_t* s);
extern uint32_t wstrsize(wchar_t* s);
extern int to_upper(int c);
extern int to_lower(int c);
extern int is_digit(int c);
extern char* sztoa(size_t value, char* str, int base);
// extern size_t strlen(const char* s);
#endif
