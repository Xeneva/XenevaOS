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
 * layout.cpp -- Kinetic grid layout.
 *
 *  - packing: first fit on a 6-column occupancy map, in pin order.
 *    A "first free row" cursor makes it O(n) amortised; it only runs on
 *    pin / unpin / drag-reorder, never per frame.
 *  - hit testing: O(1), cell -> app index through the occupancy map.
 *  - lookups by hash: O(log n) binary search over sorted index arrays.
 *  - persistence: /lnchlay.cnf, one line per app, written on change.
 */

#include "launcher.h"
#include <stdlib.h>
#include <string.h>
#include <sys/_kefile.h>
#include <livetile.h>

static uint8_t _slotOf[XL_MAX_APPS]; /* app index -> position in pinOrder */

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

/* ---------------- packing ---------------- */

static bool _fits(int r, int c, int cw, int ch) {
	if (c + cw > XL_COLS || r + ch > XL_MAX_ROWS)
		return false;
	for (int y = r; y < r + ch; y++)
		for (int x = c; x < c + cw; x++)
			if (xlnch.occ[y][x] != XL_NONE)
				return false;
	return true;
}

static bool _row_full(int r) {
	for (int x = 0; x < XL_COLS; x++)
		if (xlnch.occ[r][x] == XL_NONE)
			return false;
	return true;
}

void XlLayoutPack() {
	memset(xlnch.occ, XL_NONE, sizeof(xlnch.occ));
	xlnch.rowsUsed = 0;
	int firstFree = 0;
	for (int i = 0; i < xlnch.pinCount; i++) {
		uint8_t ai = xlnch.pinOrder[i];
		XlApp* a = &xlnch.apps[ai];
		_slotOf[ai] = (uint8_t)i;
		int cw, ch;
		XlTileCells(a->size, &cw, &ch);
		bool placed = false;
		for (int r = firstFree; r < XL_MAX_ROWS && !placed; r++) {
			for (int c = 0; c + cw <= XL_COLS; c++) {
				if (_fits(r, c, cw, ch)) {
					for (int y = r; y < r + ch; y++)
						for (int x = c; x < c + cw; x++)
							xlnch.occ[y][x] = ai;
					a->col = (int16_t)c;
					a->row = (int16_t)r;
					if (r + ch > xlnch.rowsUsed)
						xlnch.rowsUsed = r + ch;
					placed = true;
					break;
				}
			}
		}
		if (!placed) {
			a->col = -1;
			a->row = -1;
		}
		while (firstFree < XL_MAX_ROWS && _row_full(firstFree))
			firstFree++;
	}
}

void XlLayoutTileRect(XlApp* a, XlRect* r) {
	const XlMetrics* m = &xlnch.m;
	int pw, ph;
	XlTilePixels(a->size, &pw, &ph);
	r->x = m->view.x + a->col * m->pitch;
	r->y = m->view.y + a->row * m->pitch - xlnch.scrollTiles;
	r->w = pw;
	r->h = ph;
}

int XlLayoutContentHeight() {
	if (xlnch.rowsUsed == 0)
		return 0;
	return xlnch.rowsUsed * xlnch.m.pitch - xlnch.m.gap;
}

/* cell under a canvas point, false if outside the grid */
static bool _cell_at(int vx, int vy, int* col, int* row) {
	const XlMetrics* m = &xlnch.m;
	if (vx < m->view.x || vy < m->view.y || vx >= m->view.x + m->view.w ||
		vy >= m->view.y + m->view.h)
		return false;
	int gx = vx - m->view.x;
	int gy = vy - m->view.y + xlnch.scrollTiles;
	int c = gx / m->pitch, r = gy / m->pitch;
	if (c >= XL_COLS || r >= XL_MAX_ROWS)
		return false;
	*col = c;
	*row = r;
	return true;
}

/* O(1): app index under the point, -1 if none (gaps between tiles miss) */
int XlLayoutTileAt(int vx, int vy) {
	int c, r;
	if (!_cell_at(vx, vy, &c, &r))
		return -1;
	uint8_t ai = xlnch.occ[r][c];
	if (ai == XL_NONE)
		return -1;
	XlRect t;
	XlLayoutTileRect(&xlnch.apps[ai], &t);
	if (vx < t.x || vy < t.y || vx >= t.x + t.w || vy >= t.y + t.h)
		return -1;
	return ai;
}

/*
 * XlLayoutSlotAt -- the pin-order slot a dragged tile should take when
 * its centre is at (vx,vy): the slot of the tile under it, or for an
 * empty cell the slot after the last tile placed before that cell in
 * reading order. O(cells scanned) <= O(6 * rows), only while dragging
 * across cell boundaries.
 */
int XlLayoutSlotAt(int vx, int vy) {
	int c, r;
	if (!_cell_at(vx, vy, &c, &r))
		return -1;
	if (r >= xlnch.rowsUsed)
		return xlnch.pinCount - 1;
	uint8_t ai = xlnch.occ[r][c];
	if (ai != XL_NONE)
		return _slotOf[ai];
	for (int cell = r * XL_COLS + c - 1; cell >= 0; cell--) {
		uint8_t p = xlnch.occ[cell / XL_COLS][cell % XL_COLS];
		if (p != XL_NONE)
			return _slotOf[p] + 1 < xlnch.pinCount ? _slotOf[p] + 1 : xlnch.pinCount - 1;
	}
	return 0;
}

/*
 * XlLayoutMove -- move app to pin-order slot (Metro reflow: the tiles in
 * between shift by one). O(n) memmove + O(n) repack.
 */
bool XlLayoutMove(uint8_t ai, int slot) {
	if (!xlnch.apps[ai].pinned || slot < 0)
		return false;
	int from = _slotOf[ai];
	if (slot >= xlnch.pinCount)
		slot = xlnch.pinCount - 1;
	if (slot == from)
		return false;
	if (from < slot)
		memmove(&xlnch.pinOrder[from], &xlnch.pinOrder[from + 1], slot - from);
	else
		memmove(&xlnch.pinOrder[slot + 1], &xlnch.pinOrder[slot], from - slot);
	xlnch.pinOrder[slot] = ai;
	XlLayoutPack();
	xlnch.layoutDirty = true;
	return true;
}

bool XlPin(uint8_t ai) {
	XlApp* a = &xlnch.apps[ai];
	if (a->pinned || xlnch.pinCount >= XL_MAX_APPS)
		return false;
	a->pinned = 1;
	xlnch.pinOrder[xlnch.pinCount++] = ai;
	XlTileEnsure(a);
	XlLayoutPack();
	xlnch.layoutDirty = true;
	return true;
}

bool XlUnpin(uint8_t ai) {
	XlApp* a = &xlnch.apps[ai];
	if (!a->pinned)
		return false;
	int from = _slotOf[ai];
	memmove(&xlnch.pinOrder[from], &xlnch.pinOrder[from + 1], xlnch.pinCount - from - 1);
	xlnch.pinCount--;
	a->pinned = 0;
	XlTileRelease(a); /* give the cache + tile icon memory back */
	XlLayoutPack();
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
	XlLayoutPack();
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
