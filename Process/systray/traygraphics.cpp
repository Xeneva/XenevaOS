/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2025, Manas Kamal Choudhury
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

#include "traygraphics.h"
#include <chitralekha.h>
#include <widgets/icon.h>
#include <widgets/window.h>

/*
 * TrayDrawIconColorReplace -- replaces a color with new one
 * @param canv -- Pointer to canvas
 * @param ico -- Pointer to icon data
 * @param x -- X location to draw
 * @param y -- Y location to draw
 * @param searchColor -- color to replace
 * @param replaceColor -- color to replace with
 */
void TrayDrawIconColorReplace(
	ChCanvas* canv, ChIcon* ico, int x, int y, uint32_t searchColor, uint32_t replaceColor) {
	if (!ico)
		return;

	uint32_t width = ico->image.width;
	uint32_t height = ico->image.height;
	uint32_t j = 0;

	uint32_t scR, scG, scB;
	scR = GET_RED(searchColor);
	scG = GET_GREEN(searchColor);
	scB = GET_BLUE(searchColor);

	uint32_t rR, rG, rB;
	rR = GET_RED(replaceColor);
	rG = GET_GREEN(replaceColor);
	rB = GET_BLUE(replaceColor);

	uint8_t* image = ico->image.data;
	for (int i = 0; i < height; i++) {
		char* image_row = ((char*)image + (static_cast<uint64_t>(height) - i - 1) *
											  (static_cast<uint64_t>(width) * 4));
		uint32_t h = height - 1 - i;
		j = 0;
		for (int k = 0; k < width; k++) {
			uint32_t b = image_row[j++] & 0xff;
			uint32_t g = image_row[j++] & 0xff;
			uint32_t r = image_row[j++] & 0xff;
			uint32_t a = image_row[j++] & 0xff;

			if (r == scR || g == scG || b == scB)
				r = rR, g = rG, b = rB;

			uint32_t rgb = ((a << 24) | (r << 16) | (g << 8) | (b));
			if (rgb & 0xFF000000)
				ChDrawPixel(canv, x + k, y + i, rgb);
		}
	}
}

void SoundWindowPainter(ChWindow* win) {
	ChDrawRect(win->canv, 0, 0, win->info->width, win->info->height, win->color);
	for (int i = 0; i < win->widgets->pointer; i++) {
		ChWidget* wid = (ChWidget*)list_get_at(win->widgets, i);
		if (wid)
			if (wid->ChPaintHandler)
				wid->ChPaintHandler(wid, win);
	}
	ChDrawRectUnfilled(win->canv, 0, 0, win->info->width, win->info->height, DESKBLUE);
	ChWindowUpdate(win, 0, 0, win->info->width, win->info->height, 1, 0);
}