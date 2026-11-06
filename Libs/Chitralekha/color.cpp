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

#include "color.h"
#include "draw.h"
#include <math.h>
#include <stdlib.h>

static const int AMASK = 0xFF000000;
static const int RBMASK = 0x00FF00FF;
static const int GMASK = 0x0000FF00;
static const int AGMASK = AMASK | GMASK;
static const int ONEALPHA = 0x01000000;

/*
 * ChColorAlphaBlend -- blend two color
 * @param color1 -- color 1
 * @param color2 -- color 2
 * @param opacity -- opacity of alpha
 */
uint32_t ChColorAlphaBlend(uint32_t dst, uint32_t src, double opacity) {
	/*int oldB = oldColor & 0xFF;
	int oldG = (oldColor >> 8) & 0xFF;
	int oldR = (oldColor >> 16) & 0xff;
	uint8_t r = GET_RED(color2);
	uint8_t b = GET_BLUE(color2);
	uint8_t g = GET_GREEN(color2);
	uint32_t newColor = (int)(b* opacity + oldB * (1 - opacity)) |
		(((int)(g * opacity + oldG * (1 - opacity)) << 8)) |
		(((int)(r * opacity + oldR * (1 - opacity)) << 16));
	return newColor;*/
	uint8_t dstR = (dst >> 16) & 0xFF;
	uint8_t dstG = (dst >> 8) & 0xFF;
	uint8_t dstB = dst & 0xFF;

	uint8_t srcR = (src >> 16) & 0xFF;
	uint8_t srcG = (src >> 8) & 0xFF;
	uint8_t srcB = src & 0xFF;

	uint8_t outR = (uint8_t)(srcR * opacity + dstR * (1.0f - opacity));
	uint8_t outG = (uint8_t)(srcG * opacity + dstG * (1.0f - opacity));
	uint8_t outB = (uint8_t)(srcB * opacity + dstB * (1.0f - opacity));

	return (0xFF << 24) | (outR << 16) | (outG << 8) | outB;
}

uint32_t ChColorAlphaBlend2(uint32_t color1, uint32_t color2) {
	uint32_t a = (color2 & AMASK) >> 24;
	if (a == 0)
		return color1;
	else if (a == 255)
		return color2;
	else {
		uint32_t na = 255 - a;
		uint32_t rb = ((na * (color1 & RBMASK)) + (a * (color2 & RBMASK))) >> 8;
		uint32_t ag = (na * ((color1 & AGMASK) >> 8)) + (a * (ONEALPHA | ((color2 & GMASK) >> 8)));

		return ((rb & RBMASK) | (ag & AGMASK));
	}
}

uint32_t make_col(uint8_t red, uint8_t green, uint8_t blue) {
	red = max(red, 0);
	red = min(red, 255);
	green = max(green, 0);
	green = min(green, 255);
	blue = max(blue, 0);
	blue = min(blue, 255);

	uint32_t ret = (red << 16) | (green << 8) | (blue << 0);
	return ret;
}

uint32_t make_col_a(uint8_t red, uint8_t green, uint8_t blue, uint8_t a) {
	red = max(red, 0);
	red = min(red, 255);
	green = max(green, 0);
	green = min(green, 255);
	blue = max(blue, 0);
	blue = min(blue, 255);
	a = max(a, 0);
	a = min(a, 255);

	uint32_t ret = (a << 24) | (red << 16) | (green << 8) | (blue << 0);
	return ret;
}

/*
 * ChColorDrawVerticalGradient -- draws vertical linear gradient
 * @param canv -- Pointer to canvas
 * @param x -- X coord of the rect
 * @param y -- Y coord of the rect
 * @param w -- Width of the rect
 * @param h -- Height of the rect
 * @param color1 -- starting color
 * @param color2 -- end color
 */
void ChColorDrawVerticalGradient(
	ChCanvas* canv, int x, int y, int w, int h, uint32_t color1, uint32_t color2) {
	uint8_t r1 = GET_RED(color1);
	uint8_t g1 = GET_GREEN(color1);
	uint8_t b1 = GET_BLUE(color1);
	uint8_t a1 = GET_ALPHA(color1);

	uint8_t r2 = GET_RED(color2);
	uint8_t g2 = GET_GREEN(color2);
	uint8_t b2 = GET_BLUE(color2);
	uint8_t a2 = GET_ALPHA(color2);

	for (int j = 0; j < h; j++) {
		uint8_t a = (uint8_t)(j * (((double)a2 - a1) / h) + a1);
		uint8_t red = (uint8_t)(j * (((double)r2 - r1) / h) + r1);
		uint8_t green = (uint8_t)(j * (((double)g2 - g1) / h) + g1);
		uint8_t blue = (uint8_t)(j * (((double)b2 - b1) / h) + b1);
		uint32_t col = (a << 24) | (red << 16) | (green << 8) | (blue << 0);
		ChDrawRect(canv, x, y + j, w, 1, col);
	}
}

/*
* ChColorDrawHorizontalGradient -- draws horizontal linear gradient
* @param canv -- Pointer to canvas
* @param x -- X coord of the rect
* @param y -- Y coord of the rect
* @param w -- Width of the rect
* @param h -- Height of the rect
* @param color1 -- starting color
* @param color2 -- end color
*/
void ChColorDrawHorizontalGradient(
	ChCanvas* canv, int x, int y, int w, int h, uint32_t color1, uint32_t color2) {
	uint8_t r1 = GET_RED(color1);
	uint8_t g1 = GET_GREEN(color1);
	uint8_t b1 = GET_BLUE(color1);
	uint8_t a1 = GET_ALPHA(color1);

	uint8_t r2 = GET_RED(color2);
	uint8_t g2 = GET_GREEN(color2);
	uint8_t b2 = GET_BLUE(color2);
	uint8_t a2 = GET_ALPHA(color2);

	for (int j = 0; j < w; j++) {
		uint8_t a = (uint8_t)(j * (((double)a2 - a1) / w) + a1);
		uint8_t red = (uint8_t)(j * (((double)r2 - r1) / w) + r1);
		uint8_t green = (uint8_t)(j * (((double)g2 - g1) / w) + g1);
		uint8_t blue = (uint8_t)(j * (((double)b2 - b1) / w) + b1);
		uint32_t col = (a << 24) | (red << 16) | (green << 8) | (blue << 0);
		ChDrawRect(canv, x + j, y, 1, h, col);
	}
}

double clamp(double x, double upper, double lower) {
	return min(upper, max(x, lower));
}

/*
 * ChColorRGBAtoBGRA -- convert RGBA pixels to BGRA888
 * @param col -- Color to convert
 */
uint32_t ChColorRGBAtoBGRA(uint32_t col) {
	return ((col & 0xFF000000)) | ((col & 0x00FF0000) >> 16) | ((col & 0x0000FF00)) |
		   ((col & 0x000000FF) << 16);
}