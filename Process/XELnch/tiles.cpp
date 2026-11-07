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
 * tiles.cpp -- Kinetic tiles.
 *
 * Every pinned tile owns one opaque RGB565 bitmap (its "cache"). It is
 * re-rendered only when its content changes (live update, colour, size),
 * so hover, drag, scroll and the turnstile animation are pure blits.
 * Rendering goes through a transient ARGB scratch (text/icons need 32 bit)
 * that is freed right after, so the resident cost is
 * sum(pinned tile area) * 2 bytes, released on unpin.
 */

#include "launcher.h"
#include <livetile.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static inline uint16_t _to565(uint32_t c) {
	return (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
}

static inline uint32_t _from565(uint16_t p) {
	uint32_t r = p >> 11, g = (p >> 5) & 63, b = p & 31;
	return 0xFF000000u | (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) |
		   ((b << 3) | (b >> 2));
}

/* sin(0..90 deg) in Q14 */
static const int16_t _sinq14[91] = {
	0,	   286,	  572,	 857,	1143,  1428,  1713,	 1997,	2280,  2563,  2845,	 3126,	3406,  3686,
	3964,  4240,  4516,	 4790,	5063,  5334,  5604,	 5872,	6138,  6402,  6664,	 6924,	7182,  7438,
	7692,  7943,  8192,	 8438,	8682,  8923,  9162,	 9397,	9630,  9860,  10087, 10311, 10531, 10749,
	10963, 11174, 11381, 11585, 11786, 11982, 12176, 12365, 12551, 12733, 12911, 13085, 13255, 13421,
	13583, 13741, 13894, 14044, 14189, 14330, 14466, 14598, 14726, 14849, 14968, 15082, 15191, 15296,
	15396, 15491, 15582, 15668, 15749, 15826, 15897, 15964, 16026, 16083, 16135, 16182, 16225, 16262,
	16294, 16322, 16344, 16362, 16374, 16382, 16384
};

/* sin of an angle given in tenths of a degree (0..900), linear interp */
static float _sin10(int a10) {
	if (a10 <= 0)
		return 0.0f;
	if (a10 >= 900)
		return 1.0f;
	int d = a10 / 10, f = a10 % 10;
	int v = _sinq14[d] + ((_sinq14[d + 1] - _sinq14[d]) * f) / 10;
	return (float)v / 16384.0f;
}

void XlTileCells(int size, int* cw, int* ch) {
	switch (size) {
	case XL_TILE_S:
		*cw = 1;
		*ch = 1;
		break;
	case XL_TILE_W:
		*cw = 2;
		*ch = 1;
		break;
	case XL_TILE_L:
		*cw = 4;
		*ch = 2;
		break;
	default:
		*cw = 2;
		*ch = 2;
		break;
	}
}

void XlTilePixels(int size, int* pw, int* ph) {
	int cw, ch;
	XlTileCells(size, &cw, &ch);
	const XlMetrics* m = &xlnch.m;
	*pw = cw * m->cell + (cw - 1) * m->gap;
	*ph = ch * m->cell + (ch - 1) * m->gap;
}

/* the square the icon is scaled to fit in, per tile size */
int XlTileIconBox(int size) {
	int cell = xlnch.m.cell;
	switch (size) {
	case XL_TILE_S:
		return cell * 48 / 100;
	case XL_TILE_W:
		return cell * 56 / 100;
	default:
		return cell * 76 / 100;
	}
}

/*
 * XlTileEnsure -- make sure a pinned tile has its icon and cache at the
 * current size. Cheap when nothing changed.
 */
bool XlTileEnsure(XlApp* a) {
	int pw, ph;
	XlTilePixels(a->size, &pw, &ph);
	if (a->cache && (a->cacheW != pw || a->cacheH != ph)) {
		free(a->cache);
		a->cache = NULL;
		XlIconFree(&a->tileIcon); /* icon box depends on size */
	}
	if (!a->tileIcon.px) {
		int box = XlTileIconBox(a->size);
		XlIconLoad(a->icon, box, box, &a->tileIcon);
	}
	if (!a->cache) {
		a->cache = (uint16_t*)malloc((size_t)pw * ph * 2);
		if (!a->cache)
			return false;
		a->cacheW = (uint16_t)pw;
		a->cacheH = (uint16_t)ph;
		a->cacheDirty = 1;
	}
	return true;
}

void XlTileRelease(XlApp* a) {
	if (a->cache) {
		free(a->cache);
		a->cache = NULL;
	}
	a->cacheW = a->cacheH = 0;
	XlIconFree(&a->tileIcon);
}

/*
 * XlFitText -- copy src into dst, cutting it with ".." so that it fits
 * maxW pixels at the font's current size. Binary search over the cut
 * length: O(len log len) glyph measurements, only on re-render.
 */
void XlFitText(ChFont* f, const char* src, int maxW, char* dst, int dstSize) {
	int len = (int)strlen(src);
	if (len >= dstSize)
		len = dstSize - 1;
	XlMemcpy(dst, src, len);
	dst[len] = '\0';
	if (!f || ChFontGetWidth(f, dst) <= maxW)
		return;
	int lo = 0, hi = len;
	char tmp[XL_TITLE_MAX + 4];
	while (lo < hi) {
		int mid = (lo + hi + 1) / 2;
		int n = mid < XL_TITLE_MAX ? mid : XL_TITLE_MAX;
		XlMemcpy(tmp, src, n);
		tmp[n] = '.';
		tmp[n + 1] = '.';
		tmp[n + 2] = '\0';
		if (ChFontGetWidth(f, tmp) <= maxW)
			lo = mid;
		else
			hi = mid - 1;
	}
	int n = lo < dstSize - 3 ? lo : dstSize - 3;
	if (n < 0)
		n = 0;
	XlMemcpy(dst, src, n);
	dst[n] = '.';
	dst[n + 1] = '.';
	dst[n + 2] = '\0';
}

static void _text(ChCanvas* cv, const char* s, int x, int baseline, int size, int maxW, uint32_t col) {
	ChFont* f = xlnch.font;
	if (!f || !s || !s[0] || maxW <= 0)
		return;
	char buf[XL_LIVE_TEXT_MAX + 4];
	ChFontSetSize(f, size);
	XlFitText(f, s, maxW, buf, sizeof(buf));
	ChFontDrawText(cv, f, buf, x, baseline, size, col);
}

/*
 * XlTileRender -- paint tile content into its cache.
 * Layout follows the Kinetic mockup: icon centred, lowercase-free label
 * bottom-left; live tiles move the icon to the top-left and stack their
 * text above the label (M/L) or beside the icon (W).
 */
void XlTileRender(XlApp* a) {
	if (!a->cache)
		return;
	const XlMetrics* m = &xlnch.m;
	int w = a->cacheW, h = a->cacheH;
	uint32_t col = XlTileColor(a->color);
	uint32_t txt = XlTileTextColor(a->color);
	uint32_t* p = (uint32_t*)malloc((size_t)w * h * 4); /* transient */
	if (!p)
		return;
	for (int i = 0; i < w * h; i++)
		p[i] = col;

	ChCanvas cv;
	memset(&cv, 0, sizeof(ChCanvas));
	cv.buffer = p;
	cv.canvasWidth = w;
	cv.canvasHeight = h;
	cv.screenWidth = w;
	cv.screenHeight = h;
	cv.bpp = 32;
	cv.pitch = w * 4;
	if (a->color == XL_COL_IVORY)
		XlFrame(&cv, 0, 0, w, h, 1, XL_IVORY_EDGE, NULL);

	const int pad = m->cell >= 80 ? 10 : 8;
	const XlLive* lv = &a->live;
	bool liveText = lv->valid && lv->layout != XELNCH_TILE_BADGE && a->size != XL_TILE_S;
	const XlIcon* ico = &a->tileIcon;
	int iw = ico->w, ih = ico->h;

	int ix, iy;
	if (!liveText) {
		ix = (w - iw) / 2;
		iy = (h - ih) / 2;
		if (a->size != XL_TILE_S)
			iy -= (a->size == XL_TILE_W) ? 6 : 8;
	} else {
		ix = pad;
		iy = pad;
	}
	XlIconDraw(p, w, h, ico, ix, iy, NULL);

	int labelBase = h - pad + 1;
	if (a->size != XL_TILE_S) {
		int lx = pad;
		if (liveText && a->size == XL_TILE_W)
			lx = pad + iw + 10;
		_text(&cv, a->title, lx, labelBase, m->labelSize, w - lx - pad, txt);
	}

	/* keep live text clear of the badge in the top right corner */
	int badgeW = 0;
	char num[8];
	num[0] = '\0';
	if (lv->valid && lv->badge) {
		if (lv->badge > 99)
			strcpy(num, "99+");
		else
			snprintf(num, sizeof(num), "%u", (unsigned)lv->badge);
		if (xlnch.font) {
			ChFontSetSize(xlnch.font, m->textSize);
			badgeW = (int)ChFontGetWidth(xlnch.font, num) + 8;
		}
	}

	if (liveText) {
		if (a->size == XL_TILE_W) {
			int tx = pad + iw + 10;
			int b1 = pad + m->textSize;
			_text(&cv, lv->line1, tx, b1, m->textSize, w - tx - pad - badgeW, txt);
			_text(&cv, lv->line2, tx, b1 + m->smallSize + 5, m->smallSize, w - tx - pad, txt);
		} else {
			int b2 = labelBase - (m->labelSize + 8);
			int b1 = b2 - (m->smallSize + 6);
			_text(&cv, lv->line2, pad, b2, m->smallSize, w - 2 * pad, txt);
			_text(&cv, lv->line1, pad, b1, m->textSize, w - 2 * pad, txt);
			if (lv->layout == XELNCH_TILE_NOWPLAYING)
				_text(&cv, "NOW PLAYING", pad, b1 - (m->textSize + 4), m->smallSize - 1, w - 2 * pad, txt);
		}
	}

	if (badgeW)
		_text(&cv, num, w - pad - (badgeW - 8), pad + m->textSize, m->textSize, badgeW, txt);

	if (lv->valid && lv->progress != XELNCH_NO_PROGRESS) {
		int prog = lv->progress > 1000 ? 1000 : lv->progress;
		XlFill(&cv, 0, h - 3, w, 3, XlBlend(col, txt, 60), NULL);
		XlFill(&cv, 0, h - 3, w * prog / 1000, 3, XlBlend(col, txt, 200), NULL);
	}
	for (int i = 0; i < w * h; i++)
		a->cache[i] = _to565(p[i]);
	free(p);
	a->cacheDirty = 0;
}

/* XlTileBlit -- straight copy of the cache, clipped. */
void XlTileBlit(ChCanvas* c, XlApp* a, int x, int y, const XlRect* clip) {
	if (!a->cache)
		return;
	if (a->cacheDirty)
		XlTileRender(a);
	int sw = a->cacheW, sh = a->cacheH;
	int x0 = x, y0 = y, x1 = x + sw, y1 = y + sh;
	int cx0 = 0, cy0 = 0, cx1 = (int)c->canvasWidth, cy1 = (int)c->canvasHeight;
	if (clip) {
		if (clip->x > cx0)
			cx0 = clip->x;
		if (clip->y > cy0)
			cy0 = clip->y;
		if (clip->x + clip->w < cx1)
			cx1 = clip->x + clip->w;
		if (clip->y + clip->h < cy1)
			cy1 = clip->y + clip->h;
	}
	if (x0 < cx0)
		x0 = cx0;
	if (y0 < cy0)
		y0 = cy0;
	if (x1 > cx1)
		x1 = cx1;
	if (y1 > cy1)
		y1 = cy1;
	if (x1 <= x0 || y1 <= y0)
		return;
	int n = x1 - x0;
	for (int j = y0; j < y1; j++) {
		uint32_t* d = c->buffer + (size_t)j * c->canvasWidth + x0;
		const uint16_t* sp = a->cache + (size_t)(j - y) * sw + (x0 - x);
		for (int i = 0; i < n; i++)
			d[i] = _from565(sp[i]);
	}
}

/* XlTileBlitScaled -- nearest-neighbour scale around the tile centre
 * (press feedback 96%, drag lift 104%). */
void XlTileBlitScaled(ChCanvas* c, XlApp* a, int x, int y, int pct, const XlRect* clip) {
	if (!a->cache)
		return;
	if (pct == 100) {
		XlTileBlit(c, a, x, y, clip);
		return;
	}
	if (a->cacheDirty)
		XlTileRender(a);
	int sw = a->cacheW, sh = a->cacheH;
	int dw = sw * pct / 100, dh = sh * pct / 100;
	if (dw < 1 || dh < 1)
		return;
	int dx = x + (sw - dw) / 2, dy = y + (sh - dh) / 2;
	int cx0 = 0, cy0 = 0, cx1 = (int)c->canvasWidth, cy1 = (int)c->canvasHeight;
	if (clip) {
		if (clip->x > cx0)
			cx0 = clip->x;
		if (clip->y > cy0)
			cy0 = clip->y;
		if (clip->x + clip->w < cx1)
			cx1 = clip->x + clip->w;
		if (clip->y + clip->h < cy1)
			cy1 = clip->y + clip->h;
	}
	uint32_t xstep = ((uint32_t)sw << 16) / dw, ystep = ((uint32_t)sh << 16) / dh;
	uint32_t v = 0;
	for (int j = 0; j < dh; j++, v += ystep) {
		int py = dy + j;
		if (py < cy0)
			continue;
		if (py >= cy1)
			break;
		const uint16_t* srow = a->cache + (size_t)(v >> 16) * sw;
		uint32_t* drow = c->buffer + (size_t)py * c->canvasWidth;
		uint32_t u = 0;
		for (int i = 0; i < dw; i++, u += xstep) {
			int px = dx + i;
			if (px >= cx0 && px < cx1)
				drow[px] = _from565(srow[u >> 16]);
		}
	}
}

/*
 * XlTileBlitTurn -- Metro turnstile: the tile rotates about a vertical
 * axis at pivotX (the grid's left edge), in perspective. Columns are
 * inverse-mapped, so every destination pixel is written once:
 * O(projected tile area) per frame.
 * @param angle10 -- rotation in tenths of a degree, 0 = flat, 900 = edge on
 * @param fadeTo -- colour the tile dims toward as it turns away
 */
void XlTileBlitTurn(ChCanvas* c,
					XlApp* a,
					int x,
					int y,
					int pivotX,
					int angle10,
					uint32_t fadeTo,
					const XlRect* clip) {
	if (!a->cache || angle10 >= 900)
		return;
	if (angle10 <= 0) {
		XlTileBlit(c, a, x, y, clip);
		return;
	}
	if (a->cacheDirty)
		XlTileRender(a);
	const float F = 1600.0f; /* focal length in px */
	float s = _sin10(angle10);
	float co = _sin10(900 - angle10);
	int sw = a->cacheW, sh = a->cacheH;
	float X0 = (float)(x - pivotX);
	if (X0 < 0.0f)
		X0 = 0.0f;
	float X1 = X0 + (float)sw;
	float ox0 = X0 * co * F / (F + X0 * s);
	float ox1 = X1 * co * F / (F + X1 * s);
	int ix0 = (int)ox0;
	if ((float)ix0 < ox0)
		ix0++;
	int ix1 = (int)ox1;

	int cx0 = 0, cy0 = 0, cx1 = (int)c->canvasWidth, cy1 = (int)c->canvasHeight;
	if (clip) {
		if (clip->x > cx0)
			cx0 = clip->x;
		if (clip->y > cy0)
			cy0 = clip->y;
		if (clip->x + clip->w < cx1)
			cx1 = clip->x + clip->w;
		if (clip->y + clip->h < cy1)
			cy1 = clip->y + clip->h;
	}
	uint32_t fa = (uint32_t)(angle10 * 150 / 900); /* dim up to ~60% */
	for (int ox = ix0; ox < ix1; ox++) {
		int px = pivotX + ox;
		if (px < cx0)
			continue;
		if (px >= cx1)
			break;
		float den = co * F - (float)ox * s;
		if (den <= 0.0f)
			break;
		float u = (float)ox * F / den;
		int su = (int)(u - X0);
		if (su < 0)
			su = 0;
		if (su >= sw)
			su = sw - 1;
		float sc = F / (F + u * s);
		int colH = (int)((float)sh * sc + 0.5f);
		if (colH < 1)
			continue;
		int top = y + (sh - colH) / 2;
		uint32_t vstep = ((uint32_t)sh << 16) / (uint32_t)colH;
		uint32_t v = 0;
		int j = 0;
		if (top < cy0) {
			j = cy0 - top;
			v = vstep * (uint32_t)j;
		}
		for (; j < colH; j++, v += vstep) {
			int py = top + j;
			if (py >= cy1)
				break;
			uint32_t pix = _from565(a->cache[(size_t)(v >> 16) * sw + su]);
			c->buffer[(size_t)py * c->canvasWidth + px] = XlBlend(pix, fadeTo, fa);
		}
	}
}

void XlTilePinBadgeRect(XlApp* a, int x, int y, XlRect* r) {
	int s = xlnch.m.cell >= 80 ? 20 : 16;
	r->x = x + a->cacheW - s - 5;
	r->y = y + 5;
	r->w = s;
	r->h = s;
}

/*
 * XlTileOverlay -- hover state drawn on top of the blitted tile:
 * a 2px light frame and the "unpin" badge (top right).
 */
void XlTileOverlay(ChCanvas* c, XlApp* a, int x, int y, bool hover, const XlRect* clip) {
	if (!hover || !a->cache)
		return;
	uint32_t col = XlTileColor(a->color);
	bool light = (a->color == XL_COL_MUGA || a->color == XL_COL_IVORY);
	uint32_t edge = light ? XlBlend(col, XL_INK, 90) : XlBlend(col, XL_WHITE, 110);
	XlFrame(c, x, y, a->cacheW, a->cacheH, 2, edge, clip);
	XlRect b;
	XlTilePinBadgeRect(a, x, y, &b);
	XlFill(c, b.x, b.y, b.w, b.h, XlBlend(col, XL_INK, 150), clip);
	int bar = b.w / 2;
	XlFill(c, b.x + (b.w - bar) / 2, b.y + b.h / 2 - 1, bar, 2, XL_WHITE, clip);
}
