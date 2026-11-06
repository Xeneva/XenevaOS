/**
* @file chitralekha.cpp
* 
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

#include "chitralekha.h"
#include <stdio.h>
#include <sys/_kefile.h>
#include <sys/mman.h>
#include <sys/iocodes.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "_fastcpy.h"
#include "color.h"

// removed _fltused here since it's in XEClib
int ChPrintLibName() {
	_KePrint("Chitralekha Graphics Library v1.0 \n");
	return 0;
}

/**
 * @brief ChCreateCanvas -- creates a new canvas
 * @param reqW -- requested width of the canvas
 * @param reqH -- requested height of the canvas
 */
ChCanvas* ChCreateCanvas(int reqW, int reqH) {
	int graphFd = _KeOpenFile("/dev/graph", FILE_OPEN_WRITE);
	XEFileIOControl ioctl;
	memset(&ioctl, 0, sizeof(XEFileIOControl));
	ioctl.syscall_magic = AURORA_SYSCALL_MAGIC;
	int ret = 0;

	ChCanvas* canvas = (ChCanvas*)malloc(sizeof(ChCanvas));
	memset(canvas, 0, sizeof(ChCanvas));

	ret = _KeFileIoControl(graphFd, SCREEN_GETWIDTH, &ioctl);
	canvas->screenWidth = ioctl.uint_1;
	ret = _KeFileIoControl(graphFd, SCREEN_GETHEIGHT, &ioctl);
	canvas->screenHeight = ioctl.uint_1;
	ret = _KeFileIoControl(graphFd, SCREEN_GETBPP, &ioctl);
	canvas->bpp = ioctl.uint_1;
	ret = _KeFileIoControl(graphFd, SCREEN_GET_SCANLINE, &ioctl);
	canvas->scanline = ioctl.ushort_1;
	ret = _KeFileIoControl(graphFd, SCREEN_GET_PITCH, &ioctl);
	canvas->pitch = ioctl.uint_1;
	canvas->canvasHeight = reqH;
	canvas->canvasWidth = reqW;
	canvas->buffer = 0;
	canvas->bufferSz = 0;
	canvas->graphics_fd = graphFd;
	ret = _KeFileIoControl(graphFd, SCREEN_GET_FB, &ioctl);
	canvas->framebuff = (uint32_t*)ioctl.ulong_1;

	return canvas;
}

/**
 * @brief ChAllocateBuffer -- allocates buffers for graphics 
 * @param canvas -- Pointer to canvas
 */
int ChAllocateBuffer(ChCanvas* canvas) {
	if (!canvas)
		return 0;
	int reqW = canvas->canvasWidth;
	int reqH = canvas->canvasHeight;

	size_t sz = (static_cast<size_t>(reqW) * reqH * (canvas->bpp / 8) + 0x1F) & (~0x1FULL);
	void* addr = _KeMemMap(NULL, sz, 0, 0, MEMMAP_NO_FILEDESC, 0);
	if (!addr)
		return 0;
	canvas->buffer = (uint32_t*)addr;
	canvas->bufferSz = sz;
	_KePrint("Buffer allocated, canvas : %x \n", canvas);
	return 1;
}

/**
 * @brief ChDeAllocateBuffer -- de-allocates buffers from
 * canvas
 * @param canvas -- pointer to canvas structure
 */
int ChDeAllocateBuffer(ChCanvas* canvas) {
	if (!canvas)
		return 0;
	size_t sz = canvas->bufferSz;
	if (!sz)
		return 1;
	_KeMemUnmap(canvas->buffer, sz);
	return 1;
}

/**
 * @brief ChCanvasScreenUpdate -- updates screen buffer with canvas buffer
 * contents
 * @param canvas -- Pointer to canvas
 * @param x -- x position
 * @param y -- y position
 * @param w -- width of the canvas
 * @param h -- height of the canvas
 */
void ChCanvasScreenUpdate(ChCanvas* canvas, int _x, int _y, int _w, int _h) {
	if (!canvas)
		return;
	uint32_t* fb = canvas->framebuff;
	if (!fb || canvas->buffer == fb)
		return;

	int64_t x = _x, y = _y, w = _w, h = _h;

	if (w <= 0 || h <= 0 || x >= canvas->screenWidth || y >= canvas->screenHeight)
		return;

	if (x < 0) {
		w += x;
		x = 0;
	}
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (w <= 0 || h <= 0)
		return;
	if (x + w > canvas->screenWidth)
		w = canvas->screenWidth - x;
	if (y + h > canvas->screenHeight)
		h = canvas->screenHeight - y;

	uint32_t bytes_per_pixel = canvas->bpp / 8;
	uint32_t pitch_pixels =
		canvas->pitch && bytes_per_pixel ? canvas->pitch / bytes_per_pixel : canvas->screenWidth;

	for (int64_t i = 0; i < h; i++) {
		void* fb_mem = (fb + (y + i) * pitch_pixels + x);
		void* canvas_mem = (canvas->buffer + (y + i) * (canvas->canvasWidth) + x);
		_fastcpy(fb_mem, canvas_mem, w * 4);
	}
}

void ChCanvasScreenCommit() {
#if defined(ARCH_ARM64)
	/* The GOP framebuffer is Normal Non-cacheable memory. One system-scope
	 * store barrier after the complete damage batch is sufficient; _fastcpy
	 * is also used for ordinary RAM and must not carry device ordering. */
	__asm__ volatile("dmb sy" ::: "memory");
#endif
}

/**
 * @brief ChDrawPixel -- draws a pixel to canvas buffer
 * @param canvas -- pointer to canvas
 * @param x -- x position
 * @param y -- y position
 * @param color -- color of the pixel
 */
void ChDrawPixel(ChCanvas* canvas, int x, int y, uint32_t color) {
	if (((uint64_t)canvas >> 48) == 0xFFFF)
		_KePrint("ChDrawPixel : suspected canvas address : %x \n", canvas);
	unsigned int* lfb = canvas->buffer;
	if (((uint64_t)lfb >> 48) == 0xFFFF)
		_KePrint("ChDrawPixel : suspected lfb address : %x \n", lfb);
	if (x < 0) {
		_KePrint("ChDrawPixel : corrupted x -> %d \r\n", x);
		x = 0;
	}

	if (y < 0) {
		//_KePrint("ChDrawPixel : corrupted y -> %d \r\n", y);
		y = 0;
	}

	if (x >= canvas->canvasWidth) {
		//_KePrint("ChDrawPixel : corrupted x > canvasWidth \r\n");
		return;
	}

	if (y >= canvas->canvasHeight) {
		//_KePrint("ChDrawPixel : corrupted y > canvasHeight \r\n");
		return;
	}
#ifdef COLOR_BGRA
	uint32_t bgra = ChColorRGBAtoBGRA(color);
	lfb[static_cast<uint64_t>(y) * canvas->canvasWidth + x] = bgra;
#elif defined(COLOR_RGBA)
	/* By default Chitralekha uses RGBA */
	lfb[static_cast<uint64_t>(y) * canvas->canvasWidth + x] = color;
#else
	/* No pixel format selected: fail loudly instead of silently dropping
	 * every pixel this function is asked to draw. --axiss */
#error "Chitralekha pixel format not selected: define COLOR_RGBA or COLOR_BGRA"
#endif
}

/**
 * @brief ChDrawPixelRAW -- draws a pixel to canvas buffer
 * @param canvas -- pointer to canvas
 * @param x -- x position
 * @param y -- y position
 * @param color -- color of the pixel
 */
void ChDrawPixelRAW(ChCanvas* canvas, int x, int y, uint32_t color) {
	if (((uint64_t)canvas >> 48) == 0xFFFF)
		_KePrint("ChDrawPixel : suspected canvas address : %x \n", canvas);
	unsigned int* lfb = canvas->buffer;
	if (((uint64_t)lfb >> 48) == 0xFFFF)
		_KePrint("ChDrawPixel : suspected lfb address : %x \n", lfb);
	if (x < 0) {
		_KePrint("ChDrawPixel : corrupted x -> %d \r\n", x);
		x = 0;
	}

	if (y < 0) {
		_KePrint("ChDrawPixel : corrupted y -> %d \r\n", y);
		y = 0;
	}

	if (x >= canvas->canvasWidth) {
		_KePrint("ChDrawPixel : corrupted x > canvasWidth \r\n");
		return;
	}

	if (y >= canvas->canvasHeight) {
		_KePrint("ChDrawPixel : corrupted y > canvasHeight \r\n");
		return;
	}
	lfb[static_cast<uint64_t>(y) * canvas->canvasWidth + x] = color;
}

/**
 * @brief ChDrawPixelAA -- draw anti-aliased pixel
 * @param canv - Pointer to canvas
 * @param x -- X location relative to window
 * @param y -- Y location relative to window
 * @param color -- Color of the pixel
 * @param alpha -- Alpha value
 */
void ChDrawPixelAA(ChCanvas* canv, int x, int y, uint32_t color, double alpha) {
	uint32_t bgColor = 0xFFFFFFFF;
	uint32_t blendedColor = ChColorAlphaBlend(bgColor, color, alpha);
	ChDrawPixel(canv, x, y, blendedColor);
}

/**
 * @brief ChGetPixel -- retuns a pixel from canvas
 * @param canvas -- Pointer to canvas structure
 * @param x -- x position
 * @param y -- y position
 */
uint32_t ChGetPixel(ChCanvas* canvas, int x, int y) {
	unsigned int* lfb = canvas->buffer;
	if (x < 0) {
		_KePrint("ChGetPixel : corrupted x -> %d \r\n", x);
		x = 0;
	}

	if (y < 0) {
		_KePrint("ChGetPixel : corrupted y -> %d \r\n", y);
		y = 0;
	}

	if (x > canvas->canvasWidth) {
		_KePrint("ChGetPixel: corrupted x > canvasWidth -> %x \r\n", x);
		x = canvas->canvasWidth;
	}

	if (y > canvas->screenHeight) {
		_KePrint("ChGetPixel: corrupted y > canvasHeight -> %x \r\n", y);
		y = canvas->canvasHeight;
	}

	return lfb[static_cast<int64_t>(y) * canvas->canvasWidth + x];
}

/**
 * @brief ChCanvasFill -- fill the canvas with specific color
 * @param canvas -- pointer to canvas structure
 * @param w -- width to fill
 * @param h -- height to fill
 * @param color -- color to be filled with
 */
void ChCanvasFill(ChCanvas* canvas, int w, int h, uint32_t color) {
	for (int i = 0; i < w; i++)
		for (int j = 0; j < h; j++)
			ChDrawPixel(canvas, 0 + i, 0 + j, color);
}

/**
 * @brief ChGetScreenDiagonal -- get screen diagonal using
 * pythagorean theorem in centimetre 
 * @param canv -- Pointer to canvas
 */
float ChGetScreenDiagonal(ChCanvas* canv) {
	float diagonal =
		sqrtf(canv->screenWidth * canv->screenWidth + canv->screenHeight * canv->screenHeight);
	return diagonal;
}

/**
 * @brief ChGetScreenDPI -- converts screen resolution into 
 * dot-per-inch in centimetre
 * @param canv -- Pointer to canvas
 */
float ChGetScreenDPI(ChCanvas* canv) {
	float dpi =
		sqrtf(canv->screenWidth * canv->screenWidth + canv->screenHeight * canv->screenHeight) /
		ChGetScreenDiagonal(canv);
	return dpi;
}

/**
 * @brief ChGetScreenAspectRatio -- returns the aspect ration of the
 * screen
 * @param canv -- Pointer to canvas
 */
int ChGetScreenAspectRatio(ChCanvas* canv) {
	int ar = canv->screenWidth / canv->screenHeight;
	return ar;
}
