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

#include "menubar.h"
#include <stdlib.h>
#include <sys/_keproc.h>

extern void ChDefaultMenubarPainter(ChWidget* wid, ChWindow* win);

/*
 * ChMenubarDestroy -- free up all allocated resources by menubar
 * @param wid -- Pointer to menubar widget
 * @param win -- Pointer to main window
 */
void ChMenubarDestroy(ChWidget* wid, ChWindow* win) {
	ChMenubar* mb = (ChMenubar*)wid;
	_KePrint("Destroying menubar \r\n");
	for (int i = 0; i < mb->menubuttons->pointer; i++) {
		ChMenuButton* mbut = (ChMenuButton*)list_get_at(mb->menubuttons, i);
		if (mbut->wid.ChDestroy)
			mbut->wid.ChDestroy((ChWidget*)mbut, win);
	}
	list_clear_all(mb->menubuttons);
	free(mb->menubuttons);
	free(mb);
	//_KePrint("Menubar destroyed \r\n");
	//for (;;);
}

/*
 * ChMenubarMouseEvent -- menubar mouse event handler
 * @param wid -- Pointer to Menubar widget
 * @param win -- Pointer to Chitralekha Window
 * @param x -- Mouse x
 * @param y -- Mouse y
 * @param button -- Mouse button state
 */
void ChMenubarMouseEvent(ChWidget* wid, ChWindow* win, int x, int y, int button) {
	bool _need_repaint_ = false;
	ChMenubar* mb = (ChMenubar*)wid;
	ChMenuButton* clickedButton = NULL;
	/* open on the press edge only. The old check (same x/y as the
	 * previous event) dropped clicks whose press arrived together with a
	 * small move, and re-opened the menu on every event while the
	 * button was held still. mb->wid.clicked holds last event's button. */
	bool pressed = button && !mb->wid.clicked;
	if (mb->allpainted) {
		for (int i = 0; i < mb->menubuttons->pointer; i++) {
			ChMenuButton* mbut = (ChMenuButton*)list_get_at(mb->menubuttons, i);
			bool inside = (x >= (win->info->x + mbut->wid.x) &&
						   x < (win->info->x + mbut->wid.x + mbut->wid.w)) &&
						  (y >= (win->info->y + mbut->wid.y) &&
						   y < (win->info->y + mbut->wid.y + mbut->wid.h));
			bool clicked = inside && pressed;
			/* repaint only when a button's look changes, not on every
			 * mouse move over the menubar */
			if (mbut->hover != inside || mbut->clicked != clicked) {
				mbut->hover = inside;
				mbut->clicked = clicked;
				_need_repaint_ = true;
			}
			if (clicked)
				clickedButton = mbut;
		}
	}

	if (_need_repaint_) {
		mb->wid.ChPaintHandler(wid, win);
		ChWindowUpdate(win, wid->x, wid->y, wid->w - 1, wid->h, false, true);
	}

	mb->wid.clicked = button ? true : false;
	mb->wid.lastMouseX = x;
	mb->wid.lastMouseY = y;

	/* show last: ChMenuShow sends compositor messages and the menu may
	 * take pointer focus, so finish this widget's own update first */
	if (clickedButton && clickedButton->popupMenu) {
		ChMenuShow(clickedButton->popupMenu, clickedButton->wid.x, clickedButton->wid.y + 26);
		mb->lastActiveMenu = clickedButton->popupMenu;
	}
}
/*
 * ChCreateMenubar -- create a new menubar
 * @param win -- Pointer to the parent window
 */
ChMenubar* ChCreateMenubar(ChWindow* win) {
	ChMenubar* mb = (ChMenubar*)malloc(sizeof(ChMenubar));
	memset(mb, 0, sizeof(ChMenubar));
	mb->wid.x = 1;
	mb->wid.y = 26;
	mb->wid.w = win->info->width - 1;
	mb->wid.h = 26;
	mb->wid.ChMouseEvent = ChMenubarMouseEvent;
	mb->wid.ChPaintHandler = ChDefaultMenubarPainter;
	mb->wid.ChDestroy = ChMenubarDestroy;
	mb->menubuttons = initialize_list();
	mb->allpainted = false;
	return mb;
}

/*
 * ChMenubuttonDestroy -- frees up all allocated resources
 * by menu button
 */
void ChMenuButtonDestroy(ChWidget* wid, ChWindow* win) {
	ChMenuButton* mbut = (ChMenuButton*)wid;
	_KePrint("Menubutton destroyed \r\n");
	free(mbut->title);
	free(mbut);
}
/*
 * ChCreateMenubutton -- create a new menubar button
 * @param mb -- Pointer to menubar
 * @param title -- title of the button
 */
ChMenuButton* ChCreateMenubutton(ChMenubar* mb, char* title) {
	int title_len = strlen(title) + 1;
	int mbut_w = title_len + 10;
	ChMenuButton* mbut = (ChMenuButton*)malloc(sizeof(ChMenuButton));
	memset(mbut, 0, sizeof(ChMenuButton));
	mbut->wid.x = 0;
	mbut->wid.y = mb->wid.y;
	mbut->wid.w = mbut_w;
	mbut->wid.h = mb->wid.h;
	mbut->wid.ChDestroy = ChMenuButtonDestroy;
	mbut->wid.ChActionHandler = 0;
	mbut->title = (char*)malloc(strlen(title) + 1);
	mbut->popupMenu = NULL;
	memset(mbut->title, 0, strlen(title) + 1);
	strcpy(mbut->title, title);
	return mbut;
}

/*
 * ChMenubarAddButton -- add a button to menubar button list
 */
void ChMenubarAddButton(ChMenubar* mb, ChMenuButton* mbut) {
	list_add(mb->menubuttons, mbut);
}

/*
 * ChMenuButtonAddMenu -- add popup menu to given menubutton
 * @param mbutton -- Pointer to Menu Button
 * @param popup -- Pointer to Popup Menu
 */
void ChMenuButtonAddMenu(ChMenuButton* mbutton, ChPopupMenu* popup) {
	mbutton->popupMenu = popup;
}