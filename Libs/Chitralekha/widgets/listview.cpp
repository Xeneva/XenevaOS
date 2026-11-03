/**
* @file listview.cpp
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

#include "listview.h"
#include <arm_neon.h>
#include <sys/_keproc.h>

#define LV_COLOR_BG				0xFF1E1E1E
#define LV_COLOR_TEXT			0xFFE0E0E0
#define LV_SELECTED_BG			0xFF3A6EA5
#define LV_SELECTED_TEXT		0xFFFFFFFF
#define LV_COLOR_HOVER_BG		0xFF2A2A2A
#define LV_COLOR_HEADER_TEXT	0xFFCCCCCC
#define LV_COLOR_HEADER_BG		0xFF2D2D2D
#define LV_COLOR_GRID_LINE		0xFF3A3A3A
#define COLUMN_RESIZE_HANDLE_PX 4
#define MAX_SCANLINE_PX			4096

void OnListViewScroll(ChWidget* wid, ChWindow* win, int currentVal, uint8_t orientation);
void OnListViewHScroll(ChWidget* wid, ChWindow* win, int currentVal, uint8_t orientation);
void ListViewDraw(ChWidget* wid, ChWindow* win);
void ListViewMouseEvent(ChWidget* wid, ChWindow* win, int mx, int my, int button);

static void lv_recalc_content_metric(ListView* lv) {
	lv->content_height = lv->itemCount * lv->itemHeight;
	lv->content_width = 0;

	for (int c = 0; c < lv->columnCount; c++)
		lv->content_width += lv->columns[c].width;

	lv->viewport_height = lv->base.h - lv->headerHeight - 16;
	lv->viewport_width = lv->base.w - 16;

	ChScrollBarSetRange(&lv->vscroll, 0, lv->content_height, lv->viewport_height);
	ChScrollBarSetRange(&lv->hscroll, 0, lv->content_width, lv->viewport_width);

	int max_scroll = lv->content_height - lv->viewport_height;
	if (max_scroll < 0)
		max_scroll = 0;
	if (lv->scroll_y > max_scroll)
		lv->scroll_y = max_scroll;

	int max_scroll_x = lv->content_width - lv->viewport_width;
	if (max_scroll_x < 0)
		max_scroll_x = 0;
	if (lv->scroll_x > max_scroll_x)
		lv->scroll_x = max_scroll_x;
}

ListView* ChListViewCreate(int x, int y, int w, int h, int itemHeight) {
	ListView* lv = (ListView*)malloc(sizeof(ListView));
	memset(lv, 0, sizeof(ListView));

	lv->base.x = x;
	lv->base.y = y;
	lv->base.w = w;
	lv->base.h = h;
	lv->base.visible = true;
	lv->base.ChPaintHandler = ListViewDraw;
	lv->base.ChMouseEvent = ListViewMouseEvent;

	lv->itemHeight = itemHeight;
	lv->headerHeight = 22;
	lv->selectedIndex = -1;
	lv->hoverIndex = -1;
	lv->resizingColumn = -1;
	lv->scroll_y = 0;
	lv->viewport_height = h;
	lv->prev_scroll_x = -1;
	lv->prev_scroll_y = -1;
	lv->force_full_redraw = true;
	lv->onItemAction = NULL;
	lv->last_click_tick = 0;
	lv->last_click_row = -1;

	lv->itemCapacity = 32;
	lv->item = (ListItem*)malloc(sizeof(ListItem) * lv->itemCapacity);
	lv->itemCount = 0;
	lv->columnCount = 0;

	lv->icon_column_width = LIST_VIEW_ICON_COLUMN_DEFAULT_W;
	lv->icon_padding = LIST_VIEW_ICON_PADDING;
	//lv->visibleCount = h / itemHeight;

	ChScrollBarInit(&lv->vscroll,
					x + w - 16,
					y + lv->headerHeight,
					16,
					h - lv->headerHeight - 16,
					SCROLLBAR_ORIENTATION_VERTICAL);
	lv->vscroll.base.ChScrollEvent = OnListViewScroll;
	lv->vscroll.attachedTo = lv;

	ChScrollBarInit(&lv->hscroll, x, y + h - 16, w - 16, 16, SCROLLBAR_ORIENTATION_HORIZONTAL);
	lv->hscroll.base.ChScrollEvent = OnListViewHScroll;
	lv->hscroll.attachedTo = lv;

	lv_recalc_content_metric(lv);
	return lv;
}

void ListViewAddColumn(ListView* lv, const char* header, int width, int minWidth) {
	if (lv->columnCount >= LIST_VIEW_MAX_COMUMNS)
		return;
	ListColumn* col = &lv->columns[lv->columnCount++];
	strncpy(col->header, header, sizeof(col->header) - 1);
	col->header[sizeof(col->header) - 1] = '\0';
	col->width = width;
	col->minWidth = minWidth;
	lv_recalc_content_metric(lv);
}

ListItem* ListViewAddItem(ListView* lv, const char** cell_texts, int cell_count, void* user_data) {
	if (lv->itemCount >= lv->itemCapacity) {
		lv->itemCapacity *= 2;
		lv->item = (ListItem*)realloc(lv->item, sizeof(ListItem) * lv->itemCapacity);
	}

	ListItem* it = &lv->item[lv->itemCount++];
	memset(it->cells, 0, sizeof(it->cells));

	int n = cell_count < lv->columnCount ? cell_count : lv->columnCount;
	for (int c = 0; c < n; c++) {
		strncpy(it->cells[c], cell_texts[c], LIST_VIEW_MAX_TEXT - 1);
		it->cells[c][LIST_VIEW_MAX_TEXT - 1] = '\0';
	}
	it->data = user_data;
	it->icon = NULL;
	it->iconW = 0;
	it->iconH = 0;
	lv->force_full_redraw = true;
	lv_recalc_content_metric(lv);
	return it;
}

void ListViewSetItemIcon(ListView* lv, int index, ChIcon* icon, int iconW, int iconH) {
	if (index < 0 || index >= lv->itemCount)
		return;

	ListItem* it = &lv->item[index];
	it->icon = icon;
	it->iconW = iconW;
	it->iconH = iconH;

	if (icon != NULL && iconW + lv->icon_padding * 2 > lv->icon_column_width) {
		lv->icon_column_width = iconW + lv->icon_padding * 2;
		lv_recalc_content_metric(lv);
		lv->force_full_redraw = 1;
	}
}

void ListViewRemoveItem(ListView* lv, int index) {
	if (index < 0 || index >= lv->itemCount)
		return;

	memmove(&lv->item[index], &lv->item[index + 1], sizeof(ListItem) * (lv->itemCount - index - 1));
	lv->itemCount--;

	if (lv->selectedIndex == index)
		lv->selectedIndex = -1;
	else if (lv->selectedIndex > index)
		lv->selectedIndex--;
	lv->force_full_redraw = true;
	lv_recalc_content_metric(lv);
}

void ListViewClear(ListView* lv) {
	lv->itemCount = 0;
	lv->selectedIndex = -1;
	lv->last_click_row = -1;
	lv->scroll_y = 0;
	lv->scroll_x = 0;
	lv->force_full_redraw = true;
	lv_recalc_content_metric(lv);
}

ListItem* ListViewGetSelectedItem(ListView* lv) {
	if (!lv)
		return NULL;
	if (lv->selectedIndex >= 0 && lv->selectedIndex < lv->itemCount)
		return &lv->item[lv->selectedIndex];
	return NULL;
}

void ListViewSetActionHandler(
	ListView* lv, void (*handler)(ListView* lv, int index, ListItem* item)) {
	if (lv)
		lv->onItemAction = handler;
}

void OnListViewScroll(ChWidget* wid, ChWindow* win, int currentVal, uint8_t orientation) {
	ChScrollBar* sb = (ChScrollBar*)wid;
	ListView* lv = (ListView*)sb->attachedTo;
	lv->scroll_y = currentVal;
	lv->force_full_redraw = true;
}

void OnListViewHScroll(ChWidget* wid, ChWindow* win, int currentVal, uint8_t orientation) {
	ChScrollBar* sb = (ChScrollBar*)wid;
	ListView* lv = (ListView*)sb->attachedTo;
	lv->scroll_x = currentVal;
	lv->force_full_redraw = true;
}

#define BASELINE_RATIO_NUM 4
#define BASELINE_RATIO_DEN 5

static int _RowBaselineY(ListView* lv, int row_top_y, int row_height) {
	int ascent = (row_height * BASELINE_RATIO_NUM) / BASELINE_RATIO_DEN;
	if (ascent > row_height)
		ascent = row_height;
	return row_top_y + ascent;
}

static int ColumnContentX(ListView* lv, int colIdx) {
	int x = 0;
	for (int c = 0; c < colIdx; c++)
		x += lv->columns[c].width;
	return x;
}

static void ListViewDrawHeader(ListView* lv, ChWindow* win) {
	int header_w = lv->viewport_width;
	ChDrawRect(win->canv, lv->base.x, lv->base.y, header_w, lv->headerHeight, LV_COLOR_HEADER_BG);

	ChRect hclip;
	hclip.x = lv->base.x;
	hclip.y = lv->base.y;
	hclip.w = header_w;
	hclip.h = lv->headerHeight;

	int cx = lv->base.x - lv->scroll_x;
	for (int c = 0; c < lv->columnCount; c++) {
		int col_w = lv->columns[c].width;
		if (cx + col_w > lv->base.x && cx < lv->base.x + header_w) {
			int pen_x = cx + 4;
			int pen_y = _RowBaselineY(lv, lv->base.y, lv->headerHeight);
			ChFontSetSize(win->app->baseFont, 14);
			ChFontDrawTextClipped(win->canv,
								  win->app->baseFont,
								  lv->columns[c].header,
								  pen_x,
								  pen_y,
								  LV_COLOR_HEADER_TEXT,
								  &hclip);
		}

		cx += col_w;

		if (cx > lv->base.x && cx < lv->base.x + header_w)
			ChDrawVerticalLine(win->canv, cx, lv->base.y, lv->headerHeight, LV_COLOR_GRID_LINE);
	}
}

void _neon_fill_rect32(ChCanvas* canv, int stride_px, int x, int y, int w, int h, uint32_t color) {
	if (w <= 0 || h <= 0)
		return;
	uint32_t* fb = canv->buffer;
	uint32x4_t vcolor = vdupq_n_u32(color);

	for (int row = 0; row < h; row++) {
		uint32_t* dst = fb + (size_t)(y + row) * stride_px + x;
		int col = 0;
		for (; col + 16 <= w; col += 16) {
			vst1q_u32(dst + col, vcolor);
			vst1q_u32(dst + col + 4, vcolor);
			vst1q_u32(dst + col + 8, vcolor);
			vst1q_u32(dst + col + 12, vcolor);
		}
		for (; col + 4 <= w; col += 4)
			vst1q_u32(dst + col, vcolor);
		for (; col < w; col++)
			dst[col] = color;
	}
}

static inline void _neon_cpy_row32(uint32_t* dst, const uint32_t* src, int w) {
	int col = 0;
	for (; col + 16 <= w; col += 16) {
		uint32x4_t a = vld1q_u32(src + col);
		uint32x4_t b = vld1q_u32(src + col + 4);
		uint32x4_t c = vld1q_u32(src + col + 8);
		uint32x4_t d = vld1q_u32(src + col + 12);
		vst1q_u32(dst + col, a);
		vst1q_u32(dst + col + 4, b);
		vst1q_u32(dst + col + 8, c);
		vst1q_u32(dst + col + 12, d);
	}

	for (; col + 4 <= w; col += 4)
		vst1q_u32(dst + col, vld1q_u32(src + col));
	for (; col < w; col++)
		dst[col] = src[col];
}

void _neon_shift_rect_vertical(
	uint32_t* fb, int stride_px, int x, int y, int w, int h, int delta_y) {
	if (delta_y == 0 || w <= 0 | h <= 0)
		return;
	if (delta_y >= h || -delta_y >= h)
		return;

	if (delta_y > 0) {
		for (int row = h - 1; row >= delta_y; row--) {
			uint32_t* dst = fb + (size_t)(y + row) * stride_px + x;
			uint32_t* src = fb + (size_t)(y + row - delta_y) * stride_px + x;
			_neon_cpy_row32(dst, src, w);
		}
	} else {
		int up = -delta_y;
		for (int row = 0; row < h - up; row++) {
			uint32_t* dst = fb + (size_t)(y + row) * stride_px + x;
			uint32_t* src = fb + (size_t)(y + row + up) * stride_px + x;
			_neon_cpy_row32(dst, src, w);
		}
	}
}

static uint32_t g_scanline_tmp[MAX_SCANLINE_PX];
static void
_neon_shift_rect_horiz(uint32_t* fb, int stride_px, int x, int y, int w, int h, int delta_x) {
	if (delta_x == 0 || w <= 0 || h <= 0)
		return;
	if (delta_x >= w || -delta_x >= w)
		return;
	if (w > MAX_SCANLINE_PX)
		return;

	int abs_delta = delta_x > 0 ? delta_x : -delta_x;
	int copy_w = w - abs_delta;

	for (int row = 0; row < h; row++) {
		uint32_t* line = fb + (size_t)(y + row) * stride_px + x;

		if (delta_x > 0) {
			_neon_cpy_row32(g_scanline_tmp, line, copy_w);
			_neon_cpy_row32(line + delta_x, g_scanline_tmp, copy_w);
		} else {
			_neon_cpy_row32(g_scanline_tmp, line + abs_delta, copy_w);
			_neon_cpy_row32(line, g_scanline_tmp, copy_w);
		}
	}
}

static void _draw_row_range(ListView* lv, ChWindow* win, int clip_y, int clip_h) {
	int rows_y = lv->base.y + lv->headerHeight;
	int rows_w = lv->viewport_width;

	if (clip_y < rows_y) {
		clip_h -= (rows_y - clip_y);
		clip_y = rows_y;
	}
	if (clip_y + clip_h > rows_y + lv->viewport_height)
		clip_h = (rows_y + lv->viewport_height) - clip_y;
	if (clip_h <= 0)
		return;

	//shall i convert it to neon ? out of algorithms, feed me
	_neon_fill_rect32(
		win->canv, win->canv->canvasWidth, lv->base.x, clip_y, rows_w, clip_h, LV_COLOR_BG);

	int first_index = lv->scroll_y / lv->itemHeight;
	int first_row_offset = lv->scroll_y % lv->itemHeight;
	int ry = rows_y - first_row_offset;

	ChRect rows_clip;
	rows_clip.x = lv->base.x;
	rows_clip.y = rows_y;
	rows_clip.w = rows_w;
	rows_clip.h = lv->viewport_height;

	for (int i = first_index; i < lv->itemCount && ry < rows_y + lv->viewport_height; i++) {
		if (ry + lv->itemHeight <= clip_y || ry >= clip_y + clip_h) {
			ry += lv->itemHeight;
			continue;
		}

		uint32_t row_bg = LV_COLOR_BG;
		uint32_t row_text = LV_COLOR_TEXT;
		if (i == lv->selectedIndex) {
			row_bg = LV_SELECTED_BG;
			row_text = LV_SELECTED_TEXT;
		} else if (i == lv->hoverIndex) {
			row_bg = LV_COLOR_HOVER_BG;
		}

		int draw_y = ry, draw_h = lv->itemHeight;
		if (draw_y < clip_y) {
			int d = clip_y - draw_y;
			draw_y += d;
			draw_h -= d;
		}
		if (draw_y + draw_h > clip_y + clip_h)
			draw_h = (clip_y + clip_h) - draw_y;

		if (draw_h > 0 && row_bg != LV_COLOR_BG)
			_neon_fill_rect32(
				win->canv, win->canv->canvasWidth, lv->base.x, draw_y, rows_w, draw_h, row_bg);

		int cx = lv->base.x - lv->scroll_x;
		int pen_y = _RowBaselineY(lv, ry, lv->itemHeight);

		for (int c = 0; c < lv->columnCount; c++) {
			int col_w = lv->columns[c].width;

			if (cx + col_w > lv->base.x && cx < lv->base.x + rows_w) {
				int text_x = cx + 4;

				/** Draw the icon here **/
				if (c == 0) {
					ListItem* item = &lv->item[i];
					if (item->icon != NULL) {
						int icon_x = cx + lv->icon_padding;
						int icon_y = ry + (lv->itemHeight - item->iconH) / 2;
						if (icon_y < ry)
							icon_y = ry;

						ChDrawIconClipped(win->canv, item->icon, icon_x, icon_y, &rows_clip);
					}

					text_x = cx + lv->icon_column_width;
				}

				/* draw the text here */
				ChFontSetSize(win->app->baseFont, 14);
				ChFontDrawTextClipped(win->canv,
									  win->app->baseFont,
									  lv->item[i].cells[c],
									  text_x,
									  pen_y,
									  row_text,
									  &rows_clip);
			}
			cx += col_w;
			if (cx > lv->base.x && cx < lv->base.x + rows_w)
				ChDrawVerticalLine(win->canv, cx, draw_y, lv->itemHeight, LV_COLOR_GRID_LINE);
		}
		ry += lv->itemHeight;
	}
}

static void _draw_column_strip(ListView* lv, ChWindow* win, int strip_x_local, int strip_w) {
	int rows_y = lv->base.y + lv->headerHeight;
	int rows_w = lv->viewport_width;

	if (strip_x_local < 0) {
		strip_w += strip_x_local;
		strip_x_local = 0;
	}
	if (strip_x_local + strip_w > rows_w)
		strip_w = rows_w - strip_x_local;
	if (strip_w <= 0)
		return;

	int strip_x = lv->base.x + strip_x_local;

	ChRect strip_clip;
	strip_clip.x = strip_x;
	strip_clip.y = rows_y;
	strip_clip.w = strip_w;
	strip_clip.h = lv->viewport_height;

	int first_index = lv->scroll_y / lv->itemHeight;
	int first_row_offset = lv->scroll_y % lv->itemHeight;
	int ry = rows_y - first_row_offset;

	for (int i = first_index; i < lv->itemCount && ry < rows_y + lv->viewport_height; i++) {
		uint32_t row_bg = LV_COLOR_BG;
		uint32_t row_text = LV_COLOR_TEXT;
		if (i == lv->selectedIndex) {
			row_bg = LV_SELECTED_BG;
			row_text = LV_SELECTED_TEXT;
		} else if (i == lv->hoverIndex)
			row_bg = LV_COLOR_HOVER_BG;

		int draw_y = ry, draw_h = lv->itemHeight;
		if (draw_y < rows_y) {
			int diff = rows_y - draw_y;
			draw_y += diff;
			draw_h -= diff;
		}
		if (draw_y + draw_h > rows_y + lv->viewport_height)
			draw_h = (rows_y + lv->viewport_height) - draw_y;

		if (draw_h > 0)
			_neon_fill_rect32(
				win->canv, win->canv->canvasWidth, strip_x, draw_y, strip_w, draw_h, row_bg);

		int cx = lv->base.x - lv->scroll_x;
		int pen_y = _RowBaselineY(lv, ry, lv->itemHeight);

		for (int c = 0; c < lv->columnCount; c++) {
			int col_w = lv->columns[c].width;
			int col_left = cx;
			int col_right = cx + col_w;

			if (col_right > strip_x && col_left < strip_x + strip_w) {
				int text_x = cx + 4;

				if (c == 0) {
					ListItem* item = &lv->item[i];
					if (item->icon != NULL) {
						int icon_x = cx + lv->icon_padding;
						int icon_y = ry + (lv->itemHeight - item->iconH) / 2;
						if (icon_y < ry)
							icon_y = ry;
						ChDrawIconClipped(win->canv, item->icon, icon_x, icon_y, &strip_clip);
					}
					text_x = cx + lv->icon_column_width;
				}
				ChFontSetSize(win->app->baseFont, 14);
				ChFontDrawTextClipped(win->canv,
									  win->app->baseFont,
									  lv->item[i].cells[c],
									  text_x,
									  pen_y,
									  row_text,
									  &strip_clip);
			}
			cx += col_w;
		}

		ry += lv->itemHeight;
	}
}

static void _lv_draw_single_row_(ListView* lv, ChWindow* win, int index, ChRect* out_rect) {
	if (out_rect) {
		out_rect->x = 0;
		out_rect->y = 0;
		out_rect->w = 0;
		out_rect->h = 0;
	}
	if (index < 0 || index >= lv->itemCount)
		return;

	int rows_y = lv->base.y + lv->headerHeight;
	int rows_w = lv->viewport_width;

	int row_top = rows_y - (lv->scroll_y % lv->itemHeight) +
				  (index - lv->scroll_y / lv->itemHeight) * lv->itemHeight;

	if (row_top + lv->itemHeight <= rows_y || row_top >= rows_y + lv->viewport_height)
		return;

	int draw_y = row_top, draw_h = lv->itemHeight;
	if (draw_y < rows_y) {
		int diff = rows_y - draw_y;
		draw_y += diff;
		draw_h -= diff;
	}
	if (draw_y + draw_h > rows_y + lv->viewport_height)
		draw_h = (rows_y + lv->viewport_height) - draw_y;
	if (draw_h <= 0)
		return;

	uint32_t row_bg = LV_COLOR_BG;
	uint32_t row_text = LV_COLOR_TEXT;
	if (index == lv->selectedIndex)
		row_bg = LV_SELECTED_BG;
	else if (index == lv->hoverIndex)
		row_bg = LV_COLOR_HOVER_BG;

	_neon_fill_rect32(
		win->canv, win->canv->canvasWidth, lv->base.x, draw_y, rows_w, draw_h, row_bg);

	ChRect rows_clip;
	rows_clip.x = lv->base.x;
	rows_clip.y = rows_y;
	rows_clip.w = rows_w;
	rows_clip.h = lv->viewport_height;

	int cx = lv->base.x - lv->scroll_x;
	int pen_y = _RowBaselineY(lv, row_top, lv->itemHeight);

	for (int c = 0; c < lv->columnCount; c++) {
		int col_w = lv->columns[c].width;

		if (cx + col_w > lv->base.x && cx < lv->base.x + rows_w) {
			int text_x = cx + 4;

			if (c == 0) {
				ListItem* item = &lv->item[index];
				if (item->icon != NULL) {
					int icon_x = cx + lv->icon_padding;
					int icon_y = row_top + (lv->itemHeight - item->iconH) / 2;
					if (icon_y < row_top)
						icon_y = row_top;
					ChDrawIconClipped(win->canv, item->icon, icon_x, icon_y, &rows_clip);
				}
				text_x = cx + lv->icon_column_width;
			}
			ChFontSetSize(win->app->baseFont, 14);
			ChFontDrawTextClipped(win->canv,
								  win->app->baseFont,
								  lv->item[index].cells[c],
								  text_x,
								  pen_y,
								  row_text,
								  &rows_clip);
		}

		cx += col_w;
		if (cx > lv->base.x && cx < lv->base.x + rows_w)
			ChDrawVerticalLine(win->canv, cx, draw_y, lv->itemHeight, LV_COLOR_GRID_LINE);
	}

	if (out_rect) {
		out_rect->x = lv->base.x;
		out_rect->y = draw_y;
		out_rect->w = rows_w;
		out_rect->h = draw_h;
	}
}

static int _find_column_start_before(ListView* lv, int local_x) {
	int content_x_target = local_x + lv->scroll_x;
	int cx = 0;
	for (int c = 0; c < lv->columnCount; c++) {
		int next = cx + lv->columns[c].width;
		if (content_x_target < next) {
			int col_start_local = cx - lv->scroll_x;
			return col_start_local < 0 ? 0 : col_start_local;
		}
		cx = next;
	}
	return local_x;
}

static int find_column_end_after(ListView* lv, int local_x) {
	int content_x_target = local_x + lv->scroll_x;
	int cx = 0;
	for (int c = 0; c < lv->columnCount; c++) {
		int next = cx + lv->columns[c].width;
		if (content_x_target <= next) {
			int col_end_local = next - lv->scroll_x;
			return col_end_local;
		}
		cx = next;
	}
	return local_x;
}
void ListViewDraw(ChWidget* wid, ChWindow* win) {
	ListView* lv = (ListView*)wid;

	int rows_y = lv->base.y + lv->headerHeight;
	int rows_h = lv->viewport_height;
	int rows_w = lv->viewport_width;

	int delta_y = lv->scroll_y - lv->prev_scroll_y;
	int delta_x = lv->scroll_x - lv->prev_scroll_x;

	int full_redraw = (lv->prev_scroll_y < 0) || (lv->force_full_redraw) ||
					  (abs(delta_y) >= rows_h) || (abs(delta_x) >= rows_w) ||
					  (rows_w > MAX_SCANLINE_PX);

	ListViewDrawHeader(lv, win);

	if (full_redraw) {
		_draw_row_range(lv, win, rows_y, rows_h);
	} else {
		if (delta_y != 0) {
			_neon_shift_rect_vertical(win->canv->buffer,
									  win->canv->canvasWidth,
									  lv->base.x,
									  rows_y,
									  rows_w,
									  rows_h,
									  -delta_y);
			if (delta_y > 0)
				_draw_row_range(lv, win, rows_y + rows_h - delta_y, delta_y);
			else
				_draw_row_range(lv, win, rows_y, -delta_y);
		}

		if (delta_x != 0) {
			_neon_shift_rect_horiz(win->canv->buffer,
								   win->canv->canvasWidth,
								   lv->base.x,
								   rows_y,
								   rows_w,
								   rows_h,
								   -delta_x);

			if (delta_x > 0) {
				int dirty_start = rows_w - delta_x;
				int col_start = _find_column_start_before(lv, dirty_start);
				_draw_column_strip(lv, win, col_start, rows_w - col_start);
			} else {
				int dirty_end = -delta_x;
				int col_end = find_column_end_after(lv, dirty_end);
				_draw_column_strip(lv, win, 0, col_end);
			}
		}
	}

	lv->prev_scroll_y = lv->scroll_y;
	lv->prev_scroll_x = lv->scroll_x;
	lv->force_full_redraw = false;

	if (lv->vscroll.base.ChPaintHandler)
		lv->vscroll.base.ChPaintHandler((ChWidget*)&lv->vscroll, win);

	if (lv->hscroll.base.ChPaintHandler)
		lv->hscroll.base.ChPaintHandler((ChWidget*)&lv->hscroll, win);
}

static int lv_row_at(ListView* lv, ChWindow* win, int mx, int my) {
	int lx = mx - win->info->x;
	int ly = my - win->info->y;

	int rows_y = lv->base.y + lv->headerHeight;

	if (lx < lv->base.x || lx >= lv->base.x + lv->viewport_width || ly < rows_y ||
		ly >= rows_y + lv->viewport_height)
		return -1;

	int content_y = (ly - rows_y) + lv->scroll_y;
	int index = content_y / lv->itemHeight;

	if (index < 0 || index >= lv->itemCount)
		return -1;
	return index;
}

static int lv_column_at(ListView* lv, ChWindow* win, int mx, int* out_on_handle) {
	int lx = mx - win->info->x;
	int content_x = (lx - lv->base.x) + lv->scroll_x;

	int cx = 0;
	for (int c = 0; c < lv->columnCount; c++) {
		int next = cx + lv->columns[c].width;
		if (out_on_handle)
			*out_on_handle = (content_x >= next - COLUMN_RESIZE_HANDLE_PX &&
							  content_x <= next + COLUMN_RESIZE_HANDLE_PX);

		if (content_x >= cx && content_x < next)
			return c;

		cx = next;
	}
	return -1;
}

static int lv_is_over_vscrollbar(ListView* lv, ChWindow* win, int mx, int my) {
	int lx = mx - win->info->x, ly = my - win->info->y;
	return (lx >= lv->vscroll.base.x && lx < lv->vscroll.base.x + lv->vscroll.base.w &&
			ly >= lv->vscroll.base.y && ly < lv->vscroll.base.y + lv->vscroll.base.h);
}

static int lv_is_over_hscrollbar(ListView* lv, ChWindow* win, int mx, int my) {
	int lx = mx - win->info->x, ly = my - win->info->y;
	return (lx >= lv->hscroll.base.x && lx < lv->hscroll.base.x + lv->hscroll.base.w &&
			ly >= lv->hscroll.base.y && ly < lv->hscroll.base.y + lv->hscroll.base.h);
}

static int lv_is_over_header(ListView* lv, ChWindow* win, int mx, int my) {
	int ly = my - win->info->y;
	return (ly >= lv->base.y && ly < lv->base.y + lv->headerHeight);
}

void ListViewMouseEvent(ChWidget* wid, ChWindow* win, int mx, int my, int button) {
	ListView* lv = (ListView*)wid;

	if (button == DEODHAI_MOUSE_MSG_SCROLL_UP || button == DEODHAI_MOUSE_MSG_SCROLL_DOWN) {
		int max_scroll = lv->content_height - lv->viewport_height;
		if (max_scroll < 0)
			max_scroll = 0;
		int step = lv->itemHeight * 3;
		if (step < 24)
			step = 24;

		int old_scroll = lv->scroll_y;
		if (button == DEODHAI_MOUSE_MSG_SCROLL_UP) {
			lv->scroll_y -= step;
			if (lv->scroll_y < 0)
				lv->scroll_y = 0;
		} else {
			lv->scroll_y += step;
			if (lv->scroll_y > max_scroll)
				lv->scroll_y = max_scroll;
		}

		if (lv->scroll_y != old_scroll) {
			ChScrollBarSetValue(&lv->vscroll, lv->scroll_y);
			lv->force_full_redraw = true;
			ListViewDraw(wid, win);
			ChWindowUpdate(win, lv->base.x, lv->base.y, lv->base.w, lv->base.h, 0, 1);
		}
		return;
	}

	uint8_t was_down = lv->prev_button;
	uint8_t is_down = (button != 0);

	if (lv->resizingColumn != -1) {
		if (is_down) {
			int lx = mx - win->info->x;
			int delta = lx - lv->resizeStartMouseX;
			int new_w = lv->resizeStartWidth + delta;
			ListColumn* col = &lv->columns[lv->resizingColumn];
			if (new_w < col->minWidth)
				new_w = col->minWidth;

			if (new_w != col->width) {
				col->width = new_w;
				lv_recalc_content_metric(lv);
				lv->force_full_redraw = 1;
				ListViewDraw(wid, win);
				ChWindowUpdate(win, lv->base.x, lv->base.y, lv->base.w, lv->base.h, 0, 1);
			}
		} else
			lv->resizingColumn = -1;
		lv->prev_button = is_down;
		return;
	}

	if (lv_is_over_vscrollbar(lv, win, mx, my) || lv->vscroll.state == SB_STATE_DRAGGING) {
		int old_val = lv->vscroll.currentValue;
		uint8_t old_state = lv->vscroll.state;
		if (lv->vscroll.base.ChMouseEvent)
			lv->vscroll.base.ChMouseEvent((ChWidget*)&lv->vscroll, win, mx, my, button);
		if (lv->vscroll.currentValue != old_val || lv->vscroll.state != old_state || is_down != was_down) {
			lv->scroll_y = lv->vscroll.currentValue;
			lv->force_full_redraw = true;
			ListViewDraw(wid, win);
			ChWindowUpdate(win, lv->base.x, lv->base.y, lv->base.w, lv->base.h, 0, 1);
		}
		lv->prev_button = is_down;
		return;
	}

	if (lv_is_over_hscrollbar(lv, win, mx, my) || lv->hscroll.state == SB_STATE_DRAGGING) {
		int old_val = lv->hscroll.currentValue;
		uint8_t old_state = lv->hscroll.state;
		if (lv->hscroll.base.ChMouseEvent)
			lv->hscroll.base.ChMouseEvent((ChWidget*)&lv->hscroll, win, mx, my, button);
		if (lv->hscroll.currentValue != old_val || lv->hscroll.state != old_state || is_down != was_down) {
			lv->scroll_x = lv->hscroll.currentValue;
			lv->force_full_redraw = true;
			ListViewDraw(wid, win);
			ChWindowUpdate(win, lv->base.x, lv->base.y, lv->base.w, lv->base.h, 0, 1);
		}
		lv->prev_button = is_down;
		return;
	}

	if (lv_is_over_header(lv, win, mx, my)) {
		int on_handle = 0;
		int col = lv_column_at(lv, win, mx, &on_handle);

		if (!was_down && is_down && on_handle && col != -1) {
			lv->resizingColumn = col;
			lv->resizeStartMouseX = mx - win->info->x;
			lv->resizeStartWidth = lv->columns[col].width;
			lv->prev_button = is_down;
			return;
		}
		lv->prev_button = is_down;
		return;
	}

	int row = lv_row_at(lv, win, mx, my);

	if (!was_down && is_down) {
		size_t now = _KeGetSystemTimerTick();
		bool is_dbl_click = (button == DEODHAI_MESSAGE_MOUSE_DBLCLK) ||
							(row != -1 && row == lv->last_click_row && (now - lv->last_click_tick) < 500);
		lv->last_click_tick = now;
		lv->last_click_row = row;

		if (row != -1 && row != lv->selectedIndex) {
			int old_selected = lv->selectedIndex;
			lv->selectedIndex = row;
			lv->force_full_redraw = true;
			ChRect dirty;
			if (old_selected != -1) {
				memset(&dirty, 0, sizeof(dirty));
				_lv_draw_single_row_(lv, win, old_selected, &dirty);
				if (dirty.w > 0 && dirty.h > 0)
					ChWindowUpdate(win, dirty.x, dirty.y, dirty.w, dirty.h, 0, 1);
			}
			memset(&dirty, 0, sizeof(dirty));
			_lv_draw_single_row_(lv, win, row, &dirty);
			if (dirty.w > 0 && dirty.h > 0)
				ChWindowUpdate(win, dirty.x, dirty.y, dirty.w, dirty.h, 0, 1);
		}

		if (is_dbl_click && row != -1) {
			if (lv->onItemAction)
				lv->onItemAction(lv, row, &lv->item[row]);
			lv->prev_button = is_down;
			return;
		}

		lv->prev_button = is_down;
		return;
	}

	if (!was_down && !is_down) {
		if (row != lv->hoverIndex) {
			int old_hover = lv->hoverIndex;
			lv->hoverIndex = row;
			ChRect dirty;
			if (old_hover != -1) {
				memset(&dirty, 0, sizeof(dirty));
				_lv_draw_single_row_(lv, win, old_hover, &dirty);
				if (dirty.w > 0 && dirty.h > 0)
					ChWindowUpdate(win, dirty.x, dirty.y, dirty.w, dirty.h, 0, 1);
			}

			if (row != -1) {
				memset(&dirty, 0, sizeof(dirty));
				_lv_draw_single_row_(lv, win, row, &dirty);
				if (dirty.w > 0 && dirty.h > 0)
					ChWindowUpdate(win, dirty.x, dirty.y, dirty.w, dirty.h, 0, 1);
			}
		}
	}

	lv->prev_button = is_down;
}