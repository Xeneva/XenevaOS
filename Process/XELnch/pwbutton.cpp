/**
* @file pwbutton.h
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

#include "pwbutton.h"
#include <stdlib.h>
#include <draw.h>
#include <math.h>
#include "launcher.h"

void power_button_paint(ChWidget* wid, ChWindow* win) {
	power_button* pb = (power_button*)wid;

	ChDrawRect(
		win->canv, pb->base.x, pb->base.y, pb->base.w, pb->base.h, LAUNCHER_BACKGROUND_COLOR);
	if (pb->base.hover)
		ChColorDrawHorizontalGradient(
			win->canv, pb->base.x, pb->base.y, pb->base.w, pb->base.h, 0xCC658096, 0xCC8CA2B4);

	if (pb->type == POWER_BUTTON_TYPE_SHUTDOWN)
		ChDrawIcon(win->canv, pb->icon, pb->base.x, pb->base.y);

	if (pb->type == POWER_BUTTON_TYPE_RESTART)
		ChDrawIcon(win->canv, pb->icon, pb->base.x, pb->base.y);
}

void power_button_mouse_event(ChWidget* wid, ChWindow* win, int x, int y, int button) {
	power_button* pb = (power_button*)wid;

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
 * @brief create_power_button -- create a new power button
 * @param pw_type -- power type
 */
power_button* create_power_button(uint8_t pw_type, int x, int y) {
	power_button* pbutton = (power_button*)malloc(sizeof(power_button));
	pbutton->base.ChPaintHandler = power_button_paint;
	pbutton->base.ChMouseEvent = power_button_mouse_event;
	pbutton->base.ChTouchEvent = 0; //similar to mouse_event
	pbutton->base.x = x;
	pbutton->base.y = y;
	pbutton->base.w = 35;
	pbutton->base.h = 35;
	pbutton->type = pw_type;
	if (pw_type == POWER_BUTTON_TYPE_SHUTDOWN) {
		ChIcon* shut = ChCreateIcon();
		ChIconOpen(shut, "/icons/pwoff.bmp");
		ChIconRead(shut);
		pbutton->icon = shut;
	}

	if (pw_type == POWER_BUTTON_TYPE_RESTART) {
		ChIcon* reb = ChCreateIcon();
		ChIconOpen(reb, "/icons/reboot.bmp");
		ChIconRead(reb);
		pbutton->icon = reb;
	}
	return pbutton;
}