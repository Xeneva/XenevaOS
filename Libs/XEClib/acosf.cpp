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
#include <stdint.h>

static const float pio2_hi = 1.5707962513e+00, /* 0x3fc90fda */
	pio2_lo = 7.5497894159e-08,				   /* 0x33a22168 */
	pS0 = 1.6666586697e-01, pS1 = -4.2743422091e-02, pS2 = -8.6563630030e-03,
				   qS1 = -7.0662963390e-01;

static float R(float z) {
	float p, q;
	p = z * (pS0 + z * (pS1 + z * pS2));
	q = 1.0f + z * qS1;
	return p / q;
}

float acosf(float x) {
	float z, w, s, c, df;
	uint32_t hx, ix;

	GET_FLOAT_WORD(hx, x);
	ix = hx & 0x7fffffff;
	/* |x| >= 1 or nan */
	if (ix >= 0x3f800000) {
		if (ix == 0x3f800000) {
			if (hx >> 31)
				return 2 * pio2_hi + 0x1p-120f;
			return 0;
		}
		return 0 / (x - x);
	}
	/* |x| < 0.5 */
	if (ix < 0x3f000000) {
		if (ix <= 0x32800000) /* |x| < 2**-26 */
			return pio2_hi + 0x1p-120f;
		return pio2_hi - (x - (pio2_lo - x * R(x * x)));
	}
	/* x < -0.5 */
	if (hx >> 31) {
		z = (1 + x) * 0.5f;
		s = sqrtf(z);
		w = R(z) * s - pio2_lo;
		return 2 * (pio2_hi - (s + w));
	}
	/* x > 0.5 */
	z = (1 - x) * 0.5f;
	s = sqrtf(z);
	GET_FLOAT_WORD(hx, s);
	SET_FLOAT_WORD(df, hx & 0xfffff000);
	c = (z - df * df) / (s + df);
	w = R(z) * s + c;
	return 2 * (df + w);
}