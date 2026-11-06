/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2025, Manas Kamal Choudhury
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

static const float pi = 3.1415927410e+00, /* 0x40490fdb */
	pi_lo = -8.7422776573e-08;			  /* 0xb3bbbd2e */

float atan2f(float y, float x) {
	float z;
	uint32_t m, ix, iy;

	if (isnan(x) || isnan(y))
		return x + y;
	GET_FLOAT_WORD(ix, x);
	GET_FLOAT_WORD(iy, y);
	if (ix == 0x3f800000) /* x=1.0 */
		return atanf(y);
	m = ((iy >> 31) & 1) | ((ix >> 30) & 2); /* 2*sign(x)+sign(y) */
	ix &= 0x7fffffff;
	iy &= 0x7fffffff;

	/* when y = 0 */
	if (iy == 0) {
		switch (m) {
		case 0:
		case 1:
			return y; /* atan(+-0,+anything)=+-0 */
		case 2:
			return pi; /* atan(+0,-anything) = pi */
		case 3:
			return -pi; /* atan(-0,-anything) =-pi */
		}
	}
	/* when x = 0 */
	if (ix == 0)
		return m & 1 ? -pi / 2 : pi / 2;
	/* when x is INF */
	if (ix == 0x7f800000) {
		if (iy == 0x7f800000) {
			switch (m) {
			case 0:
				return pi / 4; /* atan(+INF,+INF) */
			case 1:
				return -pi / 4; /* atan(-INF,+INF) */
			case 2:
				return 3 * pi / 4; /*atan(+INF,-INF)*/
			case 3:
				return -3 * pi / 4; /*atan(-INF,-INF)*/
			}
		} else {
			switch (m) {
			case 0:
				return 0.0f; /* atan(+...,+INF) */
			case 1:
				return -0.0f; /* atan(-...,+INF) */
			case 2:
				return pi; /* atan(+...,-INF) */
			case 3:
				return -pi; /* atan(-...,-INF) */
			}
		}
	}
	/* |y/x| > 0x1p26 */
	if (ix + (26 << 23) < iy || iy == 0x7f800000)
		return m & 1 ? -pi / 2 : pi / 2;

	/* z = atan(|y/x|) with correct underflow */
	if ((m & 2) && iy + (26 << 23) < ix) /*|y/x| < 0x1p-26, x < 0 */
		z = 0.0;
	else
		z = atanf(fabsf(y / x));
	switch (m) {
	case 0:
		return z; /* atan(+,+) */
	case 1:
		return -z; /* atan(-,+) */
	case 2:
		return pi - (z - pi_lo); /* atan(+,-) */
	default:					 /* case 3 */
		return (z - pi_lo) - pi; /* atan(-,-) */
	}
}