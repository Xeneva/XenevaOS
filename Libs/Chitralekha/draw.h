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

#ifndef __DRAW_H__
#define __DRAW_H__

#include "chitralekha.h"
#include "color.h"
#include <_xeneva.h>

#ifdef __cplusplus
XE_EXTERN {
#endif

#pragma pack(push, 1)
	typedef struct _chrect_ {
		int32_t x;
		int32_t y;
		int32_t w;
		int32_t h;
	} ChRect;
#pragma pack(pop)

	/*
	* ChDrawRect -- draw a rectangle
	* @param canvas -- Pointer to canvas
	* @param x -- X coord
	* @param y -- Y coord
	* @param w -- Width of the rect
	* @param h -- Height of the rect
	*/
	XE_LIB void ChDrawRect(
		ChCanvas * canvas, unsigned x, unsigned y, unsigned w, unsigned h, uint32_t col);

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
	XE_LIB void ChDrawRectClipped(ChCanvas * canv,
								  unsigned x,
								  unsigned y,
								  unsigned w,
								  unsigned h,
								  ChRect* clip,
								  uint32_t col);

	/*
	* ChDrawVerticalLine -- draws a vertical line
	* @param canv -- Pointer to canvas
	* @paam x -- X coord
	* @param y - y coord
	* @param len -- length of the line
	* @param col -- Color to use
	*/
	XE_LIB void ChDrawVerticalLine(
		ChCanvas * canv, unsigned x, unsigned y, unsigned len, uint32_t col);

	/*
	* ChDrawVerticalLine -- draws a horizontal line
	* @param canv -- Pointer to canvas
	* @paam x -- X coord
	* @param y - y coord
	* @param len -- length of the line
	* @param col -- Color to use
	*/
	XE_LIB void ChDrawHorizontalLine(
		ChCanvas * canv, unsigned x, unsigned y, unsigned len, uint32_t col);

	/*
	* ChDrawRectUnfilled -- draws a unfilled only outlined rectangle
	* @param canv -- Pointer to canvas
	* @param x -- X coordinate
	* @param y -- Y coordinate
	* @param w -- Width of the rect
	* @param h -- Height of the rect
	*/
	XE_LIB void ChDrawRectUnfilled(
		ChCanvas * canv, unsigned x, unsigned y, unsigned w, unsigned h, uint32_t color);

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
	XE_LIB void ChDrawRectUnfilledClipped(ChCanvas * canv,
										  unsigned x,
										  unsigned y,
										  unsigned w,
										  unsigned h,
										  ChRect* clip,
										  uint32_t col);

	/*
	* ChDrawFilledCircle -- draws a filled circle
	* @param canv -- Pointer to canvas
	* @param x -- X coordinate
	* @param y -- Y coordinate
	* @param radius -- radius of the circle
	* @param fill_col -- color to use while filling
	*/
	XE_LIB void ChDrawFilledCircle(ChCanvas * canv, int x, int y, int radius, uint32_t fill_col);

	/*
     * ChDrawFilledCircleAA -- Draw antialiased filled cirlce
     * @param canv -- Pointer to canvas
     * @param cx -- x location
     * @param cy -- y location
     * @param radius -- radius of the circle
     * @param color -- color of the circle
     */
	XE_LIB void ChDrawFilledCircleAA(ChCanvas * canv, int cx, int cy, int radius, uint32_t color);

	/*
	* ChDrawLine -- draws a line
	* @param canv -- Pointer to canvas
	* @param x1 -- X coord of point one
	* @param y1 -- Y coord of point one
	* @param x2 -- X coord of point two
	* @param y2 -- Y coord of point two
	* @param color -- color to use
	*/
	XE_LIB void ChDrawLine(ChCanvas * canv, int x1, int y1, int x2, int y2, uint32_t color);

	/*
	* ChDrawCircleUnfilled -- Draws a circle without filling its internal
	* @param canvas -- Pointer to canvas
	* @param x -- X coord of the circle
	* @param y -- Y coord of the circle
	* @param radius -- radius of the circle
	* @param color -- outline color
	*/
	XE_LIB void ChDrawCircleUnfilled(ChCanvas * canvas, int x, int y, int radius, uint32_t color);

	/*
     * ChDrawCapsule -- draw a capsule shaped rectangle
     * @param canv -- Pointer to canvas
     * @param x -- X location relative to window
     * @param y -- Y location relative to window
     * @param width -- Width of the capsule
     * @param height -- Height of the capsule
     * @param color -- fill color of the capsule
     */
	XE_LIB void ChDrawCapsule(ChCanvas * canv, int x, int y, int width, int height, uint32_t color);

	XE_LIB void ChDrawRoundedRect(ChCanvas * canv,
								  int x,
								  int y,
								  int width,
								  int height,
								  uint32_t color,
								  int topleftRadius,
								  int topRightRadius,
								  int bottomRightRadius,
								  int bottomLeftRadius,
								  ChRect* mask);
#ifdef __cplusplus
}
#endif

#endif