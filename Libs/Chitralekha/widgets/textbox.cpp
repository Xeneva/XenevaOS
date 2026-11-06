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

#include "textbox.h"

/*
 * ChDefaultTextbox -- default xeneva textbox painter
 * module
 * @param wid -- Pointer to this widget
 * @param win -- Pointer to main application window
 */
extern void ChDefaultTextbox(ChWidget* wid, ChWindow* win);

/*
 * ChTextBoxDestroy -- destroy callback for text box
 * @param widget -- Pointer to widget
 * @param win -- Pointer to window
 */
void ChTextBoxDestroy(ChWidget* widget, ChWindow* win) {
	ChTextBox* textb = (ChTextBox*)widget;
	if (win->focusedWidget == widget)
		win->focusedWidget = NULL;
	free(textb->text);
	free(textb);
}

/*
 * ChTextBoxMouseEvent -- mouse event callback for textbox
 * @param wid -- Pointer to Widget
 * @param win -- Pointer to main window
 * @param x -- X position
 * @param y -- Y position
 * @param button -- Mouse button
 */
void ChTextBoxMouseEvent(ChWidget* wid, ChWindow* win, int x, int y, int button) {
	ChTextBox* tb = (ChTextBox*)wid;
	if (!tb->editable)
		return;
	if (wid->hover) {
		if (wid->ChPaintHandler) {
			wid->ChPaintHandler(wid, win);
			ChWindowUpdate(win, wid->x, wid->y, wid->w, wid->h, 0, 1);
		}
		wid->hoverPainted = true;
	}

	if (!wid->hover && wid->hoverPainted) {
		if (wid->ChPaintHandler) {
			wid->ChPaintHandler(wid, win);
			ChWindowUpdate(win, wid->x, wid->y, wid->w, wid->h, 0, 1);
		}
		wid->hoverPainted = true;
	}
}

/*
 * ChTextBoxUpdate -- updates the textbox with
 * current graphical content
 * @param tb -- Pointer to textbox
 * @param mainWin -- Pointer to main window
 */
void ChTextBoxUpdate(ChTextBox* tb, ChWindow* mainWin) {
	if (tb->wid.ChPaintHandler) {
		tb->wid.ChPaintHandler(&tb->wid, mainWin);
		ChWindowUpdate(mainWin, tb->wid.x, tb->wid.y, tb->wid.w, tb->wid.h, 0, 1);
	}
}

/*
 * ChCreateTextBox -- create a new instance of textbox widget
 * @param x -- X position
 * @param y -- Y position
 * @param width -- Width of the textbox
 * @param height -- Height of the textbox
 */
ChTextBox* ChCreateTextBox(ChWindow* win, int x, int y, int width, int height) {
	ChTextBox* text = (ChTextBox*)malloc(sizeof(ChTextBox));
	memset(text, 0, sizeof(ChTextBox));
	text->wid.x = x;
	text->wid.y = 26 + y;
	text->wid.w = width;
	text->wid.h = height;
	text->wid.ChDestroy = ChTextBoxDestroy;
	text->wid.ChMouseEvent = ChTextBoxMouseEvent;
	text->wid.ChPaintHandler = ChDefaultTextbox;
	text->text = (char*)malloc(CH_TEXTBOX_MAX_STRING);
	text->editable = true;
	text->textColor = BLACK;
	text->textCursorPosX = 2;
	text->textCursorPosY = 2;
	if (win)
		text->font = win->app->baseFont;
	text->textBackgroundColor = WHITE;
	return text;
}

/*
 * ChTextBoxSetText -- set text to textbox
 * @param tb -- Pointer to textbox
 * @param text -- text to set
 */
void ChTextBoxSetText(ChTextBox* tb, char* text) {
	if (tb->text) {
		strcpy(tb->text, text);
	}
}

/*
 * ChTextBoxSetFont -- set font for particular given
 * text box
 * @param tb -- Pointer to textbox
 * @param font -- Font to set
 */
void ChTextBoxSetFont(ChTextBox* tb, ChFont* font) {
	tb->font = font;
}

/*
 * ChTextBoxSetFontSize -- set font size for the text box
 * @param tb -- Pointer to teh textbox
 * @param sz -- font sz in pixels
 */
void ChTextBoxSetFontSize(ChTextBox* tb, int sz) {
	ChFontSetSize(tb->font, sz);
}