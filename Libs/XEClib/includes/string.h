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

#ifndef __XE_STRING_H__
#define __XE_STRING_H__

#include <stdint.h>
#include <_xeneva.h>

#define MAX_STRING_LENGTH 4095

#ifdef __cplusplus
XE_EXTERN {
#endif
	//!==========================================================
	//! strings global functions
	//!==========================================================

	XE_LIB int strcmp(const char* str1, const char* str2);
	XE_LIB char* strcpy(char* s1, const char* s2);
	XE_LIB size_t strlen(const char* str);
	XE_LIB size_t strnlen(const char* string, size_t maxlen);
	XE_LIB int strncmp(const char* s1, const char* s2, size_t n);
	XE_LIB char* strncpy(char* destString, const char* sourceString, size_t maxLength);
	XE_LIB char* strchr(const char* str, int character);
	XE_LIB int strcasecmp(const char*, const char*);
	XE_LIB int strncasecmp(const char* s1, const char* s2, size_t length);
	XE_LIB char* strcasestr(const char*, const char*);
	XE_LIB char* strcat(char*, const char*);
	XE_LIB char* strncat(char* destString, const char* sourceString, size_t maxLength);
	XE_LIB char* strdup(const char*);
	XE_LIB char* strerror(int);
	XE_LIB char* strrchr(const char*, int);
	XE_LIB size_t strspn(const char*, const char*);
	XE_LIB char* strtok(char*, const char*);
	XE_LIB char* strtok_r(char*, const char*, char**);
	XE_LIB char* strstr(const char* s1, const char* s2);
	XE_LIB long long int strtoll(const char* string, char** endString, int base);

#define index(str, chr)	 strchr(str, chr)
#define rindex(str, chr) strrchr(str, chr)

	//!=====================================================
	//! M E M O R Y  G L O B A L   F U N C T I O N S
	//!=====================================================
	XE_LIB void* memset(void* targ, int val, size_t len);

	XE_LIB void* memcpy(void* targ, void* src, size_t len);
	XE_LIB int memcmp(const void* first, const void* second, size_t length);
	XE_LIB void bcopy(const void*, void*, size_t n);
	XE_LIB void bzero(void*, size_t);
	XE_LIB int ffs(int);
	XE_LIB int fls(int);
	XE_LIB size_t mbslen(const char*);
	XE_LIB void* memmove(void* dest, void const* src, size_t bytes);
	XE_LIB void* memchr(const void* src, int c, size_t n);

#ifdef __cplusplus
}
#endif

#endif