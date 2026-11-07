/**
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
#ifndef __TOOLBAR_H__
#define __TOOLBAR_H__

#include "window.h"
#include <_xeneva.h>
#include "base.h"
#include "icon.h"

typedef enum _titemType_ {
	TOOLBAR_ITEM_BUTTON,
	TOOLBAR_ITEM_TOGGLE,
	TOOLBAR_ITEM_RADIO,
	TOOLBAR_ITEM_SEPARATOR,
	TOOLBAR_ITEM_WIDGET,
} ToolBarItemType;

#define TOOLBAR_STATE_NORMAL   0
#define TOOLBAR_STATE_HOVER	   1
#define TOOLBAR_STATE_PRESSED  2
#define TOOLBAR_STATE_ACTIVE   3
#define TOOLBAR_STATE_DISABLED 4

typedef enum _toolbar_orientation_ { TOOLBAR_HORIZONTAL, TOOLBAR_VERTICAL } ChToolbarOrientation;

#define TOOLBAR_MAX_ITEMS		64
#define TOOLBAR_BTN_PADDING		6
#define TOOLBAR_SEP_THICKNESS	1
#define TOOLBAR_SEP_MARGIN		4
#define TOOLBAR_OVERFLOW_BTN_SZ 14

#ifdef __cplusplus
XE_EXTERN {
#endif

	typedef struct _toolbar_item_ {
		uint8_t type;
		uint8_t state;
		ChIcon* icon;
		int iconW;
		int iconH;

		char tooltip[48];

		uint8_t groupID;
		uint8_t active;

		ChWidget* child;
		int child_w;
		int child_h;

		int offset;
		int extent;

		void* userData;
	} ChToolBarItem;

	typedef struct _toolbar_ {
		ChWidget base;

		ChToolBarItem items[TOOLBAR_MAX_ITEMS];
		int item_count;

		uint8_t orientation;
		int buttonPadding;

		int scrollOffset;
		int contentLength;
		int viewportLength;

		int hoverIndex;
		int pressedIndex;
		uint8_t prevButton;

		uint8_t show_overflow;
		int overflow_btn_pos;

		uint32_t color_bg;
		uint32_t color_btn_hover;
		uint32_t color_btn_pressed;
		uint32_t color_btn_active;
		uint32_t color_btn_disabled;
		uint32_t color_separator;
		uint32_t color_overflow_arrow;

		void (*onItemStateChange)(struct _toolbar_* tb, int index);
	} ChToolbar;

	XE_EXPORT ChToolbar* ChToolbarCreate(int x, int y, int w, int h, uint8_t orientation);
	XE_EXPORT int ChToolbarAddButton(
		ChToolbar * tb, ChIcon * icon, int iw, int ih, const char* tooltip, void* ud);

	XE_EXPORT int ChToolbarAddToggle(
		ChToolbar * tb, ChIcon * icon, int iw, int ih, const char* tooltip, void* ud);
	XE_EXPORT int ChToolbarAddRadio(ChToolbar * tb,
									ChIcon * icon,
									int iw,
									int ih,
									const char* tooltip,
									uint8_t groupID,
									void* ud);
	XE_EXPORT int ChToolbarAddSeparator(ChToolbar * tb);
	XE_EXPORT int ChToolbarAddWidget(ChToolbar * tb, ChWidget * child, int childW, int childH);
	XE_EXPORT void ChToolbarSetEnabled(ChToolbar * tb, int index, int enabled);
	XE_EXPORT void ChToolbarSetActive(ChToolbar * tb, int index, int active);

#ifdef __cplusplus
}
#endif

#endif