/**
* @file glwin.cpp
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

#include <chitralekha.h>
#include <widgets/base.h>
#include <widgets/window.h>
#include "glimpse.h"

#define GLIMPSE_TITLEBAR_LIGHT		0xFFd3d1d1
#define GLIMPSE_TITLEBAR_DARK		0xFFB5B5B5
#define GLIMPSE_TITLEBAR_FOCUS_DARK 0xFFA3AFBB

#define FONT_BASELINE_RATIO_NUM 4
#define FONT_BASELINE_RATIO_DEN 5

static int _win_title_baselineY(int row_top_y, int row_height) {
	int ascent = (row_height * FONT_BASELINE_RATIO_NUM) / FONT_BASELINE_RATIO_DEN;
	if (ascent > row_height)
		ascent = row_height;
	return row_top_y + ascent;
}

void GlimpsePaintTitlebar(ChWindow* win) {
	uint32_t light_color = GLIMPSE_TITLEBAR_LIGHT;
	uint32_t dark_color = win->color; //GLIMPSE_TITLEBAR_DARK;
	if (win->focused) {
		light_color = GLIMPSE_TITLEBAR_LIGHT;
		dark_color = GLIMPSE_TITLEBAR_FOCUS_DARK;
	}

	//ChDrawRect(win->canv, 0, 0, win->info->width, 26, light_color);
	ChColorDrawVerticalGradient(win->canv, 0, 0, win->info->width, 26, light_color, dark_color);
	ChFont* font = win->app->baseFont;
	ChFontSetSize(win->app->baseFont, 11);
	int font_width = ChFontGetWidth(font, win->title);
	int font_height = ChFontGetHeight(font, win->title);
	int peny = _win_title_baselineY(0, 26);
	ChFontDrawText(
		win->canv, font, win->title, win->info->width / 2 - font_width / 2, peny, 16, BLACK);
	//ChDrawRectUnfilled(win->canv, 0, 0, win->info->width, 26, LIGHTBLACK);

	for (int i = 0; i < win->GlobalControls->pointer; i++) {
		ChWinGlobalControl* global = (ChWinGlobalControl*)list_get_at(win->GlobalControls, i);
		if (global) {
			if (global->ChGlobalButtonPaint)
				global->ChGlobalButtonPaint(win, global);
		}
	}
}

void GlimpseWindowPaint(ChWindow* win) {
	ChDrawRect(win->canv, 0, 0, win->info->width, win->info->height, win->color);

	/** draw all widgets */
	for (int i = 0; i < win->widgets->pointer; i++) {
		ChWidget* wid = (ChWidget*)list_get_at(win->widgets, i);
		if (wid->visible && wid->ChPaintHandler)
			wid->ChPaintHandler(wid, win);
	}

	GlimpsePaintTitlebar(win);
	ChWindowUpdate(win, 0, 0, win->info->width, win->info->height, 1, 0);
}