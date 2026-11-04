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
 * Rows are drawn straight to the canvas: only the rows inside the
 * viewport are touched.
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

static int _pin_zone_x() {
	return xlnch.m.view.x + xlnch.m.view.w - (xlnch.m.cell >= 80 ? 112 : 92);
}

void XlListPaint(ChCanvas* c) {
	const XlMetrics* m = &xlnch.m;
	const XlRect* v = &m->view;
	ChFont* f = xlnch.font;
	XlFill(c, v->x, v->y, v->w, v->h, XL_BG, NULL);
	int count = XlListCount();
	if (count == 0) {
		if (f) {
			ChFontSetSize(f, m->textSize);
			ChFontDrawText(c, f, (char*)"no apps match", v->x + 8, v->y + 30, m->textSize, XL_INK_SOFT);
		}
		return;
	}
	int first = xlnch.scrollList / m->listRowH;
	for (int i = first; i < count; i++) {
		int y = v->y + i * m->listRowH - xlnch.scrollList;
		if (y >= v->y + v->h)
			break;
		int ai = XlListAt(i);
		XlApp* a = &xlnch.apps[ai];
		bool hov = (xlnch.hover.kind == XL_HIT_ROW || xlnch.hover.kind == XL_HIT_ROW_PIN) &&
				   xlnch.hover.index == ai;
		if (hov)
			XlFill(c, v->x, y, v->w, m->listRowH, XL_IVORY, v);
		/* colour chip in the app's tile colour, then its icon */
		int chip = m->listIcon + 8;
		int cy = y + (m->listRowH - chip) / 2;
		XlFill(c, v->x + 4, cy, chip, chip, XlTileColor(a->color), v);
		if (a->listIcon.px) {
			XlRect clip = *v;
			XlIconDraw(c->buffer,
					   (int)c->canvasWidth,
					   (int)c->canvasHeight,
					   &a->listIcon,
					   v->x + 4 + (chip - a->listIcon.w) / 2,
					   cy + (chip - a->listIcon.h) / 2,
					   &clip);
		}
		if (f) {
			char buf[XL_TITLE_MAX + 4];
			ChFontSetSize(f, m->textSize);
			int tx = v->x + 4 + chip + 14;
			XlFitText(f, a->title, _pin_zone_x() - tx - 8, buf, sizeof(buf));
			int base = y + m->listRowH / 2 + m->textSize / 2 - 2;
			if (base < v->y + v->h && base - m->textSize > v->y - 4)
				ChFontDrawText(c, f, buf, tx, base, m->textSize, XL_INK);
			if (hov) {
				const char* act = a->pinned ? "unpin" : "pin to start";
				ChFontSetSize(f, m->smallSize + 1);
				int aw = (int)ChFontGetWidth(f, (char*)act);
				uint32_t ac = xlnch.hover.kind == XL_HIT_ROW_PIN ? XL_RED_DEEP : XL_RED;
				if (base < v->y + v->h)
					ChFontDrawText(c, f, (char*)act, v->x + v->w - 10 - aw, base, m->smallSize + 1, ac);
			} else if (a->pinned) {
				XlFill(c, v->x + v->w - 16, y + m->listRowH / 2 - 3, 6, 6, XL_WEAVE_LINE, v);
			}
		}
	}
}

XlHit XlListHit(int vx, int vy) {
	XlHit h = { XL_HIT_NONE, 0 };
	const XlMetrics* m = &xlnch.m;
	const XlRect* v = &m->view;
	if (vx < v->x || vy < v->y || vx >= v->x + v->w || vy >= v->y + v->h)
		return h;
	int i = (vy - v->y + xlnch.scrollList) / m->listRowH;
	int ai = XlListAt(i);
	if (ai < 0)
		return h;
	h.kind = vx >= _pin_zone_x() ? XL_HIT_ROW_PIN : XL_HIT_ROW;
	h.index = (uint8_t)ai;
	return h;
}
