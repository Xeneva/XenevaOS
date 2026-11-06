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

#ifndef __DIRTYTILES_H__
#define __DIRTYTILES_H__

/**
 * Dirty tile map: the screen as a bitmap of 32x32 tiles.
 *  - DirtyTilesMark  O(rows touched x (1 + columns / 64)) -- no merging
 *  - DirtyTilesEmit  bit-scans each row for runs of set tiles and extends a
 *    rectangle from the row above when the run is identical, so a filled
 *    area comes out as one rectangle: O(words + runs); clears the map
 * Compared with merging rectangles pairwise (transitive merge restarts are
 * O(R^2) or worse per insert) the cost no longer depends on how many
 * damaged rectangles arrive in a frame.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "deodxr.h"

#define DIRTYTILES_SHIFT 5 /* 32 px tiles */
#define DIRTYTILES_SIZE	 (1 << DIRTYTILES_SHIFT)

typedef struct _dirty_tiles_ {
	int w, h;		/* screen size in pixels */
	int cols, rows; /* tiles */
	int words;		/* uint64 words per tile row */
	uint64_t* bits;
	bool any;
	/* emission scratch: open rectangles of the previous tile row */
	int* openX0;
	int* openX1;
	int* openRect;
	int* curX0;
	int* curX1;
	int* curRect;
} DirtyTiles;

static inline void DirtyTilesRelease(DirtyTiles* t) {
	free(t->bits);
	free(t->openX0);
	free(t->openX1);
	free(t->openRect);
	free(t->curX0);
	free(t->curX1);
	free(t->curRect);
	memset(t, 0, sizeof(DirtyTiles));
}

/* most rectangles DirtyTilesEmit can produce for this map */
static inline int DirtyTilesMaxRects(const DirtyTiles* t) {
	return t->cols * t->rows;
}

static inline bool DirtyTilesInit(DirtyTiles* t, int w, int h) {
	DirtyTilesRelease(t);
	if (w <= 0 || h <= 0)
		return false;
	t->w = w;
	t->h = h;
	t->cols = (w + DIRTYTILES_SIZE - 1) >> DIRTYTILES_SHIFT;
	t->rows = (h + DIRTYTILES_SIZE - 1) >> DIRTYTILES_SHIFT;
	t->words = (t->cols + 63) >> 6;
	size_t runs = (size_t)t->cols / 2 + 2; /* runs in a row */
	t->bits = (uint64_t*)calloc((size_t)t->words * t->rows, sizeof(uint64_t));
	t->openX0 = (int*)malloc(runs * sizeof(int));
	t->openX1 = (int*)malloc(runs * sizeof(int));
	t->openRect = (int*)malloc(runs * sizeof(int));
	t->curX0 = (int*)malloc(runs * sizeof(int));
	t->curX1 = (int*)malloc(runs * sizeof(int));
	t->curRect = (int*)malloc(runs * sizeof(int));
	if (!t->bits || !t->openX0 || !t->openX1 || !t->openRect || !t->curX0 || !t->curX1 ||
		!t->curRect) {
		DirtyTilesRelease(t);
		return false;
	}
	return true;
}

/* mark every tile the rectangle touches (clipped to the screen) */
static inline void DirtyTilesMark(DirtyTiles* t, int x, int y, int w, int h) {
	if (!t->bits)
		return;
	int x1 = x + w, y1 = y + h;
	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	if (x1 > t->w)
		x1 = t->w;
	if (y1 > t->h)
		y1 = t->h;
	if (x1 <= x || y1 <= y)
		return;
	int c0 = x >> DIRTYTILES_SHIFT, c1 = (x1 - 1) >> DIRTYTILES_SHIFT;
	int r0 = y >> DIRTYTILES_SHIFT, r1 = (y1 - 1) >> DIRTYTILES_SHIFT;
	int w0 = c0 >> 6, w1 = c1 >> 6;
	uint64_t m0 = ~0ULL << (c0 & 63);
	uint64_t m1 = ~0ULL >> (63 - (c1 & 63));
	for (int r = r0; r <= r1; r++) {
		uint64_t* row = t->bits + (size_t)r * t->words;
		if (w0 == w1) {
			row[w0] |= m0 & m1;
		} else {
			row[w0] |= m0;
			for (int k = w0 + 1; k < w1; k++)
				row[k] = ~0ULL;
			row[w1] |= m1;
		}
	}
	t->any = true;
}

static inline int _dirtytiles_ctz(uint64_t v) {
	return __builtin_ctzll(v);
}

/**
 * @brief DirtyTilesEmit -- turn the marked tiles into rectangles (pixel
 * coordinates, clipped to the screen) and clear the map
 * @return number of rectangles written. If `max` is too small, the last
 * entry becomes the bounding rectangle of everything that did not fit, so
 * nothing is ever lost (it may overlap the others)
 */
static inline int DirtyTilesEmit(DirtyTiles* t, Rect* out, int max) {
	if (!t->bits || !t->any || max <= 0)
		return 0;
	int n = 0, open = 0;
	const int exact = max - 1; /* out[max - 1] is kept for the overflow box */
	bool overflow = false;
	int ox0 = 1 << 30, oy0 = 1 << 30, ox1 = -1, oy1 = -1; /* overflow box, tiles */
	for (int r = 0; r < t->rows; r++) {
		uint64_t* row = t->bits + (size_t)r * t->words;
		int cur = 0, o = 0;
		/* runs of set tiles, left to right, by bit scanning */
		int c = 0;
		while (c < t->cols) {
			int wi = c >> 6;
			uint64_t word = row[wi] & (~0ULL << (c & 63));
			while (!word && ++wi < t->words)
				word = row[wi];
			if (!word)
				break;
			int s = (wi << 6) + _dirtytiles_ctz(word);
			if (s >= t->cols)
				break;
			/* end of the run: first clear bit at or after s */
			int ei = s >> 6;
			uint64_t inv = ~row[ei] & (~0ULL << (s & 63));
			while (!inv && ++ei < t->words)
				inv = ~row[ei];
			int e = inv ? (ei << 6) + _dirtytiles_ctz(inv) : t->words << 6;
			if (e > t->cols)
				e = t->cols;
			/* the same run as an open rectangle of the row above: extend it */
			while (o < open && t->openX0[o] < s)
				o++;
			int rect = -1;
			if (o < open && t->openX0[o] == s && t->openX1[o] == e) {
				rect = t->openRect[o];
				out[rect].h = ((r + 1) << DIRTYTILES_SHIFT) - out[rect].y;
			} else if (n < exact || (n == exact && max == 1 && !overflow)) {
				rect = n++;
				out[rect].x = s << DIRTYTILES_SHIFT;
				out[rect].y = r << DIRTYTILES_SHIFT;
				out[rect].w = (e - s) << DIRTYTILES_SHIFT;
				out[rect].h = DIRTYTILES_SIZE;
			} else {
				overflow = true;
				ox0 = s < ox0 ? s : ox0;
				ox1 = e > ox1 ? e : ox1;
				oy0 = r < oy0 ? r : oy0;
				oy1 = r + 1;
			}
			if (rect >= 0) {
				t->curX0[cur] = s;
				t->curX1[cur] = e;
				t->curRect[cur] = rect;
				cur++;
			}
			c = e;
		}
		/* this row's runs are the open rectangles for the next row */
		for (int k = 0; k < cur; k++) {
			t->openX0[k] = t->curX0[k];
			t->openX1[k] = t->curX1[k];
			t->openRect[k] = t->curRect[k];
		}
		open = cur;
		memset(row, 0, (size_t)t->words * sizeof(uint64_t));
	}
	t->any = false;
	if (overflow) {
		if (n == max) { /* max == 1: fold the one rectangle into the box */
			int x0 = out[0].x >> DIRTYTILES_SHIFT, y0 = out[0].y >> DIRTYTILES_SHIFT;
			int x1 = x0 + (out[0].w >> DIRTYTILES_SHIFT), y1 = y0 + (out[0].h >> DIRTYTILES_SHIFT);
			ox0 = x0 < ox0 ? x0 : ox0;
			oy0 = y0 < oy0 ? y0 : oy0;
			ox1 = x1 > ox1 ? x1 : ox1;
			oy1 = y1 > oy1 ? y1 : oy1;
			n = 0;
		}
		out[n].x = ox0 << DIRTYTILES_SHIFT;
		out[n].y = oy0 << DIRTYTILES_SHIFT;
		out[n].w = (ox1 - ox0) << DIRTYTILES_SHIFT;
		out[n].h = (oy1 - oy0) << DIRTYTILES_SHIFT;
		n++;
	}
	for (int i = 0; i < n; i++) { /* the last tile row / column is partial */
		if (out[i].x + out[i].w > t->w)
			out[i].w = t->w - out[i].x;
		if (out[i].y + out[i].h > t->h)
			out[i].h = t->h - out[i].y;
	}
	return n;
}

#endif
