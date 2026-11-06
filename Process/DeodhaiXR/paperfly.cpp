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

#include "paperfly.h"
#include "alpha.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#if defined(ARCH_ARM64)
#include <arm_neon.h>
#endif

#define PAPERFLY_HIDE_MS   520.0f /* roll up and fly out */
#define PAPERFLY_SHOW_MS   560.0f /* fly in and unroll */
#define PAPERFLY_MAX_DT	   50	  /* a stalled frame does not make the paper jump */
#define PAPERFLY_ROLL_END  0.62f  /* fully rolled at this point of the flight */
#define PAPERFLY_FLY_START 0.18f  /* the tube starts flying here */
#define PAPERFLY_LIFT_START 0.12f
#define PAPERFLY_CORE	   3.0f	 /* tube radius with nothing wound on */
#define PAPERFLY_THICK	   1.1f	 /* paper thickness: the tube grows as it winds */
#define PAPERFLY_SHADOW_W  10.0f /* the tube's shadow on the flat paper */
#define PAPERFLY_MAX_W	   8192	 /* PaperColumn::u is int16 */
#define PAPERFLY_PI		   3.14159265f
#define PAPERFLY_RUN	   16
#define PAPERFLY_PAPER	   0xF3F0EAu /* back of the paper */
#define PAPERFLY_SHADOW_DX 14.0f	 /* drop shadow offset at the top of the arc */
#define PAPERFLY_SHADOW_DY 20.0f
#define PAPERFLY_SHADOW_A  70.0f	 /* ... and its opacity (of 255) */

static PaperFly g_fly[DEODHAI_PAPERFLY_MAX];

static inline float _paperfly_clamp01(float v) {
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float _paperfly_smooth(float v) {
	return v * v * (3.0f - 2.0f * v);
}

static inline int _paperfly_floor(float v) {
	int i = (int)v;
	return (v < (float)i) ? i - 1 : i;
}

static inline int _paperfly_ceil(float v) {
	int i = (int)v;
	return (v > (float)i) ? i + 1 : i;
}

static bool _paperfly_eligible(Window* win) {
	if (!win || !win->backBuffer || !win->sharedInfo)
		return false;
	if (win->flags & (WINDOW_FLAG_ALWAYS_ON_TOP | WINDOW_FLAG_POPUP | WINDOW_FLAG_STATIC))
		return false; /* docks, launchers and menus keep their instant toggle */
	WinSharedInfo* info = (WinSharedInfo*)win->sharedInfo;
	return !info->zoomed && info->width > 0 && info->height > 0 &&
		   info->width <= PAPERFLY_MAX_W && info->height <= PAPERFLY_MAX_W;
}

static PaperFly* _paperfly_find(Window* win) {
	for (int i = 0; i < DEODHAI_PAPERFLY_MAX; i++)
		if (g_fly[i].used && !g_fly[i].dying && g_fly[i].win == win && g_fly[i].handle == win->handle)
			return &g_fly[i];
	return NULL;
}

/**
 * @brief DeodhaiPaperFlyInitialise -- reset the module
 */
void DeodhaiPaperFlyInitialise() {
	for (int i = 0; i < DEODHAI_PAPERFLY_MAX; i++) {
		if (g_fly[i].snap)
			_KeMemUnmap(g_fly[i].snap, g_fly[i].snapBytes);
		free(g_fly[i].col);
	}
	memset(g_fly, 0, sizeof(g_fly));
}

/**
 * @brief DeodhaiPaperFlyStart -- start a flight or turn one around
 */
bool DeodhaiPaperFlyStart(Window* win, bool hiding) {
	if (!win)
		return false;
	PaperFly* f = _paperfly_find(win);
	if (f) {
		/* already in the air: a toggle turns the paper around from where
		 * it is (DeodhaiWindowHide is a toggle, so this is always the
		 * opposite of the current direction) */
		f->dir = -f->dir;
		return true;
	}
	if (!_paperfly_eligible(win))
		return false;
	for (int i = 0; i < DEODHAI_PAPERFLY_MAX; i++) {
		if (g_fly[i].used)
			continue;
		f = &g_fly[i];
		WinSharedInfo* info = (WinSharedInfo*)win->sharedInfo;
		size_t bytes = (size_t)info->width * (size_t)info->height * 4;
		/* slot buffers are kept between flights and only grow */
		if (f->snapBytes < bytes) {
			if (f->snap)
				_KeMemUnmap(f->snap, f->snapBytes);
			f->snap = (uint32_t*)_KeMemMap(NULL, bytes, 0, 0, MEMMAP_NO_FILEDESC, 0);
			f->snapBytes = f->snap ? bytes : 0;
		}
		float rmax = sqrtf(PAPERFLY_CORE * PAPERFLY_CORE +
						   (float)info->width * PAPERFLY_THICK / PAPERFLY_PI);
		int cap = info->width + 2 * _paperfly_ceil(rmax) + 8;
		if (f->colCap < cap) {
			free(f->col);
			f->col = (PaperColumn*)malloc((size_t)cap * sizeof(PaperColumn));
			f->colCap = f->col ? cap : 0;
		}
		if (!f->snap || !f->col)
			return false;
		memcpy(f->snap, win->backBuffer, bytes);
		f->used = true;
		f->dying = 0;
		f->win = win;
		f->handle = win->handle;
		f->x = info->x;
		f->y = info->y;
		f->w = info->width;
		f->h = info->height;
		f->t = hiding ? 0.0f : 1.0f;
		f->dir = hiding ? 1 : -1;
		f->last = 0;
		f->bboxValid = f->lastValid = false;
		f->arc = 0.12f * f->h + 20.0f;
		if (f->arc > 80.0f)
			f->arc = 80.0f;
		return true;
	}
	return false;
}

/**
 * @brief DeodhaiPaperFlyEligible -- true for windows that fly
 */
bool DeodhaiPaperFlyEligible(Window* win) {
	return _paperfly_eligible(win);
}

/**
 * @brief DeodhaiPaperFlyBusy -- true while the window is in the air
 */
bool DeodhaiPaperFlyBusy(Window* win) {
	return win && _paperfly_find(win) != NULL;
}

/**
 * @brief DeodhaiPaperFlyCancel -- the window is being closed
 */
void DeodhaiPaperFlyCancel(Window* win) {
	PaperFly* f = win ? _paperfly_find(win) : NULL;
	if (!f)
		return;
	f->win = NULL; /* never touch the window again */
	if (f->dir < 0)
		f->dying = 1; /* flying in for a window that is going away: drop it */
}

/**
 * @brief DeodhaiPaperFlyActive -- true while anything needs drawing
 */
bool DeodhaiPaperFlyActive() {
	for (int i = 0; i < DEODHAI_PAPERFLY_MAX; i++)
		if (g_fly[i].used)
			return true;
	return false;
}

/* the paper at progress t: how much is rolled, the tube, where it is, and
 * the column table. O(w + tube) */
static void _paperfly_geometry(PaperFly* f, int screen_w) {
	const float t = f->t;
	const int w = f->w;
	float rolled = (float)w * _paperfly_smooth(_paperfly_clamp01(t / PAPERFLY_ROLL_END));
	float r = sqrtf(PAPERFLY_CORE * PAPERFLY_CORE + rolled * PAPERFLY_THICK / PAPERFLY_PI);
	float rmax = sqrtf(PAPERFLY_CORE * PAPERFLY_CORE + (float)w * PAPERFLY_THICK / PAPERFLY_PI);
	float fk = _paperfly_clamp01((t - PAPERFLY_FLY_START) / (1.0f - PAPERFLY_FLY_START));
	f->flyK = fk * fk * fk; /* ease in: it accelerates away, and lands softly */
	f->liftK = _paperfly_clamp01((t - PAPERFLY_LIFT_START) / (1.0f - PAPERFLY_LIFT_START));
	f->span = (float)(screen_w - f->x) + 2.0f * rmax + 32.0f;
	f->dx = (int)(f->flyK * f->span + 0.5f);
	f->dy = -(int)(f->arc * sinf(PAPERFLY_PI * f->liftK) + 0.5f);
	f->rolled = rolled;
	f->radius = r;

	const bool tube = rolled >= 0.5f;
	if (!tube) {
		f->uMin = 0;
		f->uMax = w;
		f->flatStart = 0;
		f->flatEnd = w;
	} else {
		f->uMin = _paperfly_floor(rolled - r) - 1;
		int tubeEnd = _paperfly_ceil(rolled + r) + 1;
		f->uMax = tubeEnd > w ? tubeEnd : w;
		int fs = _paperfly_ceil(rolled + r + PAPERFLY_SHADOW_W);
		f->flatStart = fs < w ? fs : w;
		f->flatEnd = w;
	}
	int n = f->uMax - f->uMin;
	if (n > f->colCap) /* cannot happen: cap covers w + 2 rmax */
		n = f->colCap;
	for (int i = 0; i < n; i++) {
		int c = f->uMin + i;
		float xc = (float)c + 0.5f;
		PaperColumn pc = {-1, 255, 0};
		float d = xc - rolled;
		if (tube && d > -r && d < r) {
			/* front half of the tube: angle from the viewer, arc length
			 * from where the paper leaves the flat part (behind the axis) */
			float phi = atan2f(d, sqrtf(r * r - d * d));
			float s = r * (PAPERFLY_PI - phi);
			float tu = rolled - s;
			float light = 0.35f + 0.65f * cosf(phi);
			if (tu >= 0.0f && tu < (float)w) {
				pc.u = (int16_t)tu;
				pc.shade = (uint8_t)(255.0f * light);
				pc.back = 1;
			} else if (c >= (int)rolled && c < w) {
				pc.u = (int16_t)c; /* a thin tube does not cover the flat paper */
				pc.shade = 200;
			}
		} else if (c >= 0 && c < w && (!tube || xc >= rolled)) {
			pc.u = (int16_t)c;
			if (tube && d < r + PAPERFLY_SHADOW_W) {
				float k = (d - r) / PAPERFLY_SHADOW_W;
				pc.shade = (uint8_t)(150.0f + 105.0f * _paperfly_clamp01(k));
			}
		}
		f->col[i] = pc;
	}
}

/* drop shadow of the paper on what is below: it grows with the lift, so
 * the first and last frames match the window exactly */
static void _paperfly_shadow(const PaperFly* f, int* ox, int* oy, int* alpha, int* c0) {
	float s = sinf(PAPERFLY_PI * f->liftK);
	*ox = (int)(PAPERFLY_SHADOW_DX * s + 0.5f);
	*oy = (int)(PAPERFLY_SHADOW_DY * s + 0.5f);
	*alpha = (int)(PAPERFLY_SHADOW_A * s);
	int vis = _paperfly_floor(f->rolled - f->radius); /* leftmost paper column */
	*c0 = vis > f->uMin ? vis : f->uMin;
}

static void _paperfly_bbox(PaperFly* f, int screen_w, int screen_h) {
	int ox, oy, alpha, c0;
	_paperfly_shadow(f, &ox, &oy, &alpha, &c0);
	int x0 = f->x + f->dx + f->uMin, x1 = f->x + f->dx + f->uMax + ox;
	int y0 = f->y + f->dy, y1 = f->y + f->dy + f->h + oy;
	if (x0 < 0)
		x0 = 0;
	if (y0 < 0)
		y0 = 0;
	if (x1 > screen_w)
		x1 = screen_w;
	if (y1 > screen_h)
		y1 = screen_h;
	f->bboxValid = x1 > x0 && y1 > y0;
	f->bbox.x = x0;
	f->bbox.y = y0;
	f->bbox.w = x1 - x0;
	f->bbox.h = y1 - y0;
}

/**
 * @brief DeodhaiPaperFlyStep -- advance every flight to `now`
 */
int DeodhaiPaperFlyStep(uint64_t now, int screen_w, int screen_h, Window** shown, int max_shown) {
	int count = 0;
	for (int i = 0; i < DEODHAI_PAPERFLY_MAX; i++) {
		PaperFly* f = &g_fly[i];
		if (!f->used)
			continue;
		f->lastBBox = f->bbox;
		f->lastValid = f->bboxValid;
		/* ending takes two steps: report the last footprint for repaint
		 * (bbox invalid, last valid), then free the slot */
		if (f->dying == 3) {
			f->used = false;
			f->dying = 0;
			f->bboxValid = f->lastValid = false;
			continue;
		}
		if (f->dying == 1 || f->dying == 2) {
			/* 2: landed last frame, the window draws itself from now on */
			f->dying = 3;
			f->bboxValid = false;
			continue;
		}
		uint64_t dt = f->last ? now - f->last : 0;
		if (dt > PAPERFLY_MAX_DT)
			dt = PAPERFLY_MAX_DT;
		f->last = now;
		f->t += (float)f->dir * (float)dt / (f->dir > 0 ? PAPERFLY_HIDE_MS : PAPERFLY_SHOW_MS);
		if (f->t >= 1.0f && f->dir > 0) {
			f->t = 1.0f; /* gone off the right edge */
			f->dying = 3;
			f->bboxValid = false;
			continue;
		}
		if (f->t <= 0.0f && f->dir < 0) {
			/* landed: drawn flat this frame, exactly where the window is,
			 * and the window is shown -- no gap, no flicker */
			f->t = 0.0f;
			f->dying = 2;
			if (f->win && count < max_shown)
				shown[count++] = f->win;
		}
		if (f->t < 0.0f)
			f->t = 0.0f;
		_paperfly_geometry(f, screen_w);
		_paperfly_bbox(f, screen_w, screen_h);
	}
	return count;
}

/**
 * @brief DeodhaiPaperFlyDirty -- 2D rectangles to repaint this frame
 */
int DeodhaiPaperFlyDirty(Rect* rects, int max) {
	int n = 0;
	for (int i = 0; i < DEODHAI_PAPERFLY_MAX && n < max; i++) {
		PaperFly* f = &g_fly[i];
		if (!f->used || (!f->bboxValid && !f->lastValid))
			continue;
		Rect r = f->bboxValid ? f->bbox : f->lastBBox;
		if (f->bboxValid && f->lastValid) {
			int x0 = r.x < f->lastBBox.x ? r.x : f->lastBBox.x;
			int y0 = r.y < f->lastBBox.y ? r.y : f->lastBBox.y;
			int x1 = (r.x + r.w) > (f->lastBBox.x + f->lastBBox.w) ? (r.x + r.w)
																	: (f->lastBBox.x + f->lastBBox.w);
			int y1 = (r.y + r.h) > (f->lastBBox.y + f->lastBBox.h) ? (r.y + r.h)
																	: (f->lastBBox.y + f->lastBBox.h);
			r.x = x0;
			r.y = y0;
			r.w = x1 - x0;
			r.h = y1 - y0;
		}
		rects[n++] = r;
	}
	return n;
}

/**
 * @brief DeodhaiPaperFlyGet -- flight slot `index`, NULL if free
 */
PaperFly* DeodhaiPaperFlyGet(int index) {
	if (index < 0 || index >= DEODHAI_PAPERFLY_MAX || !g_fly[index].used)
		return NULL;
	return &g_fly[index];
}

/**
 * @brief DeodhaiPaperFlyTexel -- pixel a paper column shows on row v
 */
uint32_t DeodhaiPaperFlyTexel(const PaperFly* fly, const PaperColumn* col, int v) {
	if (col->u < 0)
		return 0;
	uint32_t p = fly->snap[(size_t)v * (size_t)fly->w + col->u];
	if (!col->back || !(p >> 24))
		return p;
	/* back of the paper: paper white, the print showing through 20% */
	uint32_t r = (((PAPERFLY_PAPER >> 16) & 0xFF) * 205 + ((p >> 16) & 0xFF) * 51) >> 8;
	uint32_t g = (((PAPERFLY_PAPER >> 8) & 0xFF) * 205 + ((p >> 8) & 0xFF) * 51) >> 8;
	uint32_t b = ((PAPERFLY_PAPER & 0xFF) * 205 + (p & 0xFF) * 51) >> 8;
	return (p & 0xFF000000u) | (r << 16) | (g << 8) | b;
}

/**
 * @brief DeodhaiPaperFlyShadeRun -- c' = c * (shade + 1) / 256, alpha kept
 */
void DeodhaiPaperFlyShadeRun(uint32_t* run, const uint8_t* shade, int n) {
	int i = 0;
#if defined(ARCH_ARM64)
	static const uint8_t spread[16] = {0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3};
	static const uint8_t alpha[16] = {0, 0, 0, 0xFF, 0, 0, 0, 0xFF, 0, 0, 0, 0xFF, 0, 0, 0, 0xFF};
	const uint8x16_t vspread = vld1q_u8(spread);
	const uint8x16_t valpha = vld1q_u8(alpha);
	for (; i + 4 <= n; i += 4) {
		uint8x16_t px = vld1q_u8((const uint8_t*)(run + i));
		uint32_t s4 = (uint32_t)shade[i] | (uint32_t)shade[i + 1] << 8 |
					  (uint32_t)shade[i + 2] << 16 | (uint32_t)shade[i + 3] << 24;
		uint8x16_t sh = vqtbl1q_u8(vreinterpretq_u8_u32(vdupq_n_u32(s4)), vspread);
		uint16x8_t lo = vaddw_u8(vmull_u8(vget_low_u8(px), vget_low_u8(sh)), vget_low_u8(px));
		uint16x8_t hi = vaddw_high_u8(vmull_high_u8(px, sh), px);
		uint8x16_t res = vcombine_u8(vshrn_n_u16(lo, 8), vshrn_n_u16(hi, 8));
		vst1q_u8((uint8_t*)(run + i), vbslq_u8(valpha, px, res));
	}
#endif
	for (; i < n; i++) {
		uint32_t p = run[i], s = (uint32_t)shade[i] + 1;
		run[i] = (p & 0xFF000000u) | ((((p >> 16) & 0xFF) * s >> 8) << 16) |
				 ((((p >> 8) & 0xFF) * s >> 8) << 8) | ((p & 0xFF) * s >> 8);
	}
}

/* columns through the table: gathered, shaded with NEON, blended */
static void _paperfly_table_span(
	const PaperFly* f, uint32_t* drow, int x0, int x1, int X0, int v) {
	uint32_t run[PAPERFLY_RUN];
	uint8_t sh[PAPERFLY_RUN];
	for (int x = x0; x < x1; x += PAPERFLY_RUN) {
		int n = x1 - x < PAPERFLY_RUN ? x1 - x : PAPERFLY_RUN;
		const PaperColumn* pc = f->col + (x - X0 - f->uMin);
		for (int i = 0; i < n; i++) {
			run[i] = DeodhaiPaperFlyTexel(f, pc + i, v);
			sh[i] = pc[i].shade;
		}
		DeodhaiPaperFlyShadeRun(run, sh, n);
		__pixel_blend_neon(drow + x, run, n);
	}
}

static void _paperfly_draw_shadow(ChCanvas* canvas, const PaperFly* f, const Rect* clip) {
	int ox, oy, alpha, c0;
	_paperfly_shadow(f, &ox, &oy, &alpha, &c0);
	if (alpha <= 0)
		return;
	const int X0 = f->x + f->dx + ox, Y0 = f->y + f->dy + oy;
	int xs = X0 + c0 > clip->x ? X0 + c0 : clip->x;
	int xe = (X0 + f->uMax) < (clip->x + clip->w) ? (X0 + f->uMax) : (clip->x + clip->w);
	int ys = Y0 > clip->y ? Y0 : clip->y;
	int ye = (Y0 + f->h) < (clip->y + clip->h) ? (Y0 + f->h) : (clip->y + clip->h);
	if (xs < 0)
		xs = 0;
	if (ys < 0)
		ys = 0;
	if (xe > (int)canvas->canvasWidth)
		xe = (int)canvas->canvasWidth;
	if (ye > (int)canvas->canvasHeight)
		ye = (int)canvas->canvasHeight;
	if (xs >= xe || ys >= ye)
		return;
	uint32_t tint[64];
	for (int i = 0; i < 64; i++)
		tint[i] = (uint32_t)alpha << 24; /* translucent black */
	for (int y = ys; y < ye; y++) {
		uint32_t* drow = canvas->buffer + (size_t)y * canvas->canvasWidth;
		for (int x = xs; x < xe; x += 64)
			__pixel_blend_neon(drow + x, tint, xe - x < 64 ? xe - x : 64);
	}
}

static void _paperfly_draw(ChCanvas* canvas, const PaperFly* f, const Rect* clip) {
	_paperfly_draw_shadow(canvas, f, clip);
	const int X0 = f->x + f->dx, Y0 = f->y + f->dy;
	int ys = Y0 > clip->y ? Y0 : clip->y;
	int ye = (Y0 + f->h) < (clip->y + clip->h) ? (Y0 + f->h) : (clip->y + clip->h);
	int xs = X0 + f->uMin > clip->x ? X0 + f->uMin : clip->x;
	int xe = (X0 + f->uMax) < (clip->x + clip->w) ? (X0 + f->uMax) : (clip->x + clip->w);
	if (ys < 0)
		ys = 0;
	if (xs < 0)
		xs = 0;
	if (ye > (int)canvas->canvasHeight)
		ye = (int)canvas->canvasHeight;
	if (xe > (int)canvas->canvasWidth)
		xe = (int)canvas->canvasWidth;
	if (xs >= xe || ys >= ye)
		return;
	/* screen columns of the untouched flat part */
	int fs = X0 + f->flatStart, fe = X0 + f->flatEnd;
	int a1 = fs < xe ? (fs > xs ? fs : xs) : xe;	  /* end of the left table part */
	int b1 = fe < xe ? (fe > a1 ? fe : a1) : xe;	  /* end of the flat part */
	for (int y = ys; y < ye; y++) {
		const int v = y - Y0;
		uint32_t* drow = canvas->buffer + (size_t)y * canvas->canvasWidth;
		if (a1 > xs)
			_paperfly_table_span(f, drow, xs, a1, X0, v);
		if (b1 > a1) /* the window as is: one NEON blend from the snapshot */
			__pixel_blend_neon(drow + a1, f->snap + (size_t)v * f->w + (a1 - X0), b1 - a1);
		if (xe > b1)
			_paperfly_table_span(f, drow, b1, xe, X0, v);
	}
}

/**
 * @brief DeodhaiPaperFlyDraw -- 2D: draw every paper clipped to `clip`
 */
void DeodhaiPaperFlyDraw(ChCanvas* canvas, const Rect* clip) {
	for (int i = 0; i < DEODHAI_PAPERFLY_MAX; i++)
		if (g_fly[i].used && g_fly[i].bboxValid)
			_paperfly_draw(canvas, &g_fly[i], clip);
}
