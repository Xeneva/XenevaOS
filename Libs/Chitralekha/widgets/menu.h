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

#ifndef __MENU_H__
#define __MENU_H__

#include "../chitralekha.h"
#include "base.h"
#include "window.h"
#include "../widgets/list.h"

#ifdef __cplusplus
XE_EXTERN {
#endif

#define DEFAULT_POPUP_MENU_WIDTH	  100
#define DEFAULT_POPUP_MENU_HEIGHT	  15
#define DEFAULT_MENU_ITEM_BUTTON_PADY 0
#define DEFAULT_MENU_ITEM_HEIGHT	  28

	typedef struct _popup_menu_ {
		ChWidget wid;
		ChWindow* mainWindow;
		ChWindow* backWindow;
		list_t* MenuItems;
		int x_loc;
		int y_loc;
		void* lastSelectedMenuItem;
		struct _popup_menu_* parent;
	} ChPopupMenu;

	typedef struct _menu_item_ {
		ChWidget wid;
		char* title;
		ChPopupMenu* menu;
		ChPopupMenu* parent;
		bool seperator;
	} ChMenuItem;

	/*
* ChCreatePopupMenu -- create a new popup menu
* @param mainWin -- Pointer to Main Window
* @param parent -- Parent popup menu
*/
	XE_EXPORT ChPopupMenu* ChCreatePopupMenu(ChWindow * mainWin, ChPopupMenu * parent);

	/*
* ChCreateMenuItem -- create a new menu item
* @param title -- title of the menu item
* @param pm -- Pointer to popup menu
*/
	XE_EXPORT ChMenuItem* ChCreateMenuItem(char* title, ChPopupMenu* pm);

	XE_EXPORT void ChMenuShow(ChPopupMenu * menu, int x, int y);

	/*
* ChMenuHide -- Hide a popup menu and its sub menus
* @param menu -- Pointer to popup menu
*/
	XE_EXPORT void ChMenuHide(ChPopupMenu * menu);

#ifdef __cplusplus
}
#endif

#endif
