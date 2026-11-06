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

#ifndef _LIMITS_H
#define _LIMITS_H

#define CHAR_BIT 8 // Bits in a char

#define SCHAR_MAX 127  // Max value of a signed char
#define SCHAR_MIN -128 // Min value of a signed char

// Assumes that a char type is signed
#define CHAR_MAX  SCHAR_MAX // Max value of a char
#define CHAR_MIN  SCHAR_MIN // Min value of a char
#define UCHAR_MAX 255		// Max value of an unsigned char

// Assumes that a short int type is 16 bits
#define SHRT_MAX  32767	 // Max value of a short int
#define SHRT_MIN  -32768 // Min value of a short int
#define USHRT_MAX 65535	 // Max value of an unsigned short

// Assumes that an int type is 32 bits
#define INT_MAX	 2147483647		// Max value of an int
#define INT_MIN	 (-INT_MAX - 1) // Min value of an int
#define UINT_MAX 4294967295U	// Max value of an unsigned int

// Assumes that a long int type is 32 bits
#define LONG_MAX  2147483647L	   // Max value of a long int
#define LONG_MIN  (-LONG_MAX - 1L) // Min value of a long int
#define ULONG_MAX 4294967295UL	   // Max value of an unsigned long

// Maximum multibyte characters length
#define MB_LEN_MAX 4

#endif