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
 * xlui.cpp -- metrics and painting of the start panel.
 *
 *   +-------------------------------------------+
 *   | ==== pari (plain stripes) ================ |
 *   | start                        [pwr] [rst]  |
 *   | type to search ___________________________ |
 *   | pinned                         all apps  > |
 *   | [ tiles ......... 6 column Kinetic grid ]  |
 *   | ==== phul band (woven end) =============== |
 *   +-------------------------------------------+
 *  || red border threads run down both long sides ||
 *
 * Every Paint* function touches only its own rectangle and presents
 * only that rectangle.
 */

#include "launcher.h"
#include <string.h>

void XlMetricsInit(int screenW, int screenH) {
	XlMetrics* m = &xlnch.m;
	memset(m, 0, sizeof(XlMetrics));
	bool small = screenH < 640 || screenW < 700;
	m->cell = small ? 64 : 84;
	m->gap = small ? 6 : 8;
	m->pitch = m->cell + m->gap;
	m->padX = small ? 20 : 28;
	int gridW = XL_COLS * m->cell + (XL_COLS - 1) * m->gap;
	m->panelW = gridW + 2 * m->padX;
	if (m->panelW > screenW - 20)
		m->panelW = screenW - 20;
	m->panelH = screenH - 85;

	m->titleSize = small ? 24 : 30;
	m->labelSize = small ? 11 : 12;
	m->textSize = small ? 13 : 15;
	m->smallSize = small ? 10 : 11;
	m->listRowH = small ? 32 : 40;
	m->listIcon = small ? 18 : 24;

	int B = small ? 30 : 36;
	m->title.x = m->padX;
	m->title.y = small ? 24 : 30;
	m->title.w = gridW - 2 * B - 8;
	m->title.h = small ? 32 : 40;
	m->restart.w = m->restart.h = B;
	m->restart.x = m->padX + gridW - B;
	m->restart.y = m->title.y + (m->title.h - B) / 2;
	m->power = m->restart;
	m->power.x = m->restart.x - B - 8;

	m->search.x = m->padX;
	m->search.y = m->title.y + m->title.h + 14;
	m->search.w = gridW;
	m->search.h = small ? 28 : 34;

	m->section.x = m->padX;
	m->section.y = m->search.y + m->search.h + 16;
	m->section.w = gridW;
	m->section.h = 22;
	m->allApps.w = small ? 90 : 110;
	m->allApps.h = m->section.h;
	m->allApps.x = m->section.x + gridW - m->allApps.w;
	m->allApps.y = m->section.y;

	int footerSpace = small ? 40 : 48;
	m->view.x = m->padX;
	m->view.y = m->section.y + m->section.h + 10;
	m->view.w = gridW;
	m->view.h = m->panelH - m->view.y - footerSpace;
	if (m->view.h < m->pitch)
		m->view.h = m->pitch;

	m->footer.x = m->padX;
	m->footer.w = gridW;
	m->footer.h = 26;
	m->footer.y = m->panelH - (small ? 34 : 40);
}

static bool _batch = false; /* XlPaintAll presents once at the end */

static void _present(const XlRect* r) {
	if (!_batch)
		XlPresent(r->x, r->y, r->w, r->h);
}

static void _text(const char* s, int x, int baseline, int size, uint32_t col) {
	if (!xlnch.font)
		return;
	ChFontSetSize(xlnch.font, size);
	ChFontDrawText(xlnch.canv, xlnch.font, (char*)s, x, baseline, size, col);
}

static int _text_w(const char* s, int size) {
	if (!xlnch.font)
		return 0;
	ChFontSetSize(xlnch.font, size);
	return (int)ChFontGetWidth(xlnch.font, (char*)s);
}

static void _paint_button(const XlRect* r, const XlIcon* ico, bool hover) {
	ChCanvas* c = xlnch.canv;
	XlFill(c, r->x, r->y, r->w, r->h, hover ? XL_RED : XL_CHARCOAL, NULL);
	if (ico && ico->px)
		XlIconDraw(c->buffer,
				   (int)c->canvasWidth,
				   (int)c->canvasHeight,
				   ico,
				   r->x + (r->w - ico->w) / 2,
				   r->y + (r->h - ico->h) / 2,
				   r);
}

void XlPaintHeader() {
	const XlMetrics* m = &xlnch.m;
	ChCanvas* c = xlnch.canv;
	XlFill(c, m->title.x, m->title.y, m->title.w, m->title.h, XL_BG, NULL);
	_text("start", m->title.x - 2, m->title.y + m->title.h - 8, m->titleSize, XL_INK);
	_paint_button(&m->power, &xlnch.powerIcon, xlnch.hover.kind == XL_HIT_POWER);
	_paint_button(&m->restart, &xlnch.restartIcon, xlnch.hover.kind == XL_HIT_RESTART);
	XlRect all = { m->title.x, m->title.y, m->section.w, m->title.h };
	_present(&all);
}

void XlPaintSearch() {
	const XlMetrics* m = &xlnch.m;
	const XlRect* r = &m->search;
	ChCanvas* c = xlnch.canv;
	XlFill(c, r->x, r->y, r->w, r->h, XL_BG, NULL);
	int base = r->y + r->h / 2 + m->textSize / 2 - 2;
	if (xlnch.queryLen) {
		_text(xlnch.query, r->x, base, m->textSize, XL_INK);
		int cx = r->x + _text_w(xlnch.query, m->textSize) + 2;
		if (cx < r->x + r->w - 2)
			XlFill(c, cx, base - m->textSize + 2, 1, m->textSize + 1, XL_RED, NULL);
		XlFill(c, r->x, r->y + r->h - 2, r->w, 2, XL_RED, NULL);
	} else {
		_text("type to search", r->x, base, m->textSize, XL_INK_SOFT);
		XlFill(c, r->x, r->y + r->h - 2, r->w, 2, XL_WEAVE_LINE, NULL);
	}
	_present(r);
}

void XlPaintSection() {
	const XlMetrics* m = &xlnch.m;
	const XlRect* r = &m->section;
	ChCanvas* c = xlnch.canv;
	XlFill(c, r->x, r->y, r->w, r->h, XL_BG, NULL);
	const char* left = "pinned";
	if (xlnch.view == XL_VIEW_LIST)
		left = xlnch.queryLen ? "results" : "all apps";
	int base = r->y + r->h - 5;
	_text(left, r->x, base, m->textSize + 1, XL_INK);
	const char* right = xlnch.view == XL_VIEW_TILES ? "all apps  >" : "<  pinned";
	int rw = _text_w(right, m->textSize - 1);
	int rx = m->allApps.x + m->allApps.w - rw;
	bool hov = xlnch.hover.kind == XL_HIT_TOGGLE;
	_text(right, rx, base, m->textSize - 1, hov ? XL_RED_DEEP : XL_RED);
	if (hov)
		XlFill(c, rx, base + 3, rw, 1, XL_RED_DEEP, NULL);
	_present(r);
}

/* scroll indicator lives in the right padding, next to the viewport */
static void _paint_scrollbar(int content, int scroll) {
	const XlMetrics* m = &xlnch.m;
	const XlRect* v = &m->view;
	ChCanvas* c = xlnch.canv;
	int x = v->x + v->w + 8;
	XlFill(c, x, v->y, 3, v->h, XL_BG, NULL);
	if (content <= v->h || content <= 0)
		return;
	int th = v->h * v->h / content;
	if (th < 16)
		th = 16;
	int ty = v->y + (v->h - th) * scroll / (content - v->h);
	XlFill(c, x, ty, 3, th, XL_WEAVE_LINE, NULL);
}

void XlViewClear() {
	const XlRect* v = &xlnch.m.view;
	XlFill(xlnch.canv, v->x, v->y, v->w, v->h, XL_BG, NULL);
}

static bool _is_pressed(uint8_t ai) {
	return xlnch.mouseDown && !xlnch.dragging && xlnch.press.kind == XL_HIT_TILE &&
		   xlnch.press.index == ai;
}

static void _draw_tile(XlApp* a, const XlRect* clip) {
	XlRect t;
	XlLayoutTileRect(a, &t);
	uint8_t ai = (uint8_t)(a - xlnch.apps);
	if (_is_pressed(ai)) {
		XlTileBlitScaled(xlnch.canv, a, t.x, t.y, 96, clip);
		return;
	}
	XlTileBlit(xlnch.canv, a, t.x, t.y, clip);
	bool hov = !xlnch.dragging &&
			   (xlnch.hover.kind == XL_HIT_TILE || xlnch.hover.kind == XL_HIT_TILE_PIN) &&
			   xlnch.hover.index == ai;
	XlTileOverlay(xlnch.canv, a, t.x, t.y, hov, clip);
}

void XlPaintView() {
	const XlMetrics* m = &xlnch.m;
	const XlRect* v = &m->view;
	ChCanvas* c = xlnch.canv;
	if (xlnch.view == XL_VIEW_LIST) {
		XlListPaint(c);
		_paint_scrollbar(XlListCount() * m->listRowH, xlnch.scrollList);
	} else {
		XlViewClear();
		if (xlnch.pinCount == 0)
			_text("pin apps from all apps", v->x, v->y + 30, m->textSize, XL_INK_SOFT);
		for (int i = 0; i < xlnch.pinCount; i++) {
			XlApp* a = &xlnch.apps[xlnch.pinOrder[i]];
			if (a->row < 0 || (xlnch.dragging && xlnch.press.index == xlnch.pinOrder[i]))
				continue;
			XlRect t;
			XlLayoutTileRect(a, &t);
			if (t.y >= v->y + v->h || t.y + t.h <= v->y)
				continue; /* off screen: not even touched */
			_draw_tile(a, v);
		}
		if (xlnch.dragging) {
			XlApp* d = &xlnch.apps[xlnch.press.index];
			XlTileBlitScaled(c, d, xlnch.dragX, xlnch.dragY, 104, v);
		}
		_paint_scrollbar(XlLayoutContentHeight(), xlnch.scrollTiles);
	}
	XlRect r = *v;
	r.w += 12;
	_present(&r);
}

/* repaint one tile in place (hover change, live update) */
void XlPaintTileAt(uint8_t ai) {
	if (xlnch.view != XL_VIEW_TILES || xlnch.dragging) {
		XlPaintView();
		return;
	}
	XlApp* a = &xlnch.apps[ai];
	if (!a->pinned || a->row < 0)
		return;
	const XlRect* v = &xlnch.m.view;
	XlRect t;
	XlLayoutTileRect(a, &t);
	/* clip the tile rect to the viewport */
	int x0 = t.x > v->x ? t.x : v->x;
	int y0 = t.y > v->y ? t.y : v->y;
	int x1 = (t.x + t.w) < (v->x + v->w) ? (t.x + t.w) : (v->x + v->w);
	int y1 = (t.y + t.h) < (v->y + v->h) ? (t.y + t.h) : (v->y + v->h);
	if (x1 <= x0 || y1 <= y0)
		return;
	XlRect r = { x0, y0, x1 - x0, y1 - y0 };
	XlFill(xlnch.canv, r.x, r.y, r.w, r.h, XL_BG, NULL);
	_draw_tile(a, &r);
	_present(&r);
}

void XlPaintAll(bool withContent) {
	const XlMetrics* m = &xlnch.m;
	ChCanvas* c = xlnch.canv;
	XlFill(c, 0, 0, (int)c->canvasWidth, (int)c->canvasHeight, XL_BG, NULL);
	XlGamusaBorders(c, (int)c->canvasHeight, (int)c->canvasWidth);
	XlGamusaPari(c, 14, 8, (int)c->canvasWidth - 28);
	XlGamusaPhulBand(c, m->footer.x, m->footer.y, m->footer.w);
	/* the Paint* helpers present their own rects; batch them into one
	 * full present instead */
	_batch = true;
	XlPaintHeader();
	XlPaintSearch();
	XlPaintSection();
	if (withContent)
		XlPaintView();
	else
		XlViewClear();
	_batch = false;
	XlPresent(0, 0, (int)c->canvasWidth, (int)c->canvasHeight);
}

static bool _in(const XlRect* r, int x, int y) {
	return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

XlHit XlHitTest(int lx, int ly) {
	XlHit h = { XL_HIT_NONE, 0 };
	const XlMetrics* m = &xlnch.m;
	if (_in(&m->power, lx, ly)) {
		h.kind = XL_HIT_POWER;
		return h;
	}
	if (_in(&m->restart, lx, ly)) {
		h.kind = XL_HIT_RESTART;
		return h;
	}
	if (_in(&m->allApps, lx, ly)) {
		h.kind = XL_HIT_TOGGLE;
		return h;
	}
	if (!_in(&m->view, lx, ly))
		return h;
	if (xlnch.view == XL_VIEW_LIST)
		return XlListHit(lx, ly);
	int ai = XlLayoutTileAt(lx, ly);
	if (ai < 0)
		return h;
	XlApp* a = &xlnch.apps[ai];
	XlRect t, b;
	XlLayoutTileRect(a, &t);
	XlTilePinBadgeRect(a, t.x, t.y, &b);
	h.kind = _in(&b, lx, ly) ? XL_HIT_TILE_PIN : XL_HIT_TILE;
	h.index = (uint8_t)ai;
	return h;
}
