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
 * luitflow.cpp -- LuitFlow, the day as a ribbon (named for the Luit, the
 * Brahmaputra, that flows past the gamusa looms).
 *
 *            (jaapi) now
 *   ~~~~~~~~~~~~|~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *    [t] [n] [f] |[ NOW PLAYING  Asilu Kotenu  ]  [c] [s]      red threads
 *   ~~~~<>~~~<>~~|~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~<>~~~<>~~~~~~~~~~~~~~~~
 *   7:10 9:05 10:42                              18:30 tmrw
 *
 * Items: what apps reported through live tiles today (a ring of
 * XL_FLOW_MAX, one item per app, oldest reused), and the next calendar
 * events from /CALEVT (read on show, one 4 KB file). The pointer rests on
 * "now"; drag it and the item under it grows into a card while its
 * neighbours make room; let go and it springs back to now.
 *
 * Hidden until the toggle is pressed; then it unrolls left to right.
 * All geometry is O(items) (<= 16); a frame repaints only the ribbon band.
 */

#include "launcher.h"
#include <livetile.h>
#include <calevt.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FL_SPRING_MS 420
#define FL_REVEAL_MS 450

static CalMonth _cal; /* transient: only while the view is rebuilt */
static const char* _wd[7] = { "sun", "mon", "tue", "wed", "thu", "fri", "sat" };

/* ------------------------------------------------------------ geometry */

static int _yc(int x) {
	const XlMetrics* m = &xlnch.m;
	if (!xlnch.waveY8 || x < 0 || x >= m->panelW)
		return m->flowCY;
	return m->flowCY + (xlnch.waveY8[x] >> 3);
}

void XlFlowInit() {
	const XlMetrics* m = &xlnch.m;
	xlnch.waveY8 = (int16_t*)malloc(sizeof(int16_t) * m->panelW);
	if (xlnch.waveY8) {
		/* a gentle river: 8 px amplitude, under one wave across the panel */
		for (int x = 0; x < m->panelW; x++)
			xlnch.waveY8[x] = (int16_t)(64.0f * sinf((float)x / m->panelW * 6.2831853f * 0.8f + 0.7f));
	}
	xlnch.pointerX = -1;
	xlnch.flowReveal = 0;
}

static XlFlowItem* _item(int v) {
	int k = xlnch.flowView[v];
	return k < XL_FLOW_MAX ? &xlnch.flow[k] : &xlnch.next[k - XL_FLOW_MAX];
}

/* the width a focused card needs for its own text */
static int16_t _fit_width(const XlFlowItem* it) {
	const XlMetrics* m = &xlnch.m;
	int w = XlTextWidth(XL_GS_MID, it->l1) + 60;
	int w2 = XlTextWidth(XL_GS_BODY, it->l2) + 28;
	if (w2 > w)
		w = w2;
	int lo = m->compact ? 132 : 160;
	if (w < lo)
		w = lo;
	if (w > m->flowWFMax)
		w = m->flowWFMax;
	return (int16_t)w;
}

int XlFlowItemApp(int v) {
	if (v < 0 || v >= xlnch.flowCount)
		return -1;
	XlFlowItem* it = _item(v);
	return it->app == XL_NONE ? -1 : it->app;
}

/* slot spacing and the x where "now" sits */
/* slot spacing: as roomy as 112 px, tight enough that every item fits */
static int _spacing() {
	int n = xlnch.flowCount > 1 ? xlnch.flowCount - 1 : 1;
	/* the focused card pushes its neighbours out by half its growth on
	 * each side, so leave that much room at both ends */
	int sp = (xlnch.m.contentW - xlnch.m.flowWFMax - 16) / n;
	if (sp > 112)
		sp = 112;
	if (sp < xlnch.m.flowW0 + 12)
		sp = xlnch.m.flowW0 + 12;
	return sp;
}

/* "now" sits a little right of centre, slid so the first and the last
 * item stay on the ribbon when they can */
static int _now_x() {
	const XlMetrics* m = &xlnch.m;
	int x = m->padX + m->contentW * 52 / 100;
	int sp = _spacing();
	int edge = m->flowWFMax / 2 + 8;
	int lo = m->padX + edge;
	int hi = m->padX + m->contentW - edge;
	int first = x - xlnch.flowNow * sp / 256;
	int last = x + ((xlnch.flowCount - 1) * 256 - xlnch.flowNow) * sp / 256;
	int shift = 0;
	if (xlnch.flowCount > 0) {
		if (last > hi)
			shift = hi - last;
		if (first + shift < lo)
			shift = lo - first; /* too many to fit: keep the start visible */
	}
	return x + shift;
}

/* slot centre of view item v (before neighbours make room) */
static int _slot_x(int v) {
	return _now_x() + ((v * 256 - xlnch.flowNow) * _spacing()) / 256;
}

static int _pointer_x() {
	return xlnch.pointerX >= 0 ? xlnch.pointerX : _now_x();
}

typedef struct {
	int x, y, w, h, f; /* f: focus 0..256 */
} FlRect;

/* lay out every item for a pointer position: O(n) */
static void _layout(int px, FlRect* out) {
	int n = xlnch.flowCount, sp = _spacing();
	int grow[XL_FLOW_VIEW];
	int total = 0;
	for (int i = 0; i < n; i++) {
		int d = px - _slot_x(i);
		if (d < 0)
			d = -d;
		int f = d >= sp ? 0 : 256 - d * 256 / sp;
		f = (f * f * (768 - 2 * f)) >> 16; /* smoothstep */
		out[i].f = f;
		/* each card grows to the width its own text needs */
		grow[i] = (_item(i)->wf - xlnch.m.flowW0) * f / 256;
		total += grow[i];
	}
	int before = 0;
	for (int i = 0; i < n; i++) {
		int after = total - before - grow[i];
		int cx = _slot_x(i) + before / 2 - after / 2;
		int f = out[i].f;
		int w = xlnch.m.flowW0 + grow[i];
		int h = xlnch.m.flowW0 + (xlnch.m.flowHF - xlnch.m.flowW0) * f / 256;
		out[i].w = w;
		out[i].h = h;
		out[i].x = cx - w / 2;
		out[i].y = _yc(cx) - h / 2 - 6 * (256 - f) / 256;
		before += grow[i];
	}
}

/* ------------------------------------------------------------ data */

static uint16_t _minute_now() {
	CalNow n;
	CalEvtNow(&n);
	return (uint16_t)(n.hour * 60 + n.minute);
}

void XlFlowLive(uint8_t app, const XlLive* lv) {
	if (!lv->line1[0] && !lv->line2[0])
		return; /* a badge alone has nothing to show on the ribbon */
	XlFlowItem* slot = NULL;
	XlFlowItem* oldest = &xlnch.flow[0];
	for (int i = 0; i < XL_FLOW_MAX; i++) {
		XlFlowItem* it = &xlnch.flow[i];
		if (it->used && it->app == app) {
			slot = it;
			break;
		}
		if (!it->used) {
			if (!slot)
				slot = it;
		} else if (oldest->used && it->stamp < oldest->stamp) {
			oldest = it;
		}
	}
	if (!slot)
		slot = oldest;
	/* the same line again (a playing song's progress, once a second) is
	 * not news: only new text marks the ribbon unread */
	bool same = slot->used && slot->app == app && !strcmp(slot->l1, lv->line1) &&
				!strcmp(slot->l2, lv->line2);
	slot->used = 1;
	slot->app = app;
	if (!same)
		slot->seen = 0;
	slot->stamp = ++xlnch.flowStamp;
	slot->minute = _minute_now();
	slot->progress = lv->progress;
	slot->kind = (lv->layout == XELNCH_TILE_NOWPLAYING || lv->progress != XELNCH_NO_PROGRESS)
					 ? XL_FLOW_NOW
					 : XL_FLOW_PAST;
	strncpy(slot->l1, lv->line1, XL_LIVE_TEXT_MAX - 1);
	slot->l1[XL_LIVE_TEXT_MAX - 1] = 0;
	strncpy(slot->l2, lv->line2, XL_LIVE_TEXT_MAX - 1);
	slot->l2[XL_LIVE_TEXT_MAX - 1] = 0;
	slot->wf = _fit_width(slot);
}

/*
 * take the upcoming events of one loaded month, from (fromDay, fromMinute)
 * on: O(log n) to find the first, then sequential. daysBase is how many
 * days after today the 1st of this month falls (0 for the current month).
 */
static void _take_events(int fromDay, int fromMinute, int daysBase, const CalNow* now, int calApp) {
	int first = CalEvtLowerBound(&_cal, CalEvtKey(fromDay, fromMinute / 60, fromMinute % 60));
	for (int i = first; i < _cal.count && xlnch.nextCount < XL_FLOW_NEXT; i++) {
		CalEvent* e = &_cal.ev[i];
		int ahead = daysBase + e->day - (daysBase ? 1 : now->day);
		if (ahead > 40)
			break; /* fits the minute field; far enough for a ribbon */
		XlFlowItem* it = &xlnch.next[xlnch.nextCount++];
		memset(it, 0, sizeof(XlFlowItem));
		it->used = 1;
		it->kind = XL_FLOW_NEXT_KIND;
		it->app = (uint8_t)(calApp >= 0 ? calApp : XL_NONE);
		it->seen = 1;
		it->minute = (uint16_t)(e->hour * 60 + e->minute + ahead * 1440);
		it->progress = XELNCH_NO_PROGRESS;
		strncpy(it->l1, e->title, XL_LIVE_TEXT_MAX - 1);
		CalEvtWhen(it->l2, ahead, now->weekday + ahead, e->day, e->hour, e->minute);
		it->wf = _fit_width(it);
	}
}

static int _cmp_minute(const void* a, const void* b) {
	const XlFlowItem* x = &xlnch.flow[*(const uint8_t*)a];
	const XlFlowItem* y = &xlnch.flow[*(const uint8_t*)b];
	return (int)x->minute - (int)y->minute;
}

/* rebuild the view: past (by time) | now | next. O(n log n), n <= 16 */
void XlFlowRefresh() {
	CalNow now;
	CalEvtNow(&now);
	int calApp = XlFindByPathHashFirst(XelnchHash("/calendr.exe"));
	if (calApp >= 0)
		calApp = xlnch.byPath[calApp];
	xlnch.nextCount = 0;
	xlnch.flowWeekday = now.weekday;
	/* the rest of this month, then the start of the next: two reads at most */
	int leftToday = 0;
	if (CalEvtLoad(&_cal, now.year, now.month) == 0) {
		_take_events(now.day, now.hour * 60 + now.minute, 0, &now, calApp);
		leftToday = CalEvtLowerBound(&_cal, CalEvtKey(now.day + 1, 0, 0)) -
					CalEvtLowerBound(&_cal, CalEvtKey(now.day, now.hour, now.minute));
	}
	if (xlnch.nextCount < XL_FLOW_NEXT) {
		int y = now.year, mo = now.month + 1;
		if (mo > 12) {
			mo = 1;
			y++;
		}
		int base = CalEvtDaysIn(now.year, now.month) - now.day + 1;
		if (CalEvtLoad(&_cal, y, mo) == 0)
			_take_events(1, 0, base, &now, calApp);
	}
	/* the calendar card shows the next event, whether or not the calendar
	 * app is running */
	if (calApp >= 0) {
		if (xlnch.nextCount)
			XlLiveSet((uint8_t)calApp, XELNCH_TILE_TEXT, xlnch.next[0].l2, xlnch.next[0].l1, leftToday,
					  XELNCH_NO_PROGRESS);
		else
			XlLiveSet((uint8_t)calApp, XELNCH_TILE_TEXT, NULL, NULL, 0, XELNCH_NO_PROGRESS);
	}

	/* the newest "now" item wins, older ones become history */
	int nowItem = -1;
	for (int i = 0; i < XL_FLOW_MAX; i++)
		if (xlnch.flow[i].used && xlnch.flow[i].kind == XL_FLOW_NOW &&
			(nowItem < 0 || xlnch.flow[i].stamp > xlnch.flow[nowItem].stamp))
			nowItem = i;
	uint8_t past[XL_FLOW_MAX];
	int np = 0;
	for (int i = 0; i < XL_FLOW_MAX; i++)
		if (xlnch.flow[i].used && i != nowItem)
			past[np++] = (uint8_t)i;
	qsort(past, np, 1, _cmp_minute);
	int n = 0;
	for (int i = 0; i < np; i++)
		xlnch.flowView[n++] = past[i];
	if (nowItem >= 0)
		xlnch.flowView[n++] = (uint8_t)nowItem;
	for (int i = 0; i < xlnch.nextCount; i++)
		xlnch.flowView[n++] = (uint8_t)(XL_FLOW_MAX + i);
	xlnch.flowCount = n;
	/* where the pointer rests, in 1/256 slots */
	if (nowItem >= 0)
		xlnch.flowNow = np * 256;
	else
		xlnch.flowNow = np * 256 - 128;
	if (xlnch.flowNow < 0)
		xlnch.flowNow = 0;
}

int XlFlowNewCount() {
	int n = 0;
	for (int i = 0; i < XL_FLOW_MAX; i++)
		if (xlnch.flow[i].used && !xlnch.flow[i].seen)
			n++;
	return n;
}

void XlFlowMarkSeen() {
	for (int i = 0; i < XL_FLOW_MAX; i++)
		xlnch.flow[i].seen = 1;
}

/* ------------------------------------------------------------ painting */

/* "18:30", "tmrw 07:30", "sat 10:00" -- 24 h, like the calendar */
static void _fmt_minute(int m, char* out) {
	int days = m / 1440;
	m %= 1440;
	char* p = out;
	if (days == 1) {
		strcpy(p, "tmrw ");
		p += 5;
	} else if (days > 1) {
		strcpy(p, _wd[(xlnch.flowWeekday + days) % 7]);
		strcat(p, " ");
		p += 4;
	}
	CalEvtFormatTime(p, m / 60, m % 60);
}

/* the time under the pointer, interpolated between the slots around it */
static int _minute_at(int px) {
	int n = xlnch.flowCount;
	if (n == 0)
		return _minute_now();
	if (px <= _slot_x(0))
		return _item(0)->minute;
	for (int i = 0; i + 1 < n; i++) {
		int a = _slot_x(i), b = _slot_x(i + 1);
		if (px <= b)
			return _item(i)->minute + (_item(i + 1)->minute - _item(i)->minute) * (px - a) / (b - a ? b - a : 1);
	}
	return _item(n - 1)->minute;
}

static void _thread(ChCanvas* c, int x, int off8, int thick, uint32_t col, const XlRect* clip) {
	/* off8: y in 1/8 px; antialiased over two pixels */
	int y = off8 >> 3, fr = off8 & 7;
	uint32_t a0 = (8 - fr) * 255 / 8, a1 = fr * 255 / 8;
	XlFillAlpha(c, x, y, 1, 1, col, a0, clip);
	if (thick > 1)
		XlFill(c, x, y + 1, 1, thick - 1, col, clip);
	XlFillAlpha(c, x, y + thick, 1, 1, col, a1, clip);
}

static void _ribbon(ChCanvas* c, int x0, int x1, const XlRect* clip) {
	const XlMetrics* m = &xlnch.m;
	int hw = m->flowHW;
	for (int x = x0; x < x1; x++) {
		int w8 = xlnch.waveY8 && x < m->panelW ? xlnch.waveY8[x] : 0;
		int c8 = m->flowCY * 8 + w8;
		int top = (c8 >> 3) - hw, bot = (c8 >> 3) + hw;
		XlFill(c, x, top, 1, bot - top, XL_LINEN, clip);
		_thread(c, x, c8 - hw * 8, 2, XL_RED, clip);
		_thread(c, x, c8 - hw * 8 + 40, 1, XL_WEAVE_LINE, clip);
		_thread(c, x, c8 + hw * 8 - 16, 2, XL_RED, clip);
		_thread(c, x, c8 + hw * 8 - 48, 1, XL_WEAVE_LINE, clip);
	}
}

/* the rolled cloth at the unrolling edge */
static void _roll(ChCanvas* c, int x, const XlRect* clip) {
	const XlMetrics* m = &xlnch.m;
	int y0 = _yc(x) - m->flowHW - 5, y1 = _yc(x) + m->flowHW + 5;
	XlFill(c, x - 8, y0, 16, y1 - y0, 0xFFE8DDCA, clip);
	XlFill(c, x - 8, y0, 4, y1 - y0, 0xFFF6F0E4, clip);
	XlFill(c, x + 4, y0, 4, y1 - y0, 0xFFD6C8B0, clip);
	XlFill(c, x - 8, y0 + 4, 16, 2, XL_RED, clip);
	XlFill(c, x - 8, y1 - 6, 16, 2, XL_RED, clip);
}

static void _item_paint(ChCanvas* c, int v, const FlRect* r, uint32_t alpha, const XlRect* clip) {
	XlFlowItem* it = _item(v);
	int ai = it->app;
	XlApp* a = ai != XL_NONE ? &xlnch.apps[ai] : NULL;
	int colIdx = a ? (int)a->color : (int)XL_COL_RED_DEEP;
	uint32_t bg = XlTileColor(colIdx);
	uint32_t tc = XlTileTextColor(colIdx);
	XlRect rc = { r->x, r->y, r->w, r->h };
	if (!XlClipRect(c, &rc, clip))
		return;
	XlFillAlpha(c, r->x, r->y, r->w, r->h, bg, alpha, &rc);
	if (colIdx == XL_COL_IVORY)
		XlFrame(c, r->x, r->y, r->w, r->h, 1, XL_EDGE, &rc);
	const XlIcon* ic = a ? &a->small : NULL;
	if (r->f > 154) { /* a card */
		uint32_t ta = alpha * (r->f - 154) / 102;
		if (ta > 255)
			ta = 255;
		bool k = xlnch.m.compact;
		if (ic)
			XlIconDraw(c, ic, r->x + (k ? 8 : 12), r->y + (k ? 8 : 12), ta, &rc);
		char cap[24];
		if (it->kind == XL_FLOW_NOW)
			strcpy(cap, "NOW PLAYING");
		else if (it->kind == XL_FLOW_NEXT_KIND)
			strcpy(cap, "UP NEXT");
		else
			_fmt_minute(it->minute, cap);
		int tx = r->x + (k ? 40 : 46);
		XlText(c, XL_GS_SMALL, cap, tx, r->y + (k ? 16 : 22), tc, ta, &rc);
		char buf[XL_LIVE_TEXT_MAX + 4];
		XlTextFit(XL_GS_MID, it->l1, r->x + r->w - 10 - tx, buf, sizeof(buf));
		XlText(c, XL_GS_MID, buf, tx, r->y + (k ? 33 : 44), tc, ta, &rc);
		XlTextFit(XL_GS_BODY, it->l2, r->w - 26, buf, sizeof(buf));
		XlText(c, XL_GS_BODY, buf, r->x + (k ? 9 : 13), r->y + r->h - (k ? 9 : 14), tc, ta, &rc);
		if (it->kind == XL_FLOW_NOW && it->progress != XELNCH_NO_PROGRESS) {
			int p = it->progress > 1000 ? 1000 : it->progress;
			XlFill(c, r->x, r->y + r->h - 3, r->w, 3, XlBlend(bg, XL_WHITE, 110), &rc);
			XlFill(c, r->x, r->y + r->h - 3, r->w * p / 1000, 3, XL_WHITE, &rc);
		}
	} else if (ic) {
		XlIconDraw(c, ic, r->x + (r->w - ic->w) / 2, r->y + (r->h - ic->h) / 2, alpha, &rc);
	}
}

static void _labels(ChCanvas* c, const FlRect* rs, uint32_t alpha, int revealX, const XlRect* clip) {
	for (int i = 0; i < xlnch.flowCount; i++) {
		const FlRect* r = &rs[i];
		if (r->f > 40 || r->x + r->w > revealX)
			continue;
		/* short labels under resting items: "09:05", "tmrw", "sat" */
		char t[8];
		int mm = _item(i)->minute, days = mm / 1440;
		if (days == 1)
			strcpy(t, "tmrw");
		else if (days > 1)
			strcpy(t, _wd[(xlnch.flowWeekday + days) % 7]);
		else
			CalEvtFormatTime(t, (mm % 1440) / 60, mm % 60);
		int tw = XlTextWidth(XL_GS_SMALL, t);
		XlText(c, XL_GS_SMALL, t, r->x + r->w / 2 - tw / 2, r->y + r->h + 14, XL_INK_SOFT, alpha, clip);
	}
}

static void _pointer(ChCanvas* c, int px, const FlRect* rs, const XlRect* clip) {
	const XlMetrics* m = &xlnch.m;
	int head = _yc(px) - m->flowHW - 18;
	/* the thread runs above and below the items, never across one */
	int y0 = head + 10, y1 = _yc(px) + m->flowHW;
	int gapTop = y1, gapBot = y1;
	for (int i = 0; i < xlnch.flowCount; i++) {
		if (px < rs[i].x - 1 || px > rs[i].x + rs[i].w)
			continue;
		if (rs[i].y < gapTop)
			gapTop = rs[i].y;
		if (rs[i].y + rs[i].h > gapBot || gapBot == y1)
			gapBot = rs[i].y + rs[i].h;
	}
	if (gapTop < y1) {
		XlFill(c, px - 1, y0, 2, gapTop - y0, XL_RED, clip);
		if (gapBot < y1)
			XlFill(c, px - 1, gapBot, 2, y1 - gapBot, XL_RED, clip);
	} else {
		XlFill(c, px - 1, y0, 2, y1 - y0, XL_RED, clip);
	}
	XlJaapi(c, px, head, xlnch.scrubbing ? 13 : 11, clip);
	bool atNow = xlnch.pointerX < 0 || (xlnch.pointerX - _now_x() < 3 && _now_x() - xlnch.pointerX < 3);
	if (atNow) {
		XlText(c, XL_GS_MID, "now", px + 18, head + 6, XL_RED, 255, clip);
		return;
	}
	char t[16];
	_fmt_minute(_minute_at(px), t);
	int tw = XlTextWidth(XL_GS_BODY, t);
	XlFill(c, px + 18, head - 10, tw + 16, 21, XL_RED, clip);
	XlText(c, XL_GS_BODY, t, px + 26, head + 5, XL_WHITE, 255, clip);
}

static void _toggle_impl(void*) {
	const XlMetrics* m = &xlnch.m;
	const XlRect* r = &m->toggle;
	ChCanvas* c = xlnch.canv;
	bool hov = xlnch.hover.kind == XL_HIT_TOGGLE;
	int ps = r->h - 2; /* the plate */
	XlFill(c, r->x, r->y, r->w, r->h, XL_BG, r);
	XlFill(c, r->x, r->y, ps, ps, (xlnch.mouseDown && xlnch.press.kind == XL_HIT_TOGGLE) ? XL_RED_DEEP : XL_RED, r);
	/* folded cloth: three white threads; open: the top one pulled out */
	int step = ps / 4;
	for (int i = 0; i < 3; i++) {
		int w = (xlnch.flowOpen && i == 0) ? ps / 3 : ps / 2;
		XlFill(c, r->x + ps / 4, r->y + step - 1 + i * step, w, 2, XL_WHITE, r);
	}
	const char* lab = xlnch.flowOpen ? "hide your day" : "your day";
	int lx = r->x + ps + 12, base = r->y + ps * 3 / 4;
	int lw = XlText(c, XL_GS_MID, lab, lx, base, XL_INK, 255, r);
	if (hov) {
		XlFill(c, lx, base + 4, lw, 2, XL_RED, r);
		XlFill(c, lx, base + 7, lw * 3 / 5, 1, XL_WEAVE_LINE, r);
	}
	int n = XlFlowNewCount();
	if (!xlnch.flowOpen && n > 0) {
		char s[16];
		int k = 0;
		if (n >= 10)
			s[k++] = (char)('0' + n / 10);
		s[k++] = (char)('0' + n % 10);
		strcpy(s + k, " new");
		XlText(c, XL_GS_SMALL, s, lx + lw + 12, base - 1, XL_RED, 255, r);
	}
}

void XlFlowPaintToggle(bool present) {
	const XlRect* r = &xlnch.m.toggle;
	if (present)
		XlBuffered(r->x, r->y, r->w, r->h, _toggle_impl, NULL);
	else
		_toggle_impl(NULL);
}

static void _flow_impl(void*) {
	const XlMetrics* m = &xlnch.m;
	const XlRect* fr = &m->flow;
	ChCanvas* c = xlnch.canv;
	/* rolled up with the view grown into this band: the band is the view's */
	if (!xlnch.flowOpen && xlnch.flowReveal == 0 && m->toggle.y != m->toggleYOpen)
		return;
	XlFill(c, fr->x, fr->y, fr->w, fr->h, XL_BG, NULL);
	int x0 = m->padX - 8;
	int reveal = xlnch.flowReveal;
	if (reveal <= 0) {
		/* rolled up: the band is simply empty, only the toggle shows */
	} else {
		int x1 = m->padX + 8 + (m->contentW - 16) * reveal / 256;
		_ribbon(c, x0, x1, fr);
		/* a knot on the lower thread for every item that has unrolled */
		for (int i = 0; i < xlnch.flowCount; i++) {
			int sx = _slot_x(i);
			if (sx >= x0 && sx < x1 - 6)
				XlLozenge(c, sx, _yc(sx) + m->flowHW - 1, 3, XL_RED_DEEP, fr);
		}
		FlRect rs[XL_FLOW_VIEW];
		int px = _pointer_x();
		_layout(px, rs);
		/* resting items first, the growing ones on top */
		for (int pass = 0; pass < 2; pass++) {
			for (int i = 0; i < xlnch.flowCount; i++) {
				if ((rs[i].f > 40) != (pass == 1))
					continue;
				int edge = rs[i].x + rs[i].w;
				if (edge > x1 + 40)
					continue;
				uint32_t a = edge <= x1 - 40 ? 255 : (uint32_t)(255 * (x1 + 40 - edge) / 80);
				XlRect clip = { x0, fr->y, x1 - x0, fr->h };
				_item_paint(c, i, &rs[i], a, &clip);
			}
		}
		XlRect clip = { x0, fr->y, x1 - x0, fr->h };
		_labels(c, rs, 255, x1, &clip);
		if (xlnch.flowCount == 0 && reveal >= 256) {
			const char* q = "a quiet day so far";
			int qw = XlTextWidth(XL_GS_MID, q);
			XlText(c, XL_GS_MID, q, m->padX + m->contentW / 2 - qw / 2, m->flowCY + 6, XL_INK_SOFT, 255, fr);
		}
		if (reveal < 256)
			_roll(c, x1, fr);
		else
			_pointer(c, px, rs, fr);
	}
}

/* the whole band goes through the strip buffer: the compositor never
 * shows the cleared band between two frames (that was the flicker) */
void XlFlowPaint(bool present) {
	const XlRect* fr = &xlnch.m.flow;
	/* rolled up and still: there is nothing to draw, and on small screens
	 * the band belongs to the cards (XlLowerLayout) */
	if (!xlnch.flowOpen && xlnch.flowReveal == 0 && !xlnch.revealStart)
		return;
	if (present)
		XlBuffered(fr->x, fr->y, fr->w, fr->h, _flow_impl, NULL);
	else
		_flow_impl(NULL);
}

/* ------------------------------------------------------------ input */

XlHit XlFlowHit(int lx, int ly) {
	XlHit h = { XL_HIT_NONE, 0 };
	const XlMetrics* m = &xlnch.m;
	if (!xlnch.flowOpen || xlnch.flowReveal < 256)
		return h;
	const XlRect* fr = &m->flow;
	if (lx < fr->x || ly < fr->y || lx >= fr->x + fr->w || ly >= fr->y + fr->h)
		return h;
	FlRect rs[XL_FLOW_VIEW];
	_layout(_pointer_x(), rs);
	/* the grown card sits on top: test it first */
	int best = -1;
	for (int i = 0; i < xlnch.flowCount; i++)
		if (lx >= rs[i].x && lx < rs[i].x + rs[i].w && ly >= rs[i].y && ly < rs[i].y + rs[i].h)
			if (best < 0 || rs[i].f > rs[best].f)
				best = i;
	if (best >= 0) {
		h.kind = XL_HIT_FLOW_ITEM;
		h.index = (uint8_t)best;
		return h;
	}
	int yc = _yc(lx);
	if (ly >= yc - m->flowHW - 32 && ly <= yc + m->flowHW + 6 && lx >= m->padX && lx < m->padX + m->contentW)
		h.kind = XL_HIT_FLOW;
	return h;
}

static int _clamp_pointer(int lx) {
	int lo = xlnch.m.padX + 12, hi = xlnch.m.padX + xlnch.m.contentW - 12;
	if (xlnch.flowCount > 0) {
		int a = _slot_x(0) - 20, b = _slot_x(xlnch.flowCount - 1) + 20;
		if (a > lo)
			lo = a;
		if (b < hi)
			hi = b;
	}
	return lx < lo ? lo : (lx > hi ? hi : lx);
}

void XlFlowScrub(int lx) {
	xlnch.scrubbing = true;
	xlnch.springStart = 0;
	int x = _clamp_pointer(lx);
	if (x == xlnch.pointerX)
		return;
	xlnch.pointerX = x;
	XlFlowPaint(true);
}

void XlFlowRelease() {
	xlnch.scrubbing = false;
	if (xlnch.pointerX < 0)
		return;
	xlnch.springFrom = xlnch.pointerX;
	xlnch.springStart = XlNowMs();
	if (!xlnch.springStart)
		xlnch.springStart = 1;
}

void XlFlowSetOpen(bool open, bool animate) {
	if (open == xlnch.flowOpen && !(animate && xlnch.revealStart))
		return;
	xlnch.flowOpen = open;
	if (open) {
		XlFlowRefresh();
		XlFlowMarkSeen();
	}
	xlnch.pointerX = -1;
	xlnch.revealFrom = xlnch.flowReveal;
	xlnch.revealTo = open ? 256 : 0;
	if (animate) {
		xlnch.revealStart = XlNowMs();
		if (!xlnch.revealStart)
			xlnch.revealStart = 1;
	} else {
		xlnch.flowReveal = xlnch.revealTo;
		xlnch.revealStart = 0;
	}
	/* opening takes the band back from the view first; closing gives it
	 * back once the ribbon has rolled up (XlFlowStep) */
	if (open || !animate)
		XlPaintLower();
	else
		XlFlowPaintToggle(true);
}

bool XlFlowAnimating() {
	return xlnch.revealStart != 0 || xlnch.springStart != 0;
}

static int _ease_out(int t) { /* 0..256 */
	int u = 256 - t;
	return 256 - ((u * u >> 8) * u >> 8);
}

static int _ease_back(int t) {
	/* overshoots ~10% then settles: 1 + c3 (t-1)^3 + c1 (t-1)^2 */
	float x = t / 256.0f - 1.0f;
	float v = 1.0f + 2.70158f * x * x * x + 1.70158f * x * x;
	return (int)(v * 256.0f);
}

void XlFlowStep(uint64_t now) {
	bool dirty = false;
	if (xlnch.revealStart) {
		int t = (int)((now - xlnch.revealStart) * 256 / FL_REVEAL_MS);
		if (t >= 256) {
			t = 256;
			xlnch.revealStart = 0;
		}
		int e = _ease_out(t);
		xlnch.flowReveal = xlnch.revealFrom + (xlnch.revealTo - xlnch.revealFrom) * e / 256;
		dirty = true;
		if (!xlnch.revealStart && xlnch.revealTo == 0) {
			XlPaintLower(); /* rolled up: the view may take the band */
			dirty = false;
		}
	}
	if (xlnch.springStart) {
		int t = (int)((now - xlnch.springStart) * 256 / FL_SPRING_MS);
		if (t >= 256) {
			xlnch.springStart = 0;
			xlnch.pointerX = -1;
		} else {
			int e = _ease_back(t);
			xlnch.pointerX = xlnch.springFrom + (_now_x() - xlnch.springFrom) * e / 256;
		}
		dirty = true;
	}
	if (dirty)
		XlFlowPaint(true);
}
