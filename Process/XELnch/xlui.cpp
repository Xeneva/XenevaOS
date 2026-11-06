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
 * xlui.cpp -- metrics, panel painting and hit testing.
 *
 * Every Paint* function touches only its own rectangle and presents only
 * that rectangle (XlPaintAll batches them into one present).
 */

#include "launcher.h"
#include <calevt.h>
#include <stdlib.h>
#include <string.h>

static bool _batch = false;

static void _present(const XlRect* r) {
	if (!_batch)
		XlPresent(r->x, r->y, r->w, r->h);
}

/*
 * XlMetricsInit -- the panel sits at (10,10) and stops above Namdapha.
 * Below 900x600 the header and ribbon tighten their spacing; cards and
 * faces never change size (see bento.cpp, text.cpp). Everything below is
 * derived, nothing assumes a resolution.
 */
void XlMetricsInit(int screenW, int screenH) {
	XlMetrics* m = &xlnch.m;
	memset(m, 0, sizeof(XlMetrics));
	m->panelW = screenW - 20 > 1180 ? 1180 : screenW - 20;
	m->panelH = screenH - XL_PANEL_Y - XL_TASKBAR_H - 8;
	if (m->panelH < 300)
		m->panelH = 300;
	m->compact = m->panelW < 900 || m->panelH < 600;
	bool k = m->compact;
	m->padX = m->panelW >= 1000 ? 44 : (k ? 22 : 28);
	m->contentW = m->panelW - 2 * m->padX;
	m->gap = XL_CARD_GAP; /* part of the card grid: never changes */

	/* header: headline on the left, clock on the right */
	m->headline.x = m->padX;
	m->headline.y = k ? 18 : 26;
	m->headline.w = m->contentW * 55 / 100;
	m->headline.h = k ? 58 : 76;
	m->headBase = m->headline.y + (k ? 34 : 46);
	m->hintBase = m->headBase + (k ? 16 : 20);
	m->ruleY = m->headBase + (k ? 21 : 26);
	m->clock.w = k ? 240 : 300;
	m->clock.x = m->panelW - m->padX - m->clock.w;
	m->clock.y = m->headline.y;
	m->clock.h = m->headline.h;

	m->mast.x = m->padX;
	m->mast.y = m->headline.y + m->headline.h + (k ? 4 : 6);
	m->mast.w = m->contentW;
	m->mast.h = k ? 26 : 30;
	m->mastBase = k ? 15 : 18;

	/* bottom up: phul band, ribbon, toggle */
	m->footerY = m->panelH - (k ? 30 : 34);
	m->flowHW = k ? 30 : 42;
	m->flowW0 = k ? 32 : 40;
	m->flowWFMax = k ? 210 : 250;
	m->flowHF = k ? 62 : 84;
	m->flowCY = m->footerY - (k ? 10 : 12) - m->flowHW - (k ? 6 : 8);
	m->flow.x = m->padX - 26;
	m->flow.w = m->contentW + 52;
	m->flow.y = m->flowCY - m->flowHW - (k ? 34 : 44);
	m->flow.h = m->footerY - 4 - m->flow.y;
	m->toggle.x = m->padX;
	m->toggle.h = k ? 26 : 30;
	m->toggle.y = m->flow.y - m->toggle.h - 4;
	m->toggle.w = 320;
	m->toggleYOpen = m->toggle.y;

	m->view.x = m->padX;
	m->view.y = m->mast.y + m->mast.h + (k ? 6 : 10);
	m->view.w = m->contentW;
	m->view.h = m->toggle.y - (k ? 10 : 14) - m->view.y;
	if (m->view.h < 60)
		m->view.h = 60; /* tiny screens scroll */
	m->viewHOpen = m->view.h;
	/* cards are the same size on every screen: a smaller screen only
	 * wraps them into more rows (bento.cpp) and the view scrolls */
	m->rowTall = XL_CARD_TALL_H;
	m->rowShort = XL_CARD_SHORT_H;

	m->listRowH = k ? 38 : 44;
	m->listCols = m->contentW >= 640 ? 2 : 1;
	m->listColW = (m->contentW - m->gap * (m->listCols - 1)) / m->listCols;
}

/* ------------------------------------------------------------ strip buffer */

#define XL_STRIP_ROWS 32

static uint32_t* _strip;
static int _stripW; /* pixels per strip row */
static int _stripDepth;

void XlBufferInit() {
	if (_strip && _stripW >= xlnch.m.panelW)
		return;
	free(_strip);
	_strip = (uint32_t*)malloc((size_t)xlnch.m.panelW * XL_STRIP_ROWS * 4);
	_stripW = _strip ? xlnch.m.panelW : 0;
}

void XlBuffered(int x, int y, int w, int h, void (*fn)(void*), void* arg) {
	ChCanvas* real = xlnch.canv;
	/* clip to the panel */
	if (x < 0) {
		w += x;
		x = 0;
	}
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (x + w > (int)real->canvasWidth)
		w = (int)real->canvasWidth - x;
	if (y + h > (int)real->canvasHeight)
		h = (int)real->canvasHeight - y;
	if (w <= 0 || h <= 0)
		return;
	if (!_strip || _stripDepth || (int)real->canvasWidth > _stripW) {
		/* nested, or no strip: paint in place */
		fn(arg);
		if (!_stripDepth)
			XlPresent(x, y, w, h);
		return;
	}
	_stripDepth++;
	xlnch.presentHold++;
	ChCanvas strip;
	memset(&strip, 0, sizeof(strip));
	strip.canvasWidth = real->canvasWidth;
	strip.buffer = _strip;
	xlnch.canv = &strip;
	for (int sy = y; sy < y + h; sy += XL_STRIP_ROWS) {
		int rows = y + h - sy < XL_STRIP_ROWS ? y + h - sy : XL_STRIP_ROWS;
		strip.canvasHeight = rows;
		xlOriginY = sy;
		/* start from what the window shows: a painter that leaves part of
		 * the rect alone (a band owned by someone else) keeps those pixels
		 * instead of whatever the strip held from an earlier paint */
		for (int r = 0; r < rows; r++)
			XlMemcpy(_strip + (size_t)r * strip.canvasWidth + x,
					 real->buffer + (size_t)(sy + r) * real->canvasWidth + x,
					 (size_t)w * 4);
		fn(arg);
		/* the finished strip goes into the window in one copy per row */
		for (int r = 0; r < rows; r++)
			XlMemcpy(real->buffer + (size_t)(sy + r) * real->canvasWidth + x,
					 _strip + (size_t)r * strip.canvasWidth + x,
					 (size_t)w * 4);
	}
	xlOriginY = 0;
	xlnch.canv = real;
	xlnch.presentHold--;
	_stripDepth--;
	XlPresent(x, y, w, h);
}

/* ------------------------------------------------------------ header */

static void _XlPaintSearch_impl(void*) {
	const XlMetrics* m = &xlnch.m;
	const XlRect* r = &m->headline;
	ChCanvas* c = xlnch.canv;
	XlFill(c, r->x, r->y, r->w, r->h, XL_BG, NULL);
	int base = m->headBase;
	if (xlnch.queryLen == 0) {
		XlText(c, XL_GS_THIN, XL_HEADLINE, r->x - 2, base, XL_INK, 255, r);
		XlText(c, XL_GS_BODY, XL_HINT, r->x, m->hintBase, XL_INK_SOFT, 255, r);
	} else {
		/* the headline becomes the query; long ones scroll left */
		const char* q = xlnch.query;
		while (*q && XlTextWidth(XL_GS_THIN, q) > r->w - 12)
			q++;
		int w = XlText(c, XL_GS_THIN, q, r->x - 2, base, XL_INK, 255, r);
		XlFill(c,
			   r->x + w + 2,
			   base - XlTextAscent(XL_GS_THIN),
			   2,
			   XlTextAscent(XL_GS_THIN) + 4,
			   XL_RED,
			   r);
		char hint[XL_TITLE_MAX + 40];
		if (xlnch.resultCount) {
			strcpy(hint, "enter opens ");
			strcat(hint, xlnch.apps[xlnch.results[0]].title);
		} else {
			strcpy(hint, "nothing by that name");
		}
		XlText(c,
			   XL_GS_BODY,
			   hint,
			   r->x,
			   m->hintBase,
			   xlnch.resultCount ? XL_RED_DEEP : XL_INK_SOFT,
			   255,
			   r);
	}
	_present(r);
}

static const char* _wday[7] = {
	"sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday"};
static const char* _mon[13] = {"",
							   "january",
							   "february",
							   "march",
							   "april",
							   "may",
							   "june",
							   "july",
							   "august",
							   "september",
							   "october",
							   "november",
							   "december"};

static void _XlPaintClock_impl(void*) {
	const XlMetrics* m = &xlnch.m;
	const XlRect* r = &m->clock;
	ChCanvas* c = xlnch.canv;
	XlFill(c, r->x, r->y, r->w, r->h, XL_BG, NULL);
	CalNow n;
	CalEvtNow(&n);
	xlnch.minuteShown = n.hour * 60 + n.minute;
	char t[8];
	int h12 = n.hour % 12 ? n.hour % 12 : 12, k = 0;
	if (h12 >= 10)
		t[k++] = '1';
	t[k++] = (char)('0' + h12 % 10);
	t[k++] = ':';
	t[k++] = (char)('0' + n.minute / 10);
	t[k++] = (char)('0' + n.minute % 10);
	t[k] = 0;
	int right = r->x + r->w;
	int base = m->headBase;
	XlText(c, XL_GS_THIN, t, right - XlTextWidth(XL_GS_THIN, t), base, XL_INK, 255, r);
	char d[40];
	strcpy(d, _wday[n.weekday]);
	strcat(d, ", ");
	char num[4];
	num[0] = (char)('0' + n.day / 10);
	num[1] = (char)('0' + n.day % 10);
	num[2] = 0;
	strcat(d, n.day >= 10 ? num : num + 1);
	strcat(d, " ");
	strcat(d, _mon[n.month]);
	XlText(c, XL_GS_BODY, d, right - XlTextWidth(XL_GS_BODY, d), m->hintBase, XL_INK_SOFT, 255, r);
	_present(r);
}

void XlPaintHeader() {
	XlPaintSearch();
	XlPaintClock();
}

/* ------------------------------------------------------------ masthead */

static const char* _link_label(int i) {
	uint8_t ai = xlnch.linkApp[i];
	if (ai == XL_NONE)
		return xlnch.view == XL_VIEW_CARDS ? "all apps  >" : "<  always here";
	return xlnch.apps[ai].name;
}

static void _XlPaintLink_impl(void* arg) {
	int i = *(int*)arg;
	if (i < 0 || i >= xlnch.linkCount)
		return;
	const XlRect* r = &xlnch.linkRect[i];
	ChCanvas* c = xlnch.canv;
	bool hov = xlnch.hover.kind == XL_HIT_LINK && xlnch.hover.index == i;
	XlFill(c, r->x, r->y, r->w, r->h, XL_BG, NULL);
	const char* s = _link_label(i);
	bool toggle = xlnch.linkApp[i] == XL_NONE;
	uint32_t col = hov ? (toggle ? XL_RED_DEEP : XL_INK) : (toggle ? XL_RED : XL_INK_SOFT);
	int mb = xlnch.m.mastBase;
	int w = XlText(c, XL_GS_BODY, s, r->x, r->y + mb, col, 255, r);
	if (hov)
		XlFill(c, r->x, r->y + mb + 3, w, 1, col, r);
	_present(r);
}

static void _layout_links() {
	const XlRect* mr = &xlnch.m.mast;
	int x = mr->x + mr->w;
	for (int i = xlnch.linkCount - 1; i >= 0; i--) {
		int w = XlTextWidth(XL_GS_BODY, _link_label(i)) + 2;
		x -= w;
		xlnch.linkRect[i].x = x;
		xlnch.linkRect[i].y = mr->y;
		xlnch.linkRect[i].w = w;
		xlnch.linkRect[i].h = mr->h - 4;
		x -= 26; /* room for the separating dot */
	}
}

static void _XlPaintMast_impl(void*) {
	const XlMetrics* m = &xlnch.m;
	const XlRect* r = &m->mast;
	ChCanvas* c = xlnch.canv;
	XlFill(c, r->x, r->y, r->w, r->h, XL_BG, NULL);
	const char* left = "always here";
	if (xlnch.view == XL_VIEW_LIST)
		left = xlnch.queryLen ? "found" : "every app";
	int mb = m->mastBase;
	XlText(c, XL_GS_ITALIC, left, r->x, r->y + mb, XL_RED, 255, r);
	/* a fine lozenge chain instead of a plain rule: the one woven touch */
	if (xlnch.view == XL_VIEW_LIST) {
		XlFill(c, r->x, r->y + mb + 5, r->w, 1, XL_INK, r);
		for (int x = r->x + 6; x < r->x + r->w - 2; x += 18)
			XlLozenge(c, x, r->y + mb + 10, 2, XL_WEAVE_LINE, r);
	}
	_layout_links();
	bool saved = _batch;
	_batch = true;
	for (int i = 0; i < xlnch.linkCount; i++) {
		XlPaintLink(i);
		if (i + 1 < xlnch.linkCount)
			XlLozenge(c,
					  xlnch.linkRect[i].x + xlnch.linkRect[i].w + 12,
					  r->y + mb - 5,
					  2,
					  XL_INK_SOFT,
					  r);
	}
	_batch = saved;
	_present(r);
}

/* ------------------------------------------------------------ view */

void XlViewClear() {
	const XlRect* v = &xlnch.m.view;
	XlFill(xlnch.canv, v->x, v->y - 4, v->w + 14, v->h + 10, XL_BG, NULL);
}

static bool _is_pressed(uint8_t ai) {
	return xlnch.mouseDown && !xlnch.dragging && xlnch.press.kind == XL_HIT_CARD &&
		   xlnch.press.index == ai;
}

/* view clip with a little room for the hover lift and shadow */
static void _view_clip(XlRect* r) {
	*r = xlnch.m.view;
	r->y -= 4;
	r->h += 10;
	r->w += 6;
}

static void _paint_card(const XlCard* k, const XlRect* clip) {
	XlApp* a = &xlnch.apps[k->app];
	XlRect t;
	XlCardRect(k, &t);
	int lift = 3 * a->hov / 255;
	if (a->hov)
		XlFillAlpha(xlnch.canv, t.x + 4, t.y + 6 - lift, t.w, t.h, XL_SHADOW, a->hov, clip);
	XlCardPaint(xlnch.canv, k, t.x, t.y - lift, 256, 255, _is_pressed(k->app), clip);
}

static void _scrollbar() {
	const XlRect* v = &xlnch.m.view;
	ChCanvas* c = xlnch.canv;
	int content =
		xlnch.view == XL_VIEW_CARDS
			? xlnch.contentH
			: ((XlListCount() + xlnch.m.listCols - 1) / xlnch.m.listCols) * xlnch.m.listRowH;
	int scroll = xlnch.view == XL_VIEW_CARDS ? xlnch.scrollCards : xlnch.scrollList;
	int x = v->x + v->w + 10;
	XlFill(c, x, v->y, 3, v->h, XL_BG, NULL);
	if (content <= v->h)
		return;
	int th = v->h * v->h / content;
	if (th < 18)
		th = 18;
	int ty = v->y + (v->h - th) * scroll / (content - v->h);
	XlFill(c, x, ty, 3, th, XL_WEAVE_LINE, NULL);
}

static void _XlPaintView_impl(void*) {
	const XlMetrics* m = &xlnch.m;
	const XlRect* v = &m->view;
	ChCanvas* c = xlnch.canv;
	XlViewClear();
	if (xlnch.view == XL_VIEW_LIST) {
		XlListPaint(c);
	} else {
		XlRect clip;
		_view_clip(&clip);
		if (xlnch.pinCount == 0)
			XlText(c, XL_GS_MID, "pin apps from all apps", v->x, v->y + 30, XL_INK_SOFT, 255, v);
		for (int i = 0; i < xlnch.pinCount; i++) {
			const XlCard* k = &xlnch.cards[i];
			if (k->app == XL_NONE || (xlnch.dragging && xlnch.press.index == k->app))
				continue;
			XlRect t;
			XlCardRect(k, &t);
			if (t.y >= v->y + v->h || t.y + t.h <= v->y)
				continue; /* off screen: not even touched */
			_paint_card(k, &clip);
		}
		if (xlnch.dragging) {
			XlApp* d = &xlnch.apps[xlnch.press.index];
			if (d->card != XL_NONE) {
				const XlCard* k = &xlnch.cards[d->card];
				XlFillAlpha(c, xlnch.dragX + 6, xlnch.dragY + 8, k->w, k->h, XL_SHADOW, 255, &clip);
				XlCardPaint(c, k, xlnch.dragX, xlnch.dragY, 256, 255, false, &clip);
			}
		}
	}
	_scrollbar();
	XlRect r;
	_view_clip(&r);
	r.w += 10;
	_present(&r);
}

/* repaint one card in place: hover change, live update. Its dirty rect
 * never reaches a neighbour (the gaps are wider than lift + shadow). */
static void _XlPaintCard_impl(void* arg) {
	uint8_t ai = *(uint8_t*)arg;
	XlApp* a = &xlnch.apps[ai];
	if (!a->pinned || a->card == XL_NONE)
		return;
	const XlCard* k = &xlnch.cards[a->card];
	XlRect d, clip;
	XlCardDirtyRect(k, &d);
	_view_clip(&clip);
	if (!XlClipRect(xlnch.canv, &d, &clip))
		return;
	XlFill(xlnch.canv, d.x, d.y, d.w, d.h, XL_BG, NULL);
	_paint_card(k, &d);
	_present(&d);
}

/* ------------------------------------------------------------ lower half */

static int _toggleYPainted = -1;

/*
 * XlLowerLayout -- with the ribbon rolled up there is no reason to keep
 * its band empty while cards sit below the fold (small screens): the
 * view grows down to the last row and the toggle follows it, never past
 * the phul band. Rows keep their heights, so nothing re-flows.
 */
void XlLowerLayout() {
	XlMetrics* m = &xlnch.m;
	m->toggle.y = m->toggleYOpen;
	m->view.h = m->viewHOpen;
	if (!xlnch.flowOpen && xlnch.flowReveal == 0) {
		int content = xlnch.contentH;
		if (xlnch.view == XL_VIEW_LIST)
			content = ((XlListCount() + m->listCols - 1) / m->listCols) * m->listRowH;
		int want = m->view.y + content + (m->compact ? 10 : 14);
		int bottom = m->footerY - m->toggle.h - 8;
		if (want > m->toggleYOpen) {
			m->toggle.y = want < bottom ? want : bottom;
			m->view.h = m->toggle.y - (m->compact ? 10 : 14) - m->view.y;
		}
	}
	int max = xlnch.contentH - m->view.h;
	if (xlnch.scrollCards > max)
		xlnch.scrollCards = max > 0 ? max : 0;
}

static void _lower_impl(void*) {
	const XlMetrics* m = &xlnch.m;
	XlFill(xlnch.canv,
		   m->padX - 30,
		   m->view.y - 4,
		   m->contentW + 60,
		   m->footerY - 2 - (m->view.y - 4),
		   XL_BG,
		   NULL);
	bool saved = _batch;
	_batch = true;
	XlPaintView();
	_batch = saved;
	XlFlowPaintToggle(false);
	XlFlowPaint(false);
}

void XlPaintLower() {
	const XlMetrics* m = &xlnch.m;
	XlLowerLayout();
	_toggleYPainted = m->toggle.y;
	if (_batch) {
		_lower_impl(NULL);
		return;
	}
	XlBuffered(m->padX - 30,
			   m->view.y - 4,
			   m->contentW + 60,
			   m->footerY - 2 - (m->view.y - 4),
			   _lower_impl,
			   NULL);
}

/* ------------------------------------------------------------ buffered entry points */

static void _wrap(const XlRect* r, void (*fn)(void*), void* arg) {
	if (_batch)
		fn(arg);
	else
		XlBuffered(r->x, r->y, r->w, r->h, fn, arg);
}

void XlPaintSearch() {
	_wrap(&xlnch.m.headline, _XlPaintSearch_impl, NULL);
}

void XlPaintClock() {
	_wrap(&xlnch.m.clock, _XlPaintClock_impl, NULL);
}

void XlPaintLink(int i) {
	if (i < 0 || i >= xlnch.linkCount)
		return;
	_wrap(&xlnch.linkRect[i], _XlPaintLink_impl, &i);
}

void XlPaintMast() {
	_wrap(&xlnch.m.mast, _XlPaintMast_impl, NULL);
}

void XlPaintView() {
	if (!_batch && xlnch.m.toggle.y != _toggleYPainted) {
		/* the rows changed and the toggle moves with them */
		XlPaintLower();
		return;
	}
	XlRect r;
	_view_clip(&r);
	r.w += 10;
	_wrap(&r, _XlPaintView_impl, NULL);
}

/* repaint one card in place: hover change, live update. Its dirty rect
 * never reaches a neighbour (the gaps are wider than lift + shadow). */
void XlPaintCard(uint8_t ai) {
	if (xlnch.view != XL_VIEW_CARDS || xlnch.dragging) {
		XlPaintView();
		return;
	}
	XlApp* a = &xlnch.apps[ai];
	if (!a->pinned || a->card == XL_NONE)
		return;
	XlRect d, clip;
	XlCardDirtyRect(&xlnch.cards[a->card], &d);
	_view_clip(&clip);
	if (!XlClipRect(xlnch.canv, &d, &clip))
		return;
	_wrap(&d, _XlPaintCard_impl, &ai);
}

void XlPaintAll(bool withContent) {
	const XlMetrics* m = &xlnch.m;
	ChCanvas* c = xlnch.canv;
	XlFill(c, 0, 0, (int)c->canvasWidth, (int)c->canvasHeight, XL_BG, NULL);
	//XlGamusaPari(c, 14, 8, (int)c->canvasWidth - 28);
	XlGamusaPhulBand(c, m->padX - 20, m->footerY, m->contentW + 40);
	XlFill(c, 0, 0, 1, m->panelH, XL_EDGE, NULL);
	XlFill(c, m->panelW - 1, 0, 1, m->panelH, XL_EDGE, NULL);
	XlFill(c, 0, m->panelH - 1, m->panelW, 1, XL_EDGE, NULL);
	_batch = true;
	XlPaintHeader();
	XlPaintMast();
	XlLowerLayout();
	_toggleYPainted = m->toggle.y;
	if (withContent)
		XlPaintView();
	else
		XlViewClear();
	XlFlowPaintToggle(false);
	XlFlowPaint(false);
	_batch = false;
	XlPresent(0, 0, (int)c->canvasWidth, (int)c->canvasHeight);
}

/* ------------------------------------------------------------ hit test */

static bool _in(const XlRect* r, int x, int y) {
	return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

XlHit XlHitTest(int lx, int ly) {
	XlHit h = {XL_HIT_NONE, 0};
	const XlMetrics* m = &xlnch.m;
	if (ly >= m->mast.y && ly < m->mast.y + m->mast.h) {
		for (int i = 0; i < xlnch.linkCount; i++)
			if (_in(&xlnch.linkRect[i], lx, ly)) {
				h.kind = XL_HIT_LINK;
				h.index = (uint8_t)i;
				return h;
			}
		return h;
	}
	XlRect tg = m->toggle;
	tg.w = 28 + 12 + XlTextWidth(XL_GS_MID, xlnch.flowOpen ? "hide your day" : "your day");
	if (_in(&tg, lx, ly)) {
		h.kind = XL_HIT_TOGGLE;
		return h;
	}
	if (xlnch.flowOpen && ly >= m->flow.y)
		return XlFlowHit(lx, ly);
	if (!_in(&m->view, lx, ly))
		return h;
	if (xlnch.view == XL_VIEW_LIST)
		return XlListHit(lx, ly);
	int ci = XlCardAt(lx, ly);
	if (ci < 0)
		return h;
	const XlCard* k = &xlnch.cards[ci];
	XlRect t, u;
	XlCardRect(k, &t);
	XlCardUnpinRect(k, t.x, t.y, &u);
	h.kind = _in(&u, lx, ly) ? XL_HIT_CARD_UNPIN : XL_HIT_CARD;
	h.index = k->app;
	return h;
}
