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

#ifndef __STDLIB_H__
#define __STDLIB_H__

#include <_xeneva.h>
#include <stddef.h>
#include <_xeprint.h>

#ifdef __cplusplus
XE_EXTERN {
#endif

#include <stdarg.h>

#define max(a, b)			  ((a) > (b) ? (a) : (b))
#define min(a, b)			  ((a) < (b) ? (a) : (b))
#define offsetof(type, field) ((unsigned long)&(((type*)0L)->field))

#define EXIT_FAILURE 1
#define EXIT_SUCCESS 0

#define RAND_MAX (0x7fffffff)

	XE_LIB int atoi(const char*);
	XE_LIB long atol(const char*);
#define atoll(string) ((long long)_str2num(string, 10, 1, NULL))

	XE_LIB double atof(const char*);
	XE_LIB int abs(int);
	XE_LIB void* malloc(unsigned int);
	XE_LIB void free(void* ptr);
	XE_LIB void* realloc(void* address, unsigned int new_size);
	XE_LIB void* calloc(unsigned long long num, unsigned long long size);
	XE_LIB int mblen(const char*, size_t);
	XE_LIB size_t mbstowcs(wchar_t*, const char*, size_t);
	XE_LIB int mbtowc(wchar_t*, const char*, size_t);
	XE_LIB char* mkdtemp(char*);
	XE_LIB char* mkstemp(char*);
	XE_LIB int rand(void);
#define random() rand()
	XE_LIB size_t wcstombs(char*, const wchar_t*, size_t);
	XE_LIB int wctomb(char*, wchar_t);
	XE_LIB void qsort(
		void* base, size_t num, size_t size, int (*comparator)(const void*, const void*));
	XE_LIB void* bsearch(const void* key,
						 const void* base,
						 size_t num,
						 size_t size,
						 int (*comparator)(const void*, const void*));

	XE_LIB int system(const char* command);

	XE_LIB char* sztoa(size_t value, char* str, int base);
	/*
     * setenv -- set environment variables 
     * @param name -- Key name
     * @param value -- value of the variable
     * @param overwrite -- 0 - append at the last, 1 force-write
     */
	XE_LIB int setenv(const char* name, const char* value, int overwrite);
	XE_LIB char* getenv(const char*);
	XE_LIB void exit(int errno);

	XE_LIB int vsprintf(char* str, const char* format, va_list ap);
	XE_LIB long strtol(const char* nptr, char** endptr, int base);
	XE_LIB unsigned long strtoul(const char* nptr, char** endptr, int base);
	XE_LIB void itoa_s(int i, unsigned base, char* buf);
	XE_LIB void PrintOSName();
	XE_LIB void abort(void);
	XE_LIB int atexit(void (*func)(void));

// Functions are from Visopsys project
// These are unofficial, Andy-special extensions of the atoi() and atoll()
// paradigm
// Visopsys, Copyright (C) 1998-2020 J. Andrew McLaughlin
#define atou(string)			 ((unsigned)_str2num(string, 10, 0, NULL))
#define atoull(string)			 _str2num(string, 10, 0, NULL)
#define atoo(string)			 ((unsigned)_str2num(string, 8, 0, NULL))
#define atooll(string)			 _str2num(string, 8, 0, NULL)
#define dtoa(num, string, round) _dbl2str(num, string, round)
#define ftoa(num, string)		 _flt2str(num, string, round)
#define itoa(num, string)		 _num2str(num, string, 10, 1)
#define itouo(num, string)		 _num2str(num, string, 10, 1)
#define itoux(num, string)		 _num2str(num, string, 16, 0);
#define itox(num, string)		 _num2str(num, string, 16, 1)
#define lltoa(num, string)		 _lnum2str(num, string, 10, 1)
#define lltouo(num, string)		 _lnum2str(num, string, 8, 0)
#define lltoux(num, string)		 _lnum2str(num, string, 16, 0)
#define lltox(num, string)		 _lnum2str(num, string, 16, 1)
#define xtoi(string)			 ((int)_str2num(string, 16, 1, NULL))
#define xtoll(string)			 ((long long)_str2num(string, 16, 1, NULL))
#define ulltoa(num, string)		 _lnum2str(num, string, 10, 0)
#define utoa(num, string)		 _num2str(num, string, 10, 0)

#ifdef __cplusplus
}
#endif

#endif