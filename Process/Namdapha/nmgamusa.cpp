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
 * nmgamusa.cpp -- the Gamusa bar (see nmgamusa.h).
 *
 * Text: the clock repaints every minute, and Chitralekha re-bakes a font's
 * whole atlas whenever its size changes; the old clock switched sizes
 * twice per paint. Here the few characters the bar ever shows are
 * rasterised once into 8-bit coverage (clock digits in Roboto Light,
 * small lower-case text in the base font) and painting never touches a
 * font again.
 *
 * Icons: app icons are 55x50 bitmaps, the full button. Each distinct icon
 * is shrunk once (area average) to fit above the marker; the copy is
 * smaller than the mapped file, which is unmapped right after.
 */

#include "nmdapha.h"
#include "nmgamusa.h"
#include <widgets/gmstyle.h>
#include <font.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/* ------------------------------------------------------------ glyph sets */

typedef struct {
	uint8_t w, h;
	int8_t ox, oy; /* bitmap offset from (pen, baseline) */
	uint8_t adv;
	uint16_t off; /* into the pool */
} NmGlyph;

typedef struct {
	NmGlyph g[95]; /* ' ' .. '~', only the listed characters have bitmaps */
	uint8_t* pool;
	int ascent;
} NmGlyphSet;

static NmGlyphSet _clock; /* digits and ':'             */
static NmGlyphSet _small; /* lower case, digits, ", :"  */

static const char _clockChars[] = "0123456789:";
static const char _smallChars[] = "abcdefghijklmnopqrstuvwxyz0123456789,:";

static void _gs_build(NmGlyphSet* gs, ChFont* f, int pt, const char* chars) {
	memset(gs, 0, sizeof(NmGlyphSet));
	if (!f)
		return;
	ChFontSetSize(f, pt);
	int px = pt * 96 / 72;
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
	size_t cap = (size_t)px * px * 2, used = 0;
	uint8_t* pool = (uint8_t*)malloc(cap);
	char str[2] = {' ', 0};
	gs->g[0].adv = (uint8_t)(ChFontGetWidth(f, str) & 0xFF);
	for (const char* c = chars; *c && pool; c++) {
		NmGlyph* g = &gs->g[*c - 32];
		str[0] = *c;
		int64_t adv = ChFontGetWidth(f, str);
		g->adv = (uint8_t)(adv > 255 ? 255 : (adv < 0 ? 0 : adv));
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
		if (w > 255 || h > 255 || used + (size_t)w * h > 0xFFFF)
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
		g->off = (uint16_t)used;
		used += (size_t)w * h;
	}
	free(tmp);
	if (pool && used) {
		uint8_t* np = (uint8_t*)realloc(pool, used); /* give back the slack */
		if (np)
			pool = np;
	}
	gs->pool = pool;
	NmGlyph* ref = &gs->g[(chars[0] == '0' ? '0' : 'd') - 32];
	gs->ascent = ref->h ? -ref->oy : px * 7 / 10;
}

static int _gs_width(const NmGlyphSet* gs, const char* s) {
	int w = 0;
	for (; *s; s++)
		if (*s >= 32 && *s < 127)
			w += gs->g[*s - 32].adv;
	return w;
}

/* coverage blended over what is in the canvas, clipped to it */
static void
_gs_draw(ChCanvas* c, const NmGlyphSet* gs, const char* s, int x, int base, uint32_t col) {
	if (!gs->pool)
		return;
	int cw = (int)c->canvasWidth, ch = (int)c->canvasHeight;
	for (; *s; s++) {
		if (*s < 32 || *s >= 127)
			continue;
		const NmGlyph* g = &gs->g[*s - 32];
		const uint8_t* bm = gs->pool + g->off;
		for (int y = 0; y < g->h; y++) {
			int py = base + g->oy + y;
			if (py < 0 || py >= ch)
				continue;
			uint32_t* row = c->buffer + (size_t)py * cw;
			for (int k = 0; k < g->w; k++) {
				int px = x + g->ox + k;
				uint8_t a = bm[y * g->w + k];
				if (a && px >= 0 && px < cw)
					row[px] = ChGmBlend(row[px], col, a);
			}
		}
		x += g->adv;
	}
}

void NmGmInit(ChFont* baseFont) {
	ChFont* light = ChInitialiseFont((char*)ROBOTO_LIGHT);
	_gs_build(&_clock, light ? light : baseFont, 17, _clockChars);
	if (light)
		ChFontClose(light);
	_gs_build(&_small, baseFont, 9, _smallChars);
}

/* ------------------------------------------------------------------ bar */

/* corner coverage of a quarter circle: 0 outside .. 255 inside */
static uint8_t _corner_cov(int dx, int dy, int r) {
	/* 4x4 supersampled, integer */
	int in = 0;
	for (int sy = 0; sy < 4; sy++)
		for (int sx = 0; sx < 4; sx++) {
			int fx = dx * 4 + sx - r * 4 + 2, fy = dy * 4 + sy - r * 4 + 2;
			if (fx * fx + fy * fy <= r * r * 16)
				in++;
		}
	return (uint8_t)(in * 255 / 16);
}

void NmGmPaintBar(ChWindow* win) {
	ChCanvas* c = win->canv;
	int W = (int)win->info->width, H = (int)win->info->height;
	ChGmFill(c, 0, 0, W, H, 0xFFE2DACB); //CH_GM_COTTON);

	/* woven border, top: the red band and its light companion thread */
	//ChGmFill(c, 0, 0, W, 2, CH_GM_RED);
	//ChGmFill(c, 0, 3, W, 1, CH_GM_RED_THREAD);

	/* bottom: a sparse row of small pale lozenges, every 18 px */
	uint32_t pale = ChGmBlend(NM_COLOR_CREAM, CH_GM_RED_THREAD, 110); //CH_GM_COTTON
	for (int x = 12; x < W - 6; x += 18)
		ChGmLozenge(c, x, H - 4, 2, pale);

	/* between the Go logo and the apps: one stitched muga lozenge */
	ChGmLozengeStitched(c,
						GO_BUTTON_XCOORD + NAMDAPHA_BUTTON_WIDTH + 7,
						H / 2,
						4,
						CH_GM_MUGA,
						CH_GM_MUGA_DEEP,
						CH_GM_WHITE);

	/* the bar floats above the screen edge: round its top corners away */
	int r = NM_GM_RADIUS;
	if (W > 2 * r && H > r) {
		for (int y = 0; y < r; y++) {
			uint32_t* row = c->buffer + (size_t)y * c->canvasWidth;
			for (int x = 0; x < r; x++) {
				uint8_t cov = _corner_cov(x, y, r);
				if (cov == 255)
					continue;
				uint32_t* L = &row[x];
				uint32_t* R = &row[W - 1 - x];
				*L = ((uint32_t)cov << 24) | (*L & 0xFFFFFF);
				*R = ((uint32_t)cov << 24) | (*R & 0xFFFFFF);
			}
		}
	}
}

/* --------------------------------------------------------------- icons */

/* shrink an icon once to fit NM_GM_ICON_MAX*, area averaged in
 * premultiplied alpha; the mapped file is given back afterwards */
static void _icon_thumb(ButtonInfo* info) {
	if (!info || info->thumb || !info->imageData)
		return;
	int sw = info->iconWidth, sh = info->iconHeight;
	int bytes = (info->iconBpp ? info->iconBpp : 32) / 8;
	if (bytes < 3)
		return;
	int pitch = ((sw * bytes * 8 + 31) / 32) * 4;
	/* scale = min(1, maxw/sw, maxh/sh), in 1/256 */
	int s = 256;
	if (sw * s > NM_GM_ICON_MAXW * 256)
		s = NM_GM_ICON_MAXW * 256 / sw;
	if (sh * s > NM_GM_ICON_MAXH * 256)
		s = NM_GM_ICON_MAXH * 256 / sh;
	int tw = sw * s / 256, th = sh * s / 256;
	if (tw < 1)
		tw = 1;
	if (th < 1)
		th = 1;
	uint32_t* t = (uint32_t*)malloc((size_t)tw * th * 4);
	if (!t)
		return;
	for (int y = 0; y < th; y++) {
		int y0 = y * sh / th, y1 = (y + 1) * sh / th;
		if (y1 <= y0)
			y1 = y0 + 1;
		for (int x = 0; x < tw; x++) {
			int x0 = x * sw / tw, x1 = (x + 1) * sw / tw;
			if (x1 <= x0)
				x1 = x0 + 1;
			uint32_t sa = 0, sr = 0, sg = 0, sb = 0, n = 0;
			for (int yy = y0; yy < y1; yy++) {
				const uint8_t* row = info->imageData + (size_t)(sh - 1 - yy) * pitch;
				for (int xx = x0; xx < x1; xx++) {
					const uint8_t* p = row + xx * bytes;
					uint32_t a = bytes >= 4 ? p[3] : 255;
					sb += p[0] * a;
					sg += p[1] * a;
					sr += p[2] * a;
					sa += a;
					n++;
				}
			}
			uint32_t A = sa / n;
			uint32_t px = 0;
			if (sa) {
				uint32_t R = sr / sa, G = sg / sa, B = sb / sa;
				px = (A << 24) | (R << 16) | (G << 8) | B;
			}
			t[y * tw + x] = px;
		}
	}
	info->thumb = t;
	info->thumbW = tw;
	info->thumbH = th;
	/* the copy is all the bar draws from now on */
	if (info->fileBuffer) {
		_KeMemUnmap(info->fileBuffer, info->fileSize);
		info->fileBuffer = NULL;
	}
	info->imageData = NULL;
}

void NmGmIconRelease(ButtonInfo* info) {
	if (!info || !info->thumb)
		return;
	free(info->thumb);
	info->thumb = NULL;
}

static void _icon_draw(ChCanvas* c, ButtonInfo* info, int x, int y, int fade) {
	_icon_thumb(info);
	if (!info || !info->thumb)
		return;
	int cw = (int)c->canvasWidth, ch = (int)c->canvasHeight;
	for (int i = 0; i < info->thumbH; i++) {
		int py = y + i;
		if (py < 0 || py >= ch)
			continue;
		uint32_t* row = c->buffer + (size_t)py * cw;
		const uint32_t* src = info->thumb + i * info->thumbW;
		for (int k = 0; k < info->thumbW; k++) {
			int px = x + k;
			uint32_t a = src[k] >> 24;
			if (!a || px < 0 || px >= cw)
				continue;
			row[px] = ChGmBlend(row[px], src[k] | 0xFF000000u, a * fade >> 8);
		}
	}
}

void NmGmButtonPaint(NamdaphaButton* b, ChWindow* win) {
	ChCanvas* c = win->canv;
	uint32_t bg = CH_GM_COTTON;
	if (b->clicked)
		bg = CH_GM_LINEN_DEEP;
	else if (b->focused)
		bg = CH_GM_PAPER;
	else if (b->hover)
		bg = CH_GM_LINEN;
	/* the bar behind first, then the plate inset by 2 */
	ChGmFill(c, b->x, b->y, b->w, b->h, NM_COLOR_CREAM); //CH_GM_COTTON);
	if (bg != CH_GM_COTTON) {
		ChGmFill(c, b->x + 2, b->y + 2, b->w - 4, b->h - 4, bg);
		if (b->focused)
			ChGmFrame(c, b->x + 2, b->y + 2, b->w - 4, b->h - 4, CH_GM_HAIRLINE);
	}

	ButtonInfo* info = b->nmbuttoninfo;
	if (info) {
		_icon_thumb(info);
		int iw = info->thumb ? info->thumbW : 0, ih = info->thumb ? info->thumbH : 0;
		int ix = b->x + (b->w - iw) / 2;
		int iy = b->y + 4 + (NM_GM_ICON_MAXH - ih) / 2 + (b->clicked ? 1 : 0);
		_icon_draw(c, info, ix, iy, 256);
	}

	/* marker under the icon, all threads: the long red one when focused,
	 * a short muga one for a running app, ink while hovered */
	int my = b->y + b->h - 6;
	if (b->focused) {
		int tw = b->w - 18;
		ChGmThread(c, b->x + (b->w - tw) / 2, my, tw, CH_GM_RED, CH_GM_RED_THREAD);
	} else {
		uint32_t col =
			b->hover ? CH_GM_INK_SOFT : ChGmBlend(NM_COLOR_CREAM, CH_GM_MUGA, 170); //CH_GM_COTTON
		ChGmFill(c, b->x + b->w / 2 - 6, my, 12, 2, col);
	}
}

/* ---------------------------------------------------------------- clock */

static const char* const _wd[7] = {"sun", "mon", "tue", "wed", "thu", "fri", "sat"};
static const char* const _mo[12] = {
	"jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"};

/* day of week, 0 = sunday (Sakamoto) */
static int _weekday(int y, int m, int d) {
	static const int t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
	if (m < 3)
		y -= 1;
	return (y + y / 4 - y / 100 + y / 400 + t[(m - 1) % 12] + d) % 7;
}

static void _put_num(char* out, int* n, int v, bool pad) {
	if (v >= 10 || pad)
		out[(*n)++] = (char)('0' + (v / 10) % 10);
	out[(*n)++] = (char)('0' + v % 10);
}

void NmGmClockPaint(
	NamdaphaButton* b, ChWindow* win, int hour24, int minute, int day, int month, int year) {
	ChCanvas* c = win->canv;
	ChGmFill(c, b->x, b->y, b->w, b->h, NM_COLOR_CREAM); //CH_GM_COTTON);
	/* a hairline keeps the clock apart from the apps */
	ChGmFill(c, b->x, b->y + 8, 1, b->h - 16, CH_GM_HAIRLINE);

	char t[8];
	int n = 0;
	int h12 = hour24 % 12;
	if (h12 == 0)
		h12 = 12;
	_put_num(t, &n, h12, false);
	t[n++] = ':';
	_put_num(t, &n, minute < 0 ? 0 : minute % 60, true);
	t[n] = 0;
	const char* ampm = hour24 < 12 ? "am" : "pm";

	char d[16];
	n = 0;
	const char* wd = (month >= 1 && month <= 12 && day >= 1) ? _wd[_weekday(year, month, day)] : "";
	for (const char* p = wd; *p; p++)
		d[n++] = *p;
	if (n) {
		d[n++] = ',';
		d[n++] = ' ';
	}
	if (day >= 1 && day <= 31)
		_put_num(d, &n, day, false);
	if (month >= 1 && month <= 12) {
		d[n++] = ' ';
		for (const char* p = _mo[month - 1]; *p; p++)
			d[n++] = *p;
	}
	d[n] = 0;

	int tW = _gs_width(&_clock, t), aW = _gs_width(&_small, ampm);
	int line1 = tW + 3 + aW;
	int cx = b->x + 1 + (b->w - 1) / 2;
	int tx = cx - line1 / 2;
	int base1 = b->y + 6 + _clock.ascent;
	_gs_draw(c, &_clock, t, tx, base1, CH_GM_INK);
	_gs_draw(c, &_small, ampm, tx + tW + 3, base1, CH_GM_RED);

	int dW = _gs_width(&_small, d);
	int base2 = base1 + 6 + _small.ascent + 2;
	_gs_draw(c, &_small, d, cx - dW / 2, base2, CH_GM_INK_SOFT);
}

/* ------------------------------------------------------------------ go */

void NmGmGoPaint(NamdaphaButton* b, ChWindow* win, ButtonInfo* logo) {
	ChCanvas* c = win->canv;
	ChGmFill(c, b->x, b->y, b->w, b->h, NM_COLOR_CREAM); //CH_GM_COTTON);
	if (!logo || !logo->imageData)
		return;
	/* the logo is white with grey shading: keep the shading as the mix
	 * between the deep and the bright red, so it reads on paper */
	uint32_t hi, lo;
	if (b->clicked) {
		hi = CH_GM_RED_DEEP;
		lo = 0xFF5E1016;
	} else if (b->hover) {
		hi = CH_GM_RED_HOVER;
		lo = CH_GM_RED;
	} else {
		hi = CH_GM_RED;
		lo = CH_GM_RED_DEEP;
	}
	int width = logo->iconWidth, height = logo->iconHeight;
	int bytes = (logo->iconBpp ? logo->iconBpp : 32) / 8;
	if (bytes < 3)
		return;
	int pitch = ((width * bytes * 8 + 31) / 32) * 4;
	int x = b->x + (b->w - width) / 2;
	int y = b->y + (b->h - height) / 2 + (b->clicked ? 1 : 0);
	int cw = (int)c->canvasWidth, ch = (int)c->canvasHeight;
	for (int i = 0; i < height; i++) {
		int py = y + i;
		if (py < 0 || py >= ch)
			continue;
		const uint8_t* row = logo->imageData + (size_t)(height - 1 - i) * pitch;
		uint32_t* dst = c->buffer + (size_t)py * cw;
		for (int k = 0; k < width; k++) {
			const uint8_t* p = row + k * bytes;
			uint32_t a = bytes >= 4 ? p[3] : 255;
			int px = x + k;
			if (!a || px < 0 || px >= cw)
				continue;
			/* luminance 0..255 */
			uint32_t lum = (p[2] * 77 + p[1] * 150 + p[0] * 29) >> 8;
			uint32_t col = ChGmBlend(lo, hi, lum);
			/* the art is pale and soft-edged: firm the alpha a little */
			uint32_t aa = a + (a >> 1);
			if (aa > 255)
				aa = 255;
			dst[px] = ChGmBlend(dst[px], col, aa);
		}
	}
}
