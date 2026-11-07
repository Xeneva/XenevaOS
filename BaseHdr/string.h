/**
* BSD 2-Clause License
*
* Copyright (c) 2022, Manas Kamal Choudhury
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
#ifndef __STRING_H__
#define __STRING_H__


#include <stdint.h>
#include <aurora.h>

#ifdef __cplusplus
AU_EXTERN{
#endif


	/*
	 * Global Functions !!
	 */

	AU_EXPORT int strcmp(const char* str1, const char* str2);
	AU_EXPORT char *strcpy(char *s1, const char *s2);
	AU_EXPORT size_t strlen(const char* str);
	AU_EXPORT int strncmp(const char* s1, const char *s2, size_t n);
	AU_EXPORT char *strncpy(char *destString, const char* sourceString, size_t maxLength);
	AU_EXPORT char* strchr(char* str, int character);
	AU_EXPORT char* strrchr(const char* str, int c);
	AU_EXPORT char *strcat(char *destString, const char *sourceString);
	AU_EXPORT char *strncat(char *destString, const char *sourceString, size_t maxLength);
	AU_EXPORT void memset(void *targ, int val, uint32_t len);
	AU_EXPORT void*  memcpy(void *targ, const void *src, size_t len);
	AU_EXPORT int memcmp(const void *first, const void *second, size_t length);
	AU_EXPORT void* memmove(void*, const void*, size_t);
	AU_EXPORT char* strdup(const char*  c);
	AU_EXPORT char* strstr(const char* s1, const char* s2);

#ifdef __cplusplus
}
#endif

#endif

