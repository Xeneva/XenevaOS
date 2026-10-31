/**
* @file accent.h
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

#ifndef __ACCENT_H__
#define __ACCENT_H__

#include <stdint.h>

/**
 * Resampler structure
 */
typedef struct {
	uint32_t step;
	uint32_t pos;
	int16_t prev[2];
	int init;
} AccentResampler;

/**
 * @brief AccentResamplerInitialize -- initialize the resampler library
 * @param r -- Pointer to resampler
 * @param in_hz -- input frequency
 * @param out_hz -- output_frequency
 */
extern void AccentResamplerInitialize(AccentResampler* r, uint32_t in_hz, uint32_t out_hz);

/**
 * @brief AccentResamplerProcess -- takes input sample of 'a' frequency and
 * process it out to 'b' frequency
 * @param r -- Pointer to resampler structure
 * @param in -- Input PCM data
 * @param n -- Input sample bytes
 * @param out -- Pointer to memory for Out PCM
 */
extern size_t AccentResamplerProcess(AccentResampler* r, const int16_t* in, size_t n, int16_t* out);
#endif