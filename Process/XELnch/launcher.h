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
 * launcher.h -- XELnch internal header (Gamusa x Kinetic launcher)
 *
 * Memory model (everything is bounded and freed eagerly):
 *   - XlApp array: exactly appCount entries (~330 bytes each)
 *   - icons: decoded once, already scaled to the size they are drawn at;
 *     BMP file buffers are freed right after decoding
 *   - tile cache: one opaque RGB565 bitmap per *pinned* tile (half the
 *     size of ARGB), rendered through a transient ARGB scratch, freed
 *     on unpin
 *   - layout: fixed tables of uint8_t indices (no per-frame allocation)
 *
 * Time model:
 *   - hit testing: O(1) through the cell occupancy map
 *   - app lookup by hash (live tiles, layout file): O(log n) binary search
 *   - layout (pack): O(n) amortised, only on pin/unpin/drag/resize
 *   - sorting (all apps / search): O(n log n), only when the list changes
 *   - painting: O(pixels in the dirty rect) -- tiles are cached bitmaps
 */

#ifndef __LAUNCHER_H__
#define __LAUNCHER_H__

#include <stdint.h>
#include <chitralekha.h>
#include <widgets/base.h>
#include <widgets/window.h>
#include <font.h>
#include <string.h>

/* XEClib's memcpy takes a non-const source; keep const-correct callers */
static inline void* XlMemcpy(void* d, const void* s, size_t n) {
	return memcpy(d, (void*)s, n);
}

/** Init Request msgs **/
#define INIT_REQUEST_PW_DOWN   "init.request.powerdown"
#define INIT_REQUEST_PW_REBOOT "init.request.reboot"

typedef struct _init_request_msg_ {
	char message[60];
	uint16_t fromProcessId;
	uint16_t toProcessId;
} InitRequestMsg;

/* ---------------------------------------------------------------------
 * limits
 * -------------------------------------------------------------------*/
#define XL_MAX_APPS		 64
#define XL_COLS			 6
#define XL_MAX_ROWS		 96
#define XL_TITLE_MAX	 40
#define XL_PATH_MAX		 48
#define XL_PARAM_MAX	 32
#define XL_LIVE_TEXT_MAX 40
#define XL_QUERY_MAX	 48
#define XL_NONE			 0xFF

#define XL_CONFIG_FILE "/lncher.cnf"
#define XL_LAYOUT_FILE "/lnchlay.cnf"

/* ---------------------------------------------------------------------
 * Gamusa palette (ARGB). Gamusa is white cotton / pat-silk ivory with
 * red borders and red woven phul; muga silk brings the gold. Tiles only
 * use these hues so the start screen reads as one woven cloth.
 * -------------------------------------------------------------------*/
#define XL_BG		  0xFFFAF7F0u /* cotton                        */
#define XL_INK		  0xFF231F1Cu /* black thread                  */
#define XL_INK_SOFT	  0xFF6B6259u /* secondary text (5.6:1 on bg)  */
#define XL_RED		  0xFFC8102Eu /* gamusa red                    */
#define XL_RED_DEEP	  0xFF8E1A22u /* dyed-deep red                 */
#define XL_MUGA		  0xFFC99A2Eu /* muga silk gold (ink text)     */
#define XL_MUGA_DEEP  0xFF7A5A12u /* muga, shaded (white text)     */
#define XL_CHARCOAL	  0xFF2B2622u /* ink tile                      */
#define XL_IVORY	  0xFFEDE3CFu /* pat silk ivory (ink text)     */
#define XL_IVORY_EDGE 0xFFDCCFB4u
#define XL_WHITE	  0xFFFFFFFFu
/* subtle weave: red at ~14% on cotton, borders at ~45% */
#define XL_WEAVE	  0xFFF3DAD6u
#define XL_WEAVE_LINE 0xFFE6A9AEu

enum { XL_COL_RED, XL_COL_RED_DEEP, XL_COL_MUGA, XL_COL_MUGA_DEEP, XL_COL_INK, XL_COL_IVORY, XL_COL_COUNT };

/* tile sizes, in grid cells: S 1x1, M 2x2, W 2x1, L 4x2 */
enum { XL_TILE_S, XL_TILE_M, XL_TILE_W, XL_TILE_L, XL_TILE_SIZE_COUNT };

/* ---------------------------------------------------------------------
 * data
 * -------------------------------------------------------------------*/
typedef struct _xl_icon_ {
	uint16_t w;
	uint16_t h;
	uint32_t* px; /* ARGB, non premultiplied, w*h */
} XlIcon;

typedef struct _xl_live_ {
	uint8_t valid;
	uint8_t layout;
	uint16_t badge;
	uint16_t progress; /* 0..1000, 0xFFFF = none */
	char line1[XL_LIVE_TEXT_MAX];
	char line2[XL_LIVE_TEXT_MAX];
} XlLive;

typedef struct _xl_app_ {
	char title[XL_TITLE_MAX];
	char path[XL_PATH_MAX];
	char param[XL_PARAM_MAX];
	char icon[XL_PATH_MAX];
	uint32_t titleHash;
	uint32_t pathHash;
	uint8_t size;
	uint8_t color;
	uint8_t pinned;
	uint8_t defPinned;
	int16_t col; /* cell position while pinned */
	int16_t row;
	XlIcon tileIcon; /* only while pinned */
	XlIcon listIcon; /* always (small) */
	uint16_t* cache; /* rendered tile (RGB565), only while pinned */
	uint16_t cacheW;
	uint16_t cacheH;
	uint8_t cacheDirty;
	XlLive live;
} XlApp;

typedef struct _xl_rect_ {
	int x, y, w, h;
} XlRect;

typedef struct _xl_metrics_ {
	int panelW, panelH;
	int cell, gap, pitch; /* pitch = cell + gap */
	int padX;
	XlRect title;
	XlRect search;
	XlRect section;
	XlRect allApps; /* "all apps" / "pinned" toggle */
	XlRect power;
	XlRect restart;
	XlRect view; /* tile grid / list viewport */
	XlRect footer;
	int listRowH;
	int listIcon;
	int titleSize, labelSize, textSize, smallSize;
} XlMetrics;

enum { XL_VIEW_TILES, XL_VIEW_LIST };

enum {
	XL_HIT_NONE,
	XL_HIT_TILE,
	XL_HIT_TILE_PIN, /* the unpin badge on a hovered tile */
	XL_HIT_ROW,
	XL_HIT_ROW_PIN,
	XL_HIT_POWER,
	XL_HIT_RESTART,
	XL_HIT_TOGGLE
};

typedef struct _xl_hit_ {
	uint8_t kind;
	uint8_t index; /* app index for tile/row hits */
} XlHit;

typedef struct _xl_launcher_ {
	XlApp* apps;
	int appCount;

	/* pinned tiles, in user order */
	uint8_t pinOrder[XL_MAX_APPS];
	int pinCount;

	/* lookup indexes, sorted by hash -> binary search */
	uint8_t byPath[XL_MAX_APPS];
	uint8_t byTitle[XL_MAX_APPS];

	/* alphabetical list + search results */
	uint8_t listOrder[XL_MAX_APPS];
	uint8_t results[XL_MAX_APPS];
	int resultCount;
	char query[XL_QUERY_MAX];
	int queryLen;

	/* cell -> app index (XL_NONE when empty) */
	uint8_t occ[XL_MAX_ROWS][XL_COLS];
	int rowsUsed;

	int view;
	int scrollTiles; /* in pixels */
	int scrollList;

	/* input state */
	XlHit hover;
	XlHit press;
	bool mouseDown;
	bool dragging;
	int pressX, pressY;
	int dragX, dragY; /* top left of the dragged tile, view coords */
	int dragOffX, dragOffY;
	uint8_t dragTarget;

	bool animating;
	bool layoutDirty;

	XlIcon powerIcon;
	XlIcon restartIcon;
	XlMetrics m;
	ChFont* font;
	ChCanvas* canv; /* window canvas */
} XlLauncher;

extern XlLauncher xlnch;

/* ---------------------------------------------------------------------
 * gamusa.cpp -- palette, primitives and the subtle woven patterns
 * -------------------------------------------------------------------*/
extern uint32_t XlBlend(uint32_t dst, uint32_t src, uint32_t a255);
extern uint32_t XlTileColor(int colorIdx);
extern uint32_t XlTileTextColor(int colorIdx);
extern void XlFill(ChCanvas* c, int x, int y, int w, int h, uint32_t col, const XlRect* clip);
extern void XlFrame(ChCanvas* c, int x, int y, int w, int h, int t, uint32_t col, const XlRect* clip);
extern void XlGamusaPari(ChCanvas* c, int x, int y, int w);
extern void XlGamusaPhulBand(ChCanvas* c, int x, int y, int w);
extern void XlGamusaBorders(ChCanvas* c, int h, int w);
extern void XlDrawMotif(ChCanvas* c, int motif, int x, int y, int scale, uint32_t col, const XlRect* clip);
enum { XL_MOTIF_PHUL, XL_MOTIF_LOZENGE };

/* ---------------------------------------------------------------------
 * xlicon.cpp -- BMP decode + box-filter downscale
 * -------------------------------------------------------------------*/
extern bool XlIconLoad(const char* path, int maxW, int maxH, XlIcon* out);
extern void XlIconFree(XlIcon* ico);
extern void XlIconTint(XlIcon* ico, uint32_t rgb);
extern void XlIconDraw(uint32_t* dst, int dstW, int dstH, const XlIcon* ico, int x, int y, const XlRect* clip);

/* ---------------------------------------------------------------------
 * tiles.cpp -- tile cache rendering and blitting (incl. 3D turnstile)
 * -------------------------------------------------------------------*/
extern void XlTileCells(int size, int* cw, int* ch);
extern void XlTilePixels(int size, int* pw, int* ph);
extern int XlTileIconBox(int size);
extern bool XlTileEnsure(XlApp* a);
extern void XlTileRelease(XlApp* a);
extern void XlTileRender(XlApp* a);
extern void XlTileBlit(ChCanvas* c, XlApp* a, int x, int y, const XlRect* clip);
extern void XlTileBlitScaled(ChCanvas* c, XlApp* a, int x, int y, int pct, const XlRect* clip);
extern void XlTileBlitTurn(ChCanvas* c,
						   XlApp* a,
						   int x,
						   int y,
						   int pivotX,
						   int angle10,
						   uint32_t fadeTo,
						   const XlRect* clip);
extern void XlTileOverlay(ChCanvas* c, XlApp* a, int x, int y, bool hover, const XlRect* clip);
extern void XlTilePinBadgeRect(XlApp* a, int x, int y, XlRect* r);
extern void XlFitText(ChFont* f, const char* src, int maxW, char* dst, int dstSize);

/* ---------------------------------------------------------------------
 * layout.cpp -- packing, hit testing, pin/unpin, drag reorder, persistence
 * -------------------------------------------------------------------*/
extern void XlIndexBuild();
extern int XlFindByTitleHash(uint32_t h, const char* title);
extern int XlFindByPathHashFirst(uint32_t h);
extern void XlLayoutPack();
extern void XlLayoutTileRect(XlApp* a, XlRect* r); /* view coords incl. scroll */
extern int XlLayoutContentHeight();
extern int XlLayoutTileAt(int vx, int vy);		  /* app idx or -1 */
extern int XlLayoutSlotAt(int vx, int vy);		  /* pin order slot for a drop */
extern bool XlLayoutMove(uint8_t app, int slot); /* true if order changed */
extern bool XlPin(uint8_t app);
extern bool XlUnpin(uint8_t app);
extern void XlLayoutLoad();
extern void XlLayoutSave();

/* ---------------------------------------------------------------------
 * applist.cpp -- all apps list + search
 * -------------------------------------------------------------------*/
extern void XlListBuild();
extern void XlSearchUpdate();
extern int XlListCount();
extern int XlListAt(int i);
extern void XlListPaint(ChCanvas* c);
extern XlHit XlListHit(int vx, int vy);

/* ---------------------------------------------------------------------
 * xlui.cpp -- metrics and painting
 * -------------------------------------------------------------------*/
extern void XlMetricsInit(int screenW, int screenH);
extern void XlPaintAll(bool withContent);
extern void XlPaintHeader();
extern void XlPaintSearch();
extern void XlPaintSection();
extern void XlPaintView();
extern void XlPaintTileAt(uint8_t app);
extern XlHit XlHitTest(int lx, int ly);
extern void XlViewClear();
/* implemented by the host: pushes a canvas rect to the compositor */
extern void XlPresent(int x, int y, int w, int h);

/* ---------------------------------------------------------------------
 * anim.cpp -- Metro turnstile
 * -------------------------------------------------------------------*/
enum { XL_ANIM_ENTER, XL_ANIM_EXIT };
extern void XlAnimTiles(int mode, int launchApp);

/* ---------------------------------------------------------------------
 * lconfig.cpp
 * -------------------------------------------------------------------*/
extern int XlConfigLoad(const char* path);

/* ---------------------------------------------------------------------
 * livetile.cpp
 * -------------------------------------------------------------------*/
extern void XlLiveInit();
extern void XlLiveHandle(PostEvent* e);

/* ---------------------------------------------------------------------
 * main.cpp
 * -------------------------------------------------------------------*/
extern ChWindow* XELauncherGetMainWin();
extern int xelaunch_get_pipe();

#endif
