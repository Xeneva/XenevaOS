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

#include <math.h>
#include <string.h>

#define itable ((double*)xtable)

static unsigned int xtable[16] = {0x540bcb0d,
								  0x3fe56936,
								  0x415a86d3,
								  0x3fe35800,
								  0xd9ac3519,
								  0x3fe1c80d,
								  0x34f91569,
								  0x3fe08be9,
								  0x8f3386d8,
								  0x3fee4794,
								  0x9ea02719,
								  0x3feb5b28,
								  0xe4ff9edc,
								  0x3fe92589,
								  0x1c52539d,
								  0x3fe76672};

double ceil(double d) {
	int c = (int)d;

	if (d > 0)
		d = (double)(c + 1);
	else if (d < 0)
		d = (double)c;
	return d;
}

double cos(double radians) {
	double result = 0;
	double sign = 0;
	double x2n = 0;
	double factorial = 0;
	double n, count;

	while (radians > (M_PI * 2))
		radians -= (M_PI * 2);
	while (radians < -(M_PI * 2))
		radians += (M_PI * 2);

	for (n = 0; n < 15; n += 1) {
		sign = 1.0;
		for (count = 0; count < n; count += 1)
			sign *= -1;

		x2n = 1.0;
		for (count = 0; count < (2 * n); count += 1)
			x2n *= radians;

		factorial = 1.0;
		for (count = (2 * n); count > 0; count -= 1)
			factorial *= count;

		if (factorial)
			result += (sign * (x2n / factorial));
	}

	return result;
}

float cosf(float radians) {
	float result = 0;
	float sign = 0;
	float x2n = 0;
	float factorial = 0;
	float n, count;

	while (radians > (M_PI * 2))
		radians -= (M_PI * 2);
	while (radians < -(M_PI * 2))
		radians += (M_PI * 2);

	for (n = 0; n < 10; n += 1) {
		sign = 1.0;
		for (count = 0; count < n; count += 1)
			sign *= -1;

		x2n = 1.0;
		for (count = 0; count < (2 * n); count += 1)
			x2n *= radians;

		factorial = 1.0;
		for (count = (2 * n); count > 0; count -= 1)
			factorial *= count;

		if (factorial)
			result += (sign * (x2n / factorial));
	}

	return (result);
}

double fabs(double x) {
	if (x < 0)
		return (x *= -1.0);
	else
		return x;
}

float fabsf(float x) {
	if (x < 0)
		return (x *= -1.0);
	else
		return x;
}

double floor(double d) {
	int i = (int)d;
	return ((double)i);
}

float floorf(float d) {
	int i = (int)d;
	return ((float)i);
}

double fmod(double x, double y) {
	double m = 0;

	if (!y) {
		m = 0;
	} else {
		m = (x - (floor(x / y) * y));
	}

	return m;
}

double modf(double x, double* pint) {
	int i;

	if (!pint)
		return 0;

	i = (int)x;
	*pint = (double)i;
	return (x - ((double)i));
}

double pow(double x, double y) {
	int count;

	if ((x < 0) && (floor(y) != y)) {
		return 0;
	}

	if (!y) {
		x = 1;
	} else {
		for (count = 1; count < y; count++)
			x *= x;
	}

	return x;
}

double sin(double radians) {
	double result = 0;
	double sign = 0;
	double x2nplus1 = 0;
	double factorial = 0;
	double n, count;

	while (radians > (M_PI * 2))
		radians -= (M_PI * 2);
	while (radians < -(M_PI * 2))
		radians += (M_PI * 2);

	for (n = 0; n < 15; n += 1) {
		sign = 1.0;
		for (count = 0; count < n; count += 1)
			sign *= -1;

		x2nplus1 = 1.0;
		for (count = 0; count < ((2 * n) + 1); count += 1)
			x2nplus1 *= radians;

		factorial = 1.0;
		for (count = ((2 * n) + 1); count > 0; count -= 1)
			factorial *= count;

		if (factorial)
			result += (sign * (x2nplus1 / factorial));
	}

	return result;
}

float sinf(float radians) {
	float result = 0;
	float sign = 0;
	float x2nplus1 = 0;
	float factorial = 0;
	float n, count;

	while (radians > (M_PI * 2))
		radians -= (M_PI * 2);
	while (radians < -(M_PI * 2))
		radians += (M_PI * 2);

	for (n = 0; n < 10; n += 1) {
		sign = 1.0;
		for (count = 0; count < n; count += 1)
			sign *= -1;

		x2nplus1 = 1.0;
		for (count = 0; count < ((2 * n) + 1); count += 1)
			x2nplus1 *= radians;

		factorial = 1.0;
		for (count = ((2 * n) + 1); count > 0; count -= 1)
			factorial *= count;

		if (factorial)
			result += (sign * (x2nplus1 / factorial));
	}

	return result;
}

static int norm2(double* t) {
	unsigned e, f, g;

	f = ((((unsigned*)t)[1]) >> 1);
	e = ((unsigned*)t)[1];
	f += 0x1FF80000;
	g = (e & 0x000FFFFF);
	f &= 0xFFF00000;
	((int*)t)[1] = g + 0x40000000 - (e & 0x00100000);

	return (f);
}

double sqrt(double y) {
	double a;
	int e, c;

	e = norm2(&y);
	c = (((int*)&y)[1]) >> (18) & (7);
	a = itable[c];

	for (c = 0; c < 6; c++)
		a = 0.5 * a * (3.0 - y * a * a);
	a *= y;

	((int*)&a)[1] &= 0x000FFFFF;
	((int*)&a)[1] |= e;

	return (a);
}

double tan(double radians) {
	return (sin(radians) / cos(radians));
}

float tanf(float radians) {
	return (sinf(radians) / cosf(radians));
}

float sqrtf(float x) {
	return (float)sqrt(x);
}

double frexp(double x, int* exp) {
	struct {
		uint32_t lsw;
		uint32_t msw;
	} extract;

	memcpy(&extract, &x, sizeof(double));

	*exp = ((extract.msw & 0x7ff00000) >> 20) - 0x3FE;

	struct {
		uint32_t lsw;
		uint32_t msw;
	} out_double;

	out_double.msw = (extract.msw & 0x800fffff) | 0x3FE00000;
	out_double.lsw = extract.lsw;

	double out;
	memcpy(&out, &out_double, sizeof(double));
	return out;
}

XE_EXTERN XE_EXPORT int fpclassify(double x) {
	typedef union {
		double asFloat;
		uint64_t asInt;
	} bits_t;
	bits_t bits;
	memset(&bits, 0, sizeof(bits_t));
	bits.asFloat = x;
	uint64_t exponent = (bits.asInt >> 52) & 0x7FF;
	uint64_t mantissa = (bits.asInt & 0xFffffFFFFffffULL);

	if (exponent == 0x7FF) {
		return mantissa ? FP_NAN : FP_INFINITE;
	} else if (exponent == 0) {
		return mantissa ? FP_SUBNORMAL : FP_ZERO;
	}

	return FP_NORMAL;
}

static inline int isnan_f(float x) {
	union {
		float f;
		uint32_t u;
	} v = {x};

	return ((v.u & 0x7F800000u) == 0x7F800000u) && (v.u & 0x007FFFFFu);
}

static inline float fmaxf_ieee(float a, float b) {
	if (isnan_f(a))
		return b;
	if (isnan_f(b))
		return a;

	if (a == b)
		return a + b;

	return (a > b) ? a : b;
}

static inline float fminf_ieee(float a, float b) {
	if (isnan_f(a))
		return b;
	if (isnan_f(b))
		return a;

	if (a == b)
		return a + b;

	return (a < b) ? a : b;
}

float fmaxf(float a, float b) {
	return fmaxf_ieee(a, b);
}

float fminf(float a, float b) {
	return fminf_ieee(a, b);
}