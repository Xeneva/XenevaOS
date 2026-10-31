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

#include "../chitralekha.h"
#include "../color.h"
#include "../draw.h"
#include "base.h"
#include "window.h"
#include "menubar.h"
#include "menu.h"
#include "../font.h"

#define MENUBAR_COLORLIGHT 0xFF474545
#define MENUBAR_COLORDARK  0xFF232323

void ChDefaultMenubarPainter(ChWidget* wid, ChWindow* win) {
	ChMenubar* mb = (ChMenubar*)wid;
	ChColorDrawVerticalGradient(win->canv,
								mb->wid.x,
								mb->wid.y,
								mb->wid.w,
								mb->wid.h,
								MENUBAR_COLORLIGHT,
								MENUBAR_COLORDARK);
	ChFont* mainFont = win->app->baseFont;
	ChFontSetSize(mainFont, 10);
	int mbut_pox_x = mb->wid.x + 4;
	for (int i = 0; i < mb->menubuttons->pointer; i++) {
		ChMenuButton* mbut = (ChMenuButton*)list_get_at(mb->menubuttons, i);
		int title_w = ChFontGetWidth(mainFont, mbut->title);
		int title_h = ChFontGetHeight(mainFont, mbut->title);
		mbut->wid.w = title_w + 8;
		mbut->wid.x = mbut_pox_x;
		ChColorDrawVerticalGradient(win->canv,
									mbut->wid.x,
									mbut->wid.y,
									mbut->wid.w,
									mbut->wid.h,
									MENUBAR_COLORLIGHT,
									MENUBAR_COLORDARK);
		if (mbut->hover)
			ChDrawRect(win->canv, mbut->wid.x, mbut->wid.y, mbut->wid.w, mbut->wid.h, 0xFF3F8EDA);

		ChFontDrawText(win->canv,
					   mainFont,
					   mbut->title,
					   mbut->wid.x + (mbut->wid.w / 2) - title_w / 2,
					   mbut->wid.y + ((mbut->wid.y + mbut->wid.h) / 2) - title_h / 2,
					   10,
					   WHITE);
		mbut_pox_x += mbut->wid.w + 4;
	}
	if (!mb->allpainted)
		mb->allpainted = true;
}

void ChPopupMenuAppendButton(ChPopupMenu* pm, int x, int y, int w, int h) {
	//ChDrawRect(pm->backWindow->canv, x, y, w, h, WHITE);
	ChDrawLine(pm->backWindow->canv, x, y, x + w, y + h / 2, BLACK);
	ChDrawLine(pm->backWindow->canv, x + w, y + h / 2, x, y + h, BLACK);
}

void ChPopupMenuPaint(ChPopupMenu* popup) {
	ChDrawRect(popup->backWindow->canv, 0, 0, popup->wid.w, popup->wid.h, 0xFFE6CACA);
	int menu_item_height = DEFAULT_MENU_ITEM_HEIGHT;
	int menu_item_y = DEFAULT_MENU_ITEM_BUTTON_PADY;
	for (int i = 0; i < popup->MenuItems->pointer; i++) {
		ChMenuItem* mi = (ChMenuItem*)list_get_at(popup->MenuItems, i);
		if (mi->wid.y == 0)
			mi->wid.y = menu_item_y;
		if (mi->wid.hover) {
			/*ChColorDrawHorizontalGradient(popup->backWindow->canv, 
				0, mi->wid.y, popup->wid.w, DEFAULT_MENU_ITEM_HEIGHT, 0xFF658096, 0xFF8CA2B4);*/
			ChDrawRect(popup->backWindow->canv,
					   0,
					   menu_item_y + 2,
					   popup->wid.w,
					   DEFAULT_MENU_ITEM_HEIGHT - 2,
					   0xFF8CA2B4);
		}
		/*	ChDrawRect(popup->backWindow->canv, 0, menu_item_y, popup->wid.w, DEFAULT_MENU_ITEM_HEIGHT, GREEN);*/
		ChFontDrawText(popup->backWindow->canv,
					   popup->mainWindow->app->baseFont,
					   mi->title,
					   10,
					   menu_item_y + (menu_item_height / 2) + 5,
					   10,
					   BLACK);

		if (mi->menu)
			ChPopupMenuAppendButton(
				popup, popup->wid.w - 15, menu_item_y + menu_item_height / 2 - 7 / 2, 7, 7);

		if (mi->seperator) {
			ChDrawHorizontalLine(
				popup->backWindow->canv, 4, menu_item_y + 1, popup->wid.w - (4 * 2), GRAY);
			menu_item_y += DEFAULT_MENU_ITEM_HEIGHT + DEFAULT_MENU_ITEM_BUTTON_PADY + 2;
		} else
			menu_item_y += DEFAULT_MENU_ITEM_HEIGHT + DEFAULT_MENU_ITEM_BUTTON_PADY;
	}
	ChDrawRectUnfilled(popup->backWindow->canv, 0, 0, popup->wid.w, popup->wid.h, GRAY);
}