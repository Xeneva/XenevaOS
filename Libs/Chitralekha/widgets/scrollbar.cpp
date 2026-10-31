/**
* @file scrollbar.cpp
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

#include "scrollbar.h"
#include <stdlib.h>
#include <math.h>

#define ARROW_SIZE				   10
#define MIN_THUMB_SIZE			   20
#define SCROLLBAR_CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
#define SCROLLBAR_MAX(a, b)		   ((a) > (b) ? (a) : (b))

enum {
	SB_HIT_NONE,
	SB_HIT_ARROW_DEC,
	SB_HIT_TRACK_DEC,
	SB_HIT_THUMB,
	SB_HIT_TRACK_INC,
	SB_HIT_ARROW_INC,
};

/* default color theme */
#define COLOR_TRACK		  0xFF2D2D2D
#define COLOR_THUMB		  0xFF666666
#define COLOR_THUMB_HOVER 0xFF888888
#define COLOR_THUMB_DRAG  0xFFAAAAAA
#define COLOR_ARROW_BG	  0xFF3A3A3A
#define COLOR_ARROW_FG	  0xFFCCCCCC

void ChScrollBarPaint(ChWidget* wid, ChWindow* win);
void ScrollBarMouseEvent(ChWidget* wid, ChWindow* win, int mx, int my, int button);

static int ScrollBarGetTrackLen(ChScrollBar* sb) {
	if (sb->orientation == SCROLLBAR_ORIENTATION_VERTICAL)
		return sb->base.h - 2 * ARROW_SIZE;
	else
		return sb->base.w - 2 * ARROW_SIZE;
}

/**
 * @brief ScrollBarRecalcThumb -- recalculate the
 * thumb geometry
 * @param sb -- Pointer to ScrollBar
 */
static void ScrollBarRecalcThumb(ChScrollBar* sb) {
	sb->trackLen = ScrollBarGetTrackLen(sb);
	if (sb->trackLen <= 0) {
		sb->thumbSize = 0;
		sb->thumbPos = 0;
		return;
	}

	int totalRange = sb->maxValue - sb->minValue;

	if (totalRange <= 0 || sb->pageSize >= totalRange) {
		sb->thumbSize = sb->trackLen;
		sb->thumbPos = 0;
		return;
	}

	sb->thumbSize = (sb->pageSize * sb->trackLen) / totalRange;
	if (sb->thumbSize < MIN_THUMB_SIZE)
		sb->thumbSize = MIN_THUMB_SIZE;
	if (sb->thumbSize > sb->trackLen)
		sb->thumbSize = sb->trackLen;

	int scrollablePixels = sb->trackLen - sb->thumbSize;
	int scrollableRange = totalRange - sb->pageSize;

	if (scrollableRange <= 0 || scrollablePixels <= 0) {
		sb->thumbPos = 0;
		return;
	}

	sb->thumbPos = ((sb->currentValue - sb->minValue) * scrollablePixels) / scrollableRange;
	sb->thumbPos = SCROLLBAR_CLAMP(sb->thumbPos, 0, scrollablePixels);
}

static int ScrollBarThumbPosToValue(ChScrollBar* sb, int thumb_px) {
	int scrollable_pixels = sb->trackLen - sb->thumbSize;
	if (scrollable_pixels <= 0)
		return sb->minValue;

	int scrollable_range = (sb->maxValue - sb->minValue) - sb->pageSize;
	if (scrollable_range <= 0)
		return sb->minValue;

	int value = sb->minValue + (thumb_px * scrollable_range) / scrollable_pixels;

	return SCROLLBAR_CLAMP(value, sb->minValue, sb->maxValue - sb->pageSize);
}

/**
 * @brief ChCreateScrollBar -- create a scrollbar 
 * @param x -- x location within the window
 * @param y -- y location within the window
 * @param width -- width of the viewport
 * @param height -- height of the viewport
 * @param orientation -- scrollbar type, either VERTICAL or HORIZONTAL
 */
ChScrollBar* ChCreateScrollBar(int x, int y, int width, int height, uint8_t orientation) {
	ChScrollBar* sb = (ChScrollBar*)malloc(sizeof(ChScrollBar));
	memset(sb, 0, sizeof(ChScrollBar));
	sb->base.x = x;
	sb->base.y = y;
	sb->base.w = width;
	sb->base.h = height;
	sb->base.ChPaintHandler = ChScrollBarPaint;
	sb->base.ChMouseEvent = ScrollBarMouseEvent;

	sb->orientation = orientation;
	sb->minValue = 0;
	sb->maxValue = 100;
	sb->pageSize = 10;
	sb->currentValue = 0;
	sb->prevButton = 0;

	sb->color_thumb = COLOR_THUMB;
	sb->color_arrow_bg = COLOR_ARROW_BG;
	sb->color_arrow_fg = COLOR_ARROW_FG;
	sb->color_thumb_drag = COLOR_THUMB_DRAG;
	sb->color_thumb_hover = COLOR_THUMB_HOVER;
	sb->color_track = COLOR_TRACK;
	/** Re calculate the thumb */
	ScrollBarRecalcThumb(sb);
	return sb;
}

void ChScrollBarInit(ChScrollBar* sb, int x, int y, int width, int height, uint8_t orientation) {
	memset(sb, 0, sizeof(ChScrollBar));
	sb->base.x = x;
	sb->base.y = y;
	sb->base.w = width;
	sb->base.h = height;
	sb->base.ChPaintHandler = ChScrollBarPaint;
	sb->base.ChMouseEvent = ScrollBarMouseEvent;

	sb->orientation = orientation;
	sb->minValue = 0;
	sb->maxValue = 100;
	sb->pageSize = 10;
	sb->currentValue = 0;
	sb->prevButton = 0;

	sb->color_thumb = COLOR_THUMB;
	sb->color_arrow_bg = COLOR_ARROW_BG;
	sb->color_arrow_fg = COLOR_ARROW_FG;
	sb->color_thumb_drag = COLOR_THUMB_DRAG;
	sb->color_thumb_hover = COLOR_THUMB_HOVER;
	sb->color_track = COLOR_TRACK;
	/** Re calculate the thumb */
	ScrollBarRecalcThumb(sb);
}

void ChScrollBarSetRange(ChScrollBar* sb, int min_val, int max_val, int page_size) {
	sb->minValue = min_val;
	sb->maxValue = max_val;
	sb->pageSize = page_size;
	sb->currentValue = SCROLLBAR_CLAMP(sb->currentValue, min_val, max_val - page_size);

	ScrollBarRecalcThumb(sb);
}

void ChScrollBarSetValue(ChScrollBar* sb, int value) {
	int clamped = SCROLLBAR_CLAMP(value, sb->minValue, sb->maxValue - sb->pageSize);
	if (clamped == sb->currentValue)
		return;
	sb->currentValue = clamped;
	ScrollBarRecalcThumb(sb);
}

static int ScrollBarHitTest(ChScrollBar* sb, ChWindow* win, int mx, int my) {
	int lx = mx - win->info->x;
	int ly = my - win->info->y;

	int wx = sb->base.x, wy = sb->base.y;
	int ww = sb->base.w, wh = sb->base.h;

	if (lx < wx || lx >= wx + ww || ly < wy || ly >= wy + wh)
		return SB_HIT_NONE;

	int pos = (sb->orientation == SCROLLBAR_ORIENTATION_VERTICAL) ? (ly - wy) : (lx - wx);

	int total = (sb->orientation == SCROLLBAR_ORIENTATION_VERTICAL) ? wh : ww;

	if (pos < ARROW_SIZE)
		return SB_HIT_ARROW_DEC;
	if (pos >= total - ARROW_SIZE)
		return SB_HIT_ARROW_INC;

	int track_pos = pos - ARROW_SIZE;
	if (track_pos < sb->thumbPos)
		return SB_HIT_TRACK_DEC;
	if (track_pos < sb->thumbPos + sb->thumbSize)
		return SB_HIT_THUMB;
	return SB_HIT_TRACK_INC;
}

static void
_draw_arrow_(ChWindow* win, int bx, int by, int bw, int bh, int direction, uint32_t fg) {
	int aw = bw / 2;
	int ah = bh / 2;

	int cx = bx + bw / 2;
	int cy = by + bh / 2;

	if (direction == 0) {
		int tip_y = cy - ah / 2;
		int base_y = cy + ah / 2;
		int height = base_y - tip_y;
		if (height < 1)
			height = 1;
		for (int row = 0; row <= height; row++) {
			int half = (aw * row) / height;
			int y = base_y - row;
			for (int dx = -half; dx <= half; dx++)
				ChDrawPixel(win->canv, cx + dx, y, fg);
		}
	} else if (direction == 1) {
		int tip_y = cy + ah / 2;
		int base_y = cy - ah / 2;
		int height = tip_y - base_y;
		if (height < 1)
			height = 1;
		for (int row = 0; row <= height; row++) {
			int half = (aw * row) / height;
			int y = base_y + row;
			for (int dx = -half; dx <= half; dx++)
				ChDrawPixel(win->canv, cx + dx, y, fg);
		}
	} else if (direction == 2) {
		int tip_x = cx - aw / 2;
		int base_x = cx + aw / 2;
		int width = base_x - tip_x;
		if (width < 1)
			width = 1;
		for (int col = 0; col <= width; col++) {
			int half = (ah * col) / width;
			int x = base_x - col;
			for (int dy = -half; dy <= half; dy++)
				ChDrawPixel(win->canv, x, cy + dy, fg);
		}
	} else {
		int tip_x = cx + aw / 2;
		int base_x = cx - aw / 2;
		int width = tip_x - base_x;
		if (width < 1)
			width = 1;
		for (int col = 0; col <= width; col++) {
			int half = (ah * col) / width;
			int x = base_x + col;
			for (int dy = -half; dy <= half; dy++)
				ChDrawPixel(win->canv, x, cy + dy, fg);
		}
	}
}
void ChScrollBarPaint(ChWidget* wid, ChWindow* win) {
	ChScrollBar* sb = (ChScrollBar*)wid;

	int wx = sb->base.x, wy = sb->base.y;
	int ww = sb->base.w, wh = sb->base.h;

	ChDrawRect(win->canv, wx, wy, ww, wh, sb->color_track);

	if (sb->orientation == SCROLLBAR_ORIENTATION_VERTICAL) {
		ChDrawRect(win->canv, wx, wy, ww, ARROW_SIZE, sb->color_arrow_bg);
		_draw_arrow_(win,
					 (wx + ww / 2) - (ARROW_SIZE / 2),
					 wy,
					 ARROW_SIZE,
					 ARROW_SIZE,
					 1,
					 sb->color_arrow_fg);
		ChDrawRect(win->canv, wx, wy + wh - ARROW_SIZE, ww, ARROW_SIZE, sb->color_arrow_bg);
		_draw_arrow_(win,
					 (wx + ww / 2) - (ARROW_SIZE / 2),
					 wy + wh - ARROW_SIZE,
					 ARROW_SIZE,
					 ARROW_SIZE,
					 0,
					 sb->color_arrow_fg);
	} else {
		ChDrawRect(win->canv, wx, wy, ARROW_SIZE, wh, sb->color_arrow_bg);
		_draw_arrow_(win, wx, wy, ARROW_SIZE, wh, 3, sb->color_arrow_fg);
		ChDrawRect(win->canv, wx + ww - ARROW_SIZE, wy, ARROW_SIZE, wh, sb->color_arrow_bg);
		_draw_arrow_(win, wx + ww - ARROW_SIZE, wy, ARROW_SIZE, wh, 2, sb->color_arrow_fg);
	}

	if (sb->thumbSize <= 0)
		return;

	uint32_t thumbColor;
	if (sb->state == SB_STATE_DRAGGING)
		thumbColor = sb->color_thumb_drag;
	else if (sb->state == SB_STATE_HOVER_THUMB)
		thumbColor = sb->color_thumb_hover;
	else
		thumbColor = sb->color_thumb;

	if (sb->orientation == SCROLLBAR_ORIENTATION_VERTICAL) {
		int tx = wx;
		int ty = wy + ARROW_SIZE + sb->thumbPos;
		int tw = ww;
		int th = sb->thumbSize;
		ChDrawRect(win->canv, tx, ty, tw, th, thumbColor);

		for (int i = 0; i < tw; i++)
			ChDrawPixel(win->canv, tx + i, ty, 0xFF999999);

	} else {
		int tx = wx + ARROW_SIZE + sb->thumbPos;
		int ty = wy;
		int tw = sb->thumbSize;
		int th = wh;
		ChDrawRect(win->canv, tx, ty, tw, th, thumbColor);

		for (int i = 0; i < th; i++)
			ChDrawPixel(win->canv, tx, ty + i, 0xFF999999);
	}
}

/**
 * @brief ScrollBarMouseEvent -- scroll bar mouse event
 */
void ScrollBarMouseEvent(ChWidget* wid, ChWindow* win, int mx, int my, int button) {
	ChScrollBar* sb = (ChScrollBar*)wid;

	int mouse_coord = (sb->orientation == SCROLLBAR_ORIENTATION_VERTICAL) ? (my - win->info->y)
																		  : (mx - win->info->x);

	int hit = ScrollBarHitTest(sb, win, mx, my);

	uint8_t was_down = sb->prevButton;
	uint8_t is_down = (button != 0);

	if (was_down && !is_down) {
		sb->state = (hit == SB_HIT_THUMB) ? SB_STATE_HOVER_THUMB : SB_STATE_IDLE;
		sb->prevButton = is_down;
		if (sb->base.ChPaintHandler) {
			sb->base.ChPaintHandler(wid, win);
			if (!sb->attachedTo)
				ChWindowUpdate(win, sb->base.x, sb->base.y, sb->base.w, sb->base.h, 0, 1);
		}
		return;
	}

	if (!was_down && is_down) {
		int old_value = sb->currentValue;
		int new_value = old_value;

		switch (hit) {
		case SB_HIT_THUMB: {
			sb->state = SB_STATE_DRAGGING;
			sb->dragAnchor = mouse_coord;
			sb->dragAnchorValue = sb->currentValue;
			sb->prevButton = is_down;
			if (sb->base.ChPaintHandler) {
				sb->base.ChPaintHandler(wid, win);
				if (!sb->attachedTo)
					ChWindowUpdate(win, sb->base.x, sb->base.y, sb->base.w, sb->base.h, 0, 1);
			}
			return;
		}
		case SB_HIT_ARROW_DEC:
			new_value = old_value - 3;
			sb->state = SB_STATE_HOVER_ARRDEC;
			break;
		case SB_HIT_ARROW_INC:
			new_value = old_value + 3;
			sb->state = SB_STATE_HOVER_ARRINC;
			break;
		case SB_HIT_TRACK_DEC:
			new_value = old_value - sb->pageSize;
			break;
		case SB_HIT_TRACK_INC:
			new_value = old_value + sb->pageSize;
			break;

		default:
			sb->prevButton = is_down;
			return;
		}
		new_value = SCROLLBAR_CLAMP(new_value, sb->minValue, sb->maxValue - sb->pageSize);
		if (new_value != old_value) {
			sb->currentValue = new_value;
			ScrollBarRecalcThumb(sb);
			// call dedicated scrollbar event
			if (sb->base.ChScrollEvent)
				sb->base.ChScrollEvent(wid, win, sb->currentValue, sb->orientation);
		}
		sb->prevButton = is_down;
		if (sb->base.ChPaintHandler) {
			sb->base.ChPaintHandler(wid, win);
			if (!sb->attachedTo)
				ChWindowUpdate(win, sb->base.x, sb->base.y, sb->base.w, sb->base.h, 0, 1);
		}
		return;
	}

	if (was_down && is_down) {
		if (sb->state == SB_STATE_DRAGGING) {
			int mouse_delta = mouse_coord - sb->dragAnchor;
			int scrollable_pixels = sb->trackLen - sb->thumbSize;
			int scrollable_range = (sb->maxValue - sb->minValue) - sb->pageSize;

			int start_thumb_px =
				(scrollable_range > 0 && scrollable_pixels > 0)
					? ((sb->dragAnchorValue - sb->minValue) * scrollable_pixels) / scrollable_range
					: 0;

			int new_thumb_px = SCROLLBAR_CLAMP(start_thumb_px + mouse_delta, 0, scrollable_pixels);
			int new_value = ScrollBarThumbPosToValue(sb, new_thumb_px);

			if (new_value != sb->currentValue) {
				sb->currentValue = new_value;
				ScrollBarRecalcThumb(sb);
				// call dedicated widget's scroll event  ScrollEvent(sb, sb->currentValue)
				if (sb->base.ChScrollEvent)
					sb->base.ChScrollEvent(wid, win, sb->currentValue, sb->orientation);
			}
			if (sb->base.ChPaintHandler) {
				sb->base.ChPaintHandler(wid, win);
				if (!sb->attachedTo)
					ChWindowUpdate(win, sb->base.x, sb->base.y, sb->base.w, sb->base.h, 0, 1);
			}
		}

		sb->prevButton = is_down;
		return;
	}

	uint8_t old_state = sb->state;
	sb->state = (hit == SB_HIT_THUMB) ? SB_STATE_HOVER_THUMB : SB_STATE_IDLE;
	if (sb->state != old_state) {
		if (sb->base.ChPaintHandler) {
			sb->base.ChPaintHandler(wid, win);
			if (!sb->attachedTo)
				ChWindowUpdate(win, sb->base.x, sb->base.y, sb->base.w, sb->base.h, 0, 1);
		}
	}

	sb->prevButton = is_down;
}