/**
* BSD 2-Clause License
*
* Copyright (c) 2025, Manas Kamal Choudhury
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

#include "onoff.h"
#include <stdlib.h>

extern void ChDefaultOnOffPainter(ChWidget* wid, ChWindow* win);

/*
 * ChOnOffMouseEvent -- mouse event handler for onoff button
 * @param wid -- Pointer to onoff widget
 * @param win -- Pointer to application window
 * @param x -- Mouse x location
 * @param y -- Mouse y location
 * @param button -- Mouse button state
 */
void ChOnOffMouseEvent(ChWidget* wid, ChWindow* win, int x, int y, int button) {
	ChOnOffButton* onoff = (ChOnOffButton*)wid;
	bool actionRequired = false;
	if (button && onoff->wid.lastMouseX == x && onoff->wid.lastMouseY == y) {
		if (onoff->value)
			onoff->value = 0;
		else
			onoff->value = CH_ONOFF_VALUE_ON;

		if (onoff->wid.ChPaintHandler)
			onoff->wid.ChPaintHandler(wid, win);

		ChWindowUpdate(win, onoff->wid.x, onoff->wid.y, onoff->wid.w, onoff->wid.h, 0, 1);
		wid->hoverPainted = false;
		actionRequired = true;
	}

	/* call appropriate action handler */
	if (actionRequired)
		if (wid->ChActionHandler)
			wid->ChActionHandler(wid, win);

	onoff->wid.lastMouseX = x;
	onoff->wid.lastMouseY = y;
}

/*
 * ChOnOffScrollEvent -- onoff button scroll event
 * @param wid -- Pointer to onoff scroll widget
 * @param win -- Pointer to main application window
 * @param scrollval -- Scroll value
 * @param scrollType -- type of the scroll horiz-vert
 */
void ChOnOffScrollEvent(ChWidget* wid, ChWindow* win, int scrollval, uint8_t scrollType) {}

/*
 * ChOnOffDestroy -- destroy the on off button
 * @param wid -- Pointer to onoff button
 * @param win -- Pointer to main application window
 */
void ChOnOffDestroy(ChWidget* wid, ChWindow* win) {
	ChOnOffButton* onoff = (ChOnOffButton*)wid;
	free(onoff);
}
/*
 * ChCreateOnOffButton -- creates a onoff button
 * @param x -- X location on the window
 * @param y -- Y location on the window
 * @param defaultState -- default state of the button
 */
ChOnOffButton* ChCreateOnOffButton(int x, int y, uint8_t defaultState) {
	ChOnOffButton* onoff = (ChOnOffButton*)malloc(sizeof(ChOnOffButton));
	memset(onoff, 0, sizeof(ChOnOffButton));
	onoff->wid.x = x;
	onoff->wid.y = y;
	onoff->wid.w = 50;
	onoff->wid.h = 25;
	onoff->wid.ChPaintHandler = ChDefaultOnOffPainter;
	onoff->wid.ChMouseEvent = ChOnOffMouseEvent;
	onoff->wid.ChScrollEvent = ChOnOffScrollEvent;
	onoff->wid.ChDestroy = ChOnOffDestroy;
	if (defaultState > 1)
		defaultState = 0;

	onoff->value = defaultState;
	return onoff;
}

/*
 * ChOnOffGetValue -- return onoff buttons value
 * @param onoff -- Pointer onoff button
 */
uint8_t ChOnOffGetValue(ChOnOffButton* onoff) {
	return onoff->value;
}