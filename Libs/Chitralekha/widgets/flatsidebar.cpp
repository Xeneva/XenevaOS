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

/*
 * flatsidebar.cpp -- flat Gamusa sidebar painter (CH_SIDEBAR_FLAT).
 *
 *   ◆ library               section: a muga lozenge + red label
 *   ▌ music                 selected: red thread on the left, ink text
 *     pictures              others: soft ink, a linen wash on hover
 *
 * Same geometry as the classic sidebar (header 22px, rows 28px), so the
 * hit testing and the "repaint only the old and the new row" logic in
 * sidebar.cpp work unchanged; only the pixels differ.
 */

#include "sidebar.h"
#include "gmstyle.h"
#include <string.h>

void ChSidebarPaint(ChWidget* widget, ChWindow* win);

#define FLAT_HOVER 0xFFF8F2EA

static int _edge(ChSidebar* sb) {
	return (sb->styleFlags & CH_SIDEBAR_FLAT_NO_EDGE) ? 0 : 1;
}

void ChSidebarFlatDrawRow(ChSidebar* sb, ChWindow* win, int sIdx, int iIdx, int rowY, int drawY, int drawH) {
	ChCanvas* c = win->canv;
	ChSidebarItem* item = &sb->sections[sIdx].items[iIdx];
	bool hover = (sIdx == sb->hoverSectionIdx && iIdx == sb->hoverItemIdx);
	int w = sb->base.w - _edge(sb);
	uint32_t fill = item->selected ? CH_GM_LINEN : (hover ? FLAT_HOVER : sb->bgColor);
	ChGmFill(c, sb->base.x, drawY, w, drawH, fill);

	ChRect rowClip;
	rowClip.x = sb->base.x;
	rowClip.y = drawY;
	rowClip.w = w;
	rowClip.h = drawH;
	if (item->selected) {
		int ty = rowY + 3, th = SIDEBAR_ROW_HEIGHT - 6;
		int y0 = ty > drawY ? ty : drawY;
		int y1 = (ty + th) < (drawY + drawH) ? (ty + th) : (drawY + drawH);
		if (y1 > y0)
			ChGmFill(c, sb->base.x, y0, 3, y1 - y0, CH_GM_RED);
	}
	if (rowY < sb->base.y)
		return; /* partly scrolled out: wash only, like the classic one */

	int textX = sb->base.x + SIDEBAR_INDENT + 6;
	if (item->icon) {
		int iconY = rowY + (SIDEBAR_ROW_HEIGHT - item->icon->image.height) / 2;
		ChDrawIconClipped(c, item->icon, textX, iconY, &rowClip);
		textX += item->icon->image.width + 8;
	}
	uint32_t textCol = (item->selected || hover) ? CH_GM_INK : CH_GM_INK_SOFT;
	ChGmTextClipped(c, win->app->baseFont, item->label, textX, rowY + 18, textCol, &rowClip);
}

static void ChSidebarFlatPaint(ChWidget* widget, ChWindow* win) {
	ChSidebar* sb = (ChSidebar*)widget;
	ChCanvas* c = win->canv;
	int x = sb->base.x, y = sb->base.y, w = sb->base.w, h = sb->base.h;
	int iw = w - _edge(sb);
	ChGmFill(c, x, y, iw, h, sb->bgColor);
	if (_edge(sb))
		ChGmFill(c, x + w - 1, y, 1, h, CH_GM_HAIRLINE);

	int curY = y - sb->scrollOfsetY;
	for (int s = 0; s < sb->sectionCount; s++) {
		ChSidebarSection* sec = &sb->sections[s];
		if (curY + SIDEBAR_HEADER > y && curY < y + h && sec->title[0]) {
			ChRect clip = { x, curY > y ? curY : y, iw, SIDEBAR_HEADER };
			if (curY >= y) {
				ChGmLozenge(c, x + SIDEBAR_INDENT + 2, curY + 12, 2, CH_GM_MUGA);
				ChGmTextClipped(c, win->app->baseFont, sec->title, x + SIDEBAR_INDENT + 10, curY + 16,
								CH_GM_RED, &clip);
			}
		}
		curY += SIDEBAR_HEADER;
		int span = sec->itemCount * SIDEBAR_ROW_HEIGHT;
		/* rows above the viewport are skipped in O(1) */
		int first = 0;
		if (curY < y) {
			first = (y - curY) / SIDEBAR_ROW_HEIGHT;
			if (first > sec->itemCount)
				first = sec->itemCount;
		}
		for (int i = first; i < sec->itemCount; i++) {
			int rowY = curY + i * SIDEBAR_ROW_HEIGHT;
			if (rowY >= y + h)
				break;
			int drawY = rowY, drawH = SIDEBAR_ROW_HEIGHT;
			if (drawY < y) {
				drawH -= y - drawY;
				drawY = y;
			}
			if (drawY + drawH > y + h)
				drawH = y + h - drawY;
			if (drawH > 0)
				ChSidebarFlatDrawRow(sb, win, s, i, rowY, drawY, drawH);
		}
		curY += span;
	}
	sb->contentHeight = curY - (y - sb->scrollOfsetY);
}

XE_EXTERN XE_EXPORT void ChSidebarSetStyle(ChSidebar* sb, uint16_t flags) {
	if (!sb)
		return;
	sb->styleFlags = flags;
	if (flags & CH_SIDEBAR_FLAT) {
		sb->base.ChPaintHandler = ChSidebarFlatPaint;
		sb->bgColor = CH_GM_COTTON;
		sb->selectedColor = CH_GM_LINEN;
		sb->hoverColor = FLAT_HOVER;
		sb->textColor = CH_GM_INK_SOFT;
		sb->headerTextColor = CH_GM_RED;
		sb->selectedTextColor = CH_GM_INK;
	} else {
		/* the classic colours from ChSidebarCreate */
		sb->base.ChPaintHandler = ChSidebarPaint;
		sb->bgColor = 0xFFF0F0F0;
		sb->selectedColor = 0xFF3478F6;
		sb->hoverColor = 0xFFE0E0E0;
		sb->textColor = 0xFF202020;
		sb->headerTextColor = 0xFF7A7575;
		sb->selectedTextColor = 0xFFC9C6C6;
	}
}
