/**
* @file listview.h
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

#ifndef __LIST_VIEW_H__
#define __LIST_VIEW_H__

#include "base.h"
#include "../chitralekha.h"
#include "window.h"
#include "scrollbar.h"
#include <_xeneva.h>
#include "icon.h"

#ifdef __cplusplus
XE_EXTERN {
#endif

#define LIST_VIEW_MAX_TEXT				64
#define LIST_VIEW_MAX_COMUMNS			8
#define LIST_VIEW_ICON_COLUMN_DEFAULT_W 20
#define LIST_VIEW_ICON_PADDING			4

	typedef struct _list_column_ {
		char header[32];
		int width;
		int minWidth;
	} ListColumn;

	typedef struct _ListItem_ {
		char cells[LIST_VIEW_MAX_COMUMNS][LIST_VIEW_MAX_TEXT];
		void* data;
		ChIcon* icon;
		int iconW;
		int iconH;
	} ListItem;

	typedef struct _List_View_ {
		ChWidget base;
		ListItem* item;
		int itemCount;
		int itemCapacity;

		ListColumn columns[LIST_VIEW_MAX_COMUMNS];
		int columnCount;
		int headerHeight;

		int itemHeight;
		int selectedIndex;
		int hoverIndex;

		int scroll_y;
		int scroll_x;
		int content_height;
		int content_width;
		int viewport_height;
		int viewport_width;

		int icon_column_width;
		int icon_padding;

		ChScrollBar vscroll;
		ChScrollBar hscroll;
		uint8_t prev_button;

		int resizingColumn;
		int resizeStartMouseX;
		int resizeStartWidth;

		int prev_scroll_y;
		int prev_scroll_x;
		bool force_full_redraw;
		void (*onItemAction)(struct _List_View_* lv, int index, ListItem* item);
		size_t last_click_tick;
		int last_click_row;
	} ListView;

	/**
 * @brief ChListViewCreate -- create a new list view
 * @param x -- X location within the window
 * @param y -- Y location within the window
 * @param w -- Width of the view
 * @param h -- Height of the view
 * @param itemHeight -- each row item height
 */
	XE_EXPORT ListView* ChListViewCreate(int x, int y, int w, int h, int itemHeight);

	/** 
 * @brief ListViewAddItem -- add a new item to the listview
 * @param lv -- pointer to the list view
 * @param cell_texts -- item text with each column details
 * @param cell_count -- number of cells
 * @param user_data -- any data that this item holds
 */
	XE_EXPORT ListItem* ListViewAddItem(
		ListView * lv, const char** cell_texts, int cell_count, void* user_data);

	/**
 * @brief ListViewSetItemIcon -- add a icon to the item
 * @param lv -- pointer to the list view
 * @param index -- item index number
 * @param icon -- icon to add
 * @param iconW -- icon width
 * @param iconH -- icon height
 */
	XE_EXPORT void ListViewSetItemIcon(
		ListView * lv, int index, ChIcon* icon, int iconW, int iconH);
	XE_EXPORT void ListViewAddColumn(ListView * lv, const char* header, int width, int minWidth);
	XE_EXPORT void ListViewRemoveItem(ListView * lv, int index);
	XE_EXPORT void ListViewClear(ListView * lv);
	XE_EXPORT ListItem* ListViewGetSelectedItem(ListView* lv);
	XE_EXPORT void ListViewSetActionHandler(
		ListView* lv, void (*handler)(ListView* lv, int index, ListItem* item));

#ifdef __cplusplus
}
#endif

#endif
