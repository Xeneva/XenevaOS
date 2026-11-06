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
 * winstyle.cpp -- style lookup and switching for window chrome.
 */

#include "winstyle.h"
#include <string.h>

extern void ChDefaultWinPaint(ChWindow* win);
extern void ChWindowPaintCloseButton(ChWindow* win, ChWinGlobalControl* button);
extern void ChWindowPaintMaximButton(ChWindow* win, ChWinGlobalControl* button);
extern void ChWindowPaintMinimButton(ChWindow* win, ChWinGlobalControl* button);
extern void ChGlobalCtlMouseHandler(ChWindow* win, ChWinGlobalControl* global, int x, int y, int button);

/* index = style id; NULL = classic chrome. Ids 2 and 3 are free for
 * future modules: add the ops table here, nothing else changes. */
static const ChWindowStyleOps* const _styles[4] = { NULL, &ChKnotWindowStyle, NULL, NULL };

const ChWindowStyleOps* ChWindowStyleOf(ChWindow* win) {
	if (!win)
		return NULL;
	return _styles[CH_WINDOW_STYLE_OF(win->flags)];
}

void ChWindowStyleInstall(ChWindow* win) {
	const ChWindowStyleOps* st = ChWindowStyleOf(win);
	if (st && st->Install)
		st->Install(win);
}

/* put the classic controls back exactly where ChCreateWindow puts them */
static void _restore_classic(ChWindow* win) {
	win->ChWinPaint = ChDefaultWinPaint;
	win->styleState = 0;
	int n = 0;
	for (int i = 0; i < (int)win->GlobalControls->pointer; i++) {
		ChWinGlobalControl* g = (ChWinGlobalControl*)list_get_at(win->GlobalControls, i);
		if (!g || g->type == WINDOW_GLOBAL_CONTROL_CUSTOM)
			continue;
		g->y = CHITRALEKHA_WINDOW_DEFAULT_PAD_Y / 2 - 20 / 2;
		g->w = g->h = 20;
		g->hover = g->clicked = false;
		g->ChGlobalMouseEvent = ChGlobalCtlMouseHandler;
		if (g->type == WINDOW_GLOBAL_CONTROL_CLOSE) {
			g->x = win->info->width - 25;
			g->ChGlobalButtonPaint = ChWindowPaintCloseButton;
		} else if (g->type == WINDOW_GLOBAL_CONTROL_MAXIMIZE) {
			g->x = win->info->width - 25 - 20;
			g->ChGlobalButtonPaint = ChWindowPaintMaximButton;
		} else {
			g->x = win->info->width - 25 - 20 * 2;
			g->ChGlobalButtonPaint = ChWindowPaintMinimButton;
		}
		n++;
	}
	(void)n;
}

/*
 * ChWindowSetStyle -- switch the chrome of a window at run time.
 * @param win -- Pointer to window
 * @param style -- WINDOW_STYLE_DEFAULT, WINDOW_STYLE_KNOT, ...
 * Call ChWindowPaint afterwards to show it.
 */
XE_EXTERN XE_EXPORT void ChWindowSetStyle(ChWindow* win, uint8_t style) {
	if (!win || style > 3)
		return;
	if (CH_WINDOW_STYLE_OF(win->flags) == style)
		return;
	/* through the compositor too: it keeps the title bar's drag zone off
	 * the controls of the style (classic: right edge, knot: left edge) */
	ChWindowSetFlags(win, (uint16_t)((win->flags & ~WINDOW_STYLE_MASK) | (style << WINDOW_STYLE_SHIFT)));
	if (_styles[style] == NULL)
		_restore_classic(win);
	else
		ChWindowStyleInstall(win);
}

XE_EXTERN XE_EXPORT uint8_t ChWindowGetStyle(ChWindow* win) {
	return win ? (uint8_t)CH_WINDOW_STYLE_OF(win->flags) : 0;
}
