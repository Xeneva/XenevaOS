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
 * applist.cpp -- "all apps" list and search.
 *
 * The alphabetical order is built once (O(n log n)). Search re-scores
 * every app per keystroke, O(n * len), and sorts the hits O(k log k).
 * Two columns, reading order left to right. Row lookup is O(1)
 * arithmetic; a hover change repaints only the previous and the new row.
 */

#include "launcher.h"
#include <stdlib.h>
#include <string.h>

static inline char _lower(char c) {
	return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static int _icmp(const char* a, const char* b) {
	while (*a && _lower(*a) == _lower(*b)) {
		a++;
		b++;
	}
	return (unsigned char)_lower(*a) - (unsigned char)_lower(*b);
}

static int _cmp_alpha(const void* a, const void* b) {
	return _icmp(xlnch.apps[*(const uint8_t*)a].title, xlnch.apps[*(const uint8_t*)b].title);
}

void XlListBuild() {
	for (int i = 0; i < xlnch.appCount; i++)
		xlnch.listOrder[i] = (uint8_t)i;
	qsort(xlnch.listOrder, xlnch.appCount, 1, _cmp_alpha);
}

/* 100 exact, 75 prefix, 60 word prefix, 50 substring,
 * 10+n full subsequence, 0 no match. O(len) except substring O(len*q). */
static int _score(const char* title, const char* q, int qlen) {
	char t[XL_TITLE_MAX];
	int tl = 0;
	while (title[tl] && tl < XL_TITLE_MAX - 1) {
		t[tl] = _lower(title[tl]);
		tl++;
	}
	t[tl] = '\0';
	if (qlen <= 0 || qlen > tl)
		return 0;
	if (qlen == tl && memcmp(t, q, qlen) == 0)
		return 100;
	if (memcmp(t, q, qlen) == 0)
		return 75;
	for (int i = 1; i + qlen <= tl; i++)
		if (t[i - 1] == ' ' && memcmp(t + i, q, qlen) == 0)
			return 60;
	for (int i = 1; i + qlen <= tl; i++)
		if (memcmp(t + i, q, qlen) == 0)
			return 50;
	int k = 0;
	for (int i = 0; i < tl && k < qlen; i++)
		if (t[i] == q[k])
			k++;
	return k == qlen ? 10 + k : 0;
}

typedef struct {
	uint8_t app;
	uint8_t score;
} XlMatch;

static int _cmp_match(const void* a, const void* b) {
	const XlMatch* x = (const XlMatch*)a;
	const XlMatch* y = (const XlMatch*)b;
	if (x->score != y->score)
		return (int)y->score - (int)x->score;
	return _icmp(xlnch.apps[x->app].title, xlnch.apps[y->app].title);
}

void XlSearchUpdate() {
	xlnch.resultCount = 0;
	if (xlnch.queryLen == 0)
		return;
	char q[XL_QUERY_MAX];
	for (int i = 0; i < xlnch.queryLen; i++)
		q[i] = _lower(xlnch.query[i]);
	q[xlnch.queryLen] = '\0';
	XlMatch m[XL_MAX_APPS];
	int n = 0;
	for (int i = 0; i < xlnch.appCount; i++) {
		int s = _score(xlnch.apps[i].title, q, xlnch.queryLen);
		if (s > 0) {
			m[n].app = (uint8_t)i;
			m[n].score = (uint8_t)s;
			n++;
		}
	}
	qsort(m, n, sizeof(XlMatch), _cmp_match);
	for (int i = 0; i < n; i++)
		xlnch.results[i] = m[i].app;
	xlnch.resultCount = n;
}

int XlListCount() {
	return xlnch.queryLen ? xlnch.resultCount : xlnch.appCount;
}

int XlListAt(int i) {
	if (i < 0 || i >= XlListCount())
		return -1;
	return xlnch.queryLen ? xlnch.results[i] : xlnch.listOrder[i];
}

static void _row_rect(int i, XlRect* r) {
	const XlMetrics* m = &xlnch.m;
	int col = i % m->listCols, line = i / m->listCols;
	r->x = m->view.x + col * (m->listColW + m->gap);
	r->y = m->view.y + line * m->listRowH - xlnch.scrollList;
	r->w = m->listColW;
	r->h = m->listRowH;
}

static int _pin_zone(const XlRect* r) {
	return r->x + r->w - 74;
}

static void _row_impl(void* arg);

void XlListPaintRow(ChCanvas* c, int i, bool present) {
	if (present) {
		XlRect r;
		_row_rect(i, &r);
		XlRect clip = r;
		if (!XlClipRect(c, &clip, &xlnch.m.view))
			return;
		XlBuffered(clip.x, clip.y, clip.w, clip.h, _row_impl, &i);
		return;
	}
	_row_impl(&i);
}

static void _row_impl(void* arg) {
	int i = *(int*)arg;
	ChCanvas* c = xlnch.canv;
	bool present = false;
	const XlRect* v = &xlnch.m.view;
	int ai = XlListAt(i);
	if (ai < 0)
		return;
	XlRect r;
	_row_rect(i, &r);
	XlRect clip = r;
	if (!XlClipRect(c, &clip, v))
		return;
	XlApp* a = &xlnch.apps[ai];
	/* row hits carry the list position, so this is O(1) */
	bool hov = (xlnch.hover.kind == XL_HIT_ROW || xlnch.hover.kind == XL_HIT_ROW_PIN) && xlnch.hover.index == i;
	XlFill(c, r.x, r.y, r.w, r.h, hov ? XL_HOVER : XL_BG, &clip);
	int chip = 32;
	int cy = r.y + (r.h - chip) / 2;
	XlFill(c, r.x + 6, cy, chip, chip, XlTileColor(a->color), &clip);
	if (a->color == XL_COL_IVORY)
		XlFrame(c, r.x + 6, cy, chip, chip, 1, XL_EDGE, &clip);
	XlIconDraw(c, &a->small, r.x + 6 + (chip - a->small.w) / 2, cy + (chip - a->small.h) / 2, 255, &clip);
	char buf[XL_TITLE_MAX + 4];
	int tx = r.x + 6 + chip + 14;
	XlTextFit(XL_GS_MID, a->title, _pin_zone(&r) - tx - 6, buf, sizeof(buf));
	XlText(c, XL_GS_MID, buf, tx, r.y + r.h / 2 + 6, XL_INK, 255, &clip);
	if (hov) {
		const char* act = a->pinned ? "unpin" : "pin";
		int aw = XlTextWidth(XL_GS_BODY, act);
		uint32_t ac = xlnch.hover.kind == XL_HIT_ROW_PIN ? XL_RED_DEEP : XL_RED;
		int ax = r.x + r.w - 12 - aw;
		XlText(c, XL_GS_BODY, act, ax, r.y + r.h / 2 + 5, ac, 255, &clip);
		if (xlnch.hover.kind == XL_HIT_ROW_PIN)
			XlFill(c, ax, r.y + r.h / 2 + 8, aw, 1, ac, &clip);
	} else if (a->pinned) {
		XlLozenge(c, r.x + r.w - 16, r.y + r.h / 2, 3, XL_WEAVE_LINE, &clip);
	}
	XlFill(c, tx, r.y + r.h - 1, r.w - (tx - r.x), 1, 0xFFF0E8DC, &clip);
	if (present)
		XlPresent(clip.x, clip.y, clip.w, clip.h);
}

void XlListPaint(ChCanvas* c) {
	const XlMetrics* m = &xlnch.m;
	const XlRect* v = &m->view;
	XlFill(c, v->x, v->y, v->w, v->h, XL_BG, NULL);
	int count = XlListCount();
	if (count == 0) {
		XlText(c, XL_GS_MID, "nothing matches", v->x + 6, v->y + 30, XL_INK_SOFT, 255, v);
		return;
	}
	/* only the rows inside the viewport */
	int first = (xlnch.scrollList / m->listRowH) * m->listCols;
	for (int i = first; i < count; i++) {
		XlRect r;
		_row_rect(i, &r);
		if (r.y >= v->y + v->h)
			break;
		XlListPaintRow(c, i, false);
	}
}

/* O(1): the row is pure arithmetic */
XlHit XlListHit(int lx, int ly) {
	XlHit h = { XL_HIT_NONE, 0 };
	const XlMetrics* m = &xlnch.m;
	const XlRect* v = &m->view;
	if (lx < v->x || ly < v->y || lx >= v->x + v->w || ly >= v->y + v->h)
		return h;
	int col = (lx - v->x) / (m->listColW + m->gap);
	if (col >= m->listCols || (lx - v->x) - col * (m->listColW + m->gap) >= m->listColW)
		return h;
	int line = (ly - v->y + xlnch.scrollList) / m->listRowH;
	int i = line * m->listCols + col;
	int ai = XlListAt(i);
	if (ai < 0)
		return h;
	XlRect r;
	_row_rect(i, &r);
	h.kind = lx >= _pin_zone(&r) ? XL_HIT_ROW_PIN : XL_HIT_ROW;
	h.index = (uint8_t)i; /* list position; XlListAt gives the app */
	return h;
}
