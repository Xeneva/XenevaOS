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

/**
 * Accent -- XenevaOS music player, Windows 8 "Metro" style
 *
 *   rootLayout (BorderLayout)            client area
 *   |- TOP     AcHeader     "music"  library | now playing      (pivots)
 *   |- CENTER  AcPages      library: flat tiles, horizontal scroll
 *   |                       now playing: tile, title, spectrum, progress
 *   `- BOTTOM  AcAppBar     track | (prev) (play/pause) (next) | time
 *
 * Audio runs on its own thread (engine.h). This file is the UI thread:
 * it never decodes, never writes to the sound device and never blocks on the
 * engine. It reads the engine's lock-free shared block once per tick.
 *
 * Visualizer budget: a 512 point FFT plus *incremental* bar drawing (only
 * the pixels between a bar's old and new height are touched) and one dirty
 * rectangle per frame, at ~30 fps. The engine gives the CPU away for a few
 * ms after every device write; that window is enough for a frame.
 *
 * Launch: "accent /path/song.mp3" (any argv slot -- static images see the
 * arguments as passed, dynamic ones get the exe path prepended by xeldr)
 * opens the song's folder as the library and starts playing it.
 */

#include <stdint.h>
#include <_xeneva.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/_keproc.h>
#include <sys/_kefile.h>
#include <sys/_ketime.h>
#include <sys/iocodes.h>
#include <sys/mman.h>
#include <chitralekha.h>
#include <keycode.h>
#include <widgets/base.h>
#include <widgets/window.h>
#include <widgets/layout/border_layout.h>

#define MINIMP3_IMPLEMENTATION
#include "engine.h"

/* ------------------------------------------------------------------------ */
/*  Geometry                                                                */
/* ------------------------------------------------------------------------ */

#define AC_WIN_X		 90
#define AC_WIN_Y		 70
#define AC_WIN_W		 900
#define AC_WIN_H		 580
#define AC_TITLEBAR_H	 26
#define AC_HEADER_H		 72
#define AC_APPBAR_H		 86
#define AC_MARGIN		 36
#define AC_TILE			 150
#define AC_TILE_MIN		 84
#define AC_TILE_GAP		 8
#define AC_TILE_ROWS_MAX 4
#define AC_GRID_TOP		 38
#define AC_GRID_BOTTOM	 30
#define AC_PRESS_INSET	 5

#define AC_FFT_N		512
#define AC_FFT_LOG		9
#define AC_MAX_BARS		48
#define AC_VIS_FRAME_MS 33
#define AC_IDLE_TICK_MS 50

#define AC_ANIM_ZOOM_MS	  260
#define AC_ANIM_SPLASH_MS 150
#define AC_ANIM_ENTER_MS  260
#define AC_ANIM_ENTER_DX  48

#define AC_NAME_MAX 32
#define AC_TAG_MAX	64

/* ------------------------------------------------------------------------ */
/*  Palette (Windows 8)                                                     */
/* ------------------------------------------------------------------------ */

#define COL_BG			0xFF1D1D1D
#define COL_APPBAR		0xFF141414
#define COL_TEXT		0xFFFFFFFF
#define COL_DIM			0xFFA0A0A0
#define COL_FAINT		0xFF5C5C5C
#define COL_PIVOT_OFF	0xFF6A6A6A
#define COL_PIVOT_HOVER 0xFFB8B8B8
#define COL_TRACK		0xFF3A3A3A
#define COL_SCROLL		0xFF7A7A7A
#define COL_WARNING		0xFFF0A30A

static const uint32_t g_metro[] = {
	0xFF1BA1E2,
	/* blue    */ 0xFFA200FF,
	/* purple  */ 0xFF00ABA9, /* teal    */
	0xFF8CBF26,
	/* lime    */ 0xFFF09609,
	/* orange  */ 0xFFE51400, /* red     */
	0xFF339933,
	/* green   */ 0xFFD80073,
	/* magenta */ 0xFF0050EF, /* cobalt  */
	0xFF6A00FF,				  /* indigo  */
};
#define AC_METRO_COUNT (sizeof(g_metro) / sizeof(g_metro[0]))

/* ------------------------------------------------------------------------ */
/*  Scancodes (XT set 1)                                                    */
/* ------------------------------------------------------------------------ */

#define SC_RELEASE	 0x80
#define SC_MASK		 0x7F
#define SC_ESC		 0x01
#define SC_BACKSPACE 0x0E
#define SC_TAB		 0x0F
#define SC_ENTER	 0x1C
#define SC_CTRL		 0x1D
#define SC_LSHIFT	 0x2A
#define SC_RSHIFT	 0x36
#define SC_ALT		 0x38
#define SC_SPACE	 0x39
#define SC_CAPSLOCK	 0x3A
#define SC_F5		 0x3F
#define SC_HOME		 0x47
#define SC_UP		 0x48
#define SC_LEFT		 0x4B
#define SC_RIGHT	 0x4D
#define SC_END		 0x4F
#define SC_DOWN		 0x50

/* ------------------------------------------------------------------------ */
/*  Types                                                                   */
/* ------------------------------------------------------------------------ */

enum AcPage { PAGE_LIBRARY = 0, PAGE_NOW = 1 };
enum AcAnimType { ANIM_NONE = 0, ANIM_ZOOM, ANIM_SPLASH, ANIM_ENTER };
enum AcBarButton { BTN_NONE = -1, BTN_PREV = 0, BTN_PLAY, BTN_NEXT, BTN_INFO, BTN_COUNT };

typedef struct _ac_song_ {
	char name[AC_NAME_MAX];
	uint32_t size;
	uint8_t color;
	uint8_t wav;
	uint16_t reserved;
} AcSong; /* 40 bytes */

typedef struct _ac_header_ {
	ChWidget base;
	ChRect pivot[2];
	int hot;
} AcHeader;

typedef struct _ac_pages_ {
	ChWidget base;
} AcPages;

typedef struct _ac_appbar_ {
	ChWidget base;
	int cx[3], cy, r[3];
	ChRect info;
	int hot;
	int pressed;
	bool prevDown;
} AcAppBar;

typedef struct _ac_grid_ {
	int tile, rows, cols;
	int x0, y0; /* first tile, unscrolled */
	int contentW;
	int maxScroll;
	bool scrollbar;
	ChRect track;
} AcGrid;

typedef struct _ac_now_layout_ {
	bool art;
	ChRect artR;
	int textX, textW;
	int labelY, titleY, subY;
	ChRect vis;
	ChRect prog; /* hit area; the bar itself is drawn centred in it */
	int timeY;
} AcNowLayout;

typedef struct _ac_vis_ {
	int bars;
	int bw, gap, bx0;
	int lo[AC_MAX_BARS], hi[AC_MAX_BARS];
	float tilt[AC_MAX_BARS];
	float level[AC_MAX_BARS];
	float peak[AC_MAX_BARS];
	uint64_t hold[AC_MAX_BARS];
	int drawnH[AC_MAX_BARS];
	int drawnCap[AC_MAX_BARS];
	bool drawnValid;
	bool anyUp;
	uint32_t lastWrite;
	uint64_t lastFresh;
	uint64_t lastFrame;
	float window[AC_FFT_N];
	float cosT[AC_FFT_N / 2];
	float sinT[AC_FFT_N / 2];
	uint16_t rev[AC_FFT_N];
	float re[AC_FFT_N];
	float im[AC_FFT_N];
} AcVis;

typedef struct _ac_anim_ {
	int type;
	uint64_t start;
	ChRect from;
	ChRect last;
	uint32_t color;
	int page;
} AcAnim;

typedef struct _ac_app_ {
	ChitralekhaApp* app;
	ChWindow* win;
	ChFont* light; /* Roboto Light for the Metro headings */
	AcHeader* header;
	AcPages* pages;
	AcAppBar* appbar;
	ChBorderLayout* root;
	ChRect clip;

	/* library */
	char dir[AC_PATH_MAX];
	AcSong* songs;
	int count;
	int cap;
	AcGrid grid;
	int scrollX;
	int hotTile;
	int pressedTile;
	int selTile;
	bool libDown;
	bool scrollDrag;
	int dragX0, dragScroll0;

	/* playback (UI side) */
	int cur;
	bool stopped;
	uint32_t reqSeq;
	uint32_t seenEndSeq;
	int pendingPlay;
	char tagTitle[AC_TAG_MAX];
	char tagArtist[AC_TAG_MAX];
	char tagAlbum[AC_TAG_MAX];
	char notice[96];

	/* now playing */
	int page;
	AcNowLayout now;
	bool progHot;
	bool progDrag;
	int progDragX;
	uint32_t shownSec;	  /* progress bar */
	uint32_t shownBarSec; /* app bar clock */
	int shownFill;
	uint32_t shownDur;
	int shownBtnState;

	AcVis vis;
	AcAnim anim;

	uint64_t nextTick;
	bool ready;
	bool resizing;
	bool closing;
} AcApp;

static AcApp g;

static void AcRepaintPages(void);
static void AcRepaintAppBar(void);
static void AcRepaintHeader(void);
static void AcRepaintTile(int i);
static void AcPaintNowProgress(int ox);
static void AcRelayout(void);
static void AcShowPage(int page, bool animate);

/* ------------------------------------------------------------------------ */
/*  Small utilities                                                         */
/* ------------------------------------------------------------------------ */

static void AcStrCopy(char* dst, const char* src, size_t cap) {
	if (!dst || !cap)
		return;
	size_t i = 0;
	if (src)
		for (; i + 1 < cap && src[i]; i++)
			dst[i] = src[i];
	dst[i] = '\0';
}

static int AcStrCaseCmp(const char* a, const char* b) {
	while (*a && AcLowerC((unsigned char)*a) == AcLowerC((unsigned char)*b)) {
		a++;
		b++;
	}
	return AcLowerC((unsigned char)*a) - AcLowerC((unsigned char)*b);
}

static int AcClampI(int v, int lo, int hi) {
	return v < lo ? lo : (v > hi ? hi : v);
}

static float AcClampF(float v, float lo, float hi) {
	return v < lo ? lo : (v > hi ? hi : v);
}

static inline float AcSqrt(float x) {
	if (x <= 0.0f)
		return 0.0f;
#if defined(__aarch64__)
	float r;
	__asm__("fsqrt %s0, %s1" : "=w"(r) : "w"(x));
	return r;
#else
	return __builtin_sqrtf(x);
#endif
}

/* log2 with ~0.01 error -- plenty for a dB meter */
static float AcLog2(float x) {
	if (x <= 1e-30f)
		return -100.0f;
	union {
		float f;
		uint32_t i;
	} u;
	u.f = x;
	float e = (float)((int)((u.i >> 23) & 0xFF) - 127);
	u.i = (u.i & 0x007FFFFF) | 0x3F800000;
	float m = u.f;
	return e + (-0.34484843f * m + 2.02466578f) * m - 1.67487759f; /* log2(m), m in [1,2) */
}

static float AcExp2(float x) {
	if (x < -60.0f)
		return 0.0f;
	int i = (int)x;
	if ((float)i > x)
		i--;
	float f = x - (float)i;
	/* 2^f on [0,1) */
	float p = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * 0.0096181f)));
	union {
		float f;
		uint32_t i;
	} u;
	u.i = (uint32_t)(i + 127) << 23;
	return p * u.f;
}

/* sine / cosine for table setup only (range-reduced Taylor) */
static void AcSinCos(double x, float* s, float* c) {
	const double PI = 3.14159265358979323846;
	while (x > PI)
		x -= 2 * PI;
	while (x < -PI)
		x += 2 * PI;
	double x2 = x * x;
	double sn = x, term = x;
	double cs = 1.0, cterm = 1.0;
	for (int k = 1; k < 12; k++) {
		term *= -x2 / ((2 * k) * (2 * k + 1));
		sn += term;
		cterm *= -x2 / ((2 * k - 1) * (2 * k));
		cs += cterm;
	}
	*s = (float)sn;
	*c = (float)cs;
}

static uint64_t AcNow(void) {
	return _KeGetCurrentMS();
}

static void AcFormatTime(uint32_t ms, char* out, size_t cap) {
	uint32_t s = ms / 1000;
	if (s >= 3600)
		snprintf(out, cap, "%d:%02d:%02d", (int)(s / 3600), (int)((s / 60) % 60), (int)(s % 60));
	else
		snprintf(out, cap, "%d:%02d", (int)(s / 60), (int)(s % 60));
}

/* "my_song.mp3" -> "my song" */
static void AcDisplayName(const char* file, char* out, size_t cap) {
	AcStrCopy(out, file, cap);
	char* dot = strrchr(out, '.');
	if (dot && dot != out)
		*dot = '\0';
	for (char* p = out; *p; p++)
		if (*p == '_')
			*p = ' ';
}

static float AcEaseOut(float t) {
	t = AcClampF(t, 0.0f, 1.0f);
	float u = 1.0f - t;
	return 1.0f - u * u * u;
}

/* ------------------------------------------------------------------------ */
/*  Drawing -- everything clips against g.clip, never writes out of bounds  */
/* ------------------------------------------------------------------------ */

static bool AcClipRect(int* x, int* y, int* w, int* h) {
	ChCanvas* c = g.win->canv;
	if (!c || !c->buffer)
		return false;
	int x0 = *x > g.clip.x ? *x : g.clip.x;
	int y0 = *y > g.clip.y ? *y : g.clip.y;
	int x1 = *x + *w, y1 = *y + *h;
	int cx1 = g.clip.x + g.clip.w, cy1 = g.clip.y + g.clip.h;
	if (x1 > cx1)
		x1 = cx1;
	if (y1 > cy1)
		y1 = cy1;
	if (x0 < 0)
		x0 = 0;
	if (y0 < 0)
		y0 = 0;
	if (x1 > (int)c->canvasWidth)
		x1 = (int)c->canvasWidth;
	if (y1 > (int)c->canvasHeight)
		y1 = (int)c->canvasHeight;
	if (x1 <= x0 || y1 <= y0)
		return false;
	*x = x0;
	*y = y0;
	*w = x1 - x0;
	*h = y1 - y0;
	return true;
}

static void AcFill(int x, int y, int w, int h, uint32_t col) {
	if (!AcClipRect(&x, &y, &w, &h))
		return;
	ChDrawRect(g.win->canv, (unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h, col);
}

static void AcSetClip(int x, int y, int w, int h) {
	g.clip.x = x;
	g.clip.y = y;
	g.clip.w = w > 0 ? w : 0;
	g.clip.h = h > 0 ? h : 0;
}

static void AcClipAll(void) {
	AcSetClip(0, 0, g.win->info->width, g.win->info->height);
}

static void AcClipWidget(const ChWidget* w) {
	AcSetClip(w->x, w->y, w->w, w->h);
}

/* every dirty rectangle goes through here: clipped to the window, and an
 * empty one (tiny window, zero-height page) is never sent */
static void AcUpdate(int x, int y, int w, int h) {
	int W = (int)g.win->info->width, H = (int)g.win->info->height;
	int x1 = x + w, y1 = y + h;
	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	if (x1 > W)
		x1 = W;
	if (y1 > H)
		y1 = H;
	if (x1 <= x || y1 <= y)
		return;
	ChWindowUpdate(g.win, x, y, x1 - x, y1 - y, 0, 1);
}

/* alpha in 0..256 */
static inline void AcBlend(int x, int y, uint32_t col, int a) {
	if (a <= 0)
		return;
	if (x < g.clip.x || y < g.clip.y || x >= g.clip.x + g.clip.w || y >= g.clip.y + g.clip.h)
		return;
	ChCanvas* c = g.win->canv;
	if (x < 0 || y < 0 || x >= (int)c->canvasWidth || y >= (int)c->canvasHeight)
		return;
	uint32_t* p = &c->buffer[(size_t)y * c->canvasWidth + x];
	if (a >= 256) {
		*p = col | 0xFF000000;
		return;
	}
	uint32_t d = *p;
	uint32_t ia = 256 - (uint32_t)a;
	uint32_t r = ((((col >> 16) & 0xFF) * (uint32_t)a) + (((d >> 16) & 0xFF) * ia)) >> 8;
	uint32_t gg = ((((col >> 8) & 0xFF) * (uint32_t)a) + (((d >> 8) & 0xFF) * ia)) >> 8;
	uint32_t b = (((col & 0xFF) * (uint32_t)a) + ((d & 0xFF) * ia)) >> 8;
	*p = 0xFF000000 | (r << 16) | (gg << 8) | b;
}

static uint32_t AcMix(uint32_t a, uint32_t b, int t /*0..256 towards b*/) {
	uint32_t it = 256 - (uint32_t)t;
	uint32_t r = ((((a >> 16) & 0xFF) * it) + (((b >> 16) & 0xFF) * (uint32_t)t)) >> 8;
	uint32_t gg = ((((a >> 8) & 0xFF) * it) + (((b >> 8) & 0xFF) * (uint32_t)t)) >> 8;
	uint32_t bb = (((a & 0xFF) * it) + ((b & 0xFF) * (uint32_t)t)) >> 8;
	return 0xFF000000 | (r << 16) | (gg << 8) | bb;
}

/* coverage anti-aliased disc; thickness > 0 draws a ring instead */
static void AcCircle(float cx, float cy, float r, float thickness, uint32_t col) {
	int x0 = (int)(cx - r - 1), x1 = (int)(cx + r + 2);
	int y0 = (int)(cy - r - 1), y1 = (int)(cy + r + 2);
	float inner = (thickness > 0.0f) ? r - thickness : -1.0f;
	for (int y = y0; y < y1; y++) {
		float dy = (float)y + 0.5f - cy;
		for (int x = x0; x < x1; x++) {
			float dx = (float)x + 0.5f - cx;
			float d = AcSqrt(dx * dx + dy * dy);
			float cov = AcClampF(r - d + 0.5f, 0.0f, 1.0f);
			if (inner >= 0.0f)
				cov -= AcClampF(inner - d + 0.5f, 0.0f, 1.0f);
			if (cov > 0.0f)
				AcBlend(x, y, col, (int)(cov * 256.0f));
		}
	}
}

/* isosceles triangle pointing right (dir = 1) or left (dir = -1), AA tip */
static void AcTriangle(int x, int y, int w, int h, int dir, uint32_t col) {
	float half = (float)h * 0.5f;
	for (int row = 0; row < h; row++) {
		float fy = (float)row + 0.5f - half;
		float span = (float)w * (1.0f - (fy < 0 ? -fy : fy) / half);
		if (span <= 0.0f)
			continue;
		int full = (int)span;
		int frac = (int)((span - (float)full) * 256.0f);
		if (dir > 0) {
			AcFill(x, y + row, full, 1, col);
			AcBlend(x + full, y + row, col, frac);
		} else {
			AcFill(x + w - full, y + row, full, 1, col);
			AcBlend(x + w - full - 1, y + row, col, frac);
		}
	}
}

/* beamed eighth notes, the Metro music glyph, centred on (cx, cy) */
static void AcNotes(int cx, int cy, int s, uint32_t col) {
	if (s < 8)
		return;
	float rh = (float)s * 0.15f;
	int sw = s / 14 > 2 ? s / 14 : 2;
	int bt = s / 7 > 3 ? s / 7 : 3;
	float h1x = (float)cx - (float)s * 0.26f, h1y = (float)cy + (float)s * 0.30f;
	float h2x = (float)cx + (float)s * 0.28f, h2y = (float)cy + (float)s * 0.22f;
	int top1 = cy - (int)((float)s * 0.34f);
	int top2 = cy - (int)((float)s * 0.44f);
	int s1x = (int)(h1x + rh) - sw;
	int s2x = (int)(h2x + rh) - sw;
	AcCircle(h1x, h1y, rh, 0.0f, col);
	AcCircle(h2x, h2y, rh, 0.0f, col);
	AcFill(s1x, top1, sw, (int)h1y - top1, col);
	AcFill(s2x, top2, sw, (int)h2y - top2, col);
	int span = (s2x + sw) - s1x;
	for (int i = 0; i < span; i++) {
		int yt = top1 + ((top2 - top1) * i) / (span > 1 ? span - 1 : 1);
		AcFill(s1x + i, yt, 1, bt, col);
	}
}

/* ------------------------------------------------------------------------ */
/*  Text                                                                    */
/* ------------------------------------------------------------------------ */

static ChFont* AcLight(void) {
	return g.light ? g.light : g.app->baseFont;
}

static ChFont* AcBase(void) {
	return g.app->baseFont;
}

static int AcTextW(ChFont* f, int size, const char* s) {
	if (!f || !s || !*s)
		return 0;
	ChFontSetSize(f, size);
	return (int)ChFontGetWidth(f, (char*)s);
}

static void
AcText(ChFont* f, int size, const char* s, int x, int baseline, uint32_t col, int maxW) {
	if (!f || !s || !*s || maxW <= 0)
		return;
	ChRect c;
	c.x = x;
	c.y = baseline - size - size / 2;
	c.w = maxW;
	c.h = size * 2;
	/* intersect with the active clip */
	int x1 = c.x + c.w, y1 = c.y + c.h;
	if (c.x < g.clip.x)
		c.x = g.clip.x;
	if (c.y < g.clip.y)
		c.y = g.clip.y;
	if (x1 > g.clip.x + g.clip.w)
		x1 = g.clip.x + g.clip.w;
	if (y1 > g.clip.y + g.clip.h)
		y1 = g.clip.y + g.clip.h;
	c.w = x1 - c.x;
	c.h = y1 - c.y;
	if (c.w <= 0 || c.h <= 0 || x < c.x || baseline < c.y)
		return;
	ChFontSetSize(f, size);
	ChFontDrawTextClipped(g.win->canv, f, (char*)s, x, baseline, col, &c);
}

/* fit into maxW with a trailing "..." -- binary search, O(log len) widths */
static void AcElide(ChFont* f, int size, const char* src, int maxW, char* out, size_t cap) {
	AcStrCopy(out, src, cap);
	if (maxW <= 0) {
		out[0] = '\0';
		return;
	}
	if (AcTextW(f, size, out) <= maxW)
		return;
	int len = (int)strlen(out);
	int lo = 0, hi = len;
	char probe[AC_PATH_MAX];
	while (lo < hi) {
		int mid = (lo + hi + 1) / 2;
		if ((size_t)mid + 4 > sizeof(probe)) {
			hi = mid - 1;
			continue;
		}
		memmove(probe, src, (size_t)mid);
		memmove(probe + mid, "...", 4);
		if (AcTextW(f, size, probe) <= maxW)
			lo = mid;
		else
			hi = mid - 1;
	}
	if ((size_t)lo + 4 > cap)
		lo = (int)cap - 4;
	memmove(out, src, (size_t)lo);
	memmove(out + lo, "...", 4);
}

static void
AcTextFit(ChFont* f, int size, const char* s, int x, int baseline, uint32_t col, int maxW) {
	char buf[AC_PATH_MAX];
	AcElide(f, size, s, maxW, buf, sizeof(buf));
	AcText(f, size, buf, x, baseline, col, maxW);
}

/* ------------------------------------------------------------------------ */
/*  Library                                                                 */
/* ------------------------------------------------------------------------ */

static uint8_t AcColorFor(const char* name) {
	uint32_t h = 2166136261u; /* FNV-1a over the lower-cased name: stable colours */
	for (const char* p = name; *p; p++) {
		h ^= (uint32_t)AcLowerC((unsigned char)*p);
		h *= 16777619u;
	}
	return (uint8_t)(h % AC_METRO_COUNT);
}

static bool AcReserveSongs(int need) {
	if (need <= g.cap)
		return true;
	int cap = g.cap ? g.cap * 2 : 16;
	while (cap < need)
		cap *= 2;
	AcSong* s = (AcSong*)realloc(g.songs, sizeof(AcSong) * (size_t)cap);
	if (!s)
		return false;
	g.songs = s;
	g.cap = cap;
	return true;
}

/* stable bottom-up merge sort by name: O(n log n), one scratch block */
static void AcSortSongs(void) {
	int n = g.count;
	if (n < 2)
		return;
	AcSong* tmp = (AcSong*)malloc(sizeof(AcSong) * (size_t)n);
	if (!tmp)
		return;
	AcSong* a = g.songs;
	AcSong* t = tmp;
	for (int width = 1; width < n; width <<= 1) {
		for (int lo = 0; lo < n; lo += 2 * width) {
			int mid = lo + width < n ? lo + width : n;
			int hi = lo + 2 * width < n ? lo + 2 * width : n;
			int i = lo, j = mid, k = lo;
			while (i < mid && j < hi)
				t[k++] = (AcStrCaseCmp(a[j].name, a[i].name) < 0) ? a[j++] : a[i++];
			while (i < mid)
				t[k++] = a[i++];
			while (j < hi)
				t[k++] = a[j++];
		}
		AcSong* sw = a;
		a = t;
		t = sw;
	}
	if (a != g.songs)
		memmove(g.songs, a, sizeof(AcSong) * (size_t)n);
	free(tmp);
}

static void AcLoadLibrary(const char* dir) {
	AcStrCopy(g.dir, dir, sizeof(g.dir));
	g.count = 0;
	int fd = _KeOpenDir((char*)dir);
	if (fd < 0)
		return;

	XEDirectoryEntry de;
	memset(&de, 0, sizeof(de));
	int prev = -1, guard = 0;
	while (guard++ < 4096) {
		if (de.index == -1)
			break;
		prev = de.index;
		de.flags = 0;
		de.size = 0;
		memset(de.filename, 0, sizeof(de.filename));
		int code = _KeReadDir(fd, &de);
		if (de.index == -1)
			break;
		if (code == -1 && de.index == prev)
			break;
		if (code == -1 || !de.filename[0] || !(de.flags & FILE_GENERAL))
			continue;
		de.filename[sizeof(de.filename) - 1] = '\0';
		if (!AcIsAudioName(de.filename) || !AcReserveSongs(g.count + 1))
			continue;
		AcSong* s = &g.songs[g.count++];
		memset(s, 0, sizeof(*s));
		AcStrCopy(s->name, de.filename, sizeof(s->name));
		s->size = (uint32_t)de.size;
		s->color = AcColorFor(s->name);
		s->wav = AcHasExt(s->name, "wav") ? 1 : 0;
	}
	_KeCloseFile(fd);
	AcSortSongs();

	/* hand back memory if the folder shrank a lot */
	if (g.cap > 64 && g.count * 4 < g.cap) {
		int cap = 16;
		while (cap < g.count * 2)
			cap *= 2;
		AcSong* s = (AcSong*)realloc(g.songs, sizeof(AcSong) * (size_t)cap);
		if (s) {
			g.songs = s;
			g.cap = cap;
		}
	}
}

static int AcFindSong(const char* name) {
	for (int i = 0; i < g.count; i++)
		if (AcStrCaseCmp(g.songs[i].name, name) == 0)
			return i;
	return -1;
}

static void AcSongPath(int i, char* out, size_t cap) {
	if (g.dir[0] == '/' && g.dir[1] == '\0')
		snprintf(out, cap, "/%s", g.songs[i].name);
	else
		snprintf(out, cap, "%s/%s", g.dir, g.songs[i].name);
}

static uint32_t AcSongColor(int i) {
	if (i < 0 || i >= g.count)
		return g_metro[0];
	return g_metro[g.songs[i].color];
}

/* ------------------------------------------------------------------------ */
/*  ID3 text frames (title / artist / album)                                */
/*  Streams through the tag with aligned 4K block reads and seeks; picture  */
/*  frames are skipped, never loaded.                                       */
/* ------------------------------------------------------------------------ */

typedef struct {
	int fd;
	uint32_t size;
	int32_t block;
	uint8_t buf[AC_FILE_BLOCK];
} AcTagReader;

static AcTagReader g_tag; /* UI thread only */

static int AcTagRead(AcTagReader* r, uint32_t off, uint8_t* dst, int n) {
	int got = 0;
	while (got < n && off < r->size) {
		int32_t blk = (int32_t)(off / AC_FILE_BLOCK);
		if (blk != r->block) {
			if (_KeFileSetOffset(r->fd, (size_t)blk * AC_FILE_BLOCK) != 0)
				break;
			size_t rd = _KeReadFile(r->fd, r->buf, AC_FILE_BLOCK);
			if (rd == 0 || rd == (size_t)-1)
				break;
			r->block = blk;
		}
		uint32_t in = off % AC_FILE_BLOCK;
		int k = AC_FILE_BLOCK - (int)in;
		if (k > n - got)
			k = n - got;
		if ((uint32_t)k > r->size - off)
			k = (int)(r->size - off);
		memmove(dst + got, r->buf + in, (size_t)k);
		got += k;
		off += (uint32_t)k;
	}
	return got;
}

/* ID3 text payload -> UTF-8 */
static void AcTagText(const uint8_t* p, int n, char* out, int cap) {
	int w = 0;
	if (n <= 1) {
		out[0] = '\0';
		return;
	}
	int enc = p[0];
	p++;
	n--;
	if (enc == 1 || enc == 2) { /* UTF-16 (with BOM) / UTF-16BE */
		bool be = (enc == 2);
		if (n >= 2 && enc == 1) {
			if (p[0] == 0xFE && p[1] == 0xFF)
				be = true;
			if ((p[0] == 0xFE && p[1] == 0xFF) || (p[0] == 0xFF && p[1] == 0xFE)) {
				p += 2;
				n -= 2;
			}
		}
		for (int i = 0; i + 1 < n; i += 2) {
			uint32_t ch =
				be ? (uint32_t)((p[i] << 8) | p[i + 1]) : (uint32_t)(p[i] | (p[i + 1] << 8));
			if (ch == 0)
				break;
			if (ch >= 0xD800 && ch <= 0xDFFF)
				ch = '?';
			if (ch < 0x80) {
				if (w + 1 >= cap)
					break;
				out[w++] = (char)ch;
			} else if (ch < 0x800) {
				if (w + 2 >= cap)
					break;
				out[w++] = (char)(0xC0 | (ch >> 6));
				out[w++] = (char)(0x80 | (ch & 0x3F));
			} else {
				if (w + 3 >= cap)
					break;
				out[w++] = (char)(0xE0 | (ch >> 12));
				out[w++] = (char)(0x80 | ((ch >> 6) & 0x3F));
				out[w++] = (char)(0x80 | (ch & 0x3F));
			}
		}
	} else if (enc == 3) { /* UTF-8: copy, never cut inside a sequence */
		for (int i = 0; i < n && p[i]; i++) {
			int len = (p[i] < 0x80) ? 1 : (p[i] >= 0xF0) ? 4 : (p[i] >= 0xE0) ? 3 : 2;
			if (w + len >= cap || i + len > n)
				break;
			for (int k = 0; k < len; k++)
				out[w++] = (char)p[i + k];
			i += len - 1;
		}
	} else { /* ISO-8859-1 */
		for (int i = 0; i < n && p[i]; i++) {
			if (p[i] < 0x80) {
				if (w + 1 >= cap)
					break;
				out[w++] = (char)p[i];
			} else {
				if (w + 2 >= cap)
					break;
				out[w++] = (char)(0xC0 | (p[i] >> 6));
				out[w++] = (char)(0x80 | (p[i] & 0x3F));
			}
		}
	}
	out[w] = '\0';
	while (w > 0 && out[w - 1] == ' ')
		out[--w] = '\0';
}

static void AcReadTags(const char* path) {
	g.tagTitle[0] = g.tagArtist[0] = g.tagAlbum[0] = '\0';
	if (!AcHasExt(path, "mp3"))
		return;
	AcTagReader* r = &g_tag;
	r->fd = _KeOpenFile((char*)path, FILE_OPEN_READ_ONLY);
	if (r->fd < 0)
		return;
	XEFileStatus st;
	memset(&st, 0, sizeof(st));
	_KeFileStat(r->fd, &st);
	r->size = (uint32_t)st.size;
	r->block = -1;

	uint8_t h[10];
	if (AcTagRead(r, 0, h, 10) == 10 && memcmp(h, "ID3", 3) == 0) {
		int ver = h[3];
		uint32_t end = 10 + AcSynchsafe(h + 6);
		uint32_t p = 10;
		if (h[5] & 0x40) { /* extended header */
			uint8_t e[4];
			if (AcTagRead(r, p, e, 4) == 4)
				p += (ver >= 4) ? AcSynchsafe(e)
								: (4 + ((uint32_t)e[0] << 24 | (uint32_t)e[1] << 16 |
										(uint32_t)e[2] << 8 | e[3]));
		}
		int hdr = (ver == 2) ? 6 : 10;
		int found = 0;
		for (int guard = 0; guard < 96 && found < 3 && p + (uint32_t)hdr <= end; guard++) {
			uint8_t f[10];
			if (AcTagRead(r, p, f, hdr) != hdr || f[0] == 0)
				break;
			uint32_t size;
			if (ver == 2)
				size = ((uint32_t)f[3] << 16) | ((uint32_t)f[4] << 8) | f[5];
			else if (ver >= 4)
				size = AcSynchsafe(f + 4);
			else
				size =
					((uint32_t)f[4] << 24) | ((uint32_t)f[5] << 16) | ((uint32_t)f[6] << 8) | f[7];
			char* dst = NULL;
			if (ver == 2) {
				if (!memcmp(f, "TT2", 3))
					dst = g.tagTitle;
				else if (!memcmp(f, "TP1", 3))
					dst = g.tagArtist;
				else if (!memcmp(f, "TAL", 3))
					dst = g.tagAlbum;
			} else {
				if (!memcmp(f, "TIT2", 4))
					dst = g.tagTitle;
				else if (!memcmp(f, "TPE1", 4))
					dst = g.tagArtist;
				else if (!memcmp(f, "TALB", 4))
					dst = g.tagAlbum;
			}
			if (dst && size > 0 && !dst[0]) {
				uint8_t txt[160];
				int n = (int)(size < sizeof(txt) ? size : sizeof(txt));
				n = AcTagRead(r, p + (uint32_t)hdr, txt, n);
				AcTagText(txt, n, dst, AC_TAG_MAX);
				found++;
			}
			p += (uint32_t)hdr + size; /* APIC & co. are skipped, not read */
		}
	}
	_KeCloseFile(r->fd);
	r->fd = -1;
}

/* ------------------------------------------------------------------------ */
/*  Playback control (UI side of the handshake)                             */
/* ------------------------------------------------------------------------ */

static bool AcEngineReady(void) {
	return AcLoad(&g_sh.ackSeq) == AcLoad(&g_sh.cmdSeq);
}

static bool AcSendPlay(int i) {
	if (!AcEngineReady())
		return false;
	AcSongPath(i, g_sh.path, sizeof(g_sh.path));
	AcStoreI(&g_sh.cmd, AC_CMD_PLAY);
	g.reqSeq = g_sh.cmdSeq + 1;
	AcStore(&g_sh.cmdSeq, g.reqSeq);
	return true;
}

static bool AcIsPaused(void) {
	return AcLoad(&g_sh.paused) != 0;
}

static bool AcIsPlaying(void) {
	return g.cur >= 0 && !g.stopped && !AcIsPaused();
}

static void AcResetVisualizer(void) {
	for (int i = 0; i < AC_MAX_BARS; i++) {
		g.vis.level[i] = 0.0f;
		g.vis.peak[i] = 0.0f;
	}
	g.vis.lastWrite = AcLoad(&g_sh.visWrite);
}

static void AcPlaySong(int i) {
	if (i < 0 || i >= g.count)
		return;
	int old = g.cur;
	g.cur = i;
	g.selTile = i;
	g.stopped = false;
	g.notice[0] = '\0';
	AcStore(&g_sh.paused, 0);
	g.pendingPlay = AcSendPlay(i) ? -1 : i;

	char path[AC_PATH_MAX];
	AcSongPath(i, path, sizeof(path));
	AcReadTags(path);

	AcResetVisualizer();
	g.shownSec = 0xFFFFFFFF;
	if (old != i)
		AcRepaintTile(old);
	AcRepaintTile(i);
	if (g.page == PAGE_NOW && g.anim.type == ANIM_NONE)
		AcRepaintPages();
	AcRepaintAppBar();
}

static void AcTogglePlay(void) {
	if (g.count == 0)
		return;
	if (g.cur < 0) {
		AcPlaySong(g.selTile >= 0 ? g.selTile : 0);
		return;
	}
	if (g.stopped) {
		AcPlaySong(g.cur);
		return;
	}
	AcStore(&g_sh.paused, AcIsPaused() ? 0 : 1);
	AcRepaintAppBar();
	if (g.page == PAGE_NOW)
		AcRepaintPages(); /* "now playing" <-> "paused" */
}

static void AcNext(void) {
	if (g.count == 0)
		return;
	AcPlaySong(g.cur < 0 ? 0 : (g.cur + 1) % g.count);
}

static void AcPrev(void) {
	if (g.count == 0)
		return;
	if (g.cur >= 0 && !g.stopped && AcLoad(&g_sh.posMs) > 3000) {
		AcStoreI(&g_sh.seekPermille, 0); /* restart, like every player does */
		return;
	}
	AcPlaySong(g.cur <= 0 ? g.count - 1 : g.cur - 1);
}

static void AcSeekTo(uint32_t ms) {
	uint32_t dur = AcLoad(&g_sh.durMs);
	if (!dur || g.cur < 0 || g.stopped)
		return;
	if (ms > dur)
		ms = dur;
	AcStoreI(&g_sh.seekPermille, (int32_t)(((uint64_t)ms * 1000) / dur));
}

static void AcSeekBy(int deltaMs) {
	int64_t t = (int64_t)AcLoad(&g_sh.posMs) + deltaMs;
	AcSeekTo(t < 0 ? 0 : (uint32_t)t);
}

/* engine -> UI: retry a pending command, react to end of track */
static void AcServiceEngine(void) {
	if (g.pendingPlay >= 0 && AcSendPlay(g.pendingPlay))
		g.pendingPlay = -1;

	uint32_t es = AcLoad(&g_sh.endSeq);
	if (es == g.seenEndSeq)
		return;
	g.seenEndSeq = es;
	if (AcLoad(&g_sh.endTrackSeq) != g.reqSeq || g.pendingPlay >= 0)
		return; /* a track we already moved away from */

	if (AcLoad(&g_sh.endReason) == AC_STATE_ERROR) {
		char name[AC_NAME_MAX];
		AcDisplayName(g.songs[g.cur].name, name, sizeof(name));
		snprintf(g.notice, sizeof(g.notice), "can't play \"%s\"", name);
		g.stopped = true;
		AcRepaintAppBar();
		return;
	}
	if (g.cur >= 0 && g.cur + 1 < g.count) {
		AcPlaySong(g.cur + 1);
	} else {
		g.stopped = true; /* end of the list */
		AcRepaintTile(g.cur);
		AcRepaintAppBar();
		if (g.page == PAGE_NOW)
			AcRepaintPages();
	}
}

/* ------------------------------------------------------------------------ */
/*  Spectrum                                                                */
/* ------------------------------------------------------------------------ */

static void AcVisInit(void) {
	AcVis* v = &g.vis;
	for (int i = 0; i < AC_FFT_N; i++) {
		float s, c;
		AcSinCos(2.0 * 3.14159265358979323846 * i / (AC_FFT_N - 1), &s, &c);
		v->window[i] = 0.5f - 0.5f * c; /* Hann */
		uint16_t r = 0;
		for (int b = 0; b < AC_FFT_LOG; b++)
			if (i & (1 << b))
				r |= (uint16_t)(1 << (AC_FFT_LOG - 1 - b));
		v->rev[i] = r;
	}
	for (int k = 0; k < AC_FFT_N / 2; k++)
		AcSinCos(-2.0 * 3.14159265358979323846 * k / AC_FFT_N, &v->sinT[k], &v->cosT[k]);
	v->bars = 0;
	v->drawnValid = false;
}

/* in-place radix-2 FFT, O(N log N) */
static void AcFFT(float* re, float* im) {
	AcVis* v = &g.vis;
	for (int i = 0; i < AC_FFT_N; i++) {
		int j = v->rev[i];
		if (j > i) {
			float t = re[i];
			re[i] = re[j];
			re[j] = t;
			t = im[i];
			im[i] = im[j];
			im[j] = t;
		}
	}
	for (int len = 2; len <= AC_FFT_N; len <<= 1) {
		int half = len >> 1;
		int step = AC_FFT_N / len;
		for (int i = 0; i < AC_FFT_N; i += len) {
			for (int k = 0; k < half; k++) {
				float wr = v->cosT[k * step], wi = v->sinT[k * step];
				int a = i + k, b = a + half;
				float xr = re[b] * wr - im[b] * wi;
				float xi = re[b] * wi + im[b] * wr;
				re[b] = re[a] - xr;
				im[b] = im[a] - xi;
				re[a] += xr;
				im[a] += xi;
			}
		}
	}
}

/* log-spaced bands, 50 Hz .. 16 kHz, each owning at least one FFT bin */
static void AcVisMapBands(int bars) {
	AcVis* v = &g.vis;
	v->bars = bars;
	const float binHz = (float)AC_OUT_HZ / AC_FFT_N;
	const float fmin = 50.0f, fmax = 16000.0f;
	float span = AcLog2(fmax / fmin);
	int prevHi = 1;
	for (int i = 0; i < bars; i++) {
		float f0 = fmin * AcExp2(span * (float)i / bars);
		float f1 = fmin * AcExp2(span * (float)(i + 1) / bars);
		int lo = (int)(f0 / binHz + 0.5f);
		int hi = (int)(f1 / binHz + 0.5f);
		if (lo < prevHi)
			lo = prevHi;
		if (hi <= lo)
			hi = lo + 1;
		if (hi > AC_FFT_N / 2)
			hi = AC_FFT_N / 2;
		if (lo >= hi)
			lo = hi - 1;
		v->lo[i] = lo;
		v->hi[i] = hi;
		prevHi = hi;
		/* music falls ~3 dB/octave: tilt it back so the treble moves too */
		float fc = AcSqrt(f0 * f1);
		v->tilt[i] = AcClampF(3.0f * AcLog2(fc / 1000.0f), -6.0f, 9.0f);
		v->level[i] = v->peak[i] = 0.0f;
		v->hold[i] = 0;
	}
	v->drawnValid = false;
}

/* newest AC_FFT_N samples from the engine ring -> per-band targets 0..1 */
static void AcVisAnalyse(uint32_t w, float* target) {
	AcVis* v = &g.vis;
	for (int i = 0; i < AC_FFT_N; i++) {
		int16_t s = g_sh.vis[(w - AC_FFT_N + (uint32_t)i) & (AC_VIS_RING - 1)];
		v->re[i] = (float)s * v->window[i];
		v->im[i] = 0.0f;
	}
	AcFFT(v->re, v->im);
	/* full-scale sine with a Hann window peaks at 32768 * N / 4 */
	const float ref = 32768.0f * AC_FFT_N / 4.0f;
	const float invRef2 = 1.0f / (ref * ref);
	for (int b = 0; b < v->bars; b++) {
		float p = 0.0f;
		for (int k = v->lo[b]; k < v->hi[b]; k++) {
			float m = v->re[k] * v->re[k] + v->im[k] * v->im[k];
			if (m > p)
				p = m;
		}
		float db = 3.0103f * AcLog2(p * invRef2) + v->tilt[b]; /* 10*log10 */
		target[b] = AcClampF((db + 54.0f) / 54.0f, 0.0f, 1.0f);
	}
}

static void AcVisLayoutBars(void) {
	AcVis* v = &g.vis;
	ChRect* r = &g.now.vis;
	if (r->w <= 0 || r->h <= 0)
		return;
	int bars = AcClampI(r->w / 16, 12, AC_MAX_BARS);
	int gap = r->w < 360 ? 2 : 3;
	int bw = (r->w - gap * (bars - 1)) / bars;
	if (bw < 2) {
		bw = 2;
		bars = AcClampI((r->w + gap) / (bw + gap), 1, AC_MAX_BARS);
	}
	v->gap = gap;
	v->bw = bw;
	v->bx0 = r->x + (r->w - (bars * bw + (bars - 1) * gap)) / 2;
	if (bars != v->bars)
		AcVisMapBands(bars);
	v->drawnValid = false;
}

static int AcCapY(float peak) {
	ChRect* r = &g.now.vis;
	int ph = (int)(peak * (float)r->h + 0.5f);
	if (ph <= 0)
		return -1;
	int y = r->y + r->h - ph - 4;
	return y < r->y ? r->y : y;
}

/* full paint of the spectrum area (page paints, entrance animation) */
static void AcVisPaintFull(int ox) {
	AcVis* v = &g.vis;
	ChRect* r = &g.now.vis;
	if (r->w <= 0 || r->h <= 0 || v->bars <= 0)
		return;
	uint32_t col = AcSongColor(g.cur);
	AcFill(r->x + ox, r->y, r->w, r->h, COL_BG);
	int base = r->y + r->h;
	for (int b = 0; b < v->bars; b++) {
		int x = v->bx0 + b * (v->bw + v->gap) + ox;
		int h = (int)(v->level[b] * (float)r->h + 0.5f);
		AcFill(x, base - h, v->bw, h, col);
		int cy = AcCapY(v->peak[b]);
		if (cy >= 0 && cy + 2 <= base - h)
			AcFill(x, cy, v->bw, 2, COL_TEXT);
		if (ox == 0) {
			v->drawnH[b] = h;
			v->drawnCap[b] = (cy >= 0 && cy + 2 <= base - h) ? cy : -1;
		}
	}
	if (ox == 0)
		v->drawnValid = true;
}

/*
 * AcVisFrame -- one visualizer step. Bars only redraw the pixels that changed
 * (old top .. new top), caps move as 2px strips, and one dirty rectangle
 * covers the touched rows. Returns true while something is still moving.
 */
static bool AcVisFrame(uint64_t now) {
	AcVis* v = &g.vis;
	ChRect* r = &g.now.vis;
	if (r->w <= 0 || r->h <= 0 || v->bars <= 0 || g.cur < 0)
		return false;

	float dt = (float)(now - v->lastFrame);
	if (v->lastFrame == 0 || dt > 100.0f)
		dt = (float)AC_VIS_FRAME_MS;
	v->lastFrame = now;

	float target[AC_MAX_BARS];
	uint32_t w = AcLoad(&g_sh.visWrite);
	bool live = AcIsPlaying() && AcLoad(&g_sh.state) == AC_STATE_PLAYING;
	if (live && w != v->lastWrite && w >= AC_FFT_N) {
		AcVisAnalyse(w, target);
		v->lastWrite = w;
		v->lastFresh = now;
	} else if (live && now - v->lastFresh < 150) {
		for (int b = 0; b < v->bars; b++)
			target[b] = v->level[b]; /* no new block yet: hold */
	} else {
		for (int b = 0; b < v->bars; b++)
			target[b] = 0.0f;
	}

	bool moving = false;
	for (int b = 0; b < v->bars; b++) {
		float lv = v->level[b];
		if (target[b] > lv)
			lv += (target[b] - lv) * 0.65f; /* fast attack */
		else
			lv = (lv - dt * 0.0016f > target[b]) ? lv - dt * 0.0016f : target[b]; /* ~1.6/s fall */
		v->level[b] = lv;
		if (lv >= v->peak[b]) {
			v->peak[b] = lv;
			v->hold[b] = now + 380;
		} else if (now > v->hold[b]) {
			v->peak[b] -= dt * 0.0007f;
			if (v->peak[b] < lv)
				v->peak[b] = lv;
		}
		if (lv > 0.002f || v->peak[b] > 0.002f)
			moving = true;
	}

	if (!v->drawnValid || g.page != PAGE_NOW || g.anim.type != ANIM_NONE || !g.ready || g.resizing)
		return moving;

	AcClipWidget(&g.pages->base);
	uint32_t col = AcSongColor(g.cur);
	int base = r->y + r->h;
	int dirtyTop = base, dirtyBot = r->y;
	for (int b = 0; b < v->bars; b++) {
		int x = v->bx0 + b * (v->bw + v->gap);
		int h = (int)(v->level[b] * (float)r->h + 0.5f);
		int oh = v->drawnH[b];
		int cy = AcCapY(v->peak[b]);
		if (cy >= 0 && cy + 2 > base - h)
			cy = -1;
		int ocy = v->drawnCap[b];
		if (h == oh && cy == ocy)
			continue;

		if (ocy >= 0 && ocy != cy) { /* erase the old cap where the bar is not */
			int top = ocy, bot = ocy + 2;
			if (bot > base - h)
				bot = base - h;
			if (bot > top)
				AcFill(x, top, v->bw, bot - top, COL_BG);
			if (ocy < dirtyTop)
				dirtyTop = ocy;
			if (ocy + 2 > dirtyBot)
				dirtyBot = ocy + 2;
		}
		if (h > oh)
			AcFill(x, base - h, v->bw, h - oh, col);
		else if (h < oh)
			AcFill(x, base - oh, v->bw, oh - h, COL_BG);
		if (h != oh) {
			int top = base - (h > oh ? h : oh);
			if (top < dirtyTop)
				dirtyTop = top;
			if (base > dirtyBot)
				dirtyBot = base;
		}
		if (cy >= 0 && cy != ocy) {
			AcFill(x, cy, v->bw, 2, COL_TEXT);
			if (cy < dirtyTop)
				dirtyTop = cy;
			if (cy + 2 > dirtyBot)
				dirtyBot = cy + 2;
		}
		v->drawnH[b] = h;
		v->drawnCap[b] = cy;
	}
	if (dirtyBot > dirtyTop)
		AcUpdate(r->x, dirtyTop, r->w, dirtyBot - dirtyTop);
	return moving;
}

/* ------------------------------------------------------------------------ */
/*  Layout                                                                  */
/* ------------------------------------------------------------------------ */

static void AcLayoutGrid(void) {
	ChWidget* p = &g.pages->base;
	AcGrid* gr = &g.grid;
	int avail = p->h - AC_GRID_TOP - AC_GRID_BOTTOM;
	gr->tile = AC_TILE;
	gr->rows = (avail + AC_TILE_GAP) / (AC_TILE + AC_TILE_GAP);
	if (gr->rows < 1) {
		/* short window: one row of smaller tiles that still clears the bar */
		gr->rows = 1;
		gr->tile = AcClampI(avail, 24, AC_TILE);
	}
	if (gr->rows > AC_TILE_ROWS_MAX)
		gr->rows = AC_TILE_ROWS_MAX;
	gr->cols = g.count ? (g.count + gr->rows - 1) / gr->rows : 0;
	gr->x0 = p->x + AC_MARGIN;
	gr->y0 = p->y + AC_GRID_TOP;
	int step = gr->tile + AC_TILE_GAP;
	gr->contentW = gr->cols ? gr->cols * step - AC_TILE_GAP + 2 * AC_MARGIN : 0;
	gr->maxScroll = gr->contentW > p->w ? gr->contentW - p->w : 0;
	gr->scrollbar = gr->maxScroll > 0;
	gr->track.x = p->x + AC_MARGIN;
	gr->track.w = p->w - 2 * AC_MARGIN;
	gr->track.y = p->y + p->h - 18;
	gr->track.h = 12; /* hit area; the bar is 4px (8px when hot) */
	g.scrollX = AcClampI(g.scrollX, 0, gr->maxScroll);
}

static void AcTileRect(int i, ChRect* r) {
	AcGrid* gr = &g.grid;
	int step = gr->tile + AC_TILE_GAP;
	r->x = gr->x0 + (i / gr->rows) * step - g.scrollX;
	r->y = gr->y0 + (i % gr->rows) * step;
	r->w = r->h = gr->tile;
}

/* O(1): tile under a point, -1 for gaps */
static int AcTileAt(int x, int y) {
	AcGrid* gr = &g.grid;
	if (!g.count || gr->rows <= 0)
		return -1;
	int step = gr->tile + AC_TILE_GAP;
	int lx = x - gr->x0 + g.scrollX, ly = y - gr->y0;
	if (lx < 0 || ly < 0)
		return -1;
	int col = lx / step, row = ly / step;
	if (lx % step >= gr->tile || ly % step >= gr->tile || row >= gr->rows)
		return -1;
	int i = col * gr->rows + row;
	return i < g.count ? i : -1;
}

static void AcThumb(int* tx, int* tw) {
	AcGrid* gr = &g.grid;
	ChWidget* p = &g.pages->base;
	int w = gr->contentW ? (gr->track.w * p->w) / gr->contentW : gr->track.w;
	if (w < 40)
		w = 40;
	if (w > gr->track.w)
		w = gr->track.w;
	*tw = w;
	*tx = gr->track.x + (gr->maxScroll ? ((gr->track.w - w) * g.scrollX) / gr->maxScroll : 0);
}

static void AcLayoutNow(void) {
	ChWidget* p = &g.pages->base;
	AcNowLayout* n = &g.now;
	int top = p->y + 24;
	int s = p->h - 24 - 44;
	if (s > 300)
		s = 300;
	if (s > (p->w * 2) / 5)
		s = (p->w * 2) / 5;
	n->art = (p->w >= 640 && s >= 140);
	if (n->art) {
		n->artR.x = p->x + AC_MARGIN;
		n->artR.y = top;
		n->artR.w = n->artR.h = s;
		n->textX = n->artR.x + s + AC_MARGIN;
	} else {
		memset(&n->artR, 0, sizeof(n->artR));
		n->textX = p->x + AC_MARGIN;
	}
	n->textW = p->x + p->w - AC_MARGIN - n->textX;
	n->labelY = top + 12;
	n->titleY = n->labelY + 40;
	n->subY = n->titleY + 28;

	n->prog.x = n->textX;
	n->prog.w = n->textW;
	n->prog.h = 16;
	n->prog.y = p->y + p->h - 58;
	n->timeY = n->prog.y + n->prog.h + 18;

	n->vis.x = n->textX;
	n->vis.w = n->textW;
	n->vis.y = n->subY + 22;
	n->vis.h = n->prog.y - 10 - n->vis.y;
	if (n->vis.h < 36 || n->vis.w < 120)
		memset(&n->vis, 0, sizeof(n->vis));
	AcVisLayoutBars();
}

static void AcLayoutAppBar(void) {
	AcAppBar* a = g.appbar;
	ChWidget* w = &a->base;
	int cx = w->x + w->w / 2;
	a->cy = w->y + 34;
	a->cx[BTN_PREV] = cx - 78;
	a->cx[BTN_PLAY] = cx;
	a->cx[BTN_NEXT] = cx + 78;
	a->r[BTN_PREV] = a->r[BTN_NEXT] = 19;
	a->r[BTN_PLAY] = 24;
	a->info.x = w->x + 24;
	a->info.y = w->y + 18;
	a->info.h = 46;
	a->info.w = (a->cx[BTN_PREV] - 19 - 24) - a->info.x;
	if (a->info.w < 46)
		a->info.w = 0;
}

static void AcLayoutHeader(void) {
	AcHeader* h = g.header;
	ChWidget* w = &h->base;
	static const char* const names[2] = {"library", "now playing"};
	int x = w->x + AC_MARGIN + AcTextW(AcLight(), 34, "music") + 34;
	if (w->w < 520)
		x = w->x + AC_MARGIN; /* narrow: the pivots replace the title */
	for (int i = 0; i < 2; i++) {
		int tw = AcTextW(AcLight(), 22, names[i]);
		h->pivot[i].x = x - 6;
		h->pivot[i].y = w->y + 18;
		h->pivot[i].w = tw + 12;
		h->pivot[i].h = 44;
		x += tw + 28;
	}
}

/* ------------------------------------------------------------------------ */
/*  Painting                                                                */
/* ------------------------------------------------------------------------ */

static void AcHeaderPaint(ChWidget* wid, ChWindow* win) {
	(void)win;
	AcHeader* h = (AcHeader*)wid;
	if (wid->w <= 0 || wid->h <= 0)
		return;
	AcClipWidget(wid);
	AcFill(wid->x, wid->y, wid->w, wid->h, COL_BG);
	int base = wid->y + 52;
	if (wid->w >= 520)
		AcText(AcLight(), 34, "music", wid->x + AC_MARGIN, base, COL_TEXT, wid->w);
	static const char* const names[2] = {"library", "now playing"};
	for (int i = 0; i < 2; i++) {
		uint32_t col = (g.page == i) ? COL_TEXT : (h->hot == i ? COL_PIVOT_HOVER : COL_PIVOT_OFF);
		AcText(
			AcLight(), 22, names[i], h->pivot[i].x + 6, base, col, wid->x + wid->w - h->pivot[i].x);
	}
}

/* one library tile, including the gap pixels a press animation exposes */
static void AcPaintTile(int i, int ox) {
	ChRect r;
	AcTileRect(i, &r);
	r.x += ox;
	AcFill(r.x, r.y, r.w, r.h, COL_BG);
	int inset = (i == g.pressedTile) ? AC_PRESS_INSET : 0;
	int x = r.x + inset, y = r.y + inset, s = r.w - 2 * inset;
	uint32_t col = g_metro[g.songs[i].color];
	AcFill(x, y, s, s, col);

	if (i == g.hotTile || i == g.selTile) { /* Win8 hover / keyboard focus frame */
		uint32_t frame = (i == g.selTile && i != g.hotTile) ? COL_TEXT : AcMix(col, COL_TEXT, 100);
		AcFill(x, y, s, 3, frame);
		AcFill(x, y + s - 3, s, 3, frame);
		AcFill(x, y + 3, 3, s - 6, frame);
		AcFill(x + s - 3, y + 3, 3, s - 6, frame);
	}

	AcNotes(x + s / 2, y + (s * 2) / 5, s / 3, COL_TEXT);

	char name[AC_NAME_MAX];
	AcDisplayName(g.songs[i].name, name, sizeof(name));
	int fs = s >= 120 ? 14 : 12;
	AcTextFit(AcBase(), fs, name, x + 10, y + s - 12, COL_TEXT, s - 20);

	if (i == g.cur && !g.stopped) { /* now-playing mark */
		int bx = x + s - 24, by = y + 12;
		AcFill(bx, by + 6, 4, 10, COL_TEXT);
		AcFill(bx + 6, by, 4, 16, COL_TEXT);
		AcFill(bx + 12, by + 9, 4, 7, COL_TEXT);
	}
}

static void AcPaintScrollbar(int ox) {
	AcGrid* gr = &g.grid;
	if (!gr->scrollbar)
		return;
	bool hot = g.scrollDrag || g.hotTile == -2;
	int bh = hot ? 8 : 4;
	int by = gr->track.y + (gr->track.h - bh) / 2;
	AcFill(gr->track.x + ox, gr->track.y, gr->track.w, gr->track.h, COL_BG);
	AcFill(gr->track.x + ox, by, gr->track.w, bh, COL_TRACK);
	int tx, tw;
	AcThumb(&tx, &tw);
	AcFill(tx + ox, by, tw, bh, hot ? COL_TEXT : COL_SCROLL);
}

static void AcPaintLibrary(int ox) {
	ChWidget* p = &g.pages->base;
	AcGrid* gr = &g.grid;
	char sub[AC_PATH_MAX + 32];
	snprintf(sub, sizeof(sub), "%d %s  -  %s", g.count, g.count == 1 ? "song" : "songs", g.dir);
	AcTextFit(AcBase(), 14, sub, p->x + AC_MARGIN + ox, p->y + 20, COL_DIM, p->w - 2 * AC_MARGIN);

	if (!g.count) {
		AcText(
			AcLight(), 24, "no music here yet", p->x + AC_MARGIN + ox, p->y + 90, COL_TEXT, p->w);
		AcTextFit(AcBase(),
				  14,
				  "copy .mp3 or .wav files into this folder, or open one from Files",
				  p->x + AC_MARGIN + ox,
				  p->y + 118,
				  COL_DIM,
				  p->w - 2 * AC_MARGIN);
		return;
	}
	/* only the visible columns: O(visible tiles) */
	int step = gr->tile + AC_TILE_GAP;
	int first = (g.scrollX - AC_MARGIN) / step;
	int last = (g.scrollX + p->w) / step;
	if (first < 0)
		first = 0;
	if (last >= gr->cols)
		last = gr->cols - 1;
	for (int c = first; c <= last; c++)
		for (int rr = 0; rr < gr->rows; rr++) {
			int i = c * gr->rows + rr;
			if (i < g.count)
				AcPaintTile(i, ox);
		}
	AcPaintScrollbar(ox);
}

static const char* AcTitle(void) {
	static char name[AC_NAME_MAX];
	if (g.tagTitle[0])
		return g.tagTitle;
	AcDisplayName(g.songs[g.cur].name, name, sizeof(name));
	return name;
}

static void AcSubtitle(char* out, size_t cap) {
	if (g.tagArtist[0] && g.tagAlbum[0])
		snprintf(out, cap, "%s  -  %s", g.tagArtist, g.tagAlbum);
	else if (g.tagArtist[0] || g.tagAlbum[0])
		AcStrCopy(out, g.tagArtist[0] ? g.tagArtist : g.tagAlbum, cap);
	else {
		uint32_t hz = AcLoad(&g_sh.srcHz), kbps = AcLoad(&g_sh.kbps);
		const char* fmt = g.songs[g.cur].wav ? "WAV" : "MP3";
		if (hz && kbps)
			snprintf(out,
					 cap,
					 "%s  -  %d.%d kHz  -  %d kbps",
					 fmt,
					 (int)(hz / 1000),
					 (int)((hz % 1000) / 100),
					 (int)kbps);
		else
			AcStrCopy(out, fmt, cap);
	}
}

static void AcPaintNowProgress(int ox) {
	AcNowLayout* n = &g.now;
	if (n->prog.w <= 0 || g.cur < 0)
		return;
	uint32_t col = AcSongColor(g.cur);
	uint32_t dur = AcLoad(&g_sh.durMs);
	uint32_t pos = AcLoad(&g_sh.posMs);
	int x = n->prog.x + ox, w = n->prog.w;
	int fill = dur ? (int)(((uint64_t)pos * (uint32_t)w) / dur) : 0;
	if (g.progDrag)
		fill = AcClampI(g.progDragX - n->prog.x, 0, w);
	fill = AcClampI(fill, 0, w);

	AcFill(x - 8, n->prog.y, w + 16, n->timeY + 6 - n->prog.y, COL_BG);
	bool hot = g.progHot || g.progDrag;
	int bh = hot ? 6 : 4;
	int by = n->prog.y + (n->prog.h - bh) / 2;
	AcFill(x, by, w, bh, COL_TRACK);
	AcFill(x, by, fill, bh, col);
	if (hot)
		AcCircle((float)(x + fill), (float)by + bh * 0.5f, 7.0f, 0.0f, COL_TEXT);

	char a[16], b[16];
	if (g.progDrag && dur)
		AcFormatTime((uint32_t)(((uint64_t)fill * dur) / (uint32_t)w), a, sizeof(a));
	else
		AcFormatTime(pos, a, sizeof(a));
	AcFormatTime(dur, b, sizeof(b));
	AcText(AcBase(), 13, a, x, n->timeY, COL_DIM, w / 2);
	int bw = AcTextW(AcBase(), 13, b);
	AcText(AcBase(),
		   13,
		   dur ? b : "--:--",
		   x + w - (dur ? bw : AcTextW(AcBase(), 13, "--:--")),
		   n->timeY,
		   COL_DIM,
		   w / 2);
	g.shownFill = fill;
	g.shownSec = pos / 1000;
	g.shownDur = dur;
}

static void AcPaintNow(int ox) {
	ChWidget* p = &g.pages->base;
	AcNowLayout* n = &g.now;
	if (g.cur < 0 || g.count == 0) {
		AcText(AcLight(), 26, "nothing playing", p->x + AC_MARGIN + ox, p->y + 70, COL_TEXT, p->w);
		AcTextFit(AcBase(),
				  14,
				  "pick a song from your library to start",
				  p->x + AC_MARGIN + ox,
				  p->y + 98,
				  COL_DIM,
				  p->w - 2 * AC_MARGIN);
		return;
	}
	uint32_t col = AcSongColor(g.cur);
	if (n->art) {
		AcFill(n->artR.x + ox, n->artR.y, n->artR.w, n->artR.h, col);
		AcNotes(n->artR.x + ox + n->artR.w / 2,
				n->artR.y + n->artR.h / 2 - n->artR.h / 16,
				n->artR.w / 3,
				COL_TEXT);
		char idx[32];
		snprintf(idx, sizeof(idx), "%d / %d", g.cur + 1, g.count);
		AcText(AcBase(),
			   13,
			   idx,
			   n->artR.x + ox + 12,
			   n->artR.y + n->artR.h - 12,
			   COL_TEXT,
			   n->artR.w - 24);
	}
	const char* label = g.stopped ? "finished" : (AcIsPaused() ? "paused" : "now playing");
	AcText(AcBase(), 14, label, n->textX + ox, n->labelY, col, n->textW);
	AcTextFit(AcLight(), 30, AcTitle(), n->textX + ox, n->titleY, COL_TEXT, n->textW);
	char sub[160];
	AcSubtitle(sub, sizeof(sub));
	AcTextFit(AcBase(), 15, sub, n->textX + ox, n->subY, COL_DIM, n->textW);

	AcVisPaintFull(ox);
	AcPaintNowProgress(ox);
}

static void AcPagesPaint(ChWidget* wid, ChWindow* win) {
	(void)win;
	if (wid->w <= 0 || wid->h <= 0)
		return;
	AcClipWidget(wid);
	AcFill(wid->x, wid->y, wid->w, wid->h, COL_BG);
	if (g.page == PAGE_LIBRARY)
		AcPaintLibrary(0);
	else
		AcPaintNow(0);
}

static void AcAppBarButton(AcAppBar* a, int id) {
	int cx = a->cx[id], cy = a->cy, r = a->r[id];
	bool hot = (a->hot == id), pressed = hot && (a->pressed == id);
	bool enabled = g.count > 0;
	uint32_t ring = enabled ? COL_TEXT : COL_FAINT;
	uint32_t glyph = ring;

	AcFill(cx - r - 2, cy - r - 2, 2 * r + 5, 2 * r + 5, COL_APPBAR);
	if (pressed && enabled) {
		AcCircle((float)cx, (float)cy, (float)r, 0.0f, COL_TEXT);
		glyph = COL_APPBAR;
	} else {
		if (hot && enabled)
			AcCircle((float)cx, (float)cy, (float)r, 0.0f, 0xFF3A3A3A);
		AcCircle((float)cx, (float)cy, (float)r, 2.0f, ring);
	}

	int gsz = (id == BTN_PLAY) ? 18 : 14;
	switch (id) {
	case BTN_PLAY:
		if (AcIsPlaying()) {
			AcFill(cx - 7, cy - 8, 5, 16, glyph);
			AcFill(cx + 2, cy - 8, 5, 16, glyph);
		} else {
			AcTriangle(cx - 5, cy - gsz / 2, gsz - 3, gsz, 1, glyph);
		}
		break;
	case BTN_PREV:
		AcFill(cx - 8, cy - 7, 3, 14, glyph);
		AcTriangle(cx - 5, cy - 7, 12, 14, -1, glyph);
		break;
	case BTN_NEXT:
		AcTriangle(cx - 7, cy - 7, 12, 14, 1, glyph);
		AcFill(cx + 5, cy - 7, 3, 14, glyph);
		break;
	default:
		break;
	}

	static const char* const labels[3] = {"previous", "play", "next"};
	const char* label = (id == BTN_PLAY && AcIsPlaying()) ? "pause" : labels[id];
	int lw = AcTextW(AcBase(), 12, label);
	int ly = a->base.y + a->base.h - 8;
	AcFill(cx - 35, ly - 14, 70, 18, COL_APPBAR); /* buttons are 78px apart */
	AcText(AcBase(), 12, label, cx - lw / 2, ly, enabled ? COL_DIM : COL_FAINT, lw + 2);
}

static void AcAppBarTime(AcAppBar* a) {
	ChWidget* w = &a->base;
	if (w->w < 600)
		return;
	char t[40], p[16], d[16];
	uint32_t dur = AcLoad(&g_sh.durMs);
	AcFormatTime(AcLoad(&g_sh.posMs), p, sizeof(p));
	AcFormatTime(dur, d, sizeof(d));
	if (g.cur < 0)
		t[0] = '\0';
	else
		snprintf(t, sizeof(t), "%s / %s", p, dur ? d : "--:--");
	int x0 = a->cx[BTN_NEXT] + 40;
	AcFill(x0, w->y + 20, w->x + w->w - x0, 40, COL_APPBAR);
	g.shownBarSec = AcLoad(&g_sh.posMs) / 1000;
	int tw = AcTextW(AcBase(), 14, t);
	AcText(AcBase(), 14, t, w->x + w->w - 24 - tw, w->y + 46, COL_DIM, tw + 2);
}

static void AcAppBarInfo(AcAppBar* a) {
	ChRect* r = &a->info;
	if (r->w <= 0)
		return;
	AcFill(r->x, r->y, r->w, r->h, COL_APPBAR);
	if (g.cur < 0) {
		AcTextFit(AcBase(),
				  14,
				  g.count ? "choose a song" : "no songs found",
				  r->x,
				  r->y + 28,
				  COL_DIM,
				  r->w);
		return;
	}
	uint32_t col = AcSongColor(g.cur);
	AcFill(r->x, r->y, 46, 46, col);
	AcNotes(r->x + 23, r->y + 21, 16, COL_TEXT);
	int tx = r->x + 46 + 12, tw = r->x + r->w - tx;
	AcTextFit(AcBase(), 15, AcTitle(), tx, r->y + 19, COL_TEXT, tw);
	char sub[160];
	if (g.notice[0])
		AcTextFit(AcBase(), 13, g.notice, tx, r->y + 39, COL_WARNING, tw);
	else {
		AcSubtitle(sub, sizeof(sub));
		AcTextFit(AcBase(), 13, sub, tx, r->y + 39, COL_DIM, tw);
	}
}

static void AcAppBarPaint(ChWidget* wid, ChWindow* win) {
	(void)win;
	AcAppBar* a = (AcAppBar*)wid;
	if (wid->w <= 0 || wid->h <= 0)
		return;
	AcClipWidget(wid);
	AcFill(wid->x, wid->y, wid->w, wid->h, COL_APPBAR);
	AcAppBarInfo(a);
	for (int i = 0; i < 3; i++)
		AcAppBarButton(a, i);
	AcAppBarTime(a);
	g.shownBtnState = AcIsPlaying() ? 1 : 0;
}

/* ------------------------------------------------------------------------ */
/*  Repaint helpers                                                         */
/* ------------------------------------------------------------------------ */

static bool AcCanPaint(void) {
	return g.ready && !g.resizing && !g.closing && g.win->canv && g.win->canv->buffer;
}

static void AcRepaintWidget(ChWidget* w) {
	if (!AcCanPaint() || !w || w->w <= 0 || w->h <= 0)
		return;
	w->ChPaintHandler(w, g.win);
	AcUpdate(w->x, w->y, w->w, w->h);
}

static void AcRepaintPages(void) {
	if (g.anim.type == ANIM_NONE)
		AcRepaintWidget(&g.pages->base);
}

static void AcRepaintAppBar(void) {
	AcRepaintWidget(&g.appbar->base);
}

static void AcRepaintHeader(void) {
	AcRepaintWidget(&g.header->base);
}

static void AcRepaintTile(int i) {
	if (!AcCanPaint() || i < 0 || i >= g.count || g.page != PAGE_LIBRARY ||
		g.anim.type != ANIM_NONE)
		return;
	ChWidget* p = &g.pages->base;
	ChRect r;
	AcTileRect(i, &r);
	if (r.x + r.w <= p->x || r.x >= p->x + p->w)
		return;
	AcClipWidget(p);
	AcPaintTile(i, 0);
	int x0 = r.x > p->x ? r.x : p->x;
	int x1 = (r.x + r.w < p->x + p->w) ? r.x + r.w : p->x + p->w;
	AcUpdate(x0, r.y, x1 - x0, r.h);
}

static void AcRepaintScrollbar(void) {
	if (!AcCanPaint() || g.page != PAGE_LIBRARY || !g.grid.scrollbar)
		return;
	AcClipWidget(&g.pages->base);
	AcPaintScrollbar(0);
	ChRect* t = &g.grid.track;
	AcUpdate(t->x, t->y, t->w, t->h);
}

static void AcRepaintProgress(void) {
	if (!AcCanPaint() || g.page != PAGE_NOW || g.anim.type != ANIM_NONE || g.now.prog.w <= 0)
		return;
	AcClipWidget(&g.pages->base);
	AcPaintNowProgress(0);
	AcNowLayout* n = &g.now;
	AcUpdate(n->prog.x - 8, n->prog.y, n->prog.w + 16, n->timeY + 6 - n->prog.y);
}

static void AcRepaintAppBarParts(void) {
	if (!AcCanPaint())
		return;
	AcAppBar* a = g.appbar;
	AcClipWidget(&a->base);
	AcAppBarTime(a);
	ChWidget* w = &a->base;
	int x0 = a->cx[BTN_NEXT] + 40;
	if (x0 < w->x + w->w)
		AcUpdate(x0, w->y + 20, w->x + w->w - x0, 40);
}

/* ------------------------------------------------------------------------ */
/*  Animations                                                              */
/* ------------------------------------------------------------------------ */

static void AcAnimEnter(int page) {
	g.anim.type = ANIM_ENTER;
	g.anim.page = page;
	g.anim.start = AcNow();
}

/*
 * Launch a tile, Windows 8 start screen style: the tile zooms up to fill the
 * page, holds as a splash (tile colour + glyph), then the now playing page
 * slides in from the right.
 */
static void AcLaunchTile(int i) {
	AcPlaySong(i);
	ChWidget* p = &g.pages->base;
	if (!AcCanPaint() || p->w < AC_TILE_MIN || p->h < AC_TILE_MIN) {
		AcShowPage(PAGE_NOW, false); /* no room for the zoom */
		return;
	}
	ChRect r;
	AcTileRect(i, &r);
	g.anim.type = ANIM_ZOOM;
	g.anim.start = AcNow();
	g.anim.from = r;
	g.anim.last = r;
	g.anim.color = AcSongColor(i);
	g.nextTick = 0;
}

/* paint `cur` minus `last` (the zoom rectangle only ever grows) */
static void AcFillGrow(const ChRect* cur, const ChRect* last, uint32_t col) {
	int lx1 = last->x + last->w, ly1 = last->y + last->h;
	int cx1 = cur->x + cur->w, cy1 = cur->y + cur->h;
	AcFill(cur->x, cur->y, cur->w, last->y - cur->y, col);	 /* top */
	AcFill(cur->x, ly1, cur->w, cy1 - ly1, col);			 /* bottom */
	AcFill(cur->x, last->y, last->x - cur->x, last->h, col); /* left */
	AcFill(lx1, last->y, cx1 - lx1, last->h, col);			 /* right */
	(void)ly1;
}

static void AcAnimStep(uint64_t now) {
	ChWidget* p = &g.pages->base;
	if (!AcCanPaint()) {
		g.anim.type = ANIM_NONE;
		return;
	}
	AcClipWidget(p);
	float t;
	switch (g.anim.type) {
	case ANIM_ZOOM: {
		t = (float)(now - g.anim.start) / AC_ANIM_ZOOM_MS;
		float e = AcEaseOut(t);
		ChRect* f = &g.anim.from;
		ChRect cur;
		cur.x = f->x + (int)((float)(p->x - f->x) * e);
		cur.y = f->y + (int)((float)(p->y - f->y) * e);
		cur.w = f->w + (int)((float)(p->w - f->w) * e);
		cur.h = f->h + (int)((float)(p->h - f->h) * e);
		if (t >= 1.0f) {
			cur.x = p->x;
			cur.y = p->y;
			cur.w = p->w;
			cur.h = p->h;
		}
		if (g.anim.last.w == g.anim.from.w && g.anim.last.x == g.anim.from.x)
			AcFill(cur.x, cur.y, cur.w, cur.h, g.anim.color); /* first frame */
		else
			AcFillGrow(&cur, &g.anim.last, g.anim.color);
		g.anim.last = cur;
		int ux = cur.x > p->x ? cur.x : p->x;
		int uw = (cur.x + cur.w < p->x + p->w ? cur.x + cur.w : p->x + p->w) - ux;
		if (uw > 0)
			AcUpdate(ux, cur.y, uw, cur.h);
		if (t >= 1.0f) {
			int s = (p->w < p->h ? p->w : p->h) / 3;
			AcNotes(p->x + p->w / 2, p->y + p->h / 2, s, COL_TEXT);
			AcUpdate(p->x, p->y, p->w, p->h);
			g.anim.type = ANIM_SPLASH;
			g.anim.start = now;
		}
		break;
	}
	case ANIM_SPLASH:
		if (now - g.anim.start >= AC_ANIM_SPLASH_MS) {
			g.page = PAGE_NOW;
			AcRepaintHeader();
			AcAnimEnter(PAGE_NOW);
		}
		break;
	case ANIM_ENTER: {
		t = (float)(now - g.anim.start) / AC_ANIM_ENTER_MS;
		int dx = (t >= 1.0f) ? 0 : (int)((1.0f - AcEaseOut(t)) * AC_ANIM_ENTER_DX);
		AcFill(p->x, p->y, p->w, p->h, COL_BG);
		if (g.anim.page == PAGE_LIBRARY)
			AcPaintLibrary(dx);
		else
			AcPaintNow(dx);
		AcUpdate(p->x, p->y, p->w, p->h);
		if (t >= 1.0f)
			g.anim.type = ANIM_NONE;
		break;
	}
	default:
		g.anim.type = ANIM_NONE;
		break;
	}
}

static void AcShowPage(int page, bool animate) {
	if (page == g.page && g.anim.type == ANIM_NONE)
		return;
	g.page = page;
	g.hotTile = -1;
	g.pressedTile = -1;
	g.progHot = g.progDrag = false;
	AcRepaintHeader();
	if (animate && AcCanPaint()) {
		AcAnimEnter(page);
		g.nextTick = 0;
	} else {
		g.anim.type = ANIM_NONE;
		AcRepaintPages();
	}
}

/* ------------------------------------------------------------------------ */
/*  Mouse                                                                   */
/* ------------------------------------------------------------------------ */

static bool AcIsScroll(int button) {
	return button == DEODHAI_MOUSE_MSG_SCROLL_UP || button == DEODHAI_MOUSE_MSG_SCROLL_DOWN;
}

static bool AcHit(const ChRect* r, int x, int y) {
	return r->w > 0 && x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

static void AcHeaderMouse(ChWidget* wid, ChWindow* win, int mx, int my, int button) {
	AcHeader* h = (AcHeader*)wid;
	if (g.resizing || AcIsScroll(button))
		return;
	int x = mx - win->info->x, y = my - win->info->y;
	int hit = -1;
	for (int i = 0; wid->hover && i < 2; i++)
		if (AcHit(&h->pivot[i], x, y))
			hit = i;
	if (hit != h->hot) {
		h->hot = hit;
		AcRepaintHeader();
	}
	if (button && hit >= 0 && hit != g.page && g.anim.type == ANIM_NONE)
		AcShowPage(hit, true);
}

static void AcSetScroll(int sx) {
	sx = AcClampI(sx, 0, g.grid.maxScroll);
	if (sx == g.scrollX)
		return;
	g.scrollX = sx;
	AcRepaintPages();
}

static void AcEnsureTileVisible(int i) {
	ChRect r;
	AcTileRect(i, &r);
	ChWidget* p = &g.pages->base;
	if (r.x < p->x + AC_MARGIN)
		AcSetScroll(g.scrollX - (p->x + AC_MARGIN - r.x));
	else if (r.x + r.w > p->x + p->w - AC_MARGIN)
		AcSetScroll(g.scrollX + (r.x + r.w - (p->x + p->w - AC_MARGIN)));
}

static void AcLibraryMouse(int x, int y, int button) {
	ChWidget* p = &g.pages->base;
	AcGrid* gr = &g.grid;
	bool inside = p->hover;
	bool down = button != 0 && !AcIsScroll(button);
	bool press = down && !g.libDown;
	bool release = !down && g.libDown;
	g.libDown = down;

	if (AcIsScroll(button)) {
		int step = gr->tile + AC_TILE_GAP;
		AcSetScroll(g.scrollX + (button == DEODHAI_MOUSE_MSG_SCROLL_DOWN ? step : -step));
		return;
	}
	if (g.scrollDrag) {
		if (!down) {
			g.scrollDrag = false;
			AcRepaintScrollbar();
			return;
		}
		int tx, tw;
		AcThumb(&tx, &tw);
		int range = gr->track.w - tw;
		if (range > 0)
			AcSetScroll(g.dragScroll0 + ((x - g.dragX0) * gr->maxScroll) / range);
		return;
	}

	bool onBar = inside && gr->scrollbar && AcHit(&gr->track, x, y);
	int hit = (inside && !onBar && y < gr->track.y) ? AcTileAt(x, y) : -1;
	int hot = onBar ? -2 : hit;
	if (hot != g.hotTile) {
		int old = g.hotTile;
		g.hotTile = hot;
		if (old >= 0)
			AcRepaintTile(old);
		if (hot >= 0)
			AcRepaintTile(hot);
		if (old == -2 || hot == -2)
			AcRepaintScrollbar();
	}

	if (press && onBar) {
		int tx, tw;
		AcThumb(&tx, &tw);
		if (x >= tx && x < tx + tw) {
			g.scrollDrag = true;
			g.dragX0 = x;
			g.dragScroll0 = g.scrollX;
		} else {
			AcSetScroll(g.scrollX + (x < tx ? -p->w : p->w) * 3 / 4);
		}
		AcRepaintScrollbar();
		return;
	}
	if (press && hit >= 0) {
		g.pressedTile = hit;
		AcRepaintTile(hit);
		return;
	}
	if (release && g.pressedTile >= 0) {
		int t = g.pressedTile;
		g.pressedTile = -1;
		AcRepaintTile(t);
		if (t == hit)
			AcLaunchTile(t);
	}
}

static void AcNowMouse(int x, int y, int button) {
	AcNowLayout* n = &g.now;
	bool down = button != 0 && !AcIsScroll(button);
	ChRect hit = n->prog;
	hit.y -= 6;
	hit.h += 12;
	bool over = g.pages->base.hover && AcHit(&hit, x, y) && AcLoad(&g_sh.durMs) > 0;

	if (g.progDrag) {
		g.progDragX = x;
		if (!down) {
			g.progDrag = false;
			int w = n->prog.w > 0 ? n->prog.w : 1;
			uint64_t ms =
				((uint64_t)AcClampI(x - n->prog.x, 0, w) * AcLoad(&g_sh.durMs)) / (uint32_t)w;
			AcSeekTo((uint32_t)ms);
		}
		g.progHot = over;
		AcRepaintProgress();
		return;
	}
	if (down && over && !g.libDown) {
		g.progDrag = true;
		g.progDragX = x;
	}
	g.libDown = down;
	if (over != g.progHot || g.progDrag) {
		g.progHot = over;
		AcRepaintProgress();
	}
}

static void AcPagesMouse(ChWidget* wid, ChWindow* win, int mx, int my, int button) {
	(void)wid;
	if (g.resizing || g.anim.type != ANIM_NONE)
		return;
	int x = mx - win->info->x, y = my - win->info->y;
	if (g.page == PAGE_LIBRARY)
		AcLibraryMouse(x, y, button);
	else
		AcNowMouse(x, y, button);
}

static int AcAppBarHit(AcAppBar* a, int x, int y) {
	for (int i = 0; i < 3; i++) {
		int dx = x - a->cx[i], dy = y - a->cy, r = a->r[i] + 4;
		if (dx * dx + dy * dy <= r * r)
			return i;
	}
	if (AcHit(&a->info, x, y) && g.cur >= 0)
		return BTN_INFO;
	return BTN_NONE;
}

static void AcAppBarRepaintButton(int id) {
	if (!AcCanPaint() || id < 0 || id > BTN_NEXT)
		return;
	AcAppBar* a = g.appbar;
	AcClipWidget(&a->base);
	AcAppBarButton(a, id);
	int r = a->r[id] + 3;
	AcUpdate(a->cx[id] - 42, a->cy - r, 84, a->base.y + a->base.h - (a->cy - r));
}

static void AcAppBarMouse(ChWidget* wid, ChWindow* win, int mx, int my, int button) {
	AcAppBar* a = (AcAppBar*)wid;
	if (g.resizing || AcIsScroll(button))
		return;
	int x = mx - win->info->x, y = my - win->info->y;
	bool down = button != 0;
	bool press = down && !a->prevDown;
	bool release = !down && a->prevDown;
	a->prevDown = down;

	int hit = wid->hover ? AcAppBarHit(a, x, y) : BTN_NONE;
	if (hit != a->hot) {
		int old = a->hot;
		a->hot = hit;
		AcAppBarRepaintButton(old);
		AcAppBarRepaintButton(hit);
	}
	if (press) {
		a->pressed = hit;
		AcAppBarRepaintButton(hit);
		return;
	}
	if (release) {
		int p = a->pressed;
		a->pressed = BTN_NONE;
		AcAppBarRepaintButton(p);
		if (p != hit || p == BTN_NONE)
			return;
		switch (p) {
		case BTN_PREV:
			AcPrev();
			break;
		case BTN_PLAY:
			AcTogglePlay();
			break;
		case BTN_NEXT:
			AcNext();
			break;
		case BTN_INFO:
			AcShowPage(PAGE_NOW, true);
			break;
		default:
			break;
		}
	}
}

static void AcMouseLeave(void) {
	if (g.progDrag) {
		g.progDrag = false;
		AcRepaintProgress();
	}
	if (g.header->hot != -1) {
		g.header->hot = -1;
		AcRepaintHeader();
	}
	int old = g.hotTile;
	g.hotTile = -1;
	g.libDown = false;
	g.scrollDrag = false;
	if (g.pressedTile >= 0) {
		int t = g.pressedTile;
		g.pressedTile = -1;
		AcRepaintTile(t);
	}
	if (old >= 0)
		AcRepaintTile(old);
	if (old == -2)
		AcRepaintScrollbar();
	if (g.progHot) {
		g.progHot = false;
		AcRepaintProgress();
	}
	AcAppBar* a = g.appbar;
	int hot = a->hot;
	a->hot = a->pressed = BTN_NONE;
	a->prevDown = false;
	AcAppBarRepaintButton(hot);
	g.header->base.hover = g.pages->base.hover = a->base.hover = false;
}

/* ------------------------------------------------------------------------ */
/*  Keyboard                                                                */
/* ------------------------------------------------------------------------ */

static void AcMoveSelection(int delta) {
	if (!g.count)
		return;
	int old = g.selTile;
	int s = AcClampI((old < 0 ? 0 : old + delta), 0, g.count - 1);
	g.selTile = s;
	AcEnsureTileVisible(s);
	AcRepaintTile(old);
	AcRepaintTile(s);
}

static void AcHandleKey(int code) {
	ChitralekhaProcessKey(code);
	if (g.resizing || (code & SC_RELEASE))
		return;
	int sc = code & SC_MASK;
	if (sc == SC_CTRL || sc == SC_LSHIFT || sc == SC_RSHIFT || sc == SC_ALT || sc == SC_CAPSLOCK)
		return;
	if (g.anim.type != ANIM_NONE)
		return;
	bool ctrl = ChitralekhaKeyGetCTRL() || ChitralekhaKeyGetAlt();
	char c = ChitralekhaKeyToASCII(code);

	if (ctrl) {
		if (sc == SC_RIGHT)
			AcNext();
		else if (sc == SC_LEFT)
			AcPrev();
		else if (c == 'q' || c == 'Q' || c == 'w' || c == 'W')
			ChWindowCloseWindow(g.win);
		return;
	}
	switch (sc) {
	case SC_SPACE:
		AcTogglePlay();
		return;
	case SC_TAB:
		AcShowPage(g.page == PAGE_LIBRARY ? PAGE_NOW : PAGE_LIBRARY, true);
		return;
	case SC_F5:
		if (g.page == PAGE_LIBRARY) {
			char dir[AC_PATH_MAX];
			AcStrCopy(dir, g.dir, sizeof(dir));
			char cur[AC_NAME_MAX] = {0};
			if (g.cur >= 0)
				AcStrCopy(cur, g.songs[g.cur].name, sizeof(cur));
			AcLoadLibrary(dir);
			g.cur = cur[0] ? AcFindSong(cur) : -1;
			g.selTile = g.cur;
			AcRelayout();
			AcRepaintPages();
		}
		return;
	default:
		break;
	}

	if (g.page == PAGE_NOW) {
		switch (sc) {
		case SC_RIGHT:
			AcSeekBy(5000);
			break;
		case SC_LEFT:
			AcSeekBy(-5000);
			break;
		case SC_ESC:
		case SC_BACKSPACE:
			AcShowPage(PAGE_LIBRARY, true);
			break;
		default:
			if (c == 'n' || c == 'N')
				AcNext();
			else if (c == 'p' || c == 'P')
				AcPrev();
			break;
		}
		return;
	}

	int rows = g.grid.rows > 0 ? g.grid.rows : 1;
	switch (sc) {
	case SC_UP:
		AcMoveSelection(-1);
		break;
	case SC_DOWN:
		AcMoveSelection(1);
		break;
	case SC_LEFT:
		AcMoveSelection(-rows);
		break;
	case SC_RIGHT:
		AcMoveSelection(rows);
		break;
	case SC_HOME:
		AcMoveSelection(-g.count);
		break;
	case SC_END:
		AcMoveSelection(g.count);
		break;
	case SC_ENTER:
		if (g.selTile >= 0) {
			AcEnsureTileVisible(g.selTile);
			AcLaunchTile(g.selTile);
		}
		break;
	case SC_ESC:
		if (g.selTile >= 0) {
			int s = g.selTile;
			g.selTile = -1;
			AcRepaintTile(s);
		}
		break;
	default:
		if (c == 'n' || c == 'N')
			AcNext();
		else if (c == 'p' || c == 'P')
			AcPrev();
		break;
	}
}

/* ------------------------------------------------------------------------ */
/*  Layout / resize                                                         */
/* ------------------------------------------------------------------------ */

static void AcRelayout(void) {
	ChWindow* win = g.win;
	int W = win->info->width, H = win->info->height;
	for (unsigned i = 0; i < win->GlobalControls->pointer; i++) {
		ChWinGlobalControl* ctl = (ChWinGlobalControl*)list_get_at(win->GlobalControls, i);
		if (ctl)
			ctl->x = W - 25 - 20 * (int)i;
	}
	int cw = W - 2, ch = H - AC_TITLEBAR_H - 1;
	ChBorderLayoutResize(g.root, 1, AC_TITLEBAR_H, cw > 0 ? cw : 0, ch > 0 ? ch : 0);
	AcLayoutHeader();
	AcLayoutGrid();
	AcLayoutNow();
	AcLayoutAppBar();
}

static void AcHandleReinitBuffer(int key) {
	ChWindow* win = g.win;
	int id = _KeCreateSharedMem((uint16_t)key, 0, 0);
	void* buffer = _KeObtainSharedMem(id, 0, 0);
	win->app->backbufkey = key;
	win->app->fb = buffer;
	win->buffer = (uint32_t*)buffer;
	win->info->windowReady = 1;

	_KeProcessSleep(50);
	ChDeAllocateBuffer(win->canv);
	win->canv->canvasWidth = win->info->width;
	win->canv->canvasHeight = win->info->height;
	if (!ChAllocateBuffer(win->canv)) {
		win->canv->buffer = NULL;
		win->canv->bufferSz = 0;
		return;
	}
	g.resizing = false;
	g.anim.type = ANIM_NONE; /* a resize ends any running animation */
	AcRelayout();
	AcClipAll();
	ChWindowPaint(win);
}

/* ------------------------------------------------------------------------ */
/*  Tick                                                                    */
/* ------------------------------------------------------------------------ */

/* returns ms until the next tick is needed, or -1 to sleep until an event */
static int AcTick(void) {
	uint64_t now = AcNow();
	AcServiceEngine();

	if (g.anim.type != ANIM_NONE) {
		AcAnimStep(now);
		if (g.anim.type != ANIM_NONE)
			return 15;
	}

	bool playing = g.cur >= 0 && !g.stopped;
	if (playing && AcCanPaint()) {
		uint32_t pos = AcLoad(&g_sh.posMs), dur = AcLoad(&g_sh.durMs);
		int state = AcIsPlaying() ? 1 : 0;
		if (state != g.shownBtnState)
			AcRepaintAppBar();
		if (pos / 1000 != g.shownBarSec)
			AcRepaintAppBarParts();
		if (g.page == PAGE_NOW && g.now.prog.w > 0 && dur) {
			int fill = (int)(((uint64_t)pos * (uint32_t)g.now.prog.w) / dur);
			if (fill != g.shownFill || pos / 1000 != g.shownSec || dur != g.shownDur)
				AcRepaintProgress();
		}
	}

	bool moving = false;
	if (g.page == PAGE_NOW && g.cur >= 0 && g.anim.type == ANIM_NONE)
		moving = AcVisFrame(now);

	if (g.pendingPlay >= 0)
		return 10;
	bool live = AcIsPlaying();
	if (g.page == PAGE_NOW && (moving || live))
		return AC_VIS_FRAME_MS;
	if (live)
		return AC_IDLE_TICK_MS; /* clock + end-of-track detection */
	return -1;					/* paused / stopped: sleep until input */
}

/* ------------------------------------------------------------------------ */
/*  Messages                                                                */
/* ------------------------------------------------------------------------ */

static void AcHandleMessage(PostEvent* e) {
	switch (e->type) {
	case DEODHAI_REPLY_MOUSE_EVENT:
		if (!g.resizing && e->dword5 == WINDOW_HANDLE_TYPE_NORMAL) {
			ChWindow* w = ChGetWindowByHandle(g.win, e->dword4);
			if (w)
				ChWindowHandleMouse(w, e->dword, e->dword2, e->dword3);
		}
		break;
	case DEODHAI_REPLY_KEY_EVENT:
		AcHandleKey(e->dword);
		break;
	case DEODHAI_REPLY_FOCUS_CHANGED: {
		ChWindow* w = ChGetWindowByHandle(g.win, e->dword2);
		if (!w)
			break;
		if (g.resizing && w == g.win)
			w->focused = e->dword ? true : false;
		else
			ChWindowHandleFocus(w, e->dword ? true : false, e->dword2);
		break;
	}
	case DEODHAI_REPLY_MOUSE_LEAVE:
		if (!g.resizing)
			AcMouseLeave();
		break;
	case DEODHAI_REPLY_DESTROY_BUFFER:
		g.resizing = true; /* unmapped until REINIT: nothing may paint */
		ChWindowHandleDestroyBuffer(g.win);
		break;
	case DEODHAI_REPLY_REINIT_BUFFER:
		AcHandleReinitBuffer(e->dword);
		break;
	default:
		break;
	}
	memset(e, 0, sizeof(PostEvent));
}

/* ------------------------------------------------------------------------ */
/*  Widgets, layout tree, cleanup                                           */
/* ------------------------------------------------------------------------ */

static void AcFreeWidget(ChWidget* w, ChWindow* win) {
	(void)win;
	free(w);
}

static void AcLayoutFree(ChBorderLayout* l) {
	if (!l)
		return;
	while (l->items->pointer) {
		ChBorderItem* it = (ChBorderItem*)list_remove(l->items, 0);
		if (!it)
			break;
		if (it->widget && it->widget->type == CH_WIDEGT_BORDERLAYOUT)
			AcLayoutFree((ChBorderLayout*)it->widget);
		free(it);
	}
	free(l->items);
	free(l);
}

static void AcOnClose(ChWindow* win) {
	(void)win;
	if (g.closing)
		return;
	g.closing = true;
	/* stop the engine first: it unregisters its DSP (keyed by its thread id)
	 * so the kernel does not keep a stale player around */
	AcStore(&g_sh.quit, 1);
	for (int i = 0; i < 60 && !AcLoad(&g_sh.done); i++)
		_KeProcessSleep(10);

	free(g.songs);
	g.songs = NULL;
	g.count = g.cap = 0;
	AcLayoutFree(g.root);
	g.root = NULL;
	if (g.light) {
		ChFontClose(g.light);
		g.light = NULL;
	}
}

/* first argument that names an existing .mp3 / .wav (any argv slot) */
static const char* AcFindArgument(int argc, char* argv[]) {
	for (int i = 0; argv && i < argc; i++) {
		if (!argv[i] || !AcIsAudioName(argv[i]))
			continue;
		int fd = _KeOpenFile(argv[i], FILE_OPEN_READ_ONLY);
		if (fd >= 0) {
			_KeCloseFile(fd);
			return argv[i];
		}
	}
	return NULL;
}

int main(int argc, char* argv[]) {
	memset(&g, 0, sizeof(g));
	memset(&g_sh, 0, sizeof(g_sh));
	g_sh.seekPermille = -1;
	g.cur = -1;
	g.selTile = -1;
	g.hotTile = -1;
	g.pressedTile = -1;
	g.pendingPlay = -1;
	g.shownSec = 0xFFFFFFFF;
	g.shownBtnState = -1;

	g.app = ChitralekhaStartApp(argc, argv);
	g.win = ChCreateWindow(
		g.app, WINDOW_FLAG_MOVABLE, (char*)"Accent", AC_WIN_X, AC_WIN_Y, AC_WIN_W, AC_WIN_H);
	g.win->color = COL_BG;
	g.win->ChCloseWin = AcOnClose;
	g.light = ChInitialiseFont((char*)ROBOTO_LIGHT);
	AcClipAll();

	g.header = (AcHeader*)calloc(1, sizeof(AcHeader));
	g.pages = (AcPages*)calloc(1, sizeof(AcPages));
	g.appbar = (AcAppBar*)calloc(1, sizeof(AcAppBar));
	if (!g.header || !g.pages || !g.appbar) {
		_KePrint("Accent: out of memory \r\n");
		ChWindowCloseWindow(g.win);
	}
	g.header->base.h = AC_HEADER_H;
	g.header->base.ChPaintHandler = AcHeaderPaint;
	g.header->base.ChMouseEvent = AcHeaderMouse;
	g.header->base.ChDestroy = AcFreeWidget;
	g.header->hot = -1;
	g.pages->base.ChPaintHandler = AcPagesPaint;
	g.pages->base.ChMouseEvent = AcPagesMouse;
	g.pages->base.ChDestroy = AcFreeWidget;
	g.appbar->base.h = AC_APPBAR_H;
	g.appbar->base.ChPaintHandler = AcAppBarPaint;
	g.appbar->base.ChMouseEvent = AcAppBarMouse;
	g.appbar->base.ChDestroy = AcFreeWidget;
	g.appbar->hot = g.appbar->pressed = BTN_NONE;

	ChWindowAddWidget(g.win, (ChWidget*)g.header);
	ChWindowAddWidget(g.win, (ChWidget*)g.pages);
	ChWindowAddWidget(g.win, (ChWidget*)g.appbar);

	g.root =
		ChCreateBorderLayout(1, AC_TITLEBAR_H, AC_WIN_W - 2, AC_WIN_H - AC_TITLEBAR_H - 1, 0, 0);
	ChBorderLayoutAdd(g.root, (ChWidget*)g.header, CH_BORDER_ALIGN_TOP);
	ChBorderLayoutAdd(g.root, (ChWidget*)g.appbar, CH_BORDER_ALIGN_BOTTOM);
	ChBorderLayoutAdd(g.root, (ChWidget*)g.pages, CH_BORDER_ALIGN_CENTER);
	ChBorderLayoutSetPreferred(g.root, (ChWidget*)g.header, 0, AC_HEADER_H);
	ChBorderLayoutSetPreferred(g.root, (ChWidget*)g.appbar, 0, AC_APPBAR_H);

	/* library: the argument's folder, else /music */
	const char* arg = AcFindArgument(argc, argv);
	char dir[AC_PATH_MAX];
	AcStrCopy(dir, "/music", sizeof(dir));
	const char* argName = NULL;
	if (arg) {
		AcStrCopy(dir, arg, sizeof(dir));
		char* slash = strrchr(dir, '/');
		if (slash == dir)
			dir[1] = '\0';
		else if (slash)
			*slash = '\0';
		argName = strrchr(arg, '/') ? strrchr(arg, '/') + 1 : arg;
	}
	AcLoadLibrary(dir);

	AcVisInit();
	AcRelayout();

	/* audio engine thread: registers itself with the sound layer */
	_KeCreateThread(AcAudioThread, (char*)"accent-audio");

	if (argName) {
		int i = AcFindSong(argName);
		if (i >= 0) {
			AcPlaySong(i);
			g.page = PAGE_NOW;
		}
	}

	g.ready = true;
	AcClipAll();
	ChWindowPaint(g.win);
	ChWindowBroadcastIcon(g.app, (char*)"/icons/media.bmp");

	PostEvent e;
	memset(&e, 0, sizeof(PostEvent));
	setjmp(g.win->jump);
	for (;;) {
		int err = _KeFileIoControl(g.app->postboxfd, POSTBOX_GET_EVENT, &e);
		if (err != POSTBOX_NO_EVENT) {
			AcHandleMessage(&e);
			if (AcNow() < g.nextTick)
				continue; /* drain the queue; tick below when due */
		}
		int wait = AcTick();
		g.nextTick = AcNow() + (uint64_t)(wait < 0 ? 0 : wait);
		if (err != POSTBOX_NO_EVENT)
			continue;
		if (wait < 0)
			_KePauseThread();
		else if (wait > 0)
			_KeProcessSleep((uint64_t)wait);
	}
}