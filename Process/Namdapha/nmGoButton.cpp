/**
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

/*
 * nmGoButton.cpp -- the Go button: just the Xeneva logo (GoIcon.bmp).
 *
 * No plate behind it. Hover lifts the logo a little toward white, a press
 * dims it; both are computed while drawing, so only one bitmap is loaded
 * (the hover / pressed copies GoIconL / GoIconS are no longer read).
 */

#include "nmdapha.h"
#include <sys/_kefile.h>
#include <sys/mman.h>

static ButtonInfo* gobtn;

enum { GO_REST, GO_HOVER, GO_PRESSED };

/* the logo, with the state applied per pixel: O(icon pixels), no copy */
static void _draw_logo(ButtonInfo* info, ChCanvas* canv, int x, int y, int state) {
	if (!info || !info->imageData)
		return;
	int width = info->iconWidth;
	int height = info->iconHeight;
	int bpp = info->iconBpp ? info->iconBpp : 32;
	int bytes = bpp / 8;
	if (bytes <= 0)
		bytes = 4;
	int pitch = ((width * bpp + 31) / 32) * 4;
	uint8_t* image = info->imageData;
	for (int i = 0; i < height; i++) {
		uint8_t* row = image + (height - 1 - i) * pitch;
		for (int k = 0; k < width; k++) {
			uint8_t* p = row + k * bytes;
			uint32_t b = p[0], g = p[1], r = p[2];
			uint32_t a = bytes >= 4 ? p[3] : 255;
			if (a == 0)
				continue;
			if (state == GO_HOVER) {
				/* 30% toward white and a touch more opaque */
				r += (255 - r) * 77 >> 8;
				g += (255 - g) * 77 >> 8;
				b += (255 - b) * 77 >> 8;
				a += a >> 2;
				if (a > 255)
					a = 255;
			} else if (state == GO_PRESSED) {
				a = a * 160 >> 8;
			}
			ChDrawPixel(canv, x + k, y + i, (a << 24) | (r << 16) | (g << 8) | b);
		}
	}
}

void NamdaphaGoButtonPaint(NamdaphaButton* button, ChWindow* win) {
	if (NmGamusa()) {
		NmGmGoPaint(button, win, gobtn); /* the logo dyed red, on paper */
		return;
	}
	/* clear to the bar's glass, then the logo alone */
	ChDrawRect(win->canv, button->x, button->y, button->w, button->h, NAMDAPHA_COLOR);
	if (!gobtn)
		return;
	int state = button->clicked ? GO_PRESSED : (button->hover ? GO_HOVER : GO_REST);
	int x = button->x + button->w / 2 - gobtn->iconWidth / 2;
	int y = button->y + button->h / 2 - gobtn->iconHeight / 2 + (state == GO_PRESSED ? 1 : 0);
	_draw_logo(gobtn, win->canv, x, y, state);
}

NamdaphaButton* NamdaphaInitialiseGoButton(ChWindow* win) {
	gobtn = NmCreateButtonInfo((char*)"/icons/GoIcon.bmp");
	NmButtonInfoRead(gobtn);
	if (gobtn && !gobtn->imageData) {
		NmButtonInfoRelease(gobtn);
		gobtn = NULL;
	}
	/* the button is exactly the logo's box, centred in the bar */
	int w = gobtn ? gobtn->iconWidth : NAMDAPHA_WIDTH - 20;
	int h = gobtn ? gobtn->iconHeight : 50;
	if (h > win->info->height)
		h = win->info->height;
	NamdaphaButton* gobutton = NmCreateButton(GO_BUTTON_XCOORD, (win->info->height - h) / 2, w, h, (char*)"Go");
	gobutton->drawNamdaphaButton = NamdaphaGoButtonPaint;
	gobutton->nmbuttoninfo = NULL; /* drawn here, never released by the bar */
	gobutton->actionHandler = 0;
	return gobutton;
}
