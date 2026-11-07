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

#include "toolbar.h"

extern void
_neon_fill_rect32(ChCanvas* canv, int stride_px, int x, int y, int w, int h, uint32_t color);
extern void _neon_cpy_row32(uint32_t* dst, const uint32_t* src, int w);
extern void
_neon_shift_rect_vertical(uint32_t* fb, int stride_px, int x, int y, int w, int h, int delta_y);
extern void ChToolbarMouseEvent(ChWidget* wid, ChWindow* win, int mx, int my, int button);
extern void _toolbar_draw(ChWidget* tb, ChWindow* win);

static void
_draw_overflow_arrow(ChCanvas* canv, int cx, int cy, int sz, uint8_t orientation, uint32_t color) {
	if (orientation == TOOLBAR_HORIZONTAL) {
		for (int row = 0; row < sz; row++) {
			int half = row;
			for (int dy = -half; dy <= half; dy++)
				ChDrawPixel(canv, cx - sz / 2 + row, cy + dy, color);
		}
	} else {
		for (int col = 0; col < sz; col++) {
			int half = col;
			for (int dx = -half; dx <= half; dx++)
				ChDrawPixel(canv, cx + dx, cy - sz / 2 + col, color);
		}
	}
}

static void _recalc_toolbar_layout(ChToolbar* tb) {
	int offset = 0;
	for (int i = 0; i < tb->item_count; i++) {
		ChToolBarItem* it = &tb->items[i];
		it->offset = offset;

		switch (it->type) {
		case TOOLBAR_ITEM_SEPARATOR:
			it->extent = TOOLBAR_SEP_THICKNESS + TOOLBAR_SEP_MARGIN * 2;
			break;
		case TOOLBAR_ITEM_WIDGET:
			it->extent = (tb->orientation == TOOLBAR_HORIZONTAL) ? it->child_w : it->child_h;
			break;
		default:
			it->extent = ((tb->orientation == TOOLBAR_HORIZONTAL) ? it->iconW : it->iconH) +
						 tb->buttonPadding * 2;
			break;
		}

		offset += it->extent;
	}

	tb->contentLength = offset;

	int widgetLen = (tb->orientation == TOOLBAR_HORIZONTAL) ? tb->base.w : tb->base.h;

	tb->show_overflow = (tb->contentLength > widgetLen);
	tb->viewportLength = tb->show_overflow ? widgetLen - TOOLBAR_OVERFLOW_BTN_SZ : widgetLen;
	tb->overflow_btn_pos = tb->viewportLength;

	int max_scroll = tb->contentLength - tb->viewportLength;
	if (max_scroll < 0)
		max_scroll = 0;
	if (tb->scrollOffset > max_scroll)
		tb->scrollOffset = max_scroll;

	for (int i = 0; i < tb->item_count; i++) {
		ChToolBarItem* it = &tb->items[i];
		if (it->type != TOOLBAR_ITEM_WIDGET || it->child == NULL)
			continue;

		int screen_offset = it->offset - tb->scrollOffset;
		if (tb->orientation == TOOLBAR_HORIZONTAL) {
			it->child->x = tb->base.x + screen_offset;
			it->child->y = tb->base.y;
		} else {
			it->child->x = tb->base.x;
			it->child->y = tb->base.y + screen_offset;
		}
	}
}

ChToolbar* ChToolbarCreate(int x, int y, int w, int h, uint8_t orientation) {
	ChToolbar* tb = (ChToolbar*)malloc(sizeof(ChToolbar));
	memset(tb, 0, sizeof(ChToolbar));

	tb->base.x = x;
	tb->base.y = y;
	tb->base.w = w;
	tb->base.h = h;
	tb->base.ChMouseEvent = ChToolbarMouseEvent;
	tb->base.ChPaintHandler = _toolbar_draw;

	tb->orientation = orientation;
	tb->buttonPadding = TOOLBAR_BTN_PADDING;
	tb->item_count = 0;
	tb->hoverIndex = -1;
	tb->pressedIndex = -1;

	tb->color_bg = 0xFF2D2D2D;
	tb->color_btn_hover = 0xFF3D3D3D;
	tb->color_btn_pressed = 0xFF1A1A1A;
	tb->color_btn_active = 0xFF3A6EA5;
	tb->color_btn_disabled = 0xFF444444;
	tb->color_separator = 0xFF555555;
	tb->color_overflow_arrow = 0xFFCCCCCC;

	_recalc_toolbar_layout(tb);
	return tb;
}

static int _PushItem(ChToolbar* tb) {
	if (tb->item_count >= TOOLBAR_MAX_ITEMS)
		return -1;
	int index = tb->item_count++;
	memset(&tb->items[index], 0, sizeof(ChToolBarItem));
	tb->items[index].state = TOOLBAR_STATE_NORMAL;
	return index;
}

int ChToolbarAddButton(ChToolbar* tb, ChIcon* icon, int iw, int ih, const char* tooltip, void* ud) {
	int i = _PushItem(tb);
	if (i < 0)
		return -1;
	ChToolBarItem* it = &tb->items[i];
	it->type = TOOLBAR_ITEM_BUTTON;
	it->icon = icon;
	it->iconW = iw;
	it->iconH = ih;
	it->userData = ud;
	if (tooltip)
		strncpy(it->tooltip, tooltip, sizeof(it->tooltip) - 1);
	_recalc_toolbar_layout(tb);
	return i;
}

int ChToolbarAddToggle(ChToolbar* tb, ChIcon* icon, int iw, int ih, const char* tooltip, void* ud) {
	int i = ChToolbarAddButton(tb, icon, iw, ih, tooltip, ud);
	if (i >= 0)
		tb->items[i].type = TOOLBAR_ITEM_TOGGLE;
	return i;
}

int ChToolbarAddRadio(
	ChToolbar* tb, ChIcon* icon, int iw, int ih, const char* tooltip, uint8_t groupID, void* ud) {
	int i = ChToolbarAddButton(tb, icon, iw, ih, tooltip, ud);
	if (i >= 0) {
		tb->items[i].type = TOOLBAR_ITEM_RADIO;
		tb->items[i].groupID = groupID;
	}
	return i;
}

int ChToolbarAddSeparator(ChToolbar* tb) {
	int i = _PushItem(tb);
	if (i < 0)
		return -1;
	tb->items[i].type = TOOLBAR_ITEM_SEPARATOR;
	_recalc_toolbar_layout(tb);
	return i;
}

int ChToolbarAddWidget(ChToolbar* tb, ChWidget* child, int childW, int childH) {
	int i = _PushItem(tb);
	if (i < 0)
		return -1;
	ChToolBarItem* it = &tb->items[i];
	it->type = TOOLBAR_ITEM_WIDGET;
	it->child = child;
	it->child_w = childW;
	it->child_h = childH;
	_recalc_toolbar_layout(tb);
	return i;
}

void ChToolbarSetEnabled(ChToolbar* tb, int index, int enabled) {
	if (index < 0 || index >= tb->item_count)
		return;
	tb->items[index].state = enabled ? TOOLBAR_STATE_NORMAL : TOOLBAR_STATE_DISABLED;
}

void ChToolbarSetActive(ChToolbar* tb, int index, int active) {
	if (index < 0 || index >= tb->item_count)
		return;
	ChToolBarItem* it = &tb->items[index];

	if (it->type == TOOLBAR_ITEM_RADIO && active) {
		for (int i = 0; i < tb->item_count; i++) {
			if (i != index && tb->items[i].type == TOOLBAR_ITEM_RADIO &&
				tb->items[i].groupID == it->groupID) {
				tb->items[i].active = 0;
				tb->items[i].state = TOOLBAR_STATE_NORMAL;
			}
		}
	}

	it->active = active ? 1 : 0;
	it->state = active ? TOOLBAR_STATE_ACTIVE : TOOLBAR_STATE_NORMAL;
}

static void _draw_toolbar_item(ChToolbar* tb, ChWindow* win, int index, ChRect* clip) {
	ChToolBarItem* it = &tb->items[index];

	int screen_offset = it->offset - tb->scrollOffset;

	if (screen_offset + it->extent <= 0 || screen_offset >= tb->viewportLength)
		return;

	int bx, by, bw, bh;

	if (tb->orientation == TOOLBAR_HORIZONTAL) {
		bx = tb->base.x + screen_offset;
		by = tb->base.y;
		bw = it->extent;
		bh = tb->base.h;
	} else {
		bx = tb->base.x;
		by = tb->base.y + screen_offset;
		bw = tb->base.w;
		bh = it->extent;
	}

	if (it->type == TOOLBAR_ITEM_SEPARATOR) {
		if (tb->orientation == TOOLBAR_HORIZONTAL) {
			int sx = bx + TOOLBAR_SEP_MARGIN;
			ChDrawVerticalLine(win->canv, sx, by + 2, bh - 4, tb->color_separator);
		} else {
			int sy = by + TOOLBAR_SEP_MARGIN;
			ChDrawHorizontalLine(win->canv, bx + 2, sy, bw - 4, tb->color_separator);
		}
		return;
	}

	if (it->type == TOOLBAR_ITEM_WIDGET)
		return;

	uint32_t bg = tb->color_bg;
	switch (it->state) {
	case TOOLBAR_STATE_HOVER:
		bg = tb->color_btn_hover;
		break;
	case TOOLBAR_STATE_PRESSED:
		bg = tb->color_btn_pressed;
		break;
	case TOOLBAR_STATE_ACTIVE:
		bg = tb->color_btn_active;
		break;
	case TOOLBAR_STATE_DISABLED:
		bg = tb->color_btn_disabled;
		break;
	default:
		break;
	}

	_neon_fill_rect32(win->canv, win->canv->canvasWidth, bx, by, bw, bh, bg);

	if (it->icon && it->state != TOOLBAR_STATE_DISABLED) {
		int ix = bx + (bw - it->iconW) / 2;
		int iy = by + (bh - it->iconH) / 2;
		ChDrawIconClipped(win->canv, it->icon, ix, iy, clip);
	} else if (it->icon) {
		// have some tint effect here
		int ix = bx + (bw - it->iconW) / 2;
		int iy = by + (bh - it->iconH) / 2;
		ChDrawIconClipped(win->canv, it->icon, ix, iy, clip);
	}
}

void _toolbar_draw(ChWidget* wid, ChWindow* win) {
	ChToolbar* tb = (ChToolbar*)wid;

	_neon_fill_rect32(win->canv,
					  win->canv->canvasWidth,
					  tb->base.x,
					  tb->base.y,
					  tb->base.w,
					  tb->base.h,
					  tb->color_bg);

	ChRect clip;
	clip.x = tb->base.x;
	clip.y = tb->base.y;
	clip.w = tb->base.w;
	clip.h = tb->base.h;

	if (tb->orientation == TOOLBAR_HORIZONTAL)
		clip.w = tb->viewportLength;
	else
		clip.h = tb->viewportLength;

	for (int i = 0; i < tb->item_count; i++)
		_draw_toolbar_item(tb, win, i, &clip);

	if (tb->show_overflow) {
		int cx, cy;
		if (tb->orientation == TOOLBAR_HORIZONTAL) {
			cx = tb->base.x + tb->overflow_btn_pos + TOOLBAR_OVERFLOW_BTN_SZ / 2;
			cy = tb->base.y + tb->base.h / 2;
		} else {
			cx = tb->base.x + tb->base.w / 2;
			cy = tb->base.y + tb->overflow_btn_pos + TOOLBAR_OVERFLOW_BTN_SZ / 2;
		}
		_draw_overflow_arrow(win->canv, cx, cy, 5, tb->orientation, tb->color_overflow_arrow);
	}
}

static void _toolbar_redraw_item(ChToolbar* tb, ChWindow* win, int index) {
	if (index < 0 || index >= tb->item_count)
		return;

	ChToolBarItem* it = &tb->items[index];
	int screen_offset = it->offset - tb->scrollOffset;
	if (screen_offset + it->extent <= 0 || screen_offset >= tb->viewportLength)
		return;

	int bx, by, bw, bh;
	if (tb->orientation == TOOLBAR_HORIZONTAL) {
		bx = tb->base.x + screen_offset;
		by = tb->base.y;
		bw = it->extent;
		bh = tb->base.h;
	} else {
		bx = tb->base.x;
		by = tb->base.y + screen_offset;
		bw = tb->base.w;
		bh = it->extent;
	}

	_neon_fill_rect32(win->canv, win->canv->canvasWidth, bx, by, bw, bh, tb->color_bg);
	ChRect clip;
	clip.x = tb->base.x;
	clip.y = tb->base.y;
	clip.w = (tb->orientation == TOOLBAR_HORIZONTAL) ? tb->viewportLength : tb->base.w;
	clip.h = (tb->orientation == TOOLBAR_HORIZONTAL) ? tb->base.h : tb->viewportLength;

	_draw_toolbar_item(tb, win, index, &clip);
	ChWindowUpdate(win, bx, by, bw, bh, 0, 1);
}

static int _toolbar_hit_test(ChToolbar* tb, ChWindow* win, int mx, int my) {
	int lx = mx - win->info->x;
	int ly = my - win->info->y;

	if (lx < tb->base.x || lx >= tb->base.x + tb->base.w || ly < tb->base.y ||
		ly >= tb->base.y + tb->base.h)
		return -1;

	int pos = (tb->orientation == TOOLBAR_HORIZONTAL) ? (lx - tb->base.x) : (ly - tb->base.y);

	if (tb->show_overflow && pos >= tb->overflow_btn_pos)
		return -2;

	int content_pos = pos + tb->scrollOffset;

	for (int i = 0; i < tb->item_count; i++) {
		ChToolBarItem* it = &tb->items[i];
		if (content_pos >= it->offset && content_pos < it->offset + it->extent)
			return i;
	}

	return -1;
}

static bool is_overflow_hit(int hit) {
	return hit == -2;
}

void ChToolbarMouseEvent(ChWidget* wid, ChWindow* win, int mx, int my, int button) {
	ChToolbar* tb = (ChToolbar*)wid;
	uint8_t was_down = tb->prevButton;
	uint8_t is_down = (button != 0);

	int hit = _toolbar_hit_test(tb, win, mx, my);

	if (was_down && !is_down) {
		if (tb->pressedIndex != -1 && hit == tb->pressedIndex) {
			ChToolBarItem* it = &tb->items[hit];

			if (it->state != TOOLBAR_STATE_DISABLED) {
				if (it->type == TOOLBAR_ITEM_TOGGLE) {
					it->active = !it->active;
					it->state = it->active ? TOOLBAR_STATE_ACTIVE : TOOLBAR_STATE_NORMAL;
				} else if (it->type == TOOLBAR_ITEM_RADIO) {
					ChToolbarSetActive(tb, hit, 1);
				} else {
					it->state = TOOLBAR_STATE_HOVER;
				}

				//if(it->onClick) it->onClick(tb, hit)
				if (tb->onItemStateChange)
					tb->onItemStateChange(tb, hit);
			}
		}

		if (tb->pressedIndex != -1) {
			ChToolBarItem* it = &tb->items[tb->pressedIndex];
			if (it->state == TOOLBAR_STATE_PRESSED)
				it->state = TOOLBAR_STATE_NORMAL;
			_toolbar_redraw_item(tb, win, tb->pressedIndex);
			tb->pressedIndex = -1;
		}

		tb->prevButton = is_down;
		return;
	}

	if (!was_down && is_down) {
		if (is_overflow_hit(hit)) {
			int step = tb->viewportLength / 2;
			int max_scroll = tb->contentLength - tb->viewportLength;
			tb->scrollOffset += step;
			if (tb->scrollOffset > max_scroll)
				tb->scrollOffset = max_scroll;
			_recalc_toolbar_layout(tb);
			_toolbar_draw((ChWidget*)tb, win);
			ChWindowUpdate(win, tb->base.x, tb->base.y, tb->base.w, tb->base.h, 0, 1);
			tb->prevButton = is_down;
			return;
		}

		if (hit >= 0) {
			ChToolBarItem* it = &tb->items[hit];
			if (it->type != TOOLBAR_ITEM_SEPARATOR && it->type != TOOLBAR_ITEM_WIDGET &&
				it->type != TOOLBAR_STATE_DISABLED) {
				tb->pressedIndex = hit;
				it->state = TOOLBAR_STATE_PRESSED;
				_toolbar_redraw_item(tb, win, hit);
			}

			if (it->type == TOOLBAR_ITEM_WIDGET && it->child && it->child->ChMouseEvent)
				it->child->ChMouseEvent(it->child, win, mx, my, button);
		}

		tb->prevButton = is_down;
		return;
	}

	if (was_down && is_down) {
		if (hit >= 0 && tb->items[hit].type == TOOLBAR_ITEM_WIDGET && tb->items[hit].child &&
			tb->items[hit].child->ChMouseEvent)
			tb->items[hit].child->ChMouseEvent(tb->items[hit].child, win, mx, my, button);
		tb->prevButton = is_down;
		return;
	}

	if (hit != tb->hoverIndex) {
		int old_hover = tb->hoverIndex;
		tb->hoverIndex = hit;

		if (old_hover >= 0) {
			ChToolBarItem* it = &tb->items[old_hover];
			if (it->state == TOOLBAR_STATE_HOVER)
				it->state = it->active ? TOOLBAR_STATE_ACTIVE : TOOLBAR_STATE_NORMAL;
			_toolbar_redraw_item(tb, win, old_hover);
		}

		if (hit >= 0) {
			ChToolBarItem* it = &tb->items[hit];
			if (it->state == TOOLBAR_STATE_NORMAL)
				it->state = TOOLBAR_STATE_HOVER;
			_toolbar_redraw_item(tb, win, hit);
		}
	}

	tb->prevButton = is_down;
}