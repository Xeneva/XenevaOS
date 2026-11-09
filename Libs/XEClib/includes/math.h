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

typedef double double_t;
typedef float float_t;

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

#define HUGE_VAL   (__builtin_huge_val())
#define HUGE_VALF  (__builtin_huge_valf())
#define INFINITY   (__builtin_inff())
#define NAN	       (__builtin_nanf(""))

#define FP_ILOGB0   (-1 - 0x7fffffff)
#define FP_ILOGBNAN (-1 - 0x7fffffff)

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

#define signbit(x) ((asuint64((double)(x)) >> 63) != 0)

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

	/* --- absolute value, rounding, decomposition ------------------------- */
	XE_LIB double fabs(double);
	XE_LIB float fabsf(float);
	XE_LIB double floor(double);
	XE_LIB float floorf(float);
	XE_LIB double ceil(double);
	XE_LIB float ceilf(float);
	XE_LIB double trunc(double);
	XE_LIB float truncf(float);
	XE_LIB double round(double);
	XE_LIB float roundf(float);
	XE_LIB double rint(double);
	XE_LIB double nearbyint(double);
	XE_LIB long lround(double);
	XE_LIB long long llround(double);
	XE_LIB double copysign(double, double);
	XE_LIB float copysignf(float, float);
	XE_LIB double fmod(double, double);
	XE_LIB float fmodf(float x, float y);
	XE_LIB double modf(double, double*);
	XE_LIB float modff(float, float*);
	XE_LIB double frexp(double x, int* exp);
	XE_LIB float frexpf(float x, int* exp);
	XE_LIB double ldexp(double x, int exp);
	XE_LIB float ldexpf(float x, int exp);
	XE_LIB double scalbn(double x, int exp);
	XE_LIB float scalbnf(float x, int exp);

	/* --- exponential and logarithmic ------------------------------------- */
	XE_LIB double exp(double);
	XE_LIB float expf(float);
	XE_LIB double exp2(double);
	XE_LIB float exp2f(float);
	XE_LIB double log(double);
	XE_LIB float logf(float);
	XE_LIB double log2(double);
	XE_LIB float log2f(float);
	XE_LIB double log10(double);
	XE_LIB float log10f(float);
	XE_LIB double pow(double, double);
	XE_LIB float powf(float, float);
	XE_LIB double sqrt(double);
	XE_LIB float sqrtf(float x);
	XE_LIB double cbrt(double);
	XE_LIB float cbrtf(float);
	XE_LIB double hypot(double, double);
	XE_LIB float hypotf(float, float);
	XE_LIB double expm1(double);
	XE_LIB float expm1f(float);
	XE_LIB double log1p(double);
	XE_LIB float log1pf(float);

	/* --- hyperbolic ------------------------------------------------------- */
	XE_LIB double sinh(double);
	XE_LIB float sinhf(float);
	XE_LIB double cosh(double);
	XE_LIB float coshf(float);
	XE_LIB double tanh(double);
	XE_LIB float tanhf(float);
	XE_LIB double asinh(double);
	XE_LIB float asinhf(float);
	XE_LIB double acosh(double);
	XE_LIB float acoshf(float);
	XE_LIB double atanh(double);
	XE_LIB float atanhf(float);

	/* --- error function, gamma function ---------------------------------- */
	XE_LIB double erf(double);
	XE_LIB float erff(float);
	XE_LIB double erfc(double);
	XE_LIB float erfcf(float);
	XE_LIB double tgamma(double);
	XE_LIB float tgammaf(float);
	XE_LIB double lgamma(double);
	XE_LIB float lgammaf(float);
	XE_LIB double lgamma_r(double, int*);
	XE_LIB float lgammaf_r(float, int*);
	extern int __signgam;
#define signgam __signgam

	/* --- IEEE remainder, neighbours, fused multiply-add ------------------- */
	XE_LIB double remainder(double, double);
	XE_LIB float remainderf(float, float);
	XE_LIB double remquo(double, double, int*);
	XE_LIB float remquof(float, float, int*);
	XE_LIB double nextafter(double, double);
	XE_LIB float nextafterf(float, float);
	XE_LIB double fma(double, double, double);
	XE_LIB float fmaf(float, float, float);

	/* --- exponent extraction, long conversions ---------------------------- */
	XE_LIB double logb(double);
	XE_LIB float logbf(float);
	XE_LIB int ilogb(double);
	XE_LIB int ilogbf(float);
	XE_LIB long lrint(double);
	XE_LIB long long llrint(double);
	XE_LIB long lrintf(float);
	XE_LIB long long llrintf(float);
	XE_LIB long lroundf(float);
	XE_LIB long long llroundf(float);
	XE_LIB double scalbln(double, long);
	XE_LIB float scalblnf(float, long);
	XE_LIB double nan(const char*);
	XE_LIB float nanf(const char*);

	/* --- trigonometric ---------------------------------------------------- */
	XE_LIB double sin(double);
	XE_LIB float sinf(float);
	XE_LIB double cos(double);
	XE_LIB float cosf(float);
	XE_LIB double tan(double);
	XE_LIB float tanf(float);
	XE_LIB double asin(double);
	XE_LIB float asinf(float x);
	XE_LIB double acos(double);
	XE_LIB float acosf(float x);
	XE_LIB double atan(double);
	XE_LIB float atanf(float x);
	XE_LIB double atan2(double y, double x);
	XE_LIB float atan2f(float y, float x);

	/* --- min/max ----------------------------------------------------------- */
	XE_LIB double fmax(double, double);
	XE_LIB float fmaxf(float a, float b);
	XE_LIB double fmin(double, double);
	XE_LIB float fminf(float a, float b);
	XE_LIB double fdim(double, double);
	XE_LIB float fdimf(float, float);

#ifdef __cplusplus
}
#endif

#endif
