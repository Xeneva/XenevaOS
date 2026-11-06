/**
* @file gridview.h
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

#ifndef __GRID_VIEW_H__
#define __GRID_VIEW_H__

#include <stdint.h>
#include "scrollbar.h"
#include "icon.h"
#include "window.h"
#include <_xeneva.h>

#ifdef __cplusplus
XE_EXTERN {
#endif

	/* grid type */
	typedef enum {
		GRIDCELL_THUMBNAIL,
		GRIDCELL_WIDGET,
	} ChGridCellType;

	typedef struct _grid_cell_ {
		ChGridCellType type;
		ChIcon* thumbnail;
		char label[64];

		ChWidget* child;
		void* data;
	} ChGridCell;

	typedef struct _grid_view_ {
		ChWidget wid;

		ChGridCell* cells;
		int cell_count;
		int cell_capacity;

		int columns;
		int cell_w;
		int cell_h;
		int cell_padding;

		int selected_index;
		int hover_index;

		int scroll_y;
		int content_height;
		int viewport_height;
		int viewport_width;

		ChScrollBar vscroll;
		uint8_t prev_button;

		int prev_scroll_y;
		bool force_full_redraw;

		uint32_t color_bg;
		uint32_t color_selected_bg;
		uint32_t color_hover_bg;
		uint32_t color_label_text;

		void (*OnSelect)(struct _grid_view_* gv, ChWindow* win, int index, ChGridCell* cell);
	} ChGridView;

	XE_EXPORT ChGridView* ChGridViewCreate(
		int x, int y, int w, int h, int columns, int cell_w, int cell_h, int padding);
	/**
	 * @brief ChGridAddThumbnail -- add a thumbnail to the grid view
	 */
	XE_EXPORT int ChGridAddThumbnail(
		ChGridView * gv, ChIcon * icon, const char* label, void* user_data);

	XE_EXPORT int ChGridAddWidget(ChGridView * gv, ChWidget * child, void* data);

#ifdef __cplusplus
}
#endif

#endif