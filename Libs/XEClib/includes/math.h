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

#ifndef __MATH_H__
#define __MATH_H__

#include <_xeneva.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
XE_EXTERN {
#endif

#define M_E		   2.7182818284590452354  // e
#define M_LOG2E	   1.4426950408889634074  // log_2 e
#define M_LOG10E   0.43429448190325182765 // log_10 e
#define M_LN2	   0.69314718055994530942 // log_e 2
#define M_LN10	   2.30258509299404568402 // log_e 10
#define M_PI	   3.14159265358979323846 // pi
#define M_PI_2	   1.57079632679489661923 // pi/2
#define M_PI_4	   0.78539816339744830962 // pi/4
#define M_1_PI	   0.31830988618379067154 // 1/pi
#define M_2_PI	   0.63661977236758134308 // 2/pi
#define M_2_SQRTPI 1.12837916709551257390 // 2/sqrt(pi)
#define M_SQRT2	   1.41421356237309504880 // sqrt(2)
#define M_SQRT1_2  0.70710678118654752440 // 1/sqrt(2)

	typedef union {
		float f;
		uint32_t i;
	} float_bits_t;

	typedef union {
		double d;
		uint64_t i;
	} double_bits_t;

	//#define asuint(f) ((union{float _f; uint32_t _i;}){f})._i
	static INLINE uint32_t asuint(float f) {
		float_bits_t u;
		memset(&u, 0, sizeof(float_bits_t));
		u.f = f;
		return u.i;
	}

	static INLINE float asfloat(uint32_t u) {
		float_bits_t fb;
		memset(&fb, 0, sizeof(float_bits_t));
		fb.i = u;
		return fb.f;
	}

	static INLINE uint64_t asuint64(double d) {
		double_bits_t db;
		memset(&db, 0, sizeof(double_bits_t));
		db.d = d;
		return db.i;
	}

	static INLINE double asdouble(uint64_t u) {
		double_bits_t db;
		memset(&db, 0, sizeof(double_bits_t));
		db.i = u;
		return db.d;
	}

#define EXTRACT_WORDS(hi, lo, d)                                                                   \
	do {                                                                                           \
		uint64_t __u = asuint64(d);                                                                \
		(hi) = __u >> 32;                                                                          \
		(lo) = (uint32_t)__u;                                                                      \
	} while (0)

#define GET_HIGH_WORD(hi, d)                                                                       \
	do {                                                                                           \
		(hi) = asuint64(d) >> 32;                                                                  \
	} while (0)

#define GET_LOW_WORD(lo, d)                                                                        \
	do {                                                                                           \
		(lo) = (uint32_t)asuint64(d);                                                              \
	} while (0)

#define INSERT_WORDS(d, hi, lo)                                                                    \
	do {                                                                                           \
		(d) = asdouble(((uint64_t)(hi) << 32) | (uint32_t)(lo));                                   \
	} while (0)

#define SET_HIGH_WORD(d, hi) INSERT_WORDS(d, hi, (uint32_t)asuint64(d))

#define SET_LOW_WORD(d, lo) INSERT_WORDS(d, asuint64(d) >> 32, lo)

#define GET_FLOAT_WORD(w, d)                                                                       \
	do {                                                                                           \
		(w) = asuint(d);                                                                           \
	} while (0)

#define SET_FLOAT_WORD(d, w)                                                                       \
	do {                                                                                           \
		(d) = asfloat(w);                                                                          \
	} while (0)

	enum { FP_NAN, FP_INFINITE, FP_ZERO, FP_SUBNORMAL, FP_NORMAL };

	static INLINE void fp_force_evalf(float x) {
		volatile float y;
		y = x;
	}

	static INLINE void fp_force_eval(double x) {
		volatile double y;
		y = x;
	}

	static INLINE void fp_force_evall(long double x) {
		volatile long double y;
		y = x;
	}
	XE_LIB int fpclassify(double x);

#define isfinite(x) ((fpclassify(x) != FP_NAN && fpclassify(x) != FP_INFINITE))
#define isnormal(x) (fpclassify(x) == FP_NORMAL)
#define isnan(x)	(fpclassify(x) == FP_NAN)
#define isinf(x)	(fpclassify(x) == FP_INFINITE)

#define FORCE_EVAL(x)                                                                              \
	do {                                                                                           \
		if (sizeof(x) == sizeof(float))                                                            \
			fp_force_evalf(x);                                                                     \
		else if (sizeof(x) == sizeof(double))                                                      \
			fp_force_eval(x);                                                                      \
		else                                                                                       \
			fp_force_evall(x);                                                                     \
	} while (0)

	XE_LIB double ceil(double);
	XE_LIB float ceilf(float x);
	XE_LIB double cos(double);
	XE_LIB float acosf(float x);
	XE_LIB float cosf(float);
	XE_LIB double fabs(double);
	XE_LIB float fabsf(float);
	XE_LIB double floor(double);
	XE_LIB float floorf(float);
	XE_LIB double fmod(double, double);
	XE_LIB double modf(double, double*);
	XE_LIB float fmodf(float x, float y);
	XE_LIB double pow(double, double);
	XE_LIB double sin(double);
	XE_LIB float sinf(float);
	XE_LIB double sqrt(double);
	XE_LIB float sqrtf(float x);
	XE_LIB double tan(double);
	XE_LIB float tanf(float);
	XE_LIB float atanf(float x);
	XE_LIB float atan2f(float y, float x);
	XE_LIB double frexp(double x, int* exp);
	XE_LIB float roundf(float x);
	XE_LIB float fmaxf(float a, float b);
	XE_LIB float fminf(float a, float b);

#ifdef __cplusplus
}
#endif

#endif