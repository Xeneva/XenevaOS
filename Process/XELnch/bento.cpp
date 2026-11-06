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
 * bento.cpp -- AHEntryList: the bento spread of pinned cards.
 *
 * Cards come in two heights. Hero (L) and medium (M) cards share the tall
 * rows, wide (W) and small (S) cards the short rows; each class keeps the
 * user's pin order. Every card has one fixed size on every screen, on a
 * grid (hero = 2 medium, wide = 2 small, plus the gap): a row takes cards
 * while they fit and the next card starts a new row. Only the number of
 * cards per row changes with the resolution.
 *
 *   layout     O(n), only on pin / unpin / reorder / resize
 *   hit test   binary search on the rows, then on the cards of the row
 *   lookups    binary search over hash-sorted indexes
 *   persist    /lnchlay.cnf, the same format the earlier launcher wrote
 */

#include "launcher.h"
#include <stdlib.h>
#include <string.h>
#include <sys/_kefile.h>
#include <livetile.h>

/* ---------------- indexes ---------------- */

static int _cmp_path(const void* a, const void* b) {
	uint32_t ha = xlnch.apps[*(const uint8_t*)a].pathHash;
	uint32_t hb = xlnch.apps[*(const uint8_t*)b].pathHash;
	return ha < hb ? -1 : (ha > hb ? 1 : 0);
}

static int _cmp_title(const void* a, const void* b) {
	uint32_t ha = xlnch.apps[*(const uint8_t*)a].titleHash;
	uint32_t hb = xlnch.apps[*(const uint8_t*)b].titleHash;
	return ha < hb ? -1 : (ha > hb ? 1 : 0);
}

/* O(n log n), once after the config is loaded */
void XlIndexBuild() {
	for (int i = 0; i < xlnch.appCount; i++) {
		xlnch.byPath[i] = (uint8_t)i;
		xlnch.byTitle[i] = (uint8_t)i;
	}
	qsort(xlnch.byPath, xlnch.appCount, 1, _cmp_path);
	qsort(xlnch.byTitle, xlnch.appCount, 1, _cmp_title);
}

/* lower bound of h in idx (sorted by the hash selected with byPath) */
static int _lower(const uint8_t* idx, uint32_t h, bool byPath) {
	int lo = 0, hi = xlnch.appCount;
	while (lo < hi) {
		int mid = (lo + hi) / 2;
		const XlApp* a = &xlnch.apps[idx[mid]];
		uint32_t v = byPath ? a->pathHash : a->titleHash;
		if (v < h)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo;
}

/* O(log n) (+ the equal-hash run, normally 1) */
int XlFindByTitleHash(uint32_t h, const char* title) {
	for (int i = _lower(xlnch.byTitle, h, false); i < xlnch.appCount; i++) {
		XlApp* a = &xlnch.apps[xlnch.byTitle[i]];
		if (a->titleHash != h)
			break;
		if (!title || strcmp(a->title, title) == 0)
			return xlnch.byTitle[i];
	}
	return -1;
}

/* position in byPath of the first app with this path hash, or -1.
 * Several tiles may share one exe (e.g. /ctrl.exe with different
 * params), the caller walks the run. */
int XlFindByPathHashFirst(uint32_t h) {
	int i = _lower(xlnch.byPath, h, true);
	if (i < xlnch.appCount && xlnch.apps[xlnch.byPath[i]].pathHash == h)
		return i;
	return -1;
}

/* ---------------- layout ---------------- */

static bool _is_tall(int size) {
	return size == XL_TILE_L || size == XL_TILE_M;
}

/* the fixed width of a card; wider than the view only on a tiny screen */
static int _card_w(const XlApp* a) {
	static const int base[XL_TILE_SIZE_COUNT] = {
		XL_CARD_S_W,					 /* S */
		XL_CARD_M_W,					 /* M */
		2 * XL_CARD_S_W + XL_CARD_GAP,	 /* W */
		2 * XL_CARD_M_W + XL_CARD_GAP,	 /* L */
	};
	int size = a->size < XL_TILE_SIZE_COUNT ? (int)a->size : (int)XL_TILE_M;
	int w = base[size];
	return w > xlnch.m.view.w ? xlnch.m.view.w : w;
}

/* place cards [from, to) of xlnch.cards as one row at y, left aligned */
static void _justify(int from, int to, int y, int h, bool last) {
	(void)last;
	const XlMetrics* m = &xlnch.m;
	int n = to - from;
	if (n <= 0)
		return;
	int x = 0;
	for (int i = from; i < to; i++) {
		XlCard* k = &xlnch.cards[i];
		k->x = (int16_t)x;
		k->y = (int16_t)y;
		k->h = (int16_t)h;
		k->row = (uint8_t)xlnch.rowCount;
		x += k->w + m->gap;
	}
	XlRow* r = &xlnch.rows[xlnch.rowCount++];
	r->y = (int16_t)y;
	r->h = (int16_t)h;
	r->first = (uint8_t)from;
	r->count = (uint8_t)n;
}

static int _nextCard; /* cards are appended in visual order */

/* lay out one height class; returns the y below its last row */
static int _flow_class(bool tall, int y) {
	const XlMetrics* m = &xlnch.m;
	int h = tall ? m->rowTall : m->rowShort;
	int rowStart = -1, rowW = 0;
	for (int i = 0; i < xlnch.pinCount; i++) {
		uint8_t ai = xlnch.pinOrder[i];
		XlApp* a = &xlnch.apps[ai];
		if (_is_tall(a->size) != tall)
			continue;
		if (xlnch.rowCount >= XL_MAX_ROWS - 1)
			break;
		int w = _card_w(a);
		int ci = _nextCard;
		if (ci >= XL_MAX_APPS)
			break;
		if (rowStart >= 0 && rowW + m->gap + w > m->view.w) {
			_justify(rowStart, ci, y, h, false);
			y += h + m->gap;
			rowStart = -1;
		}
		if (rowStart < 0) {
			rowStart = ci;
			rowW = w;
		} else {
			rowW += m->gap + w;
		}
		XlCard* k = &xlnch.cards[ci];
		k->app = ai;
		k->w = (int16_t)w;
		k->kind = a->size == XL_TILE_L ? XL_CARD_HERO : (tall ? XL_CARD_TALL : XL_CARD_SHORT);
		k->style = (k->kind == XL_CARD_HERO || XlColorIsDark(a->color)) ? XL_STYLE_SOLID : XL_STYLE_PAPER;
		a->card = (uint8_t)ci;
		_nextCard++;
	}
	if (rowStart >= 0) {
		_justify(rowStart, _nextCard, y, h, true);
		y += h + m->gap;
	}
	return y;
}

void XlBentoLayout() {
	for (int i = 0; i < XL_MAX_APPS; i++)
		xlnch.cards[i].app = XL_NONE;
	for (int i = 0; i < xlnch.appCount; i++)
		xlnch.apps[i].card = XL_NONE;
	xlnch.rowCount = 0;
	_nextCard = 0;
	int y = _flow_class(true, 0);
	y = _flow_class(false, y);
	xlnch.contentH = y > 0 ? y - xlnch.m.gap : 0;
	XlLowerLayout(); /* the view may grow or shrink with the rows */
}

void XlCardRect(const XlCard* k, XlRect* r) {
	r->x = xlnch.m.view.x + k->x;
	r->y = xlnch.m.view.y + k->y - xlnch.scrollCards;
	r->w = k->w;
	r->h = k->h;
}

/* O(log rows + log cards-in-row) */
int XlCardAt(int lx, int ly) {
	const XlRect* v = &xlnch.m.view;
	if (lx < v->x || ly < v->y || lx >= v->x + v->w || ly >= v->y + v->h || !xlnch.rowCount)
		return -1;
	int vx = lx - v->x, vy = ly - v->y + xlnch.scrollCards;
	int lo = 0, hi = xlnch.rowCount - 1;
	while (lo < hi) { /* last row with y <= vy */
		int mid = (lo + hi + 1) / 2;
		if (xlnch.rows[mid].y <= vy)
			lo = mid;
		else
			hi = mid - 1;
	}
	const XlRow* r = &xlnch.rows[lo];
	if (vy < r->y || vy >= r->y + r->h)
		return -1;
	int a = r->first, b = r->first + r->count - 1;
	while (a < b) { /* last card with x <= vx */
		int mid = (a + b + 1) / 2;
		if (xlnch.cards[mid].x <= vx)
			a = mid;
		else
			b = mid - 1;
	}
	const XlCard* k = &xlnch.cards[a];
	if (vx < k->x || vx >= k->x + k->w)
		return -1;
	return a;
}

static int _pin_pos(uint8_t ai) {
	for (int i = 0; i < xlnch.pinCount; i++)
		if (xlnch.pinOrder[i] == ai)
			return i;
	return -1;
}

/* move app to where the card toCard's app sits in the pin order */
bool XlCardMove(uint8_t ai, int toCard) {
	if (toCard < 0 || toCard >= XL_MAX_APPS || xlnch.cards[toCard].app == XL_NONE)
		return false;
	uint8_t target = xlnch.cards[toCard].app;
	if (target == ai)
		return false;
	int from = _pin_pos(ai), to = _pin_pos(target);
	if (from < 0 || to < 0)
		return false;
	if (from < to)
		memmove(&xlnch.pinOrder[from], &xlnch.pinOrder[from + 1], to - from);
	else
		memmove(&xlnch.pinOrder[to + 1], &xlnch.pinOrder[to], from - to);
	xlnch.pinOrder[to] = ai;
	XlBentoLayout();
	xlnch.layoutDirty = true;
	return true;
}

bool XlPin(uint8_t ai) {
	XlApp* a = &xlnch.apps[ai];
	if (a->pinned || xlnch.pinCount >= XL_MAX_APPS)
		return false;
	a->pinned = 1;
	xlnch.pinOrder[xlnch.pinCount++] = ai;
	if (!a->big.px)
		XlIconLoad(a->icon, 36, 36, &a->big);
	XlBentoLayout();
	xlnch.layoutDirty = true;
	return true;
}

bool XlUnpin(uint8_t ai) {
	XlApp* a = &xlnch.apps[ai];
	if (!a->pinned)
		return false;
	int from = _pin_pos(ai);
	if (from >= 0) {
		memmove(&xlnch.pinOrder[from], &xlnch.pinOrder[from + 1], xlnch.pinCount - from - 1);
		xlnch.pinCount--;
	}
	a->pinned = 0;
	a->hov = 0;
	XlIconFree(&a->big); /* the card icon goes with the card */
	XlBentoLayout();
	xlnch.layoutDirty = true;
	return true;
}

/* ---------------- persistence ---------------- */

static const char _sizeChar[XL_TILE_SIZE_COUNT] = { 'S', 'M', 'W', 'L' };

static int _size_from_char(char c, int def) {
	for (int i = 0; i < XL_TILE_SIZE_COUNT; i++)
		if (c == _sizeChar[i])
			return i;
	return def;
}

/*
 * XlLayoutLoad -- apply /lnchlay.cnf on top of the config defaults.
 * Line format: <P|U><S|M|W|L>|<title>\n ... "#end\n".
 * Apps not mentioned (new entries in lncher.cnf) keep their defaults and
 * are appended. O(L log n) for L lines.
 */
void XlLayoutLoad() {
	uint8_t seen[XL_MAX_APPS];
	memset(seen, 0, sizeof(seen));
	xlnch.pinCount = 0;

	int fd = _KeOpenFile((char*)XL_LAYOUT_FILE, FILE_OPEN_READ_ONLY);
	if (fd != -1) {
		XEFileStatus st;
		memset(&st, 0, sizeof(st));
		_KeFileStat(fd, &st);
		size_t sz = st.size;
		char* buf = (sz > 0 && sz < 16384) ? (char*)malloc(sz + 1) : NULL;
		if (buf) {
			_KeReadFile(fd, buf, sz);
			buf[sz] = '\0';
			char* p = buf;
			while (*p) {
				char* nl = strchr(p, '\n');
				if (nl)
					*nl = '\0';
				if (p[0] == '#')
					break;
				if ((p[0] == 'P' || p[0] == 'U') && p[1] && p[2] == '|') {
					char* title = p + 3;
					size_t tl = strlen(title);
					if (tl && title[tl - 1] == '\r')
						title[tl - 1] = '\0';
					int ai = XlFindByTitleHash(XelnchHash(title), title);
					if (ai >= 0 && !seen[ai]) {
						XlApp* a = &xlnch.apps[ai];
						seen[ai] = 1;
						a->size = (uint8_t)_size_from_char(p[1], a->size);
						a->pinned = (p[0] == 'P');
						if (a->pinned)
							xlnch.pinOrder[xlnch.pinCount++] = (uint8_t)ai;
					}
				}
				if (!nl)
					break;
				p = nl + 1;
			}
			free(buf);
		}
		_KeCloseFile(fd);
	}

	for (int i = 0; i < xlnch.appCount; i++) {
		if (seen[i])
			continue;
		XlApp* a = &xlnch.apps[i];
		a->pinned = a->defPinned;
		if (a->pinned)
			xlnch.pinOrder[xlnch.pinCount++] = (uint8_t)i;
	}
	XlBentoLayout();
	xlnch.layoutDirty = false;
}

/* XlLayoutSave -- rewrite /lnchlay.cnf in one write. O(n). */
void XlLayoutSave() {
	if (!xlnch.layoutDirty)
		return;
	size_t cap = (size_t)xlnch.appCount * (XL_TITLE_MAX + 5) + 8;
	char* buf = (char*)malloc(cap);
	if (!buf)
		return;
	size_t len = 0;
	/* pinned first, in order, then the unpinned ones */
	for (int pass = 0; pass < 2; pass++) {
		int n = pass == 0 ? xlnch.pinCount : xlnch.appCount;
		for (int i = 0; i < n; i++) {
			int ai = pass == 0 ? xlnch.pinOrder[i] : i;
			XlApp* a = &xlnch.apps[ai];
			if (pass == 1 && a->pinned)
				continue;
			buf[len++] = a->pinned ? 'P' : 'U';
			buf[len++] = _sizeChar[a->size < XL_TILE_SIZE_COUNT ? (int)a->size : (int)XL_TILE_M];
			buf[len++] = '|';
			size_t tl = strlen(a->title);
			XlMemcpy(buf + len, a->title, tl);
			len += tl;
			buf[len++] = '\n';
		}
	}
	XlMemcpy(buf + len, "#end\n", 5);
	len += 5;
	/* remove first: the file system may not truncate on rewrite */
	_KeRemoveFile((char*)XL_LAYOUT_FILE);
	int fd = _KeOpenFile((char*)XL_LAYOUT_FILE, FILE_OPEN_WRITE | FILE_OPEN_CREAT);
	if (fd != -1) {
		_KeWriteFile(fd, buf, len);
		_KeCloseFile(fd);
		xlnch.layoutDirty = false;
	}
	free(buf);
}
