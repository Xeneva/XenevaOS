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

#ifndef __VALUES_H__
#define __VALUES_H__

#include <limits.h>
#include <float.h>

#define CHARBITS   (sizeof(char) * CHAR_BIT)
#define SHORTBITS  (sizeof(short int) * CHAR_BIT)
#define INTBITS	   (sizeof(int) * CHAR_BIT)
#define LONGBITS   (sizeof(long int) * CHAR_BIT)
#define PTRBITS	   (sizeof(char*) * CHAR_BIT)
#define DOUBLEBITS (sizeof(double) * CHAR_BIT)
#define FLOATBITS  (sizeof(float) * CHAR_BIT)

#define MINSHORT SHRT_MIN
#define MININT	 INT_MIN
#define MINLONG	 LONG_MIN

#define MAXSHORT SHRT_MAX
#define MAXINT	 INT_MAX
#define MAXLONG	 LONG_MAX

#define HIBITS MINSHORT
#define HIBITL MINLONG

#define MAXDOUBLE	DBL_MAX
#define MAXFLOAT	FLT_MAX
#define MINDOUBLE	DBL_MIN
#define MINFLOAT	FLT_MIN
#define DMINEXP		DBL_MIN_EXP
#define FMINEXP		FLT_MIN_EXP
#define DMAXEXP		DBL_MAX_EXP
#define FMAXEXP		FLT_MAX_EXP
#define BITSPERBYTE CHAR_BIT

#endif