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

#include "paperash.h"
#include "dirtytiles.h"
#include "alpha.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#if defined(ARCH_ARM64)
#include <arm_neon.h>
#endif

/* timeline (ms) */
#define PAPERASH_SWEEP_MS	 560.0f /* the burn front crosses the window top to bottom */
#define PAPERASH_RAGGED_MS	 170.0f /* ... with a wavy front: per column delay */
#define PAPERASH_HEAT_MS	 150.0f /* a flake glows this long before it breaks off */
#define PAPERASH_LIFE_MS	 680.0f /* flight time, plus up to ... */
#define PAPERASH_LIFE_VAR_MS 420.0f
#define PAPERASH_MAX_DT		 50 /* a stalled frame does not make the ash jump */
/* flakes */
#define PAPERASH_TARGET_FLAKES 5000.0f /* flake size is picked to stay near this */
#define PAPERASH_MIN_SIZE	   6
#define PAPERASH_MAX_W		   8192
#define PAPERASH_SHRINK		   0.62f /* size lost by the end of the flight */
#define PAPERASH_WOBBLE_HZ	   5.0f	 /* rad / s of the sideways wobble */
#define PAPERASH_MIN_FLIP	   0.12f /* edge-on flakes keep a sliver */
#define PAPERASH_SEAM		   0.45f /* 3D: attached flakes overlap (< half a pixel at 1:1), no cracks */
#define PAPERASH_TWO_PI		   6.28318531f
#define PAPERASH_INV_TWO_PI	   0.15915494f
#define PAPERASH_HALF_PI	   1.57079633f

static PaperAsh g_ash[DEODHAI_PAPERASH_MAX];
static DirtyTiles g_tiles;
static Rect* g_rects;
static int g_rectCap;
static int g_rectCount;

/* ---- small helpers --------------------------------------------------- */

static inline float _paperash_clamp01(float v) {
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float _paperash_smooth(float v) {
	return v * v * (3.0f - 2.0f * v);
}

static inline int _paperash_floor(float v) {
	int i = (int)v;
	return (v < (float)i) ? i - 1 : i;
}

static inline int _paperash_ceil(float v) {
	int i = (int)v;
	return (v > (float)i) ? i + 1 : i;
}

static inline float _paperash_absf(float v) {
	return v < 0.0f ? -v : v;
}

/* deterministic 0..1 noise */
static inline float _paperash_hash(uint32_t x) {
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return (float)(x & 0xFFFFFF) * (1.0f / 16777216.0f);
}

/* sin on any range: reduce to [-pi, pi], parabola + one refinement
 * (|error| < 0.001); the NEON version below runs the same operations */
static inline float _paperash_sin(float x) {
	float n = floorf(x * PAPERASH_INV_TWO_PI + 0.5f);
	x -= n * PAPERASH_TWO_PI;
	float ax = x < 0.0f ? -x : x;
	float y = 1.27323954f * x - 0.40528473f * x * ax;
	float ay = y < 0.0f ? -y : y;
	return 0.225f * (y * ay - y) + y;
}

#if defined(ARCH_ARM64)
static inline float32x4_t _paperash_sin4(float32x4_t x) {
	float32x4_t n = vrndmq_f32(vmlaq_n_f32(vdupq_n_f32(0.5f), x, PAPERASH_INV_TWO_PI));
	x = vmlsq_n_f32(x, n, PAPERASH_TWO_PI);
	float32x4_t y = vmlsq_f32(vmulq_n_f32(x, 1.27323954f), vmulq_n_f32(x, 0.40528473f), vabsq_f32(x));
	float32x4_t t = vsubq_f32(vmulq_f32(y, vabsq_f32(y)), y);
	return vmlaq_n_f32(y, t, 0.225f);
}
#endif

/* ---- module ---------------------------------------------------------- */

static void _paperash_free(PaperAsh* a) {
	if (a->snap)
		_KeMemUnmap(a->snap, a->snapBytes);
	free(a->block);
}

void DeodhaiPaperAshInitialise() {
	for (int i = 0; i < DEODHAI_PAPERASH_MAX; i++)
		_paperash_free(&g_ash[i]);
	memset(g_ash, 0, sizeof(g_ash));
	DirtyTilesRelease(&g_tiles);
	free(g_rects);
	g_rects = NULL;
	g_rectCap = g_rectCount = 0;
}

bool DeodhaiPaperAshEligible(Window* win) {
	if (!win || !win->backBuffer || !win->sharedInfo)
		return false;
	if (win->flags & (WINDOW_FLAG_ALWAYS_ON_TOP | WINDOW_FLAG_POPUP | WINDOW_FLAG_STATIC))
		return false;
	WinSharedInfo* info = (WinSharedInfo*)win->sharedInfo;
	return !info->hide && !info->zoomed && info->width > 0 && info->height > 0 &&
		   info->width <= PAPERASH_MAX_W && info->height <= PAPERASH_MAX_W;
}

bool DeodhaiPaperAshActive() {
	for (int i = 0; i < DEODHAI_PAPERASH_MAX; i++)
		if (g_ash[i].used)
			return true;
	return false;
}

PaperAsh* DeodhaiPaperAshGet(int index) {
	if (index < 0 || index >= DEODHAI_PAPERASH_MAX || !g_ash[index].used)
		return NULL;
	return &g_ash[index];
}

/* carve the SoA arrays out of one block (grown, never shrunk) */
static bool _paperash_layout(PaperAsh* a, int n, int cols) {
	const int floats = 28; /* float arrays below */
	size_t np = ((size_t)n + 3) & ~(size_t)3; /* 4-wide loops may run past n */
	size_t bytes = np * floats * sizeof(float) + np * 4 * sizeof(int16_t) +
				   np * sizeof(int32_t) + (size_t)cols * 2 * sizeof(int32_t) + 64;
	if (a->blockBytes < bytes) {
		free(a->block);
		a->block = malloc(bytes);
		a->blockBytes = a->block ? bytes : 0;
		if (!a->block)
			return false;
	}
	memset(a->block, 0, bytes);
	float* f = (float*)a->block;
	float** arrays[floats] = {&a->detach, &a->invLife, &a->hx, &a->hy,	&a->hu,	 &a->hv,  &a->vx,
							  &a->vy,	  &a->acc,	   &a->spin, &a->flip, &a->phase, &a->wob, &a->dz,
							  &a->glow,	  &a->ox,	   &a->oy, &a->oz,	&a->rc,	 &a->rs,  &a->fl,
							  &a->k,	  &a->sx,	   &a->sy, &a->a00, &a->a01, &a->a10, &a->a11};
	for (int i = 0; i < floats; i++) {
		*arrays[i] = f;
		f += np;
	}
	int16_t* s = (int16_t*)f;
	a->bx0 = s;
	a->by0 = s + np;
	a->bx1 = s + 2 * np;
	a->by1 = s + 3 * np;
	int32_t* g = (int32_t*)(s + 4 * np);
	a->grid = g;
	a->attach = g + np;
	a->heat = g + np + cols;
	return true;
}

/* Flakes of a w x h window: break-off time grows row by row (top first),
 * with a wavy front (smooth per column delay) and per flake jitter of less
 * than one row step, so within a column every flake leaves after the one
 * above it. Counting sort by break-off time: O(n + range). */
static bool _paperash_build(PaperAsh* a) {
	const int w = a->w, h = a->h;
	int S = _paperash_ceil(sqrtf((float)w * (float)h / PAPERASH_TARGET_FLAKES));
	if (S < PAPERASH_MIN_SIZE)
		S = PAPERASH_MIN_SIZE;
	const int cols = (w + S - 1) / S, rows = (h + S - 1) / S, n = cols * rows;
	if (!_paperash_layout(a, n, cols))
		return false;
	a->S = S;
	a->cols = cols;
	a->rows = rows;
	a->n = n;

	const uint32_t seed = a->handle * 2654435761u + (uint32_t)w * 40503u + (uint32_t)h;
	const float rowStep = PAPERASH_SWEEP_MS / (float)rows;
	const float p1 = _paperash_hash(seed) * PAPERASH_TWO_PI;
	const float p2 = _paperash_hash(seed + 1) * PAPERASH_TWO_PI;
	const int range = (int)(PAPERASH_HEAT_MS + PAPERASH_SWEEP_MS + PAPERASH_RAGGED_MS + rowStep) + 2;

	/* counting sort: bucket = whole ms of the break-off time */
	int* count = (int*)calloc((size_t)range + 1, sizeof(int));
	float* when = (float*)malloc((size_t)n * sizeof(float));
	if (!count || !when) {
		free(count);
		free(when);
		return false;
	}
	for (int r = 0; r < rows; r++) {
		for (int c = 0; c < cols; c++) {
			float xc = (float)c / (float)(cols > 1 ? cols - 1 : 1);
			float wave = 0.5f + 0.3f * _paperash_sin(xc * 7.1f + p1) + 0.2f * _paperash_sin(xc * 17.3f + p2);
			float jitter = _paperash_hash(seed ^ (uint32_t)(r * 7919 + c * 104729)) * 0.9f * rowStep;
			/* + HEAT: nothing glows yet on the first frame, it is the window */
			float d = PAPERASH_HEAT_MS + (float)r * rowStep + _paperash_clamp01(wave) * PAPERASH_RAGGED_MS +
					  jitter;
			int b = (int)d;
			b = b < 0 ? 0 : (b > range - 1 ? range - 1 : b);
			when[r * cols + c] = d;
			count[b + 1]++;
		}
	}
	for (int b = 0; b < range; b++)
		count[b + 1] += count[b];
	for (int i = 0; i < n; i++) { /* stable: row order kept within a bucket */
		int b = (int)when[i];
		b = b < 0 ? 0 : (b > range - 1 ? range - 1 : b);
		a->grid[i] = count[b]++;
	}
	free(count);

	for (int i = 0; i < n; i++) {
		const int r = i / cols, c = i - r * cols;
		const int j = a->grid[i];
		const uint32_t hs = seed ^ (uint32_t)(i * 2246822519u);
		const int u0 = c * S, v0 = r * S;
		const int u1 = u0 + S < w ? u0 + S : w, v1 = v0 + S < h ? v0 + S : h;
		a->detach[j] = when[i];
		a->hx[j] = (u0 + u1) * 0.5f;
		a->hy[j] = (v0 + v1) * 0.5f;
		a->hu[j] = (u1 - u0) * 0.5f;
		a->hv[j] = (v1 - v0) * 0.5f;
		float life = PAPERASH_LIFE_MS + _paperash_hash(hs + 1) * PAPERASH_LIFE_VAR_MS;
		a->invLife[j] = 1000.0f / life; /* ages are in seconds */
		a->vx[j] = (_paperash_hash(hs + 2) - 0.5f) * 90.0f;	 /* px / s */
		a->vy[j] = 20.0f + _paperash_hash(hs + 3) * 60.0f;	 /* initial rise */
		a->acc[j] = 240.0f + _paperash_hash(hs + 4) * 320.0f; /* rises faster and faster */
		a->spin[j] = (_paperash_hash(hs + 5) - 0.5f) * 12.0f; /* rad / s */
		a->flip[j] = 2.0f + _paperash_hash(hs + 6) * 7.0f;
		a->phase[j] = _paperash_hash(hs + 7) * PAPERASH_TWO_PI;
		a->wob[j] = 2.0f + _paperash_hash(hs + 8) * 10.0f;
		a->dz[j] = _paperash_hash(hs + 9) * 70.0f; /* 3D: drifts towards the viewer */
		a->glow[j] = _paperash_hash(hs + 10);	   /* > 0.85: a spark, glows longer */
		a->bx1[j] = a->bx0[j] = 0;
	}
	free(when);
	for (int c = 0; c < cols; c++)
		a->attach[c] = a->heat[c] = 0;
	a->first = a->next = 0;
	return true;
}

bool DeodhaiPaperAshStart(Window* win, const PaperAshPlane* plane) {
	if (!DeodhaiPaperAshEligible(win))
		return false;
	for (int i = 0; i < DEODHAI_PAPERASH_MAX; i++) {
		PaperAsh* a = &g_ash[i];
		if (a->used)
			continue;
		WinSharedInfo* info = (WinSharedInfo*)win->sharedInfo;
		size_t bytes = (size_t)info->width * (size_t)info->height * 4;
		/* slot buffers are kept between animations and only grow */
		if (a->snapBytes < bytes) {
			if (a->snap)
				_KeMemUnmap(a->snap, a->snapBytes);
			a->snap = (uint32_t*)_KeMemMap(NULL, bytes, 0, 0, MEMMAP_NO_FILEDESC, 0);
			a->snapBytes = a->snap ? bytes : 0;
		}
		if (!a->snap)
			return false;
		a->handle = win->handle;
		a->x = info->x;
		a->y = info->y;
		a->w = info->width;
		a->h = info->height;
		if (!_paperash_build(a))
			return false;
		/* the shared back buffer is unmapped right after this */
		memcpy(a->snap, win->backBuffer, bytes);
		if (plane && plane->valid) {
			a->plane = *plane;
			float c[3];
			for (int k = 0; k < 3; k++)
				c[k] = plane->o[k] + plane->u[k] * a->w * 0.5f + plane->v[k] * a->h * 0.5f;
			a->depth = sqrtf(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
		} else {
			a->plane.valid = false;
			a->depth = 0.0f;
		}
		/* the window stacked right above (its own popups close with it) */
		Window* above = win->next;
		while (above && (above->flags & WINDOW_FLAG_POPUP) && above->parent == win)
			above = above->next;
		a->hasAbove = above != NULL;
		a->aboveHandle = above ? above->handle : 0;
		a->used = true;
		a->dying = 0;
		a->t = 0.0f;
		a->last = 0;
		a->projected = false;
		a->bboxValid = false;
		return true;
	}
	return false;
}

/* ---- per frame ------------------------------------------------------- */

static void _paperash_mark(int x0, int y0, int x1, int y1) {
	DirtyTilesMark(&g_tiles, x0, y0, x1 - x0, y1 - y0);
}

static void _paperash_bbox_add(PaperAsh* a, int x0, int y0, int x1, int y1) {
	if (x1 <= x0 || y1 <= y0)
		return;
	if (!a->bboxValid) {
		a->bbox.x = x0;
		a->bbox.y = y0;
		a->bbox.w = x1 - x0;
		a->bbox.h = y1 - y0;
		a->bboxValid = true;
		return;
	}
	int r = a->bbox.x + a->bbox.w, b = a->bbox.y + a->bbox.h;
	a->bbox.x = x0 < a->bbox.x ? x0 : a->bbox.x;
	a->bbox.y = y0 < a->bbox.y ? y0 : a->bbox.y;
	a->bbox.w = (x1 > r ? x1 : r) - a->bbox.x;
	a->bbox.h = (y1 > b ? y1 : b) - a->bbox.y;
}

/* motion of the flakes in [i0, i1), four per NEON step:
 *   age a (s), k = a / life
 *   ox = vx a + wob sin(phase + 5 a)      sideways drift and wobble
 *   oy = -(vy a + acc a^2 / 2)            rises faster and faster
 *   oz = -dz a                            (3D) towards the viewer
 *   spin angle s a, tumble cos(flip a), size 1 - shrink k */
static void _paperash_motion(PaperAsh* a, int i0, int i1) {
	const float t = a->t;
	int i = i0;
#if defined(ARCH_ARM64)
	const float32x4_t vt = vdupq_n_f32(t), zero = vdupq_n_f32(0.0f);
	const float32x4_t ms = vdupq_n_f32(0.001f), half = vdupq_n_f32(0.5f);
	const float32x4_t minFlip = vdupq_n_f32(PAPERASH_MIN_FLIP), one = vdupq_n_f32(1.0f);
	for (; i + 4 <= i1; i += 4) {
		float32x4_t age = vmulq_f32(vmaxq_f32(vsubq_f32(vt, vld1q_f32(a->detach + i)), zero), ms);
		float32x4_t k = vmulq_f32(age, vld1q_f32(a->invLife + i));
		float32x4_t wob = _paperash_sin4(vmlaq_n_f32(vld1q_f32(a->phase + i), age, PAPERASH_WOBBLE_HZ));
		float32x4_t ox = vmlaq_f32(vmulq_f32(vld1q_f32(a->vx + i), age), vld1q_f32(a->wob + i), wob);
		float32x4_t rise = vmlaq_f32(vld1q_f32(a->vy + i), vmulq_f32(half, vld1q_f32(a->acc + i)), age);
		float32x4_t oy = vnegq_f32(vmulq_f32(rise, age));
		float32x4_t oz = vnegq_f32(vmulq_f32(vld1q_f32(a->dz + i), age));
		float32x4_t ang = vmulq_f32(vld1q_f32(a->spin + i), age);
		float32x4_t sc = vmlsq_n_f32(one, vminq_f32(k, one), PAPERASH_SHRINK);
		float32x4_t sn = _paperash_sin4(ang);
		float32x4_t cs = _paperash_sin4(vaddq_f32(ang, vdupq_n_f32(PAPERASH_HALF_PI)));
		float32x4_t fl = _paperash_sin4(vmlaq_f32(vdupq_n_f32(PAPERASH_HALF_PI), vld1q_f32(a->flip + i), age));
		/* keep a sliver when edge on, sign kept (the back shows) */
		uint32x4_t neg = vcltq_f32(fl, zero);
		float32x4_t afl = vmaxq_f32(vabsq_f32(fl), minFlip);
		fl = vbslq_f32(neg, vnegq_f32(afl), afl);
		vst1q_f32(a->k + i, k);
		vst1q_f32(a->ox + i, ox);
		vst1q_f32(a->oy + i, oy);
		vst1q_f32(a->oz + i, oz);
		vst1q_f32(a->rc + i, vmulq_f32(cs, sc));
		vst1q_f32(a->rs + i, vmulq_f32(sn, sc));
		vst1q_f32(a->fl + i, fl);
	}
#endif
	for (; i < i1; i++) {
		float age = t - a->detach[i];
		age = (age > 0.0f ? age : 0.0f) * 0.001f;
		float k = age * a->invLife[i];
		float ang = a->spin[i] * age;
		float sc = 1.0f - (k < 1.0f ? k : 1.0f) * PAPERASH_SHRINK;
		float fl = _paperash_sin(PAPERASH_HALF_PI + a->flip[i] * age);
		float afl = fl < 0.0f ? -fl : fl;
		afl = afl > PAPERASH_MIN_FLIP ? afl : PAPERASH_MIN_FLIP;
		a->k[i] = k;
		a->ox[i] = a->vx[i] * age + a->wob[i] * _paperash_sin(a->phase[i] + PAPERASH_WOBBLE_HZ * age);
		a->oy[i] = -((a->vy[i] + 0.5f * a->acc[i] * age) * age);
		a->oz[i] = -(a->dz[i] * age);
		a->rc[i] = _paperash_sin(ang + PAPERASH_HALF_PI) * sc;
		a->rs[i] = _paperash_sin(ang) * sc;
		a->fl[i] = fl < 0.0f ? -afl : afl;
	}
}

/* screen placement of flake i: centre + local -> screen matrix. Flying
 * flakes are R(spin) * diag(tumble, 1) * size; attached ones identity. */
static bool _paperash_place(PaperAsh* a, int i, bool flying, PaperAshProjectFn project) {
	float m00 = 1.0f, m01 = 0.0f, m10 = 0.0f, m11 = 1.0f;
	float lx = a->hx[i], ly = a->hy[i], lz = 0.0f;
	if (flying) {
		m00 = a->rc[i] * a->fl[i];
		m01 = -a->rs[i];
		m10 = a->rs[i] * a->fl[i];
		m11 = a->rc[i];
		lx += a->ox[i];
		ly += a->oy[i];
		lz = a->oz[i];
	}
	if (a->projected) {
		float o[6];
		if (!project(&a->plane, lx, ly, lz, o)) {
			/* behind the viewer: a zero matrix is skipped by the drawer */
			a->a00[i] = a->a01[i] = a->a10[i] = a->a11[i] = 0.0f;
			return false;
		}
		a->sx[i] = o[0];
		a->sy[i] = o[1];
		a->a00[i] = o[2] * m00 + o[3] * m10;
		a->a01[i] = o[2] * m01 + o[3] * m11;
		a->a10[i] = o[4] * m00 + o[5] * m10;
		a->a11[i] = o[4] * m01 + o[5] * m11;
	} else {
		a->sx[i] = (float)a->x + lx;
		a->sy[i] = (float)a->y + ly;
		a->a00[i] = m00;
		a->a01[i] = m01;
		a->a10[i] = m10;
		a->a11[i] = m11;
	}
	return true;
}

/* footprint of a placed flake; returns false if it covers nothing */
static bool _paperash_footprint(const PaperAsh* a, int i, float pad, int* x0, int* y0, int* x1, int* y1) {
	float hu = a->hu[i] + pad, hv = a->hv[i] + pad;
	float ex = _paperash_absf(a->a00[i]) * hu + _paperash_absf(a->a01[i]) * hv;
	float ey = _paperash_absf(a->a10[i]) * hu + _paperash_absf(a->a11[i]) * hv;
	float fx0 = a->sx[i] - ex, fx1 = a->sx[i] + ex, fy0 = a->sy[i] - ey, fy1 = a->sy[i] + ey;
	if (!(fx1 > -16384.0f && fx0 < 16384.0f && fy1 > -16384.0f && fy0 < 16384.0f))
		return false;
	*x0 = _paperash_floor(fx0) - 1;
	*y0 = _paperash_floor(fy0) - 1;
	*x1 = _paperash_ceil(fx1) + 1;
	*y1 = _paperash_ceil(fy1) + 1;
	return true;
}

static void _paperash_store_footprint(PaperAsh* a, int i, bool has, int x0, int y0, int x1, int y1) {
	if (a->bx1[i] > a->bx0[i])
		_paperash_mark(a->bx0[i], a->by0[i], a->bx1[i], a->by1[i]);
	if (has) {
		x0 = x0 < -32000 ? -32000 : x0;
		y0 = y0 < -32000 ? -32000 : y0;
		x1 = x1 > 32000 ? 32000 : x1;
		y1 = y1 > 32000 ? 32000 : y1;
		_paperash_mark(x0, y0, x1, y1);
		_paperash_bbox_add(a, x0, y0, x1, y1);
		a->bx0[i] = (int16_t)x0;
		a->by0[i] = (int16_t)y0;
		a->bx1[i] = (int16_t)x1;
		a->by1[i] = (int16_t)y1;
	} else {
		a->bx0[i] = a->bx1[i] = 0;
	}
}

static void _paperash_step_one(PaperAsh* a, uint64_t now, PaperAshProjectFn project) {
	if (a->last == 0)
		a->last = now;
	uint64_t dt = now > a->last ? now - a->last : 0;
	a->last = now;
	a->t += (float)(dt > PAPERASH_MAX_DT ? PAPERASH_MAX_DT : dt);
	a->projected = a->plane.valid && project != NULL;
	a->bboxValid = false;
	const float t = a->t;
	const int cols = a->cols, rows = a->rows;

	/* flakes that break off now (sorted, so a pointer that only advances) */
	while (a->next < a->n && a->detach[a->next] <= t)
		a->next++;
	/* per column: attached and glowing rows (pointers only advance) */
	for (int c = 0; c < cols; c++) {
		while (a->attach[c] < rows && a->grid[a->attach[c] * cols + c] < a->next)
			a->attach[c]++;
		if (a->heat[c] < a->attach[c])
			a->heat[c] = a->attach[c];
		while (a->heat[c] < rows &&
			   a->detach[a->grid[a->heat[c] * cols + c]] - t <= PAPERASH_HEAT_MS)
			a->heat[c]++;
	}

	/* flakes in the air */
	_paperash_motion(a, a->first, a->next);
	for (int i = a->first; i < a->next; i++) {
		int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		bool has = a->k[i] < 1.0f && _paperash_place(a, i, true, project) &&
				   _paperash_footprint(a, i, 0.0f, &x0, &y0, &x1, &y1);
		_paperash_store_footprint(a, i, has, x0, y0, x1, y1);
	}
	while (a->first < a->next && a->k[a->first] >= 1.0f && a->bx1[a->first] <= a->bx0[a->first])
		a->first++;

	/* attached flakes: placed every frame (3D projection may change), and
	 * their area is repainted -- everything drawn lies inside the marks */
	for (int c = 0; c < cols; c++) {
		if (a->attach[c] >= rows)
			continue;
		if (!a->projected) {
			/* 2D: the column below the front is one rectangle */
			int u0 = c * a->S, u1 = u0 + a->S < a->w ? u0 + a->S : a->w;
			int v0 = a->attach[c] * a->S;
			_paperash_mark(a->x + u0, a->y + v0, a->x + u1, a->y + a->h);
			_paperash_bbox_add(a, a->x + u0, a->y + v0, a->x + u1, a->y + a->h);
			for (int r = a->attach[c]; r < a->heat[c]; r++) /* glowing: drawn as flakes */
				_paperash_place(a, a->grid[r * cols + c], false, project);
			continue;
		}
		for (int r = a->attach[c]; r < rows; r++) {
			int i = a->grid[r * cols + c];
			int x0, y0, x1, y1;
			if (_paperash_place(a, i, false, project) &&
				_paperash_footprint(a, i, PAPERASH_SEAM, &x0, &y0, &x1, &y1)) {
				_paperash_mark(x0, y0, x1, y1);
				_paperash_bbox_add(a, x0, y0, x1, y1);
			}
		}
	}

	bool attached = false;
	for (int c = 0; c < cols && !attached; c++)
		attached = a->attach[c] < rows;
	if (!attached && a->next == a->n && a->first == a->n)
		a->dying = 1; /* last footprint marked this frame; freed next step */
}

void DeodhaiPaperAshStep(uint64_t now, int screen_w, int screen_h, PaperAshProjectFn project) {
	if (g_tiles.w != screen_w || g_tiles.h != screen_h) {
		DirtyTilesInit(&g_tiles, screen_w, screen_h);
		int cap = DirtyTilesMaxRects(&g_tiles);
		free(g_rects);
		g_rects = (Rect*)malloc((size_t)(cap > 0 ? cap : 1) * sizeof(Rect));
		g_rectCap = g_rects ? cap : 0;
	}
	g_rectCount = 0;
	for (int i = 0; i < DEODHAI_PAPERASH_MAX; i++) {
		PaperAsh* a = &g_ash[i];
		if (!a->used)
			continue;
		if (a->dying) {
			a->used = false; /* buffers are kept for the next one */
			a->dying = 0;
			continue;
		}
		_paperash_step_one(a, now, project);
	}
}

int DeodhaiPaperAshDirty(const Rect** rects) {
	g_rectCount = g_rects ? DirtyTilesEmit(&g_tiles, g_rects, g_rectCap) : 0;
	*rects = g_rects;
	return g_rectCount;
}

/* ---- colour ---------------------------------------------------------- */

/* ember -> char -> grey ash -> gone, by k = age / life; e > 0: the flake
 * is still attached and heating up */
static void _paperash_tint_for(float k, float e, float glow, PaperAshTint* t) {
	/* ember and ash colours, b g r */
	const float EB = 24.0f, EG = 105.0f, ER = 255.0f;
	const float GB = 40.0f, GG = 41.0f, GR = 44.0f;
	float ms, ml, eAmt, gAmt, am = 1.0f;
	if (e > 0.0f) { /* heating: darkens, orange creeps in */
		ms = 1.0f - 0.50f * e;
		ml = 0.0f;
		eAmt = 0.45f * e;
		gAmt = 0.0f;
	} else {
		float kE = glow > 0.85f ? 0.55f : 0.22f; /* sparks glow longer */
		float kC = kE + 0.33f;
		if (k < kE) {
			float q = k / kE;
			ms = 0.5f - 0.4f * q;
			ml = 0.0f;
			eAmt = 0.45f + (glow > 0.85f ? 0.5f : 0.35f) * q;
			gAmt = 0.0f;
		} else if (k < kC) {
			float r = _paperash_smooth((k - kE) / (kC - kE));
			float e0 = glow > 0.85f ? 0.95f : 0.8f;
			ms = 0.1f * (1.0f - r);
			ml = 0.42f * r;
			eAmt = e0 * (1.0f - r);
			gAmt = r;
		} else {
			ms = 0.0f;
			ml = 0.42f;
			eAmt = 0.0f;
			gAmt = 1.0f;
		}
		if (k > 0.55f)
			am = 1.0f - _paperash_smooth(_paperash_clamp01((k - 0.55f) / 0.45f));
	}
	t->ms = (uint8_t)(_paperash_clamp01(ms) * 255.0f + 0.5f);
	t->ml = ml > 0.0f ? (uint8_t)(_paperash_clamp01(ml) * 255.0f + 0.5f) : 0;
	t->am = (uint8_t)(_paperash_clamp01(am) * 255.0f + 0.5f);
	float b = EB * eAmt + GB * gAmt, g = EG * eAmt + GG * gAmt, r = ER * eAmt + GR * gAmt;
	t->add[0] = (uint8_t)(b > 255.0f ? 255.0f : b);
	t->add[1] = (uint8_t)(g > 255.0f ? 255.0f : g);
	t->add[2] = (uint8_t)(r > 255.0f ? 255.0f : r);
}

static inline uint32_t _paperash_tint_pixel(uint32_t p, const PaperAshTint* t) {
	uint32_t b = p & 0xFF, g = (p >> 8) & 0xFF, r = (p >> 16) & 0xFF, a = p >> 24;
	uint32_t L = (b * 29 + g * 150 + r * 77) >> 8;
	uint32_t lm = (L * (t->ml + 1u)) >> 8;
	uint32_t ms = t->ms + 1u;
	uint32_t ob = ((b * ms) >> 8) + lm, og = ((g * ms) >> 8) + lm, orr = ((r * ms) >> 8) + lm;
	ob = (ob > 255 ? 255 : ob) + t->add[0];
	og = (og > 255 ? 255 : og) + t->add[1];
	orr = (orr > 255 ? 255 : orr) + t->add[2];
	ob = ob > 255 ? 255 : ob;
	og = og > 255 ? 255 : og;
	orr = orr > 255 ? 255 : orr;
	uint32_t oa = (a * (t->am + 1u)) >> 8;
	return (oa << 24) | (orr << 16) | (og << 8) | ob;
}

/**
 * @brief DeodhaiPaperAshTintRun -- apply a flake colour transform in place
 */
void DeodhaiPaperAshTintRun(uint32_t* run, int n, const PaperAshTint* t) {
	int i = 0;
#if defined(ARCH_ARM64)
	static const uint8_t wts[16] = {29, 150, 77, 0, 29, 150, 77, 0, 29, 150, 77, 0, 29, 150, 77, 0};
	/* luma byte of pixel p into its b, g, r bytes; 16 = out of range -> 0 */
	static const uint8_t spread[16] = {0, 0, 0, 16, 1, 1, 1, 16, 2, 2, 2, 16, 3, 3, 3, 16};
	const uint8x16_t vw = vld1q_u8(wts), vspread = vld1q_u8(spread);
	const uint8_t mulb[16] = {t->ms, t->ms, t->ms, t->am, t->ms, t->ms, t->ms, t->am,
							  t->ms, t->ms, t->ms, t->am, t->ms, t->ms, t->ms, t->am};
	const uint8_t addb[16] = {t->add[0], t->add[1], t->add[2], 0, t->add[0], t->add[1], t->add[2], 0,
							  t->add[0], t->add[1], t->add[2], 0, t->add[0], t->add[1], t->add[2], 0};
	const uint8x16_t vmul = vld1q_u8(mulb), vadd = vld1q_u8(addb);
	const uint8x8_t vml = vdup_n_u8(t->ml);
	for (; i + 4 <= n; i += 4) {
		uint8x16_t px = vld1q_u8((const uint8_t*)(run + i));
		/* luma: weighted bytes, two pairwise adds -> L0..L3 (x256) */
		uint16x8_t wl = vmull_u8(vget_low_u8(px), vget_low_u8(vw));
		uint16x8_t wh = vmull_high_u8(px, vw);
		uint16x8_t s1 = vpaddq_u16(wl, wh);
		uint16x8_t s2 = vpaddq_u16(s1, s1);
		uint8x8_t L = vshrn_n_u16(s2, 8);			   /* L0 L1 L2 L3 L0 L1 L2 L3 */
		uint8x8_t Lm = vshrn_n_u16(vaddw_u8(vmull_u8(L, vml), L), 8); /* L * (ml + 1) >> 8 */
		uint8x16_t Ls = vqtbl1q_u8(vcombine_u8(Lm, Lm), vspread);
		/* c * (m + 1) >> 8 per byte; the alpha byte uses am */
		uint16x8_t lo = vaddw_u8(vmull_u8(vget_low_u8(px), vget_low_u8(vmul)), vget_low_u8(px));
		uint16x8_t hi = vaddw_high_u8(vmull_high_u8(px, vmul), px);
		uint8x16_t c = vcombine_u8(vshrn_n_u16(lo, 8), vshrn_n_u16(hi, 8));
		c = vqaddq_u8(vqaddq_u8(c, Ls), vadd);
		vst1q_u8((uint8_t*)(run + i), c);
	}
#endif
	for (; i < n; i++)
		run[i] = _paperash_tint_pixel(run[i], t);
}

/* ---- drawing ---------------------------------------------------------- */

/* gather `n` texels along a span: (U, V) 16.16 texel position, steps
 * dU, dV, clamped to the window; four indices per NEON step */
static void _paperash_gather(const PaperAsh* a, uint32_t* out, int32_t U, int32_t V, int32_t dU, int32_t dV, int n) {
	const int wmax = a->w - 1, hmax = a->h - 1, stride = a->w;
	const uint32_t* snap = a->snap;
	int i = 0;
#if defined(ARCH_ARM64)
	static const int32_t steps[4] = {0, 1, 2, 3};
	const int32x4_t vs = vld1q_s32(steps);
	int32x4_t vu = vmlaq_n_s32(vdupq_n_s32(U), vs, dU);
	int32x4_t vv = vmlaq_n_s32(vdupq_n_s32(V), vs, dV);
	const int32x4_t du4 = vdupq_n_s32(dU * 4), dv4 = vdupq_n_s32(dV * 4);
	const int32x4_t zero = vdupq_n_s32(0), vw = vdupq_n_s32(wmax), vh = vdupq_n_s32(hmax);
	for (; i + 4 <= n; i += 4) {
		int32x4_t iu = vminq_s32(vmaxq_s32(vshrq_n_s32(vu, 16), zero), vw);
		int32x4_t iv = vminq_s32(vmaxq_s32(vshrq_n_s32(vv, 16), zero), vh);
		int32x4_t idx = vmlaq_n_s32(iu, iv, stride);
		out[i] = snap[vgetq_lane_s32(idx, 0)];
		out[i + 1] = snap[vgetq_lane_s32(idx, 1)];
		out[i + 2] = snap[vgetq_lane_s32(idx, 2)];
		out[i + 3] = snap[vgetq_lane_s32(idx, 3)];
		vu = vaddq_s32(vu, du4);
		vv = vaddq_s32(vv, dv4);
	}
	U += dU * i;
	V += dV * i;
#endif
	for (; i < n; i++) {
		int iu = U >> 16, iv = V >> 16;
		iu = iu < 0 ? 0 : (iu > wmax ? wmax : iu);
		iv = iv < 0 ? 0 : (iv > hmax ? hmax : iv);
		out[i] = snap[iv * stride + iu];
		U += dU;
		V += dV;
	}
}

static inline int32_t _paperash_fixed(float v) {
	float s = v * 65536.0f;
	s = s > 2.0e9f ? 2.0e9f : (s < -2.0e9f ? -2.0e9f : s);
	return (int32_t)(s < 0.0f ? s - 0.5f : s + 0.5f);
}

/* a flake's pixels are gathered row after row into one buffer, tinted in
 * one go and then blended row by row */
#define PAPERASH_BATCH		1024
#define PAPERASH_BATCH_ROWS 128

typedef struct _paperash_batch_ {
	uint32_t px[PAPERASH_BATCH];
	uint32_t* dst[PAPERASH_BATCH_ROWS];
	int len[PAPERASH_BATCH_ROWS];
	int count, rows;
} PaperAshBatch;

static void _paperash_flush(PaperAshBatch* b, const PaperAshTint* tint) {
	if (tint && b->count)
		DeodhaiPaperAshTintRun(b->px, b->count, tint);
	int o = 0;
	for (int r = 0; r < b->rows; r++) {
		__pixel_blend_neon(b->dst[r], b->px + o, b->len[r]);
		o += b->len[r];
	}
	b->count = b->rows = 0;
}

/* one flake: an octagon (square of half size hu x hv with corners cut at
 * |u| + |v| <= c) mapped by the 2x2 matrix to the screen. Each of the four
 * slabs |g X + h dy| <= b bounds X per row by -|b/g| - (h/g) dy and
 * +|b/g| - (h/g) dy: the coefficients are set up once per flake, a row costs
 * two NEON multiply-adds and a horizontal max / min. Slabs with g = 0 (an
 * unrotated flake's top and bottom) only limit the rows. */
static void _paperash_draw_flake(ChCanvas* canvas, const PaperAsh* a, int i, float pad, float cut,
								 const PaperAshTint* tint, int cx0, int cy0, int cx1, int cy1) {
	const float a00 = a->a00[i], a01 = a->a01[i], a10 = a->a10[i], a11 = a->a11[i];
	const float det = a00 * a11 - a01 * a10;
	if (det < 1e-4f && det > -1e-4f)
		return;
	const float id = 1.0f / det;
	/* screen offset -> flake local: (u, v) = M (dx, dy) */
	const float m00 = a11 * id, m01 = -a01 * id, m10 = -a10 * id, m11 = a00 * id;
	const float hu = a->hu[i] + pad, hv = a->hv[i] + pad;
	const float c = cut * (hu < hv ? hu : hv) + (hu > hv ? hu : hv); /* |u| + |v| bound */
	const float cxs = a->sx[i], cys = a->sy[i];
	int x0, y0, x1, y1;
	if (!_paperash_footprint(a, i, pad, &x0, &y0, &x1, &y1))
		return;
	const float g[4] = {m00, m10, m00 + m10, m00 - m10};
	const float h[4] = {m01, m11, m01 + m11, m01 - m11};
	const float bnd[4] = {hu, hv, c, c};
	float L[4], U[4], S[4];
	float dyLo = -1e9f, dyHi = 1e9f;
	for (int k = 0; k < 4; k++) {
		if (g[k] > 1e-6f || g[k] < -1e-6f) {
			float ig = 1.0f / g[k];
			float e = _paperash_absf(bnd[k] * ig);
			L[k] = -e;
			U[k] = e;
			S[k] = -h[k] * ig;
		} else {
			L[k] = -1e9f;
			U[k] = 1e9f;
			S[k] = 0.0f;
			float ah = _paperash_absf(h[k]);
			if (ah > 1e-9f) {
				float r = bnd[k] / ah;
				dyLo = -r > dyLo ? -r : dyLo;
				dyHi = r < dyHi ? r : dyHi;
			}
		}
	}
	if (dyLo > -1e8f) {
		int r0 = _paperash_ceil(cys + dyLo - 0.5f);
		y0 = r0 > y0 ? r0 : y0;
	}
	if (dyHi < 1e8f) {
		int r1 = _paperash_floor(cys + dyHi - 0.5f) + 1;
		y1 = r1 < y1 ? r1 : y1;
	}
	y0 = y0 > cy0 ? y0 : cy0;
	y1 = y1 < cy1 ? y1 : cy1;
	if (y0 >= y1)
		return;
#if defined(ARCH_ARM64)
	const float32x4_t vL = vld1q_f32(L), vU = vld1q_f32(U), vS = vld1q_f32(S);
#endif
	static PaperAshBatch batch; /* single compositor thread */
	PaperAshBatch* b = &batch;
	b->count = b->rows = 0;
	const int stride = (int)canvas->canvasWidth;
	const int32_t dU = _paperash_fixed(m00), dV = _paperash_fixed(m10);
	for (int y = y0; y < y1; y++) {
		const float dy = (float)y + 0.5f - cys;
#if defined(ARCH_ARM64)
		const float lo = vmaxvq_f32(vmlaq_n_f32(vL, vS, dy));
		const float hi = vminvq_f32(vmlaq_n_f32(vU, vS, dy));
#else
		float lo = L[0] + S[0] * dy, hi = U[0] + S[0] * dy;
		for (int k = 1; k < 4; k++) {
			float l = L[k] + S[k] * dy, r = U[k] + S[k] * dy;
			lo = l > lo ? l : lo;
			hi = r < hi ? r : hi;
		}
#endif
		if (lo > hi)
			continue;
		/* pixel centres x + 0.5 - cx inside [lo, hi] */
		int xs = _paperash_ceil(cxs + lo - 0.5f), xe = _paperash_floor(cxs + hi - 0.5f) + 1;
		xs = xs > cx0 ? xs : cx0;
		xe = xe < cx1 ? xe : cx1;
		if (xs >= xe)
			continue;
		const float dx = (float)xs + 0.5f - cxs;
		/* texel = flake centre + (u, v); step one screen pixel along x */
		int32_t Uf = _paperash_fixed(a->hx[i] + m00 * dx + m01 * dy);
		int32_t Vf = _paperash_fixed(a->hy[i] + m10 * dx + m11 * dy);
		uint32_t* drow = canvas->buffer + (size_t)y * stride;
		for (int x = xs; x < xe;) {
			int n = xe - x;
			if (n > PAPERASH_BATCH)
				n = PAPERASH_BATCH;
			if (b->count + n > PAPERASH_BATCH || b->rows == PAPERASH_BATCH_ROWS)
				_paperash_flush(b, tint);
			_paperash_gather(a, b->px + b->count, Uf, Vf, dU, dV, n);
			b->dst[b->rows] = drow + x;
			b->len[b->rows++] = n;
			b->count += n;
			Uf += dU * n;
			Vf += dV * n;
			x += n;
		}
	}
	_paperash_flush(b, tint);
}

/* 2D: the attached part below the front, row by row; columns whose front
 * is above the row merge into one NEON blend straight from the snapshot */
static void _paperash_draw_attached_2d(ChCanvas* canvas, const PaperAsh* a, int cx0, int cy0, int cx1, int cy1) {
	const int stride = (int)canvas->canvasWidth, S = a->S;
	int top = a->h;
	for (int c = 0; c < a->cols; c++) {
		int v = a->heat[c] * S;
		top = v < top ? v : top;
	}
	int ys = a->y + top > cy0 ? a->y + top : cy0;
	int ye = a->y + a->h < cy1 ? a->y + a->h : cy1;
	for (int y = ys; y < ye; y++) {
		const int v = y - a->y;
		const int row = v / S;
		uint32_t* drow = canvas->buffer + (size_t)y * stride;
		const uint32_t* srow = a->snap + (size_t)v * a->w;
		int c = 0;
		while (c < a->cols) {
			while (c < a->cols && a->heat[c] > row)
				c++;
			if (c >= a->cols)
				break;
			int c1 = c;
			while (c1 < a->cols && a->heat[c1] <= row)
				c1++;
			int u0 = c * S, u1 = c1 * S < a->w ? c1 * S : a->w;
			int xs = a->x + u0 > cx0 ? a->x + u0 : cx0;
			int xe = a->x + u1 < cx1 ? a->x + u1 : cx1;
			if (xe > xs)
				__pixel_blend_neon(drow + xs, srow + (xs - a->x), xe - xs);
			c = c1;
		}
	}
}

static void _paperash_draw_one(ChCanvas* canvas, const PaperAsh* a, int cx0, int cy0, int cx1, int cy1) {
	const int cols = a->cols, rows = a->rows;
	PaperAshTint tint;
	/* attached part */
	if (!a->projected)
		_paperash_draw_attached_2d(canvas, a, cx0, cy0, cx1, cy1);
	for (int c = 0; c < cols; c++) {
		int rEnd = a->projected ? rows : a->heat[c];
		for (int r = a->attach[c]; r < rEnd; r++) {
			int i = a->grid[r * cols + c];
			float e = 1.0f - (a->detach[i] - a->t) / PAPERASH_HEAT_MS;
			const PaperAshTint* tp = NULL;
			if (e > 0.0f) {
				_paperash_tint_for(0.0f, e > 1.0f ? 1.0f : e, a->glow[i], &tint);
				tp = &tint;
			}
			/* 3D: plain or glowing through the projection, squares overlap
			 * by a fraction of a texel so the seams do not crack; 2D: the
			 * glowing row, exact squares */
			_paperash_draw_flake(canvas, a, i, a->projected ? PAPERASH_SEAM : 0.0f, 1.0f, tp, cx0, cy0, cx1, cy1);
		}
	}
	/* flakes in the air: oldest (highest) first */
	for (int i = a->first; i < a->next; i++) {
		if (a->k[i] >= 1.0f || a->bx1[i] <= a->bx0[i])
			continue;
		_paperash_tint_for(a->k[i], 0.0f, a->glow[i], &tint);
		if (tint.am == 0)
			continue;
		/* corners chip off over the first moments of the flight */
		float cut = 1.0f - 0.55f * _paperash_clamp01(a->k[i] * 6.0f) * (0.6f + 0.4f * a->glow[i]);
		_paperash_draw_flake(canvas, a, i, 0.0f, cut, &tint, cx0, cy0, cx1, cy1);
	}
}

void DeodhaiPaperAshDraw(ChCanvas* canvas, const Rect* clip) {
	int cx0 = 0, cy0 = 0, cx1 = (int)canvas->canvasWidth, cy1 = (int)canvas->canvasHeight;
	if (clip) {
		cx0 = clip->x > cx0 ? clip->x : cx0;
		cy0 = clip->y > cy0 ? clip->y : cy0;
		cx1 = clip->x + clip->w < cx1 ? clip->x + clip->w : cx1;
		cy1 = clip->y + clip->h < cy1 ? clip->y + clip->h : cy1;
	}
	if (cx0 >= cx1 || cy0 >= cy1)
		return;
	for (int i = 0; i < DEODHAI_PAPERASH_MAX; i++)
		if (DeodhaiPaperAshDrawn(&g_ash[i]))
			_paperash_draw_one(canvas, &g_ash[i], cx0, cy0, cx1, cy1);
}

void DeodhaiPaperAshDrawOne(ChCanvas* canvas, int index, const Rect* clip) {
	if (index < 0 || index >= DEODHAI_PAPERASH_MAX || !DeodhaiPaperAshDrawn(&g_ash[index]))
		return;
	int cx0 = 0, cy0 = 0, cx1 = (int)canvas->canvasWidth, cy1 = (int)canvas->canvasHeight;
	if (clip) {
		cx0 = clip->x > cx0 ? clip->x : cx0;
		cy0 = clip->y > cy0 ? clip->y : cy0;
		cx1 = clip->x + clip->w < cx1 ? clip->x + clip->w : cx1;
		cy1 = clip->y + clip->h < cy1 ? clip->y + clip->h : cy1;
	}
	if (cx0 < cx1 && cy0 < cy1)
		_paperash_draw_one(canvas, &g_ash[index], cx0, cy0, cx1, cy1);
}
