/**
* @file button.cpp
* 
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

#include "button.h"

#ifdef THEME_DEFAULT
#define BUTTON_NORMAL_DARK	 0xFF252525
#define BUTTON_NORMAL_LIGHT	 0xFF454343
#define BUTTON_HOVER_DARK	 0xFF0463C0
#define BUTTON_HOVER_LIGHT	 0xFF3F8EDA
#define BUTTON_CLICKED_DARK	 0xFF374E64
#define BUTTON_CLICKED_LIGHT 0xFF485A6C
#elif THEME_PURPLE
#define BUTTON_NORMAL_DARK	 0xFF252525
#define BUTTON_NORMAL_LIGHT	 0xFF454343
#define BUTTON_HOVER_DARK	 0xFF0463C0
#define BUTTON_HOVER_LIGHT	 0xFF1E9D97
#define BUTTON_CLICKED_DARK	 0xFF014D4E
#define BUTTON_CLICKED_LIGHT 0xFF014D4E
#endif

/**
 * @brief ChButtonMouseEvent -- mouse event handler for chitralekha button
 * @param wid -- Pointer to button widget
 * @param win -- Pointer to root window
 * @param x -- X coord of the button
 * @param y -- Y coord of the button
 * @param button -- mouse button code
 */
void ChButtonMouseEvent(ChWidget* wid, ChWindow* win, int x, int y, int button) {
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

void ChButtonTouchEvent(ChWidget* wid, ChWindow* win, int x, int y) {
	if (!wid->KillFocus) {
		wid->clicked = true;
		wid->touched = true;
	}

	if (wid->KillFocus) {
		wid->clicked = false;
		wid->touched = false;
	}

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
	if (wid->clicked) {
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

void ChButtonDestroy(ChWidget* widget, ChWindow* win) {
	ChButton* but = (ChButton*)widget;
	if (win->focusedWidget == widget)
		win->focusedWidget = NULL;
	free(but->title);
	free(but);
	_KePrint("Button destroyed \r\n");
}

/**
 * @brief ChButtonDefaultPainter -- default button painting function
 * @param widget -- Pointer to Widget 
 * @param win -- Pointer to root window
 */
void ChButtonDefaultPainter(ChWidget* widget, ChWindow* win) {
	ChButton* button = (ChButton*)widget;

	uint32_t color1 = BUTTON_NORMAL_LIGHT;
	uint32_t color2 = BUTTON_NORMAL_DARK;

	if (button->base.hover) {
		color1 = BUTTON_HOVER_LIGHT;
		color2 = BUTTON_HOVER_DARK;
	}

	if (button->base.clicked) {
		color1 = BUTTON_CLICKED_LIGHT;
		color2 = BUTTON_CLICKED_DARK;
	}

	if (button->base.touched) {
		color1 = BUTTON_HOVER_LIGHT;
		color2 = BUTTON_HOVER_DARK;
	}

	ChDrawRect(win->canv, widget->x, widget->y, widget->w, widget->h, color1);
	/*ChColorDrawVerticalGradient(win->canv,widget->x, widget->y, 
		widget->w, widget->h, color1, color2);*/
	ChFontSetSize(win->app->baseFont, 11);
	int font_w = ChFontGetWidth(win->app->baseFont, button->title);
	int font_h =
		win->app->baseFont->fontHeight; //ChFontGetHeight(win->app->baseFont, button->title);
	ChFontDrawText(win->canv,
				   win->app->baseFont,
				   button->title,
				   widget->x + widget->w / 2 - font_w / 2,
				   widget->y + widget->h / 2 + 4,
				   11,
				   WHITE);
	ChDrawRectUnfilled(win->canv, widget->x, widget->y, widget->w, widget->h, GRAY);
	ChDrawRectUnfilled(
		win->canv, widget->x + 1, widget->y + 1, widget->w - 2, widget->h - 2, 0xFF9A9A9A);
	ChDrawRectUnfilled(
		win->canv, widget->x + 2, widget->y + 2, widget->w - 4, widget->h - 4, 0xFF9A9A9A);
}
/**
 * @brief ChCreateButton -- Create a button widget
 * @param x -- x coord of the widget
 * @param y -- y coord of the widget
 * @param w -- width of the button
 * @param h -- height of the button
 * @param text -- button text
 */
XE_EXTERN XE_EXPORT ChButton* ChCreateButton(int x, int y, int w, int h, char* text) {
	ChButton* button = (ChButton*)malloc(sizeof(ChButton));
	memset(button, 0, sizeof(ChButton));
	button->base.x = CHITRALEKHA_WINDOW_DEFAULT_PAD_X + x;
	button->base.y = CHITRALEKHA_WINDOW_DEFAULT_PAD_Y + y;
	button->base.w = w;
	button->base.h = h;
	button->title = (char*)malloc(strlen(text) + 1);
	memset(button->title, 0, strlen(text) + 1);
	strcpy(button->title, text);
	button->base.ChMouseEvent = ChButtonMouseEvent;
	button->base.ChPaintHandler = ChButtonDefaultPainter;
	button->base.ChDestroy = ChButtonDestroy;
	button->base.ChTouchEvent = ChButtonTouchEvent;
	return button;
}