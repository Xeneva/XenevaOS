/**
* @file sidebar.cpp
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

#include "sidebar.h"
#include <stdlib.h>
#include <string.h>

void ChSidebarPaint(ChWidget* widget, ChWindow* win);
void ChSidebarMouseEvent(ChWidget* wid, ChWindow* win, int mx, int my, int button);
/* flatsidebar.cpp */
void ChSidebarFlatDrawRow(ChSidebar* sb, ChWindow* win, int sIdx, int iIdx, int rowY, int drawY, int drawH);
/**
 * @brief ChSidebarCreate -- create a new sidebar
 * @param x -- x location of the sidebar
 * @param y -- y location of the sidebar
 * @param w -- width of the sidebar
 * @param h -- height of the sidebar
 */
ChSidebar* ChSidebarCreate(int x, int y, int w, int h) {
	ChSidebar* sb = (ChSidebar*)malloc(sizeof(ChSidebar));
	if (!sb)
		return NULL;

	memset(sb, 0, sizeof(ChSidebar));

	sb->base.x = x;
	sb->base.y = y;
	sb->base.w = w;
	sb->base.h = h;
	sb->base.ChPaintHandler = ChSidebarPaint;
	sb->base.ChMouseEvent = ChSidebarMouseEvent;

	sb->bgColor = 0xFFF0F0F0;
	sb->selectedColor = 0xFF3478F6;
	sb->hoverColor = 0xFFE0E0E0;
	sb->textColor = 0xFF202020;
	sb->headerTextColor = 0xFF7A7575;
	sb->hoverSectionIdx = -1;
	sb->hoverItemIdx = -1;
	sb->selectedTextColor = 0xFFC9C6C6;

	return sb;
}

ChSidebarSection* ChSidebarAddSection(ChSidebar* sb, const char* title) {
	sb->sections =
		(ChSidebarSection*)realloc(sb->sections, sizeof(ChSidebarSection) * (sb->sectionCount + 1));
	ChSidebarSection* sec = &sb->sections[sb->sectionCount++];
	memset(sec, 0, sizeof(ChSidebarSection));
	strncpy(sec->title, title, SIDEBAR_MAX_LABEL - 1);
	return sec;
}

#define BASELINE_RATIO_NUM 7
#define BASELINE_RATIO_DEN 10

static int _sidebar_baseline_y(int rowTop, int rowHeight, int fontSz) {
	/*int ascent = (fontSz * BASELINE_RATIO_NUM) / BASELINE_RATIO_DEN;
	return rowTop  +(rowHeight - fontSz) / 2 + ascent;*/
	int ascent = (rowHeight * BASELINE_RATIO_NUM) / BASELINE_RATIO_DEN;
	if (ascent > rowHeight)
		ascent = rowHeight;
	return rowTop + ascent;
}

ChSidebarItem* ChSidebarAddItem(
	ChSidebarSection* sec, const char* label, ChIcon* icon, int iconW, int iconH, void* udata) {
	sec->items = (ChSidebarItem*)realloc(sec->items, sizeof(ChSidebarItem) * (sec->itemCount + 1));
	ChSidebarItem* item = &sec->items[sec->itemCount++];
	memset(item, 0, sizeof(ChSidebarItem));
	strncpy(item->label, label, SIDEBAR_MAX_LABEL - 1);
	item->icon = icon;
	item->iconW = iconW;
	item->iconH = iconH;
	item->data = udata;
	return item;
}

static int _rect_intersect(ChRect* dst, const ChRect* a, const ChRect* b) {
	int ax2 = a->x + a->w;
	int ay2 = a->y + a->h;
	int bx2 = b->x + b->w;
	int by2 = b->y + b->h;

	int x1 = (a->x > b->x) ? a->x : b->x;
	int y1 = (a->y > b->y) ? a->y : b->y;
	int x2 = (ax2 < bx2) ? ax2 : bx2;
	int y2 = (ay2 < by2) ? ay2 : by2;

	if (x2 <= x1 || y2 <= y1) {
		dst->x = 0;
		dst->y = 0;
		dst->w = 0;
		dst->h = 0;
		return 0;
	}

	dst->x = x1;
	dst->y = y1;
	dst->w = x2 - x1;
	dst->h = y2 - y1;
	return 1;
}

void ChSidebarPaint(ChWidget* widget, ChWindow* win) {
	ChSidebar* sb = (ChSidebar*)widget;
	int curY = sb->base.y - sb->scrollOfsetY;

	ChRect sidebarClip;
	sidebarClip.x = sb->base.x;
	sidebarClip.y = sb->base.y;
	sidebarClip.w = sb->base.w;
	sidebarClip.h = sb->base.h;

	ChDrawRect(win->canv, sb->base.x, sb->base.y, sb->base.w, sb->base.h, sb->bgColor);

	for (int s = 0; s < sb->sectionCount; s++) {
		ChSidebarSection* sec = &sb->sections[s];

		if (curY + SIDEBAR_HEADER > sb->base.y && curY < sb->base.y + sb->base.h) {
			ChRect headerClip;
			headerClip.x = sb->base.x;
			headerClip.y = curY;
			headerClip.w = sb->base.w;
			headerClip.h = SIDEBAR_HEADER;

			//header intersect calculation
			//_rect_intersect(&headerClip, &sidebarClip, &headerClip);
			ChFontSetSize(win->app->baseFont, 13);
			int baseY = _sidebar_baseline_y(curY, SIDEBAR_HEADER, 13);
			ChFontDrawTextClipped(win->canv,
								  win->app->baseFont,
								  sec->title,
								  sb->base.x + SIDEBAR_INDENT,
								  baseY,
								  sb->headerTextColor,
								  &headerClip);
		}
		curY += SIDEBAR_HEADER;

		for (int i = 0; i < sec->itemCount; i++) {
			ChSidebarItem* item = &sec->items[i];

			if (curY + SIDEBAR_ROW_HEIGHT < sb->base.y || curY > sb->base.y + sb->base.h) {
				curY += SIDEBAR_ROW_HEIGHT;
				continue;
			}

			ChRect rowClip;
			rowClip.x = sb->base.x;
			rowClip.y = curY;
			rowClip.w = sb->base.w;
			rowClip.h = SIDEBAR_ROW_HEIGHT;
			//calculate intersection
			//_rect_intersect(&rowClip, &sidebarClip, &rowClip);

			uint32_t row_col = sb->bgColor;
			if (item->selected)
				row_col = sb->selectedColor;
			else if (s == sb->hoverSectionIdx && i == sb->hoverItemIdx)
				row_col = sb->hoverColor;

			ChDrawRect(win->canv, sb->base.x, curY, sb->base.w, SIDEBAR_ROW_HEIGHT, row_col);

			int textX = sb->base.x + SIDEBAR_INDENT;

			if (item->icon) {
				int iconY = curY + (SIDEBAR_ROW_HEIGHT - item->icon->image.height) / 2;
				ChDrawIconClipped(win->canv, item->icon, textX, iconY, &rowClip);
				textX += item->icon->image.width + 8;
			}

			uint32_t textCol = item->selected ? sb->selectedTextColor : sb->textColor;

			int baseY = _sidebar_baseline_y(curY, SIDEBAR_ROW_HEIGHT, 13);
			ChFontDrawTextClipped(
				win->canv, win->app->baseFont, item->label, textX, baseY, textCol, &rowClip);

			curY += SIDEBAR_ROW_HEIGHT;
		}
	}

	sb->contentHeight = curY - (sb->base.y - sb->scrollOfsetY);
}

static bool _sidebar_item_screen_y(ChSidebar* sb, int sectionIdx, int itemIdx, int* out_y) {
	int curY = sb->base.y - sb->scrollOfsetY;

	for (int s = 0; s <= sectionIdx; s++) {
		curY += SIDEBAR_HEADER;
		int itemLimit = (s == sectionIdx) ? itemIdx : sb->sections[s].itemCount;
		curY += itemLimit * SIDEBAR_ROW_HEIGHT;
	}

	if (curY + SIDEBAR_ROW_HEIGHT <= sb->base.y || curY >= sb->base.y + sb->base.h)
		return false;

	*out_y = curY;
	return true;
}

static bool
_sidebar_draw_single_row_(ChSidebar* sb, ChWindow* win, int sIdx, int iIdx, ChRect* out_rect) {
	if (sIdx < 0 || sIdx >= sb->sectionCount)
		return false;
	if (iIdx < 0 || iIdx >= sb->sections[sIdx].itemCount)
		return false;

	int curY;
	if (!_sidebar_item_screen_y(sb, sIdx, iIdx, &curY))
		return false;

	ChSidebarItem* item = &sb->sections[sIdx].items[iIdx];

	int drawY = curY, drawH = SIDEBAR_ROW_HEIGHT;
	if (drawY < sb->base.y) {
		int d = sb->base.y - drawY;
		drawY += d;
		drawH -= d;
	}

	if (drawY + drawH > sb->base.y + sb->base.h)
		drawH = (sb->base.y + sb->base.h) - drawY;
	if (drawH <= 0)
		return false;

	if (sb->styleFlags & CH_SIDEBAR_FLAT) {
		ChSidebarFlatDrawRow(sb, win, sIdx, iIdx, curY, drawY, drawH);
	} else {
		uint32_t row_col = sb->bgColor;
		if (item->selected)
			row_col = sb->selectedColor;
		else if (sIdx == sb->hoverSectionIdx && iIdx == sb->hoverItemIdx)
			row_col = sb->hoverColor;

		ChDrawRect(win->canv, sb->base.x, drawY, sb->base.w, drawH, row_col);

		if (curY >= sb->base.y) {
			ChRect rowClip;
			rowClip.x = sb->base.x;
			rowClip.y = drawY;
			rowClip.w = sb->base.w;
			rowClip.h = drawH;

			int textX = sb->base.x + SIDEBAR_INDENT;

			if (item->icon) {
				int iconY = curY + (SIDEBAR_ROW_HEIGHT - item->icon->image.height) / 2;
				ChDrawIconClipped(win->canv, item->icon, textX, iconY, &rowClip);
				textX += item->icon->image.width + 8;
			}

			uint32_t textCol = item->selected ? sb->selectedTextColor : sb->textColor;
			int baseY = _sidebar_baseline_y(curY, SIDEBAR_ROW_HEIGHT, 13);
			ChFontSetSize(win->app->baseFont, 13);
			ChFontDrawTextClipped(
				win->canv, win->app->baseFont, item->label, textX, baseY, textCol, &rowClip);
		}
	}

	if (out_rect) {
		out_rect->x = sb->base.x;
		out_rect->y = drawY;
		out_rect->w = sb->base.w;
		out_rect->h = drawH;
	}
	return true;
}

void ChSidebarMouseEvent(ChWidget* wid, ChWindow* win, int mx, int my, int button) {
	ChSidebar* sb = (ChSidebar*)wid;
	int lx = mx, ly = my;
	mx = lx - win->info->x;
	my = ly - win->info->y;

	if (mx < sb->base.x || mx > sb->base.x + sb->base.w || my < sb->base.y ||
		my > sb->base.y + sb->base.h) {
		if (sb->hoverSectionIdx != -1 || sb->hoverItemIdx != -1) {
			int oldS = sb->hoverSectionIdx;
			int OldI = sb->hoverItemIdx;

			sb->hoverSectionIdx = -1;
			sb->hoverItemIdx = -1;
			ChRect r;
			if (_sidebar_draw_single_row_(sb, win, oldS, OldI, &r))
				ChWindowUpdate(win, r.x, r.y, r.w, r.h, 0, 1);
		}
		return;
	}

	int curY = sb->base.y - sb->scrollOfsetY;
	int hitSection = -1, hitItem = -1;

	/* rows have a fixed height: one division per section finds the row,
	 * O(sections) instead of O(rows) */
	for (int s = 0; s < sb->sectionCount; s++) {
		ChSidebarSection* sec = &sb->sections[s];
		curY += SIDEBAR_HEADER;
		int span = sec->itemCount * SIDEBAR_ROW_HEIGHT;
		if (my >= curY && my < curY + span) {
			hitSection = s;
			hitItem = (my - curY) / SIDEBAR_ROW_HEIGHT;
			break;
		}
		if (my < curY)
			break; /* on a header */
		curY += span;
	}

	if (button == 0) {
		if (hitSection != sb->hoverSectionIdx || hitItem != sb->hoverItemIdx) {
			int oldS = sb->hoverSectionIdx;
			int oldI = sb->hoverItemIdx;
			sb->hoverSectionIdx = hitSection;
			sb->hoverItemIdx = hitItem;
			_KePrint("Hover drawing \r\n");

			ChRect r1;
			bool has_r1 = _sidebar_draw_single_row_(sb, win, oldS, oldI, &r1);
			if (has_r1)
				ChWindowUpdate(win, r1.x, r1.y, r1.w, r1.h, 0, 1);
			ChRect r2;
			bool has_r2 = _sidebar_draw_single_row_(sb, win, hitSection, hitItem, &r2);
			if (has_r2)
				ChWindowUpdate(win, r2.x, r2.y, r2.w, r2.h, 0, 1);
		}
		return;
	}

	if (button == 1) {
		if (hitSection >= 0 && hitItem >= 0) {
			int prevSelS = -1, prevSelI = -1;
			for (int s = 0; s < sb->sectionCount; s++) {
				for (int i = 0; i < sb->sections[s].itemCount; i++) {
					if (sb->sections[s].items[i].selected) {
						prevSelS = s;
						prevSelI = i;
						sb->sections[s].items[i].selected = 0;
					}
				}
			}

			ChSidebarItem* item = &sb->sections[hitSection].items[hitItem];
			item->selected = 1;

			if (item->OnSelect)
				item->OnSelect(item, win);

			ChRect r1;
			bool has_r1 = _sidebar_draw_single_row_(sb, win, prevSelS, prevSelI, &r1);
			if (has_r1)
				ChWindowUpdate(win, r1.x, r1.y, r1.w, r1.h, 0, 1);

			ChRect r2;
			bool has_r2 = _sidebar_draw_single_row_(sb, win, hitSection, hitItem, &r2);
			if (has_r2)
				ChWindowUpdate(win, r2.x, r2.y, r2.w, r2.h, 0, 1);
		}
		return;
	}

	if (button == 2) {
		//context menu
		return;
	}
}