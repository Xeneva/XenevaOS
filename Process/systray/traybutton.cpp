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

#include "systray.h"
#include "traygraphics.h"
#include <sys/_keproc.h>

static int _tray_y_loc;

void TrayButtonMouseEvent(ChWidget* wid, ChWindow* win, int x, int y, int button) {
	TrayButton* tb = (TrayButton*)wid;
	if (button && !wid->KillFocus)
		wid->clicked = true;

	if (button == 0)
		wid->clicked = false;

	if (wid->KillFocus)
		wid->clicked = false;

	/* Mouse Hover code */
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
		if (tb->type == TRAY_BUTTON_TYPE_ONOFF)
			if (tb->onoff)
				tb->onoff = 0;
			else
				tb->onoff = 1;

		if (wid->ChPaintHandler)
			wid->ChPaintHandler(wid, win);
		ChWindowUpdate(win, wid->x, wid->y, wid->w, wid->h, false, true);

		_action_required = true;

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

void TrayButtonPaint(ChWidget* wid, ChWindow* win) {
#define ICON_ON_COLOR 0xFF96ACF2

	TrayButton* tb = (TrayButton*)wid;
	uint32_t col = LIGHTBLACK;
	uint32_t icoColor = LIGHTSILVER;

	if (wid->hover) {
		col = LIGHTSILVER;
		icoColor = LIGHTBLACK;
	}

	if (wid->clicked)
		col = GRAY;

	if (tb->type == TRAY_BUTTON_TYPE_ONOFF)
		if (tb->onoff)
			icoColor = ICON_ON_COLOR;

	ChDrawRect(win->canv, wid->x, wid->y, wid->w, wid->h, col);

	TrayDrawIconColorReplace(win->canv, tb->icon, wid->x, wid->y, BLACK, icoColor);
}

/*
 * TrayCreateButton -- create a tray button
 * @param title -- title of the button
 * @param x -- X location of the button
 * @param y -- Y location of the button
 * @param width -- Width of the button
 * @param height -- Height of the button
 */
TrayButton* TrayCreateButton(char* title, int x, int y, int width, int height) {
	TrayButton* tb = (TrayButton*)malloc(sizeof(TrayButton));
	memset(tb, 0, sizeof(TrayButton));
	tb->base.x = x;
	tb->base.y = _tray_y_loc;
	tb->base.w = width;
	tb->base.h = height;
	tb->base.ChMouseEvent = TrayButtonMouseEvent;
	tb->base.ChPaintHandler = TrayButtonPaint;
	tb->title = (char*)malloc(strlen(title));
	memset(tb->title, 0, strlen(title));
	strcpy(tb->title, title);
	tb->icon = NULL;
	tb->frameCount = 100;

	_tray_y_loc += tb->base.h + 15;
	return tb;
}

void TrayButtonInitialize() {
	_tray_y_loc = 15;
}
