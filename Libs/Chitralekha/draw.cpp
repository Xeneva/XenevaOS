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

#include "draw.h"
#include <stdlib.h>
#include <math.h>
#include "_fastcpy.h"
#include <string.h>

#define sign(x) ((x < 0) ? -1 : ((x > 0) ? 1 : 0))

/*
 * ChDrawRect -- draw a rectangle 
 * @param canvas -- Pointer to canvas
 * @param x -- X coord
 * @param y -- Y coord
 * @param w -- Width of the rect
 * @param h -- Height of the rect
 */
void ChDrawRect(ChCanvas* canvas, unsigned x, unsigned y, unsigned w, unsigned h, uint32_t col) {
	if (!canvas || !canvas->buffer || x >= canvas->canvasWidth || y >= canvas->canvasHeight)
		return;
	if (w > canvas->canvasWidth - x)
		w = canvas->canvasWidth - x;
	if (h > canvas->canvasHeight - y)
		h = canvas->canvasHeight - y;
#ifdef COLOR_BGRA
	col = ChColorRGBAtoBGRA(col);
#endif
	for (unsigned row = 0; row < h; ++row) {
		uint32_t* dst = canvas->buffer + (uint64_t)(y + row) * canvas->canvasWidth + x;
		for (unsigned column = 0; column < w; ++column)
			dst[column] = col;
	}
}

/*
 * ChDrawRectClipped -- draw a rectangle within clipped boundary
 * @param canv -- Pointer to canvas
 * @param x -- X coord
 * @param y -- Y coord
 * @param w -- Width of the rect
 * @param h -- Height of the rect
 * @param clip -- Pointer to clip rect
 * @param col -- Color of the rectangle
 */
void ChDrawRectClipped(
	ChCanvas* canv, unsigned x, unsigned y, unsigned w, unsigned h, ChRect* clip, uint32_t col) {
	int r_x = x;
	int r_y = y;
	int r_w = w;
	int r_h = h;

	if (x > (clip->x + clip->w))
		return;

	if (y > (clip->y + clip->h))
		return;

	if (y <= clip->y) {
		int diffy = clip->y - r_y;
		r_y = clip->y;
		r_h -= diffy;
	}

	if (x <= clip->x)
		r_x = clip->x;

	if ((x + w) > (clip->x + clip->w))
		r_w = (clip->x + clip->w) - x;

	if ((y + h) > (clip->y + clip->h))
		r_h = (clip->y + clip->h) - y;

	ChDrawRect(canv, r_x, r_y, r_w, r_h, col);
}

/*
 * ChDrawVerticalLine -- draws a vertical line
 * @param canv -- Pointer to canvas
 * @paam x -- X coord
 * @param y - y coord
 * @param len -- length of the line
 * @param col -- Color to use
 */
void ChDrawVerticalLine(ChCanvas* canv, unsigned x, unsigned y, unsigned len, uint32_t col) {
	ChDrawRect(canv, x, y, 1, len, col);
}

/*
* ChDrawVerticalLine -- draws a horizontal line
* @param canv -- Pointer to canvas
* @paam x -- X coord
* @param y - y coord
* @param len -- length of the line
* @param col -- Color to use
*/
void ChDrawHorizontalLine(ChCanvas* canv, unsigned x, unsigned y, unsigned len, uint32_t col) {
	ChDrawRect(canv, x, y, len, 1, col);
}

/*
 * ChDrawRectUnfilled -- draws a unfilled only outlined rectangle
 * @param canv -- Pointer to canvas
 * @param x -- X coordinate
 * @param y -- Y coordinate
 * @param w -- Width of the rect
 * @param h -- Height of the rect
 */
void ChDrawRectUnfilled(
	ChCanvas* canv, unsigned x, unsigned y, unsigned w, unsigned h, uint32_t color) {
	ChDrawHorizontalLine(canv, x, y, w, color);
	ChDrawVerticalLine(canv, x, y + 1, h - 2, color);
	ChDrawHorizontalLine(canv, x, y + h - 1, w, color);
	ChDrawVerticalLine(canv, x + w - 1, y + 1, h - 2, color);
}

/*
* ChDrawRectUnfilledClipped -- draw a unfilled rectangle within clipped boundary
* @param canv -- Pointer to canvas
* @param x -- X coord
* @param y -- Y coord
* @param w -- Width of the rect
* @param h -- Height of the rect
* @param clip -- Pointer to clip rect
* @param col -- Color of the rectangle
*/
void ChDrawRectUnfilledClipped(
	ChCanvas* canv, unsigned x, unsigned y, unsigned w, unsigned h, ChRect* clip, uint32_t col) {
	int r_x = x;
	int r_y = y;
	int r_w = w;
	int r_h = h;

	if (x > (clip->x + clip->w))
		return;

	if (y > (clip->y + clip->h))
		return;

	if (y <= clip->y)
		r_y = clip->y;

	if (x <= clip->x)
		r_x = clip->x;

	if ((x + w) > (clip->x + clip->w))
		r_w = (clip->w + clip->x) - x;

	if ((y + h) > (clip->y + clip->h))
		r_h = (clip->h + clip->y) - y;

	ChDrawRectUnfilled(canv, r_x, r_y, r_w, r_h, col);
}

/*
 * ChDrawFilledCircle -- draws a filled circle
 * @param canv -- Pointer to canvas
 * @param x -- X coordinate
 * @param y -- Y coordinate
 * @param radius -- radius of the circle
 * @param fill_col -- color to use while filling
 */
void ChDrawFilledCircle(ChCanvas* canv, int x, int y, int radius, uint32_t fill_col) {
	ChDrawVerticalLine(canv, x, y - radius, 2 * radius + 1, fill_col);
	int f = 1 - radius;
	int ddF_x = 1;
	int ddF_y = -2 * radius;
	int i = 0;
	int j = radius;

	while (i < j) {
		if (f >= 0) {
			j--;
			ddF_y += 2;
			f += ddF_y;
		}
		i++;
		ddF_x += 2;
		f += ddF_x;

		ChDrawVerticalLine(canv, x + i, y - j, 2 * j + 1, fill_col);
		ChDrawVerticalLine(canv, x + j, y - i, 2 * i + 1, fill_col);
		ChDrawVerticalLine(canv, x - i, y - j, 2 * j + 1, fill_col);
		ChDrawVerticalLine(canv, x - j, y - i, 2 * i + 1, fill_col);
	}
}

/*
 * ChDrawLine -- draws a line
 * @param canv -- Pointer to canvas
 * @param x1 -- X coord of point one
 * @param y1 -- Y coord of point one
 * @param x2 -- X coord of point two
 * @param y2 -- Y coord of point two
 * @param color -- color to use
 */
void ChDrawLine(ChCanvas* canv, int x1, int y1, int x2, int y2, uint32_t color) {
	int dx = x2 - x1;
	int dy = y2 - y1;
	int dxabs = abs(dx);
	int dyabs = abs(dy);
	int sdx = sign(dx);
	int sdy = sign(dy);
	int x = 0;
	int y = 0;
	int px = x1;
	int py = y1;

	ChDrawPixel(canv, px, py, color);
	if (dxabs >= dyabs) {
		for (int i = 0; i < dxabs; i++) {
			y += dyabs;
			if (y >= dxabs) {
				y -= dxabs;
				py += sdy;
			}
			px += sdx;
			ChDrawPixel(canv, px, py, color);
		}
	} else {
		for (int i = 0; i < dyabs; i++) {
			x += dxabs;
			if (x >= dyabs) {
				x -= dyabs;
				px += sdx;
			}
			py += sdy;
			ChDrawPixel(canv, px, py, color);
		}
	}
}

/*
 * ChDrawCircleUnfilled -- Draws a circle without filling its internal
 * @param canvas -- Pointer to canvas
 * @param x -- X coord of the circle
 * @param y -- Y coord of the circle
 * @param radius -- radius of the circle
 * @param color -- outline color
 */
void ChDrawCircleUnfilled(ChCanvas* canvas, int x, int y, int radius, uint32_t color) {
	int f = 1 - radius;
	int ddF_x = 1;
	int ddF_y = -2 * radius;
	int i = 0;
	int j = radius;

	ChDrawPixel(canvas, x, y + radius, color);
	ChDrawPixel(canvas, x, y - radius, color);
	ChDrawPixel(canvas, x + radius, y, color);
	ChDrawPixel(canvas, x - radius, y, color);

	while (i < j) {
		if (f >= 0) {
			j--;
			ddF_y += 2;
			f += ddF_y;
		}
		i++;
		ddF_x += 2;
		f += ddF_x;
		ChDrawPixel(canvas, x + i, y + j, color);
		ChDrawPixel(canvas, x - i, y + j, color);
		ChDrawPixel(canvas, x + i, y - j, color);
		ChDrawPixel(canvas, x - i, y - j, color);
		ChDrawPixel(canvas, x + j, y + i, color);
		ChDrawPixel(canvas, x - j, y + i, color);
		ChDrawPixel(canvas, x + j, y - i, color);
		ChDrawPixel(canvas, x - j, y - i, color);
	}
}

/*
 * ChDrawFilledCircleAA -- Draw antialiased filled cirlce
 * @param canv -- Pointer to canvas
 * @param cx -- x location
 * @param cy -- y location
 * @param radius -- radius of the circle
 * @param color -- color of the circle
 */
void ChDrawFilledCircleAA(ChCanvas* canv, int cx, int cy, int radius, uint32_t color) {
	for (int y = -radius; y <= radius; y++) {
		for (int x = -radius; x <= radius; x++) {
			double dist = sqrt(x * x + y * y);
			double alpha = (radius - dist) / radius;
			if (dist <= radius) {
				ChDrawPixelAA(canv, cx + x, cy + y, color, alpha > 0 ? alpha : 0);
			}
		}
	}
}

/*
 * ChDrawCapsule -- draw a capsule shaped rectangle
 * @param canv -- Pointer to canvas
 * @param x -- X location relative to window
 * @param y -- Y location relative to window
 * @param width -- Width of the capsule
 * @param height -- Height of the capsule
 * @param color -- fill color of the capsule
 */
void ChDrawCapsule(ChCanvas* canv, int x, int y, int width, int height, uint32_t color) {
	int radius = height / 2;
	for (int i = 0; i < width - (2 * radius); i++) {
		for (int j = 0; j < height; j++) {
			ChDrawPixel(canv, x + radius + i, y + j, color);
		}
	}

	ChDrawFilledCircle(canv, x + radius, y + radius, radius, color);
	ChDrawFilledCircle(canv, x + width - radius - 1, y + radius, radius, color);
}

/**
* acrylic_box_blur -- Adds box blur filter to a given image using 3x3 matrix kernel
* @param canvas -- current canvas where to draw
* @param input -- input image buffer
* @param output -- output image buffer
* @param cx -- current x position to focus
* @param cy -- current y position to focus
* @param w -- box boundary width
* @param h -- box boundary height
*/
void ChDrawBoxBlur(
	ChCanvas* canv, uint32_t* inputBuf, uint32_t* outputBuf, int cx, int cy, int w, int h) {
	for (int j = 0; j < h; j++) {
		for (int i = 0; i < w; i++) {
			int redTotal = 0;
			int greenTotal = 0;
			int blueTotal = 0;
			int alphaTotal = 0;

			for (int row = -1; row <= 1; row++) {
				for (int col = -1; col <= 1; col++) {
					int currentX = cx + i + col;
					int currentY = cy + j + row;

					if (currentX >= 0 && currentX < canv->canvasWidth && currentY >= 0 &&
						currentY < canv->canvasHeight) {
						uint32_t color = inputBuf[(currentY * canv->canvasWidth + currentX)];

						uint8_t red = GET_RED(color);
						uint8_t green = GET_GREEN(color);
						uint8_t blue = GET_BLUE(color);
						uint8_t alpha = GET_ALPHA(color);

						redTotal += red;
						greenTotal += green;
						blueTotal += blue;
						alphaTotal += alpha;
					}
				}
			}

			uint8_t red = redTotal / 9;
			uint8_t green = greenTotal / 9;
			uint8_t blue = blueTotal / 9;
			uint8_t alpha = alphaTotal / 9;

			outputBuf[(cy + j) * canv->canvasWidth + (cx + i)] =
				make_col_a(red, green, blue, alpha);
		}
	}
}
