/**
* @file sidebar.h
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

#ifndef __SIDEBAR_H__
#define __SIDEBAR_H__

#include "scrollbar.h"
#include "window.h"
#include "icon.h"

#ifdef __cplusplus
XE_EXTERN {
#endif

#define SIDEBAR_MAX_LABEL  64
#define SIDEBAR_ROW_HEIGHT 28
#define SIDEBAR_HEADER	   22
#define SIDEBAR_ICON_SIZE  16
#define SIDEBAR_INDENT	   10

	typedef struct _sidebar_item_ {
		char label[SIDEBAR_MAX_LABEL];
		ChIcon* icon;
		int iconW, iconH;

		void* data;
		void (*OnSelect)(struct _sidebar_item_* item, ChWindow* win);
		int selected;
	} ChSidebarItem;

	typedef struct _sidebar_section_ {
		char title[SIDEBAR_MAX_LABEL];
		ChSidebarItem* items;
		int itemCount;
		int collapsed;
	} ChSidebarSection;

	typedef struct _sidebar_ {
		ChWidget base;

		ChSidebarSection* sections;
		int sectionCount;

		int scrollOfsetY;
		int contentHeight;

		uint32_t bgColor;
		uint32_t selectedColor;
		uint32_t hoverColor;
		uint32_t textColor;
		uint32_t headerTextColor;
		uint32_t selectedTextColor;

		int hoverSectionIdx;
		int hoverItemIdx;
	} ChSidebar;

	/**
 * @brief ChSidebarCreate -- create a new sidebar
 * @param x -- x location of the sidebar
 * @param y -- y location of the sidebar
 * @param w -- width of the sidebar
 * @param h -- height of the sidebar
 */
	XE_EXPORT ChSidebar* ChSidebarCreate(int x, int y, int w, int h);

	XE_EXPORT ChSidebarSection* ChSidebarAddSection(ChSidebar * sb, const char* title);

	XE_EXPORT ChSidebarItem* ChSidebarAddItem(
		ChSidebarSection * sec, const char* label, ChIcon* icon, int iconW, int iconH, void* udata);

#ifdef __cplusplus
}
#endif

#endif
