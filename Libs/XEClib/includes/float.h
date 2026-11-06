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

#ifndef _FLOAT_H
#define _FLOAT_H

#define DBL_DIG		15 // <integer rvalue >= 10>
#define DBL_EPSILON 2.2204460492503131E-16
// <double rvalue <= 10^(-9)>
#define DBL_MANT_DIG 53 // <integer rvalue>
#define DBL_MAX		 1.7976931348623157E+308
// <double rvalue >= 10^37>
#define DBL_MAX_10_EXP 308	// <integer rvalue >= 37>
#define DBL_MAX_EXP	   1024 // <integer rvalue>
#define DBL_MIN		   2.2250738585072014E-308
// <double rvalue <= 10^(-37)>
#define DBL_MIN_10_EXP -307	 // <integer rvalue <= -37>
#define DBL_MIN_EXP	   -1021 // <integer rvalue>

#define FLT_DIG		   6				// <integer rvalue >= 10>
#define FLT_EPSILON	   1.19209290e-07F	// <double rvalue <= 10^(-9)>
#define FLT_MANT_DIG   24				// <integer rvalue>
#define FLT_MAX		   3.402823466E+38F // <float rvalue >= 10^37>
#define FLT_MAX_10_EXP 38				// <integer rvalue >= 37>
#define FLT_MAX_EXP	   128				// <integer rvalue>
#define FLT_MIN		   1.175494351E-38F // <float rvalue <= 10^(-37)>
#define FLT_MIN_10_EXP -37				// <integer rvalue <= -37>
#define FLT_MIN_EXP	   -125				// <integer rvalue>
#define FLT_RADIX	   2				// <#if expression >= 2>
#define FLT_ROUNDS	   1				// <integer rvalue>

#define LDBL_DIG	 18 // <integer rvalue >= 10>
#define LDBL_EPSILON 1.0842021724855044340075E-19L
// <long double rvalue <= 10^(-9)>
#define LDBL_MANT_DIG 64 // <integer rvalue>
#define LDBL_MAX	  1.1897314953572317650213E+4932L
// <long double rvalue >= 10^37>
#define LDBL_MAX_10_EXP 4932  // <integer rvalue >= 37>
#define LDBL_MAX_EXP	16384 // <integer rvalue>
#define LDBL_MIN		3.362103143112093506262677817321752603E-4932L
// <long double rvalue <= 10^(-37)>
#define LDBL_MIN_10_EXP 4931   // <integer rvalue <= -37>
#define LDBL_MIN_EXP	-16381 // <integer rvalue>

#endif
