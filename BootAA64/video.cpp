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

#include "video.h"
#include <aurora.h>

FRAMEBUFFER_INFORMATION fbinfo;
size_t xpos;
size_t ypos;
size_t bpp;
size_t h_res, v_res;
uint32_t background;
uint32_t foreground;

int_fast8_t high_set_bit(size_t sz) {
	int_fast8_t count = -1;
	while (sz) {
		sz >>= 1;
		count += 1;
	}
	return count;
}

int_fast8_t low_set_bit(size_t sz) {
	int_fast8_t count = -1;
	while (sz) {
		count += 1;
		if ((sz & 1) != 0)
			break;
		sz >>= 1;
	}
	return count;
}

/*
 * XEInitialiseGraphics -- Initialise the screen and store graphics
 * information in fbinfo structure
 * @GraphicsOutput -- Pointer to Graphics Output protocol
 */
EFI_STATUS XEInitialiseGraphics(EFI_GRAPHICS_OUTPUT_PROTOCOL* GraphicsOutput) {
	gSystemTable->ConOut->SetAttribute(gSystemTable->ConOut, EFI_BACKGROUND_BLUE | EFI_WHITE);
	gSystemTable->ConOut->SetCursorPosition(gSystemTable->ConOut, 0, 0);

	EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* info = GraphicsOutput->Mode->Info;

	//clear screen
	EFI_GRAPHICS_OUTPUT_BLT_PIXEL pixel;
	pixel.Blue = 164;
	pixel.Green = 115;
	pixel.Red = 0;

	//GraphicsOutput->Blt(GraphicsOutput, &pixel, EfiBltVideoFill, 0, 0, 0, 0, info->HorizontalResolution, info->VerticalResolution, 0);

	// QEMU ramfb Blt can be buggy, so manually fill the framebuffer with the background color
	uint32_t bg_color = RGB(0, 115, 164);
	uint32_t* fb = (uint32_t*)GraphicsOutput->Mode->FrameBufferBase;
	for (size_t i = 0; i < GraphicsOutput->Mode->FrameBufferSize / 4; i++) {
		fb[i] = bg_color;
	}
	//now we need to emulate the console ourselves, as some UEFI implementations change mode back
	fbinfo.phyaddr = (uint32_t*)GraphicsOutput->Mode->FrameBufferBase;
	fbinfo.graphics_framebuffer = (uint32_t*)GraphicsOutput->Mode->FrameBufferBase;
	fbinfo.size = GraphicsOutput->Mode->FrameBufferSize;
	fbinfo.pixelsPerLine = GraphicsOutput->Mode->Info->PixelsPerScanLine;
	fbinfo.X_Resolution = GraphicsOutput->Mode->Info->HorizontalResolution;
	fbinfo.Y_Resolution = GraphicsOutput->Mode->Info->VerticalResolution;

	switch (GraphicsOutput->Mode->Info->PixelFormat) {
	case PixelRedGreenBlueReserved8BitPerColor:
		fbinfo.redmask = 0xFF;
		fbinfo.greenmask = 0xFF00;
		fbinfo.bluemask = 0xFF0000;
		fbinfo.resvmask = 0xFF000000;
		break;
	case PixelBlueGreenRedReserved8BitPerColor:
		fbinfo.redmask = 0xFF0000;
		fbinfo.greenmask = 0xFF00;
		fbinfo.bluemask = 0xFF;
		fbinfo.resvmask = 0xFF000000;
		break;
	case PixelBitMask:
		fbinfo.redmask = GraphicsOutput->Mode->Info->PixelInformation.RedMask;
		fbinfo.greenmask = GraphicsOutput->Mode->Info->PixelInformation.GreenMask;
		fbinfo.bluemask = GraphicsOutput->Mode->Info->PixelInformation.BlueMask;
		fbinfo.resvmask = GraphicsOutput->Mode->Info->PixelInformation.ReservedMask;
		break;
	}

	xpos = 0;
	ypos = 0;
	size_t mergemasks = fbinfo.redmask | fbinfo.greenmask | fbinfo.bluemask | fbinfo.resvmask;
	h_res = fbinfo.X_Resolution;
	v_res = fbinfo.Y_Resolution;
	bpp = high_set_bit(mergemasks) + 1;
	foreground = RGB(255, 255, 255);
	background = RGB(0, 115, 164);
	return EFI_SUCCESS;
}

/*
 * XEPutPixel -- puts a pixel on the screen
 * @param x -- x location of the screen
 * @param y -- y location of the screen
 * @param col -- color of the pixel
 */
void XEPutPixel(size_t x, size_t y, uint32_t col) {
	uint32_t* framebuffer = fbinfo.phyaddr;

	uint32_t* pixelloc =
		raw_offset<uint32_t*>(framebuffer, (fbinfo.pixelsPerLine * y + x) * (bpp / 8));
	size_t pixel = ((RED(col) << low_set_bit(fbinfo.redmask)) & fbinfo.redmask) |
				   ((GREEN(col) << low_set_bit(fbinfo.greenmask)) & fbinfo.greenmask) |
				   ((BLUE(col) << low_set_bit(fbinfo.bluemask)) & fbinfo.bluemask) |
				   (*pixelloc & fbinfo.resvmask);
	*pixelloc = pixel;
}

/*
 * XEGraphicsPutC -- put a character to the screen
 * @param str -- character to print
 */
void XEGraphicsPutC(char str) {
	if (xpos > v_res / 9) {
		xpos = 0;
		ypos++;
	}

	for (size_t y = 0; y < 16; ++y) {
		for (size_t x = 0; x < 8; ++x) {
			const bx_fontcharbitmap_t& entry = bx_vgafont[str];
			if (entry.data[y] & (1 << x)) {
				XEPutPixel(x + xpos * 9, y + ypos * 16, foreground);
			} else {
				XEPutPixel(x + xpos * 9, y + ypos * 16, background);
			}
		}
		XEPutPixel(8 + xpos * 9, y + ypos * 16, background);
	}

	xpos++;

	uint32_t* lfb = fbinfo.phyaddr;
	if (ypos + 1 > h_res / 16) {
		for (int i = 16; i < h_res * v_res; i++)
			lfb[i] = lfb[i + v_res * 16];
		ypos--;
	}
}

/*
 * XEGraphicsPuts -- put string on the screen
 * @param str -- string to put
 */
void XEGraphicsPuts(const char* str) {
	while (*str) {
		if (*str > 0xFF) {
			//unicode
		} else if (*str == '\n') {
			++ypos;
			xpos = 0;
		} else if (*str == '\r') {
		} else if (*str == '\b') {
			if (xpos > 0)
				--xpos;
		} else {
			const bx_fontcharbitmap_t entry = bx_vgafont[*str];
			for (size_t y = 0; y < 16; ++y) {
				for (size_t x = 0; x < 8; ++x) {
					if (entry.data[y] & (1 << x)) {
						XEPutPixel(x + xpos * 9, y + ypos * 16, foreground);
					} else {
						XEPutPixel(x + xpos * 9, y + ypos * 16, background);
					}
				}
				XEPutPixel(8 + xpos * 9, y + ypos * 16, background);
			}
			++xpos;
			if (xpos > h_res / 9) {
				xpos = 0;
				++ypos;
			}
		}

		++str;
	}

	/* Scroll */
	if (ypos + 1 > v_res / 16) {
		for (int i = 16; i < v_res * h_res; i++)
			fbinfo.phyaddr[i] = fbinfo.phyaddr[i + h_res * 16];
		ypos--;
	}
}

/*
 * XEGraphicsClearScreen -- clear the entire screen
 * @param gop -- Pointer to Graphics Output Protocol
 */
void XEGraphicsClearScreen(EFI_GRAPHICS_OUTPUT_PROTOCOL* gop) {
	EFI_GRAPHICS_OUTPUT_BLT_PIXEL pixel;
	pixel.Blue = 0;
	pixel.Green = 0;
	pixel.Red = 0;
	xpos = ypos = 0;

	gop->Blt(gop, &pixel, EfiBltVideoFill, 0, 0, 0, 0, fbinfo.X_Resolution, fbinfo.Y_Resolution, 0);
}

uint32_t* XEGetFramebuffer() {
	return fbinfo.phyaddr;
}

uint16_t XEGetScreenWidth() {
	return fbinfo.X_Resolution;
}

uint16_t XEGetScreenHeight() {
	return fbinfo.Y_Resolution;
}

size_t XEGetFramebufferSz() {
	return fbinfo.size;
}

size_t XEGetPixelsPerLine() {
	return fbinfo.pixelsPerLine;
}

uint32_t XEGetRedMask() {
	return fbinfo.redmask;
}

uint32_t XEGetBlueMask() {
	return fbinfo.bluemask;
}

uint32_t XEGetGreenMask() {
	return fbinfo.greenmask;
}

uint32_t XEGetResvMask() {
	return fbinfo.resvmask;
}
