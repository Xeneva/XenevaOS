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

#include "../chitralekha.h"
#include "../color.h"
#include "../draw.h"
#include "base.h"
#include "window.h"
#include "onoff.h"
#include "menubar.h"
#include "menu.h"
#include "../font.h"
#include "textbox.h"

void ChDefaultTextboxPrint(ChCanvas* canv, ChTextBox* tb, char* text) {
	ChRect clipRect;
	clipRect.x = tb->wid.x;
	clipRect.y = tb->wid.y;
	clipRect.w = tb->wid.w;
	clipRect.h = tb->wid.h;
	char buf[256];
	int textOff = 0;
	bool _has_newline = false;
	int penx = tb->wid.x + (tb->textCursorPosX * 2);
	int peny = tb->wid.y + (tb->textCursorPosY * 19);
	for (int i = 0; i < strlen(text); i++) {
		if (text[i] == '\n') {
			_has_newline = true;
			tb->textCursorPosY += 1;
			tb->textCursorPosX = 1;
			penx = tb->wid.x + (tb->textCursorPosX * 2);
			peny = tb->wid.y + (tb->textCursorPosY * 19);
			textOff = 0;
			continue;
		}
		if (text[i] == '\0')
			break;
		int w = ChFontGetWidthChar(tb->font, text[i]);
		int h = ChFontGetHeightChar(tb->font, text[i]);
#ifdef _USE_FREETYPE
		int x_v = penx + tb->font->face->glyph->bitmap_left;
		int y_v = peny - tb->font->face->glyph->bitmap_top;
		int b_w = tb->font->face->glyph->bitmap.width;
		int draw_width = tb->font->face->glyph->bitmap.width;
#else
		int x_v = penx;
		int y_v = peny;
		int b_w = w;
		int draw_width = w;
#endif

		if (x_v + draw_width >= (tb->wid.x + tb->wid.w)) {
			tb->textCursorPosY += 1;
			tb->textCursorPosX = 1;
			penx = tb->wid.x + (tb->textCursorPosX * 2);
			peny = tb->wid.y + (tb->textCursorPosY * 19);
		}

		ChFontDrawCharClipped(canv, tb->font, text[i], penx, peny, tb->textColor, &clipRect);
#ifdef _USE_FREETYPE
		penx += tb->font->face->glyph->advance.x >> 6;
		peny += tb->font->face->glyph->advance.y >> 6;
#else
		penx += w;
#endif
		tb->textCursorPosX++;
	}

	// Draw the text cursor
	int cursorHeight = 14;
	int cursorY = peny - cursorHeight + 2;
	ChDrawRect(canv, penx, cursorY, 2, cursorHeight, tb->textColor);
	
	tb->textCursorPosX = 2;
	tb->textCursorPosY = 2;
}

/*
 * ChDefaultTextbox -- default xeneva textbox painter
 * module
 * @param wid -- Pointer to this widget
 * @param win -- Pointer to main application window
 */
void ChDefaultTextbox(ChWidget* wid, ChWindow* win) {
	ChTextBox* tb = (ChTextBox*)wid;
	ChDrawRect(win->canv, tb->wid.x, tb->wid.y, tb->wid.w, tb->wid.h, tb->textBackgroundColor);
	ChFontSetSize(tb->font, 14);
	ChRect clipRect;
	clipRect.x = tb->wid.x;
	clipRect.y = tb->wid.y;
	clipRect.w = tb->wid.w;
	clipRect.h = tb->wid.h;
	if (tb->text) {
		ChDefaultTextboxPrint(win->canv, tb, tb->text);
	}
	if (wid->hover) {
		ChDrawRectUnfilled(win->canv, tb->wid.x, tb->wid.y, tb->wid.w, tb->wid.h, 0xFF4067BA);
		ChDrawRectUnfilled(
			win->canv, tb->wid.x + 1, tb->wid.y + 1, tb->wid.w - 2, tb->wid.h - 2, 0xFF6689D5);
	}
}