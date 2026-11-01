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

#include "compose3d.h"
#include "compose.h"
#include "cursor.h"
#include "xr_present.h"
#include "paperfly.h"
#include "paperash.h"
#include "dirtytiles.h"
#include "nanojpg.h"
#include <keycode.h>
#include <color.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/_kefile.h>
#include <_xeneva.h>
#include <sys/_ketime.h>
#if defined(ARCH_ARM64)
#include <arm_neon.h>
#endif

/* ---- provided by main.cpp ------------------------------------------- */
extern Window* _get_root_window();
extern Window* _get_always_on_top();
extern Window* _get_focused_window();
extern Cursor* _get_current_cursor();
extern void _take_update_bits(bool* window_update_all, bool* always_on_top_update);
extern void _set_always_on_top_update(bool update);
extern void XRComposeFrame(ChCanvas* canvas);
extern void CursorStoreBack(ChCanvas* canv, Cursor* cur, unsigned x, unsigned y);
extern void CursorDrawBack(ChCanvas* canv, Cursor* cur, unsigned x, unsigned y);
extern void DeodhaiWindowSetFocused(Window* win, bool notify);
extern void DeodhaiWindowShowNow(Window* win);
extern void DeodhaiSendMouseEvent(
	int handle, int ownerId, uint8_t handleType, uint8_t eventType, int x, int y, int button);

#define DEODHAI3D_PI		  3.14159265f
#define DEODHAI3D_TWO_PI	  6.28318531f
#define DEODHAI3D_FOV_X		  1.57079633f /* 90 degrees horizontal */
#define DEODHAI3D_PITCH_LIMIT 0.52359878f /* 30 degrees */
#define DEODHAI3D_NEAR		  0.05f		  /* near plane, fraction of focal */
#define DEODHAI3D_MIN_DIST	  0.25f		  /* closest a window may come, x focal */
#define DEODHAI3D_MAX_DIST	  8.0f		  /* farthest, x focal */
#define DEODHAI3D_DEPTH_SPEED 0.004f	  /* Ctrl+drag: distance change per pixel */
#define DEODHAI3D_DEPTH_STEP  1.25f		  /* Ctrl+'-' / Ctrl+'=' distance factor */
#define DEODHAI3D_DEPTH_GAP	  1.12f		  /* send-to-back / bring-to-front spacing */
#define DEODHAI3D_DEPTH_EASE  0.25f		  /* fraction of the remaining depth per frame */
#define DEODHAI3D_DEN_EPS	  1e-3f
#define DEODHAI3D_TITLEBAR	  26 /* same hit zone as DeodhaiWindowCheckDraggable */
#define DEODHAI3D_CURSOR_SIZE 24
#define DEODHAI3D_SUBSPAN	  16 /* perspective-correct samples every N pixels */
#define DEODHAI3D_DRAG_LOD	  1	 /* half-resolution panorama while turning */
#define DEODHAI3D_HASH_SIZE	  256 /* power of two, > 2 x DEODHAI3D_MAX_WINDOWS */
#define DEODHAI3D_EXACT_REGIONS 32 /* exact rectangles per frame, then the tile map */

#define DEODHAI3D_DRAG_NONE	  0
#define DEODHAI3D_DRAG_CAMERA 1
#define DEODHAI3D_DRAG_WINDOW 2
#define DEODHAI3D_DRAG_TEAR	  3

/* tearing: shake over a window, press near the shake point and pull */
#define DEODHAI3D_MAX_TEARS		   8
#define DEODHAI3D_TEAR_SAMPLES	   128	/* edge profile resolution along the crack */
#define DEODHAI3D_SHAKE_REVERSALS  4	/* direction changes that make a shake */
#define DEODHAI3D_SHAKE_WINDOW_MS  700	/* ... within this time */
#define DEODHAI3D_SHAKE_MIN_TRAVEL 10	/* px travelled between two reversals */
#define DEODHAI3D_SHAKE_MAX_SPREAD 140	/* the shake stays inside this box */
#define DEODHAI3D_SHAKE_COOLDOWN_MS 800 /* a continued wiggle is still one shake */
#define DEODHAI3D_TEAR_ARM_MS	   1500 /* time to press after shaking */
#define DEODHAI3D_TEAR_ARM_RADIUS  56	/* press this close (screen px) to the slit */
#define DEODHAI3D_TEAR_SEED_LEN	   10.0f /* slit shown when armed (texels) */
#define DEODHAI3D_TEAR_MIN_LEN	   6.0f	 /* shorter than this on release: heals */
#define DEODHAI3D_TEAR_HEAL		   0.80f /* opening kept per frame while healing */
#define DEODHAI3D_TEAR_MAX_WIDTH   70.0f /* widest half-opening (texels) */

#define DEODHAI3D_TEAR_NONE	   0
#define DEODHAI3D_TEAR_HOLE	   1
#define DEODHAI3D_TEAR_SHADOW  2 /* hole close to the edge: darkened backdrop */
#define DEODHAI3D_TEAR_RIM	   3 /* torn paper fibre along the edge */
#define DEODHAI3D_TEAR_SHIFTED 4 /* material pushed aside by the opening */

typedef struct _vec3_ {
	float x;
	float y;
	float z;
} Vec3;

/* a * X + b * Y + c, evaluated at screen pixel centres (X = x + 0.5) */
typedef struct _lin3_ {
	float a;
	float b;
	float c;
} Lin3;

/* A crack from the anchor A to the tip T in window texels. The opening is
 * lens shaped (zero at both ends), edges are jagged by noise sampled at
 * the absolute distance along the crack so they stay put while it grows.
 * The material beside the opening is compressed outward over `band`
 * texels -- the window's own pixels look torn apart, not erased. */
/* one profile sample; interleaved so a lookup touches one or two lines */
typedef struct _tear_sample_ {
	float center; /* crack line wander */
	float smooth; /* lens half-width the paper bends to */
	float slack;  /* beyond smooth + slack: plain pushed-aside paper */
	float edgeL;  /* ragged torn edges */
	float edgeR;
	float rimL; /* fibre rim widths */
	float rimR;
	float pad;
} TearSample;

typedef struct _tear3d_ {
	bool used;
	bool armed;	  /* shaken, waiting for the press */
	bool torn;	  /* pulled open: the window takes no input */
	bool healing; /* closing animation, freed when gone */
	bool dirty;
	uint64_t armedAt;
	uint32_t seed;
	float ax, ay;
	float tx, ty;
	float scale; /* opening, 1 -> 0 while healing */
	/* derived by _compose3d_tear_build */
	float dirx, diry, len, invStep, band, invBand;
	float maxAcross; /* nothing farther than this from the crack line is touched */
	float box[4]; /* local bounds x0, y0, x1, y1 */
	TearSample sample[DEODHAI3D_TEAR_SAMPLES];
} Tear3D;

typedef struct _win3d_ {
	Window* win;
	uint32_t handle;
	bool used;
	bool seen;
	bool visible;
	bool front;		/* plane faces the camera */
	bool drawable;	/* front, past the near plane, overlaps the screen */
	bool popup;		/* pose derived from the parent this frame */
	bool unitScale; /* screen aligned at 1:1 -> rows are straight copies */
	bool ackDamage;
	bool ackEntire;
	bool depthAnimating;
	float targetDist; /* eased depth target (Ctrl+'-', send to back, ...) */
	Vec3 center;	  /* world position of the window centre */
	float facing;	  /* yaw of the plane normal */
	int w;
	int h;
	/* camera-space plane and the screen -> window pixel mapping */
	Vec3 camO;
	Vec3 camU;
	Vec3 camV;
	Lin3 nu;
	Lin3 nv;
	Lin3 den;
	float sortKey; /* distance from the viewer: rotation invariant */
	Rect bbox;
	/* state as of the last composed frame, for change detection */
	bool lastVisible;
	bool lastDrawable;
	Rect lastBBox;
	/* tear (index into g_tears, -1 = none) and its screen bounds */
	int tear;
	Rect tearBox;
	bool tearBoxValid;
	Rect lastTearBox;
	bool lastTearValid;
	Vec3 lastCenter;
	float lastFacing;
	int lastW;
	int lastH;
} Win3D;

static struct {
	bool ready;
	bool enabled;
	bool needSync;
	ChCanvas* canvas;
	int W;
	int H;
	float cx;
	float cy;
	float f;
	/* camera */
	float yaw;
	float pitch;
	float shear;
	float sinYaw;
	float cosYaw;
	bool cameraDirty;
	/* panorama texture */
	uint32_t* pano;
	int panoW;
	int panoH;
	float panoSpan;
	bool panoMirror;
	bool panoOwned;
	uint32_t* flat; /* the flat wallpaper, restores 2D mode */
	int32_t* colU;	/* per column texel offset relative to the yaw texel */
	float* colK;	/* per column texels per unit of screen y */
	int32_t* colTex; /* per column final texel column for this frame */
	/* frame bookkeeping */
	bool wasZoomed;
	bool blurStale;
	bool hudRefresh;
	/* input */
	int dragMode;
	Win3D* dragEntry;
	uint32_t dragHandle;
	float grabU;
	float grabV;
	float grabR;
	int lastX;
	int lastY;
	int lastButton;
	Window* hovered;
	uint32_t hoveredHandle;
	/* shake detector: ring of recent direction reversals */
	uint64_t now;
	int shakeX, shakeY;
	int shakeDirX, shakeDirY;
	int shakeTravelX, shakeTravelY;
	int shakeSumX, shakeSumY;
	uint64_t shakeTime[8];
	int shakePX[8], shakePY[8];
	int shakeHead;
	int shakeCX, shakeCY; /* centre of the last detected shake */
	uint64_t shakeQuietUntil;
	bool shakeHorizontal;
} g3d;

static Win3D g_win3d[DEODHAI3D_MAX_WINDOWS];
static Win3D* g_order[DEODHAI3D_MAX_WINDOWS];
static Win3D* g_orderTmp[DEODHAI3D_MAX_WINDOWS];
static int g_orderCount;
/* repaint regions: up to DEODHAI3D_EXACT_REGIONS exact rectangles; a busier
 * frame switches to the tile map, emitted once before drawing */
static Rect g_regionStore[DEODHAI3D_MAX_REGIONS];
static Rect* g_regions = g_regionStore;
static int g_regionCap = DEODHAI3D_MAX_REGIONS;
static DirtyTiles g_tiles;
static bool g_tilesMode;
static Tear3D g_tears[DEODHAI3D_MAX_TEARS];
static int g_regionCount;
/* Window* -> slot, open addressing: slot + 1, 0 = empty, -1 = deleted */
static int16_t g_hash[DEODHAI3D_HASH_SIZE];
static int g_hashDeleted;

/* ---- small math ----------------------------------------------------- */

static inline float _compose3d_clampf(float v, float lo, float hi) {
	return v < lo ? lo : (v > hi ? hi : v);
}

static inline float _compose3d_absf(float v) {
	return v < 0.0f ? -v : v;
}

/* float -> int floor/ceil for values already clamped to a sane range */
static inline int _compose3d_floori(float v) {
	v = _compose3d_clampf(v, -1.0e6f, 1.0e6f);
	int i = (int)v;
	return (v < (float)i) ? i - 1 : i;
}

static inline int _compose3d_ceili(float v) {
	v = _compose3d_clampf(v, -1.0e6f, 1.0e6f);
	int i = (int)v;
	return (v > (float)i) ? i + 1 : i;
}

static inline float _compose3d_dot(const Vec3* a, const Vec3* b) {
	return a->x * b->x + a->y * b->y + a->z * b->z;
}

static inline float _compose3d_length(const Vec3* a) {
	return sqrtf(_compose3d_dot(a, a));
}

/* p + s * d */
static inline Vec3 _compose3d_madd(const Vec3* p, float s, const Vec3* d) {
	Vec3 r = {p->x + s * d->x, p->y + s * d->y, p->z + s * d->z};
	return r;
}

static inline Vec3 _compose3d_to_camera(const Vec3* w) {
	Vec3 c = {g3d.cosYaw * w->x - g3d.sinYaw * w->z, w->y, g3d.sinYaw * w->x + g3d.cosYaw * w->z};
	return c;
}

static inline Vec3 _compose3d_to_world(const Vec3* c) {
	Vec3 w = {g3d.cosYaw * c->x + g3d.sinYaw * c->z, c->y, -g3d.sinYaw * c->x + g3d.cosYaw * c->z};
	return w;
}

/* window plane axes: U = right, V = down, N = U x V (points away from a
 * window that faces the camera) */
static void _compose3d_axes(float facing, Vec3* U, Vec3* V, Vec3* N) {
	float s = sinf(facing), c = cosf(facing);
	U->x = c;
	U->y = 0.0f;
	U->z = -s;
	V->x = 0.0f;
	V->y = 1.0f;
	V->z = 0.0f;
	N->x = s;
	N->y = 0.0f;
	N->z = c;
}

/* d(X,Y) . W  with  d = (X - cx, Y - cy - shear, f) */
static inline Lin3 _compose3d_lin(const Vec3* W) {
	Lin3 l = {W->x, W->y, -g3d.cx * W->x - (g3d.cy + g3d.shear) * W->y + g3d.f * W->z};
	return l;
}

static inline float _compose3d_eval(const Lin3* l, float X, float Y) {
	return l->a * X + l->b * Y + l->c;
}

static bool _compose3d_project(const Vec3* c, float* sx, float* sy) {
	if (c->z < DEODHAI3D_NEAR * g3d.f)
		return false;
	*sx = g3d.cx + g3d.f * c->x / c->z;
	*sy = g3d.cy + g3d.shear + g3d.f * c->y / c->z;
	return true;
}

/* world ray through the centre of screen pixel (x, y), unit length */
static Vec3 _compose3d_ray_world(int x, int y) {
	Vec3 c = {(x + 0.5f) - g3d.cx, (y + 0.5f) - g3d.cy - g3d.shear, g3d.f};
	float len = _compose3d_length(&c);
	c.x /= len;
	c.y /= len;
	c.z /= len;
	return _compose3d_to_world(&c);
}

/* ---- screen regions to repaint -------------------------------------- */

static bool _compose3d_rect_touch(const Rect* a, const Rect* b) {
	return a->x <= b->x + b->w && b->x <= a->x + a->w && a->y <= b->y + b->h &&
		   b->y <= a->y + a->h;
}

static bool _compose3d_rect_overlap(const Rect* a, int x, int y, int w, int h) {
	return a->x < x + w && x < a->x + a->w && a->y < y + h && y < a->y + a->h;
}

static Rect _compose3d_rect_union(const Rect* a, const Rect* b) {
	int l = a->x < b->x ? a->x : b->x;
	int t = a->y < b->y ? a->y : b->y;
	int r = (a->x + a->w) > (b->x + b->w) ? (a->x + a->w) : (b->x + b->w);
	int bt = (a->y + a->h) > (b->y + b->h) ? (a->y + a->h) : (b->y + b->h);
	Rect u = {l, t, r - l, bt - t};
	return u;
}

static bool _compose3d_rect_equal(const Rect* a, const Rect* b) {
	return a->x == b->x && a->y == b->y && a->w == b->w && a->h == b->h;
}

/* Few rectangles: the transitive merge of AddDirtyClip, bounded by
 * DEODHAI3D_EXACT_REGIONS (O(1) per insert). More than that in one frame:
 * everything goes to the tile map -- O(tiles touched) per insert, and one
 * O(tiles / 64 + rectangles) bit scan before drawing. */
static void _compose3d_add_region(int x, int y, int w, int h) {
	if (x < 0) {
		w += x;
		x = 0;
	}
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (w > g3d.W - x)
		w = g3d.W - x;
	if (h > g3d.H - y)
		h = g3d.H - y;
	if (w <= 0 || h <= 0)
		return;
	if (g_tilesMode) {
		DirtyTilesMark(&g_tiles, x, y, w, h);
		return;
	}

	Rect in = {x, y, w, h};
	for (int i = 0; i < g_regionCount;) {
		if (!_compose3d_rect_touch(&in, &g_regions[i])) {
			i++;
			continue;
		}
		in = _compose3d_rect_union(&in, &g_regions[i]);
		g_regions[i] = g_regions[--g_regionCount];
		i = 0;
	}
	int exact = DEODHAI3D_EXACT_REGIONS < g_regionCap ? DEODHAI3D_EXACT_REGIONS : g_regionCap;
	if (g_regionCount < exact) {
		g_regions[g_regionCount++] = in;
		return;
	}
	if (g_tiles.bits) {
		for (int i = 0; i < g_regionCount; i++)
			DirtyTilesMark(&g_tiles, g_regions[i].x, g_regions[i].y, g_regions[i].w, g_regions[i].h);
		DirtyTilesMark(&g_tiles, in.x, in.y, in.w, in.h);
		g_regionCount = 0;
		g_tilesMode = true;
		return;
	}
	for (int i = 0; i < g_regionCount; i++) /* no tile map: collapse */
		in = _compose3d_rect_union(&in, &g_regions[i]);
	g_regions[0] = in;
	g_regionCount = 1;
}

/* the regions of this frame, ready to draw */
static void _compose3d_finish_regions() {
	if (!g_tilesMode)
		return;
	g_regionCount = DirtyTilesEmit(&g_tiles, g_regions, g_regionCap);
	g_tilesMode = false;
}

static void _compose3d_reset_regions() {
	if (g_tilesMode)
		DirtyTilesEmit(&g_tiles, g_regions, g_regionCap); /* clears the map */
	g_tilesMode = false;
	g_regionCount = 0;
}

/* ---- panorama ------------------------------------------------------- */

/* O(W): rebuilt only when the texture or the canvas changes */
static void _compose3d_build_tables() {
	float ppr = (float)g3d.panoW / g3d.panoSpan; /* texels per radian */
	for (int x = 0; x < g3d.W; x++) {
		float px = (x + 0.5f) - g3d.cx;
		g3d.colU[x] = (int32_t)_compose3d_floori(atanf(px / g3d.f) * ppr);
		g3d.colK[x] = ppr / sqrtf(px * px + g3d.f * g3d.f);
	}
}

static void _compose3d_set_texture(
	uint32_t* tex, int w, int h, float span, bool mirror, bool owned) {
	if (g3d.panoOwned && g3d.pano && g3d.pano != tex)
		_KeMemUnmap(g3d.pano, (size_t)g3d.panoW * (size_t)g3d.panoH * 4);
	g3d.pano = tex;
	g3d.panoW = w;
	g3d.panoH = h;
	/* span >= FOV keeps every column offset inside one wrap period, so the
	 * wrap below is a single compare */
	g3d.panoSpan = span < DEODHAI3D_FOV_X ? DEODHAI3D_FOV_X : span;
	g3d.panoMirror = mirror;
	g3d.panoOwned = owned;
	_compose3d_build_tables();
	g3d.cameraDirty = true;
}

/* O(W) per frame: wrap / mirror resolved once per column, so the per
 * pixel work is one multiply-add, a clamp and a gather */
static void _compose3d_build_column_texels() {
	const int tw = g3d.panoW;
	const int period = g3d.panoMirror ? 2 * tw : tw;
	const float ppr = (float)tw / g3d.panoSpan;
	int yawU = _compose3d_floori(g3d.yaw * ppr) % period;
	if (yawU < 0)
		yawU += period;
	for (int x = 0; x < g3d.W; x++) {
		int u = yawU + g3d.colU[x];
		if (u < 0)
			u += period;
		else if (u >= period)
			u -= period;
		if (u >= tw) /* mirrored half, only reachable when period = 2 * tw */
			u = period - 1 - u;
		g3d.colTex[x] = u;
	}
}

/* one panorama row; four columns per step with NEON */
static void _compose3d_panorama_row(uint32_t* row, float py) {
	const int W = g3d.W, tw = g3d.panoW, th = g3d.panoH;
	const uint32_t* tex = g3d.pano;
	const float vc = th * 0.5f;
	int x = 0;
#if defined(ARCH_ARM64)
	const float32x4_t vvc = vdupq_n_f32(vc);
	const int32x4_t vzero = vdupq_n_s32(0);
	const int32x4_t vlast = vdupq_n_s32(th - 1);
	const int32x4_t vtw = vdupq_n_s32(tw);
	int32_t idx[4];
	for (; x <= W - 4; x += 4) {
		float32x4_t vf = vmlaq_n_f32(vvc, vld1q_f32(g3d.colK + x), py);
		int32x4_t vi = vminq_s32(vmaxq_s32(vcvtq_s32_f32(vf), vzero), vlast);
		vst1q_s32(idx, vmlaq_s32(vld1q_s32(g3d.colTex + x), vi, vtw));
		row[x] = tex[idx[0]];
		row[x + 1] = tex[idx[1]];
		row[x + 2] = tex[idx[2]];
		row[x + 3] = tex[idx[3]];
	}
#endif
	for (; x < W; x++) {
		int v = (int)(vc + py * g3d.colK[x]);
		if (v < 0)
			v = 0;
		else if (v >= th)
			v = th - 1;
		row[x] = tex[(size_t)v * (size_t)tw + g3d.colTex[x]];
	}
}

/* O(W*H), O(1) per pixel. Runs only when the camera moved; the result
 * lives in the back surface where every 2D restore path reads it. `mirror`
 * receives a copy of every row while it is still in cache (the canvas on a
 * full frame), `lod` renders 2x2 blocks while the camera is being turned. */
static void _compose3d_render_panorama(uint32_t* dst, uint32_t* mirror, bool lod) {
	const int W = g3d.W, H = g3d.H;
	const size_t rowBytes = (size_t)W * 4;
	_compose3d_build_column_texels();
	if (!lod) {
		for (int y = 0; y < H; y++) {
			uint32_t* row = dst + (size_t)y * W;
			_compose3d_panorama_row(row, (y + 0.5f) - g3d.cy - g3d.shear);
			if (mirror)
				memcpy(mirror + (size_t)y * W, row, rowBytes);
		}
		return;
	}
	const int tw = g3d.panoW, th = g3d.panoH;
	const float vc = th * 0.5f;
	for (int y = 0; y < H; y += 2) {
		const float py = (y + 1.0f) - g3d.cy - g3d.shear;
		uint32_t* row = dst + (size_t)y * W;
		for (int x = 0; x < W; x += 2) {
			int v = (int)(vc + py * g3d.colK[x]);
			if (v < 0)
				v = 0;
			else if (v >= th)
				v = th - 1;
			uint32_t p = g3d.pano[(size_t)v * (size_t)tw + g3d.colTex[x]];
			row[x] = p;
			if (x + 1 < W)
				row[x + 1] = p;
		}
		if (y + 1 < H)
			memcpy(row + W, row, rowBytes);
		if (mirror) {
			memcpy(mirror + (size_t)y * W, row, rowBytes);
			if (y + 1 < H)
				memcpy(mirror + (size_t)(y + 1) * W, row, rowBytes);
		}
	}
}

static void* _compose3d_jpeg_alloc(uint32_t sz) {
	return malloc(sz);
}

static void _compose3d_jpeg_free(void* p) {
	free(p);
}

/**
 * @brief Deodhai3DLoadPanorama -- load a 360 degree cylindrical panorama
 */
bool Deodhai3DLoadPanorama(const char* path) {
	if (!g3d.ready || !path)
		return false;
	int fd = _KeOpenFile((char*)path, FILE_OPEN_READ_ONLY);
	if (fd < 0)
		return false;
	XEFileStatus stat;
	memset(&stat, 0, sizeof(XEFileStatus));
	_KeFileStat(fd, &stat);
	if (stat.size == 0) {
		_KeCloseFile(fd);
		return false;
	}
	size_t alloc = ((size_t)stat.size + 4095) & ~(size_t)4095;
	uint8_t* data = (uint8_t*)_KeMemMap(NULL, alloc, 0, 0, MEMMAP_NO_FILEDESC, 0);
	if (!data) {
		_KeCloseFile(fd);
		return false;
	}
	memset(data, 0, alloc);
	_KeReadFile(fd, data, alloc);

	bool ok = false;
	Jpeg::Decoder* dec =
		new Jpeg::Decoder(data, (uint32_t)alloc, _compose3d_jpeg_alloc, _compose3d_jpeg_free);
	if (dec && dec->GetResult() == Jpeg::Decoder::OK && dec->IsColor()) {
		int w = dec->GetWidth(), h = dec->GetHeight();
		const uint8_t* rgb = dec->GetImage();
		uint32_t* tex = NULL;
		if (w > 0 && h > 0)
			tex = (uint32_t*)_KeMemMap(NULL, (size_t)w * h * 4, 0, 0, MEMMAP_NO_FILEDESC, 0);
		if (tex) {
			for (size_t i = 0; i < (size_t)w * h; i++)
				tex[i] = 0xFF000000u | ((uint32_t)rgb[i * 3] << 16) |
						 ((uint32_t)rgb[i * 3 + 1] << 8) | rgb[i * 3 + 2];
			_compose3d_set_texture(tex, w, h, DEODHAI3D_TWO_PI, false, true);
			ok = true;
		}
	} else {
		_KePrint("[deodhaiXR]: 3D panorama %s not usable, keeping wallpaper\r\n", path);
	}
	delete dec;
	_KeMemUnmap(data, alloc);
	_KeCloseFile(fd);
	return ok;
}

/* ---- window entries: O(1) lookup by Window* --------------------------- */

static inline uint32_t _compose3d_hash_of(const Window* w) {
	uintptr_t p = (uintptr_t)w;
	return (uint32_t)(((p >> 4) ^ (p >> 20)) * 2654435761u) & (DEODHAI3D_HASH_SIZE - 1);
}

static void _compose3d_hash_rebuild() {
	memset(g_hash, 0, sizeof(g_hash));
	g_hashDeleted = 0;
	for (int i = 0; i < DEODHAI3D_MAX_WINDOWS; i++) {
		if (!g_win3d[i].used)
			continue;
		uint32_t h = _compose3d_hash_of(g_win3d[i].win);
		while (g_hash[h] > 0)
			h = (h + 1) & (DEODHAI3D_HASH_SIZE - 1);
		g_hash[h] = (int16_t)(i + 1);
	}
}

static void _compose3d_hash_insert(int slot) {
	uint32_t h = _compose3d_hash_of(g_win3d[slot].win);
	while (g_hash[h] > 0)
		h = (h + 1) & (DEODHAI3D_HASH_SIZE - 1);
	if (g_hash[h] < 0)
		g_hashDeleted--;
	g_hash[h] = (int16_t)(slot + 1);
}

static void _compose3d_hash_remove(int slot) {
	uint32_t h = _compose3d_hash_of(g_win3d[slot].win);
	for (int n = 0; n < DEODHAI3D_HASH_SIZE && g_hash[h] != 0; n++) {
		if (g_hash[h] == slot + 1) {
			g_hash[h] = -1;
			g_hashDeleted++;
			break;
		}
		h = (h + 1) & (DEODHAI3D_HASH_SIZE - 1);
	}
	if (g_hashDeleted > DEODHAI3D_HASH_SIZE / 4)
		_compose3d_hash_rebuild();
}

/* pointer-only lookup; entries are pruned to live windows by sync */
static Win3D* _compose3d_find_ptr(Window* win) {
	if (!win)
		return NULL;
	uint32_t h = _compose3d_hash_of(win);
	for (int n = 0; n < DEODHAI3D_HASH_SIZE && g_hash[h] != 0; n++) {
		if (g_hash[h] > 0 && g_win3d[g_hash[h] - 1].win == win)
			return &g_win3d[g_hash[h] - 1];
		h = (h + 1) & (DEODHAI3D_HASH_SIZE - 1);
	}
	return NULL;
}

/* Place a window where it sits on screen right now: at depth f, facing
 * the view direction, it projects 1:1 onto its 2D rectangle. */
static void _compose3d_place_from_2d(Win3D* e) {
	WinSharedInfo* info = (WinSharedInfo*)e->win->sharedInfo;
	Vec3 c = {(info->x + info->width * 0.5f) - g3d.cx,
			  (info->y + info->height * 0.5f) - g3d.cy - g3d.shear,
			  g3d.f};
	e->center = _compose3d_to_world(&c);
	e->facing = g3d.yaw;
}

static Win3D* _compose3d_create(Window* win) {
	for (int i = 0; i < DEODHAI3D_MAX_WINDOWS; i++) {
		if (g_win3d[i].used)
			continue;
		Win3D* e = &g_win3d[i];
		memset(e, 0, sizeof(Win3D));
		e->used = true;
		e->win = win;
		e->handle = win->handle;
		e->tear = -1;
		_compose3d_place_from_2d(e);
		_compose3d_hash_insert(i);
		return e;
	}
	return NULL; /* table full: the window is simply not shown in 3D */
}

/* forget an entry; its last footprint gets repainted */
static void _compose3d_drop(Win3D* e) {
	if (e->lastVisible && e->lastDrawable)
		_compose3d_add_region(e->lastBBox.x, e->lastBBox.y, e->lastBBox.w, e->lastBBox.h);
	if (g3d.dragEntry == e)
		g3d.dragMode = DEODHAI3D_DRAG_NONE;
	if (e->tear >= 0)
		g_tears[e->tear].used = false;
	e->tear = -1;
	_compose3d_hash_remove((int)(e - g_win3d));
	e->used = false;
	e->win = NULL;
}

/* Prune entries to the live window list and create entries for new
 * windows. O(N) with the hash table. */
static void _compose3d_sync_entries() {
	for (int i = 0; i < DEODHAI3D_MAX_WINDOWS; i++)
		g_win3d[i].seen = false;
	for (Window* w = _get_root_window(); w != NULL; w = w->next) {
		Win3D* e = _compose3d_find_ptr(w);
		if (e && e->handle != w->handle) { /* address reused by a new window */
			_compose3d_drop(e);
			e = NULL;
		}
		if (!e)
			e = _compose3d_create(w);
		if (e)
			e->seen = true;
	}
	for (int i = 0; i < DEODHAI3D_MAX_WINDOWS; i++)
		if (g_win3d[i].used && !g_win3d[i].seen)
			_compose3d_drop(&g_win3d[i]);
	g3d.needSync = false;
}

/* the window list only changes between frames (postbox handling), so input
 * events and API calls sync at most once per frame */
static void _compose3d_sync_if_needed() {
	if (g3d.needSync)
		_compose3d_sync_entries();
}

/* O(1) for normal windows (the hash mirrors the window list once synced),
 * the short always-on-top list is walked */
static bool _compose3d_window_alive(Window* win, uint32_t handle) {
	if (!win)
		return false;
	_compose3d_sync_if_needed();
	Win3D* e = _compose3d_find_ptr(win);
	if (e && e->handle == handle)
		return true;
	for (Window* w = _get_always_on_top(); w != NULL; w = w->next)
		if (w == win && w->handle == handle)
			return true;
	return false;
}

/* Build the camera-space plane, the screen -> window mapping and the
 * projected bounds.
 *   ray d(X,Y) = (X - cx, Y - cy - shear, f), plane P = O + u U + v V
 *   t = (O.N)/(d.N),  u = t (d.U) - O.U,  v = t (d.V) - O.V
 * so u = nu(X,Y) / den(X,Y) with nu, nv, den linear in X and Y. */
static void _compose3d_build_mapping(Win3D* e) {
	Vec3 U, V, N;
	_compose3d_axes(e->facing, &U, &V, &N);
	float hw = e->w * 0.5f, hh = e->h * 0.5f;
	Vec3 O = {e->center.x - hw * U.x - hh * V.x,
			  e->center.y - hw * U.y - hh * V.y,
			  e->center.z - hw * U.z - hh * V.z};
	e->camO = _compose3d_to_camera(&O);
	e->camU = _compose3d_to_camera(&U);
	e->camV = _compose3d_to_camera(&V);
	Vec3 camN = _compose3d_to_camera(&N);

	float On = _compose3d_dot(&e->camO, &camN);
	float OU = _compose3d_dot(&e->camO, &e->camU);
	float OV = _compose3d_dot(&e->camO, &e->camV);
	Lin3 lU = _compose3d_lin(&e->camU);
	Lin3 lV = _compose3d_lin(&e->camV);
	Lin3 lN = _compose3d_lin(&camN);
	e->nu.a = On * lU.a - OU * lN.a;
	e->nu.b = On * lU.b - OU * lN.b;
	e->nu.c = On * lU.c - OU * lN.c;
	e->nv.a = On * lV.a - OV * lN.a;
	e->nv.b = On * lV.b - OV * lN.b;
	e->nv.c = On * lV.c - OV * lN.c;
	e->den = lN;
	if (!e->popup)
		e->sortKey = _compose3d_length(&e->center);

	/* plane parallel to the screen at 1:1: den constant, v constant per
	 * row and u stepping exactly one texel per pixel. Tolerances are drift
	 * across a whole screen row / column (1e-3 texel). */
	float D = e->den.c;
	float span = (float)(g3d.W > g3d.H ? g3d.W : g3d.H);
	float tol = 1e-3f * D / span;
	/*  u step = nu.a / D(row), D(row) = den.b * Y + den.c
	 *  v step = nv.a / D(row)
	 *  u error from D varying along a row ~ w * |den.a| * span / D */
	e->unitScale = D > DEODHAI3D_DEN_EPS && _compose3d_absf(e->den.a) * (float)e->w <= tol &&
				   _compose3d_absf(e->nv.a) <= tol &&
				   _compose3d_absf(e->nu.a - D) + _compose3d_absf(e->den.b) * span <= tol;

	e->front = On > DEODHAI3D_DEN_EPS;
	e->drawable = false;
	if (!e->front || e->w <= 0 || e->h <= 0)
		return;

	float minx = 1e30f, miny = 1e30f, maxx = -1e30f, maxy = -1e30f;
	for (int k = 0; k < 4; k++) {
		Vec3 p = _compose3d_madd(&e->camO, (k & 1) ? (float)e->w : 0.0f, &e->camU);
		p = _compose3d_madd(&p, (k & 2) ? (float)e->h : 0.0f, &e->camV);
		float sx, sy;
		if (!_compose3d_project(&p, &sx, &sy))
			return; /* crosses the near plane: culled */
		minx = sx < minx ? sx : minx;
		maxx = sx > maxx ? sx : maxx;
		miny = sy < miny ? sy : miny;
		maxy = sy > maxy ? sy : maxy;
	}
	int x0 = _compose3d_floori(minx), y0 = _compose3d_floori(miny);
	int x1 = _compose3d_ceili(maxx), y1 = _compose3d_ceili(maxy);
	if (x0 < 0)
		x0 = 0;
	if (y0 < 0)
		y0 = 0;
	if (x1 > g3d.W)
		x1 = g3d.W;
	if (y1 > g3d.H)
		y1 = g3d.H;
	if (x1 <= x0 || y1 <= y0)
		return;
	e->bbox.x = x0;
	e->bbox.y = y0;
	e->bbox.w = x1 - x0;
	e->bbox.h = y1 - y0;
	e->drawable = true;
}

/* screen bounds of a window-local rectangle (client damage) */
static bool _compose3d_local_rect_bounds(const Win3D* e, int lx, int ly, int lw, int lh, Rect* out) {
	float minx = 1e30f, miny = 1e30f, maxx = -1e30f, maxy = -1e30f;
	for (int k = 0; k < 4; k++) {
		Vec3 p = _compose3d_madd(&e->camO, (float)(lx + ((k & 1) ? lw : 0)), &e->camU);
		p = _compose3d_madd(&p, (float)(ly + ((k & 2) ? lh : 0)), &e->camV);
		float sx, sy;
		if (!_compose3d_project(&p, &sx, &sy))
			return false;
		minx = sx < minx ? sx : minx;
		maxx = sx > maxx ? sx : maxx;
		miny = sy < miny ? sy : miny;
		maxy = sy > maxy ? sy : maxy;
	}
	out->x = _compose3d_floori(minx);
	out->y = _compose3d_floori(miny);
	out->w = _compose3d_ceili(maxx) - out->x;
	out->h = _compose3d_ceili(maxy) - out->y;
	return true;
}

static bool _compose3d_screen_to_local(const Win3D* e, float X, float Y, float* u, float* v) {
	if (!e->drawable)
		return false;
	float D = _compose3d_eval(&e->den, X, Y);
	if (D <= DEODHAI3D_DEN_EPS)
		return false;
	*u = _compose3d_eval(&e->nu, X, Y) / D;
	*v = _compose3d_eval(&e->nv, X, Y) / D;
	return *u >= 0.0f && *u < (float)e->w && *v >= 0.0f && *v < (float)e->h;
}

/* Popups ride on the parent's plane at their 2D offset from it, nudged
 * half a unit towards the camera and always drawn just after it. */
static void _compose3d_derive_popup(Win3D* e, const Win3D* parent) {
	WinSharedInfo* pinfo = (WinSharedInfo*)e->win->sharedInfo;
	WinSharedInfo* parentInfo = (WinSharedInfo*)parent->win->sharedInfo;
	Vec3 U, V, N;
	_compose3d_axes(parent->facing, &U, &V, &N);
	float dx = (float)(pinfo->x - parentInfo->x);
	float dy = (float)(pinfo->y - parentInfo->y);
	Vec3 c = parent->center;
	c = _compose3d_madd(&c, dx - parent->w * 0.5f + e->w * 0.5f, &U);
	c = _compose3d_madd(&c, dy - parent->h * 0.5f + e->h * 0.5f, &V);
	c = _compose3d_madd(&c, -0.5f, &N);
	e->center = c;
	e->facing = parent->facing;
	e->sortKey = parent->sortKey - 0.001f;
}

/* ease the window along its view ray towards targetDist: it stays under
 * the same screen spot and only grows / shrinks */
static void _compose3d_animate_depth(Win3D* e) {
	if (!e->depthAnimating)
		return;
	float d = _compose3d_length(&e->center);
	if (d <= 0.0f) {
		e->depthAnimating = false;
		return;
	}
	float nd = d + (e->targetDist - d) * DEODHAI3D_DEPTH_EASE;
	if (_compose3d_absf(e->targetDist - nd) < 0.5f) {
		nd = e->targetDist;
		e->depthAnimating = false;
	}
	float s = nd / d;
	e->center.x *= s;
	e->center.y *= s;
	e->center.z *= s;
}

/* CAS snapshot of the client damage list: an append that races with the
 * copy makes the CAS fail and we copy again, so nothing is lost. */
static uint32_t _compose3d_take_damage(WinSharedInfo* info, Rect* out) {
	uint32_t raw = __atomic_load_n(&info->rect_count, __ATOMIC_ACQUIRE);
	for (;;) {
		uint32_t n = raw > 256 ? 256 : raw;
		memcpy(out, info->rect, n * sizeof(Rect));
		if (__atomic_compare_exchange_n(
				&info->rect_count, &raw, 0, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
			return n;
	}
}

/* ack after the pixels were read (BLEED clients wait on this), re-ringing
 * the doorbell if the client queued more damage meanwhile */
static void _compose3d_ack_damage(WinSharedInfo* info) {
	__atomic_store_n(&info->dirty, false, __ATOMIC_SEQ_CST);
	if (__atomic_load_n(&info->rect_count, __ATOMIC_SEQ_CST))
		__atomic_store_n(&info->dirty, true, __ATOMIC_RELEASE);
}


/* ---- tearing -------------------------------------------------------- */

static inline float _compose3d_hash11(uint32_t x) {
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return (float)(x & 0xFFFFFFu) * (2.0f / 16777215.0f) - 1.0f;
}

/* smooth 1D value noise in [-1, 1] */
static float _compose3d_noise1(uint32_t seed, float p) {
	int i = _compose3d_floori(p);
	float f = p - (float)i;
	float a = _compose3d_hash11(seed ^ ((uint32_t)i * 0x9E3779B1u));
	float b = _compose3d_hash11(seed ^ ((uint32_t)(i + 1) * 0x9E3779B1u));
	f = f * f * (3.0f - 2.0f * f);
	return a + (b - a) * f;
}

static int _compose3d_tear_alloc() {
	for (int i = 0; i < DEODHAI3D_MAX_TEARS; i++) {
		if (g_tears[i].used)
			continue;
		memset(&g_tears[i], 0, sizeof(Tear3D));
		g_tears[i].used = true;
		return i;
	}
	return -1;
}

/* edge profile, O(DEODHAI3D_TEAR_SAMPLES); runs only when the tear changed.
 * Noise is sampled at the absolute distance along the crack (texels), so
 * the ragged pattern stays put while the tear grows:
 *   centre : slow wander of the crack line
 *   smooth : lens half-width the paper is bent aside by (no jag, so the
 *            content beside the crack curves instead of zigzagging)
 *   edges  : smooth width x low-frequency irregularity + fine fibre fuzz
 *   rims   : fibre rim width, itself noisy */
static void _compose3d_tear_build(Tear3D* t) {
	const int S = DEODHAI3D_TEAR_SAMPLES;
	float dx = t->tx - t->ax, dy = t->ty - t->ay;
	float len = sqrtf(dx * dx + dy * dy);
	if (len >= 1.0f) {
		t->dirx = dx / len;
		t->diry = dy / len;
	} else {
		len = 1.0f; /* keep the previous direction */
	}
	t->len = len;
	t->invStep = (float)(S - 1) / len;
	float wmax = 0.16f * len + 1.5f;
	if (wmax > DEODHAI3D_TEAR_MAX_WIDTH)
		wmax = DEODHAI3D_TEAR_MAX_WIDTH;
	wmax *= t->scale;
	t->band = 2.0f * wmax + 6.0f * t->scale + 0.5f;
	t->invBand = 1.0f / t->band;

	float reach = 0.0f;
	for (int i = 0; i < S; i++) {
		float s = (float)i / (float)(S - 1);
		float d = s * len;
		float pr = sinf(DEODHAI3D_PI * s);
		if (pr < 0.0f) /* sinf(pi) is a hair below zero: sqrtf would be NaN */
			pr = 0.0f;
		pr = 0.5f * (pr + sqrtf(pr)); /* fuller lens than a plain sine */
		float ws = wmax * pr;
		float fuzz = t->scale * (pr > 0.15f ? 1.0f : pr / 0.15f); /* fibres fade at the tips */
		TearSample* sp = &t->sample[i];
		sp->smooth = ws;
		sp->center = (0.12f * ws + 0.6f * fuzz) *
						   _compose3d_noise1(t->seed ^ 0x77u, d * 0.035f) +
					   0.5f * fuzz * _compose3d_noise1(t->seed ^ 0x3Bu, d * 0.12f);
		float l = ws * (1.0f + 0.22f * _compose3d_noise1(t->seed ^ 0x51u, d * 0.08f) +
						0.12f * _compose3d_noise1(t->seed ^ 0x93u, d * 0.25f)) +
				  0.9f * fuzz * _compose3d_noise1(t->seed ^ 0x2Du, d * 0.9f);
		float r = ws * (1.0f + 0.22f * _compose3d_noise1(t->seed ^ 0xA3u, d * 0.08f) +
						0.12f * _compose3d_noise1(t->seed ^ 0xC7u, d * 0.25f)) +
				  0.9f * fuzz * _compose3d_noise1(t->seed ^ 0x6Fu, d * 0.9f);
		sp->edgeL = l > 0.0f ? l : 0.0f;
		sp->edgeR = r > 0.0f ? r : 0.0f;
		sp->rimL = fuzz * (1.6f + 0.9f * _compose3d_noise1(t->seed ^ 0x11u, d * 0.7f));
		sp->rimR = fuzz * (1.6f + 0.9f * _compose3d_noise1(t->seed ^ 0x13u, d * 0.7f));
		float edge = sp->edgeL > sp->edgeR ? sp->edgeL : sp->edgeR;
		float rimw = sp->rimL > sp->rimR ? sp->rimL : sp->rimR;
		sp->slack = edge - ws + rimw;
		if (sp->slack < 0.0f)
			sp->slack = 0.0f;
		float e = _compose3d_absf(sp->center) + edge;
		if (e > reach)
			reach = e;
	}
	/* a pixel between two samples sees the larger slack of either */
	for (int i = 0; i < S - 1; i++)
		t->sample[i].pad = t->sample[i].slack > t->sample[i + 1].slack ? t->sample[i].slack
																	   : t->sample[i + 1].slack;
	t->sample[S - 1].pad = t->sample[S - 1].slack;
	float m = reach + t->band + 2.0f;
	t->maxAcross = m;
	t->box[0] = (t->ax < t->tx ? t->ax : t->tx) - m;
	t->box[1] = (t->ay < t->ty ? t->ay : t->ty) - m;
	t->box[2] = (t->ax > t->tx ? t->ax : t->tx) + m;
	t->box[3] = (t->ay > t->ty ? t->ay : t->ty) + m;
}

#define DEODHAI3D_TEAR_SHADOW_PX 4.0f /* soft shadow inside the opening */

/* classify a window texel by its crack coordinates. For material beside
 * the opening, (*su, *sv) is the texel that got pushed there; *aux is the
 * shadow opacity (SHADOW) or rim strength (RIM), 0..1 */
static inline int _compose3d_tear_classify(
	const Tear3D* t, float along, float across, float* su, float* sv, float* aux) {
	if (along <= 0.0f || along >= t->len || across > t->maxAcross || across < -t->maxAcross)
		return DEODHAI3D_TEAR_NONE;
	float fi = along * t->invStep;
	int i = (int)fi;
	if (i > DEODHAI3D_TEAR_SAMPLES - 2)
		i = DEODHAI3D_TEAR_SAMPLES - 2;
	float fr = fi - (float)i;
	const TearSample* s0 = &t->sample[i];
	const TearSample* s1 = s0 + 1;
#define DEODHAI3D_LERP(f) (s0->f + (s1->f - s0->f) * fr)
	float c = DEODHAI3D_LERP(center);
	float ws = DEODHAI3D_LERP(smooth);
	float rel = across - c;
	float arel = rel < 0.0f ? -rel : rel;
	float o = arel - ws;
	if (o >= t->band)
		return DEODHAI3D_TEAR_NONE;
	if (o > s0->pad) {
		/* common case: plain paper pushed aside, no edge or rim nearby */
		float src = c + (rel < 0.0f ? -o : o) * (t->band + ws) * t->invBand;
		*su = t->ax + along * t->dirx - src * t->diry;
		*sv = t->ay + along * t->diry + src * t->dirx;
		return DEODHAI3D_TEAR_SHIFTED;
	}
	float l = DEODHAI3D_LERP(edgeL);
	float r = DEODHAI3D_LERP(edgeR);
	if (rel > -l && rel < r) {
		/* inside the opening: shadow fades in from both torn edges */
		float edge = (rel + l) < (r - rel) ? (rel + l) : (r - rel);
		if (edge >= DEODHAI3D_TEAR_SHADOW_PX)
			return DEODHAI3D_TEAR_HOLE;
		*aux = 1.0f - edge * (1.0f / DEODHAI3D_TEAR_SHADOW_PX);
		return DEODHAI3D_TEAR_SHADOW;
	}
	float src, rim;
	if (rel >= r) {
		src = c + (o > 0.0f ? o * (t->band + ws) * t->invBand : 0.0f);
		rim = DEODHAI3D_LERP(rimR) - (rel - r);
	} else {
		src = c - (o > 0.0f ? o * (t->band + ws) * t->invBand : 0.0f);
		rim = DEODHAI3D_LERP(rimL) - (-l - rel);
	}
#undef DEODHAI3D_LERP
	*su = t->ax + along * t->dirx - src * t->diry;
	*sv = t->ay + along * t->diry + src * t->dirx;
	if (rim > 0.0f) {
		*aux = rim > 1.0f ? 1.0f : rim;
		return DEODHAI3D_TEAR_RIM;
	}
	return DEODHAI3D_TEAR_SHIFTED;
}

static inline int _compose3d_tear_map(const Tear3D* t, float u, float v, float* su, float* sv) {
	if (u < t->box[0] || u > t->box[2] || v < t->box[1] || v > t->box[3])
		return DEODHAI3D_TEAR_NONE;
	float du = u - t->ax, dv = v - t->ay, aux;
	return _compose3d_tear_classify(
		t, du * t->dirx + dv * t->diry, dv * t->dirx - du * t->diry, su, sv, &aux);
}

/* towards paper fibre white, up to 70% at the very edge */
static inline uint32_t _compose3d_tear_rim(uint32_t p, float strength) {
	uint32_t k = (uint32_t)(strength * 179.0f); /* 0..179 of 256 */
	uint32_t ik = 256 - k;
	uint32_t r = (((p >> 16) & 0xFF) * ik + 0xF4 * k) >> 8;
	uint32_t g = (((p >> 8) & 0xFF) * ik + 0xF0 * k) >> 8;
	uint32_t b = ((p & 0xFF) * ik + 0xE8 * k) >> 8;
	return 0xFF000000u | (r << 16) | (g << 8) | b;
}

/* one texel through a tear (scalar): untouched, opening (transparent),
 * soft shadow (translucent black), rim (towards fibre white) or the texel
 * pushed aside into this spot */
static inline uint32_t _compose3d_tear_pixel(const Tear3D* t,
											 float along,
											 float across,
											 int32_t Uj,
											 int32_t Vj,
											 const uint32_t* src,
											 int ww,
											 int wh) {
	float su, sv, aux = 0.0f;
	int kind = _compose3d_tear_classify(t, along, across, &su, &sv, &aux);
	if (kind == DEODHAI3D_TEAR_NONE)
		return src[(size_t)(Vj >> 16) * (size_t)ww + (Uj >> 16)];
	if (kind == DEODHAI3D_TEAR_HOLE)
		return 0;
	if (kind == DEODHAI3D_TEAR_SHADOW)
		return (uint32_t)(aux * 110.0f) << 24;
	int u = su >= 0.0f ? (su < (float)ww ? (int)su : ww - 1) : 0; /* NaN -> 0 */
	int v = sv >= 0.0f ? (sv < (float)wh ? (int)sv : wh - 1) : 0;
	uint32_t p = src[(size_t)v * (size_t)ww + u];
	return kind == DEODHAI3D_TEAR_RIM ? _compose3d_tear_rim(p, aux) : p;
}

/* Texels for a run of n (<= DEODHAI3D_SUBSPAN) pixels stepping (U, V) in
 * 16.16 through a tear. Crack coordinates are computed four lanes at a
 * time with NEON; holes come out fully transparent (the NEON blender
 * skips them, so whatever is behind shows) and the edge shadow as
 * translucent black. */
static bool _compose3d_tear_run(const Tear3D* t,
								int32_t U,
								int32_t V,
								int32_t dU,
								int32_t dV,
								int n,
								const uint32_t* src,
								int ww,
								int wh,
								uint32_t* out) {
	float along[DEODHAI3D_SUBSPAN], across[DEODHAI3D_SUBSPAN];
	const float k = 1.0f / 65536.0f;
	/* the run is a straight segment in window space and the tear only
	 * touches the convex strip 0 < along < len, |across| < maxAcross:
	 * both ends off the same side -> nothing in between is torn */
	{
		float u0 = U * k - t->ax, v0 = V * k - t->ay;
		float u1 = (U + (n - 1) * dU) * k - t->ax, v1 = (V + (n - 1) * dV) * k - t->ay;
		float a0 = u0 * t->dirx + v0 * t->diry, a1 = u1 * t->dirx + v1 * t->diry;
		float c0 = v0 * t->dirx - u0 * t->diry, c1 = v1 * t->dirx - u1 * t->diry;
		if ((a0 <= 0.0f && a1 <= 0.0f) || (a0 >= t->len && a1 >= t->len) ||
			(c0 >= t->maxAcross && c1 >= t->maxAcross) ||
			(c0 <= -t->maxAcross && c1 <= -t->maxAcross))
			return false; /* untouched: the caller keeps its fast path */
	}
	int i = 0;
#if defined(ARCH_ARM64)
	{
		int32_t lu[4] = {U, U + dU, U + 2 * dU, U + 3 * dU};
		int32_t lv[4] = {V, V + dV, V + 2 * dV, V + 3 * dV};
		int32x4_t vU = vld1q_s32(lu), vV = vld1q_s32(lv);
		const int32x4_t sU = vdupq_n_s32(4 * dU), sV = vdupq_n_s32(4 * dV);
		const float32x4_t vax = vdupq_n_f32(t->ax), vay = vdupq_n_f32(t->ay);
		for (; i + 4 <= n; i += 4) {
			float32x4_t fu = vsubq_f32(vmulq_n_f32(vcvtq_f32_s32(vU), k), vax);
			float32x4_t fv = vsubq_f32(vmulq_n_f32(vcvtq_f32_s32(vV), k), vay);
			vst1q_f32(along + i, vmlaq_n_f32(vmulq_n_f32(fu, t->dirx), fv, t->diry));
			vst1q_f32(across + i, vmlsq_n_f32(vmulq_n_f32(fv, t->dirx), fu, t->diry));
			vU = vaddq_s32(vU, sU);
			vV = vaddq_s32(vV, sV);
		}
	}
#endif
	for (; i < n; i++) {
		float fu = (float)(U + i * dU) * k - t->ax;
		float fv = (float)(V + i * dV) * k - t->ay;
		along[i] = fu * t->dirx + fv * t->diry;
		across[i] = fv * t->dirx - fu * t->diry;
	}
	int j = 0;
#if defined(ARCH_ARM64)
	/* four pixels at a time for the common cases -- untouched texels and
	 * paper pushed aside away from any edge; a group that meets the torn
	 * edge, the rim or the opening is finished by the scalar code below */
	{
		const float32x4_t zero = vdupq_n_f32(0.0f), vlen = vdupq_n_f32(t->len);
		const float32x4_t vmaxA = vdupq_n_f32(t->maxAcross), vband = vdupq_n_f32(t->band);
		const int32x4_t vlastI = vdupq_n_s32(DEODHAI3D_TEAR_SAMPLES - 2), vzeroI = vdupq_n_s32(0);
		const int32x4_t vww1 = vdupq_n_s32(ww - 1), vwh1 = vdupq_n_s32(wh - 1), vww = vdupq_n_s32(ww);
		for (; j + 4 <= n; j += 4) {
			float32x4_t va = vld1q_f32(along + j), vc = vld1q_f32(across + j);
			uint32x4_t none = vorrq_u32(vorrq_u32(vcleq_f32(va, zero), vcgeq_f32(va, vlen)),
										vcgeq_f32(vabsq_f32(vc), vmaxA));
			float32x4_t fi = vmulq_n_f32(va, t->invStep);
			int32x4_t vi = vminq_s32(vmaxq_s32(vcvtq_s32_f32(fi), vzeroI), vlastI);
			float32x4_t fr = vsubq_f32(fi, vcvtq_f32_s32(vi));
			int32_t li[4];
			float c0[4], c1[4], w0[4], w1[4], pd[4];
			vst1q_s32(li, vi);
			for (int q = 0; q < 4; q++) {
				const TearSample* a = &t->sample[li[q]];
				c0[q] = a[0].center;
				c1[q] = a[1].center;
				w0[q] = a[0].smooth;
				w1[q] = a[1].smooth;
				pd[q] = a[0].pad;
			}
			float32x4_t vc0 = vld1q_f32(c0), vw0 = vld1q_f32(w0);
			float32x4_t cc = vmlaq_f32(vc0, vsubq_f32(vld1q_f32(c1), vc0), fr);
			float32x4_t ws = vmlaq_f32(vw0, vsubq_f32(vld1q_f32(w1), vw0), fr);
			float32x4_t rel = vsubq_f32(vc, cc);
			float32x4_t o = vsubq_f32(vabsq_f32(rel), ws);
			uint32x4_t far = vorrq_u32(none, vcgeq_f32(o, vband)); /* untouched texel */
			uint32x4_t shifted = vcgtq_f32(o, vld1q_f32(pd));
			uint32x4_t easy = vorrq_u32(far, shifted);
			if (vminvq_u32(easy) == 0) { /* an edge is near: these four go scalar */
				for (int q = 0; q < 4; q++)
					out[j + q] = _compose3d_tear_pixel(
						t, along[j + q], across[j + q], U + (j + q) * dU, V + (j + q) * dV, src, ww, wh);
				continue;
			}
			/* src = c +- o * (band + ws) / band, sign of rel */
			float32x4_t mag = vmulq_n_f32(vmulq_f32(o, vaddq_f32(vband, ws)), t->invBand);
			float32x4_t srcv = vaddq_f32(cc, vbslq_f32(vcltq_f32(rel, zero), vnegq_f32(mag), mag));
			float32x4_t su = vmlsq_n_f32(vmlaq_n_f32(vdupq_n_f32(t->ax), va, t->dirx), srcv, t->diry);
			float32x4_t sv = vmlaq_n_f32(vmlaq_n_f32(vdupq_n_f32(t->ay), va, t->diry), srcv, t->dirx);
			int32x4_t iu = vminq_s32(vmaxq_s32(vcvtq_s32_f32(su), vzeroI), vww1);
			int32x4_t iv = vminq_s32(vmaxq_s32(vcvtq_s32_f32(sv), vzeroI), vwh1);
			/* untouched lanes keep their own texel */
			int32_t lu[4] = {U + j * dU, U + (j + 1) * dU, U + (j + 2) * dU, U + (j + 3) * dU};
			int32_t lv[4] = {V + j * dV, V + (j + 1) * dV, V + (j + 2) * dV, V + (j + 3) * dV};
			int32x4_t ou = vshrq_n_s32(vld1q_s32(lu), 16), ov = vshrq_n_s32(vld1q_s32(lv), 16);
			iu = vbslq_s32(far, ou, iu);
			iv = vbslq_s32(far, ov, iv);
			int32_t idx[4];
			vst1q_s32(idx, vmlaq_s32(iu, iv, vww));
			out[j] = src[idx[0]];
			out[j + 1] = src[idx[1]];
			out[j + 2] = src[idx[2]];
			out[j + 3] = src[idx[3]];
		}
	}
#endif
	for (; j < n; j++)
		out[j] = _compose3d_tear_pixel(t, along[j], across[j], U + j * dU, V + j * dV, src, ww, wh);
	return true;
}

/* per frame: arm expiry, healing, rebuild, screen bounds, repaint regions */
static void _compose3d_tear_update(Win3D* e, bool full, bool windowChanged) {
	Tear3D* t = e->tear >= 0 ? &g_tears[e->tear] : NULL;
	if (t) {
		bool held = g3d.dragMode == DEODHAI3D_DRAG_TEAR && g3d.dragEntry == e;
		if (t->armed && !held && g3d.now - t->armedAt > DEODHAI3D_TEAR_ARM_MS) {
			t->armed = false;
			t->healing = true;
		}
		if (t->healing) {
			t->scale *= DEODHAI3D_TEAR_HEAL;
			t->dirty = true;
			if (t->scale < 0.03f) {
				t->used = false;
				e->tear = -1;
				t = NULL;
			}
		}
		if (t && t->dirty)
			_compose3d_tear_build(t);
	}
	bool valid = false;
	Rect box = {0, 0, 0, 0};
	if (t && e->drawable) {
		int x0 = _compose3d_floori(t->box[0]), y0 = _compose3d_floori(t->box[1]);
		int x1 = _compose3d_ceili(t->box[2]), y1 = _compose3d_ceili(t->box[3]);
		if (!_compose3d_local_rect_bounds(e, x0, y0, x1 - x0, y1 - y0, &box))
			box = e->bbox;
		valid = true;
	}
	bool changed = (t && t->dirty) || valid != e->lastTearValid ||
				   (valid && !_compose3d_rect_equal(&box, &e->lastTearBox));
	if (changed && !full && !windowChanged) {
		if (e->lastTearValid)
			_compose3d_add_region(e->lastTearBox.x, e->lastTearBox.y, e->lastTearBox.w, e->lastTearBox.h);
		if (valid)
			_compose3d_add_region(box.x, box.y, box.w, box.h);
	}
	e->tearBox = box;
	e->tearBoxValid = valid;
	e->lastTearBox = box;
	e->lastTearValid = valid;
	if (t)
		t->dirty = false;
}

static void _compose3d_update_entry(Win3D* e, Win3D* parent, bool full, bool updateAll) {
	WinSharedInfo* info = (WinSharedInfo*)e->win->sharedInfo;
	e->w = info->width;
	e->h = info->height;
	e->popup = parent != NULL;
	e->visible = !info->hide && (!parent || parent->visible);
	if (parent)
		_compose3d_derive_popup(e, parent);
	else
		_compose3d_animate_depth(e);
	_compose3d_build_mapping(e);

	bool changed = e->visible != e->lastVisible || e->drawable != e->lastDrawable ||
				   !_compose3d_rect_equal(&e->bbox, &e->lastBBox) || e->w != e->lastW ||
				   e->h != e->lastH || e->facing != e->lastFacing ||
				   e->center.x != e->lastCenter.x || e->center.y != e->lastCenter.y ||
				   e->center.z != e->lastCenter.z;
	_compose3d_tear_update(e, full, changed);
	if (!full) {
		if (changed) {
			if (e->lastVisible && e->lastDrawable)
				_compose3d_add_region(e->lastBBox.x, e->lastBBox.y, e->lastBBox.w, e->lastBBox.h);
			if (e->visible && e->drawable)
				_compose3d_add_region(e->bbox.x, e->bbox.y, e->bbox.w, e->bbox.h);
		} else if (updateAll && e->visible && e->drawable) {
			_compose3d_add_region(e->bbox.x, e->bbox.y, e->bbox.w, e->bbox.h);
		}
	}

	/* client damage -- hidden windows keep theirs, exactly like 2D */
	if (e->visible) {
		if (WinSharedFlagLoad(&info->dirty) && info->rect_count > 0) {
			Rect damage[256];
			uint32_t n = _compose3d_take_damage(info, damage);
			for (uint32_t k = 0; k < n && e->drawable && !full; k++) {
				Rect r;
				if (damage[k].w <= 0 || damage[k].h <= 0)
					continue;
				if (_compose3d_local_rect_bounds(
						e, damage[k].x, damage[k].y, damage[k].w, damage[k].h, &r))
					_compose3d_add_region(r.x, r.y, r.w, r.h);
				else
					_compose3d_add_region(e->bbox.x, e->bbox.y, e->bbox.w, e->bbox.h);
			}
			e->ackDamage = true;
		}
		if (WinSharedFlagLoad(&info->updateEntireWindow)) {
			if (e->drawable && !full)
				_compose3d_add_region(e->bbox.x, e->bbox.y, e->bbox.w, e->bbox.h);
			e->ackEntire = true;
		}
	}

	e->lastVisible = e->visible;
	e->lastDrawable = e->drawable;
	e->lastBBox = e->bbox;
	e->lastCenter = e->center;
	e->lastFacing = e->facing;
	e->lastW = e->w;
	e->lastH = e->h;
}

static Win3D* _compose3d_parent_of(Win3D* e) {
	Window* w = e->win;
	if (!(w->flags & WINDOW_FLAG_POPUP) || !w->parent)
		return NULL;
	Win3D* p = _compose3d_find_ptr(w->parent);
	return (p && p != e) ? p : NULL;
}

static void _compose3d_update_entries(bool full, bool updateAll) {
	/* parents first so popups derive from this frame's pose */
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < DEODHAI3D_MAX_WINDOWS; i++) {
			Win3D* e = &g_win3d[i];
			if (!e->used)
				continue;
			Win3D* parent = _compose3d_parent_of(e);
			if ((pass == 0) != (parent == NULL))
				continue;
			_compose3d_update_entry(e, parent, full, updateAll);
		}
	}
}

/* painter order: far to near; ties keep the 2D stacking order */
static void _compose3d_build_order() {
	g_orderCount = 0;
	for (Window* w = _get_root_window(); w != NULL; w = w->next) {
		Win3D* e = _compose3d_find_ptr(w);
		if (e && g_orderCount < DEODHAI3D_MAX_WINDOWS)
			g_order[g_orderCount++] = e;
	}
	/* far to near, stable: O(N) when already in order (the usual frame),
	 * O(N log N) bottom-up merge sort otherwise (insertion sort was O(N^2)
	 * when the order is reversed, e.g. send-to-back of many windows) */
	int i = 1;
	while (i < g_orderCount && !(g_order[i - 1]->sortKey < g_order[i]->sortKey))
		i++;
	if (i >= g_orderCount)
		return;
	Win3D** src = g_order;
	Win3D** dst = g_orderTmp;
	for (int width = 1; width < g_orderCount; width *= 2) {
		for (int lo = 0; lo < g_orderCount; lo += 2 * width) {
			int mid = lo + width < g_orderCount ? lo + width : g_orderCount;
			int hi = lo + 2 * width < g_orderCount ? lo + 2 * width : g_orderCount;
			int a = lo, b = mid, k = lo;
			while (a < mid && b < hi) /* ties take the left one: stable */
				dst[k++] = (src[a]->sortKey < src[b]->sortKey) ? src[b++] : src[a++];
			while (a < mid)
				dst[k++] = src[a++];
			while (b < hi)
				dst[k++] = src[b++];
		}
		Win3D** t = src;
		src = dst;
		dst = t;
	}
	if (src != g_order)
		memcpy(g_order, src, (size_t)g_orderCount * sizeof(Win3D*));
}

/* ---- rasterisation -------------------------------------------------- */

/* narrow [lo, hi] to the X where p * X + q >= 0 */
static inline bool _compose3d_span_ge0(float p, float q, float* lo, float* hi) {
	if (p > 1e-12f) {
		float r = -q / p;
		if (r > *lo)
			*lo = r;
	} else if (p < -1e-12f) {
		float r = -q / p;
		if (r < *hi)
			*hi = r;
	} else if (q < 0.0f) {
		return false;
	}
	return *lo <= *hi;
}

/* exact (perspective-correct) texel position at screen X as 16.16 fixed
 * point, clamped into the window so interpolation between two such points
 * can never leave it */
static inline void _compose3d_sample_fixed(const Win3D* e,
										   float X,
										   float qu,
										   float qv,
										   float qd,
										   float umax,
										   float vmax,
										   int32_t* U,
										   int32_t* V) {
	const float inv = 65536.0f / (e->den.a * X + qd);
	*U = (int32_t)_compose3d_clampf((e->nu.a * X + qu) * inv, 0.0f, umax);
	*V = (int32_t)_compose3d_clampf((e->nv.a * X + qv) * inv, 0.0f, vmax);
}

/* Draw one window clipped to `clip`. Each scanline solves the exact pixel
 * span where 0 <= u < w and 0 <= v < h (four linear inequalities, since
 * den > 0), so the cost is the window's projected area, not its bounds.
 * Inside the span:
 *   - screen aligned 1:1 windows blend straight from the source row
 *   - otherwise the mapping is sampled exactly every L pixels (one divide)
 *     and stepped in 16.16 fixed point between, L chosen per row so the
 *     interpolation error stays under a quarter texel
 *   - texels are gathered into a small run and blended with the NEON
 *     scanline blender the 2D compositor uses */
static void _compose3d_draw_window(ChCanvas* canvas, const Win3D* e, const Rect* clip, bool lod) {
	Window* win = e->win;
	WinSharedInfo* info = (WinSharedInfo*)win->sharedInfo;
	const uint32_t* src = win->backBuffer;
	const int ww = info->width, wh = info->height;
	if (!src || !e->drawable || ww <= 0 || wh <= 0 || ww != e->w || wh != e->h)
		return;

	int x0 = e->bbox.x > clip->x ? e->bbox.x : clip->x;
	int y0 = e->bbox.y > clip->y ? e->bbox.y : clip->y;
	int x1 = (e->bbox.x + e->bbox.w) < (clip->x + clip->w) ? (e->bbox.x + e->bbox.w)
														   : (clip->x + clip->w);
	int y1 = (e->bbox.y + e->bbox.h) < (clip->y + clip->h) ? (e->bbox.y + e->bbox.h)
														   : (clip->y + clip->h);
	if (x1 <= x0 || y1 <= y0)
		return;

	const size_t stride = canvas->canvasWidth;
	const bool alpha = info->alpha;
	const double alphaValue = info->alphaValue;
	const float fw = (float)ww, fh = (float)wh;
	/* clamp a margin below w << 16: floats near 2^28 are spaced 32 apart,
	 * so (w << 16) - 1 would round up to w and read past the row */
	const float umax = (float)ww * 65536.0f - 256.0f;
	const float vmax = (float)wh * 65536.0f - 256.0f;
	/* perspective strength: |d den/dx| / den, scaled by the texel rate */
	const float denA = _compose3d_absf(e->den.a);
	const float rate = _compose3d_absf(e->nu.a) + _compose3d_absf(e->nv.a) + (fw + fh) * denA;
	uint32_t run[DEODHAI3D_SUBSPAN];
	const Tear3D* tear = (e->tear >= 0 && e->tearBoxValid) ? &g_tears[e->tear] : NULL;
	const int tearX0 = tear ? e->tearBox.x : 0, tearX1 = tear ? e->tearBox.x + e->tearBox.w : 0;

	for (int y = y0; y < y1; y++) {
		const float Y = y + 0.5f;
		const float qu = e->nu.b * Y + e->nu.c;
		const float qv = e->nv.b * Y + e->nv.c;
		const float qd = e->den.b * Y + e->den.c;
		float lo = x0 + 0.5f, hi = x1 - 0.5f;
		if (!_compose3d_span_ge0(e->den.a, qd - DEODHAI3D_DEN_EPS, &lo, &hi) ||
			!_compose3d_span_ge0(e->nu.a, qu, &lo, &hi) ||
			!_compose3d_span_ge0(fw * e->den.a - e->nu.a, fw * qd - qu, &lo, &hi) ||
			!_compose3d_span_ge0(e->nv.a, qv, &lo, &hi) ||
			!_compose3d_span_ge0(fh * e->den.a - e->nv.a, fh * qd - qv, &lo, &hi))
			continue;
		int xs = _compose3d_ceili(lo - 0.5f);
		int xe = _compose3d_floori(hi - 0.5f);
		if (xs < x0)
			xs = x0;
		if (xe > x1 - 1)
			xe = x1 - 1;
		if (xs > xe)
			continue;
		uint32_t* drow = canvas->buffer + (size_t)y * stride;
		const bool tornRow = tear && y >= e->tearBox.y && y < e->tearBox.y + e->tearBox.h;

		/* while the camera turns, odd rows repeat the row above -- the
		 * panorama is rendered in the same row pairs, so every layer stays
		 * consistent; full resolution returns on release */
		if (lod && (y & 1) && y - 1 >= y0) {
			memcpy(drow + xs, drow - stride + xs, (size_t)(xe - xs + 1) * 4);
			continue;
		}

		if (alpha) { /* whole-window opacity (fades): rare, exact per pixel */
			for (int x = xs; x <= xe; x++) {
				const float X = x + 0.5f;
				const float D = e->den.a * X + qd;
				if (D <= 0.0f)
					continue;
				const float inv = 1.0f / D;
				int u = (int)((e->nu.a * X + qu) * inv);
				int v = (int)((e->nv.a * X + qv) * inv);
				if (tornRow) {
					float su, sv;
					int kind = _compose3d_tear_map(tear, (e->nu.a * X + qu) * inv, (e->nv.a * X + qv) * inv, &su, &sv);
					if (kind == DEODHAI3D_TEAR_HOLE || kind == DEODHAI3D_TEAR_SHADOW)
						continue;
					if (kind != DEODHAI3D_TEAR_NONE) {
						u = su >= 0.0f ? (su < (float)ww ? (int)su : ww - 1) : 0;
						v = sv >= 0.0f ? (sv < (float)wh ? (int)sv : wh - 1) : 0;
					}
				}
				u = u < 0 ? 0 : (u >= ww ? ww - 1 : u);
				v = v < 0 ? 0 : (v >= wh ? wh - 1 : v);
				drow[x] = ChColorAlphaBlend(drow[x], src[(size_t)v * ww + u], alphaValue);
			}
			continue;
		}

		const float Xs = xs + 0.5f;
		if (e->unitScale) {
			const float inv = 1.0f / (e->den.a * Xs + qd);
			int u = (int)((e->nu.a * Xs + qu) * inv);
			int v = (int)((e->nv.a * Xs + qv) * inv);
			u = u < 0 ? 0 : (u >= ww ? ww - 1 : u);
			v = v < 0 ? 0 : (v >= wh ? wh - 1 : v);
			int n = xe - xs + 1;
			if (u + n > ww)
				n = ww - u;
			const uint32_t* srow = src + (size_t)v * ww + u - xs; /* srow[x] for screen x */
			if (!tornRow) {
				__pixel_blend_neon(drow + xs, srow + xs, n);
				continue;
			}
			/* straight NEON copies either side of the tear's columns, and
			 * for every 16-pixel run inside them the tear does not touch */
			const int last = xs + n - 1;
			int ta = tearX0 > xs ? tearX0 : xs;
			int tb = tearX1 - 1 < last ? tearX1 - 1 : last;
			if (ta > tb) {
				__pixel_blend_neon(drow + xs, srow + xs, n);
				continue;
			}
			if (ta > xs)
				__pixel_blend_neon(drow + xs, srow + xs, ta - xs);
			for (int x = ta; x <= tb; x += DEODHAI3D_SUBSPAN) {
				int m = tb - x + 1 > DEODHAI3D_SUBSPAN ? DEODHAI3D_SUBSPAN : tb - x + 1;
				int32_t U = (int32_t)(u + (x - xs)) << 16 | 0x8000; /* texel centre */
				int32_t V = (int32_t)v << 16 | 0x8000;
				if (_compose3d_tear_run(tear, U, V, 65536, 0, m, src, ww, wh, run))
					__pixel_blend_neon(drow + x, run, m);
				else
					__pixel_blend_neon(drow + x, srow + x, m);
			}
			if (tb < last)
				__pixel_blend_neon(drow + tb + 1, srow + tb + 1, last - tb);
			continue;
		}

		/* subdivision length for this row: interpolation error is about
		 * L^2 * rate * denA / (4 D^2) texels, keep it below 1/4 */
		int L = DEODHAI3D_SUBSPAN;
		if (denA > 0.0f) {
			float D0 = e->den.a * Xs + qd, D1 = e->den.a * (xe + 0.5f) + qd;
			float Dm = D0 < D1 ? D0 : D1;
			float k = rate * denA / (Dm * Dm);
			if (k * (float)(L * L) > 1.0f) {
				L = (int)(1.0f / sqrtf(k));
				if (L < 1)
					L = 1;
			}
		}

		int32_t U0, V0, U1 = 0, V1 = 0;
		_compose3d_sample_fixed(e, Xs, qu, qv, qd, umax, vmax, &U0, &V0);
		for (int x = xs; x <= xe;) {
			int n = xe - x + 1;
			int32_t dU = 0, dV = 0;
			if (n > L) {
				n = L;
				_compose3d_sample_fixed(e, x + L + 0.5f, qu, qv, qd, umax, vmax, &U1, &V1);
				dU = (U1 - U0) / L;
				dV = (V1 - V0) / L;
			} else if (n > 1) {
				_compose3d_sample_fixed(e, xe + 0.5f, qu, qv, qd, umax, vmax, &U1, &V1);
				dU = (U1 - U0) / (n - 1);
				dV = (V1 - V0) / (n - 1);
			}
			if (tornRow && x + n > tearX0 && x < tearX1 &&
				_compose3d_tear_run(tear, U0, V0, dU, dV, n, src, ww, wh, run)) {
				/* run[] holds the torn texels */
			} else {
				int32_t U = U0, V = V0;
				for (int i = 0; i < n; i++) {
					run[i] = src[(size_t)(V >> 16) * (size_t)ww + (U >> 16)];
					U += dU;
					V += dV;
				}
			}
			__pixel_blend_neon(drow + x, run, n);
			x += n;
			U0 = U1;
			V0 = V1;
		}
	}
}


/* ---- paper flight (hide / unhide) in world space -------------------- */

/* the paper keeps the window's 3D pose, rolls on the window's own plane
 * (the shared column table is applied through the perspective mapping)
 * and flies to the right of the camera */
typedef struct _fly3d_ {
	bool active;	 /* the slot was busy last frame */
	uint32_t handle; /* whose flight this is */
	bool posed;
	Vec3 center;
	float facing;
	bool spanSet;
	float span; /* screen pixels to fly so it leaves the right edge */
	Win3D ghost;
	Rect last;
	bool lastValid;
} Fly3D;

static Fly3D g_fly3d[DEODHAI_PAPERFLY_MAX];

static void _compose3d_fly_pose(Fly3D* g, const PaperFly* fly, float flyPx, float liftPx) {
	Vec3 U, V, N;
	_compose3d_axes(g->facing, &U, &V, &N);
	Vec3 camC = _compose3d_to_camera(&g->center);
	float depth = camC.z > 2.0f * DEODHAI3D_NEAR * g3d.f ? camC.z : 2.0f * DEODHAI3D_NEAR * g3d.f;
	float k = depth / g3d.f; /* world units per screen pixel at the window */
	Vec3 right = {g3d.cosYaw, 0.0f, -g3d.sinYaw};
	int outW = fly->uMax - fly->uMin;
	Vec3 c = _compose3d_madd(&g->center, (float)fly->uMin + outW * 0.5f - fly->w * 0.5f, &U);
	c = _compose3d_madd(&c, flyPx * k, &right);
	c = _compose3d_madd(&c, liftPx * k, &V);
	memset(&g->ghost, 0, sizeof(Win3D));
	g->ghost.used = true;
	g->ghost.tear = -1;
	g->ghost.center = c;
	g->ghost.facing = g->facing;
	g->ghost.w = outW;
	g->ghost.h = fly->h;
	_compose3d_build_mapping(&g->ghost);
}

/* per frame: poses, the flight path and repaint regions of every paper */
static void _compose3d_fly_update(bool full) {
	for (int i = 0; i < DEODHAI_PAPERFLY_MAX; i++) {
		PaperFly* fly = DeodhaiPaperFlyGet(i);
		Fly3D* g = &g_fly3d[i];
		if (fly && (!g->active || g->handle != fly->handle)) {
			/* a new flight in this slot */
			g->handle = fly->handle;
			g->posed = g->spanSet = false;
		}
		g->active = fly != NULL;
		if (!DeodhaiPaperFlyDrawn(fly)) {
			if (g->lastValid && !full)
				_compose3d_add_region(g->last.x, g->last.y, g->last.w, g->last.h);
			g->lastValid = false;
			g->ghost.drawable = false;
			continue;
		}
		Win3D* e = fly->win ? _compose3d_find_ptr(fly->win) : NULL;
		if (e && e->handle == fly->handle) {
			g->center = e->center; /* follows the window's pose while it exists */
			g->facing = e->facing;
			g->posed = true;
		}
		if (!g->posed) { /* never seen in 3D: start where it is on screen */
			Vec3 c = {(fly->x + fly->w * 0.5f) - g3d.cx, (fly->y + fly->h * 0.5f) - g3d.cy - g3d.shear, g3d.f};
			g->center = _compose3d_to_world(&c);
			g->facing = g3d.yaw;
			g->posed = true;
		}
		if (!g->spanSet) {
			/* far enough that the fully rolled tube clears the right edge */
			_compose3d_fly_pose(g, fly, 0.0f, 0.0f);
			float rmax = sqrtf(9.0f + (float)fly->w * 1.1f / DEODHAI3D_PI);
			int left = g->ghost.drawable ? g->ghost.bbox.x : (int)g3d.cx;
			g->span = (float)(g3d.W - left) + 2.0f * rmax + 32.0f;
			g->spanSet = true;
		}
		_compose3d_fly_pose(g, fly, fly->flyK * g->span, (float)fly->dy);
		if (!full) {
			if (g->lastValid)
				_compose3d_add_region(g->last.x, g->last.y, g->last.w, g->last.h);
			if (g->ghost.drawable)
				_compose3d_add_region(g->ghost.bbox.x, g->ghost.bbox.y, g->ghost.bbox.w, g->ghost.bbox.h);
		}
		g->last = g->ghost.bbox;
		g->lastValid = g->ghost.drawable;
	}
}

/* Draw one flying paper clipped to `clip`: the exact perspective span of
 * every row (as for windows), texels through the paper's column table,
 * shading and blending four pixels per NEON step. O(projected area). */
static void _compose3d_draw_paper(ChCanvas* canvas, const Win3D* e, const PaperFly* fly, const Rect* clip, bool lod) {
	const int ww = e->w, wh = e->h;
	if (!e->drawable || ww <= 0 || wh <= 0)
		return;
	int x0 = e->bbox.x > clip->x ? e->bbox.x : clip->x;
	int y0 = e->bbox.y > clip->y ? e->bbox.y : clip->y;
	int x1 = (e->bbox.x + e->bbox.w) < (clip->x + clip->w) ? (e->bbox.x + e->bbox.w) : (clip->x + clip->w);
	int y1 = (e->bbox.y + e->bbox.h) < (clip->y + clip->h) ? (e->bbox.y + e->bbox.h) : (clip->y + clip->h);
	if (x1 <= x0 || y1 <= y0)
		return;
	const size_t stride = canvas->canvasWidth;
	const float fw = (float)ww, fh = (float)wh;
	const float umax = (float)ww * 65536.0f - 256.0f;
	const float vmax = (float)wh * 65536.0f - 256.0f;
	const float denA = _compose3d_absf(e->den.a);
	const float rate = _compose3d_absf(e->nu.a) + _compose3d_absf(e->nv.a) + (fw + fh) * denA;
	uint32_t run[DEODHAI3D_SUBSPAN];
	uint8_t sh[DEODHAI3D_SUBSPAN];

	for (int y = y0; y < y1; y++) {
		const float Y = y + 0.5f;
		const float qu = e->nu.b * Y + e->nu.c;
		const float qv = e->nv.b * Y + e->nv.c;
		const float qd = e->den.b * Y + e->den.c;
		float lo = x0 + 0.5f, hi = x1 - 0.5f;
		if (!_compose3d_span_ge0(e->den.a, qd - DEODHAI3D_DEN_EPS, &lo, &hi) ||
			!_compose3d_span_ge0(e->nu.a, qu, &lo, &hi) ||
			!_compose3d_span_ge0(fw * e->den.a - e->nu.a, fw * qd - qu, &lo, &hi) ||
			!_compose3d_span_ge0(e->nv.a, qv, &lo, &hi) ||
			!_compose3d_span_ge0(fh * e->den.a - e->nv.a, fh * qd - qv, &lo, &hi))
			continue;
		int xs = _compose3d_ceili(lo - 0.5f);
		int xe = _compose3d_floori(hi - 0.5f);
		if (xs < x0)
			xs = x0;
		if (xe > x1 - 1)
			xe = x1 - 1;
		if (xs > xe)
			continue;
		uint32_t* drow = canvas->buffer + (size_t)y * stride;
		if (lod && (y & 1) && y - 1 >= y0) {
			memcpy(drow + xs, drow - stride + xs, (size_t)(xe - xs + 1) * 4);
			continue;
		}
		const float Xs = xs + 0.5f;
		int L = DEODHAI3D_SUBSPAN;
		if (denA > 0.0f) {
			float D0 = e->den.a * Xs + qd, D1 = e->den.a * (xe + 0.5f) + qd;
			float Dm = D0 < D1 ? D0 : D1;
			float k = rate * denA / (Dm * Dm);
			if (k * (float)(L * L) > 1.0f) {
				L = (int)(1.0f / sqrtf(k));
				if (L < 1)
					L = 1;
			}
		}
		int32_t U0, V0, U1 = 0, V1 = 0;
		_compose3d_sample_fixed(e, Xs, qu, qv, qd, umax, vmax, &U0, &V0);
		for (int x = xs; x <= xe;) {
			int n = xe - x + 1;
			int32_t dU = 0, dV = 0;
			if (n > L) {
				n = L;
				_compose3d_sample_fixed(e, x + L + 0.5f, qu, qv, qd, umax, vmax, &U1, &V1);
				dU = (U1 - U0) / L;
				dV = (V1 - V0) / L;
			} else if (n > 1) {
				_compose3d_sample_fixed(e, xe + 0.5f, qu, qv, qd, umax, vmax, &U1, &V1);
				dU = (U1 - U0) / (n - 1);
				dV = (V1 - V0) / (n - 1);
			}
			int32_t U = U0, V = V0;
			for (int i = 0; i < n; i++) {
				const PaperColumn* pc = fly->col + (U >> 16);
				run[i] = DeodhaiPaperFlyTexel(fly, pc, V >> 16);
				sh[i] = pc->shade;
				U += dU;
				V += dV;
			}
			DeodhaiPaperFlyShadeRun(run, sh, n);
			__pixel_blend_neon(drow + x, run, n);
			x += n;
			U0 = U1;
			V0 = V1;
		}
	}
}

/* ---- HUD (always-on-top, screen locked) ----------------------------- */

static void _compose3d_compose_hud(ChCanvas* canvas, bool hudFull) {
	Window* root = _get_root_window();
	Window* focused = _get_focused_window();
	for (Window* w = _get_always_on_top(); w != NULL; w = w->next) {
		WinSharedInfo* info = (WinSharedInfo*)w->sharedInfo;
		if (info->hide)
			continue;
		bool redraw = hudFull;
		for (int i = 0; i < g_regionCount && !redraw; i++)
			redraw = _compose3d_rect_overlap(&g_regions[i], info->x, info->y, info->width, info->height);
		_compose_always_on_top_dirty(canvas, info, false, focused, w);
		_compose_always_on_top_entire(canvas, w, redraw, false, info, root);
	}
}

static Window* _compose3d_hud_at(int x, int y) {
	for (Window* w = _get_always_on_top(); w != NULL; w = w->next) {
		WinSharedInfo* info = (WinSharedInfo*)w->sharedInfo;
		if (info->hide)
			continue;
		if (x >= info->x && x < info->x + info->width && y >= info->y && y < info->y + info->height)
			return w;
	}
	return NULL;
}

/* where a screen pixel lands on a window's own texels, tear included:
 * false in the opening, the pushed-aside texel beside it */
static bool _compose3d_screen_to_texel(const Win3D* e, float X, float Y, float* u, float* v) {
	if (!_compose3d_screen_to_local(e, X, Y, u, v))
		return false;
	if (e->tear < 0)
		return true;
	float su, sv;
	int kind = _compose3d_tear_map(&g_tears[e->tear], *u, *v, &su, &sv);
	if (kind == DEODHAI3D_TEAR_HOLE || kind == DEODHAI3D_TEAR_SHADOW)
		return false;
	if (kind != DEODHAI3D_TEAR_NONE) {
		if (!(su >= 0.0f)) /* also catches NaN */
			su = 0.0f;
		if (!(sv >= 0.0f))
			sv = 0.0f;
		*u = su < (float)e->w - 0.001f ? su : (float)e->w - 0.001f;
		*v = sv < (float)e->h - 0.001f ? sv : (float)e->h - 0.001f;
	}
	return true;
}

/* nearest window under a screen pixel, from the last composed frame;
 * clicks fall through a tear's opening unless `holes` is set */
static Win3D* _compose3d_window_at(int x, int y, float* u, float* v, bool holes = false) {
	for (int i = g_orderCount - 1; i >= 0; i--) {
		Win3D* e = g_order[i];
		if (!e->used || !e->visible || !e->drawable)
			continue;
		if (x < e->bbox.x || x >= e->bbox.x + e->bbox.w || y < e->bbox.y ||
			y >= e->bbox.y + e->bbox.h)
			continue;
		if (holes ? _compose3d_screen_to_local(e, x + 0.5f, y + 0.5f, u, v)
				  : _compose3d_screen_to_texel(e, x + 0.5f, y + 0.5f, u, v))
			return e;
	}
	return NULL;
}

static bool _compose3d_zoom_active() {
	for (Window* w = _get_root_window(); w != NULL; w = w->next) {
		WinSharedInfo* info = (WinSharedInfo*)w->sharedInfo;
		if (!info->hide && info->zoomed)
			return true;
	}
	return false;
}

/* ---- camera --------------------------------------------------------- */

/**
 * @brief Deodhai3DSetCamera -- set camera orientation (radians)
 */
void Deodhai3DSetCamera(float yaw, float pitch) {
	while (yaw >= DEODHAI3D_PI)
		yaw -= DEODHAI3D_TWO_PI;
	while (yaw < -DEODHAI3D_PI)
		yaw += DEODHAI3D_TWO_PI;
	pitch = _compose3d_clampf(pitch, -DEODHAI3D_PITCH_LIMIT, DEODHAI3D_PITCH_LIMIT);
	if (yaw != g3d.yaw || pitch != g3d.pitch)
		g3d.cameraDirty = true;
	g3d.yaw = yaw;
	g3d.pitch = pitch;
	g3d.sinYaw = sinf(yaw);
	g3d.cosYaw = cosf(yaw);
	g3d.shear = g3d.f * tanf(pitch);
}

/**
 * @brief Deodhai3DGetCamera -- get camera orientation (radians)
 */
void Deodhai3DGetCamera(float* yaw, float* pitch) {
	if (yaw)
		*yaw = g3d.yaw;
	if (pitch)
		*pitch = g3d.pitch;
}

/* ---- close animation (ash) ----------------------------------------- */

/* a point of a closed window's world plane -> screen, with the screen
 * derivatives along the plane axes (each flake is drawn as the local
 * affine map of the perspective) */
static bool _compose3d_ash_project(const PaperAshPlane* p, float lx, float ly, float lz, float* out) {
	Vec3 P = {p->o[0] + lx * p->u[0] + ly * p->v[0] + lz * p->n[0],
			  p->o[1] + lx * p->u[1] + ly * p->v[1] + lz * p->n[1],
			  p->o[2] + lx * p->u[2] + ly * p->v[2] + lz * p->n[2]};
	Vec3 U = {p->u[0], p->u[1], p->u[2]}, V = {p->v[0], p->v[1], p->v[2]};
	Vec3 c = _compose3d_to_camera(&P);
	if (c.z < DEODHAI3D_NEAR * g3d.f)
		return false;
	Vec3 cu = _compose3d_to_camera(&U), cv = _compose3d_to_camera(&V);
	const float iz = 1.0f / c.z, fz = g3d.f * iz * iz;
	out[0] = g3d.cx + g3d.f * c.x * iz;
	out[1] = g3d.cy + g3d.shear + g3d.f * c.y * iz;
	out[2] = fz * (cu.x * c.z - c.x * cu.z);
	out[3] = fz * (cv.x * c.z - c.x * cv.z);
	out[4] = fz * (cu.y * c.z - c.y * cu.z);
	out[5] = fz * (cv.y * c.z - c.y * cv.z);
	return true;
}

/* ---- public API ----------------------------------------------------- */

static void _compose3d_release_buffers() {
	size_t bytes = (size_t)g3d.W * (size_t)g3d.H * 4;
	if (g3d.panoOwned && g3d.pano)
		_KeMemUnmap(g3d.pano, (size_t)g3d.panoW * (size_t)g3d.panoH * 4);
	if (g3d.flat)
		_KeMemUnmap(g3d.flat, bytes);
	free(g3d.colU);
	free(g3d.colK);
	free(g3d.colTex);
	DirtyTilesRelease(&g_tiles);
	if (g_regions != g_regionStore)
		free(g_regions);
	g_regions = g_regionStore;
	g_regionCap = DEODHAI3D_MAX_REGIONS;
	g_tilesMode = false;
}

/**
 * @brief Deodhai3DInitialise -- set up the 3D compositor
 */
void Deodhai3DInitialise(ChCanvas* canvas) {
	_compose3d_release_buffers(); /* safe to call again */
	memset(&g3d, 0, sizeof(g3d));
	memset(g_win3d, 0, sizeof(g_win3d));
	memset(g_hash, 0, sizeof(g_hash));
	memset(g_tears, 0, sizeof(g_tears));
	g_hashDeleted = 0;
	g_orderCount = 0;
	g_regionCount = 0;
	if (!canvas || canvas->canvasWidth == 0 || canvas->canvasHeight == 0)
		return;
	/* tile map for busy frames; the region list can then hold every tile */
	if (DirtyTilesInit(&g_tiles, (int)canvas->canvasWidth, (int)canvas->canvasHeight)) {
		int cap = DirtyTilesMaxRects(&g_tiles);
		Rect* big = cap > DEODHAI3D_MAX_REGIONS ? (Rect*)malloc((size_t)cap * sizeof(Rect)) : NULL;
		if (big) {
			g_regions = big;
			g_regionCap = cap;
		} else if (cap > DEODHAI3D_MAX_REGIONS) {
			DirtyTilesRelease(&g_tiles); /* exact list with collapse only */
		}
	}
	g3d.canvas = canvas;
	g3d.W = (int)canvas->canvasWidth;
	g3d.H = (int)canvas->canvasHeight;
	g3d.cx = g3d.W * 0.5f;
	g3d.cy = g3d.H * 0.5f;
	g3d.f = (g3d.W * 0.5f) / tanf(DEODHAI3D_FOV_X * 0.5f);

	size_t pixels = (size_t)g3d.W * (size_t)g3d.H;
	uint32_t* back = DeoGetBackSurface();
	g3d.flat = (uint32_t*)_KeMemMap(NULL, pixels * 4, 0, 0, MEMMAP_NO_FILEDESC, 0);
	g3d.colU = (int32_t*)malloc((size_t)g3d.W * sizeof(int32_t));
	g3d.colK = (float*)malloc((size_t)g3d.W * sizeof(float));
	g3d.colTex = (int32_t*)malloc((size_t)g3d.W * sizeof(int32_t));
	if (!back || !g3d.flat || !g3d.colU || !g3d.colK || !g3d.colTex) {
		_KePrint("[deodhaiXR]: 3D compositor unavailable (allocation failed)\r\n");
		return;
	}
	memcpy(g3d.flat, back, pixels * 4);
	/* the panorama path writes texels as-is: make the fallback opaque */
	for (size_t i = 0; i < pixels; i++)
		g3d.flat[i] |= 0xFF000000u;
	Deodhai3DSetCamera(0.0f, 0.0f);
	/* no panorama yet: wrap the flat wallpaper across one field of view
	 * with mirror repeat, so turning never shows a seam */
	_compose3d_set_texture(g3d.flat, g3d.W, g3d.H, DEODHAI3D_FOV_X, true, false);
	g3d.ready = true;
}

/**
 * @brief Deodhai3DEnabled -- returns true while 3D mode is active
 */
bool Deodhai3DEnabled() {
	return g3d.ready && g3d.enabled;
}

static void _compose3d_leave_hovered(int x, int y, int button) {
	if (g3d.hovered && _compose3d_window_alive(g3d.hovered, g3d.hoveredHandle)) {
		uint8_t type = (g3d.hovered->flags & WINDOW_FLAG_POPUP) ? HANDLE_TYPE_POPUP_WINDOW
																: HANDLE_TYPE_NORMAL_WINDOW;
		DeodhaiSendMouseEvent(g3d.hovered->handle,
							  g3d.hovered->ownerId,
							  type,
							  DEODHAI_REPLY_MOUSE_LEAVE,
							  x,
							  y,
							  button);
	}
	g3d.hovered = NULL;
}

/**
 * @brief Deodhai3DSetEnabled -- switch between 3D and 2D compositing
 */
void Deodhai3DSetEnabled(bool enable) {
	if (!g3d.ready || enable == g3d.enabled)
		return;
	g3d.dragMode = DEODHAI3D_DRAG_NONE;
	_compose3d_leave_hovered(g3d.lastX, g3d.lastY, 0);
	if (enable) {
		/* fresh layout from the current 2D positions, camera straight ahead */
		memset(g_win3d, 0, sizeof(g_win3d));
		memset(g_hash, 0, sizeof(g_hash));
		memset(g_tears, 0, sizeof(g_tears));
		memset(g_fly3d, 0, sizeof(g_fly3d));
		g_hashDeleted = 0;
		g_orderCount = 0;
		_compose3d_reset_regions();
		Deodhai3DSetCamera(0.0f, 0.0f);
		g3d.cameraDirty = true;
		g3d.needSync = true;
		g3d.enabled = true;
		return;
	}
	g3d.enabled = false;
	uint32_t* back = DeoGetBackSurface();
	if (back)
		memcpy(back, g3d.flat, (size_t)g3d.W * (size_t)g3d.H * 4);
	DeoBakeScreenBlur(g3d.W, g3d.H);
	BackDirtyAdd(0, 0, g3d.W, g3d.H);
	DeodhaiUpdateBits(true, false);
	_set_always_on_top_update(true);
}

/**
 * @brief Deodhai3DToggle -- flips 3D mode
 */
void Deodhai3DToggle() {
	Deodhai3DSetEnabled(!Deodhai3DEnabled());
}

/* entry for API calls: popups resolve to the window they ride on */
static Win3D* _compose3d_api_entry(Window* win) {
	if (!Deodhai3DEnabled() || !win)
		return NULL;
	_compose3d_sync_if_needed();
	Win3D* e = _compose3d_find_ptr(win);
	if (e && (win->flags & WINDOW_FLAG_POPUP) && win->parent) {
		Win3D* p = _compose3d_find_ptr(win->parent);
		if (p)
			e = p;
	}
	return e;
}

/**
 * @brief Deodhai3DWindowSetPosition -- translate a window in 3D space
 */
bool Deodhai3DWindowSetPosition(Window* win, float x, float y, float z) {
	if (!Deodhai3DEnabled() || !win)
		return false;
	_compose3d_sync_if_needed();
	Win3D* e = _compose3d_find_ptr(win);
	if (!e || _compose3d_parent_of(e))
		return false; /* popups follow their parent */
	float dist = sqrtf(x * x + y * y + z * z);
	if (dist < DEODHAI3D_MIN_DIST * g3d.f || dist > DEODHAI3D_MAX_DIST * g3d.f)
		return false;
	e->center.x = x;
	e->center.y = y;
	e->center.z = z;
	e->facing = atan2f(x, z);
	e->depthAnimating = false;
	return true;
}

/**
 * @brief Deodhai3DWindowGetPosition -- get the world position of a window
 */
bool Deodhai3DWindowGetPosition(Window* win, float* x, float* y, float* z) {
	Win3D* e = _compose3d_find_ptr(win);
	if (!Deodhai3DEnabled() || !e)
		return false;
	if (x)
		*x = e->center.x;
	if (y)
		*y = e->center.y;
	if (z)
		*z = e->center.z;
	return true;
}

static float _compose3d_target_of(const Win3D* e) {
	return e->depthAnimating ? e->targetDist : _compose3d_length(&e->center);
}

static void _compose3d_set_target(Win3D* e, float dist) {
	e->targetDist =
		_compose3d_clampf(dist, DEODHAI3D_MIN_DIST * g3d.f, DEODHAI3D_MAX_DIST * g3d.f);
	e->depthAnimating = true;
}

/**
 * @brief Deodhai3DWindowPushDepth -- move a window along its view ray
 */
bool Deodhai3DWindowPushDepth(Window* win, float factor) {
	Win3D* e = _compose3d_api_entry(win);
	if (!e || factor <= 0.0f)
		return false;
	_compose3d_set_target(e, _compose3d_target_of(e) * factor);
	return true;
}

/* farthest (back = true) or nearest target depth among the other windows */
static bool _compose3d_extreme_depth(const Win3D* self, bool back, float* out) {
	bool any = false;
	for (int i = 0; i < DEODHAI3D_MAX_WINDOWS; i++) {
		const Win3D* o = &g_win3d[i];
		if (!o->used || o == self || o->popup || !o->lastVisible)
			continue;
		float d = _compose3d_target_of(o);
		if (!any || (back ? d > *out : d < *out))
			*out = d;
		any = true;
	}
	return any;
}

/**
 * @brief Deodhai3DWindowSendToBack -- behind every other window
 */
bool Deodhai3DWindowSendToBack(Window* win) {
	Win3D* e = _compose3d_api_entry(win);
	float far;
	if (!e)
		return false;
	if (_compose3d_extreme_depth(e, true, &far) && _compose3d_target_of(e) <= far)
		_compose3d_set_target(e, far * DEODHAI3D_DEPTH_GAP);
	return true;
}

/**
 * @brief Deodhai3DWindowBringToFront -- in front of every other window
 */
bool Deodhai3DWindowBringToFront(Window* win) {
	Win3D* e = _compose3d_api_entry(win);
	float nearest;
	if (!e)
		return false;
	if (_compose3d_extreme_depth(e, false, &nearest) && _compose3d_target_of(e) >= nearest)
		_compose3d_set_target(e, nearest / DEODHAI3D_DEPTH_GAP);
	return true;
}


/* the entry that owns a window's tear: popups ride on their parent */
static Win3D* _compose3d_tear_owner(Window* win) {
	if (!Deodhai3DEnabled() || !win)
		return NULL;
	Win3D* e = _compose3d_find_ptr(win);
	if (e && (win->flags & WINDOW_FLAG_POPUP) && win->parent) {
		Win3D* p = _compose3d_find_ptr(win->parent);
		if (p)
			e = p;
	}
	return e;
}

static Tear3D* _compose3d_tear_start(Win3D* e, float ax, float ay, float tx, float ty, bool torn) {
	if (e->tear < 0)
		e->tear = _compose3d_tear_alloc();
	if (e->tear < 0)
		return NULL; /* every tear slot busy */
	Tear3D* t = &g_tears[e->tear];
	t->ax = ax;
	t->ay = ay;
	t->tx = tx;
	t->ty = ty;
	t->dirx = 1.0f;
	t->diry = 0.0f;
	t->scale = 1.0f;
	t->armed = !torn;
	t->torn = torn;
	t->healing = false;
	t->armedAt = g3d.now;
	t->seed = (uint32_t)(g3d.now * 2654435761u) ^ (e->handle * 0x85EBCA6Bu) ^ (uint32_t)(ax * 131.0f);
	t->dirty = true;
	return t;
}

/**
 * @brief Deodhai3DWindowTear -- tear a window open from (ax, ay) to
 * (tx, ty), window texels. The window takes no input until mended.
 */
bool Deodhai3DWindowTear(Window* win, float ax, float ay, float tx, float ty) {
	if (!Deodhai3DEnabled())
		return false;
	_compose3d_sync_if_needed();
	Win3D* e = _compose3d_tear_owner(win);
	return e && _compose3d_tear_start(e, ax, ay, tx, ty, true) != NULL;
}

/**
 * @brief Deodhai3DWindowMend -- heal a tear (animated); input returns once
 * it has closed
 */
bool Deodhai3DWindowMend(Window* win) {
	Win3D* e = _compose3d_tear_owner(win);
	if (!e || e->tear < 0)
		return false;
	g_tears[e->tear].healing = true;
	g_tears[e->tear].armed = false;
	return true;
}

/**
 * @brief Deodhai3DWindowInputBlocked -- true while the window (or the
 * window a popup rides on) is torn
 */
bool Deodhai3DWindowInputBlocked(Window* win) {
	Win3D* e = _compose3d_tear_owner(win);
	return e && e->tear >= 0 && g_tears[e->tear].torn;
}

/**
 * @brief Deodhai3DWindowTearBounds -- screen bounds of a window's tear as
 * of the last composed frame
 */
bool Deodhai3DWindowTearBounds(Window* win, Rect* bounds) {
	Win3D* e = _compose3d_tear_owner(win);
	if (!e || e->tear < 0 || !e->tearBoxValid)
		return false;
	if (bounds)
		*bounds = e->tearBox;
	return true;
}

/**
 * @brief Deodhai3DWindowPlane -- the world plane a window sits on now, for
 * its close animation (DeodhaiPaperAshStart)
 */
bool Deodhai3DWindowPlane(Window* win, PaperAshPlane* plane) {
	if (!plane)
		return false;
	plane->valid = false;
	if (!win || !win->sharedInfo || !Deodhai3DEnabled() || _compose3d_zoom_active())
		return false; /* drawn by the 2D pipeline */
	_compose3d_sync_if_needed();
	WinSharedInfo* info = (WinSharedInfo*)win->sharedInfo;
	Win3D* e = _compose3d_find_ptr(win);
	Vec3 center;
	float facing;
	if (e && e->handle == win->handle) {
		center = e->center;
		facing = e->facing;
	} else { /* not composed in 3D yet: where it is on screen */
		Vec3 c = {(info->x + info->width * 0.5f) - g3d.cx,
				  (info->y + info->height * 0.5f) - g3d.cy - g3d.shear,
				  g3d.f};
		center = _compose3d_to_world(&c);
		facing = g3d.yaw;
	}
	Vec3 U, V, N;
	_compose3d_axes(facing, &U, &V, &N);
	float hw = info->width * 0.5f, hh = info->height * 0.5f;
	plane->o[0] = center.x - hw * U.x - hh * V.x;
	plane->o[1] = center.y - hw * U.y - hh * V.y;
	plane->o[2] = center.z - hw * U.z - hh * V.z;
	plane->u[0] = U.x, plane->u[1] = U.y, plane->u[2] = U.z;
	plane->v[0] = V.x, plane->v[1] = V.y, plane->v[2] = V.z;
	plane->n[0] = N.x, plane->n[1] = N.y, plane->n[2] = N.z;
	plane->valid = true;
	return true;
}

/**
 * @brief Deodhai3DHandleKey -- 3D shortcuts (with Ctrl held)
 */
bool Deodhai3DHandleKey(int key, bool ctrl) {
	if (!ctrl || !g3d.ready)
		return false;
	if (key == '3') {
		Deodhai3DToggle();
		return true;
	}
	if (!Deodhai3DEnabled())
		return false;
	Window* focused = _get_focused_window();
	switch (key) {
	case '-':
		Deodhai3DWindowPushDepth(focused, DEODHAI3D_DEPTH_STEP);
		return true;
	case '=':
		Deodhai3DWindowPushDepth(focused, 1.0f / DEODHAI3D_DEPTH_STEP);
		return true;
	case '[':
		Deodhai3DWindowSendToBack(focused);
		return true;
	case ']':
		Deodhai3DWindowBringToFront(focused);
		return true;
	}
	return false;
}

/**
 * @brief Deodhai3DScreenToWindow -- maps a screen pixel onto a window
 */
bool Deodhai3DScreenToWindow(Window* win, int sx, int sy, int* lx, int* ly) {
	Win3D* e = _compose3d_find_ptr(win);
	float u, v;
	if (!Deodhai3DEnabled() || !e || !e->visible)
		return false;
	if (!_compose3d_screen_to_texel(e, sx + 0.5f, sy + 0.5f, &u, &v))
		return false;
	if (lx)
		*lx = (int)u;
	if (ly)
		*ly = (int)v;
	return true;
}

/* ---- compose -------------------------------------------------------- */

/**
 * @brief XRComposeFrame3D -- the 3D compose pipeline
 */
void XRComposeFrame3D(ChCanvas* canvas) {
	if (!Deodhai3DEnabled() || canvas != g3d.canvas) {
		XRComposeFrame(canvas);
		return;
	}
	/* zoomed windows are screen locked: the 2D pipeline already does that */
	if (_compose3d_zoom_active()) {
		g3d.wasZoomed = true;
		XRComposeFrame(canvas);
		return;
	}

	bool updateAll = false, hudFull = false;
	_take_update_bits(&updateAll, &hudFull);
	g3d.now = _KeGetCurrentMS();
	/* paper flights: a window whose paper lands is shown before the entries
	 * are updated, so it is drawn this very frame under its flat paper */
	if (DeodhaiPaperFlyActive()) {
		Window* shown[DEODHAI_PAPERFLY_MAX];
		int landed = DeodhaiPaperFlyStep(g3d.now, g3d.W, g3d.H, shown, DEODHAI_PAPERFLY_MAX);
		for (int i = 0; i < landed; i++)
			DeodhaiWindowShowNow(shown[i]);
	}
	if (g3d.wasZoomed) {
		g3d.wasZoomed = false;
		g3d.cameraDirty = true;
	}
	if (g3d.hudRefresh) {
		g3d.hudRefresh = false;
		hudFull = true;
	}

	Cursor* cur = _get_current_cursor();
	if (cur) {
		CursorDrawBack(canvas, cur, cur->oldXPos, cur->oldYPos);
		AddDirtyClip(cur->oldXPos, cur->oldYPos, DEODHAI3D_CURSOR_SIZE, DEODHAI3D_CURSOR_SIZE);
	}

	/* 1. panorama: only when the camera moved; written to the back surface
	 * and, row by row while hot in cache, to the canvas */
	bool full = g3d.cameraDirty;
	bool lod = false;
	if (full) {
		uint32_t* back = DeoGetBackSurface();
		lod = DEODHAI3D_DRAG_LOD && g3d.dragMode == DEODHAI3D_DRAG_CAMERA;
		if (back)
			_compose3d_render_panorama(back, canvas->buffer, lod);
		g3d.cameraDirty = false;
		g3d.blurStale = true;
		hudFull = true;
		_compose3d_add_region(0, 0, g3d.W, g3d.H);
	}

	/* screen-space restore requests from the shared 2D handlers (hide,
	 * close, animations) */
	int backCount = BackDirtyGetDirtyCount();
	for (int i = 0; i < backCount; i++) {
		int x, y, w, h;
		BackDirtyGetRect(&x, &y, &w, &h, i);
		_compose3d_add_region(x, y, w, h);
	}
	BackDirtyCountReset();

	/* 2. windows: poses, change detection, client damage -> regions */
	_compose3d_sync_entries();
	_compose3d_update_entries(full, updateAll);
	_compose3d_build_order();
	_compose3d_fly_update(full);
	/* close animations (ash): flakes keep the closed window's plane; their
	 * old and new footprints join the regions */
	bool ash = DeodhaiPaperAshActive();
	if (ash) {
		DeodhaiPaperAshStep(g3d.now, g3d.W, g3d.H, _compose3d_ash_project);
		const Rect* d;
		int n = DeodhaiPaperAshDirty(&d);
		for (int i = 0; i < n && !full; i++)
			_compose3d_add_region(d[i].x, d[i].y, d[i].w, d[i].h);
	}
	_compose3d_finish_regions();

	/* windows far to near. A burning window's ash is drawn at the window's
	 * depth: windows behind it first, then the ash (once, unclipped -- it
	 * lies inside the regions), then the windows in front of it. One pass
	 * over the regions per ash, so O((ashes + 1) x R x N) like before. */
	int ashSlot[DEODHAI_PAPERASH_MAX];
	float ashDepth[DEODHAI_PAPERASH_MAX];
	int ashCount = 0;
	for (int i = 0; ash && i < DEODHAI_PAPERASH_MAX; i++) {
		const PaperAsh* pa = DeodhaiPaperAshGet(i);
		if (!DeodhaiPaperAshDrawn(pa))
			continue;
		float d = pa->projected ? pa->depth : 0.0f; /* no plane: in front */
		int k = ashCount++;
		while (k > 0 && ashDepth[k - 1] < d) { /* far first; at most 4 */
			ashDepth[k] = ashDepth[k - 1];
			ashSlot[k] = ashSlot[k - 1];
			k--;
		}
		ashDepth[k] = d;
		ashSlot[k] = i;
	}
	int w0 = 0;
	for (int pass = 0; pass <= ashCount; pass++) {
		int w1 = g_orderCount;
		if (pass < ashCount) {
			w1 = w0;
			while (w1 < g_orderCount && g_order[w1]->sortKey > ashDepth[pass])
				w1++;
		}
		for (int r = 0; r < g_regionCount; r++) {
			const Rect* reg = &g_regions[r];
			/* a full frame already copied the fresh panorama to the canvas */
			if (pass == 0 && !full)
				DeodhaiBackSurfaceUpdate(canvas, reg->x, reg->y, reg->w, reg->h);
			for (int i = w0; i < w1; i++) {
				const Win3D* e = g_order[i];
				if (e->visible && e->drawable &&
					_compose3d_rect_overlap(reg, e->bbox.x, e->bbox.y, e->bbox.w, e->bbox.h))
					_compose3d_draw_window(canvas, e, reg, lod);
			}
		}
		if (pass < ashCount)
			DeodhaiPaperAshDrawOne(canvas, ashSlot[pass], NULL);
		w0 = w1;
	}
	for (int r = 0; r < g_regionCount; r++) {
		const Rect* reg = &g_regions[r];
		/* flying papers above the windows, below the HUD */
		for (int i = 0; i < DEODHAI_PAPERFLY_MAX; i++) {
			const PaperFly* fly = DeodhaiPaperFlyGet(i);
			const Win3D* gh = &g_fly3d[i].ghost;
			if (DeodhaiPaperFlyDrawn(fly) && gh->drawable &&
				_compose3d_rect_overlap(reg, gh->bbox.x, gh->bbox.y, gh->bbox.w, gh->bbox.h))
				_compose3d_draw_paper(canvas, gh, fly, reg, lod);
		}
		AddDirtyClip(reg->x, reg->y, reg->w, reg->h);
	}

	/* 3. HUD over everything that was repainted */
	_compose3d_compose_hud(canvas, hudFull);
	g_regionCount = 0;

	for (int i = 0; i < DEODHAI3D_MAX_WINDOWS; i++) {
		Win3D* e = &g_win3d[i];
		if (!e->used)
			continue;
		WinSharedInfo* info = (WinSharedInfo*)e->win->sharedInfo;
		if (e->ackDamage)
			_compose3d_ack_damage(info);
		if (e->ackEntire)
			WinSharedFlagStore(&info->updateEntireWindow, false);
		e->ackDamage = e->ackEntire = false;
		if (e->visible && !info->windowReady)
			info->windowReady = 1; /* clients block on this after create */
	}

	/* 4. cursor */
	if (cur) {
		CursorStoreBack(canvas, cur, cur->xpos, cur->ypos);
		CursorDraw(canvas, cur, cur->xpos, cur->ypos);
		AddDirtyClip(cur->xpos, cur->ypos, DEODHAI3D_CURSOR_SIZE, DEODHAI3D_CURSOR_SIZE);
	}
#ifdef __XENEVA_OPENXR__
	XrPresentFrame(canvas);
#endif
	DirtyScreenUpdate(canvas);
	if (cur) {
		cur->oldXPos = cur->xpos;
		cur->oldYPos = cur->ypos;
	}

	/* glass HUD blends against the baked blur: re-bake once the camera
	 * settles, not on every drag frame */
	if (g3d.blurStale && g3d.dragMode != DEODHAI3D_DRAG_CAMERA) {
		DeoBakeScreenBlur(g3d.W, g3d.H);
		g3d.blurStale = false;
		g3d.hudRefresh = true;
	}
	g3d.needSync = true; /* the postbox may add / close windows next */
}

/* ---- input ---------------------------------------------------------- */

static void _compose3d_forward(Window* w, int ex, int ey, int x, int y, int button) {
	if (g3d.hovered && (g3d.hovered != w || g3d.hoveredHandle != w->handle))
		_compose3d_leave_hovered(x, y, button);
	g3d.hovered = w;
	g3d.hoveredHandle = w->handle;
	uint8_t type =
		(w->flags & WINDOW_FLAG_POPUP) ? HANDLE_TYPE_POPUP_WINDOW : HANDLE_TYPE_NORMAL_WINDOW;
	DeodhaiSendMouseEvent(w->handle, w->ownerId, type, DEODHAI_REPLY_MOUSE_EVENT, ex, ey, button);
}

/* keep the grabbed window pixel under the cursor at distance grabR */
static void _compose3d_drag_window(Win3D* e, int x, int y, bool depth) {
	if (depth) {
		g3d.grabR *= 1.0f - (y - g3d.lastY) * DEODHAI3D_DEPTH_SPEED;
		g3d.grabR = _compose3d_clampf(
			g3d.grabR, DEODHAI3D_MIN_DIST * g3d.f, DEODHAI3D_MAX_DIST * g3d.f);
	}
	Vec3 dir = _compose3d_ray_world(x, y);
	Vec3 point = {dir.x * g3d.grabR, dir.y * g3d.grabR, dir.z * g3d.grabR};
	Vec3 U, V, N;
	e->facing = atan2f(dir.x, dir.z);
	_compose3d_axes(e->facing, &U, &V, &N);
	Vec3 c = _compose3d_madd(&point, -(g3d.grabU - e->w * 0.5f), &U);
	e->center = _compose3d_madd(&c, -(g3d.grabV - e->h * 0.5f), &V);
}


/* Shake detector, O(1) per event: count cursor direction reversals (on
 * either axis) that followed at least SHAKE_MIN_TRAVEL px of travel; a
 * shake is SHAKE_REVERSALS of them within SHAKE_WINDOW_MS, all inside a
 * SHAKE_MAX_SPREAD box. The centre and dominant axis are kept. */
static bool _compose3d_shake_feed(int x, int y) {
	int dx = x - g3d.shakeX, dy = y - g3d.shakeY;
	g3d.shakeX = x;
	g3d.shakeY = y;
	bool reversed = false;
	if (dx != 0) {
		int dir = dx > 0 ? 1 : -1;
		if (dir != g3d.shakeDirX) {
			reversed |= g3d.shakeDirX != 0 && g3d.shakeTravelX >= DEODHAI3D_SHAKE_MIN_TRAVEL;
			g3d.shakeTravelX = 0;
			g3d.shakeDirX = dir;
		}
		g3d.shakeTravelX += dx > 0 ? dx : -dx;
		g3d.shakeSumX += dx > 0 ? dx : -dx;
	}
	if (dy != 0) {
		int dir = dy > 0 ? 1 : -1;
		if (dir != g3d.shakeDirY) {
			reversed |= g3d.shakeDirY != 0 && g3d.shakeTravelY >= DEODHAI3D_SHAKE_MIN_TRAVEL;
			g3d.shakeTravelY = 0;
			g3d.shakeDirY = dir;
		}
		g3d.shakeTravelY += dy > 0 ? dy : -dy;
		g3d.shakeSumY += dy > 0 ? dy : -dy;
	}
	if (!reversed)
		return false;
	int slot = g3d.shakeHead++ & 7;
	g3d.shakeTime[slot] = g3d.now + 1; /* 0 marks an empty slot */
	g3d.shakePX[slot] = x;
	g3d.shakePY[slot] = y;

	int count = 0, minx = x, maxx = x, miny = y, maxy = y, sx = 0, sy = 0;
	for (int i = 0; i < 8; i++) {
		if (!g3d.shakeTime[i] || g3d.now + 1 - g3d.shakeTime[i] > DEODHAI3D_SHAKE_WINDOW_MS)
			continue;
		count++;
		sx += g3d.shakePX[i];
		sy += g3d.shakePY[i];
		minx = g3d.shakePX[i] < minx ? g3d.shakePX[i] : minx;
		maxx = g3d.shakePX[i] > maxx ? g3d.shakePX[i] : maxx;
		miny = g3d.shakePY[i] < miny ? g3d.shakePY[i] : miny;
		maxy = g3d.shakePY[i] > maxy ? g3d.shakePY[i] : maxy;
	}
	if (count < DEODHAI3D_SHAKE_REVERSALS || maxx - minx > DEODHAI3D_SHAKE_MAX_SPREAD ||
		maxy - miny > DEODHAI3D_SHAKE_MAX_SPREAD || g3d.now < g3d.shakeQuietUntil)
		return false;
	g3d.shakeQuietUntil = g3d.now + DEODHAI3D_SHAKE_COOLDOWN_MS;
	g3d.shakeCX = sx / count;
	g3d.shakeCY = sy / count;
	g3d.shakeHorizontal = g3d.shakeSumX >= g3d.shakeSumY;
	memset(g3d.shakeTime, 0, sizeof(g3d.shakeTime));
	g3d.shakeSumX = g3d.shakeSumY = 0;
	return true;
}

static void _compose3d_shake_reset() {
	memset(g3d.shakeTime, 0, sizeof(g3d.shakeTime));
	g3d.shakeDirX = g3d.shakeDirY = 0;
	g3d.shakeTravelX = g3d.shakeTravelY = 0;
	g3d.shakeSumX = g3d.shakeSumY = 0;
}

/* the window plane under a screen pixel, without the window bounds: a
 * tear may be pulled past the window edge */
static bool _compose3d_screen_to_plane(const Win3D* e, int x, int y, float* u, float* v) {
	float X = x + 0.5f, Y = y + 0.5f;
	float D = _compose3d_eval(&e->den, X, Y);
	if (!e->front || D <= DEODHAI3D_DEN_EPS)
		return false;
	*u = _compose3d_eval(&e->nu, X, Y) / D;
	*v = _compose3d_eval(&e->nv, X, Y) / D;
	return true;
}

static bool _compose3d_local_to_screen(const Win3D* e, float u, float v, float* sx, float* sy) {
	Vec3 p = _compose3d_madd(&e->camO, u, &e->camU);
	p = _compose3d_madd(&p, v, &e->camV);
	return _compose3d_project(&p, sx, sy);
}

/* a shake over a window: heal it if torn, otherwise arm a slit there */
static void _compose3d_shake_window() {
	float u, v;
	Win3D* hit = _compose3d_window_at(g3d.shakeCX, g3d.shakeCY, &u, &v, true);
	if (!hit)
		return;
	if (hit->popup) {
		Win3D* p = _compose3d_parent_of(hit);
		if (!p)
			return;
		hit = p;
		if (!_compose3d_screen_to_local(hit, g3d.shakeCX + 0.5f, g3d.shakeCY + 0.5f, &u, &v))
			return;
	}
	if (hit->tear >= 0) {
		Tear3D* t = &g_tears[hit->tear];
		t->healing = true;
		t->armed = false;
		return;
	}
	float h = DEODHAI3D_TEAR_SEED_LEN * 0.5f;
	if (g3d.shakeHorizontal)
		_compose3d_tear_start(hit, u - h, v, u + h, v, false);
	else
		_compose3d_tear_start(hit, u, v - h, u, v + h, false);
}

/**
 * @brief Deodhai3DHandleMouse -- 3D input routing
 */
bool Deodhai3DHandleMouse(ChCanvas* canvas, int x, int y, int button) {
	(void)canvas;
	if (!Deodhai3DEnabled())
		return false;
	if (_compose3d_zoom_active()) {
		g3d.dragMode = DEODHAI3D_DRAG_NONE;
		g3d.lastButton = button;
		return false; /* zoomed: the 2D handlers own input */
	}
	_compose3d_sync_if_needed();
	g3d.now = _KeGetCurrentMS();
	const bool pressed = button && !g3d.lastButton;

	if (g3d.dragMode == DEODHAI3D_DRAG_TEAR) {
		/* the crack's tip follows the cursor on the window plane */
		Win3D* e = g3d.dragEntry;
		Tear3D* t = (e && e->used && e->handle == g3d.dragHandle && e->tear >= 0)
						? &g_tears[e->tear]
						: NULL;
		float u, v;
		if (t && button && _compose3d_screen_to_plane(e, x, y, &u, &v)) {
			t->tx = _compose3d_clampf(u, -0.5f * e->w, 1.5f * e->w);
			t->ty = _compose3d_clampf(v, -0.5f * e->h, 1.5f * e->h);
			t->armed = false;
			t->dirty = true;
			float dx = t->tx - t->ax, dy = t->ty - t->ay;
			if (dx * dx + dy * dy >= DEODHAI3D_TEAR_MIN_LEN * DEODHAI3D_TEAR_MIN_LEN)
				t->torn = true;
		}
		if (!button || !t) {
			g3d.dragMode = DEODHAI3D_DRAG_NONE;
			if (t && !t->torn)
				t->healing = true; /* barely pulled: let it close again */
		}
	} else if (g3d.dragMode == DEODHAI3D_DRAG_CAMERA) {
		if (button) {
			Deodhai3DSetCamera(g3d.yaw - (x - g3d.lastX) / g3d.f,
							   g3d.pitch + (y - g3d.lastY) / g3d.f);
		} else {
			g3d.dragMode = DEODHAI3D_DRAG_NONE;
			g3d.cameraDirty = true; /* full resolution panorama again */
		}
	} else if (g3d.dragMode == DEODHAI3D_DRAG_WINDOW) {
		Win3D* e = g3d.dragEntry;
		if (e && e->used && e->handle == g3d.dragHandle) {
			if (button)
				_compose3d_drag_window(e, x, y, ChitralekhaKeyGetCTRL());
			WinSharedInfo* info = (WinSharedInfo*)e->win->sharedInfo;
			_compose3d_forward(
				e->win, info->x + (int)g3d.grabU, info->y + (int)g3d.grabV, x, y, button);
		}
		if (!button || !e || !e->used)
			g3d.dragMode = DEODHAI3D_DRAG_NONE;
	} else {
		Window* hud = _compose3d_hud_at(x, y);
		if (!hud && !button && _compose3d_shake_feed(x, y))
			_compose3d_shake_window();
		float u = 0, v = 0;
		Win3D* hit = hud ? NULL : _compose3d_window_at(x, y, &u, &v);
		Win3D* owner = hit && hit->popup ? _compose3d_parent_of(hit) : hit;
		Tear3D* tear = (owner && owner->tear >= 0) ? &g_tears[owner->tear] : NULL;
		/* an armed slit near the press (its own opening lets clicks fall
		 * through, so it is looked up directly, not via the hit test) */
		Win3D* slitOwner = NULL;
		for (int i = 0; pressed && !hud && i < DEODHAI3D_MAX_WINDOWS && !slitOwner; i++) {
			Win3D* e = &g_win3d[i];
			if (!e->used || !e->visible || !e->drawable || e->tear < 0)
				continue;
			Tear3D* t = &g_tears[e->tear];
			float sx, sy;
			if (t->armed && !t->healing &&
				_compose3d_local_to_screen(e, 0.5f * (t->ax + t->tx), 0.5f * (t->ay + t->ty), &sx, &sy) &&
				(sx - x) * (sx - x) + (sy - y) * (sy - y) <=
					(float)(DEODHAI3D_TEAR_ARM_RADIUS * DEODHAI3D_TEAR_ARM_RADIUS))
				slitOwner = e;
		}
		if (hud) {
			_compose3d_forward(hud, x, y, x, y, button);
		} else if (slitOwner) {
			/* pull the armed slit open from the shake point: the crack runs
			 * from there to wherever the cursor goes */
			Tear3D* t = &g_tears[slitOwner->tear];
			float mx = 0.5f * (t->ax + t->tx), my = 0.5f * (t->ay + t->ty);
			t->ax = t->tx = mx;
			t->ay = t->ty = my;
			t->dirty = true;
			g3d.dragEntry = slitOwner;
			g3d.dragHandle = slitOwner->handle;
			g3d.dragMode = DEODHAI3D_DRAG_TEAR;
			_compose3d_shake_reset();
		} else if (hit && tear && tear->torn) {
			/* torn: the application gets nothing; the page itself can
			 * still be moved around by its titlebar */
			if (pressed && !hit->popup && !(hit->win->flags & WINDOW_FLAG_STATIC) &&
				v < DEODHAI3D_TITLEBAR && u >= 10.0f && u < (float)(hit->w - 74)) {
				Vec3 U, V, N;
				_compose3d_axes(hit->facing, &U, &V, &N);
				Vec3 p = _compose3d_madd(&hit->center, u - hit->w * 0.5f, &U);
				p = _compose3d_madd(&p, v - hit->h * 0.5f, &V);
				g3d.grabU = u;
				g3d.grabV = v;
				g3d.grabR = _compose3d_length(&p);
				g3d.dragEntry = hit;
				g3d.dragHandle = hit->handle;
				g3d.dragMode = DEODHAI3D_DRAG_WINDOW;
				hit->depthAnimating = false;
			}
			if (g3d.hovered)
				_compose3d_leave_hovered(x, y, button);
		} else if (hit) {
			Window* w = hit->win;
			WinSharedInfo* info = (WinSharedInfo*)w->sharedInfo;
			if (pressed && tear && tear->armed)
				tear->healing = true; /* pressed elsewhere: cancel the slit */
			if (!(w->flags & WINDOW_FLAG_BLOCKED)) {
				if (pressed) {
					if (_get_focused_window() != w)
						DeodhaiWindowSetFocused(w, true);
					if (!hit->popup && !(w->flags & WINDOW_FLAG_STATIC) &&
						v < DEODHAI3D_TITLEBAR && u >= 10.0f && u < (float)(hit->w - 74)) {
						Vec3 U, V, N;
						_compose3d_axes(hit->facing, &U, &V, &N);
						Vec3 p = _compose3d_madd(&hit->center, u - hit->w * 0.5f, &U);
						p = _compose3d_madd(&p, v - hit->h * 0.5f, &V);
						g3d.grabU = u;
						g3d.grabV = v;
						g3d.grabR = _compose3d_length(&p);
						g3d.dragEntry = hit;
						g3d.dragHandle = hit->handle;
						g3d.dragMode = DEODHAI3D_DRAG_WINDOW;
						hit->depthAnimating = false; /* the hand wins over easing */
					}
				}
				_compose3d_forward(w, info->x + (int)u, info->y + (int)v, x, y, button);
			}
		} else {
			if (pressed)
				g3d.dragMode = DEODHAI3D_DRAG_CAMERA;
			if (g3d.hovered)
				_compose3d_leave_hovered(x, y, button);
		}
	}

	g3d.lastX = x;
	g3d.lastY = y;
	g3d.lastButton = button;
	return true;
}
