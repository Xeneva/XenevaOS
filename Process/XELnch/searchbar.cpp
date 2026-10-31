/**
* @file searchbar.h
*
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

#include "searchbar.h"
#include <stdlib.h>
#include <font.h>

/**
 * @brief draw_rrect_filled -- draws rounded rectangle filled
 * @param canv -- Pointer to canvas
 * @param x -- X coordinate
 * @param y -- Y coordinate
 * @param w -- width of the rounded rect
 * @param h -- Height of the rounded rectangle
 */
extern void
draw_rrect_filled(ChCanvas* canv, int x, int y, int w, int h, int radius, uint32_t color);

/**
 * XESearchBarPaint -- paint the search bar 
 * @param wid -- Pointer to chitralekha widget
 * @param win -- Pointer to root window
 */
void XESearchBarPaint(ChWidget* wid, ChWindow* win) {
	XESearchBar* search = (XESearchBar*)wid;
	ChFontSetSize(win->app->baseFont, 16);
	uint32_t fontcol = LIGHTSILVER;
	if (wid->hover) {
		draw_rrect_filled(win->canv, wid->x, wid->y, wid->w, wid->h, 16, GRAY);
		draw_rrect_filled(
			win->canv, wid->x + 2, wid->y + 2, wid->w - 4, wid->h - 4, 16, win->color);
		fontcol = WHITE;
	} else {
		draw_rrect_filled(win->canv, wid->x, wid->y, wid->w, wid->h, 16, LIGHTBLACK);
		draw_rrect_filled(
			win->canv, wid->x + 2, wid->y + 2, wid->w - 4, wid->h - 4, 16, win->color);
	}
	ChRect rect;
	rect.x = wid->x;
	rect.y = wid->y;
	rect.w = wid->w;
	rect.h = wid->h;
	if (search->textPos > 0) {
		int width = ChFontGetWidth(win->app->baseFont, search->text);
		if (width >= (wid->w - 10)) {
			search->_need_scroll = 1;
		}
		if (search->_need_scroll)
			search->scrollOffset += (search->textPos) - (search->prevTextPos);
		else {
			/** check once if scrolling is turned off, but scrolloffset still is in 1, **/
			if (search->scrollOffset == 1 && search->_need_scroll != 1)
				search->scrollOffset = 0;
		}

		ChFontDrawTextClipped(win->canv,
							  win->app->baseFont,
							  search->text + search->scrollOffset,
							  wid->x + 10,
							  wid->y + wid->h - 10,
							  fontcol,
							  &rect);
	} else
		ChFontDrawTextClipped(win->canv,
							  win->app->baseFont,
							  "Search Apps..",
							  wid->x + 30,
							  wid->y + wid->h - 10,
							  fontcol,
							  &rect);

	search->_need_scroll = 0;
}

/**
 * @brief XESearchMouseEvent -- mouse event handler for search bar
 * @param wid -- Pointer to button widget
 * @param win -- Pointer to root window
 * @param x -- X coord of the search bar
 * @param y -- Y coord of the search bar
 * @param button -- mouse button code
 */
void XESearchMouseEvent(ChWidget* wid, ChWindow* win, int x, int y, int button) {
	if (button && !wid->KillFocus)
		wid->clicked = true;

	if (button == 0)
		wid->clicked = false;

	if (wid->KillFocus)
		wid->clicked = false;

	if (!wid->hoverPainted && wid->hover) {
		if (wid->ChPaintHandler)
			wid->ChPaintHandler(wid, win);
		ChWindowUpdate(win, wid->x, wid->y, wid->w, wid->h, false, true);
		wid->hoverPainted = true;
	}

	if (!wid->hover && wid->clicked == false) {
		wid->hoverPainted = false;
		if (wid->ChPaintHandler)
			wid->ChPaintHandler(wid, win);
		ChWindowUpdate(win, wid->x, wid->y, wid->w, wid->h, false, true);
	}

	bool _action_required = false;
	if (wid->clicked && wid->lastMouseX == x && wid->lastMouseY == y) {
		if (wid->ChPaintHandler)
			wid->ChPaintHandler(wid, win);
		ChWindowUpdate(win, wid->x, wid->y, wid->w, wid->h, false, true);

		_action_required = true;
		win->focusedWidget = wid;
		wid->hoverPainted = false;
		wid->clicked = false;
	}

	if (_action_required) {
		/* call the action handler */
		if (wid->ChActionHandler)
			wid->ChActionHandler(wid, win);
	}

	wid->lastMouseX = x;
	wid->lastMouseY = y;
}

/**
 * @brief XECreateSearchBar -- create the app search bar
 * @param x -- x location within launcher
 * @param y -- Y location within launcher
 * @param w -- width of the search bar
 * @param h -- height of the search bar
 */
XESearchBar* XECreateSearchBar(int x, int y, int w, int h) {
	XESearchBar* bar = (XESearchBar*)malloc(sizeof(XESearchBar));
	bar->wid.x = x;
	bar->wid.y = y;
	bar->wid.w = w;
	bar->wid.h = h;
	bar->wid.ChPaintHandler = XESearchBarPaint;
	bar->wid.ChMouseEvent = XESearchMouseEvent;
	bar->textPos = 0;
	memset(bar->text, 0, 1024);
	return bar;
}