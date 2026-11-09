/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2026, Manas Kamal Choudhury
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

/*
 * Float entry points and classification helpers.
 *
 * The double-precision functions live in the math/ directory and come from
 * musl (MIT-licensed, Copyright (c) 2018 Arm Limited and other musl
 * contributors).  Most float counterparts are wrappers that compute in
 * double and round once to float; nextafterf and fmaf need float-specific
 * implementations and live in math/ as well.
 *
 * acosf, atanf, atan2f, ceilf, fmodf and roundf have dedicated fdlibm
 * implementations in the parent directory and must not be defined here.
 */

#include <fenv.h>
#include <math.h>
#include <string.h>

/* Private reentrant gamma helper, defined in math/lgamma_r.c. */
extern "C" double __lgamma_r(double, int*);

/* --- classification ---------------------------------------------------- */

XE_EXTERN XE_EXPORT int fpclassify(double x) {
	double_bits_t bits;

	memset(&bits, 0, sizeof(bits));
	bits.d = x;
	uint64_t exponent = (bits.i >> 52) & 0x7FF;
	uint64_t mantissa = (bits.i & 0xFFFFFFFFFFFFFULL);

	if (exponent == 0x7FF)
		return mantissa ? FP_NAN : FP_INFINITE;
	if (exponent == 0)
		return mantissa ? FP_SUBNORMAL : FP_ZERO;

	return FP_NORMAL;
}

/* --- absolute value and rounding --------------------------------------- */

float fabsf(float x) {
	return (float)fabs((double)x);
}

float floorf(float x) {
	return (float)floor((double)x);
}

float truncf(float x) {
	return (float)trunc((double)x);
}

double nearbyint(double x) {
	/* nearbyint differs from rint only in that it must not raise the
	 * inexact exception.  rint rounds in hardware, which does raise it,
	 * so preserve the caller's flag across the call. */
	int inexact = fetestexcept(FE_INEXACT);

	feclearexcept(FE_INEXACT);
	double y = rint(x);
	if (inexact)
		feraiseexcept(FE_INEXACT);
	else
		feclearexcept(FE_INEXACT);

	return y;
}

/* --- decomposition ----------------------------------------------------- */

float modff(float x, float* iptr) {
	double ip;
	double fp = modf((double)x, &ip);

	if (iptr)
		*iptr = (float)ip;
	return (float)fp;
}

float frexpf(float x, int* exp) {
	return (float)frexp((double)x, exp);
}

float ldexpf(float x, int exp) {
	return (float)ldexp((double)x, exp);
}

float scalbnf(float x, int exp) {
	return (float)scalbn((double)x, exp);
}

/* --- exponential and logarithmic --------------------------------------- */

float expf(float x) {
	return (float)exp((double)x);
}

float exp2f(float x) {
	return (float)exp2((double)x);
}

float logf(float x) {
	return (float)log((double)x);
}

float log2f(float x) {
	return (float)log2((double)x);
}

float log10f(float x) {
	return (float)log10((double)x);
}

float powf(float x, float y) {
	return (float)pow((double)x, (double)y);
}

float sqrtf(float x) {
	return (float)sqrt((double)x);
}

float cbrtf(float x) {
	return (float)cbrt((double)x);
}

float hypotf(float x, float y) {
	return (float)hypot((double)x, (double)y);
}

float expm1f(float x) {
	return (float)expm1((double)x);
}

float log1pf(float x) {
	return (float)log1p((double)x);
}

/* --- trigonometric ------------------------------------------------------ */

float sinf(float x) {
	return (float)sin((double)x);
}

float cosf(float x) {
	return (float)cos((double)x);
}

float tanf(float x) {
	return (float)tan((double)x);
}

float asinf(float x) {
	return (float)asin((double)x);
}

float copysignf(float x, float y) {
	return (float)copysign((double)x, (double)y);
}

/* --- hyperbolic --------------------------------------------------------- */

float sinhf(float x) {
	return (float)sinh((double)x);
}

float coshf(float x) {
	return (float)cosh((double)x);
}

float tanhf(float x) {
	return (float)tanh((double)x);
}

float asinhf(float x) {
	return (float)asinh((double)x);
}

float acoshf(float x) {
	return (float)acosh((double)x);
}

float atanhf(float x) {
	return (float)atanh((double)x);
}

/* --- error function, gamma function ------------------------------------- */

float erff(float x) {
	return (float)erf((double)x);
}

float erfcf(float x) {
	return (float)erfc((double)x);
}

float tgammaf(float x) {
	return (float)tgamma((double)x);
}

float lgammaf(float x) {
	return (float)lgamma((double)x);
}

double lgamma_r(double x, int* sign) {
	return __lgamma_r(x, sign);
}

float lgammaf_r(float x, int* sign) {
	return (float)__lgamma_r((double)x, sign);
}

/* --- remainder, neighbours, fused multiply-add -------------------------- */

float remainderf(float x, float y) {
	return (float)remainder((double)x, (double)y);
}

float remquof(float x, float y, int* quo) {
	return (float)remquo((double)x, (double)y, quo);
}

/* --- exponent extraction, long conversions ------------------------------ */

float logbf(float x) {
	return (float)logb((double)x);
}

int ilogbf(float x) {
	return ilogb((double)x);
}

long lrintf(float x) {
	return (long)rint((double)x);
}

long long llrintf(float x) {
	return (long long)rint((double)x);
}

long lroundf(float x) {
	return (long)round((double)x);
}

long long llroundf(float x) {
	return (long long)round((double)x);
}

float scalblnf(float x, long n) {
	return (float)scalbln((double)x, n);
}

float nanf(const char* s) {
	(void)s;
	return (float)nan(s);
}

/* --- min/max and positive difference ------------------------------------ */

float fmaxf(float a, float b) {
	if (isnan(a))
		return b;
	if (isnan(b))
		return a;
	if (a == b)
		return a + b;
	return (a > b) ? a : b;
}

float fminf(float a, float b) {
	if (isnan(a))
		return b;
	if (isnan(b))
		return a;
	if (a == b)
		return a + b;
	return (a < b) ? a : b;
}

float fdimf(float x, float y) {
	if (isnan(x))
		return x;
	if (isnan(y))
		return y;
	return x > y ? x - y : 0.0f;
}
