/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2023, Manas Kamal Choudhury
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

#include "../color.h"
#include "../draw.h"
#include "window.h"
#include "../font.h"
#include <math.h>
#include "winstyle.h"

#ifdef THEME_DEFAULT
#define DEFAULT_TITLEBAR_DARK		 0xFF252525
#define DEFAULT_TITLEBAR_LIGHT		 0xFF454343
#define DEFAULT_TITLEBAR_FOCUS_LIGHT 0xFF576674
#define DEFAULT_TITLEBAR_FOCUS_DARK	 0xFF454F58
#elif THEME_PURPLE
#define DEFAULT_TITLEBAR_DARK		 0xFF252525
#define DEFAULT_TITLEBAR_LIGHT		 0xFF454343
#define DEFAULT_TITLEBAR_FOCUS_LIGHT 0xFF1E9D97 //0x33576674
#define DEFAULT_TITLEBAR_FOCUS_DARK	 0xFF454F58
#endif

/* When a window opts into the glass pipeline (WINDOW_FLAG_GLASS) the default
 * chrome is drawn with translucent fills too, so the compositor's blurred
 * desktop backdrop shows through the titlebar and edges -- not just the
 * client area the app paints. --axiss */
#define GLASS_TITLEBAR_ALPHA 0xC8
#define GLASS_CHROME_EDGE	 0x5A3E3E3E
#define GLASS_ACTIVITY_EDGE	 0x4D808080

/*
 * ChWindowPaintCloseButton -- close button painter
 */
void ChWindowPaintCloseButton(ChWindow* win, ChWinGlobalControl* button) {
	//ChDrawCircleUnfilled(win->canv, button->x + button->w / 2,button->y + button->h / 2, 8, button->outlineColor);
	/* draw the actual symbol */
	uint32_t outline_col = button->outlineColor;
	if (button->hover) {
		outline_col = button->hoverOutlineColor;
	}

	if (button->clicked) {
		outline_col = button->clickedOutlineColor;
		button->clicked = false;
	}

	ChDrawLine(win->canv,
			   button->x + 7,
			   button->y + 7,
			   button->x + button->w - 7,
			   button->y + button->h - 7,
			   outline_col);
	ChDrawLine(win->canv,
			   button->x + button->w - 7,
			   button->y + 7,
			   button->x + 7,
			   button->y + button->h - 7,
			   outline_col);
}

/*
 * ChWindowPaintMaximButton -- maximize button painter
 */
void ChWindowPaintMaximButton(ChWindow* win, ChWinGlobalControl* button) {
	//ChDrawCircleUnfilled(win->canv, button->x + button->w / 2, button->y + button->h / 2, 8, button->outlineColor);
	/* draw the actual symbol */
	uint32_t outline_col = button->outlineColor;
	if (button->hover) {
		outline_col = button->hoverOutlineColor;
	}

	if (button->clicked) {
		outline_col = button->clickedOutlineColor;
		button->clicked = false;
	}

	ChDrawRectUnfilled(win->canv, button->x + 7, button->y + 7, 7, 7, outline_col);
	ChDrawHorizontalLine(win->canv, button->x + 7, button->y + 7 + 1, 7, outline_col);
}

/* 
 * ChWindowPaintMinimButton -- minimize button painter
 */
void ChWindowPaintMinimButton(ChWindow* win, ChWinGlobalControl* button) {
	uint32_t outline_col = button->outlineColor;
	if (button->hover) {
		outline_col = button->hoverOutlineColor;
	}

	if (button->clicked) {
		outline_col = button->clickedOutlineColor;
		button->clicked = false;
	}

	ChDrawHorizontalLine(win->canv, button->x + 7, button->y + button->h / 2, 7, outline_col);
	ChDrawHorizontalLine(win->canv, button->x + 7, button->y + button->h / 2 + 1, 7, outline_col);
}

void ChWindowPaintTitlebar(ChWindow* win) {
	/* styled chrome (knot, ...) paints its own bar; the classic path
	 * below is unchanged */
	const ChWindowStyleOps* style = ChWindowStyleOf(win);
	if (style) {
		if (style->PaintTitlebar)
			style->PaintTitlebar(win);
		return;
	}
	uint32_t light_color = DEFAULT_TITLEBAR_LIGHT;
	uint32_t dark_color = DEFAULT_TITLEBAR_DARK;
	if (win->focused) {
		light_color = DEFAULT_TITLEBAR_FOCUS_LIGHT;
		dark_color = DEFAULT_TITLEBAR_FOCUS_DARK;
	}

	if (win->flags & WINDOW_FLAG_GLASS) {
		light_color = SET_ALPHA(light_color, GLASS_TITLEBAR_ALPHA);
		dark_color = SET_ALPHA(dark_color, GLASS_TITLEBAR_ALPHA);
	}

	ChDrawRect(win->canv, 0, 0, win->info->width, 26, light_color);
	ChFont* font = win->app->baseFont;
	ChFontSetSize(win->app->baseFont, 10);
	int font_width = ChFontGetWidth(font, win->title);
	int font_height = ChFontGetHeight(font, win->title);
	ChFontDrawText(
		win->canv, font, win->title, win->info->width / 2 - font_width / 2, 26 / 2 + 4, 16, WHITE);
	ChDrawRectUnfilled(win->canv,
					   0,
					   0,
					   win->info->width,
					   26,
					   (win->flags & WINDOW_FLAG_GLASS) ? GLASS_CHROME_EDGE : LIGHTBLACK);

	for (int i = 0; i < win->GlobalControls->pointer; i++) {
		ChWinGlobalControl* global = (ChWinGlobalControl*)list_get_at(win->GlobalControls, i);
		if (global) {
			if (global->ChGlobalButtonPaint)
				global->ChGlobalButtonPaint(win, global);
		}
	}
}

/*
 * ChWindowPaintMainActivity -- paints the main activity area
 */
void ChWindowPaintMainActivity(ChWindow* win) {
	for (int i = 0; i < win->widgets->pointer; i++) {
		ChWidget* wid = (ChWidget*)list_get_at(win->widgets, i);
		if (wid->ChPaintHandler)
			wid->ChPaintHandler(wid, win);
	}
	ChDrawRectUnfilled(win->canv,
					   0,
					   0,
					   win->info->width,
					   win->info->height,
					   (win->flags & WINDOW_FLAG_GLASS) ? GLASS_ACTIVITY_EDGE : GRAY);
}

void ChDefaultWinPaint(ChWindow* win) {
	ChDrawRect(win->canv, 0, 0, win->info->width, win->info->height, win->color);
	ChWindowPaintTitlebar(win);
	ChWindowPaintMainActivity(win);
	ChWindowUpdate(win, 0, 0, win->info->width, win->info->height, 1, 0);
}

void ChDefaultPopupWinPaint(ChWindow* popup) {
	ChDrawRect(popup->canv, 0, 0, popup->info->width, popup->info->height, 0xFFD2D2D2);
	for (int i = 0; i < popup->widgets->pointer; i++) {
		ChWidget* wid = (ChWidget*)list_get_at(popup->widgets, i);
		if (wid->ChPaintHandler)
			wid->ChPaintHandler(wid, popup);
	}
	ChDrawRectUnfilled(popup->canv, 0, 0, popup->info->width, popup->info->height, GRAY);
}

#define CORNER_RADIUS 12

void _apply_rounded_corner(uint32_t* backbuff, int radius, int winw, int winh) {
	int w = winw;
	int h = winh;
	for (int y = 0; y < radius; y++) {
		for (int x = 0; x < radius; x++) {
			float dx = (float)(radius - 1 - x);
			float dy = (float)(radius - 1 - y);
			float dist = sqrtf(dx * dx + dy * dy) - (float)(radius - 1);

			float a = 1.0f - fmaxf(0.0f, fminf(1.0f, dist + 0.5f));
			uint8_t alpha = (uint8_t)(a * 255.0f);

			uint32_t* tl = &backbuff[y * w + x];
			*tl = (*tl & 0x00FFFFFF) | ((uint32_t)alpha << 24);

			uint32_t* tr = &backbuff[y * w + (w - 1 - x)];
			*tr = (*tr & 0x00FFFFFF) | ((uint32_t)alpha << 24);

			uint32_t* bl = &backbuff[(h - 1 - y) * w + x];
			*bl = (*bl & 0x00FFFFFF) | ((uint32_t)alpha << 24);

			uint32_t* br = &backbuff[(h - 1 - y) * w + (w - 1 - x)];
			*br = (*br & 0x00FFFFFF) | ((uint32_t)alpha << 24);
		}
	}
}