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

#ifndef __CHITRALEKHA_H__
#define __CHITRALEKHA_H__

#include <stdint.h>
#include <_xeneva.h>

#ifdef __cplusplus
XE_EXTERN {
#endif

	typedef struct _ChCanvas_ {
		int graphics_fd;
		uint32_t screenWidth;
		uint32_t screenHeight;
		uint32_t bpp;
		uint16_t scanline;
		uint32_t pitch;
		uint32_t canvasWidth;
		uint32_t canvasHeight;
		uint32_t* buffer;
		size_t bufferSz;
		uint32_t* framebuff;
	} ChCanvas;

	XE_LIB int ChPrintLibName();

	/*
	* ChCreateCanvas -- creates a new canvas
	* @param reqW -- requested width of the canvas
	* @param reqH -- requested height of the canvas
	*/
	XE_LIB ChCanvas* ChCreateCanvas(int reqW, int reqH);

	/*
	* ChAllocateBuffer -- allocates buffers for graphics
	* @param canvas -- Pointer to canvas
	*/
	XE_LIB int ChAllocateBuffer(ChCanvas * canvas);

	/*
	* ChDeAllocateBuffer -- de-allocates buffers from
	* canvas
	* @param canvas -- pointer to canvas structure
	*/
	XE_LIB int ChDeAllocateBuffer(ChCanvas * canvas);

	/*
	* ChCanvasScreenUpdate -- updates screen buffer with canvas buffer
	* contents
	* @param canvas -- Pointer to canvas
	* @param x -- x position
	* @param y -- y position
	* @param w -- width of the canvas
	* @param h -- height of the canvas
	*/
	XE_LIB void ChCanvasScreenUpdate(ChCanvas * canvas, int x, int y, int w, int h);

	/* Orders a completed batch of framebuffer stores. Keep this outside the
	 * scanline copy primitive so one presentation does not issue a full-system
	 * barrier for every row. */
	XE_LIB void ChCanvasScreenCommit();

	/*
	* ChDrawPixel -- draws a pixel to canvas buffer
	* @param canvas -- pointer to canvas
	* @param x -- x position
	* @param y -- y position
	* @param color -- color of the pixel
	*/
	XE_LIB void ChDrawPixel(ChCanvas * canvas, int x, int y, uint32_t color);

	/*
     * ChDrawPixelRAW -- draws a pixel to canvas buffer
     * @param canvas -- pointer to canvas
     * @param x -- x position
     * @param y -- y position
     * @param color -- color of the pixel
     */
	XE_LIB void ChDrawPixelRAW(ChCanvas * canvas, int x, int y, uint32_t color);

	/*
     * ChDrawPixelAA -- draw anti-aliased pixel
     * @param canv - Pointer to canvas
     * @param x -- X location relative to window
     * @param y -- Y location relative to window
     * @param color -- Color of the pixel
     * @param alpha -- Alpha value
     */
	XE_LIB void ChDrawPixelAA(ChCanvas * canv, int x, int y, uint32_t color, double alpha);

	/*
	* ChGetPixel -- retuns a pixel from canvas
	* @param canvas -- Pointer to canvas structure
	* @param x -- x position
	* @param y -- y position
	*/
	XE_LIB uint32_t ChGetPixel(ChCanvas * canvas, int x, int y);

	/*
	* ChCanvasFill -- fill the canvas with specific color
	* @param canvas -- pointer to canvas structure
	* @param w -- width to fill
	* @param h -- height to fill
	* @param color -- color to be filled with
	*/
	XE_LIB void ChCanvasFill(ChCanvas * canvas, int w, int h, uint32_t color);

	/*
	* ChGetScreenDiagonal -- get screen diagonal using
	* pythagorean theorem in centimetre
	* @param canv -- Pointer to canvas
	*/
	XE_LIB float ChGetScreenDiagonal(ChCanvas * canv);

	/*
	* ChGetScreenDPI -- converts screen resolution into
	* dot-per-inch in centimetre
	* @param canv -- Pointer to canvas
	*/
	XE_LIB float ChGetScreenDPI(ChCanvas * canv);

	/*
	* ChGetScreenAspectRatio -- returns the aspect ration of the
	* screen
	* @param canv -- Pointer to canvas
	*/
	XE_LIB int ChGetScreenAspectRatio(ChCanvas * canv);

#ifdef __cplusplus
}
#endif

#endif
