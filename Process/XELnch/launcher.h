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
 * launcher.h -- XELnch internal header.
 *
 * The start panel is a magazine spread on gamusa cotton:
 *
 *   ==== pari =========================================================
 *   seek                                              [o][r]   1:04
 *   type to find an app ____                         thursday, 5 november
 *   always here ----------------------- controls . xeneva . all apps >
 *   <> <> <> <> <> <> <> <> <> <> <> <> <> <> <> <> <> <> <> <> <>
 *   [ AHEntryList: bento spread of cards, justified rows           ]
 *   [  tall row: hero . files . glimpse . calendar                 ]
 *   [  short row: notes . netsurf . playverse . doom . ...         ]
 *   [=] your day . 3 new
 *   ~~~~~~~~~~~~ LuitFlow: the day as a ribbon, a pointer you scrub ~~~
 *   ==== phul band ====================================================
 *
 * Memory (bounded, freed eagerly):
 *   - window: the canvas draws straight into the compositor's buffer
 *     (no second full-panel copy)
 *   - text: every string is drawn from a few pre-rasterised glyph sets
 *     (8-bit coverage, ~150 KB in all); no font is touched after start-up,
 *     so there is never a glyph-atlas re-bake while painting
 *   - icons: one small (list / plate) per app, one big only while pinned
 *   - LuitFlow: a fixed ring of XL_FLOW_MAX items
 *
 * Time:
 *   - card hit test: binary search over rows, then over the cards of the
 *     row, O(log n); list rows O(1)
 *   - app lookup by hash: O(log n)
 *   - bento layout: O(n), only when pins / order change
 *   - painting: O(pixels of the dirty rect); a hover change repaints only
 *     the previous and the current item
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

/* the word that heads the panel and doubles as the search field */
#define XL_HEADLINE "seek"
#define XL_HINT		"type to find an app"

/* ---------------------------------------------------------------------
 * limits
 * -------------------------------------------------------------------*/
#define XL_MAX_APPS		 64
#define XL_MAX_ROWS		 24
#define XL_TITLE_MAX	 40
#define XL_PATH_MAX		 48
#define XL_PARAM_MAX	 32
#define XL_KICK_MAX		 20
#define XL_DECK_MAX		 32
#define XL_LIVE_TEXT_MAX 40
#define XL_QUERY_MAX	 40
#define XL_FLOW_MAX		 12 /* live ring */
#define XL_FLOW_NEXT	 4	/* calendar events shown */
#define XL_FLOW_VIEW	 (XL_FLOW_MAX + XL_FLOW_NEXT)
#define XL_NONE			 0xFF

/* Namdapha, the taskbar, sits along the bottom of the screen
 * (NAMDAPHA_WIDTH in Process/Namdapha/nmdapha.h); the panel stops above it */
#define XL_TASKBAR_H 65
#define XL_PANEL_Y	 10

/* card sizes: the same on every screen (hero = 2 M + gap, wide = 2 S + gap) */
#define XL_CARD_TALL_H	168 /* hero and medium rows */
#define XL_CARD_SHORT_H 108 /* wide and small rows */
#define XL_CARD_M_W		172
#define XL_CARD_S_W		132
#define XL_CARD_GAP		10

#define XL_CONFIG_FILE "/lncher.cnf"
#define XL_LAYOUT_FILE "/lnchlay.cnf"

/* ---------------------------------------------------------------------
 * Gamusa palette (ARGB)
 * -------------------------------------------------------------------*/
#define XL_BG		  0xFFFAF7F0u /* cotton                        */
#define XL_PAPER	  0xFFFFFEFAu /* paper cards                   */
#define XL_INK		  0xFF231F1Cu
#define XL_INK_SOFT	  0xFF6B6259u
#define XL_RED		  0xFFC8102Eu
#define XL_RED_DEEP	  0xFF8E1A22u
#define XL_MUGA		  0xFFC99A2Eu
#define XL_MUGA_DEEP  0xFF7A5A12u
#define XL_CHARCOAL	  0xFF2B2622u
#define XL_IVORY	  0xFFEDE3CFu
#define XL_EDGE		  0xFFDCCFB4u
#define XL_SHADOW	  0xFFE4DBCBu
#define XL_WHITE	  0xFFFFFFFFu
#define XL_WEAVE	  0xFFF3DAD6u
#define XL_WEAVE_LINE 0xFFE6A9AEu
#define XL_FAINT	  0xFFF6E9E5u /* a phul woven into paper       */
#define XL_LINEN	  0xFFF5EFE4u /* ribbon cloth                  */
#define XL_HOVER	  0xFFF6EEE4u
#define XL_SAND		  0xFFE2D6C0u

enum { XL_COL_RED, XL_COL_RED_DEEP, XL_COL_MUGA, XL_COL_MUGA_DEEP, XL_COL_INK, XL_COL_IVORY, XL_COL_COUNT };

/* card sizes from lncher.cnf: S small, M medium (tall), W wide, L hero */
enum { XL_TILE_S, XL_TILE_M, XL_TILE_W, XL_TILE_L, XL_TILE_SIZE_COUNT };

/* ---------------------------------------------------------------------
 * glyph sets: pre-rasterised ASCII, 8-bit coverage
 * -------------------------------------------------------------------*/
enum {
	XL_GS_BODY,	  /* calibri ~13px : decks, list, dates           */
	XL_GS_SMALL,  /* calibri ~11px : kickers, captions, times     */
	XL_GS_MID,	  /* light   ~17px : links, flow lines, toggle    */
	XL_GS_ITALIC, /* light italic ~17px : masthead                */
	XL_GS_NAME_S, /* light   ~25px : short card names             */
	XL_GS_NAME_L, /* light   ~40px : tall card names              */
	XL_GS_THIN,	  /* thin    ~48px : headline, query, clock, hero */
	XL_GS_COUNT
};

typedef struct _xl_glyph_ {
	uint8_t w, h;
	int8_t ox, oy; /* bitmap offset from (pen, baseline) */
	uint8_t adv;
	uint32_t off; /* into the set's pool */
} XlGlyph;

typedef struct _xl_glyphset_ {
	XlGlyph g[95]; /* ' ' .. '~' */
	uint8_t* pool;
	int px;
	int ascent;
} XlGlyphSet;

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
	char kicker[XL_KICK_MAX]; /* "LISTEN" -- upper case */
	char deck[XL_DECK_MAX];	  /* "music, played your way" */
	char name[XL_TITLE_MAX];  /* lower-case display name */
	uint32_t titleHash;
	uint32_t pathHash;
	uint8_t size;
	uint8_t color;
	uint8_t pinned;
	uint8_t defPinned;
	uint8_t card; /* index into xlnch.cards while pinned, else XL_NONE */
	uint8_t hov;  /* hover animation 0..255 */
	uint8_t tick; /* live ticker animation 0..255, 255 = settled */
	XlIcon small; /* always: plates, list, flow */
	XlIcon big;	  /* only while pinned */
	XlLive live;
	char tickOld[XL_LIVE_TEXT_MAX * 2 + 8]; /* the line the ticker slides away */
} XlApp;

typedef struct _xl_rect_ {
	int x, y, w, h;
} XlRect;

enum { XL_CARD_HERO, XL_CARD_TALL, XL_CARD_SHORT };
enum { XL_STYLE_PAPER, XL_STYLE_SOLID };

typedef struct _xl_card_ {
	int16_t x, y, w, h; /* content coords (y before scroll) */
	uint8_t app;
	uint8_t kind;
	uint8_t style;
	uint8_t row;
} XlCard;

typedef struct _xl_row_ {
	int16_t y, h;
	uint8_t first, count;
} XlRow;

/* LuitFlow */
enum { XL_FLOW_PAST, XL_FLOW_NOW, XL_FLOW_NEXT_KIND };

typedef struct _xl_flow_item_ {
	uint8_t used;
	uint8_t kind;
	uint8_t app;
	uint8_t seen;
	uint16_t minute; /* minute of the day, + 1440 per day ahead */
	uint16_t progress;
	int16_t wf; /* width when focused: fits its text */
	uint32_t stamp; /* update order, for the ring */
	char l1[XL_LIVE_TEXT_MAX];
	char l2[XL_LIVE_TEXT_MAX];
} XlFlowItem;

typedef struct _xl_metrics_ {
	int panelW, panelH;
	bool compact; /* small screens: tighter rows, smaller faces */
	int headBase, hintBase, ruleY; /* headline / clock baselines */
	int mastBase;				   /* masthead text baseline offset */
	int padX;
	int contentW;
	int gap;
	XlRect headline; /* search / headline */
	XlRect clock;
	XlRect power;
	XlRect restart;
	XlRect mast;	/* "always here" masthead row */
	XlRect view;	/* AHEntryList / list viewport */
	XlRect toggle;	/* LuitFlow toggle */
	XlRect flow;	/* ribbon area */
	int flowCY;		/* ribbon centre line */
	int flowHW;		/* ribbon half height */
	int flowW0, flowWFMax, flowHF; /* resting item, focused card */
	int footerY;
	int toggleYOpen, viewHOpen; /* with the ribbon open; rolled up, the
								   view may grow down to show every row */
	int rowTall, rowShort;
	int listRowH, listCols, listColW;
} XlMetrics;

enum { XL_VIEW_CARDS, XL_VIEW_LIST };

enum {
	XL_HIT_NONE,
	XL_HIT_CARD,
	XL_HIT_CARD_UNPIN,
	XL_HIT_ROW,
	XL_HIT_ROW_PIN,
	XL_HIT_POWER,
	XL_HIT_RESTART,
	XL_HIT_LINK, /* index: 0 first link .. */
	XL_HIT_TOGGLE,
	XL_HIT_FLOW_ITEM, /* index: position in the flow view */
	XL_HIT_FLOW
};

typedef struct _xl_hit_ {
	uint8_t kind;
	uint8_t index;
} XlHit;

#define XL_LINKS 3

typedef struct _xl_launcher_ {
	XlApp* apps;
	int appCount;

	/* pinned cards, in user order */
	uint8_t pinOrder[XL_MAX_APPS];
	int pinCount;
	XlCard cards[XL_MAX_APPS];
	XlRow rows[XL_MAX_ROWS];
	int rowCount;
	int contentH;

	/* lookup indexes, sorted by hash -> binary search */
	uint8_t byPath[XL_MAX_APPS];
	uint8_t byTitle[XL_MAX_APPS];

	/* alphabetical list + search results */
	uint8_t listOrder[XL_MAX_APPS];
	uint8_t results[XL_MAX_APPS];
	int resultCount;
	char query[XL_QUERY_MAX];
	int queryLen;

	/* masthead links: app indexes (XL_NONE = the all apps toggle) */
	uint8_t linkApp[XL_LINKS];
	XlRect linkRect[XL_LINKS];
	int linkCount;

	int view;
	int scrollCards; /* pixels */
	int scrollList;

	/* input */
	XlHit hover;
	XlHit press;
	bool mouseDown;
	bool dragging;
	int pressX, pressY;
	int dragX, dragY;
	int dragOffX, dragOffY;
	uint8_t hoverPrevApp; /* card whose hover is fading out */
	uint8_t hoverApp;	  /* card whose hover is fading in  */

	/* LuitFlow */
	XlFlowItem flow[XL_FLOW_MAX];
	XlFlowItem next[XL_FLOW_NEXT];
	int nextCount;
	uint8_t flowView[XL_FLOW_VIEW]; /* <XL_FLOW_MAX ring, else next + XL_FLOW_MAX */
	int flowCount;
	int flowNow; /* index in flowView the pointer rests on */
	uint32_t flowStamp;
	bool flowOpen;
	int flowReveal; /* 0..256 unroll */
	int pointerX;	/* -1 = at rest on now */
	bool scrubbing;
	int springFrom;
	uint64_t springStart;
	uint64_t revealStart;
	int revealFrom, revealTo;
	int16_t* waveY8; /* ribbon centre offset per column, 1/8 px */

	int flowWeekday; /* weekday of "today" at the last refresh */
	int presentHold; /* >0 while painting into the strip buffer */

	bool animating; /* a blocking animation runs */
	bool layoutDirty;
	int minuteShown;

	XlIcon powerIcon;
	XlIcon restartIcon;
	XlMetrics m;
	XlGlyphSet gs[XL_GS_COUNT];
	ChCanvas* canv; /* window canvas (== compositor buffer) */
} XlLauncher;

extern XlLauncher xlnch;

/* ---------------------------------------------------------------------
 * gamusa.cpp -- palette, primitives, motifs
 * -------------------------------------------------------------------*/
extern uint32_t XlBlend(uint32_t dst, uint32_t src, uint32_t a255);
extern uint32_t XlTileColor(int colorIdx);
extern uint32_t XlTileTextColor(int colorIdx);
extern bool XlColorIsDark(int colorIdx);
extern bool XlClipRect(ChCanvas* c, XlRect* r, const XlRect* clip);
extern void XlFill(ChCanvas* c, int x, int y, int w, int h, uint32_t col, const XlRect* clip);
extern void XlFillAlpha(ChCanvas* c, int x, int y, int w, int h, uint32_t col, uint32_t a, const XlRect* clip);
extern void XlFrame(ChCanvas* c, int x, int y, int w, int h, int t, uint32_t col, const XlRect* clip);
extern void XlLozenge(ChCanvas* c, int cx, int cy, int r, uint32_t col, const XlRect* clip);
extern void XlGamusaPari(ChCanvas* c, int x, int y, int w);
extern void XlGamusaPhulBand(ChCanvas* c, int x, int y, int w);
extern void XlDrawMotif(ChCanvas* c, int motif, int x, int y, int scale, uint32_t col, const XlRect* clip);
extern void XlJaapi(ChCanvas* c, int cx, int cy, int r, const XlRect* clip);
/* row of the canvas buffer's first line: 0 normally, the strip's top
 * while XlBuffered paints into the strip */
extern int xlOriginY;
enum { XL_MOTIF_PHUL, XL_MOTIF_LOZENGE };

/* ---------------------------------------------------------------------
 * xlicon.cpp -- BMP decode + box-filter downscale
 * -------------------------------------------------------------------*/
extern bool XlIconLoad(const char* path, int maxW, int maxH, XlIcon* out);
extern void XlIconFree(XlIcon* ico);
extern void XlIconTint(XlIcon* ico, uint32_t rgb);
extern void XlIconDraw(ChCanvas* c, const XlIcon* ico, int x, int y, uint32_t alpha, const XlRect* clip);

/* ---------------------------------------------------------------------
 * text.cpp -- glyph sets
 * -------------------------------------------------------------------*/
extern void XlTextInit(ChFont* body);
extern int XlTextWidth(int set, const char* s);
extern int XlTextWidthN(int set, const char* s, int n);
extern int XlText(ChCanvas* c, int set, const char* s, int x, int baseline, uint32_t col, uint32_t alpha,
				  const XlRect* clip);
extern void XlTextFit(int set, const char* src, int maxW, char* dst, int dstSize);
extern int XlTextAscent(int set);

/* ---------------------------------------------------------------------
 * bento.cpp -- AHEntryList layout, hit testing, pins, persistence
 * -------------------------------------------------------------------*/
extern void XlIndexBuild();
extern int XlFindByTitleHash(uint32_t h, const char* title);
extern int XlFindByPathHashFirst(uint32_t h);
extern void XlBentoLayout();
extern void XlCardRect(const XlCard* k, XlRect* r); /* canvas coords, with scroll */
extern int XlCardAt(int lx, int ly);				/* card index or -1, O(log n) */
extern bool XlCardMove(uint8_t app, int toCard);
extern bool XlPin(uint8_t app);
extern bool XlUnpin(uint8_t app);
extern void XlLayoutLoad();
extern void XlLayoutSave();

/* ---------------------------------------------------------------------
 * cards.cpp -- card painting
 * -------------------------------------------------------------------*/
/* reveal: 0..256 unroll from the top edge; fade: content alpha */
extern void XlCardPaint(ChCanvas* c, const XlCard* k, int x, int y, int reveal, uint32_t fade, bool pressed,
						const XlRect* clip);
extern void XlCardUnpinRect(const XlCard* k, int x, int y, XlRect* r);
extern void XlCardDirtyRect(const XlCard* k, XlRect* r);

/* ---------------------------------------------------------------------
 * applist.cpp -- all apps list + search
 * -------------------------------------------------------------------*/
extern void XlListBuild();
extern void XlSearchUpdate();
extern int XlListCount();
extern int XlListAt(int i);
extern void XlListPaint(ChCanvas* c);
extern void XlListPaintRow(ChCanvas* c, int i, bool present);
extern XlHit XlListHit(int lx, int ly);

/* ---------------------------------------------------------------------
 * luitflow.cpp -- the day ribbon
 * -------------------------------------------------------------------*/
extern void XlFlowInit();
extern void XlFlowLive(uint8_t app, const XlLive* lv); /* a live update arrived */
extern void XlFlowRefresh();						   /* rebuild the view (calendar, now) */
extern void XlFlowPaint(bool present);
extern void XlFlowPaintToggle(bool present);
extern XlHit XlFlowHit(int lx, int ly);
extern int XlFlowNewCount();
extern void XlFlowMarkSeen();
extern int XlFlowItemApp(int viewIdx);
extern void XlFlowScrub(int lx);		/* pointer follows the mouse */
extern void XlFlowRelease();			/* spring back to now */
extern bool XlFlowAnimating();
extern void XlFlowStep(uint64_t now);	/* advance unroll / spring */
extern void XlFlowSetOpen(bool open, bool animate);

/* ---------------------------------------------------------------------
 * xlui.cpp -- metrics, panel painting, hit testing
 * -------------------------------------------------------------------*/
extern void XlMetricsInit(int screenW, int screenH);
extern void XlPaintAll(bool withContent);
extern void XlPaintHeader();
extern void XlPaintSearch();
extern void XlPaintClock();
extern void XlPaintMast();
extern void XlPaintLink(int i);
extern void XlPaintView();
extern void XlPaintCard(uint8_t app);
extern XlHit XlHitTest(int lx, int ly);
extern void XlViewClear();
extern void XlLowerLayout(); /* toggle / view geometry for the ribbon state */
extern void XlPaintLower();	 /* view + toggle + ribbon, one buffered pass */
/* implemented by the host: pushes a canvas rect to the compositor */
extern void XlPresent(int x, int y, int w, int h);
/*
 * XlBuffered -- paint a rect without the compositor ever seeing it half
 * drawn: fn paints into a small strip buffer, 32 rows at a time, and each
 * finished strip is copied into the window in one go; then the rect is
 * presented once. The panel itself has no second full-size buffer.
 */
extern void XlBufferInit();
extern void XlBuffered(int x, int y, int w, int h, void (*fn)(void*), void* arg);

/* ---------------------------------------------------------------------
 * anim.cpp -- unfold / fold / launch (blocking) and hover (stepped)
 * -------------------------------------------------------------------*/
enum { XL_ANIM_ENTER, XL_ANIM_EXIT };
extern void XlAnimCards(int mode);
extern void XlAnimLaunch(int app);
extern void XlHoverSet(uint8_t app); /* XL_NONE: no card hovered */
extern bool XlHoverAnimating();
extern void XlHoverStep(uint64_t now);
extern void XlTickStart(uint8_t app);
extern bool XlTickAnimating();
extern void XlTickStep(uint64_t now);

/* ---------------------------------------------------------------------
 * lconfig.cpp
 * -------------------------------------------------------------------*/
extern int XlConfigLoad(const char* path);

/* ---------------------------------------------------------------------
 * livetile.cpp
 * -------------------------------------------------------------------*/
extern void XlLiveInit();
extern void XlLiveHandle(PostEvent* e);
extern void XlLiveSet(uint8_t app, int layout, const char* l1, const char* l2, int badge, int progress);
extern void XlLiveLine(const XlApp* a, char* out, int size);

/* ---------------------------------------------------------------------
 * main.cpp
 * -------------------------------------------------------------------*/
extern ChWindow* XELauncherGetMainWin();
extern int xelaunch_get_pipe();
extern bool XlVisible();
extern uint64_t XlNowMs();

#endif
