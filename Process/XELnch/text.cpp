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
 * text.cpp -- glyph sets: every face and size the launcher draws text
 * in is rasterised once at start-up into tight 8-bit coverage bitmaps.
 *
 * Why: Chitralekha re-bakes a font's whole glyph atlas whenever its size
 * changes, and a start screen mixes seven sizes on one card. With glyph
 * sets painting never touches a font, text can be faded and clipped like
 * any other pixels, and the Roboto faces are closed again right after
 * start-up (their atlases freed). Cost: ~150 KB of coverage in all.
 */

#include "launcher.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
	const char* face; /* NULL = the app's base font */
	int pt;
	int ptCompact; /* unused: cards keep one size on every screen */
} XlSetSpec;

static const XlSetSpec _spec[XL_GS_COUNT] = {
	{ NULL, 10, 9 },				 /* BODY   */
	{ NULL, 8, 7 },					 /* SMALL  */
	{ ROBOTO_LIGHT, 13, 11 },		 /* MID    */
	{ ROBOTO_LIGHT_ITALIC, 13, 11 }, /* ITALIC */
	{ ROBOTO_LIGHT, 19, 15 },		 /* NAME_S */
	{ ROBOTO_LIGHT, 30, 22 },		 /* NAME_L */
	{ ROBOTO_THIN, 36, 26 },		 /* THIN   */
};

static void _build(XlGlyphSet* gs, ChFont* f, int pt) {
	memset(gs, 0, sizeof(XlGlyphSet));
	ChFontSetSize(f, pt);
	int px = (int)(pt * 96 / 72);
	gs->px = px;
	int tw = px * 2 + 8, th = px * 2 + 8;
	int penx = px / 2, base = px + px / 4 + 2;
	uint32_t* tmp = (uint32_t*)malloc((size_t)tw * th * 4);
	if (!tmp)
		return;
	ChCanvas cv;
	memset(&cv, 0, sizeof(cv));
	cv.canvasWidth = tw;
	cv.canvasHeight = th;
	cv.buffer = tmp;
	size_t cap = (size_t)px * px * 24, used = 0;
	uint8_t* pool = (uint8_t*)malloc(cap);
	char str[2] = { 0, 0 };
	for (int ch = 32; ch < 127; ch++) {
		XlGlyph* g = &gs->g[ch - 32];
		str[0] = (char)ch;
		int adv = (int)ChFontGetWidth(f, str);
		g->adv = (uint8_t)(adv > 255 ? 255 : (adv < 0 ? 0 : adv));
		if (ch == ' ' || !pool)
			continue;
		for (int i = 0; i < tw * th; i++)
			tmp[i] = 0xFF000000u;
		ChFontDrawText(&cv, f, str, penx, base, pt, 0xFFFFFFFFu);
		int x0 = tw, y0 = th, x1 = -1, y1 = -1;
		for (int y = 0; y < th; y++)
			for (int x = 0; x < tw; x++)
				if (tmp[y * tw + x] & 0xFF0000u) {
					if (x < x0)
						x0 = x;
					if (x > x1)
						x1 = x;
					if (y < y0)
						y0 = y;
					if (y > y1)
						y1 = y;
				}
		if (x1 < 0)
			continue;
		int w = x1 - x0 + 1, h = y1 - y0 + 1;
		if (w > 255 || h > 255)
			continue;
		if (used + (size_t)w * h > cap) {
			size_t ncap = cap * 2 + (size_t)w * h;
			uint8_t* np = (uint8_t*)realloc(pool, ncap);
			if (!np)
				continue;
			pool = np;
			cap = ncap;
		}
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++)
				pool[used + y * w + x] = (uint8_t)((tmp[(y0 + y) * tw + x0 + x] >> 16) & 0xFF);
		g->w = (uint8_t)w;
		g->h = (uint8_t)h;
		g->ox = (int8_t)(x0 - penx);
		g->oy = (int8_t)(y0 - base);
		g->off = (uint32_t)used;
		used += (size_t)w * h;
	}
	free(tmp);
	/* give back the slack */
	if (pool && used) {
		uint8_t* np = (uint8_t*)realloc(pool, used);
		if (np)
			pool = np;
	}
	gs->pool = pool;
	XlGlyph* H = &gs->g['H' - 32];
	gs->ascent = H->h ? -H->oy : px * 7 / 10;
}

void XlTextInit(ChFont* body) {
	ChFont* faces[3] = { NULL, NULL, NULL };
	const char* names[3] = { ROBOTO_LIGHT, ROBOTO_LIGHT_ITALIC, ROBOTO_THIN };
	for (int s = 0; s < XL_GS_COUNT; s++) {
		ChFont* f = body;
		if (_spec[s].face) {
			for (int i = 0; i < 3; i++) {
				if (strcmp(_spec[s].face, names[i]) != 0)
					continue;
				if (!faces[i])
					faces[i] = ChInitialiseFont((char*)names[i]);
				if (faces[i])
					f = faces[i];
			}
		}
		if (f)
			_build(&xlnch.gs[s], f, _spec[s].pt); /* one size on every screen */
	}
	/* the Roboto faces were only needed to rasterise */
	for (int i = 0; i < 3; i++)
		if (faces[i])
			ChFontClose(faces[i]);
}

int XlTextAscent(int set) {
	return xlnch.gs[set].ascent;
}

int XlTextWidthN(int set, const char* s, int n) {
	const XlGlyphSet* gs = &xlnch.gs[set];
	int w = 0;
	for (int i = 0; i < n && s[i]; i++) {
		unsigned c = (unsigned char)s[i];
		if (c >= 32 && c < 127)
			w += gs->g[c - 32].adv;
	}
	return w;
}

int XlTextWidth(int set, const char* s) {
	return XlTextWidthN(set, s, 0x7FFFFFFF);
}

int XlText(ChCanvas* c, int set, const char* s, int x, int baseline, uint32_t col, uint32_t alpha,
		   const XlRect* clip) {
	const XlGlyphSet* gs = &xlnch.gs[set];
	if (!c || !c->buffer || !s)
		return 0;
	int cx0 = 0, cy0 = xlOriginY, cx1 = (int)c->canvasWidth, cy1 = xlOriginY + (int)c->canvasHeight;
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
	int pen = x;
	for (; *s; s++) {
		unsigned ch = (unsigned char)*s;
		if (ch < 32 || ch >= 127)
			continue;
		const XlGlyph* g = &gs->g[ch - 32];
		if (g->w && gs->pool && alpha) {
			int gx = pen + g->ox, gy = baseline + g->oy;
			int x0 = gx < cx0 ? cx0 : gx, x1 = gx + g->w > cx1 ? cx1 : gx + g->w;
			int y0 = gy < cy0 ? cy0 : gy, y1 = gy + g->h > cy1 ? cy1 : gy + g->h;
			for (int y = y0; y < y1; y++) {
				const uint8_t* src = gs->pool + g->off + (y - gy) * g->w + (x0 - gx);
				uint32_t* dst = c->buffer + (size_t)(y - xlOriginY) * c->canvasWidth + x0;
				for (int xx = x0; xx < x1; xx++, src++, dst++) {
					uint32_t a = *src;
					if (!a)
						continue;
					if (alpha < 255)
						a = (a * alpha + 128) >> 8;
					*dst = XlBlend(*dst, col, a);
				}
			}
		}
		pen += g->adv;
	}
	return pen - x;
}

/* longest prefix + "..." that fits, by binary search over the length */
void XlTextFit(int set, const char* src, int maxW, char* dst, int dstSize) {
	int n = (int)strlen(src);
	if (n > dstSize - 1)
		n = dstSize - 1;
	if (XlTextWidthN(set, src, n) <= maxW) {
		XlMemcpy(dst, src, n);
		dst[n] = '\0';
		return;
	}
	int dots = XlTextWidth(set, "...");
	int lo = 0, hi = n;
	while (lo < hi) {
		int mid = (lo + hi + 1) / 2;
		if (XlTextWidthN(set, src, mid) + dots <= maxW)
			lo = mid;
		else
			hi = mid - 1;
	}
	if (lo > dstSize - 4)
		lo = dstSize - 4;
	while (lo > 0 && src[lo - 1] == ' ')
		lo--;
	XlMemcpy(dst, src, lo);
	XlMemcpy(dst + lo, "...", 4);
}
