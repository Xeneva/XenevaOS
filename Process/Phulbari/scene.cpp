/**
* BSD 2-Clause License
*
* Copyright (c) 2026, Manas Kamal Choudhury
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

#include "phulbari.h"
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* palette                                                             */
/* ------------------------------------------------------------------ */

enum {
	P_PAPER,
	P_SHADE,
	P_INK,
	P_INK_SOFT,
	P_RED,
	P_RED_DEEP,
	P_MUGA,
	P_MUGA_DEEP,
	P_PINK,
	P_CHARCOAL,
	P_LEAF,
	P_WHITE,
	P_SKIN,
	P_COUNT
};

static const uint32_t _pal[P_COUNT] = {
	PB_C_IVORY,	   PB_C_IVORY_SHADE, PB_C_INK,		  PB_C_INK_SOFT,
	PB_C_RED,	   PB_C_RED_DEEP,	 PB_C_MUGA,		  PB_C_MUGA_DEEP,
	PB_C_WEAVE_LINE, PB_C_CHARCOAL,	 PB_C_LEAF,		  PB_C_WHITE,
	PB_C_SKIN,
};

/* the cell swatches: one per colour and thread direction */
#define PB_MAX_CELL 6
static uint32_t _swatch[P_COUNT][2][PB_MAX_CELL * PB_MAX_CELL];

/* the gamusa phul Seek uses (Process/XELnch/gamusa.cpp), woven into the
 * flying gamusa; bit 8 is the leftmost cell */
static const uint16_t _phul[9] = { 0x010, 0x038, 0x092, 0x0D6, 0x1FF, 0x0D6, 0x092, 0x038, 0x010 };

/* the Xeneva bird, traced from XeLogo.png at 48x34; bit 47 is the
 * leftmost cell, the bird faces right */
#define BIRD_W 48
#define BIRD_H 34
static const uint64_t _bird[BIRD_H] = {
	0x800000000000ull, 0xC00000000000ull, 0x600000000000ull,
	0x780000000000ull, 0x3E0000000000ull, 0x1FC000000000ull,
	0x0FF000000000ull, 0x07FE00000000ull, 0x03FFC0000000ull,
	0x00FFF8000000ull, 0x183FFE000000ull, 0x0C0FFF800000ull,
	0x0781FFE00000ull, 0x03FFFFF80000ull, 0x01FFFFFC0000ull,
	0x007FFFFE00F8ull, 0x001FFFFF07FFull, 0x0001FFFF9F9Full,
	0x004007FFCFFCull, 0x007FFFFFE7F8ull, 0x001FFFFFF7F0ull,
	0x0007FFFFF3E0ull, 0x0000FFFFF3C0ull, 0x0000003FFBC0ull,
	0x000063FFFB80ull, 0x00003FFFFB00ull, 0x00000001FA00ull,
	0x00000000F800ull, 0x00000000F800ull, 0x000000003800ull,
	0x000000003000ull, 0x000000003000ull, 0x000000006000ull,
	0x000000004000ull,
};
/* where the wing meets the body; columns left of BIRD_WING_X are wing */
#define BIRD_WING_X 33
#define BIRD_PIVOT_X 31.0f
#define BIRD_PIVOT_Y 18.0f

/* the gamusa lozenge, woven into the butterfly's wings */
static const uint16_t _lozenge[7] = { 0x08, 0x14, 0x22, 0x49, 0x22, 0x14, 0x08 };

/* ------------------------------------------------------------------ */
/* state                                                               */
/* ------------------------------------------------------------------ */

#define MASK_DIM 128
#define NUM_FLOWERS 200
#define NUM_TUFTS 50
/* half width of the path the camera walks along (world units) */
#define PATH_HALF 0.5f
/* eye height above the ground (world units): low, among the stems */
#define CAM_H 0.42f
/* how deep the garden is planted ahead of the camera */
#define GARDEN_DEPTH 13.0f
#define NUM_ITEMS (NUM_FLOWERS + NUM_TUFTS)

/* timeline, in cuts (one every PB_CUT_MS, about 6.7 a second) */
#define CUT_SCENE2 72	   /* frame 2: hard cut to a closer shot */
#define CUT_LAND 78		   /* the butterfly is suddenly there */
#define CUT_TAKEOFF 85	   /* ...for one second, then it leaves */
#define CUT_FOLLOW 97	   /* frame 3: the camera follows it */
#define FOLLOW_CUTS 14	   /* two seconds of following */
#define EXIT_CUTS 6		   /* then it darts off screen */
#define CUT_FOLLOW_END (CUT_FOLLOW + FOLLOW_CUTS + EXIT_CUTS)
#define CUT_WELCOME 119	   /* "Welcome" rises out of the flowers */
#define CUT_BREAK 152	   /* frame 4: it breaks into butterflies */
#define BREAK_FLY_CUTS 24  /* how long the swarm is followed */
#define CUT_AURORA 184	   /* frame 5: the kernel's name rises */
#define CUT_GAMUSA 251	   /* frame 6: after a 5 s hold, a gamusa sweeps */
#define SWEEP_CUTS 26
#define CUT_BAMBOO (CUT_GAMUSA + SWEEP_CUTS) /* frame 7: bamboo drawn */
#define BAMBOO_DRAW_CUTS 30				   /* the drawing, stalk by stalk */
#define CUT_BIRD (CUT_BAMBOO + BAMBOO_DRAW_CUTS) /* the Xeneva bird */
#define BIRD_CUTS 24
#define FRAME7_HOLD 47 /* 7 s on "DeodhaiXR" */
#define CUT_BOY (CUT_BIRD + BIRD_CUTS + 4 + FRAME7_HOLD) /* frame 8 */
#define UNRAVEL_CUTS 13	   /* the words come unstitched */
#define CUT_RUNIN (CUT_BOY + 4) /* a boy runs in from the left */
#define RUNIN_CUTS 18
#define CUT_GLASSES (CUT_RUNIN + RUNIN_CUTS) /* he puts his glasses on */
#define GLASSES_CUTS 6
#define CUT_XR (CUT_GLASSES + GLASSES_CUTS) /* apps in the air, 4 s */
#define XR_CUTS 27
#define CUT_RUNOUT (CUT_XR + XR_CUTS) /* and he runs off to the right */
#define RUNOUT_CUTS 14
#define CUT_ERASE (CUT_RUNOUT + RUNOUT_CUTS + 2) /* frame 9: bamboo goes */
#define ERASE_CUTS 22
#define CUT_CHITRA (CUT_ERASE + ERASE_CUTS) /* Chitralekha at her easel */
#define REVEAL_CUTS 6
#define CUT_PORTRAIT (CUT_CHITRA + REVEAL_CUTS + 2) /* she draws Aniruddha */
#define PORTRAIT_CUTS 36
#define CUT_COLOUR (CUT_PORTRAIT + PORTRAIT_CUTS) /* a little colour */
#define COLOUR_CUTS 5
#define CUT_CHTEXT (CUT_COLOUR + COLOUR_CUTS) /* the library's name */
#define CHTEXT_CUTS 24
#define CUT_LOOP (CUT_CHTEXT + CHTEXT_CUTS + 34) /* 5 s hold, then frame 1 */
#define FLY_CUTS (CUT_FOLLOW - CUT_TAKEOFF)
#define EMERGE_CUTS 14	   /* one letter's flight out of a flower */
/* camera speeds, world units per cut */
#define WALK_SPEED 0.045f
#define FOLLOW_SPEED 0.085f
/* flower heads seen in the last cut, where text letters come from */
#define MAX_HEADS 256

enum { T_PHUL, T_MUGA, T_KOPOU, T_LILY, T_TUFT };

typedef struct _item_ {
	float x, z;	 /* world position, z away from the viewer */
	float h;	 /* stem height (world) */
	float rs;	 /* head radius (world) */
	float phase; /* sway phase */
	uint8_t type;
	uint8_t leafy; /* leaf fill: 0 paper, 1 leaf green */
} Item;

static int S;				/* cell size in pixels */
static int gw, gh;			/* grid size in cells */
static int pxW, pxH;		/* drawing area in pixels */
static int sceneTop, sceneBot, capTop, capBot;
static int horizon;
static uint8_t* _cur;	/* the cut being drawn */
static uint8_t* _shown; /* the cut on screen */
static uint8_t* _mask;	/* scratch for shapes with ink outlines */
static size_t _heapBytes;
static int clipTop, clipBot;
static float _fs; /* figure scale: 1 on a 255 x 160 cell scene */

static Item _items[NUM_ITEMS];
static uint8_t _order[NUM_ITEMS];
static float _camz;
static float _camx; /* sideways pan, used while following the butterfly */
static int16_t _headX[MAX_HEADS], _headY[MAX_HEADS];
static int _nheads;
static int16_t _srcX[MAX_HEADS], _srcY[MAX_HEADS];
static int _nsrc;
static uint32_t _cut;	   /* cut within the loop */
static uint32_t _cutTotal; /* cuts since start */
static uint32_t _rng;
static int _hero;		   /* flower the butterfly lands on */
static float _heroX, _heroY, _heroR;

static PbCaption _cap;

/* ------------------------------------------------------------------ */
/* small maths, no libm needed                                         */
/* ------------------------------------------------------------------ */

#define PB_PI 3.14159265f

static float pb_sin(float x) {
	/* reduce to [-pi, pi] */
	float k = (float)(int)(x * (1.0f / (2.0f * PB_PI)));
	x -= k * 2.0f * PB_PI;
	if (x > PB_PI)
		x -= 2.0f * PB_PI;
	else if (x < -PB_PI)
		x += 2.0f * PB_PI;
	/* parabola + correction, error < 0.001 */
	float y = 1.27323954f * x - 0.405284735f * x * (x < 0 ? -x : x);
	return 0.225f * (y * (y < 0 ? -y : y) - y) + y;
}

static float pb_cos(float x) { return pb_sin(x + PB_PI * 0.5f); }

static inline int pb_round(float v) { return (int)(v < 0 ? v - 0.5f : v + 0.5f); }

static float pb_sqrt(float v) {
	if (v <= 0)
		return 0;
	float x = v > 1 ? v : 1;
	for (int i = 0; i < 14; i++)
		x = 0.5f * (x + v / x);
	return x;
}

static inline float pb_clamp(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

static inline float pb_ease(float u) {
	u = pb_clamp(u, 0, 1);
	return u * u * (3 - 2 * u);
}

static uint32_t pb_hash(uint32_t x) {
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return x;
}

static float rnd01() {
	_rng = _rng * 1664525u + 1013904223u;
	return (float)(_rng >> 8) * (1.0f / 16777216.0f);
}

static float rnd(float a, float b) { return a + (b - a) * rnd01(); }

static uint32_t mix(uint32_t a, uint32_t b, int t) {
	/* t in 0..256, from a towards b */
	int ra = (a >> 16) & 0xFF, ga = (a >> 8) & 0xFF, ba = a & 0xFF;
	int rb = (b >> 16) & 0xFF, gb = (b >> 8) & 0xFF, bb = b & 0xFF;
	int r = ra + (((rb - ra) * t) >> 8);
	int g = ga + (((gb - ga) * t) >> 8);
	int bl = ba + (((bb - ba) * t) >> 8);
	return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

/* ------------------------------------------------------------------ */
/* woven swatches                                                      */
/* ------------------------------------------------------------------ */

/*
 * Each grid cell is shown as one thread of plain-weave cloth: cells
 * alternate between a vertical (warp) and a horizontal (weft) thread.
 * A thread is dark at one edge, catches light next to it, and dips at
 * its end where it passes under the crossing thread.
 */
static void BuildSwatches() {
	for (int c = 0; c < P_COUNT; c++) {
		uint32_t base = _pal[c];
		bool paper = (c == P_PAPER || c == P_SHADE);
		uint32_t dark = mix(base, PB_C_INK, paper ? 34 : 64);
		uint32_t light = mix(base, PB_C_WHITE, paper ? 40 : 52);
		for (int o = 0; o < 2; o++) {
			for (int y = 0; y < S; y++) {
				for (int x = 0; x < S; x++) {
					int across = o == 0 ? x : y; /* across the thread */
					int along = o == 0 ? y : x;	 /* along the thread */
					uint32_t col = base;
					if (across == 0)
						col = dark;
					else if (across == 1)
						col = light;
					else if (across == S - 1 && S > 3)
						col = mix(base, dark, 128);
					if (along == S - 1)
						col = mix(col, dark, 110);
					_swatch[c][o][y * S + x] = col;
				}
			}
		}
	}
}

static inline void PutSwatch(uint32_t* dst, int pitch, int cx, int cy, uint8_t c) {
	const uint32_t* sw = _swatch[c][(cx + cy) & 1];
	uint32_t* p = dst + (size_t)cy * S * pitch + (size_t)cx * S;
	for (int y = 0; y < S; y++) {
		for (int x = 0; x < S; x++)
			p[x] = sw[y * S + x];
		p += pitch;
	}
}

/* ------------------------------------------------------------------ */
/* drawing on the grid                                                 */
/* ------------------------------------------------------------------ */

static inline void Put(int x, int y, uint8_t c) {
	if ((unsigned)x < (unsigned)gw && y >= clipTop && y < clipBot)
		_cur[y * gw + x] = c;
}

static void Line(int x0, int y0, int x1, int y1, uint8_t c, int thick) {
	int dx = x1 > x0 ? x1 - x0 : x0 - x1;
	int dy = y1 > y0 ? y0 - y1 : y1 - y0;
	int sx = x0 < x1 ? 1 : -1;
	int sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;
	for (int guard = 0; guard < 4096; guard++) {
		Put(x0, y0, c);
		if (thick > 1) {
			Put(x0 + 1, y0, c);
			Put(x0, y0 + 1, c);
		}
		if (x0 == x1 && y0 == y1)
			break;
		int e2 = 2 * err;
		if (e2 >= dy) {
			err += dy;
			x0 += sx;
		}
		if (e2 <= dx) {
			err += dx;
			y0 += sy;
		}
	}
}

static void FillRect(int x, int y, int w, int h, uint8_t c) {
	for (int j = 0; j < h; j++)
		for (int i = 0; i < w; i++)
			Put(x + i, y + j, c);
}

/* a dotted line: only every step-th cell is inked */
static void DotLine(int x0, int y0, int x1, int y1, uint8_t c, int step) {
	int dx = x1 > x0 ? x1 - x0 : x0 - x1;
	int dy = y1 > y0 ? y0 - y1 : y1 - y0;
	int sx = x0 < x1 ? 1 : -1;
	int sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;
	for (int n = 0; n < 4096; n++) {
		if (n % step == 0)
			Put(x0, y0, c);
		if (x0 == x1 && y0 == y1)
			break;
		int e2 = 2 * err;
		if (e2 >= dy) {
			err += dy;
			x0 += sx;
		}
		if (e2 <= dx) {
			err += dx;
			y0 += sy;
		}
	}
}

/*
 * The mask: shapes are filled into it first, then committed to the grid
 * with an ink outline wherever a filled cell touches an empty one. That
 * is what gives every flower and wing its pen line.
 */
static int mx0, my0;
/* the touched part of the mask; outside it the mask is always zero, so
 * a commit only visits (and clears) what the shape actually covered */
static int mbx0, mby0, mbx1, mby1;

static void MaskBegin(int cx, int cy) {
	mx0 = cx - MASK_DIM / 2;
	my0 = cy - MASK_DIM / 2;
	mbx0 = mby0 = MASK_DIM;
	mbx1 = mby1 = -1;
}

static inline void MaskPut(int x, int y, uint8_t c) {
	x -= mx0;
	y -= my0;
	if ((unsigned)x < MASK_DIM && (unsigned)y < MASK_DIM) {
		_mask[y * MASK_DIM + x] = c + 1;
		if (x < mbx0)
			mbx0 = x;
		if (x > mbx1)
			mbx1 = x;
		if (y < mby0)
			mby0 = y;
		if (y > mby1)
			mby1 = y;
	}
}

/*
 * MaskEllipse -- rotated ellipse. Cells whose normalised radius is past
 * edgeAt get edge colour instead (a wing margin, say).
 */
static void MaskEllipse(float cx, float cy, float rx, float ry, float ang, uint8_t fill,
						uint8_t edge = 0xFF, float edgeAt = 2.0f) {
	if (rx < 0.6f)
		rx = 0.6f;
	if (ry < 0.6f)
		ry = 0.6f;
	float ca = pb_cos(ang), sa = pb_sin(ang);
	int R = (int)(rx > ry ? rx : ry) + 1;
	int icx = pb_round(cx), icy = pb_round(cy);
	float irx2 = 1.0f / (rx * rx), iry2 = 1.0f / (ry * ry);
	float edge2 = edgeAt * edgeAt;
	for (int y = icy - R; y <= icy + R; y++) {
		for (int x = icx - R; x <= icx + R; x++) {
			float dx = (float)x - cx, dy = (float)y - cy;
			float u = dx * ca + dy * sa;
			float v = -dx * sa + dy * ca;
			float n = u * u * irx2 + v * v * iry2;
			if (n <= 1.0f)
				MaskPut(x, y, (edge != 0xFF && n > edge2) ? edge : fill);
		}
	}
}

static void MaskCircle(float cx, float cy, float r, uint8_t c) { MaskEllipse(cx, cy, r, r, 0, c); }

static void MaskCommit() {
	for (int y = mby0; y <= mby1; y++) {
		for (int x = mbx0; x <= mbx1; x++) {
			uint8_t m = _mask[y * MASK_DIM + x];
			if (!m)
				continue;
			bool edge = x == 0 || y == 0 || x == MASK_DIM - 1 || y == MASK_DIM - 1 ||
						!_mask[y * MASK_DIM + x - 1] || !_mask[y * MASK_DIM + x + 1] ||
						!_mask[(y - 1) * MASK_DIM + x] || !_mask[(y + 1) * MASK_DIM + x];
			Put(mx0 + x, my0 + y, edge ? P_INK : (uint8_t)(m - 1));
		}
	}
	for (int y = mby0; y <= mby1; y++)
		memset(_mask + y * MASK_DIM + mbx0, 0, mbx1 - mbx0 + 1);
}

/* MaskCapsule -- a thick stroke with round ends, for arms and legs */
static void MaskCapsule(float x0, float y0, float x1, float y1, float r, uint8_t c) {
	if (r < 0.6f)
		r = 0.6f;
	int ax = (int)((x0 < x1 ? x0 : x1) - r) - 1, bx = (int)((x0 > x1 ? x0 : x1) + r) + 1;
	int ay = (int)((y0 < y1 ? y0 : y1) - r) - 1, by = (int)((y0 > y1 ? y0 : y1) + r) + 1;
	float dx = x1 - x0, dy = y1 - y0, L2 = dx * dx + dy * dy;
	for (int y = ay; y <= by; y++) {
		for (int x = ax; x <= bx; x++) {
			float px = x - x0, py = y - y0;
			float t = L2 > 0 ? pb_clamp((px * dx + py * dy) / L2, 0, 1) : 0;
			float qx = px - t * dx, qy = py - t * dy;
			if (qx * qx + qy * qy <= r * r)
				MaskPut(x, y, c);
		}
	}
}

/*
 * ScanPoly -- fill a polygon (at most 16 points, x/y pairs) into the
 * mask, or straight onto the grid when toMask is false
 */
static void ScanPoly(const float* xy, int n, uint8_t c, bool toMask) {
	float ymin = xy[1], ymax = xy[1];
	for (int i = 1; i < n; i++) {
		if (xy[i * 2 + 1] < ymin)
			ymin = xy[i * 2 + 1];
		if (xy[i * 2 + 1] > ymax)
			ymax = xy[i * 2 + 1];
	}
	for (int y = (int)ymin - 1; y <= (int)ymax + 1; y++) {
		float fy = (float)y;
		float xs[16];
		int k = 0;
		for (int i = 0; i < n && k < 16; i++) {
			float ax = xy[i * 2], ay = xy[i * 2 + 1];
			float bx = xy[((i + 1) % n) * 2], by = xy[((i + 1) % n) * 2 + 1];
			if ((ay <= fy && by > fy) || (by <= fy && ay > fy))
				xs[k++] = ax + (fy - ay) * (bx - ax) / (by - ay);
		}
		for (int i = 1; i < k; i++) {
			float v = xs[i];
			int j = i - 1;
			while (j >= 0 && xs[j] > v) {
				xs[j + 1] = xs[j];
				j--;
			}
			xs[j + 1] = v;
		}
		for (int i = 0; i + 1 < k; i += 2) {
			for (int x = pb_round(xs[i]); x <= pb_round(xs[i + 1]); x++) {
				if (toMask)
					MaskPut(x, y, c);
				else
					Put(x, y, c);
			}
		}
	}
}

/*
 * IKElbow -- the elbow for a two-bone limb from s to h (lengths l1, l2),
 * bent towards the side that favours (wx, wy). The hand is pulled in
 * when it is out of reach.
 */
static void IKElbow(float sx, float sy, float* hx, float* hy, float l1, float l2, float wx, float wy,
					float* ex, float* ey) {
	float dx = *hx - sx, dy = *hy - sy;
	float d = pb_sqrt(dx * dx + dy * dy);
	float maxd = (l1 + l2) * 0.995f;
	if (d < 0.01f) {
		d = 0.01f;
		dx = 0.01f;
		dy = 0;
	}
	if (d > maxd) {
		dx *= maxd / d;
		dy *= maxd / d;
		*hx = sx + dx;
		*hy = sy + dy;
		d = maxd;
	}
	float ux = dx / d, uy = dy / d;
	float a = (l1 * l1 - l2 * l2 + d * d) / (2 * d);
	float h = pb_sqrt(l1 * l1 - a * a);
	float px = -uy, py = ux;
	if (px * wx + py * wy < 0) {
		px = -px;
		py = -py;
	}
	*ex = sx + ux * a + px * h;
	*ey = sy + uy * a + py * h;
}

/* ------------------------------------------------------------------ */
/* world                                                               */
/* ------------------------------------------------------------------ */

static void Spawn(Item* it, float z, bool flower) {
	it->z = z;
	/* beside the path, never on it, and spread over roughly what the
	 * camera can see at that distance so the near beds stay full */
	float d = z - _camz;
	float spread = d < 1.0f ? 1.0f : d > 7.0f ? 7.0f : d;
	float side = rnd01() < 0.5f ? -1.0f : 1.0f;
	/* biased towards the path edges, where the camera looks */
	float u = rnd01();
	it->x = side * (PATH_HALF + 0.12f + u * u * spread);
	it->phase = rnd(0.0f, 6.28f);
	if (!flower) {
		it->type = T_TUFT;
		it->h = rnd(0.08f, 0.16f);
		it->rs = 0;
		return;
	}
	float t = rnd01();
	it->type = t < 0.34f ? T_PHUL : t < 0.58f ? T_MUGA : t < 0.74f ? T_KOPOU : T_LILY;
	it->h = rnd(0.30f, 0.80f);
	it->rs = rnd(0.11f, 0.18f);
	/* "little colours where needed": only some leaves are green */
	it->leafy = rnd01() < 0.3f ? 1 : 0;
}

static void WorldReset() {
	_rng = 0x5eed1234u;
	_camz = 0;
	_camx = 0;
	for (int i = 0; i < NUM_ITEMS; i++) {
		Spawn(&_items[i], rnd(0.9f, 0.9f + GARDEN_DEPTH), i < NUM_FLOWERS);
		_order[i] = (uint8_t)i;
	}
	_hero = -1;
}

static void WorldAdvance(float dz) {
	_camz += dz;
	for (int i = 0; i < NUM_ITEMS; i++) {
		Item* it = &_items[i];
		/* passed the camera: plant it again far ahead */
		if (it->z - _camz < 0.55f)
			Spawn(it, it->z + GARDEN_DEPTH, i < NUM_FLOWERS);
	}
}

/* sort far to near; the order barely changes between cuts */
static void SortItems() {
	for (int i = 1; i < NUM_ITEMS; i++) {
		uint8_t k = _order[i];
		float dk = _items[k].z;
		int j = i - 1;
		while (j >= 0 && _items[_order[j]].z < dk) {
			_order[j + 1] = _order[j];
			j--;
		}
		_order[j + 1] = k;
	}
}

/* ------------------------------------------------------------------ */
/* the drawings                                                        */
/* ------------------------------------------------------------------ */

static void DrawSky() {
	int skyH = horizon - sceneTop;

	/* sun: a muga ring, woven dots inside, rays that boil every cut */
	float sx = gw * 0.80f, sy = sceneTop + skyH * 0.42f;
	float R = skyH * 0.24f;
	if (R < 4)
		R = 4;
	int iR = (int)R;
	for (int y = -iR - 1; y <= iR + 1; y++) {
		for (int x = -iR - 1; x <= iR + 1; x++) {
			float d2 = (float)(x * x + y * y);
			int px = pb_round(sx) + x, py = pb_round(sy) + y;
			if (d2 <= (R + 0.5f) * (R + 0.5f) && d2 > (R - 0.7f) * (R - 0.7f))
				Put(px, py, P_INK);
			else if (d2 <= (R - 0.7f) * (R - 0.7f) && d2 > (R - 2.2f) * (R - 2.2f))
				Put(px, py, P_MUGA);
			else if (d2 <= (R - 2.2f) * (R - 2.2f) && ((px + py) % 3) == 0)
				Put(px, py, P_MUGA);
		}
	}
	int boil = (int)(_cutTotal & 1);
	for (int i = 0; i < 12; i++) {
		float a = i * (PB_PI / 6.0f) + boil * 0.13f;
		float r0 = R + 2, r1 = R + ((i + boil) & 1 ? 4.0f : 5.5f);
		Line(pb_round(sx + pb_cos(a) * r0), pb_round(sy + pb_sin(a) * r0),
			 pb_round(sx + pb_cos(a) * r1), pb_round(sy + pb_sin(a) * r1), P_INK, 1);
	}

	/* far hills, a soft line; near hills, ink with woven hatching */
	int pyFar = 0, pyNear = 0;
	for (int x = 0; x < gw; x++) {
		float fx = (float)x;
		int yf = horizon - 3 - pb_round(3.0f * pb_sin(fx * 0.045f + 1.0f) + 1.5f * pb_sin(fx * 0.11f));
		float bump = pb_sin(fx * 0.028f - 0.6f);
		int yn = horizon - 1 - pb_round(skyH * 0.16f * (bump > 0 ? bump : 0) +
										1.2f * pb_sin(fx * 0.17f + 2.0f) + 1.2f);
		if (x > 0) {
			Line(x - 1, pyFar, x, yf, P_INK_SOFT, 1);
			Line(x - 1, pyNear, x, yn, P_INK, 1);
		}
		for (int y = yn + 1; y < horizon; y++)
			if (((x - y) & 3) == 0)
				Put(x, y, P_INK_SOFT);
		pyFar = yf;
		pyNear = yn;
	}
}

static void DrawGround() {
	int vpx = gw / 2;
	float F = gw * 0.5f;
	/* the path's two edges run to the vanishing point */
	float run = (float)(sceneBot - horizon);
	float px = run / CAM_H; /* world-to-cells scale at the bottom edge */
	DotLine(vpx, horizon, pb_round(vpx + (-PATH_HALF - _camx) * px), sceneBot + 2, P_INK_SOFT, 2);
	DotLine(vpx, horizon, pb_round(vpx + (PATH_HALF - _camx) * px), sceneBot + 2, P_INK_SOFT, 2);

	/* stepping stones, one per world unit, sliding towards the viewer */
	int base = (int)_camz;
	for (int k = 14; k >= 1; k--) {
		float d = (float)(base + k) + 0.5f - _camz;
		if (d < 0.35f)
			continue;
		float sc = F / d;
		float cy = horizon + CAM_H * sc;
		if (cy - 2 >= sceneBot)
			continue;
		float rx = PATH_HALF * 0.55f * sc, ry = rx * 0.28f;
		float cx = vpx + (((k & 1) ? 0.08f : -0.08f) - _camx) * sc;
		if (rx < 1.2f) {
			Put(pb_round(cx), pb_round(cy), P_INK_SOFT);
			continue;
		}
		MaskBegin(pb_round(cx), pb_round(cy));
		MaskEllipse(cx, cy, rx, ry < 0.7f ? 0.7f : ry, 0, P_SHADE);
		MaskCommit();
	}

	/* the beds: grass stitches pinned to the ground every half unit,
	 * so they slide past with the camera and fill the near ground */
	int zb = (int)(_camz * 2.0f);
	for (int zi = 16; zi >= 1; zi--) {
		int zw = zb + zi;
		float d = zw * 0.5f - _camz;
		if (d < 0.4f)
			continue;
		float sc = F / d;
		int y = pb_round(horizon + CAM_H * sc);
		if (y >= sceneBot + 2 || y <= horizon)
			continue;
		for (int xi = 0; xi < 16; xi++) {
			for (int side = -1; side <= 1; side += 2) {
				uint32_t h = pb_hash((uint32_t)zw * 977u + xi * 31u + (side > 0 ? 7u : 0u));
				float wx = side * (PATH_HALF + 0.08f + xi * 0.42f + (h & 0xFF) * (0.3f / 255.0f));
				int x = pb_round(vpx + (wx - _camx) * sc);
				if (x < -2 || x > gw + 2)
					continue;
				if (sc < 7.0f) {
					if ((h >> 8) % 3 == 0)
						Put(x, y, P_INK_SOFT);
					continue;
				}
				float L = 0.07f * sc * (0.6f + ((h >> 16) & 0xFF) * (0.6f / 255.0f));
				uint8_t c = sc > 40.0f ? P_INK : P_INK_SOFT;
				int lean = (int)((h >> 24) % 3) - 1;
				Line(x, y, x + lean * pb_round(L * 0.3f), pb_round(y - L), c, 1);
				if ((h >> 12) % 4 == 0)
					Line(x, y, x - lean - 1 - pb_round(L * 0.25f), pb_round(y - L * 0.7f), c, 1);
				if ((h >> 20) % 9 == 0)
					Put(x, pb_round(y - L) - 1, P_LEAF);
			}
		}
	}
	Line(0, horizon, gw - 1, horizon, P_INK, 1);
}

static void DrawTuft(int x, int y, float s, float sway) {
	float L = s;
	uint8_t c = s < 3.5f ? P_INK_SOFT : P_INK;
	for (int i = -1; i <= 1; i++) {
		float a = -PB_PI / 2 + i * 0.5f + sway * 0.25f;
		Line(x, y, pb_round(x + pb_cos(a) * L * (i ? 0.8f : 1.0f)),
			 pb_round(y + pb_sin(a) * L * (i ? 0.8f : 1.0f)), c, 1);
	}
}

static void DrawLeaf(float x, float y, float len, float ang, uint8_t fill) {
	MaskBegin(pb_round(x), pb_round(y));
	float cx = x + pb_cos(ang) * len * 0.5f, cy = y + pb_sin(ang) * len * 0.5f;
	MaskEllipse(cx, cy, len * 0.5f, len * 0.2f, ang, fill);
	MaskCommit();
	/* the midrib */
	Line(pb_round(x), pb_round(y), pb_round(x + pb_cos(ang) * len * 0.85f),
		 pb_round(y + pb_sin(ang) * len * 0.85f), P_INK, 1);
}

static void DrawHead(int type, float hx, float hy, float r, float spin) {
	MaskBegin(pb_round(hx), pb_round(hy));
	switch (type) {
	case T_PHUL: {
		for (int i = 0; i < 6; i++) {
			float a = spin + i * (PB_PI / 3.0f);
			MaskEllipse(hx + pb_cos(a) * r * 0.52f, hy + pb_sin(a) * r * 0.52f, r * 0.48f,
						r * 0.27f, a, P_RED, P_RED_DEEP, 0.78f);
		}
		MaskCircle(hx, hy, r * 0.32f, P_INK);
		MaskCircle(hx, hy, r * 0.24f, P_MUGA);
		MaskCircle(hx, hy, r * 0.08f, P_RED_DEEP);
		break;
	}
	case T_MUGA: {
		/* marigold: a scalloped disc woven in two golds */
		for (int i = 0; i < 9; i++) {
			float a = spin + i * (2 * PB_PI / 9.0f);
			MaskCircle(hx + pb_cos(a) * r * 0.70f, hy + pb_sin(a) * r * 0.70f, r * 0.32f, P_MUGA);
		}
		MaskCircle(hx, hy, r * 0.78f, P_MUGA);
		int ir = (int)(r * 0.55f);
		for (int y = -ir; y <= ir; y++)
			for (int x = -ir; x <= ir; x++)
				if (x * x + y * y <= ir * ir && ((x + y) & 1) == 0)
					MaskPut(pb_round(hx) + x, pb_round(hy) + y, P_MUGA_DEEP);
		MaskCircle(hx, hy, r * 0.12f, P_RED_DEEP);
		break;
	}
	case T_LILY: {
		/* ivory petals: on paper these are almost all pen line */
		for (int i = 0; i < 5; i++) {
			float a = spin - PB_PI / 2 + i * (2 * PB_PI / 5.0f);
			MaskEllipse(hx + pb_cos(a) * r * 0.5f, hy + pb_sin(a) * r * 0.5f, r * 0.56f, r * 0.21f,
						a, P_WHITE);
		}
		MaskCircle(hx, hy, r * 0.2f, P_MUGA);
		break;
	}
	case T_KOPOU: {
		/* kopou phul, the foxtail orchid: a hanging spray of pink */
		for (int i = 0; i < 7; i++) {
			float t = i / 6.0f;
			float bx = hx + r * 0.7f * t + pb_sin(spin + t * 3) * r * 0.1f;
			float by = hy + r * 1.5f * t;
			float br = r * (0.24f - 0.08f * t);
			MaskCircle(bx, by, br, P_PINK);
			MaskCircle(bx, by, br * 0.35f, P_RED_DEEP);
		}
		break;
	}
	}
	MaskCommit();
}

static void DrawFlower(int idx, const Item* it, float d, float dyHero, bool still) {
	float F = gw * 0.5f;
	float s = F / d;
	float sx = gw * 0.5f + (it->x - _camx) * s;
	float by = horizon + CAM_H * s;
	float r = it->rs * s;
	float sway = still ? 0.0f : pb_sin(_cutTotal * 0.35f + it->phase);
	float hx = sx + sway * r * 0.3f;
	float hy = by - it->h * s + dyHero;

	if (sx < -r * 3 || sx > gw + r * 3)
		return;

	if (it->type == T_TUFT) {
		DrawTuft(pb_round(sx), pb_round(by), it->h * s, sway);
		return;
	}

	if (r >= 1.5f && _nheads < MAX_HEADS && hy >= sceneTop + 2 && hy < sceneBot - 2 &&
		hx >= 2 && hx < gw - 2) {
		_headX[_nheads] = (int16_t)pb_round(hx);
		_headY[_nheads] = (int16_t)pb_round(hy);
		_nheads++;
	}

	static const uint8_t dot[4] = { P_RED, P_MUGA, P_PINK, P_INK_SOFT };
	if (r < 1.1f) {
		Put(pb_round(hx), pb_round(hy), dot[it->type]);
		return;
	}

	/* stem: two strokes through a control point that boils by a cell */
	int jit = (int)(pb_hash(idx * 131u + _cutTotal) % 3) - 1;
	int thick = r >= 8 ? 2 : 1;
	int cx = pb_round(sx + sway * r * 0.12f) + (r >= 4 ? jit : 0);
	int cy = pb_round((by + hy) * 0.5f);
	float topY = it->type == T_KOPOU ? hy - r * 0.6f : hy;
	Line(pb_round(sx), pb_round(by), cx, cy, P_INK, thick);
	Line(cx, cy, pb_round(hx), pb_round(topY), P_INK, thick);

	if (r < 2.6f) {
		uint8_t c = dot[it->type];
		int x = pb_round(hx), y = pb_round(hy);
		Put(x, y, P_INK);
		Put(x - 1, y, c);
		Put(x + 1, y, c);
		Put(x, y - 1, c);
		Put(x, y + 1, c);
		return;
	}

	if (r >= 3.5f) {
		float len = r * 1.25f;
		float ly = by + (hy - by) * 0.45f;
		float side = it->phase > 3.14f ? 1.0f : -1.0f;
		DrawLeaf(sx + (cx - sx) * 0.5f, ly, len, side > 0 ? -0.6f : PB_PI + 0.6f,
				 it->leafy ? P_LEAF : P_PAPER);
		if (r >= 6.0f)
			DrawLeaf(sx + (cx - sx) * 0.7f, by + (hy - by) * 0.65f, len * 0.8f,
					 side > 0 ? PB_PI + 0.5f : -0.5f, P_PAPER);
	}

	if (it->type == T_KOPOU) {
		/* the spray hangs from an arched tip */
		Line(pb_round(hx), pb_round(topY), pb_round(hx + r * 0.35f), pb_round(hy - r * 0.2f), P_INK,
			 thick);
		DrawHead(T_KOPOU, hx + r * 0.35f, hy, r, it->phase + _cutTotal * 0.2f);
		return;
	}
	DrawHead(it->type, hx, hy, r, it->phase + (still ? 0 : sway * 0.08f));
}

/* ------------------------------------------------------------------ */
/* the butterfly                                                       */
/* ------------------------------------------------------------------ */

/*
 * k is how open the wings are: 1 flat open, small values are the wings
 * seen edge-on as they beat.
 */
static void DrawButterfly(float cx, float cy, float s, float k) {
	if (s < 2.5f) {
		Put(pb_round(cx), pb_round(cy), P_INK);
		if (k > 0.4f) {
			Put(pb_round(cx) - 1, pb_round(cy), P_MUGA);
			Put(pb_round(cx) + 1, pb_round(cy), P_MUGA);
		}
		return;
	}
	MaskBegin(pb_round(cx), pb_round(cy));
	for (int side = -1; side <= 1; side += 2) {
		float sg = (float)side;
		/* forewing with a deep red margin, an ivory spot near its tip */
		MaskEllipse(cx + sg * k * s * 0.44f, cy - s * 0.2f, k * s * 0.48f, s * 0.31f, sg * -0.5f,
					P_MUGA, P_RED_DEEP, 0.74f);
		if (k > 0.5f)
			MaskCircle(cx + sg * k * s * 0.62f, cy - s * 0.33f, s * 0.07f, P_WHITE);
		/* hindwing with a red eye */
		MaskEllipse(cx + sg * k * s * 0.31f, cy + s * 0.22f, k * s * 0.33f, s * 0.25f, sg * 0.5f,
					P_MUGA, P_RED_DEEP, 0.8f);
		if (k > 0.5f)
			MaskCircle(cx + sg * k * s * 0.34f, cy + s * 0.26f, s * 0.08f, P_RED);
	}
	/* woven lozenge rows across the forewings, gamusa style */
	if (k > 0.7f && s >= 10) {
		for (int side = -1; side <= 1; side += 2) {
			int lx = pb_round(cx + side * k * s * 0.38f) - 3;
			int ly = pb_round(cy - s * 0.2f) - 3;
			for (int r = 0; r < 7; r++)
				for (int b = 0; b < 7; b++)
					if (_lozenge[r] & (1u << (6 - b)))
						MaskPut(lx + b, ly + r, P_RED);
		}
	}
	MaskEllipse(cx, cy + s * 0.04f, s * 0.07f + 0.5f, s * 0.42f, 0, P_CHARCOAL);
	MaskCircle(cx, cy - s * 0.38f, s * 0.08f + 0.4f, P_CHARCOAL);
	MaskCommit();
	for (int side = -1; side <= 1; side += 2) {
		int tx = pb_round(cx + side * s * 0.26f), ty = pb_round(cy - s * 0.78f);
		Line(pb_round(cx), pb_round(cy - s * 0.42f), tx, ty, P_INK, 1);
		Put(tx, ty - 1, P_INK);
		Put(tx + side, ty, P_INK);
	}
}

/* ------------------------------------------------------------------ */
/* the Xeneva bird                                                     */
/* ------------------------------------------------------------------ */

static inline bool BirdAt(int x, int y) {
	if ((unsigned)x >= BIRD_W || (unsigned)y >= BIRD_H)
		return false;
	return (_bird[y] >> (BIRD_W - 1 - x)) & 1;
}

/*
 * DrawBird -- the logo bird, mirrored to fly left, sc cells per logo
 * pixel. The wing turns about the shoulder by flap radians: 0 is the
 * logo's raised wing, positive values beat it down.
 */
static void DrawBird(float cx, float cy, float sc, float flap) {
	MaskBegin(pb_round(cx), pb_round(cy));
	float ca = pb_cos(flap), sa = pb_sin(flap);
	int R = (int)(BIRD_W * 0.62f * sc) + 2;
	if (R > MASK_DIM / 2 - 1)
		R = MASK_DIM / 2 - 1;
	for (int dy = -R; dy <= R; dy++) {
		for (int dx = -R; dx <= R; dx++) {
			/* back to logo pixels; mirrored, so the bird faces left */
			float u = -(float)dx / sc + BIRD_W * 0.5f;
			float v = (float)dy / sc + BIRD_H * 0.5f;
			bool on = false;
			if (u >= BIRD_WING_X) {
				on = BirdAt((int)u, (int)v);
			} else {
				/* undo the wing's turn about the shoulder */
				float rx = u - BIRD_PIVOT_X, ry = v - BIRD_PIVOT_Y;
				float wu = BIRD_PIVOT_X + rx * ca - ry * sa;
				float wv = BIRD_PIVOT_Y + rx * sa + ry * ca;
				on = wu < BIRD_WING_X && BirdAt((int)wu, (int)wv);
			}
			if (on)
				MaskPut(pb_round(cx) + dx, pb_round(cy) + dy, P_WHITE);
		}
	}
	/* the outline also rings the logo's eye, a hole in the mark */
	MaskCommit();
}

/* ------------------------------------------------------------------ */
/* frame 6: the flying gamusa                                          */
/* ------------------------------------------------------------------ */

/* the cloth's left edge on row y, t cuts into the sweep */
static float ClothLeft(int y, float t, float width) {
	float u = t / SWEEP_CUTS;
	float A = 6.0f + 3.0f * pb_sin(t * 0.7f);
	float from = -width - A - 6, to = gw + A + 6;
	return from + (to - from) * u + A * pb_sin(y * 0.07f + t * 0.8f);
}

/*
 * DrawGamusaSweep -- a gamusa flies across from the left, rippling. The
 * garden is gone wherever it has passed: behind it is bare cloth, ready
 * for frame 7.
 */
static void DrawGamusaSweep() {
	float t = (float)(_cut - CUT_GAMUSA);
	float width = gw * 0.3f;
	int w = (int)width;
	for (int y = sceneTop; y < sceneBot; y++) {
		int xl = pb_round(ClothLeft(y, t, width));
		int xr = xl + w + pb_round(4.0f * pb_sin(y * 0.05f - t * 0.6f));
		/* behind the cloth, the garden has been taken away */
		for (int x = 0; x < xl && x < gw; x++)
			Put(x, y, P_PAPER);
		int cw = xr - xl;
		int phase = (int)(t * 2);
		for (int x = xl < 0 ? 0 : xl; x <= xr && x < gw; x++) {
			int c = x - xl;
			uint8_t col;
			if (c == 0 || c == cw)
				col = P_INK;
			else if (c <= 2 || c >= cw - 2)
				col = P_RED;
			else if (c == 4 || c == cw - 4)
				col = P_RED_DEEP;
			else {
				/* a column of red phul near each long edge */
				int mc = c >= 6 && c < 15 ? c - 6 : (c > cw - 16 && c <= cw - 7 ? c - (cw - 15) : -1);
				int my = (y + phase) % 13;
				if (mc >= 0 && my < 9 && (_phul[my] & (1u << (8 - mc))))
					col = P_RED;
				else if (pb_sin(y * 0.12f + c * 0.05f + t * 0.9f) > 0.55f)
					col = P_SHADE; /* folds rippling through the cloth */
				else
					col = P_WHITE;
			}
			Put(x, y, col);
		}
	}
}

/* ------------------------------------------------------------------ */
/* frame 7: the bamboo garden, drawn as you watch                      */
/* ------------------------------------------------------------------ */

#define NUM_BAMBOO 10
#define STALK_GROW 10 /* cuts for one stalk to be drawn up */

/* a brush nib at the point being drawn */
static void Nib(int x, int y) {
	Put(x, y, P_INK);
	Put(x, y - 1, P_INK);
	Put(x - 1, y - 2, P_INK);
	Put(x, y - 2, P_MUGA);
	Put(x + 1, y - 2, P_INK);
}

static void DrawBambooLeaf(float x, float y, float len, float ang, uint8_t fill) {
	MaskBegin(pb_round(x), pb_round(y));
	MaskEllipse(x + pb_cos(ang) * len * 0.5f, y + pb_sin(ang) * len * 0.5f, len * 0.5f,
				len * 0.13f + 0.6f, ang, fill);
	MaskCommit();
}

/*
 * DrawBambooGarden -- erase is the cut into frame 9, or -1. While it
 * runs, the stalks shrink back down one after another, their leaves
 * come loose and fall, and last the hills are wiped away right to left.
 */
static void DrawBambooGarden(int erase) {
	int t = (int)(_cut - CUT_BAMBOO);

	/* the horizon and far hills are inked first, left to right */
	int reach = (int)((t + 1) * gw / 5.0f);
	if (reach > gw)
		reach = gw;
	if (erase > 14)
		reach = (int)(gw * (1.0f - (erase - 14) / 6.0f));
	int prev = 0;
	for (int x = 0; x < reach; x++) {
		int yh = horizon - 2 - pb_round(4.0f * pb_sin(x * 0.04f + 2.0f) + 2.0f * pb_sin(x * 0.13f));
		if (x > 0)
			Line(x - 1, prev, x, yh, P_INK_SOFT, 1);
		prev = yh;
		Put(x, horizon, P_INK);
	}
	if (reach < gw && t < 5)
		Nib(reach, horizon);

	/* stalks in two groves, left and right, the middle left open */
	for (int i = 0; i < NUM_BAMBOO; i++) {
		uint32_t h = pb_hash(0xBA3B00u + i * 97u);
		bool left = i < NUM_BAMBOO / 2;
		float lane = (i % (NUM_BAMBOO / 2)) / (float)(NUM_BAMBOO / 2 - 1);
		bool nearStalk = (h & 3) != 0;
		float x = left ? gw * (0.03f + 0.25f * lane) : gw * (0.72f + 0.25f * lane);
		x += (float)((h >> 4) % 7) - 3;
		int base = nearStalk ? sceneBot - 2 - (int)((h >> 8) % 10) : horizon + 3 + (int)((h >> 8) % 4);
		int thick = nearStalk ? 3 + (int)((h >> 12) & 1) : 1;
		float lean = (((h >> 16) % 9) - 4.0f) * 0.02f;
		int start = 4 + i * 2;
		float pGrow = (float)(t - start) / STALK_GROW;
		if (pGrow <= 0)
			continue;
		if (pGrow > 1)
			pGrow = 1;
		/* frame 9: the stalks are taken down in a scattered order */
		int es = (i * 7) % NUM_BAMBOO;
		float pErase = erase < 0 ? 1.0f : 1.0f - (erase - es) / 6.0f;
		float p = pGrow < pErase ? pGrow : pErase;
		int top = sceneTop - 4;
		int tipY = pb_round(base - (base - top) * (p > 0 ? p : 0));
		int seg = 9 + (int)((h >> 20) % 5);
		for (int y = base; p > 0 && y >= tipY; y--) {
			int xc = pb_round(x + (base - y) * lean);
			bool node = ((base - y) % seg) == 0 && y != base;
			for (int k = 0; k < thick; k++) {
				uint8_t c = (k == 0 || k == thick - 1) ? P_INK : (nearStalk ? P_LEAF : P_PAPER);
				if (node)
					c = P_INK;
				Put(xc + k, y, thick == 1 ? (nearStalk ? P_INK : P_INK_SOFT) : c);
			}
			if (node && thick > 1) {
				Put(xc - 1, y, P_INK);
				Put(xc + thick, y, P_INK);
			}
		}
		if (p > 0 && p < 1)
			Nib(pb_round(x + (base - tipY) * lean) + thick / 2, tipY);

		/* leaves sprout as the stalk passes their height */
		for (int j = 0; j < 4; j++) {
			float at = 0.42f + j * 0.16f;
			if (pGrow < at + 0.05f)
				continue;
			float grow = (pGrow - at - 0.05f) * 6.0f;
			if (grow > 1)
				grow = 1;
			int ly = pb_round(base - (base - top) * at);
			float lx = x + (base - ly) * lean + thick * 0.5f;
			float len = (nearStalk ? 16.0f : 8.0f) * (0.4f + 0.6f * grow);
			uint32_t hl = pb_hash(h + j * 13u);
			/* once the stalk has shrunk below it, the leaf falls */
			float fall = -1, fdx = 0, spin = 0;
			if (pErase < at + 0.05f) {
				fall = erase - (es + 6.0f * (1.0f - at - 0.05f));
				if (fall < 0)
					fall = 0;
				fdx = pb_sin(fall * 1.3f + j) * 3.0f + fall * ((hl & 8) ? 0.8f : -0.8f);
				spin = fall * 0.7f;
				fall = fall * 2.0f + fall * fall * 0.9f;
				if (ly + fall > sceneBot + 6)
					continue;
			}
			for (int n = 0; n < 3; n++) {
				float side = ((hl >> n) & 1) ? 1.0f : -1.0f;
				float ang = side > 0 ? 0.35f + n * 0.3f : PB_PI - 0.35f - n * 0.3f;
				if (fall >= 0 && n > 0)
					break; /* a falling sprig is one leaf */
				uint8_t fill = (n == 1 && nearStalk) ? P_LEAF : P_PAPER;
				if (fall >= 0 && nearStalk && (hl & 2))
					fill = P_LEAF; /* a falling sprig keeps a little green */
				DrawBambooLeaf(lx + fdx, ly + n * 2 + (fall > 0 ? fall : 0), len * (1 - n * 0.15f),
							   ang + spin, fill);
			}
		}
	}

	/* a few ground strokes */
	if (t > 6 && (erase < 0 || erase < 12)) {
		for (int i = 0; i < 40; i++) {
			uint32_t h = pb_hash(0x6400u + i * 7u);
			int gx = (int)(h % (uint32_t)gw);
			int gy = horizon + 3 + (int)((h >> 8) % (uint32_t)(sceneBot - horizon - 4));
			int L = 1 + (gy - horizon) / 12;
			Line(gx, gy, gx + (int)((h >> 16) % 3) - 1, gy - L, P_INK_SOFT, 1);
		}
	}
}

/* ------------------------------------------------------------------ */
/* frame 8: a boy, his XR glasses, apps in the air                     */
/* ------------------------------------------------------------------ */

/*
 * The boy is a jointed cut-out, like a shadow puppet. Joints are in
 * "boy units" (56 tall, feet at 0, up negative) and placed on the grid
 * at bx/by with the figure scale. Each body part is filled into the mask
 * and committed separately, so limbs in front get their own pen line.
 */
typedef struct _boy_ {
	float hx, hy;		 /* head centre */
	float nx, ny;		 /* neck */
	float sx[2], sy[2];	 /* shoulders, 0 the far / screen-left one */
	float ex[2], ey[2];	 /* elbows */
	float wx[2], wy[2];	 /* hands */
	float px[2], py[2];	 /* hips */
	float kx[2], ky[2];	 /* knees */
	float fx[2], fy[2];	 /* feet */
	int face;			 /* 0 towards us, +1 / -1 running right / left */
	bool glasses;		 /* worn */
	bool inHand;		 /* held in hand 1, on the way to his face */
} Boy;

#define BOY_ARM 8.5f
#define BOY_LEG 11.0f

static float _bx, _by; /* where boy units land on the grid */
static float _bf;	   /* grid cells per boy unit */

static inline float BX(float u) { return _bx + u * _bf; }
static inline float BY(float v) { return _by + v * _bf; }

/* BoyRun -- one of four running poses, facing dir */
static void BoyRun(Boy* b, int dir, int t) {
	float ph = t * (PB_PI * 0.5f) + 0.4f;
	float sp = pb_sin(ph);
	float bob = 1.3f * (1.0f - (sp < 0 ? -sp : sp));
	float hipX = 0, hipY = -22 - bob;
	float shX = dir * 2.6f, shY = -35.8f - bob;
	b->face = dir;
	b->hx = shX + dir * 1.4f;
	b->hy = shY - 9.0f;
	b->nx = shX + dir * 0.6f;
	b->ny = shY - 2.0f;
	for (int i = 0; i < 2; i++) {
		float pi = ph + i * PB_PI;
		/* legs: swing, and the knee folds as the leg comes through */
		float a = 0.75f * pb_sin(pi);
		float c = pb_cos(pi);
		float bend = 0.2f + 1.2f * (c > 0 ? c : 0);
		b->px[i] = hipX;
		b->py[i] = hipY;
		b->kx[i] = hipX + dir * BOY_LEG * pb_sin(a);
		b->ky[i] = hipY + BOY_LEG * pb_cos(a);
		float s = a - bend;
		b->fx[i] = b->kx[i] + dir * BOY_LEG * pb_sin(s);
		b->fy[i] = b->ky[i] + BOY_LEG * pb_cos(s);
		/* arms swing against the legs, elbows bent */
		float ua = -0.9f * pb_sin(pi);
		b->sx[i] = shX - dir * (i == 0 ? 0.8f : 0.0f);
		b->sy[i] = shY + 0.5f;
		b->ex[i] = b->sx[i] + dir * BOY_ARM * pb_sin(ua);
		b->ey[i] = b->sy[i] + BOY_ARM * pb_cos(ua);
		float fa = ua + 1.5f;
		b->wx[i] = b->ex[i] + dir * BOY_ARM * pb_sin(fa);
		b->wy[i] = b->ey[i] + BOY_ARM * pb_cos(fa);
	}
}

/* BoyFront -- standing, facing us, hands at the given points */
static void BoyFront(Boy* b, const float* hand) {
	b->face = 0;
	b->hx = 0;
	b->hy = -45;
	b->nx = 0;
	b->ny = -38.5f;
	for (int i = 0; i < 2; i++) {
		float side = i == 0 ? -1.0f : 1.0f;
		b->px[i] = side * 3.0f;
		b->py[i] = -22;
		b->kx[i] = side * 3.1f;
		b->ky[i] = -11;
		b->fx[i] = side * 3.4f;
		b->fy[i] = 0;
		b->sx[i] = side * 6.2f;
		b->sy[i] = -36;
		b->wx[i] = hand[i * 2];
		b->wy[i] = hand[i * 2 + 1];
		IKElbow(b->sx[i], b->sy[i], &b->wx[i], &b->wy[i], BOY_ARM, BOY_ARM, side, 0.8f, &b->ex[i],
				&b->ey[i]);
	}
}

static void BoyLeg(const Boy* b, int i) {
	MaskBegin(pb_round(_bx), pb_round(BY(-28)));
	float r = 1.9f * _bf;
	MaskCapsule(BX(b->px[i]), BY(b->py[i]), BX(b->kx[i]), BY(b->ky[i]), r, P_SKIN);
	MaskCapsule(BX(b->kx[i]), BY(b->ky[i]), BX(b->fx[i]), BY(b->fy[i]), r * 0.95f, P_SKIN);
	/* half pant: the thigh is inside it to just above the knee */
	float mx = b->px[i] + (b->kx[i] - b->px[i]) * 0.6f, my = b->py[i] + (b->ky[i] - b->py[i]) * 0.6f;
	MaskCapsule(BX(b->px[i]), BY(b->py[i]), BX(mx), BY(my), 2.9f * _bf, P_MUGA_DEEP);
	/* shoe */
	MaskEllipse(BX(b->fx[i] + b->face * 1.2f), BY(b->fy[i] - 0.4f), (b->face ? 2.7f : 2.0f) * _bf,
				1.4f * _bf, 0, P_CHARCOAL);
	MaskCommit();
}

static void BoyArm(const Boy* b, int i) {
	MaskBegin(pb_round(BX(b->sx[i])), pb_round(BY(b->sy[i])));
	MaskCapsule(BX(b->sx[i]), BY(b->sy[i]), BX(b->ex[i]), BY(b->ey[i]), 1.6f * _bf, P_SKIN);
	MaskCapsule(BX(b->ex[i]), BY(b->ey[i]), BX(b->wx[i]), BY(b->wy[i]), 1.5f * _bf, P_SKIN);
	MaskCircle(BX(b->wx[i]), BY(b->wy[i]), 1.9f * _bf, P_SKIN);
	/* the T-shirt sleeve */
	float mx = b->sx[i] + (b->ex[i] - b->sx[i]) * 0.45f, my = b->sy[i] + (b->ey[i] - b->sy[i]) * 0.45f;
	MaskCapsule(BX(b->sx[i]), BY(b->sy[i]), BX(mx), BY(my), 2.5f * _bf, P_RED);
	MaskCommit();
}

static void BoyBody(const Boy* b) {
	float f = _bf;
	MaskBegin(pb_round(_bx), pb_round(BY(-28)));
	float hipX = (b->px[0] + b->px[1]) * 0.5f, hipY = (b->py[0] + b->py[1]) * 0.5f;
	float shX = (b->sx[0] + b->sx[1]) * 0.5f, shY = (b->sy[0] + b->sy[1]) * 0.5f;
	MaskCapsule(BX(b->nx), BY(b->ny), BX(b->hx), BY(b->hy + 4), 1.6f * f, P_SKIN);
	if (b->face == 0) {
		float shirt[] = { BX(-7.0f), BY(-37.0f), BX(7.0f), BY(-37.0f), BX(5.6f), BY(-23.5f),
						  BX(-5.6f), BY(-23.5f) };
		ScanPoly(shirt, 4, P_RED, true);
		float pant[] = { BX(-5.8f), BY(-25.0f), BX(5.8f), BY(-25.0f), BX(6.4f), BY(-15.0f),
						 BX(0.9f), BY(-15.0f), BX(0), BY(-19.0f), BX(-0.9f), BY(-15.0f),
						 BX(-6.4f), BY(-15.0f) };
		ScanPoly(pant, 7, P_MUGA_DEEP, true);
		/* a gamusa band woven round the hem of the T-shirt */
		for (int x = pb_round(BX(-5.6f)); x <= pb_round(BX(5.6f)); x++)
			MaskPut(x, pb_round(BY(-26.0f)), (x % 3) ? P_WHITE : P_RED);
	} else {
		MaskCapsule(BX(hipX), BY(hipY - 2.0f), BX(shX), BY(shY + 0.5f), 4.3f * f, P_RED);
		MaskCapsule(BX(hipX), BY(hipY - 2.4f), BX(hipX), BY(hipY + 0.8f), 4.6f * f, P_MUGA_DEEP);
		float hx0 = hipX + (shX - hipX) * 0.28f;
		for (int dx = -4; dx <= 4; dx++)
			MaskPut(pb_round(BX(hx0) + dx * f), pb_round(BY(hipY - 4.8f)), (dx & 1) ? P_WHITE : P_RED);
	}
	MaskCommit();
}

static void BoyHead(const Boy* b) {
	float f = _bf;
	float hx = BX(b->hx), hy = BY(b->hy), r = 6.5f * f;
	MaskBegin(pb_round(hx), pb_round(hy));
	if (b->face == 0) {
		MaskCircle(hx - r, hy + 0.8f * f, 1.5f * f, P_SKIN);
		MaskCircle(hx + r, hy + 0.8f * f, 1.5f * f, P_SKIN);
	}
	MaskCircle(hx, hy, r, P_SKIN);
	/* hair: a cap with a fringe, or the back of the head side on */
	int R = (int)r + 1;
	for (int y = -R; y <= R; y++) {
		for (int x = -R; x <= R; x++) {
			float dx = x / f, dy = y / f;
			if (dx * dx + dy * dy > 6.5f * 6.5f + 1.0f)
				continue;
			bool hair;
			if (b->face == 0)
				hair = dy < -2.6f + (((x + 64) % 3) == 0 ? 0.8f : 0.0f) ||
					   ((dx > 5.0f || dx < -5.0f) && dy < -0.5f);
			else
				hair = dy < -2.8f || (dx * b->face < -0.3f && dy < 2.2f);
			if (hair)
				MaskPut(pb_round(hx) + x, pb_round(hy) + y, P_CHARCOAL);
		}
	}
	if (b->face)
		MaskPut(pb_round(hx + b->face * 6.9f * f), pb_round(hy + 0.8f * f), P_SKIN); /* nose */
	MaskCommit();

	/* the face, in ink, and the glasses */
	int ix = pb_round(hx), iy = pb_round(hy);
	int u = pb_round(f) > 0 ? pb_round(f) : 1;
	bool glow = _cut >= CUT_XR && _cut < CUT_RUNOUT;
	uint8_t lens = glow && (_cutTotal & 1) ? P_WHITE : P_PINK;
	if (b->face == 0) {
		Put(ix - 1, iy + 3 * u, P_INK);
		Put(ix, iy + 3 * u + 1, P_INK);
		Put(ix + 1, iy + 3 * u, P_INK);
		if (b->glasses) {
			Line(ix - 5 * u, iy - 1, ix + 5 * u, iy - 1, P_INK, 1);
			for (int y = 0; y < 2; y++) {
				for (int x = 1; x <= 4 * u; x++) {
					Put(ix - x, iy + y, x == 4 * u ? P_INK : lens);
					Put(ix + x, iy + y, x == 4 * u ? P_INK : lens);
				}
				Put(ix, iy + y, P_INK);
			}
		} else {
			Put(ix - 2 * u, iy + 1, P_INK);
			Put(ix + 2 * u, iy + 1, P_INK);
		}
	} else {
		int d = b->face;
		Put(ix + d * 5 * u, iy + 3 * u + 1, P_INK);
		if (b->glasses) {
			Line(ix - d * 2 * u, iy - 1, ix + d * 6 * u, iy - 1, P_INK, 1);
			for (int x = 3 * u; x <= 5 * u; x++) {
				Put(ix + d * x, iy, lens);
				Put(ix + d * x, iy + 1, lens);
			}
		} else {
			Put(ix + d * 3 * u, iy + 1, P_INK);
		}
	}
}

static void DrawBoy(const Boy* b) {
	if (b->face == 0) {
		BoyLeg(b, 0);
		BoyLeg(b, 1);
		BoyBody(b);
		BoyHead(b);
		BoyArm(b, 0);
		BoyArm(b, 1);
	} else {
		BoyArm(b, 0);
		BoyLeg(b, 0);
		BoyBody(b);
		BoyHead(b);
		BoyLeg(b, 1);
		BoyArm(b, 1);
	}
	if (b->inHand) {
		/* the glasses, on their way from his pocket */
		int x = pb_round(BX(b->wx[1])), y = pb_round(BY(b->wy[1])) - pb_round(2 * _bf);
		int u = pb_round(_bf) > 0 ? pb_round(_bf) : 1;
		Line(x - 4 * u, y, x + 4 * u, y, P_INK, 1);
		for (int i = 1; i <= 3 * u; i++) {
			Put(x - i, y + 1, P_PINK);
			Put(x + i, y + 1, P_PINK);
		}
	}
}

/*
 * DrawPanel -- an app window floating in the air, as seen through the
 * glasses: a dotted glow around it, a title bar, and the app inside
 */
static void DrawPanel(float cx, float cy, float sc, int app, bool flash) {
	int w = pb_round(22 * _bf * sc), h = pb_round(15.5f * _bf * sc);
	if (w < 4 || h < 3) {
		if (sc > 0.05f)
			Put(pb_round(cx), pb_round(cy), P_PINK);
		return;
	}
	int x0 = pb_round(cx - w * 0.5f), y0 = pb_round(cy - h * 0.5f);
	for (int x = x0 - 2; x <= x0 + w + 1; x++) {
		if ((x + _cutTotal) & 1) {
			Put(x, y0 - 2, P_PINK);
			Put(x, y0 + h + 1, P_PINK);
		}
	}
	for (int y = y0 - 2; y <= y0 + h + 1; y++) {
		if ((y + _cutTotal) & 1) {
			Put(x0 - 2, y, P_PINK);
			Put(x0 + w + 1, y, P_PINK);
		}
	}
	FillRect(x0, y0, w, h, app == 1 ? P_CHARCOAL : P_WHITE);
	int tb = h >= 10 ? 3 : 1;
	FillRect(x0, y0, w, tb, flash ? P_MUGA : P_RED);
	if (tb == 3 && w >= 10) {
		Put(x0 + w - 3, y0 + 1, P_WHITE);
		Put(x0 + w - 5, y0 + 1, P_WHITE);
		Put(x0 + w - 7, y0 + 1, P_MUGA);
	}
	Line(x0 - 1, y0 - 1, x0 + w, y0 - 1, P_INK, 1);
	Line(x0 - 1, y0 + h, x0 + w, y0 + h, P_INK, 1);
	Line(x0 - 1, y0 - 1, x0 - 1, y0 + h, P_INK, 1);
	Line(x0 + w, y0 - 1, x0 + w, y0 + h, P_INK, 1);
	if (w < 12 || h < 9)
		return;
	/* the app inside */
	int ax = x0 + 2, ay = y0 + tb + 1, aw = w - 4, ah = h - tb - 3;
	if (app == 0) {
		/* a calculator: a display and a block of keys */
		FillRect(ax, ay, aw, 2, P_SHADE);
		Put(ax + aw - 2, ay, P_INK);
		Put(ax + aw - 4, ay, P_INK);
		int kw = (aw - 2) / 3, kh = (ah - 3) / 3;
		for (int r = 0; r < 3; r++)
			for (int c = 0; c < 3; c++)
				FillRect(ax + c * (kw + 1), ay + 3 + r * (kh + 1), kw, kh > 0 ? kh : 1,
						 (r == 2 && c == 2) ? P_RED : P_MUGA);
	} else if (app == 1) {
		/* a terminal: lines of text, a cursor that blinks */
		static const uint8_t len[5] = { 9, 14, 6, 11, 4 };
		for (int r = 0; r < 5 && r * 2 < ah; r++) {
			int L = len[r] * aw / 16;
			Put(ax, ay + r * 2, P_MUGA);
			for (int x = 2; x < L; x++)
				if ((x * 7 + r) % 5)
					Put(ax + x, ay + r * 2, P_WHITE);
		}
		if (_cutTotal & 1)
			FillRect(ax + 2 + 4 * aw / 16, ay + 8, 2, 1, P_MUGA);
	} else {
		/* a picture: the sun over hills */
		int sx = ax + aw * 3 / 4, sy = ay + ah / 3;
		int sr = ah / 4 > 1 ? ah / 4 : 1;
		for (int y = -sr; y <= sr; y++)
			for (int x = -sr; x <= sr; x++)
				if (x * x + y * y <= sr * sr)
					Put(sx + x, sy + y, P_MUGA);
		int prev = ay + ah - 1;
		for (int x = 0; x < aw; x++) {
			int hy = ay + ah - 2 - pb_round((ah * 0.35f) * (0.5f + 0.5f * pb_sin(x * 0.35f)));
			Line(ax + x - 1, prev, ax + x, hy, P_INK, 1);
			for (int y = hy + 1; y < ay + ah; y++)
				Put(ax + x, y, ((x + y) & 1) ? P_LEAF : P_WHITE);
			prev = hy;
		}
	}
}

/* the three slots the apps float in: left, above, right (boy units) */
static const float _slot[3][2] = { { -34, -40 }, { 0, -70 }, { 34, -40 } };

/*
 * DrawPanels -- the apps, for shift (a carousel the hands push round),
 * zoom (of the app above his head), open (0..1, how far out of the
 * glasses the apps have come)
 */
static void DrawPanels(float shift, float zoom, float open, bool flash) {
	float px[3], py[3], ps[3], wt[3];
	for (int a = 0; a < 3; a++) {
		float s = a - shift;
		float s0f = (float)(int)(s < 0 ? s - 1 : s);
		if (s0f > s)
			s0f -= 1;
		int s0 = (int)s0f;
		float fr = s - s0f;
		int A = ((s0 % 3) + 3) % 3, B = (A + 1) % 3;
		float sc = 1;
		float x, y;
		if (A == 2 && B == 0) {
			/* wrapping from right to left: it fades out and in again */
			const float* q = fr < 0.5f ? _slot[2] : _slot[0];
			x = q[0];
			y = q[1];
			sc = fr < 0.5f ? 1 - 2 * fr : 2 * fr - 1;
		} else {
			x = _slot[A][0] + (_slot[B][0] - _slot[A][0]) * fr;
			y = _slot[A][1] + (_slot[B][1] - _slot[A][1]) * fr;
		}
		float sm = s - 3.0f * (float)(int)((s + 300.0f) / 3.0f - 100.0f);
		float w = 1 - (sm - 1 < 0 ? 1 - sm : sm - 1);
		if (w < 0)
			w = 0;
		wt[a] = w;
		sc *= 1 + 0.15f * w;
		sc *= 1 + (zoom - 1) * w;
		/* projected from the glasses: they open where they float */
		px[a] = BX(x);
		py[a] = BY(y);
		ps[a] = sc * open;
	}
	/* the one above his head is drawn last, on top */
	int order[3] = { 0, 1, 2 };
	for (int i = 0; i < 3; i++)
		for (int j = i + 1; j < 3; j++)
			if (wt[order[j]] < wt[order[i]]) {
				int t = order[i];
				order[i] = order[j];
				order[j] = t;
			}
	for (int i = 0; i < 3; i++) {
		int a = order[i];
		if (open < 1)
			DotLine(pb_round(BX(0)), pb_round(BY(-45)), pb_round(px[a]), pb_round(py[a]), P_PINK, 2);
		DrawPanel(px[a], py[a], ps[a], a, flash && wt[a] > 0.5f);
	}
}

static inline void Lerp2(float* o, float ax, float ay, float bx, float by, float u) {
	o[0] = ax + (bx - ax) * u;
	o[1] = ay + (by - ay) * u;
}

/* DrawBoyScene -- frame 8, over the finished bamboo garden */
static void DrawBoyScene() {
	if (_cut < CUT_RUNIN)
		return;
	Boy b;
	memset(&b, 0, sizeof(b));
	float mid = gw * 0.5f;
	_bf = _fs * 1.3f;
	_by = (float)(sceneBot - pb_round(12 * _bf));

	if (_cut < CUT_GLASSES) {
		/* in from the left, slowing as he gets to the middle */
		float u = (float)(_cut - CUT_RUNIN) / RUNIN_CUTS;
		float e = 1 - (1 - u) * (1 - u);
		_bx = -16 * _bf + (mid + 16 * _bf) * e;
		BoyRun(&b, 1, (int)_cut);
		DrawBoy(&b);
		return;
	}
	if (_cut >= CUT_RUNOUT) {
		/* and off to the right, picking up speed */
		float u = (float)(_cut - CUT_RUNOUT) / RUNOUT_CUTS;
		_bx = mid + (gw + 20 * _bf - mid) * u * u;
		if (_bx - 12 * _bf > gw)
			return;
		BoyRun(&b, 1, (int)_cut);
		b.glasses = true;
		DrawBoy(&b);
		return;
	}

	_bx = mid;
	float hand[4] = { -7.6f, -20.0f, 7.6f, -20.0f }; /* screen-left, screen-right */
	float shift = 0, zoom = 1, open = 0;
	bool flash = false;
	if (_cut < CUT_XR) {
		/* from his pocket to his face */
		int g = (int)(_cut - CUT_GLASSES);
		static const float path[6][2] = { { 7.6f, -20 }, { 7.0f, -21 }, { 8.0f, -32 },
										  { 4.5f, -43 }, { 6.0f, -45 }, { 9.0f, -31 } };
		hand[2] = path[g][0];
		hand[3] = path[g][1];
		b.inHand = g >= 1 && g <= 3;
		b.glasses = g >= 4;
	} else {
		b.glasses = true;
		int x = (int)(_cut - CUT_XR);
		if (x < 4) {
			/* the apps come out of the glasses */
			open = (x + 1) / 4.0f;
			Lerp2(hand + 2, 7.6f, -20, 14, -44, open);
		} else if (x < 10) {
			/* swipe: the right hand pushes the apps round */
			float u = pb_ease((x - 4) / 5.0f);
			open = 1;
			shift = u;
			Lerp2(hand + 2, 21, -47, 2, -53, u);
		} else if (x < 16) {
			/* both hands spread: the app above grows */
			float u = pb_ease((x - 10) / 5.0f);
			open = 1;
			shift = 1;
			zoom = 1 + 0.35f * u;
			Lerp2(hand, -5, -54, -14, -56, u);
			Lerp2(hand + 2, 5, -54, 14, -56, u);
		} else if (x < 22) {
			/* swipe back with the left hand */
			float u = pb_ease((x - 16) / 5.0f);
			open = 1;
			shift = 1 - u;
			zoom = 1 + 0.35f * (1 - pb_ease((x - 16) / 2.0f));
			Lerp2(hand, -21, -47, -2, -53, u);
		} else {
			/* a tap on the app above, then they fold back in */
			int k = x - 22;
			static const float tap[5][2] = { { 6, -50 }, { 3, -56 }, { 3, -59 }, { 6, -50 }, { 7.6f, -24 } };
			hand[2] = tap[k][0];
			hand[3] = tap[k][1];
			flash = k == 2 || k == 3;
			open = k < 3 ? 1.0f : (k == 3 ? 0.55f : 0.2f);
		}
	}
	if (open > 0)
		DrawPanels(shift, zoom, open, flash);
	BoyFront(&b, hand);
	DrawBoy(&b);
}

/* ------------------------------------------------------------------ */
/* frame 9: Chitralekha draws Aniruddha                                */
/* ------------------------------------------------------------------ */

/*
 * Chitralekha, Usha's friend, could paint anyone from a description;
 * she painted the face Usha saw in her dream, and it was Aniruddha.
 * Here she sits on a mora at her easel and draws him, line by line.
 *
 * The portrait is a table of pen strokes on a 40 x 50 design grid, in
 * half units: { points, colour, x0, y0, x1, y1, ... }, ended by a 0.
 * A stroke of one point is a dot.
 */
#define HU(x, y) (int8_t)((x) * 2), (int8_t)((y) * 2)
static const int8_t _portrait[] = {
	/* the face */
	17, P_INK, HU(28, 24), HU(27.5, 27.8), HU(25.7, 31.1), HU(23.1, 33.2), HU(20, 34), HU(16.9, 33.2),
	HU(14.3, 31.1), HU(12.5, 27.8), HU(12, 24), HU(12.5, 20.2), HU(14.3, 16.9), HU(16.9, 14.8),
	HU(20, 14), HU(23.1, 14.8), HU(25.7, 16.9), HU(27.5, 20.2), HU(28, 24),
	/* the crown, its band and jewels */
	11, P_INK, HU(12, 15), HU(11, 8), HU(14, 10), HU(16, 4), HU(18, 8), HU(20, 1), HU(22, 8), HU(24, 4),
	HU(26, 10), HU(29, 8), HU(28, 15),
	2, P_INK, HU(12, 15), HU(28, 15),
	1, P_RED, HU(20, 9),
	1, P_RED, HU(16, 11),
	1, P_RED, HU(24, 11),
	/* hair falling to the shoulders */
	4, P_INK, HU(12, 16), HU(10, 20), HU(9, 26), HU(10, 32),
	4, P_INK, HU(28, 16), HU(30, 20), HU(31, 26), HU(30, 32),
	/* brows, eyes like lotus petals */
	3, P_INK, HU(14, 19), HU(16, 18), HU(18, 19),
	3, P_INK, HU(22, 19), HU(24, 18), HU(26, 19),
	5, P_INK, HU(14, 21.5), HU(16, 20.5), HU(18, 21.5), HU(16, 22.5), HU(14, 21.5),
	5, P_INK, HU(22, 21.5), HU(24, 20.5), HU(26, 21.5), HU(24, 22.5), HU(22, 21.5),
	1, P_INK, HU(16, 21.5),
	1, P_INK, HU(24, 21.5),
	/* nose, lips, the tilak */
	3, P_INK, HU(20, 21), HU(19, 26), HU(21, 26.5),
	3, P_RED, HU(17, 29), HU(20, 28.5), HU(23, 29),
	3, P_RED, HU(17.5, 29.5), HU(20, 31), HU(22.5, 29.5),
	2, P_RED, HU(20, 15.5), HU(20, 18),
	/* ears and kundal earrings */
	4, P_INK, HU(12, 21), HU(10.5, 22), HU(10.5, 25), HU(12, 26),
	4, P_INK, HU(28, 21), HU(29.5, 22), HU(29.5, 25), HU(28, 26),
	5, P_MUGA, HU(11, 26.5), HU(10, 28.5), HU(11, 30.5), HU(12, 28.5), HU(11, 26.5),
	5, P_MUGA, HU(29, 26.5), HU(28, 28.5), HU(29, 30.5), HU(30, 28.5), HU(29, 26.5),
	/* neck and shoulders */
	2, P_INK, HU(17.5, 33), HU(17, 37),
	2, P_INK, HU(22.5, 33), HU(23, 37),
	4, P_INK, HU(17, 37), HU(10, 39), HU(6, 43), HU(4.5, 49),
	4, P_INK, HU(23, 37), HU(30, 39), HU(34, 43), HU(35.5, 49),
	3, P_INK, HU(17, 37), HU(20, 41), HU(23, 37),
	/* a necklace with a pendant */
	5, P_MUGA, HU(13, 40), HU(16.5, 43.5), HU(20, 45), HU(23.5, 43.5), HU(27, 40),
	1, P_RED, HU(20, 46.5),
	0,
};

/* the colour washes, laid in after the drawing (design units) */
static const float _crownFill[] = { 12, 15, 11, 8, 14, 10, 16, 4, 18, 8, 20, 1, 22, 8, 24, 4, 26, 10, 29, 8, 28, 15 };
static const float _neckFill[] = { 17.5f, 33, 22.5f, 33, 23, 37.5f, 17, 37.5f };
static const float _robeFill[] = { 4.5f, 49, 6, 43, 10, 39, 17, 37, 20, 41, 23, 37, 30, 39, 34, 43, 35.5f, 49 };

static float _cvx, _cvy, _cvq; /* the canvas: top-left and scale */

static inline float CvX(float x) { return _cvx + x * _cvq; }
static inline float CvY(float y) { return _cvy + y * _cvq; }

static int PortraitSegments() {
	int n = 0;
	for (const int8_t* p = _portrait; *p; p += 2 + *p * 2)
		n += *p > 1 ? *p - 1 : 1;
	return n;
}

static void CanvasPoly(const float* xy, int n, uint8_t c) {
	float g[32];
	for (int i = 0; i < n; i++) {
		g[i * 2] = CvX(xy[i * 2]);
		g[i * 2 + 1] = CvY(xy[i * 2 + 1]);
	}
	ScanPoly(g, n, c, false);
}

/*
 * DrawPortrait -- the strokes drawn so far (segs, fractional) and the
 * washes laid in so far; the brush tip comes back in tip
 */
static void DrawPortrait(float segs, int washes, float* tipX, float* tipY) {
	if (washes > 0)
		CanvasPoly(_crownFill, 11, P_MUGA);
	if (washes > 1) {
		int cx = pb_round(CvX(20)), cy = pb_round(CvY(24));
		float rx = 8 * _cvq, ry = 10 * _cvq;
		for (int y = -(int)ry; y <= (int)ry; y++)
			for (int x = -(int)rx; x <= (int)rx; x++)
				if ((x * x) / (rx * rx) + (y * y) / (ry * ry) <= 1.0f)
					Put(cx + x, cy + y, P_SKIN);
		CanvasPoly(_neckFill, 4, P_SKIN);
	}
	if (washes > 2)
		CanvasPoly(_robeFill, 9, P_RED);

	float done = 0;
	for (const int8_t* p = _portrait; *p; p += 2 + *p * 2) {
		int n = p[0];
		uint8_t c = (uint8_t)p[1];
		const int8_t* q = p + 2;
		if (n == 1) {
			if (done >= segs)
				return;
			int x = pb_round(CvX(q[0] * 0.5f)), y = pb_round(CvY(q[1] * 0.5f));
			Put(x, y, c);
			*tipX = (float)x;
			*tipY = (float)y;
			done += 1;
			continue;
		}
		for (int i = 0; i + 1 < n; i++) {
			if (done >= segs)
				return;
			float u = segs - done;
			if (u > 1)
				u = 1;
			float ax = CvX(q[i * 2] * 0.5f), ay = CvY(q[i * 2 + 1] * 0.5f);
			float bx = CvX(q[i * 2 + 2] * 0.5f), by = CvY(q[i * 2 + 3] * 0.5f);
			bx = ax + (bx - ax) * u;
			by = ay + (by - ay) * u;
			Line(pb_round(ax), pb_round(ay), pb_round(bx), pb_round(by), c, 1);
			*tipX = bx;
			*tipY = by;
			done += 1;
		}
	}
}

/* a mora, the Assamese cane stool, an hourglass of woven cane */
static void DrawMora(float cx, float top, float bot) {
	MaskBegin(pb_round(cx), pb_round((top + bot) * 0.5f));
	float H = bot - top;
	for (int y = pb_round(top); y <= pb_round(bot); y++) {
		float r = (y - top) / H;
		float half = 6.5f * _fs * (0.5f + 0.5f * (2 * r - 1 < 0 ? 1 - 2 * r : 2 * r - 1));
		for (int x = pb_round(cx - half); x <= pb_round(cx + half); x++) {
			int a = x - y, b = x + y;
			bool cane = ((a % 3) + 3) % 3 == 0 || ((b % 3) + 3) % 3 == 0;
			bool rim = y <= pb_round(top) + 1 || y >= pb_round(bot) - 1;
			MaskPut(x, y, rim ? P_MUGA_DEEP : (cane ? P_MUGA_DEEP : P_MUGA));
		}
	}
	MaskCommit();
}

/* DrawChitralekhaScene -- frame 9 after the bamboo is gone */
static void DrawChitralekhaScene() {
	float f = _fs;
	int t = (int)(_cut - CUT_CHITRA);
	float G = (float)(sceneBot - pb_round(4 * f));

	/* the canvas on its easel */
	float cw = 36 * f, ch = 46 * f;
	float cl = gw * 0.5f - 5 * f;
	float cb = G - 27 * f, ct = cb - ch;
	_cvx = cl;
	_cvy = ct;
	_cvq = 0.9f * f;

	/* everything is drawn up from the floor in the first cuts */
	float figTop = ct - 6 * f;
	if (t < REVEAL_CUTS)
		clipTop = pb_round(G + 2 - (t + 1) * (G + 2 - figTop) / REVEAL_CUTS);

	/* floor and a woven mat under the mora */
	DotLine(0, pb_round(G) + 2, gw - 1, pb_round(G) + 2, P_INK_SOFT, 2);
	for (int x = pb_round(cl - 34 * f); x <= pb_round(cl - 2 * f); x++) {
		Put(x, pb_round(G) - 1, P_RED);
		Put(x, pb_round(G), (x % 4) ? P_WHITE : P_RED);
		Put(x, pb_round(G) + 1, P_RED);
	}

	/* the easel: two front legs, one behind, a ledge, a framed cloth */
	int apx = pb_round(cl + cw * 0.5f), apy = pb_round(ct - 5 * f);
	Line(apx, apy, apx + 2, pb_round(G), P_INK_SOFT, 1);
	Line(apx, apy, pb_round(cl - 3 * f), pb_round(G), P_MUGA_DEEP, 2);
	Line(apx, apy, pb_round(cl + cw + 3 * f), pb_round(G), P_MUGA_DEEP, 2);
	FillRect(pb_round(cl - 3 * f), pb_round(cb) + 1, pb_round(cw + 6 * f), 2, P_MUGA_DEEP);
	FillRect(pb_round(cl) - 1, pb_round(ct) - 1, pb_round(cw) + 2, pb_round(ch) + 2, P_MUGA_DEEP);
	FillRect(pb_round(cl), pb_round(ct), pb_round(cw), pb_round(ch), P_WHITE);
	Line(pb_round(cl) - 2, pb_round(ct) - 2, pb_round(cl + cw) + 1, pb_round(ct) - 2, P_INK, 1);
	Line(pb_round(cl) - 2, pb_round(ct + ch) + 1, pb_round(cl + cw) + 1, pb_round(ct + ch) + 1, P_INK, 1);
	Line(pb_round(cl) - 2, pb_round(ct) - 2, pb_round(cl) - 2, pb_round(ct + ch) + 1, P_INK, 1);
	Line(pb_round(cl + cw) + 1, pb_round(ct) - 2, pb_round(cl + cw) + 1, pb_round(ct + ch) + 1, P_INK, 1);

	/* the portrait so far, and where the brush is */
	float tipX = CvX(3), tipY = CvY(40);
	int pt = (int)_cut - CUT_PORTRAIT;
	if (pt >= 0) {
		int total = PortraitSegments();
		float segs = pt >= PORTRAIT_CUTS ? (float)total : (float)total * pt / PORTRAIT_CUTS;
		int washes = (int)_cut - CUT_COLOUR + 1;
		if (washes > 3)
			washes = 3;
		DrawPortrait(segs, washes, &tipX, &tipY);
		static const float washAt[3][2] = { { 20, 9 }, { 22, 25 }, { 10, 45 } };
		if (washes > 0 && _cut < CUT_CHTEXT) {
			tipX = CvX(washAt[washes - 1][0]);
			tipY = CvY(washAt[washes - 1][1]);
		} else if (_cut >= CUT_CHTEXT) {
			tipX = CvX(2);
			tipY = CvY(46);
		}
	}

	/* Chitralekha: hip on the mora, leaning to her work */
	float sx = cl - 13 * f, sy = G - 44 * f; /* shoulder */
	float hpx = cl - 16 * f, hpy = G - 22 * f; /* hip */
	float kx = hpx + 12 * f, ky = hpy + 0.5f * f; /* knee */

	/* the drawing arm: a long reed brush, hand kept at a natural reach */
	float Lb = 13 * f, L1 = 12 * f, L2 = 10 * f;
	float ax = sx + 0.5f * f, ay = sy + 1.5f * f;
	float dx = tipX - ax, dy = tipY - ay;
	float D = pb_sqrt(dx * dx + dy * dy);
	float lean = pb_clamp(D - Lb - (L1 + L2) * 0.97f, 0, 6 * f);
	if (D > 0.01f && lean > 0) {
		sx += dx / D * lean;
		sy += dy / D * lean * 0.5f;
		ax = sx + 0.5f * f;
		ay = sy + 1.5f * f;
		dx = tipX - ax;
		dy = tipY - ay;
		D = pb_sqrt(dx * dx + dy * dy);
	}
	if (D < 0.01f)
		D = 0.01f;
	float ex_ = dx / D, ey_ = dy / D;
	float dh = pb_clamp(D - Lb, 13 * f, (L1 + L2) * 0.97f);
	float cosT = pb_clamp((D * D + Lb * Lb - dh * dh) / (2 * D * Lb), -1, 1);
	float sinT = pb_sqrt(1 - cosT * cosT);
	/* tilt the brush so the hand sits below the tip */
	float ux = ex_ * cosT - ey_ * sinT, uy = ey_ * cosT + ex_ * sinT;
	float ux2 = ex_ * cosT + ey_ * sinT, uy2 = ey_ * cosT - ex_ * sinT;
	if (uy2 < uy) {
		ux = ux2;
		uy = uy2;
	}
	float hx = tipX - ux * Lb, hy = tipY - uy * Lb;
	float elx, ely;
	IKElbow(ax, ay, &hx, &hy, L1, L2, -0.2f, 1, &elx, &ely);

	/* the mora */
	DrawMora(hpx, hpy + 1.5f * f, G - 1);

	/* far arm, resting, a bowl of colours in her hand */
	float bwx = hpx + 8 * f, bwy = hpy - 3 * f;
	MaskBegin(pb_round(sx), pb_round(sy + 10 * f));
	MaskCapsule(sx - 0.5f * f, sy + f, sx - 1.5f * f, sy + 11 * f, 1.5f * f, P_SKIN);
	MaskCapsule(sx - 1.5f * f, sy + 11 * f, bwx, bwy, 1.4f * f, P_SKIN);
	MaskCircle(bwx, bwy, 1.7f * f, P_SKIN);
	MaskCommit();

	/* mekhela over the legs, a foot peeping out, then the chador */
	float gx = hpx, gy = (hpy + sy) * 0.5f;
	MaskBegin(pb_round(gx), pb_round(gy));
	MaskEllipse(kx + 4.5f * f, G - 1.2f * f, 2.2f * f, 1.1f * f, 0, P_SKIN);
	float mek[] = { hpx - 5.5f * f, hpy - 2.5f * f, kx + 2.5f * f, ky - 2.8f * f, kx + 3.5f * f,
					ky + 0.5f * f, kx + 3.8f * f, G - 1.5f * f, kx - 5 * f, G - 1.5f * f,
					kx - 4.5f * f, ky + 3.5f * f, hpx - 4 * f, hpy + 3.5f * f };
	ScanPoly(mek, 7, P_RED, true);
	float body[] = { hpx - 5.5f * f, hpy - 2 * f, hpx + 5.5f * f, hpy - 2.5f * f, sx + 5.5f * f,
					 sy + 7 * f, sx + 3.5f * f, sy, sx - 4 * f, sy - 0.5f * f, hpx - 6.5f * f,
					 hpy - 10 * f };
	ScanPoly(body, 6, P_WHITE, true);
	/* woven muga motifs on the mekhela */
	for (int y = 0; y < MASK_DIM; y++)
		for (int x = 0; x < MASK_DIM; x++)
			if (_mask[y * MASK_DIM + x] == P_RED + 1 && y % 3 == 0 && ((x + y) % 6) == 0)
				_mask[y * MASK_DIM + x] = P_MUGA + 1;
	MaskCapsule(sx + 1 * f, sy, sx + 2 * f, sy - 4.5f * f, 1.7f * f, P_SKIN); /* neck */
	MaskCommit();
	/* the mekhela's muga hem and the chador's red border */
	for (int x = pb_round(kx - 5 * f) + 1; x <= pb_round(kx + 3.8f * f) - 1; x++)
		Put(x, pb_round(G - 2.5f * f), P_MUGA);
	Line(pb_round(sx - 2.5f * f), pb_round(sy + f), pb_round(hpx + 4 * f), pb_round(hpy - 3 * f), P_RED, 2);
	DotLine(pb_round(sx - 1.5f * f), pb_round(sy + f), pb_round(hpx + 5 * f), pb_round(hpy - 3 * f), P_MUGA, 2);

	/* head: hair drawn back into a bun, a kopou phul in it */
	float Hx = sx + 2 * f, Hy = sy - 9 * f, r = 6 * f;
	MaskBegin(pb_round(Hx), pb_round(Hy));
	MaskCircle(Hx - 6.5f * f, Hy - 0.5f * f, 3.7f * f, P_CHARCOAL);
	MaskCircle(Hx, Hy, r, P_SKIN);
	int R = (int)r + 1;
	for (int y = -R; y <= R; y++)
		for (int x = -R; x <= R; x++) {
			float u = x / f, v = y / f;
			if (u * u + v * v > 37.0f)
				continue;
			if (v < -3.3f + u * 0.15f || (u < 0.8f && v < 3.0f))
				MaskPut(pb_round(Hx) + x, pb_round(Hy) + y, P_CHARCOAL);
		}
	MaskPut(pb_round(Hx + 6.4f * f), pb_round(Hy + 0.6f * f), P_SKIN); /* nose */
	MaskCommit();
	{
		int ix = pb_round(Hx), iy = pb_round(Hy);
		int u = pb_round(f) > 0 ? pb_round(f) : 1;
		Line(ix + 3 * u, iy, ix + 4 * u, iy, P_INK, 1);			   /* eye, cast down */
		Put(ix + 5 * u, iy - 3 * u, P_RED);						   /* bindi */
		Put(ix + 5 * u, iy + 3 * u, P_RED);						   /* lips */
		Put(ix + u / 2, iy + 3 * u + 1, P_MUGA);				   /* earring */
		Put(ix + u / 2, iy + 3 * u + 2, P_MUGA);
		int bx = pb_round(Hx - 6.5f * f), by = pb_round(Hy - 3.8f * f);
		Put(bx - 1, by, P_PINK);
		Put(bx, by - 1, P_PINK);
		Put(bx + 1, by, P_PINK);
		Put(bx, by, P_RED_DEEP);
		Put(bx + 2, by + 1, P_PINK);
	}

	/* the drawing arm, its red blouse sleeve, a muga bangle, the brush */
	MaskBegin(pb_round(ax), pb_round(ay));
	MaskCapsule(ax, ay, elx, ely, 1.5f * f, P_SKIN);
	MaskCapsule(elx, ely, hx, hy, 1.4f * f, P_SKIN);
	MaskCircle(hx, hy, 1.7f * f, P_SKIN);
	MaskCapsule(ax, ay, ax + (elx - ax) * 0.35f, ay + (ely - ay) * 0.35f, 2.3f * f, P_RED);
	MaskCommit();
	Put(pb_round(elx + (hx - elx) * 0.8f), pb_round(ely + (hy - ely) * 0.8f), P_MUGA);
	Line(pb_round(hx), pb_round(hy), pb_round(tipX), pb_round(tipY), P_MUGA_DEEP, 1);
	Put(pb_round(tipX), pb_round(tipY), P_INK);

	/* the bowl of colours on her lap */
	MaskBegin(pb_round(bwx), pb_round(bwy));
	MaskEllipse(bwx + 1.5f * f, bwy - 0.5f * f, 3.2f * f, 1.4f * f, 0, P_WHITE);
	MaskCommit();
	Put(pb_round(bwx + 0.5f * f), pb_round(bwy - 1.2f * f), P_RED);
	Put(pb_round(bwx + 2.5f * f), pb_round(bwy - 1.2f * f), P_MUGA);

	clipTop = sceneTop;
}

/* ------------------------------------------------------------------ */
/* captions                                                            */
/* ------------------------------------------------------------------ */

static const char* FrameTitle() {
	if (_cut < CUT_SCENE2)
		return "Phulbari  |  the flower garden";
	if (_cut < CUT_FOLLOW)
		return "Prajapati  |  the butterfly";
	if (_cut < CUT_WELCOME)
		return "Follow the butterfly";
	if (_cut < CUT_AURORA)
		return "Welcome";
	if (_cut < CUT_GAMUSA)
		return "Aurora Bordoisila";
	if (_cut < CUT_BAMBOO)
		return "Gamusa";
	if (_cut < CUT_BOY)
		return "Bahbari  |  the bamboo garden";
	if (_cut < CUT_ERASE)
		return "DeodhaiXR  |  apps in the air";
	return "Chitralekha  |  she drew Aniruddha";
}

static void CaptionUpdate() {
	const char* t = FrameTitle();
	if (_cap.title != t) {
		_cap.title = t;
		_cap.lines[0] = _cap.lines[1] = NULL;
		_cap.visible[0] = _cap.visible[1] = 0;
		_cap.version++;
	}
}

/* ------------------------------------------------------------------ */
/* stitched text                                                       */
/* ------------------------------------------------------------------ */

/*
 * A 5x7 thread font, only the letters the frames need. Each row is five
 * bits, bit 4 the leftmost stitch.
 */
typedef struct _glyph_ {
	char c;
	uint8_t rows[7];
} Glyph;

static const Glyph _font[] = {
	{ 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
	{ 'B', { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E } },
	{ 'C', { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E } },
	{ 'D', { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E } },
	{ 'N', { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 } },
	{ 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } },
	{ 'X', { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 } },
	{ 'K', { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 } },
	{ 'W', { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A } },
	{ 'G', { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F } },
	{ 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
	{ 'b', { 0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x1E } },
	{ 'g', { 0x00, 0x00, 0x0F, 0x11, 0x0F, 0x01, 0x0E } },
	{ 'k', { 0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12 } },
	{ 'y', { 0x00, 0x00, 0x11, 0x11, 0x0F, 0x01, 0x0E } },
	{ 'a', { 0x00, 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F } },
	{ 'c', { 0x00, 0x00, 0x0E, 0x10, 0x10, 0x11, 0x0E } },
	{ 'd', { 0x01, 0x01, 0x0D, 0x13, 0x11, 0x11, 0x0F } },
	{ 'e', { 0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E } },
	{ 'f', { 0x06, 0x09, 0x08, 0x1C, 0x08, 0x08, 0x08 } },
	{ 'h', { 0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x11 } },
	{ 'i', { 0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E } },
	{ 'l', { 0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E } },
	{ 'm', { 0x00, 0x00, 0x1A, 0x15, 0x15, 0x11, 0x11 } },
	{ 'n', { 0x00, 0x00, 0x16, 0x19, 0x11, 0x11, 0x11 } },
	{ 'o', { 0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E } },
	{ 'p', { 0x00, 0x00, 0x1E, 0x11, 0x1E, 0x10, 0x10 } },
	{ 'r', { 0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10 } },
	{ 's', { 0x00, 0x00, 0x0E, 0x10, 0x0E, 0x01, 0x1E } },
	{ 't', { 0x08, 0x08, 0x1C, 0x08, 0x08, 0x09, 0x06 } },
	{ 'u', { 0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0D } },
	{ 'v', { 0x00, 0x00, 0x11, 0x11, 0x11, 0x0A, 0x04 } },
	{ 'w', { 0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0A } },
	{ ':', { 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00 } },
};

static const uint8_t* GlyphRows(char c) {
	for (unsigned i = 0; i < sizeof(_font) / sizeof(_font[0]); i++)
		if (_font[i].c == c)
			return _font[i].rows;
	return NULL;
}

#define GLYPH_ADV 6	  /* 5 stitches and a gap */
#define SPACE_ADV 4

typedef struct _text_line_ {
	const char* str;
	int k;		  /* cells per stitch */
	uint8_t fill; /* thread colour */
	int x0, y0;	  /* where it settles, top-left, in cells */
} TextLine;

static TextLine _welcome[1];
static TextLine _aurora[3];
static TextLine _deodhai[2];
static TextLine _chitra[3];

static int TextUnits(const char* str) {
	int w = 0;
	for (const char* p = str; *p; p++)
		w += *p == ' ' ? SPACE_ADV : GLYPH_ADV;
	return w - 1;
}

static int TextLetters(const TextLine* lines, int n) {
	int count = 0;
	for (int l = 0; l < n; l++)
		for (const char* p = lines[l].str; *p; p++)
			if (*p != ' ')
				count++;
	return count;
}

/* centre a line on cy, as big as fits in frac of the width (max kmax) */
static void LayoutLine(TextLine* t, const char* str, int kmax, float frac, int cy, uint8_t fill) {
	int units = TextUnits(str);
	int k = (int)(gw * frac) / units;
	if (k > kmax)
		k = kmax;
	if (k < 1)
		k = 1;
	t->str = str;
	t->k = k;
	t->fill = fill;
	t->x0 = (gw - units * k) / 2;
	t->y0 = cy - (7 * k) / 2;
}

/* keep the flower heads of the last cut: the letters come out of them */
static void SnapshotSources() {
	_nsrc = 0;
	/* sort by x so letters leave flowers roughly in reading order */
	for (int i = 0; i < _nheads; i++) {
		int16_t x = _headX[i], y = _headY[i];
		int j = _nsrc++;
		while (j > 0 && _srcX[j - 1] > x) {
			_srcX[j] = _srcX[j - 1];
			_srcY[j] = _srcY[j - 1];
			j--;
		}
		_srcX[j] = x;
		_srcY[j] = y;
	}
}

static void LetterSource(int n, int total, float* x, float* y) {
	if (_nsrc == 0) {
		*x = gw * (0.1f + 0.8f * n / (float)(total > 1 ? total - 1 : 1));
		*y = (float)(horizon + 4);
		return;
	}
	int i = (int)((n + 0.5f) * _nsrc / total);
	if (i >= _nsrc)
		i = _nsrc - 1;
	*x = _srcX[i];
	*y = _srcY[i];
}

enum { PASS_HALO, PASS_SHADOW, PASS_FILL };

/*
 * DrawLetter -- one letter centred on cx, cy at sc cells per stitch. A
 * settled letter (shown at its full size k) sits on its own patch of
 * clean cloth, one advance wide, so the patches of a word join into a
 * ribbon and no stem or stroke runs through the letters.
 */
static void DrawLetter(const uint8_t* rows, float cx, float cy, float sc, int k, uint8_t fill,
					   int pass, bool settled, float unravel = 0, uint32_t seed = 0) {
	if (pass == PASS_HALO) {
		if (settled && unravel < 0.5f)
			FillRect(pb_round(cx - 2.5f * k) - 2, pb_round(cy - 3.5f * k) - 2, GLYPH_ADV * k + 3,
					 7 * k + 4, P_PAPER);
		return;
	}
	if (pass == PASS_SHADOW && (k < 2 || !settled))
		return;
	int b = pb_round(sc);
	if (b < 1)
		b = 1;
	for (int fy = 0; fy < 7; fy++) {
		for (int fx = 0; fx < 5; fx++) {
			if (!(rows[fy] & (0x10 >> fx)))
				continue;
			int bx = pb_round(cx - 2.5f * sc + fx * sc);
			int by = pb_round(cy - 3.5f * sc + fy * sc);
			if (unravel > 0) {
				/* coming unstitched: each stitch lets go at its own
				 * moment and drops away as a loose thread */
				float h = (pb_hash(seed * 61u + fy * 7u + fx) & 1023) / 1024.0f;
				if (h < unravel) {
					float age = (unravel - h) * 3.0f;
					if (pass == PASS_FILL && age < 2.0f)
						Put(bx + b / 2, by + b + pb_round(age * 3.0f * k), P_INK_SOFT);
					continue;
				}
			}
			if (pass == PASS_SHADOW) {
				FillRect(bx + 1, by + 1, b, b, P_INK);
			} else {
				FillRect(bx, by, b, b, fill);
				if (b >= 3)
					Put(bx + b / 2, by + b / 2, P_MUGA);
			}
		}
	}
}

/*
 * DrawText -- letters rise out of flower heads one after another and
 * arc to their place, growing as they come.
 * start -- cut the first letter leaves its flower
 * stagger -- cuts between letters, in halves
 * breakAt -- cut the first letter breaks into butterflies, or -1
 */
static void DrawText(const TextLine* lines, int nlines, int start, int staggerHalves, int breakAt) {
	int total = TextLetters(lines, nlines);
	for (int pass = PASS_HALO; pass <= PASS_FILL; pass++) {
		int n = 0;
		for (int l = 0; l < nlines; l++) {
			const TextLine* t = &lines[l];
			int k = t->k;
			int ox = 0;
			for (const char* p = t->str; *p; p++) {
				if (*p == ' ') {
					ox += SPACE_ADV;
					continue;
				}
				const uint8_t* rows = GlyphRows(*p);
				int letter = n++;
				int adv = ox;
				ox += GLYPH_ADV;
				if (!rows)
					continue;
				int t0 = start + (letter * staggerHalves) / 2;
				if ((int)_cut < t0)
					continue; /* still inside its flower */
				if (breakAt >= 0 && (int)_cut >= breakAt + letter)
					continue; /* flown off as butterflies */
				float u = (float)((int)_cut - t0) / EMERGE_CUTS;
				if (u > 1)
					u = 1;
				float e = u * u * (3 - 2 * u);
				float tcx = t->x0 + adv * k + 2.5f * k;
				float tcy = t->y0 + 3.5f * k;
				float scx, scy;
				LetterSource(letter, total, &scx, &scy);
				float cx = scx + (tcx - scx) * e;
				float cy = scy + (tcy - scy) * e;
				/* arc up out of the flower */
				float lift = (scy - tcy) * 0.35f;
				if (lift < 6)
					lift = 6;
				cy -= pb_sin(u * PB_PI) * lift;
				/* the word shudders just before it breaks */
				if (breakAt >= 0 && (int)_cut >= breakAt - 2)
					cx += ((_cut + letter) & 1) ? 1.0f : -1.0f;
				DrawLetter(rows, cx, cy, k * (0.12f + 0.88f * e), k, t->fill, pass, u >= 1);
			}
		}
	}
}

/* where the bird is, in cells, t cuts into its flight (right to left) */
static float BirdX(float t) {
	float x0 = gw + 40.0f, x1 = -44.0f;
	return x0 + (x1 - x0) * (t / BIRD_CUTS);
}

/*
 * DrawTextBehindBird -- each letter pops into place just after the bird
 * has flown over it, so the words are drawn in its wake
 */
static void DrawTextBehindBird(const TextLine* lines, int nlines) {
	float speed = (BirdX(0) - BirdX(BIRD_CUTS)) / BIRD_CUTS;
	for (int pass = PASS_HALO; pass <= PASS_FILL; pass++) {
		for (int l = 0; l < nlines; l++) {
			const TextLine* t = &lines[l];
			int k = t->k;
			int ox = 0;
			for (const char* p = t->str; *p; p++) {
				if (*p == ' ') {
					ox += SPACE_ADV;
					continue;
				}
				const uint8_t* rows = GlyphRows(*p);
				float cx = t->x0 + ox * k + 2.5f * k;
				float cy = t->y0 + 3.5f * k;
				ox += GLYPH_ADV;
				if (!rows)
					continue;
				/* the cut the bird's tail clears this letter */
				float t0 = CUT_BIRD + (BirdX(0) - cx + 10.0f) / speed;
				float u = ((float)_cut - t0) / 3.0f;
				if (u < 0)
					continue;
				if (u > 1)
					u = 1;
				/* drops a little into place as it grows */
				DrawLetter(rows, cx, cy - (1 - u) * 3, k * (0.35f + 0.65f * u), k, t->fill, pass,
						   u >= 1);
			}
		}
	}
}

/*
 * DrawTextTimed -- settled text whose letters either pop into place one
 * after another (stagger cuts apart from start) or, when unravel is
 * set, come unstitched one after another from start
 */
static void DrawTextTimed(const TextLine* lines, int nlines, int start, float stagger, bool unravel) {
	for (int pass = PASS_HALO; pass <= PASS_FILL; pass++) {
		int letter = 0;
		for (int l = 0; l < nlines; l++) {
			const TextLine* t = &lines[l];
			int k = t->k;
			int ox = 0;
			for (const char* p = t->str; *p; p++) {
				if (*p == ' ') {
					ox += SPACE_ADV;
					continue;
				}
				const uint8_t* rows = GlyphRows(*p);
				float cx = t->x0 + ox * k + 2.5f * k;
				float cy = t->y0 + 3.5f * k;
				ox += GLYPH_ADV;
				int n = letter++;
				if (!rows)
					continue;
				float t0 = start + n * stagger;
				float u = ((float)_cut - t0) / 3.0f;
				if (unravel) {
					if (u >= 1)
						continue;
					DrawLetter(rows, cx, cy, (float)k, k, t->fill, pass, true, u > 0 ? u : 0,
							   (uint32_t)n + 1);
					continue;
				}
				if (u < 0)
					continue;
				if (u > 1)
					u = 1;
				DrawLetter(rows, cx, cy - (1 - u) * 3, k * (0.35f + 0.65f * u), k, t->fill, pass, u >= 1);
			}
		}
	}
}

/* the swarm "Welcome" breaks into: three butterflies per letter */
static void DrawSwarm(const TextLine* t) {
	static const float beat[4] = { 1.0f, 0.55f, 0.18f, 0.55f };
	int k = t->k;
	int letter = 0;
	int ox = 0;
	for (const char* p = t->str; *p; p++) {
		if (*p == ' ') {
			ox += SPACE_ADV;
			continue;
		}
		int born = CUT_BREAK + letter;
		float lcx = t->x0 + ox * k + 2.5f * k;
		float lcy = t->y0 + 3.5f * k;
		for (int j = 0; j < 3 && (int)_cut >= born; j++) {
			int age = (int)_cut - born;
			uint32_t h = pb_hash((uint32_t)letter * 7u + j * 131u + 17u);
			/* any direction, a little more often upwards */
			float ang = (h % 628) / 100.0f;
			if (pb_sin(ang) > 0.3f && (h & 0x100))
				ang = -ang;
			float v = 1.2f + ((h >> 10) % 100) / 60.0f;
			float dist = v * age * (1 + age * 0.06f);
			float sx = lcx + (j - 1) * 1.5f * k + pb_cos(ang) * dist;
			float sy = lcy + (j == 1 ? -k : k) + pb_sin(ang) * dist;
			sy += pb_sin(age * 1.3f + j * 2.0f + letter) * 1.5f;
			float size = k * 1.8f * (1 + age * 0.02f);
			if (sx < -size || sx > gw + size || sy < sceneTop - size || sy > sceneBot + size)
				continue;
			DrawButterfly(sx, sy, size, beat[(age + j + letter) & 3]);
		}
		letter++;
		ox += GLYPH_ADV;
	}
}

/*
 * Frame 2 opens on a staged close shot: the hard cut hides the change,
 * so the foreground is cleared and the flower the butterfly will land on
 * is planted mid-frame beside the path.
 */
static void StageScene2() {
	for (int i = 0; i < NUM_ITEMS; i++) {
		Item* it = &_items[i];
		if (it->z - _camz < 1.45f)
			Spawn(it, it->z + GARDEN_DEPTH, i < NUM_FLOWERS);
	}
	_hero = 0;
	Item* it = &_items[0];
	it->type = T_PHUL;
	it->z = _camz + 1.55f;
	it->x = -(PATH_HALF + 0.3f);
	it->h = 0.62f;
	it->rs = 0.16f;
	it->leafy = 1;
	it->phase = 1.0f;

	float F = gw * 0.5f;
	float d = it->z - _camz;
	float s = F / d;
	_heroX = gw * 0.5f + (it->x - _camx) * s;
	_heroY = horizon + CAM_H * s - it->h * s;
	_heroR = it->rs * s;
}

static void DrawCut() {
	/* the strip below the scene never changes; clear just the scene */
	memset(_cur + sceneTop * gw, P_PAPER, (size_t)(sceneBot - sceneTop) * gw);
	clipTop = sceneTop;
	clipBot = sceneBot;
	_nheads = 0;

	if (_cut >= CUT_CHITRA) {
		/* frame 9: Chitralekha draws Aniruddha, then the library's name */
		DrawChitralekhaScene();
		if (_cut >= CUT_CHTEXT)
			DrawTextTimed(_chitra, 3, CUT_CHTEXT, 0.5f, false);
		CaptionUpdate();
		return;
	}

	if (_cut >= CUT_BAMBOO) {
		/* frame 7: the garden is gone; bamboo is drawn on bare cloth */
		DrawBambooGarden(_cut >= CUT_ERASE ? (int)(_cut - CUT_ERASE) : -1);
		/* frame 8: the words come unstitched, and a boy runs in */
		if (_cut >= CUT_BOY) {
			if (_cut < CUT_BOY + UNRAVEL_CUTS)
				DrawTextTimed(_deodhai, 2, CUT_BOY, 0.33f, true);
			if (_cut < CUT_ERASE)
				DrawBoyScene();
		} else if (_cut >= CUT_BIRD) {
			DrawTextBehindBird(_deodhai, 2);
			int t = (int)(_cut - CUT_BIRD);
			if (t <= BIRD_CUTS) {
				static const float flap[4] = { 0.0f, 0.55f, 1.0f, 0.55f };
				/* high in the open sky, clear of the bamboo leaves */
				float y = sceneTop + (horizon - sceneTop) * 0.42f + pb_sin(t * 0.6f) * 2.0f;
				DrawBird(BirdX((float)t), y, 1.45f, flap[t & 3]);
			}
		}
		CaptionUpdate();
		return;
	}

	DrawSky();
	DrawGround();

	SortItems();
	bool scene2 = _cut >= CUT_SCENE2 && _cut < CUT_FOLLOW;
	for (int n = 0; n < NUM_ITEMS; n++) {
		int i = _order[n];
		float d = _items[i].z - _camz;
		if (d < 0.5f || d > GARDEN_DEPTH + 1.0f)
			continue;
		float dy = 0;
		bool still = false;
		if (scene2 && i == _hero) {
			/* the flower dips as the butterfly lands, springs as it leaves */
			still = _cut >= CUT_LAND && _cut < CUT_TAKEOFF + 2;
			if (_cut == CUT_LAND || _cut == CUT_LAND + 1)
				dy = 1;
			else if (_cut == CUT_TAKEOFF)
				dy = -1;
		}
		DrawFlower(i, &_items[i], d, dy, still);
	}

	static const float beat[4] = { 1.0f, 0.55f, 0.18f, 0.55f };
	float s0 = _heroR * 2.4f;
	if (s0 > MASK_DIM / 2 - 4)
		s0 = MASK_DIM / 2 - 4;

	/* frame 2: lands, holds a second, takes off */
	if (scene2 && _cut >= CUT_LAND) {
		float bx = _heroX, by = _heroY - _heroR * 0.35f;
		if (_cut < CUT_TAKEOFF) {
			float k = ((_cut - CUT_LAND) / 2) & 1 ? 0.85f : 1.0f;
			DrawButterfly(bx, by, s0, k);
		} else {
			float u = (float)(_cut - CUT_TAKEOFF) / (float)FLY_CUTS;
			float tx = gw * 0.58f, ty = sceneTop + (horizon - sceneTop) * 0.55f;
			float qx = bx + gw * 0.06f, qy = sceneTop + (by - sceneTop) * 0.25f;
			float iu = 1 - u;
			float px = iu * iu * bx + 2 * iu * u * qx + u * u * tx;
			float py = iu * iu * by + 2 * iu * u * qy + u * u * ty;
			py += pb_sin(u * 4 * PB_PI) * s0 * 0.22f * iu;
			DrawButterfly(px, py, s0 * (1 - 0.6f * u), beat[(_cut - CUT_TAKEOFF) & 3]);
		}
	}

	/* frame 3: followed for two seconds, then it darts off screen */
	if (_cut >= CUT_FOLLOW && _cut < CUT_FOLLOW_END) {
		int t = (int)(_cut - CUT_FOLLOW);
		float s = gw * 0.09f;
		if (s > 26)
			s = 26;
		float px = gw * 0.5f + pb_sin(t * 0.45f + 1.0f) * gw * 0.05f;
		float py = sceneTop + (horizon - sceneTop) * 0.72f + pb_sin(t * 1.1f) * 2.0f;
		if (t >= FOLLOW_CUTS) {
			float u = (float)(t - FOLLOW_CUTS + 1) / EXIT_CUTS;
			px += u * u * gw * 0.6f;
			py -= u * u * (py - sceneTop + s * 2);
			s *= 1 + u * 0.6f;
		}
		DrawButterfly(px, py, s, beat[t & 3]);
	}

	/* "Welcome" rises out of the flowers, then breaks into butterflies */
	if (_cut >= CUT_WELCOME && _cut < CUT_AURORA) {
		DrawText(_welcome, 1, CUT_WELCOME, 2, CUT_BREAK);
		if (_cut >= CUT_BREAK)
			DrawSwarm(&_welcome[0]);
	}

	/* frame 5: the kernel's name */
	if (_cut >= CUT_AURORA)
		DrawText(_aurora, 3, CUT_AURORA, 1, -1);

	/* frame 6: the gamusa sweeps it all away */
	if (_cut >= CUT_GAMUSA)
		DrawGamusaSweep();

	CaptionUpdate();
}

/* ------------------------------------------------------------------ */
/* public                                                              */
/* ------------------------------------------------------------------ */

static void* PbAlloc(size_t sz) {
	void* p = malloc(sz);
	if (p)
		_heapBytes += sz;
	return p;
}

bool PbInit(int w, int h) {
	PbShutdown();
	pxW = w;
	pxH = h;
	S = (w + 128) / 256;
	if (S < 3)
		S = 3;
	if (S > PB_MAX_CELL)
		S = PB_MAX_CELL;
	gw = w / S;
	gh = h / S;
	int capRows = (PB_CAPTION_PX + S - 1) / S;
	if (gw < 64 || gh < capRows + 40)
		return false;

	sceneTop = 0;
	capBot = gh;
	capTop = capBot - capRows;
	sceneBot = capTop;
	horizon = sceneTop + (sceneBot - sceneTop) * 36 / 100;

	_cur = (uint8_t*)PbAlloc((size_t)gw * gh);
	_shown = (uint8_t*)PbAlloc((size_t)gw * gh);
	_mask = (uint8_t*)PbAlloc(MASK_DIM * MASK_DIM);
	if (!_cur || !_shown || !_mask) {
		PbShutdown();
		return false;
	}
	memset(_mask, 0, MASK_DIM * MASK_DIM);
	BuildSwatches();

	/* paper everywhere, the strip below drawn once into both grids */
	memset(_cur, P_PAPER, (size_t)gw * gh);
	clipTop = 0;
	clipBot = gh;
	/* a fine shade line between scene and caption */
	for (int x = 0; x < gw; x++)
		if (x & 1)
			Put(x, capTop, P_SHADE);
	memcpy(_shown, _cur, (size_t)gw * gh);

	memset(&_cap, 0, sizeof(_cap));
	/* the text settles in the centre of the screen */
	int mid = (sceneTop + sceneBot) / 2;
	LayoutLine(&_welcome[0], "Welcome", 5, 0.72f, mid, P_RED);
	/* three lines so the subtitle can keep two-cell stitches */
	LayoutLine(&_aurora[0], "Aurora Bordoisila:", 3, 0.86f, 0, P_RED);
	LayoutLine(&_aurora[1], "Kernel written", _aurora[0].k, 0.86f, 0, P_INK);
	LayoutLine(&_aurora[2], "from scratch", _aurora[0].k, 0.86f, 0, P_INK);
	{
		int gap = _aurora[0].k * 3;
		int h0 = 7 * _aurora[0].k, h1 = 7 * _aurora[1].k, h2 = 7 * _aurora[2].k;
		int top = mid - (h0 + gap + h1 + gap + h2) / 2;
		_aurora[0].y0 = top;
		_aurora[1].y0 = top + h0 + gap;
		_aurora[2].y0 = _aurora[1].y0 + h1 + gap;
	}
	/* frame 7, written behind the bird */
	LayoutLine(&_deodhai[0], "DeodhaiXR", 4, 0.75f, 0, P_RED);
	LayoutLine(&_deodhai[1], "XR Native Compositor", 2, 0.95f, 0, P_INK);
	{
		int h0 = 7 * _deodhai[0].k, gap = _deodhai[0].k * 3;
		int top = mid - (h0 + gap + 7 * _deodhai[1].k) / 2;
		_deodhai[0].y0 = top;
		_deodhai[1].y0 = top + h0 + gap;
	}
	/* frame 9: centred in the open cloth above Chitralekha and her easel */
	{
		float sw = gw / 255.0f, sh = (sceneBot - sceneTop) / 160.0f;
		_fs = sw < sh ? sw : sh;
		int figTop = sceneBot - pb_round(4 * _fs) - pb_round(79 * _fs);
		int room = figTop - 3 - (sceneTop + 2);
		int k0 = 3, k1 = 2;
		for (;;) {
			LayoutLine(&_chitra[0], "Chitralekha", k0, 0.8f, 0, P_RED);
			LayoutLine(&_chitra[1], "Graphics and Widget", k1, 0.92f, 0, P_INK);
			LayoutLine(&_chitra[2], "Library for Apps", _chitra[1].k, 0.92f, 0, P_INK);
			int h = 7 * _chitra[0].k + 3 * _chitra[0].k + 7 * _chitra[1].k * 2 + 3 * _chitra[1].k;
			if (h <= room || (k0 == 1 && k1 == 1))
				break;
			if (k0 > k1)
				k0--;
			else
				k1--;
		}
		int h0 = 7 * _chitra[0].k, g0 = 3 * _chitra[0].k, h1 = 7 * _chitra[1].k, g1 = 3 * _chitra[1].k;
		int top = sceneTop + 2 + (room - (h0 + g0 + h1 + g1 + h1)) / 2;
		_chitra[0].y0 = top;
		_chitra[1].y0 = top + h0 + g0;
		_chitra[2].y0 = _chitra[1].y0 + h1 + g1;
	}
	_cut = 0;
	_cutTotal = 0;
	WorldReset();
	DrawCut();
	return true;
}

void PbShutdown() {
	free(_cur);
	free(_shown);
	free(_mask);
	_cur = _shown = _mask = NULL;
	_heapBytes = 0;
}

void PbStep() {
	if (!_cur)
		return;
	_cut++;
	_cutTotal++;
	if (_cut >= CUT_LOOP) {
		/* hard cut back to the start of frame 1 */
		_cut = 0;
		WorldReset();
	} else if (_cut < CUT_SCENE2) {
		/* frame 1: the camera walks slowly into the garden */
		WorldAdvance(WALK_SPEED);
	} else if (_cut == CUT_SCENE2) {
		/* frame 2: cut to a closer shot, then the camera holds */
		WorldAdvance(0.9f);
		StageScene2();
	} else if (_cut >= CUT_FOLLOW && _cut < CUT_FOLLOW_END) {
		/* frame 3 opens on a hard cut: clear the near flowers, so the
		 * shot stays open for the text that comes later */
		if (_cut == CUT_FOLLOW)
			for (int i = 0; i < NUM_ITEMS; i++)
				if (_items[i].z - _camz < 2.3f)
					Spawn(&_items[i], _items[i].z + GARDEN_DEPTH, i < NUM_FLOWERS);
		/* frame 3: run after the butterfly, panning with its weave,
		 * and ease to a stop as it darts away */
		int t = (int)(_cut - CUT_FOLLOW);
		float speed = FOLLOW_SPEED;
		if (t >= FOLLOW_CUTS)
			speed *= 1.0f - (float)(t - FOLLOW_CUTS + 1) / EXIT_CUTS;
		WorldAdvance(speed);
		_camx = 0.5f * pb_sin(t * 0.45f);
	}
	DrawCut();
	/* the letters come out of the flowers that were just drawn */
	if (_cut == CUT_WELCOME - 1 || _cut == CUT_AURORA - 1)
		SnapshotSources();
}

bool PbRender(uint32_t* dst, int pitch, bool force, int* rx, int* ry, int* rw, int* rh) {
	if (!_cur || !dst)
		return false;
	int minx = gw, miny = gh, maxx = -1, maxy = -1;
	for (int y = 0; y < gh; y++) {
		const uint8_t* a = _cur + y * gw;
		const uint8_t* b = _shown + y * gw;
		if (!force && memcmp(a, b, gw) == 0)
			continue;
		for (int x = 0; x < gw; x++) {
			if (!force && a[x] == b[x])
				continue;
			PutSwatch(dst, pitch, x, y, a[x]);
			if (x < minx)
				minx = x;
			if (x > maxx)
				maxx = x;
			if (y < miny)
				miny = y;
			if (y > maxy)
				maxy = y;
		}
	}
	/* what is drawn is now what is shown; the old grid is reused */
	uint8_t* t = _shown;
	_shown = _cur;
	_cur = t;
	memcpy(_cur, _shown, (size_t)sceneTop * gw); /* keep bands in both */
	memcpy(_cur + sceneBot * gw, _shown + sceneBot * gw, (size_t)(gh - sceneBot) * gw);

	if (maxx < 0)
		return false;
	if (rx)
		*rx = minx * S;
	if (ry)
		*ry = miny * S;
	if (rw)
		*rw = (maxx - minx + 1) * S;
	if (rh)
		*rh = (maxy - miny + 1) * S;
	return true;
}

void PbRepaint(uint32_t* dst, int pitch) {
	if (!_shown || !dst)
		return;
	for (int y = 0; y < gh; y++)
		for (int x = 0; x < gw; x++)
			PutSwatch(dst, pitch, x, y, _shown[y * gw + x]);
}

void PbRenderCaptionBackground(uint32_t* dst, int pitch) {
	if (!_shown || !dst)
		return;
	for (int y = capTop + 1; y < capBot; y++)
		for (int x = 0; x < gw; x++)
			PutSwatch(dst, pitch, x, y, _shown[y * gw + x]);
}

void PbCaptionRect(int* x, int* y, int* w, int* h) {
	if (x)
		*x = 0;
	if (y)
		*y = (capTop + 1) * S;
	if (w)
		*w = gw * S;
	if (h)
		*h = (capBot - capTop - 1) * S;
}

const PbCaption* PbGetCaption() { return &_cap; }

size_t PbHeapBytes() { return _heapBytes; }

uint32_t PbCutNumber() { return _cutTotal; }
