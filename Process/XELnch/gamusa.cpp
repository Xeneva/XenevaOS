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
 * gamusa.cpp -- palette, fill primitives and the woven Gamusa motifs.
 *
 * The motifs are 1-bit bitmaps on a weave grid (1 cell = 1 thread
 * crossing), drawn in very light tints of gamusa red so they read as
 * texture, not decoration. All drawing here is O(pixels touched).
 */

#include "launcher.h"
#include <string.h>

/* phul (flower), 9x9, bit 8 = leftmost cell */
static const uint16_t _phul[9] = { 0x010, 0x038, 0x092, 0x0D6, 0x1FF, 0x0D6, 0x092, 0x038, 0x010 };
/* lozenge (diamond), 7x7, bit 6 = leftmost cell */
static const uint16_t _lozenge[7] = { 0x08, 0x14, 0x22, 0x49, 0x22, 0x14, 0x08 };

static const uint32_t _tile_col[XL_COL_COUNT] = {
	XL_RED, XL_RED_DEEP, XL_MUGA, XL_MUGA_DEEP, XL_CHARCOAL, XL_IVORY
};
static const uint32_t _tile_txt[XL_COL_COUNT] = {
	XL_WHITE, XL_WHITE, XL_INK, XL_WHITE, XL_WHITE, XL_INK
};

/*
 * XlBlend -- src over dst with alpha a255 (0..255), result opaque.
 * Integer only: (x * a + y * (255 - a)) / 255 via the 257 trick.
 */
uint32_t XlBlend(uint32_t dst, uint32_t src, uint32_t a) {
	if (a >= 255)
		return src | 0xFF000000u;
	if (a == 0)
		return dst | 0xFF000000u;
	uint32_t ia = 255 - a;
	uint32_t r = ((src >> 16) & 0xFF) * a + ((dst >> 16) & 0xFF) * ia;
	uint32_t g = ((src >> 8) & 0xFF) * a + ((dst >> 8) & 0xFF) * ia;
	uint32_t b = (src & 0xFF) * a + (dst & 0xFF) * ia;
	r = (r + 1 + (r >> 8)) >> 8;
	g = (g + 1 + (g >> 8)) >> 8;
	b = (b + 1 + (b >> 8)) >> 8;
	return 0xFF000000u | (r << 16) | (g << 8) | b;
}

uint32_t XlTileColor(int idx) {
	if (idx < 0 || idx >= XL_COL_COUNT)
		idx = 0;
	return _tile_col[idx];
}

uint32_t XlTileTextColor(int idx) {
	if (idx < 0 || idx >= XL_COL_COUNT)
		idx = 0;
	return _tile_txt[idx];
}

/* clip (x,y,w,h) to the canvas and optional clip rect; false if empty */
static bool _clip(ChCanvas* c, int* x, int* y, int* w, int* h, const XlRect* clip) {
	int x0 = *x, y0 = *y, x1 = *x + *w, y1 = *y + *h;
	if (x0 < 0)
		x0 = 0;
	if (y0 < 0)
		y0 = 0;
	if (x1 > (int)c->canvasWidth)
		x1 = (int)c->canvasWidth;
	if (y1 > (int)c->canvasHeight)
		y1 = (int)c->canvasHeight;
	if (clip) {
		if (x0 < clip->x)
			x0 = clip->x;
		if (y0 < clip->y)
			y0 = clip->y;
		if (x1 > clip->x + clip->w)
			x1 = clip->x + clip->w;
		if (y1 > clip->y + clip->h)
			y1 = clip->y + clip->h;
	}
	if (x1 <= x0 || y1 <= y0)
		return false;
	*x = x0;
	*y = y0;
	*w = x1 - x0;
	*h = y1 - y0;
	return true;
}

void XlFill(ChCanvas* c, int x, int y, int w, int h, uint32_t col, const XlRect* clip) {
	if (!c || !c->buffer || !_clip(c, &x, &y, &w, &h, clip))
		return;
	uint32_t* row = c->buffer + (size_t)y * c->canvasWidth + x;
	for (int j = 0; j < h; j++, row += c->canvasWidth)
		for (int i = 0; i < w; i++)
			row[i] = col;
}

void XlFrame(ChCanvas* c, int x, int y, int w, int h, int t, uint32_t col, const XlRect* clip) {
	XlFill(c, x, y, w, t, col, clip);
	XlFill(c, x, y + h - t, w, t, col, clip);
	XlFill(c, x, y + t, t, h - 2 * t, col, clip);
	XlFill(c, x + w - t, y + t, t, h - 2 * t, col, clip);
}

void XlDrawMotif(ChCanvas* c, int motif, int x, int y, int s, uint32_t col, const XlRect* clip) {
	const uint16_t* rows = (motif == XL_MOTIF_PHUL) ? _phul : _lozenge;
	int n = (motif == XL_MOTIF_PHUL) ? 9 : 7;
	for (int r = 0; r < n; r++) {
		uint16_t bits = rows[r];
		/* draw runs of set bits as one fill each */
		int cx = 0;
		while (cx < n) {
			if (bits & (1u << (n - 1 - cx))) {
				int start = cx;
				while (cx < n && (bits & (1u << (n - 1 - cx))))
					cx++;
				XlFill(c, x + start * s, y + r * s, (cx - start) * s, s, col, clip);
			} else {
				cx++;
			}
		}
	}
}

/*
 * XlGamusaPari -- the plain-stripe end of a gamusa: three threads of
 * different weight. 11px tall.
 */
void XlGamusaPari(ChCanvas* c, int x, int y, int w) {
	XlFill(c, x, y, w, 2, XL_WEAVE_LINE, NULL);
	XlFill(c, x, y + 4, w, 4, XL_WEAVE, NULL);
	XlFill(c, x, y + 10, w, 1, XL_WEAVE_LINE, NULL);
}

/*
 * XlGamusaPhulBand -- the woven end: alternating phul and lozenge on a
 * 2px weave grid between two threads. 26px tall, pattern centred in w.
 */
void XlGamusaPhulBand(ChCanvas* c, int x, int y, int w) {
	const int s = 2;
	const int unit = 21 * s; /* phul(9) + 2 + lozenge(7) + 3 */
	XlFill(c, x, y, w, 1, XL_WEAVE_LINE, NULL);
	XlFill(c, x, y + 25, w, 1, XL_WEAVE_LINE, NULL);
	int count = w / unit;
	if (count <= 0)
		return;
	int ox = x + (w - count * unit) / 2;
	XlRect clip = { x, y, w, 26 };
	for (int i = 0; i < count; i++) {
		int px = ox + i * unit;
		XlDrawMotif(c, XL_MOTIF_PHUL, px, y + 4, s, XL_WEAVE, &clip);
		XlDrawMotif(c, XL_MOTIF_LOZENGE, px + 11 * s, y + 6, s, XL_WEAVE, &clip);
	}
}

/*
 * XlGamusaBorders -- the red borders along the two long sides, as two
 * fine threads each, inset from the panel edge.
 */
void XlGamusaBorders(ChCanvas* c, int h, int w) {
	XlFill(c, 5, 0, 2, h, XL_WEAVE_LINE, NULL);
	XlFill(c, 9, 0, 1, h, XL_WEAVE, NULL);
	XlFill(c, w - 7, 0, 2, h, XL_WEAVE_LINE, NULL);
	XlFill(c, w - 10, 0, 1, h, XL_WEAVE, NULL);
}
