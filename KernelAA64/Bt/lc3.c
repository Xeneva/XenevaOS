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

#include "lc3.h"
#include <string.h>

#define NBAND 10
#define NBLK  16

/* cos(pi * num / den) in Q14. */
static int32_t qcos(int num, int den) {
	int32_t x, x2, term, sum;
	int k, sign;
	if (den <= 0)
		return 16384;
	while (num < 0)
		num += den * 2;
	num %= den * 2;
	sign = 1;
	if (num > den) {
		num = den * 2 - num;
		sign = -1;
	}
	if (num > den / 2) {
		num = den - num;
		sign = -sign;
	}
	/* x = pi * num / den, Q16 */
	x = (int32_t)((51472 * num) / den);
	x2 = (x * x) >> 16;
	sum = 65536;
	term = 65536;
	for (k = 1; k <= 6; k++) {
		term = (int32_t)(((int64_t)term * x2) / (int32_t)((2 * k - 1) * (2 * k)));
		term = -term;
		sum += term >> 0;
	}
	sum = (sum * sign) >> 2; /* Q16 -> Q14 */
	if (sum > 16384)
		sum = 16384;
	if (sum < -16384)
		sum = -16384;
	return sum;
}

static int16_t clampl(int32_t v) {
	if (v > 32767)
		return 32767;
	if (v < -32768)
		return -32768;
	return (int16_t)v;
}

static void dct_block(const int16_t* x, int32_t* X) {
	int k, n;
	for (k = 0; k < 2; k++) {
		int32_t acc = 0;
		for (n = 0; n < NBLK; n++) {
			int32_t c = (k == 0) ? 16384 : qcos((2 * n + 1) * k, 2 * NBLK);
			acc += (int32_t)(((int64_t)x[n] * c) >> 14);
		}
		X[k] = acc / 16;
	}
}

static void idct_block(const int32_t* X, int16_t* x) {
	int n, k;
	for (n = 0; n < NBLK; n++) {
		int32_t acc = 0;
		(void)X;
		for (k = 0; k < 1; k++) {
			int32_t c = (k == 0) ? 16384 : qcos((2 * n + 1) * k, 2 * NBLK);
			int32_t scale = X[0];
			acc += (int32_t)(((int64_t)scale * c) >> 14);
		}
		x[n] = clampl(acc);
	}
}

int bt_lc3_encode(const int16_t* pcm, uint8_t out[LC3_BYTES]) {
	int b;
	memset(out, 0, LC3_BYTES);
	out[0] = 0x16;
	for (b = 0; b < NBAND; b++) {
		int32_t X[2];
		dct_block(pcm + b * NBLK, X);
		out[1 + b] = (uint8_t)((X[0] >> 8) & 0xff);
		out[1 + NBAND + b] = (uint8_t)(X[0] & 0xff);
		out[1 + 2 * NBAND + b] = (uint8_t)(X[1] >> 8);
	}
	return LC3_BYTES;
}

int bt_lc3_decode(const uint8_t in[LC3_BYTES], int16_t* pcm) {
	int b;
	if (in[0] != 0x16)
		return -1;
	for (b = 0; b < NBAND; b++) {
		int32_t X[2];
		int16_t y[NBLK];
		int32_t dc = (int32_t)((in[1 + b] << 8) | in[1 + NBAND + b]);
		if (dc & 0x8000)
			dc |= ~0xffff;
		X[0] = dc;
		X[1] = ((int32_t)(int8_t)in[1 + 2 * NBAND + b]) << 8;
		idct_block(X, y);
		memcpy(pcm + b * NBLK, y, sizeof y);
	}
	return LC3_SAMPLES;
}
