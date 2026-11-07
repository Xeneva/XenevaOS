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

#ifndef __MENUBAR_H__
#define __MENUBAR_H__

#include "../chitralekha.h"
#include "../color.h"
#include "base.h"
#include "window.h"
#include "list.h"
#include <_xeneva.h>
#include "menu.h"

#ifdef __cplusplus
XE_EXTERN {
#endif

#pragma pack(push, 1)
	typedef struct _menubar_ {
		ChWidget wid;
		list_t* menubuttons;
		bool allpainted;
		ChPopupMenu* lastActiveMenu;
	} ChMenubar;
#pragma pack(pop)

	typedef struct _menubutton_ {
		ChWidget wid;
		char* title;
		bool hover;
		bool clicked;
		ChPopupMenu* popupMenu;
	} ChMenuButton;

	/*
	* ChCreateMenubar -- create a new menubar
	* @param win -- Pointer to the parent window
	*/
	XE_LIB ChMenubar* ChCreateMenubar(ChWindow * win);
	XE_LIB ChMenuButton* ChCreateMenubutton(ChMenubar * mb, char* title);
	XE_LIB void ChMenubarAddButton(ChMenubar * mb, ChMenuButton * mbut);
	/*
	* ChMenuButtonAddMenu -- add popup menu to given menubutton
	* @param mbutton -- Pointer to Menu Button
	* @param popup -- Pointer to Popup Menu
	*/
	XE_LIB void ChMenuButtonAddMenu(ChMenuButton * mbutton, ChPopupMenu * popup);

#ifdef __cplusplus
}
#endif

#endif