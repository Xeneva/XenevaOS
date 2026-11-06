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
 * anim.cpp -- motion.
 *
 * Blocking, short (they run while the panel is opening, closing or
 * handing over to an app):
 *   enter   cards unroll from their top edge, staggered by row and by x,
 *           content fading up from the paper (~0.45 s)
 *   exit    the same, rolled back up and faster (~0.3 s)
 *   launch  the panel recedes to cotton, the card grows to fill it in the
 *           app's colour with its icon and name (~0.25 s)
 *
 * Stepped from the event loop (never blocking input):
 *   hover   only two cards ever animate: the one the pointer left (fading
 *           out) and the one it is on (fading in); a frame repaints just
 *           those two cards
 *   ticker  a live line that changed slides up and is replaced
 */

#include "launcher.h"
#include <string.h>
#include <sys/_keproc.h>

#define ENTER_MS	   230
#define EXIT_MS		   150
#define ROW_DELAY_MS   80
#define X_DELAY_MS	   150
#define FRAME_MS	   16
#define HOVER_IN_MS	   140
#define HOVER_OUT_MS   110
#define TICK_MS		   320
#define LAUNCH_GROW_MS 230

static int _ease_out(int t) {
	if (t <= 0)
		return 0;
	if (t >= 256)
		return 256;
	int u = 256 - t;
	return 256 - ((u * u >> 8) * u >> 8);
}

static int _ease_in_out(int t) {
	if (t <= 0)
		return 0;
	if (t >= 256)
		return 256;
	if (t < 128)
		return (4 * ((t * t >> 8) * t >> 8)) >> 0;
	int u = 512 - 2 * t;
	return 256 - (((u * u >> 8) * u >> 8) >> 1);
}

static int _delay(const XlCard* k, bool exit) {
	int d = k->row * ROW_DELAY_MS + k->x * X_DELAY_MS / (xlnch.m.view.w ? xlnch.m.view.w : 1);
	if (!exit)
		return d;
	int maxd = (xlnch.rowCount ? xlnch.rowCount - 1 : 0) * ROW_DELAY_MS + X_DELAY_MS;
	return (maxd - d) / 2; /* fold back from the bottom right, quicker */
}

typedef struct {
	int t, dur;
	bool exit;
} AnimFrame;

/* one frame of the unroll / roll-up, painted through the strip buffer */
static void _cards_frame(void* arg) {
	const AnimFrame* f = (const AnimFrame*)arg;
	const XlRect* v = &xlnch.m.view;
	XlRect clip = *v;
	clip.h += 6;
	XlViewClear();
	for (int i = 0; i < xlnch.pinCount; i++) {
		const XlCard* k = &xlnch.cards[i];
		XlRect r;
		XlCardRect(k, &r);
		if (r.y >= v->y + v->h || r.y + r.h <= v->y)
			continue;
		int lt = (f->t - _delay(k, f->exit)) * 256 / f->dur;
		int u = f->exit ? 256 - _ease_out(lt) : _ease_out(lt);
		if (u <= 0)
			continue;
		uint32_t fade = u * 4 / 3 > 255 ? 255 : (uint32_t)(u * 4 / 3);
		XlCardPaint(xlnch.canv, k, r.x, r.y, u, fade, false, &clip);
	}
}

void XlAnimCards(int mode) {
	if (xlnch.view != XL_VIEW_CARDS || xlnch.pinCount == 0) {
		if (mode == XL_ANIM_ENTER)
			XlPaintView();
		return;
	}
	bool exit = mode == XL_ANIM_EXIT;
	int dur = exit ? EXIT_MS : ENTER_MS;
	int last = 0;
	for (int i = 0; i < xlnch.pinCount; i++) {
		int d = _delay(&xlnch.cards[i], exit) + dur;
		if (d > last)
			last = d;
	}
	xlnch.animating = true;
	const XlRect* v = &xlnch.m.view;
	uint64_t start = XlNowMs();
	AnimFrame f;
	f.dur = dur;
	f.exit = exit;
	while (1) {
		int t = (int)(XlNowMs() - start);
		if (t > last)
			t = last;
		f.t = t;
		XlBuffered(v->x, v->y - 4, v->w + 14, v->h + 10, _cards_frame, &f);
		if (t >= last)
			break;
		_KeProcessSleep(FRAME_MS);
	}
	xlnch.animating = false;
}

void XlAnimLaunch(int ai) {
	if (ai < 0 || ai >= xlnch.appCount)
		return;
	XlApp* a = &xlnch.apps[ai];
	ChCanvas* c = xlnch.canv;
	const XlMetrics* m = &xlnch.m;
	xlnch.animating = true;
	/* where it grows from: its card, else the middle of the view */
	XlRect from;
	if (xlnch.view == XL_VIEW_CARDS && a->pinned && a->card != XL_NONE) {
		XlCardRect(&xlnch.cards[a->card], &from);
	} else {
		from.w = 120;
		from.h = 80;
		from.x = m->view.x + (m->view.w - from.w) / 2;
		from.y = m->view.y + (m->view.h - from.h) / 2;
	}
	/* everything else recedes toward cotton, once */
	XlFillAlpha(c, 0, 0, m->panelW, m->panelH, XL_BG, 150, NULL);
	if (xlnch.view == XL_VIEW_CARDS && a->card != XL_NONE)
		XlCardPaint(c, &xlnch.cards[a->card], from.x, from.y, 256, 255, false, NULL);
	XlPresent(0, 0, m->panelW, m->panelH);
	_KeProcessSleep(FRAME_MS * 3);
	uint32_t col = XlTileColor(a->color);
	uint32_t tc = XlTileTextColor(a->color);
	uint64_t start = XlNowMs();
	XlRect prev = from;
	while (1) {
		int t = (int)((XlNowMs() - start) * 256 / LAUNCH_GROW_MS);
		if (t > 256)
			t = 256;
		int g = _ease_in_out(t);
		XlRect r;
		r.x = from.x + (0 - from.x) * g / 256;
		r.y = from.y + (0 - from.y) * g / 256;
		r.w = from.w + (m->panelW - from.w) * g / 256;
		r.h = from.h + (m->panelH - from.h) * g / 256;
		XlFill(c, r.x, r.y, r.w, r.h, col, NULL);
		if (g > 150) {
			const XlIcon* ic = a->big.px ? &a->big : &a->small;
			int cx = r.x + r.w / 2, cy = r.y + r.h / 2 - 20;
			XlIconDraw(c, ic, cx - ic->w / 2, cy - ic->h / 2, (uint32_t)((g - 150) * 255 / 106), &r);
			int nw = XlTextWidth(XL_GS_THIN, a->name);
			XlText(c, XL_GS_THIN, a->name, cx - nw / 2, cy + 70, tc, (uint32_t)((g - 150) * 255 / 106), &r);
			for (int i = 0; i < 7; i++)
				XlLozenge(c, cx - 54 + i * 18, cy + 92, 3, XlBlend(col, tc, 90), &r);
		}
		/* present the union of the old and the new rect only */
		int x0 = r.x < prev.x ? r.x : prev.x, y0 = r.y < prev.y ? r.y : prev.y;
		XlPresent(x0, y0, r.x + r.w - x0, r.y + r.h - y0);
		prev = r;
		if (t >= 256)
			break;
		_KeProcessSleep(FRAME_MS);
	}
	_KeProcessSleep(FRAME_MS * 4);
	xlnch.animating = false;
}

/* ------------------------------------------------------------ hover */

static uint64_t _hoverLast;

static void _settle(uint8_t ai, uint8_t v) {
	if (ai == XL_NONE)
		return;
	XlApp* a = &xlnch.apps[ai];
	if (a->hov != v) {
		a->hov = v;
		XlPaintCard(ai);
	}
}

void XlHoverSet(uint8_t ai) {
	if (ai == xlnch.hoverApp)
		return;
	/* a third card never animates: the one still fading out snaps off */
	if (xlnch.hoverPrevApp != XL_NONE && xlnch.hoverPrevApp != ai)
		_settle(xlnch.hoverPrevApp, 0);
	xlnch.hoverPrevApp = xlnch.hoverApp;
	xlnch.hoverApp = ai;
	if (xlnch.hoverPrevApp == ai)
		xlnch.hoverPrevApp = XL_NONE;
	_hoverLast = XlNowMs();
}

bool XlHoverAnimating() {
	if (xlnch.hoverApp != XL_NONE && xlnch.apps[xlnch.hoverApp].hov < 255)
		return true;
	if (xlnch.hoverPrevApp != XL_NONE && xlnch.apps[xlnch.hoverPrevApp].hov > 0)
		return true;
	return false;
}

void XlHoverStep(uint64_t now) {
	int dt = (int)(now - _hoverLast);
	if (dt <= 0)
		return;
	_hoverLast = now;
	if (xlnch.hoverApp != XL_NONE) {
		XlApp* a = &xlnch.apps[xlnch.hoverApp];
		if (a->hov < 255) {
			int v = a->hov + dt * 255 / HOVER_IN_MS;
			a->hov = (uint8_t)(v > 255 ? 255 : v);
			XlPaintCard(xlnch.hoverApp);
		}
	}
	if (xlnch.hoverPrevApp != XL_NONE) {
		XlApp* a = &xlnch.apps[xlnch.hoverPrevApp];
		if (a->hov > 0) {
			int v = a->hov - dt * 255 / HOVER_OUT_MS;
			a->hov = (uint8_t)(v < 0 ? 0 : v);
			XlPaintCard(xlnch.hoverPrevApp);
		}
		if (a->hov == 0)
			xlnch.hoverPrevApp = XL_NONE;
	}
}

/* ------------------------------------------------------------ ticker */

static uint64_t _tickLast;
static int _ticking;

void XlTickStart(uint8_t ai) {
	XlApp* a = &xlnch.apps[ai];
	a->tick = 0;
	if (!_ticking)
		_tickLast = XlNowMs();
	_ticking++;
}

bool XlTickAnimating() {
	return _ticking > 0;
}

void XlTickStep(uint64_t now) {
	int dt = (int)(now - _tickLast);
	if (dt <= 0)
		return;
	_tickLast = now;
	int step = dt * 255 / TICK_MS;
	if (step < 1)
		step = 1;
	_ticking = 0;
	for (int i = 0; i < xlnch.appCount; i++) {
		XlApp* a = &xlnch.apps[i];
		if (a->tick >= 255)
			continue;
		int v = a->tick + step;
		a->tick = (uint8_t)(v >= 255 ? 255 : v);
		if (a->tick < 255)
			_ticking++;
		if (a->pinned)
			XlPaintCard((uint8_t)i);
	}
}
