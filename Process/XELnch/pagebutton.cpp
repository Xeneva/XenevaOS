/**
* @file pagebutton.cpp
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

#include "pagebutton.h"
#include <stdlib.h>

void draw_arrow_header(ChCanvas* canv, int cx, int cy, int r, int dir, uint32_t col) {
	int half_w = r * 55 / 100;
	int height = r * 55 / 100;

	int tip_offset = r * 10 / 100;

	int tip_x, tip_y, left_x, left_y, right_x, right_y;

	if (dir == 0) {
		/* up arrow */
		tip_x = cx;
		tip_y = cy - tip_offset - height / 2;
		left_x = cx - half_w;
		left_y = cy - tip_offset + height / 2;
		right_x = cx + half_w;
		right_y = cy - tip_offset + height / 2;
	} else {
		/* down arrow */
		tip_x = cx;
		tip_y = cy + tip_offset + height / 2;
		left_x = cx - half_w;
		left_y = cy + tip_offset - height / 2;
		right_x = cx + half_w;
		right_y = cy + tip_offset - height / 2;
	}

	ChDrawLine(canv, tip_x, tip_y, left_x, left_y, col);
	ChDrawLine(canv, tip_x, tip_y, right_x, right_y, col);
}

void XEPageButtonPaint(ChWidget* wid, ChWindow* win) {
	XEPageButton* pb = (XEPageButton*)wid;

	int cx = wid->x + wid->w / 2;
	int cy = wid->y + wid->h / 2;

	uint32_t outline = LIGHTSILVER;
	if (pb->disabled)
		outline = GRAY;
	if (wid->hover)
		outline = DESKBLUE;

	int r = (wid->w < wid->h ? wid->w : wid->h) / 2 - 1;
	ChDrawCircleUnfilled(win->canv, cx, cy, r, outline);

	int dir = 0;
	if (pb->type == PAGE_BUTTON_UP)
		dir = 0;
	else if (pb->type == PAGE_BUTTON_DOWN)
		dir = 1;

	draw_arrow_header(win->canv, cx, cy, r, dir, outline);
}

void XEPageButtonMouseEvent(ChWidget* wid, ChWindow* win, int x, int y, int button) {
	XEPageButton* pb = (XEPageButton*)wid;
	if (pb->disabled)
		return;

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

XEPageButton* CreatePageButton(int x, int y, int w, int h, uint8_t type) {
	XEPageButton* but = (XEPageButton*)malloc(sizeof(XEPageButton));
	but->wid.x = x;
	but->wid.y = y;
	but->wid.w = w;
	but->wid.h = h;
	but->type = type;
	but->disabled = false;
	but->wid.ChPaintHandler = XEPageButtonPaint;
	but->wid.ChMouseEvent = XEPageButtonMouseEvent;
	return but;
}