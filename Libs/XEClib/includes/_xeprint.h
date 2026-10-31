/**
* @file _xeprint.h
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

#ifndef __XE_PRINT_H__
#define __XE_PRINT_H__

#include <_xeneva.h>
#include <stdarg.h>

#ifdef __cplusplus
XE_EXTERN {
#endif

	XE_LIB int _ldigits(unsigned long long num, int base, int sign);
	XE_LIB int _digits(unsigned num, int base, int sign);
	XE_LIB unsigned long long _str2num(const char* string, unsigned base, int sign, int* consumed);
	XE_LIB void _lnum2str(unsigned long long num, char* string, int base, int sign);
	XE_LIB void _dbl2str(double num, char* string, int roundPlaces);
	void _num2str(unsigned num, char* string, int base, int sign);
	XE_LIB void _flt2str(float num, char* string, int roundPlaces);
	double strtod(const char* nptr, char** endptr);
	XE_LIB float strtof(const char* nptr, char** endptr);
	int _xeprint(char* output, int outputlen, const char* format, va_list list);
	int _xeinput(const char* input, const char* format, va_list list);
#ifdef __cplusplus
}
#endif

#endif