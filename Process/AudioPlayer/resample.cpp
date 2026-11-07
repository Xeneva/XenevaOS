/**
* @file resample.cpp
*
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

#include "accent.h"

/**
 * @brief AccentResamplerInitialize -- initialize the resampler library
 * @param r -- Pointer to resampler
 * @param in_hz -- input frequency
 * @param out_hz -- output_frequency
 */
void AccentResamplerInitialize(AccentResampler* r, uint32_t in_hz, uint32_t out_hz) {
	r->step = (uint32_t)(((uint64_t)in_hz << 16) / out_hz);
	r->pos = 0;
	r->init = 0;
}

/**
 * @brief AccentResamplerProcess -- takes input sample of 'a' frequency and
 * process it out to 'b' frequency
 * @param r -- Pointer to resampler structure
 * @param in -- Input PCM data
 * @param n -- Input sample bytes
 * @param out -- Pointer to memory for Out PCM
 */
size_t AccentResamplerProcess(AccentResampler* r, const int16_t* in, size_t n, int16_t* out) {
	if (!n)
		return 0;
	if (!r->init) {
		r->prev[0] = in[0];
		r->prev[1] = in[1];
		r->init = 1;
	}

	size_t o = 0;
	while (((r->pos >> 16) + 1) <= n) {
		uint32_t i = r->pos >> 16;
		int32_t f = r->pos & 0xFFFF;
		for (int c = 0; c < 2; c++) {
			int32_t a = (i == 0) ? r->prev[c] : in[(i - 1) * 2 + c];
			int32_t b = in[i * 2 + c];
			/* 64-bit: (b - a) * f overflows int32 once |b - a| > 32767 */
			out[o * 2 + c] = (int16_t)(a + (int32_t)(((int64_t)(b - a) * f) >> 16));
		}
		o++;
		r->pos += r->step;
	}
	r->pos -= (uint32_t)n << 16;
	r->prev[0] = in[(n - 1) * 2];
	r->prev[1] = in[(n - 1) * 2 + 1];
	return o;
}