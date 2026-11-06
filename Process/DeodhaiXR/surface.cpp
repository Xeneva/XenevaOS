/**
* BSD 2-Clause License
*
* Copyright (c) 2022, Manas Kamal Choudhury
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

#include <stdint.h>
#include <sys/mman.h>
#include <chitralekha.h>
#include "_fastcpy.h"
#include <color.h>
#include "surface.h"
#include "alpha.h"

uint32_t* backSurface;
static uint32_t* screenBlur;
static uint32_t* screenBlurTmp;
static int screenBlurW;
static int screenBlurH;

/*
 * DeodhaiBackSurfaceUpdate -- update the back surface
 */
void DeodhaiBackSurfaceUpdate(ChCanvas* canv, int x, int y, int w, int h) {
	uint32_t* lfb = (uint32_t*)canv->buffer;
	uint32_t* wallp = (uint32_t*)backSurface;

	/* Damage rectangles can be merged while windows are toggled quickly. Clamp
	 * the rectangle before taking any pointers, and use the clamped values for
	 * both source and destination so restore never walks outside the canvas --axiss */
	if (x < 0) {
		w += x;
		x = 0;
	}
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (x >= (int)canv->canvasWidth || y >= (int)canv->canvasHeight)
		return;
	if (w > (int)canv->canvasWidth - x)
		w = (int)canv->canvasWidth - x;
	if (h > (int)canv->canvasHeight - y)
		h = (int)canv->canvasHeight - y;
	if (w <= 0 || h <= 0)
		return;

	for (int j = 0; j < h; j++) {
		_fastcpy(canv->buffer + (y + j) * canv->canvasWidth + x,
				 wallp + (y + j) * canv->canvasWidth + x,
				 (size_t)w * 4);
	}
}

/*
 * DeoInitializeBackSurface -- initialize the backsurface
 * @param canv -- Pointer to system canvas
 */
void DeoInitializeBackSurface(ChCanvas* canv) {
	backSurface = NULL;
	/* allocate a surface buffer */
	backSurface =
		(uint32_t*)_KeMemMap(NULL,
							 static_cast<size_t>(canv->screenWidth) * canv->screenHeight * 4,
							 0,
							 0,
							 MEMMAP_NO_FILEDESC,
							 0);
	for (int i = 0; i < canv->screenWidth; i++)
		for (int j = 0; j < canv->screenHeight; j++)
			backSurface[j * canv->canvasWidth + i] = GRAY; //0xFF938585;
	DeodhaiBackSurfaceUpdate(canv, 0, 0, canv->screenWidth, canv->screenHeight);
}

uint32_t* DeoGetBackSurface() {
	return backSurface;
}

void DeoBakeScreenBlur(int canvas_w, int canvas_h) {
	if (!backSurface || canvas_w <= 0 || canvas_h <= 0)
		return;
	size_t bytes = (size_t)canvas_w * (size_t)canvas_h * 4;
	if (!screenBlur || screenBlurW != canvas_w || screenBlurH != canvas_h) {
		if (screenBlur)
			_KeMemUnmap(screenBlur, (size_t)screenBlurW * (size_t)screenBlurH * 4);
		if (screenBlurTmp)
			_KeMemUnmap(screenBlurTmp, (size_t)screenBlurW * (size_t)screenBlurH * 4);
		screenBlur = (uint32_t*)_KeMemMap(NULL, bytes, 0, 0, MEMMAP_NO_FILEDESC, 0);
		screenBlurTmp = (uint32_t*)_KeMemMap(NULL, bytes, 0, 0, MEMMAP_NO_FILEDESC, 0);
		screenBlurW = canvas_w;
		screenBlurH = canvas_h;
	}
	if (!screenBlur || !screenBlurTmp)
		return;
	glass_precompute_blur(
		screenBlur, screenBlurTmp, backSurface, canvas_w, canvas_h, 0, 0, canvas_w, canvas_h, 4);
	/* Boot-time diagnostic for the glass-darkening reports: mean channel
	 * average of the blur source vs the baked blur, sampled on a stride so
	 * it costs nothing measurable. The glass blend is anchored on
	 * screenBlur, so `back` bright + `blur` dark here means the bake read
	 * the wrong surface; both bright means the runtime input was fine and
	 * the fault is downstream. One line per boot on serial. --axiss */
	{
		unsigned long accBack = 0, accBlur = 0;
		int n = 0;
		for (int sy = 0; sy < canvas_h; sy += 16) {
			for (int sx = 0; sx < canvas_w; sx += 16) {
				uint32_t p = backSurface[(size_t)sy * canvas_w + sx];
				uint32_t q = screenBlur[(size_t)sy * canvas_w + sx];
				accBack += (((p >> 16) & 0xFF) + ((p >> 8) & 0xFF) + (p & 0xFF)) / 3;
				accBlur += (((q >> 16) & 0xFF) + ((q >> 8) & 0xFF) + (q & 0xFF)) / 3;
				n++;
			}
		}
		if (n > 0)
			_KePrint("[deodhaiXR]: blurcheck back=%d blur=%d n=%d\r\n",
					 (int)(accBack / (unsigned long)n),
					 (int)(accBlur / (unsigned long)n),
					 n);
	}
}

uint32_t* DeoGetScreenBlur() {
	return screenBlur;
}
