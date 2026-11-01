/**
* @file gridview.cpp
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

#include "gridview.h"
#include <stdlib.h>

#define BASELINE_RATIO_NUM 4
#define BASELINE_RATIO_DEN 5
#define LABEL_HEIGHT	   16

extern void
_neon_fill_rect32(ChCanvas* canv, int stride_px, int x, int y, int w, int h, uint32_t color);
extern void _neon_cpy_row32(uint32_t* dst, const uint32_t* src, int w);
extern void
_neon_shift_rect_vertical(uint32_t* fb, int stride_px, int x, int y, int w, int h, int delta_y);

void ChGridViewMouseEvent(ChWidget* wid, ChWindow* win, int mx, int my, int button);
void ChGridViewDraw(ChWidget* gv, ChWindow* win);

/** 
 * @brief _ch_grid_row_count -- calculate the number
 * row count in the grid
 * @param gv -- Pointer to Grid view
 */
static int _ch_grid_row_count(ChGridView* gv) {
	if (gv->columns <= 0)
		return 0;
	return (gv->cell_count + gv->columns - 1) / gv->columns;
}

static void _reposition_child(ChGridView* gv) {
	for (int i = 0; i < gv->cell_count; i++) {
		if (gv->cells[i].type != GRIDCELL_WIDGET || gv->cells[i].child == NULL)
			continue;
		int row = i / gv->columns;
		int col = i % gv->columns;

		int cell_x = gv->wid.x + gv->cell_padding + col * (gv->cell_w + gv->cell_padding);
		int cell_y =
			gv->wid.y + gv->cell_padding + row * (gv->cell_h + gv->cell_padding) - gv->scroll_y;

		gv->cells[i].child->x = cell_x;
		gv->cells[i].child->y = cell_y;
	}
}
/**
 * @brief _recalc_grid_metrics -- recalculate entire
 * grid metrics
 * @param gv -- pointer to griv view
 */
static void _recalc_grid_metrics(ChGridView* gv) {
	int rows = _ch_grid_row_count(gv);
	gv->content_height = rows * (gv->cell_h + gv->cell_padding) + gv->cell_padding;

	gv->viewport_height = gv->wid.h;
	gv->viewport_width = gv->wid.w - 16; //16 is the scrollbar width

	ChScrollBarSetRange(&gv->vscroll, 0, gv->content_height, gv->viewport_height);

	int max_scroll = gv->content_height - gv->viewport_height;
	if (max_scroll < 0)
		max_scroll = 0;
	if (gv->scroll_y > max_scroll)
		gv->scroll_y = max_scroll;

	_reposition_child(gv);

	gv->force_full_redraw = true;
}

/**
 * @brief ChOnGridVScroll -- vertical scroll handle for grid veiw
 * @param widget -- pointer to scrollbar widget
 * @param win -- pointer to main window
 * @param currentVal -- current scroll value
 * @param orientation -- scrollbar orientation
 */
static void ChOnGridVScroll(ChWidget* widget, ChWindow* win, int currentVal, uint8_t orientation) {
	ChScrollBar* sb = (ChScrollBar*)widget;
	ChGridView* gv = (ChGridView*)sb->attachedTo;
	gv->scroll_y = currentVal;
	gv->force_full_redraw = true;
	_reposition_child(gv);
}

/**
 * @brief ChGridViewCreate -- create a new fresh grid view
 * @param x -- najanu moi
 * @param y -- janibi de toi
 * @param w -- eitu moi bujam neki??
 * @param h -- bujubi de toi nije
 * @param columns -- kiba bhagore lagisi
 * @param cell_w -- tuk bujabo mon nai
 * @param cell_h -- eman khiniu nije buji napao ?
 * @param padding -- olop jaga eribi etu xitur majot, noholi
 * atha khai thakibo
 */
ChGridView*
ChGridViewCreate(int x, int y, int w, int h, int columns, int cell_w, int cell_h, int padding) {
	ChGridView* gv = (ChGridView*)malloc(sizeof(ChGridView));
	memset(gv, 0, sizeof(ChGridView));

	gv->wid.x = x;
	gv->wid.y = y;
	gv->wid.w = w;
	gv->wid.h = h;
	gv->wid.ChPaintHandler = ChGridViewDraw;
	gv->wid.ChMouseEvent = ChGridViewMouseEvent;

	gv->columns = columns;
	gv->cell_w = cell_w;
	gv->cell_h = cell_h;
	gv->cell_padding = padding;

	gv->selected_index = -1;
	gv->hover_index = -1;
	gv->prev_scroll_y = -1;

	gv->cell_capacity = 32;
	gv->cells = (ChGridCell*)malloc(sizeof(ChGridCell) * gv->cell_capacity);
	gv->cell_count = 0;

	gv->color_bg = 0xFF1E1E1E;
	gv->color_selected_bg = 0xFF3A6EA5;
	gv->color_hover_bg = 0xFF2A2A2A;
	gv->color_label_text = 0xFFCCCCCC;

	ChScrollBarInit(&gv->vscroll, x + w - 16, y, 16, h, SCROLLBAR_ORIENTATION_VERTICAL);
	gv->vscroll.base.ChScrollEvent = ChOnGridVScroll;
	gv->vscroll.attachedTo = gv;

	_recalc_grid_metrics(gv);
	return gv;
}

static ChGridCell* PushCell(ChGridView* gv) {
	if (gv->cell_count >= gv->cell_capacity) {
		gv->cell_capacity *= 2;
		gv->cells = (ChGridCell*)realloc(gv->cells, sizeof(ChGridCell) * gv->cell_capacity);
	}

	ChGridCell* cell = &gv->cells[gv->cell_count];
	memset(cell, 0, sizeof(ChGridCell));
	return cell;
}

/**
 * @brief ChGridAddThumbnail -- add a thumbnail to the grid view
 */
int ChGridAddThumbnail(ChGridView* gv, ChIcon* icon, const char* label, void* user_data) {
	ChGridCell* cell = PushCell(gv);
	cell->type = GRIDCELL_THUMBNAIL;
	cell->thumbnail = icon;
	if (label) {
		strncpy(cell->label, label, sizeof(cell->label) - 1);
		cell->label[sizeof(cell->label) - 1] = '\0';
	}
	cell->data = user_data;
	int index = gv->cell_count++;
	_recalc_grid_metrics(gv);
	return index;
}

int ChGridAddWidget(ChGridView* gv, ChWidget* child, void* data) {
	ChGridCell* cell = PushCell(gv);
	cell->type = GRIDCELL_WIDGET;
	cell->child = child;
	cell->data = data;

	int index = gv->cell_count++;
	_recalc_grid_metrics(gv);
	return index;
}

void ChGridViewRemoveItem(ChGridView* gv, int index) {
	if (index < 0 || index >= gv->cell_count)
		return;

	memmove(&gv->cells[index],
			&gv->cells[index + 1],
			sizeof(ChGridCell) * (gv->cell_count - index - 1));
	gv->cell_count--;

	if (gv->selected_index == index)
		gv->selected_index = -1;
	else if (gv->selected_index > index)
		gv->selected_index--;

	_recalc_grid_metrics(gv);
}

void ChGridViewClear(ChGridView* gv) {
	gv->cell_count = 0;
	gv->selected_index = -1;
	gv->scroll_y = 0;
	_recalc_grid_metrics(gv);
}

static int _cell_baseline_y(int topy, int h) {
	int ascent = (h * BASELINE_RATIO_NUM) / BASELINE_RATIO_DEN;
	if (ascent > h)
		ascent = h;
	return topy + ascent;
}

static void _grid_draw_range(ChGridView* gv, ChWindow* win, int clip_y, int clip_h) {
	if (clip_y < gv->wid.y) {
		clip_h -= (gv->wid.y - clip_y);
		clip_y = gv->wid.y;
	}
	if (clip_y + clip_h > gv->wid.y + gv->viewport_height)
		clip_h = (gv->wid.y + gv->viewport_height) - clip_y;
	if (clip_h <= 0)
		return;

	_neon_fill_rect32(win->canv,
					  win->canv->canvasWidth,
					  gv->wid.x,
					  clip_y,
					  gv->viewport_width,
					  clip_h,
					  gv->color_bg);

	ChRect clip;
	clip.x = gv->wid.x;
	clip.y = gv->wid.y;
	clip.w = gv->viewport_width;
	clip.h = gv->viewport_height;

	int row_pitch = gv->cell_h + gv->cell_padding;

	for (int i = 0; i < gv->cell_count; i++) {
		int row = i / gv->columns;
		int col = i % gv->columns;

		int cell_top = gv->wid.y + gv->cell_padding + row * row_pitch - gv->scroll_y;
		int cell_left = gv->wid.x + gv->cell_padding + col * (gv->cell_w + gv->cell_padding);

		if (cell_top + gv->cell_h <= clip_y || cell_top >= clip_y + clip_h)
			continue;
		if (cell_top + gv->cell_h <= gv->wid.y || cell_top >= gv->wid.y + gv->viewport_height)
			continue;

		uint32_t bg = gv->color_bg;
		if (i == gv->selected_index)
			bg = gv->color_selected_bg;
		else if (i == gv->hover_index)
			bg = gv->color_hover_bg;

		if (bg != gv->color_bg) {
			int dy = cell_top, dh = gv->cell_h;
			if (dy < clip_y) {
				int diff = clip_y - dy;
				dy += diff;
				dh -= diff;
			}
			if (dy + dh > clip_y + clip_h)
				dh = (clip_y + clip_h) - dy;
			if (dh > 0)
				_neon_fill_rect32(
					win->canv, win->canv->canvasWidth, cell_left, dy, gv->cell_w, dh, bg);
		}

		ChGridCell* cell = &gv->cells[i];

		if (cell->type == GRIDCELL_THUMBNAIL) {
			if (cell->thumbnail) {
				int thumb_area_h = gv->cell_h - (cell->label[0] ? LABEL_HEIGHT : 0);

				ChDrawIconClipped(win->canv, cell->thumbnail, cell_left, cell_top, &clip);

				if (cell->label[0]) {
					int penx = cell_left + 2;
					int peny = _cell_baseline_y(cell_top + thumb_area_h, LABEL_HEIGHT);
					ChFontSetSize(win->app->baseFont, 13);
					ChFontDrawTextClipped(win->canv,
										  win->app->baseFont,
										  cell->label,
										  penx,
										  peny,
										  gv->color_label_text,
										  &clip);
				}
			}
		}

		if (cell->type == GRIDCELL_WIDGET) {}
	}
}

static void _draw_single_cell(ChGridView* gv, ChWindow* win, int index, ChRect* out_rect) {
	if (!gv || !gv->wid.visible || index < 0 || index >= gv->cell_count)
		return;

	int row = index / gv->columns;
	int col = index % gv->columns;
	int row_pitch = gv->cell_h + gv->cell_padding;

	int cell_top = gv->wid.y + gv->cell_padding + row * row_pitch - gv->scroll_y;
	int cell_left = gv->wid.x + gv->cell_padding + col * (gv->cell_w + gv->cell_padding);

	if (cell_top + gv->cell_h <= gv->wid.y || cell_top >= gv->wid.y + gv->viewport_height)
		return;

	int draw_y = cell_top, draw_h = gv->cell_h;
	if (draw_y < gv->wid.y) {
		int diff = gv->wid.y - draw_y;
		draw_y += diff;
		draw_h -= diff;
	}

	if (draw_y + draw_h > gv->wid.y + gv->viewport_height)
		draw_h = (gv->wid.y + gv->viewport_height) - draw_y;
	if (draw_h <= 0)
		return;

	uint32_t bg = gv->color_bg;
	if (index == gv->selected_index)
		bg = gv->color_selected_bg;
	else if (index == gv->hover_index)
		bg = gv->color_hover_bg;

	_neon_fill_rect32(win->canv, win->canv->canvasWidth, cell_left, draw_y, gv->cell_w, draw_h, bg);

	ChGridCell* cell = &gv->cells[index];
	ChRect clip;
	clip.x = gv->wid.x;
	clip.y = gv->wid.y;
	clip.w = gv->viewport_width;
	clip.h = gv->viewport_height;

	if (cell->type == GRIDCELL_THUMBNAIL) {
		if (cell->thumbnail) {
			int thumb_area_h = gv->cell_h - (cell->label[0] ? LABEL_HEIGHT : 0);
			ChDrawIconClipped(win->canv, cell->thumbnail, cell_left, cell_top, &clip);

			if (cell->label[0]) {
				int pen_x = cell_left + 2;
				int pen_y = _cell_baseline_y(cell_top + thumb_area_h, LABEL_HEIGHT);
				ChFontDrawTextClipped(win->canv,
									  win->app->baseFont,
									  cell->label,
									  pen_x,
									  pen_y,
									  gv->color_label_text,
									  &clip);
			}
		}
	}

	if (out_rect) {
		out_rect->x = cell_left;
		out_rect->y = draw_y;
		out_rect->w = gv->cell_w;
		out_rect->h = draw_h;
	}
}

void ChGridViewDraw(ChWidget* wid, ChWindow* win) {
	ChGridView* gv = (ChGridView*)wid;
	if (!gv || !gv->wid.visible)
		return;
	int delta_y = gv->scroll_y - gv->prev_scroll_y;

	int full_redraw =
		(gv->prev_scroll_y < 0) || gv->force_full_redraw || (abs(delta_y) >= gv->viewport_height);

	if (full_redraw) {
		_grid_draw_range(gv, win, gv->wid.y, gv->viewport_height);
	} else if (delta_y != 0) {
		_neon_shift_rect_vertical(win->canv->buffer,
								  win->canv->canvasWidth,
								  gv->wid.x,
								  gv->wid.y,
								  gv->viewport_width,
								  gv->viewport_height,
								  -delta_y);

		if (delta_y > 0)
			_grid_draw_range(gv, win, gv->wid.y + gv->viewport_height - delta_y, delta_y);
		else
			_grid_draw_range(gv, win, gv->wid.y, -delta_y);
	}

	gv->prev_scroll_y = gv->scroll_y;
	gv->force_full_redraw = false;

	if (gv->vscroll.base.ChPaintHandler)
		gv->vscroll.base.ChPaintHandler((ChWidget*)&gv->vscroll, win);
}

void ChGridViewDrawChildren(ChGridView* gv, ChWindow* win) {
	for (int i = 0; i < gv->cell_count; i++) {
		if (gv->cells[i].type != GRIDCELL_WIDGET || gv->cells[i].child == NULL)
			continue;

		ChWidget* child = gv->cells[i].child;

		if (child->y + child->h <= gv->wid.y || child->y >= gv->wid.y + gv->viewport_height)
			continue;

		//child-draw call
	}
}

static int _grid_index_at(ChGridView* gv, ChWindow* win, int mx, int my) {
	int lx = mx - win->info->x;
	int ly = my - win->info->y;

	if (lx < gv->wid.x || lx >= gv->wid.x + gv->viewport_width || ly < gv->wid.y ||
		ly >= gv->wid.y + gv->viewport_height)
		return -1;

	int content_y = (ly - gv->wid.y) + gv->scroll_y;
	int row_pitch = gv->cell_h + gv->cell_padding;

	int row = (content_y - gv->cell_padding) / row_pitch;
	int row_local_y = (content_y - gv->cell_padding) - row * row_pitch;
	if (row_local_y < 0 || row_local_y >= gv->cell_h)
		return -1;

	int col_x = (lx - gv->wid.x - gv->cell_padding) % (gv->cell_w + gv->cell_padding);
	int col = (lx - gv->wid.x - gv->cell_padding) / (gv->cell_w + gv->cell_padding);

	if (col_x < 0 || col_x >= gv->cell_w)
		return -1;
	if (col < 0 || col >= gv->columns)
		return -1;

	int index = row * gv->columns + col;
	if (index < 0 || index >= gv->cell_count)
		return -1;
	return index;
}

static int is_over_vscrollbar(ChGridView* gv, ChWindow* win, int mx, int my) {
	int lx = mx - win->info->x, ly = my - win->info->y;
	return (lx >= gv->vscroll.base.x && lx < gv->vscroll.base.x + gv->vscroll.base.w &&
			ly >= gv->vscroll.base.y && ly < gv->vscroll.base.y + gv->vscroll.base.h);
}

void ChGridViewMouseEvent(ChWidget* wid, ChWindow* win, int mx, int my, int button) {
	ChGridView* gv = (ChGridView*)wid;
	if (!gv || !gv->wid.visible)
		return;
	uint8_t was_down = gv->prev_button;
	uint8_t is_down = (button != 0);

	// Mouse wheel scrolling
	if (button == DEODHAI_MOUSE_MSG_SCROLL_UP) {
		int old_scroll = gv->scroll_y;
		int step = (gv->cell_h + gv->cell_padding);
		if (step < 30) step = 30;
		gv->scroll_y -= step;
		if (gv->scroll_y < 0)
			gv->scroll_y = 0;
		if (gv->scroll_y != old_scroll) {
			_reposition_child(gv);
			gv->force_full_redraw = true;
			ChScrollBarSetValue(&gv->vscroll, gv->scroll_y);
			ChGridViewDraw(wid, win);
			ChWindowUpdate(win, gv->wid.x, gv->wid.y, gv->wid.w, gv->wid.h, 0, 1);
		}
		return;
	} else if (button == DEODHAI_MOUSE_MSG_SCROLL_DOWN) {
		int max_scroll = gv->content_height - gv->viewport_height;
		if (max_scroll < 0) max_scroll = 0;
		int old_scroll = gv->scroll_y;
		int step = (gv->cell_h + gv->cell_padding);
		if (step < 30) step = 30;
		gv->scroll_y += step;
		if (gv->scroll_y > max_scroll)
			gv->scroll_y = max_scroll;
		if (gv->scroll_y != old_scroll) {
			_reposition_child(gv);
			gv->force_full_redraw = true;
			ChScrollBarSetValue(&gv->vscroll, gv->scroll_y);
			ChGridViewDraw(wid, win);
			ChWindowUpdate(win, gv->wid.x, gv->wid.y, gv->wid.w, gv->wid.h, 0, 1);
		}
		return;
	}

	if (is_over_vscrollbar(gv, win, mx, my) || gv->vscroll.state == SB_STATE_DRAGGING) {
		if (button != 0) {
			win->focusedWidget = (void*)wid;
		} else {
			if (win->focusedWidget == (void*)wid)
				win->focusedWidget = NULL;
		}
		if (gv->vscroll.base.ChMouseEvent)
			gv->vscroll.base.ChMouseEvent((ChWidget*)&gv->vscroll, win, mx, my, button);
		gv->force_full_redraw = true;
		ChGridViewDraw(wid, win);
		ChWindowUpdate(win, gv->wid.x, gv->wid.y, gv->wid.w, gv->wid.h, 0, 1);
		return;
	}

	int index = _grid_index_at(gv, win, mx, my);

	if (index != -1 && gv->cells[index].type == GRIDCELL_WIDGET && gv->cells[index].child &&
		gv->cells[index].child->ChMouseEvent) {
		gv->cells[index].child->ChMouseEvent(gv->cells[index].child, win, mx, my, button);
	}

	if (!was_down && is_down) {
		if (index != -1) {
			int old_selected = gv->selected_index;
			gv->selected_index = index;
			if (gv->OnSelect)
				gv->OnSelect(gv, win, index, &gv->cells[index]);
			if (!gv->wid.visible) {
				gv->prev_button = is_down;
				return;
			}
			gv->force_full_redraw = true;
			ChRect dirty;
			if (old_selected != -1 && old_selected != index) {
				_draw_single_cell(gv, win, old_selected, &dirty);
				ChWindowUpdate(win, dirty.x, dirty.y, dirty.w, dirty.h, 0, 1);
			}
			_draw_single_cell(gv, win, index, &dirty);
			ChWindowUpdate(win, dirty.x, dirty.y, dirty.w, dirty.h, 0, 1);
		}
		gv->prev_button = is_down;
		return;
	}

	if (!was_down && !is_down) {
		if (index != gv->hover_index) {
			int old_hover = gv->hover_index;
			gv->hover_index = index;
			gv->force_full_redraw = true;

			ChRect dirty;
			if (old_hover != -1) {
				_draw_single_cell(gv, win, old_hover, &dirty);
				ChWindowUpdate(win, dirty.x, dirty.y, dirty.w, dirty.h, 0, 1);
			}
			if (index != -1) {
				_draw_single_cell(gv, win, index, &dirty);
				ChWindowUpdate(win, dirty.x, dirty.y, dirty.w, dirty.h, 0, 1);
			}
			//ChGridViewDraw(wid, win);
		}
	}

	gv->prev_button = is_down;
}
