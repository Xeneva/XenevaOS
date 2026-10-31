/**
* BSD 2-Clause License
*
* Copyright (c) 2023-2025, Manas Kamal Choudhury
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

#ifndef __VIDEO_H__
#define __VIDEO_H__

#include "xnldr.h"
#include "font.h"
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include "clib.h"

#define RGB(r, g, b)                                                                               \
	(((b) & 0xFF) | (((g) << 8) & 0xFF00) |                                                        \
	 (((r) << 16) & 0xFF0000)) // ((r & 0xFF) | ((g << 8)&0xFF00) | ((b << 16)&0xFF0000))

#define RED(col) ((col >> 16) & 0xFF)

#define GREEN(col) ((col >> 8) & 0xFF)

#define BLUE(col) (col & 0xFF)

/*
 * XEInitialiseGraphics -- Initialise the screen and store graphics
 * information in fbinfo structure
 * @GraphicsOutput -- Pointer to Graphics Output protocol
 */
extern EFI_STATUS XEInitialiseGraphics(EFI_GRAPHICS_OUTPUT_PROTOCOL* GraphicsOutput);
/*
 * XEPutPixel -- puts a pixel on the screen
 * @param x -- x location of the screen
 * @param y -- y location of the screen
 * @param col -- color of the pixel
 */
extern void XEPutPixel(size_t x, size_t y, uint32_t col);

/*
 * XEGraphicsPutC -- put a character to the screen
 * @param str -- character to print
 */
extern void XEGraphicsPutC(char str);

/*
 * XEGraphicsPuts -- put string on the screen
 * @param str -- string to put
 */
extern void XEGraphicsPuts(const char* str);

/*
 * XEGraphicsClearScreen -- clear the entire screen
 * @param gop -- Pointer to Graphics Output Protocol
 */
extern void XEGraphicsClearScreen(EFI_GRAPHICS_OUTPUT_PROTOCOL* gop);

extern uint32_t* XEGetFramebuffer();
extern uint16_t XEGetScreenWidth();
extern uint16_t XEGetScreenHeight();
extern size_t XEGetFramebufferSz();
extern size_t XEGetPixelsPerLine();
extern uint32_t XEGetRedMask();
extern uint32_t XEGetBlueMask();
extern uint32_t XEGetGreenMask();
extern uint32_t XEGetResvMask();

#endif
