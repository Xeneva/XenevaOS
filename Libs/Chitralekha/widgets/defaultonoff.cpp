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

#include "../chitralekha.h"
#include "../color.h"
#include "../draw.h"
#include "base.h"
#include "window.h"
#include "onoff.h"
#include "menubar.h"
#include "menu.h"
#include "../font.h"

/*
 * ChDefaultOnOffPainter -- default xeneva onoff button painter
 * module
 * @param wid -- Pointer to this widget
 * @param win -- Pointer to main application window
 */
void ChDefaultOnOffPainter(ChWidget* wid, ChWindow* win) {
	ChOnOffButton* onoff = (ChOnOffButton*)wid;
	int handle_radius = (onoff->wid.h / 2) - 1;

	uint32_t button_hover = ORANGE;
	uint32_t button_color = 0xFFEBFAE8;
	uint32_t capsule_off_col = 0xFFB4BCB2;
	uint32_t capsule_on_col = 0xFF28B217;
	uint32_t capsule_col = capsule_off_col;

	int off_posx = onoff->wid.x + onoff->wid.w / 2 - handle_radius - 2;
	int on_posx = onoff->wid.x + onoff->wid.w / 2 + handle_radius + 2;

	int button_posx = off_posx;
	if (onoff->value) {
		button_posx = on_posx;
		capsule_col = capsule_on_col;
	}

	ChDrawCapsule(win->canv, onoff->wid.x, onoff->wid.y, onoff->wid.w, onoff->wid.h, 0xFF5B6A59);
	ChDrawCapsule(win->canv,
				  onoff->wid.x + 1,
				  onoff->wid.y + 1,
				  onoff->wid.w - 2,
				  onoff->wid.h - 2,
				  capsule_col);

	//button handle
	ChDrawFilledCircle(
		win->canv, button_posx, onoff->wid.y + onoff->wid.h / 2, handle_radius, 0xFF3E4B4C);
	ChDrawFilledCircle(
		win->canv, button_posx, onoff->wid.y + onoff->wid.h / 2, handle_radius - 1, button_color);
}