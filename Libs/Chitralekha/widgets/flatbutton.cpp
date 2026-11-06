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
 * flatbutton.cpp -- flat Gamusa button painter (CH_BUTTON_FLAT).
 *
 *   primary     [  open  ]   gamusa red plate, white label
 *   secondary   [ keep both ] ink hairline on paper
 *   quiet         cancel      red text; hover weaves a thread under it
 *   disabled    sand plate, soft label, no action
 *
 * The classic painter (button.cpp) is untouched; this module only swaps
 * the widget's paint / mouse handlers. One repaint = the button rect.
 */

#include "button.h"
#include "gmstyle.h"
#include <string.h>
#include <stdlib.h>

extern void ChButtonMouseEvent(ChWidget* wid, ChWindow* win, int x, int y, int button);
extern void ChButtonTouchEvent(ChWidget* wid, ChWindow* win, int x, int y);
extern void ChButtonDefaultPainter(ChWidget* widget, ChWindow* win);
extern void ChButtonDestroy(ChWidget* widget, ChWindow* win);

static void ChButtonFlatPainter(ChWidget* widget, ChWindow* win) {
	ChButton* b = (ChButton*)widget;
	ChCanvas* c = win->canv;
	uint16_t f = b->styleFlags;
	int x = widget->x, y = widget->y, w = widget->w, h = widget->h;
	bool off = (f & CH_BUTTON_DISABLED) != 0;
	bool hover = !off && (widget->hover || widget->touched);
	bool press = !off && widget->clicked;
	ChFont* font = win->app->baseFont;

	uint32_t label = CH_GM_WHITE;
	uint32_t bg = win->color;
	int tw = ChGmTextWidth(font, b->title);
	int lz = (f & CH_BUTTON_FLAT_LOZENGE) ? 10 : 0;

	if (f & CH_BUTTON_FLAT_QUIET) {
		/* text only, left aligned like a link */
		ChGmFill(c, x, y, w, h, bg);
		label = off ? CH_GM_INK_SOFT : (press ? CH_GM_RED_DEEP : CH_GM_RED);
		int tx = x + lz;
		int base = y + h / 2 + 4;
		if (lz)
			ChGmLozenge(c, x + 3, base - 4, 3, label);
		ChGmText(c, font, b->title, tx, base, label);
		if (hover || press)
			ChGmThread(c, tx, base + 3, tw, label, CH_GM_RED_THREAD);
		return;
	}
	if (f & CH_BUTTON_FLAT_SECONDARY) {
		uint32_t fill = press ? CH_GM_LINEN_DEEP : (hover ? CH_GM_LINEN : CH_GM_PAPER);
		ChGmFill(c, x, y, w, h, fill);
		ChGmFrame(c, x, y, w, h, off ? CH_GM_SAND : CH_GM_INK);
		label = off ? CH_GM_INK_SOFT : CH_GM_INK;
	} else {
		uint32_t fill = off ? CH_GM_SAND : (press ? CH_GM_RED_DEEP : (hover ? CH_GM_RED_HOVER : CH_GM_RED));
		ChGmFill(c, x, y, w, h, fill);
		label = off ? CH_GM_INK_SOFT : CH_GM_WHITE;
	}
	int tx = x + (w - tw - lz) / 2 + lz;
	int base = y + h / 2 + 4;
	if (lz)
		ChGmLozenge(c, tx - 7, base - 4, 3, label);
	ChRect clip = { x + 2, y, w - 4, h };
	ChGmTextClipped(c, font, b->title, tx, base, label, &clip);
}

static void ChButtonFlatMouseEvent(ChWidget* wid, ChWindow* win, int x, int y, int button) {
	ChButton* b = (ChButton*)wid;
	if (b->styleFlags & CH_BUTTON_DISABLED) {
		/* a disabled button ignores input */
		wid->clicked = false;
		wid->lastMouseX = x;
		wid->lastMouseY = y;
		return;
	}
	ChButtonMouseEvent(wid, win, x, y, button);
}

static void ChButtonFlatTouchEvent(ChWidget* wid, ChWindow* win, int x, int y) {
	ChButton* b = (ChButton*)wid;
	if (b->styleFlags & CH_BUTTON_DISABLED)
		return;
	ChButtonTouchEvent(wid, win, x, y);
}

XE_EXTERN XE_EXPORT void ChButtonSetStyle(ChButton* button, uint16_t flags) {
	if (!button)
		return;
	button->styleFlags = flags;
	if (flags & CH_BUTTON_FLAT) {
		button->base.ChPaintHandler = ChButtonFlatPainter;
		button->base.ChMouseEvent = ChButtonFlatMouseEvent;
		button->base.ChTouchEvent = ChButtonFlatTouchEvent;
	} else {
		button->base.ChPaintHandler = ChButtonDefaultPainter;
		button->base.ChMouseEvent = ChButtonMouseEvent;
		button->base.ChTouchEvent = ChButtonTouchEvent;
	}
}

XE_EXTERN XE_EXPORT ChButton* ChCreateButtonEx(int x, int y, int w, int h, char* text, uint16_t flags) {
	ChButton* b = ChCreateButton(x, y, w, h, text);
	if (b)
		ChButtonSetStyle(b, flags);
	return b;
}
