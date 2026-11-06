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
 * knotwindow.cpp -- the "knot" window chrome (WINDOW_FLAG_STYLE_KNOT).
 *
 *  rest            .:.
 *                 :<#>: files          one embroidered motif: a satin
 *                  ':'                 lozenge with a cotton eye inside a
 *                                      back-stitched ring, a cross-stitch
 *  hover           <x> <-> <o>  files  flower on each tip
 *                                      it unties into three stitched
 *                                      lozenges: close, minimise, maximise
 *
 * Focus is a red thread along the top edge (2px red + 1px light thread);
 * an inactive window loses the thread and its knot turns sand. The bar
 * keeps the classic 26px height, so client layouts are unchanged.
 *
 * Costs: the title bar is 26 rows; untie/tie repaint only that strip,
 * a hover change repaints only one 20x20 control cell.
 */

#include "winstyle.h"
#include "gmstyle.h"
#include <string.h>
#include <sys/_keproc.h>

#define KNOT_BAR_H	   26
#define KNOT_CY		   13
#define KNOT_REST_CX   14
#define KNOT_PITCH	   20
#define KNOT_R		   8
#define KNOT_TITLE_TIED	  30
#define KNOT_TITLE_UNTIED 70
#define KNOT_ANIM_MS	  12
#define KNOT_BASE_BG	  CH_GM_PAPER

enum { KNOT_TIED = 0, KNOT_UNTIED = 1 };
enum { KNOT_SLOT_CLOSE, KNOT_SLOT_MIN, KNOT_SLOT_MAX, KNOT_SLOT_COUNT };

extern void ChGlobalMinimiseAction(ChWindow* win, ChWinGlobalControl* ctl);

static void _knot_mouse(ChWindow* win, ChWinGlobalControl* g, int x, int y, int button);
static void _knot_paint_ctl(ChWindow* win, ChWinGlobalControl* g);

static int _slot_of(ChWinGlobalControl* g) {
	if (g->type == WINDOW_GLOBAL_CONTROL_CLOSE)
		return KNOT_SLOT_CLOSE;
	if (g->type == WINDOW_GLOBAL_CONTROL_MINIMIZE)
		return KNOT_SLOT_MIN;
	if (g->type == WINDOW_GLOBAL_CONTROL_MAXIMIZE)
		return KNOT_SLOT_MAX;
	return -1;
}

static bool _ours(ChWinGlobalControl* g) {
	return g && g->ChGlobalButtonPaint == _knot_paint_ctl;
}

/* ------------------------------------------------------------ colours */

typedef struct {
	uint32_t thread, edge, sheen, glyph;
} KnotInk;

static KnotInk _ink(int slot, bool hover, bool pressed) {
	KnotInk k;
	switch (slot) {
	case KNOT_SLOT_CLOSE:
		k.thread = pressed ? CH_GM_RED_DEEP : (hover ? CH_GM_RED_HOVER : CH_GM_RED);
		k.edge = CH_GM_RED_DEEP;
		k.glyph = CH_GM_WHITE;
		break;
	case KNOT_SLOT_MIN:
		k.thread = pressed ? CH_GM_MUGA_DEEP : (hover ? 0xFFD9AC44 : CH_GM_MUGA);
		k.edge = CH_GM_MUGA_DEEP;
		k.glyph = pressed ? CH_GM_WHITE : CH_GM_INK;
		break;
	default:
		k.thread = pressed ? 0xFF000000 : (hover ? 0xFF3E3732 : CH_GM_CHARCOAL);
		k.edge = 0xFF0F0D0C;
		k.glyph = CH_GM_WHITE;
		break;
	}
	k.sheen = ChGmBlend(k.thread, CH_GM_WHITE, 26);
	return k;
}

/* ------------------------------------------------------------ motifs */

static inline void _px(ChCanvas* c, int x, int y, uint32_t col) {
	if (x < 0 || y < 0 || x >= (int)c->canvasWidth || y >= (int)c->canvasHeight)
		return;
	c->buffer[(size_t)y * c->canvasWidth + x] = col;
}

/* a cross-stitch flower: five stitches in a plus */
static void _flower(ChCanvas* c, int cx, int cy, uint32_t col) {
	_px(c, cx, cy, col);
	_px(c, cx - 1, cy, col);
	_px(c, cx + 1, cy, col);
	_px(c, cx, cy - 1, col);
	_px(c, cx, cy + 1, col);
}

/*
 * the resting knot: satin core (r 4) with a cotton eye, a back-stitched
 * ring (r 7) broken at the four tips, and a flower on each tip (r 8)
 */
static void _rest_knot(ChCanvas* c, int cx, int cy, bool focused) {
	uint32_t thread = focused ? CH_GM_RED : CH_GM_SAND;
	uint32_t edge = focused ? CH_GM_RED_DEEP : 0xFFC8BAA0;
	uint32_t sheen = focused ? 0xFFDE465C : 0xFFF0E8DA;
	ChGmLozengeStitched(c, cx, cy, 4, thread, edge, sheen);
	_px(c, cx, cy, KNOT_BASE_BG);
	const int r = 7;
	for (int dy = -r; dy <= r; dy++) {
		int ady = dy < 0 ? -dy : dy;
		int hw = r - ady;
		if (ady >= r - 1 || hw <= 1)
			continue; /* leave the tips open for the flowers */
		_px(c, cx - hw, cy + dy, edge);
		_px(c, cx + hw, cy + dy, edge);
	}
	_flower(c, cx - 8, cy, thread);
	_flower(c, cx + 8, cy, thread);
	_flower(c, cx, cy - 8, thread);
	_flower(c, cx, cy + 8, thread);
}

static void _glyph(ChCanvas* c, int slot, int cx, int cy, uint32_t col) {
	if (slot == KNOT_SLOT_CLOSE) {
		/* two-pixel stitches so the cross reads on the satin */
		for (int i = -3; i <= 2; i++) {
			_px(c, cx + i, cy + i, col);
			_px(c, cx + i + 1, cy + i, col);
			_px(c, cx + i, cy - i, col);
			_px(c, cx + i + 1, cy - i, col);
		}
		_px(c, cx + 3, cy + 3, col);
		_px(c, cx + 3, cy - 3, col);
	} else if (slot == KNOT_SLOT_MIN) {
		ChGmFill(c, cx - 3, cy, 7, 2, col);
	} else {
		ChGmFrame(c, cx - 3, cy - 3, 7, 7, col);
		ChGmFill(c, cx - 3, cy - 2, 7, 1, col);
	}
}

static int _slot_cx(int slot) {
	return KNOT_REST_CX + slot * KNOT_PITCH;
}

/* ------------------------------------------------------------ painting */

static ChWinGlobalControl* _ctl_for_slot(ChWindow* win, int slot) {
	for (int i = 0; i < (int)win->GlobalControls->pointer; i++) {
		ChWinGlobalControl* g = (ChWinGlobalControl*)list_get_at(win->GlobalControls, i);
		if (_ours(g) && _slot_of(g) == slot)
			return g;
	}
	return NULL;
}

static void _paint_bar_bg(ChWindow* win) {
	ChCanvas* c = win->canv;
	int w = win->info->width;
	ChGmFill(c, 0, 0, w, KNOT_BAR_H, KNOT_BASE_BG);
	if (win->focused) {
		ChGmFill(c, 0, 0, w, 2, CH_GM_RED);
		ChGmFill(c, 0, 3, w, 1, CH_GM_RED_THREAD);
	} else {
		ChGmFill(c, 0, 0, w, 1, CH_GM_EDGE);
	}
	ChGmFill(c, 0, KNOT_BAR_H - 1, w, 1, CH_GM_HAIRLINE);
	ChGmFill(c, 0, 0, 1, KNOT_BAR_H, CH_GM_EDGE);
	ChGmFill(c, w - 1, 0, 1, KNOT_BAR_H, CH_GM_EDGE);
}

/* one untied control in its cell, background included */
static void _paint_control(ChWindow* win, int slot, int cx, int r, bool hover, bool pressed, bool glyph) {
	KnotInk k = _ink(slot, hover, pressed);
	ChGmLozengeStitched(win->canv, cx, KNOT_CY, r, k.thread, k.edge, k.sheen);
	if (glyph)
		_glyph(win->canv, slot, cx, KNOT_CY, k.glyph);
}

/* t = 0 tied .. 256 untied; in between the knot slides apart */
static void _paint_cluster(ChWindow* win, int t) {
	if (t <= 0) {
		_rest_knot(win->canv, KNOT_REST_CX, KNOT_CY, win->focused);
		return;
	}
	int r = 4 + (KNOT_R - 4) * t / 256;
	for (int s = KNOT_SLOT_COUNT - 1; s >= 0; s--) {
		int cx = KNOT_REST_CX + (_slot_cx(s) - KNOT_REST_CX) * t / 256;
		ChWinGlobalControl* g = _ctl_for_slot(win, s);
		bool hov = g && g->hover && t >= 256;
		bool prs = g && g->clicked && t >= 256;
		_paint_control(win, s, cx, r, hov, prs, t >= 192);
	}
}

static void _paint_title(ChWindow* win, int x) {
	ChRect clip;
	clip.x = x;
	clip.y = 4;
	clip.w = (int)win->info->width - x - 8;
	clip.h = KNOT_BAR_H - 5;
	if (clip.w <= 0 || !win->title)
		return;
	ChGmTextClipped(win->canv,
					win->app->baseFont,
					win->title,
					x,
					KNOT_CY + 5,
					win->focused ? CH_GM_INK : CH_GM_INK_SOFT,
					&clip);
}

/* custom global buttons an app added keep their own painters */
static void _paint_custom_controls(ChWindow* win) {
	for (int i = 0; i < (int)win->GlobalControls->pointer; i++) {
		ChWinGlobalControl* g = (ChWinGlobalControl*)list_get_at(win->GlobalControls, i);
		if (g && !_ours(g) && g->ChGlobalButtonPaint)
			g->ChGlobalButtonPaint(win, g);
	}
}

static void _paint_bar(ChWindow* win, int t) {
	_paint_bar_bg(win);
	_paint_cluster(win, t);
	_paint_title(win, KNOT_TITLE_TIED + (KNOT_TITLE_UNTIED - KNOT_TITLE_TIED) * t / 256);
	_paint_custom_controls(win);
}

static void KnotPaintTitlebar(ChWindow* win) {
	_paint_bar(win, win->styleState == KNOT_UNTIED ? 256 : 0);
}

static void _present_bar(ChWindow* win) {
	ChWindowUpdate(win, 0, 0, win->info->width, KNOT_BAR_H, false, true);
}

static void KnotFocus(ChWindow* win) {
	KnotPaintTitlebar(win);
	_present_bar(win);
}

static void KnotWinPaint(ChWindow* win) {
	ChCanvas* c = win->canv;
	int w = win->info->width, h = win->info->height;
	ChGmFill(c, 0, KNOT_BAR_H, w, h - KNOT_BAR_H, win->color);
	KnotPaintTitlebar(win);
	for (int i = 0; i < (int)win->widgets->pointer; i++) {
		ChWidget* wid = (ChWidget*)list_get_at(win->widgets, i);
		if (wid && wid->visible && wid->ChPaintHandler)
			wid->ChPaintHandler(wid, win);
	}
	/* hairline frame below the bar (the bar draws its own sides) */
	ChGmFill(c, 0, KNOT_BAR_H, 1, h - KNOT_BAR_H, CH_GM_EDGE);
	ChGmFill(c, w - 1, KNOT_BAR_H, 1, h - KNOT_BAR_H, CH_GM_EDGE);
	ChGmFill(c, 0, h - 1, w, 1, CH_GM_EDGE);
	ChWindowUpdate(win, 0, 0, w, h, 1, 0);
}

/* ------------------------------------------------------------ tie / untie */

/* hit boxes as ChWindowHandleMouse tests them (it uses strict >, <) */
static void _set_rects(ChWindow* win) {
	for (int i = 0; i < (int)win->GlobalControls->pointer; i++) {
		ChWinGlobalControl* g = (ChWinGlobalControl*)list_get_at(win->GlobalControls, i);
		if (!_ours(g))
			continue;
		int s = _slot_of(g);
		if (win->styleState == KNOT_UNTIED) {
			g->x = _slot_cx(s) - KNOT_PITCH / 2 - 1;
			g->y = 2;
			g->w = KNOT_PITCH + 1;
			g->h = KNOT_BAR_H - 3;
		} else if (s == KNOT_SLOT_CLOSE) {
			/* only the knot itself unties the cluster */
			g->x = KNOT_REST_CX - 10;
			g->y = 2;
			g->w = 21;
			g->h = KNOT_BAR_H - 3;
		} else {
			g->x = g->y = 0;
			g->w = g->h = 0; /* never hit while tied */
		}
	}
}

static void _animate(ChWindow* win, int from, int to) {
	const int steps = 3;
	for (int i = 1; i <= steps; i++) {
		int t = from + (to - from) * i / steps;
		_paint_bar(win, t);
		_present_bar(win);
		if (i < steps)
			_KeProcessSleep(KNOT_ANIM_MS);
	}
}

static void _untie(ChWindow* win) {
	if (win->styleState == KNOT_UNTIED)
		return;
	win->styleState = KNOT_UNTIED;
	_set_rects(win);
	_animate(win, 0, 256);
}

static void _tie(ChWindow* win, bool animate) {
	if (win->styleState == KNOT_TIED)
		return;
	win->styleState = KNOT_TIED;
	for (int i = 0; i < (int)win->GlobalControls->pointer; i++) {
		ChWinGlobalControl* g = (ChWinGlobalControl*)list_get_at(win->GlobalControls, i);
		if (_ours(g))
			g->hover = g->clicked = false;
	}
	_set_rects(win);
	if (animate) {
		_animate(win, 256, 0);
	} else {
		KnotPaintTitlebar(win);
		_present_bar(win);
	}
}

static bool _in_cluster(ChWindow* win, int lx, int ly) {
	if (ly < 1 || ly >= KNOT_BAR_H)
		return false;
	if (win->styleState == KNOT_UNTIED)
		return lx >= _slot_cx(0) - KNOT_PITCH / 2 - 1 &&
			   lx < _slot_cx(KNOT_SLOT_COUNT - 1) + KNOT_PITCH / 2 + 1;
	return lx >= KNOT_REST_CX - 10 && lx <= KNOT_REST_CX + 10;
}

/* repaint one untied control cell */
static void _knot_paint_ctl(ChWindow* win, ChWinGlobalControl* g) {
	if (win->styleState != KNOT_UNTIED) {
		KnotPaintTitlebar(win);
		return;
	}
	int s = _slot_of(g);
	int cx = _slot_cx(s);
	/* cell background, keeping the focus threads above it */
	ChGmFill(win->canv, cx - KNOT_PITCH / 2, 4, KNOT_PITCH, KNOT_BAR_H - 5, KNOT_BASE_BG);
	_paint_control(win, s, cx, KNOT_R, g->hover, g->clicked, true);
}

static void _knot_mouse(ChWindow* win, ChWinGlobalControl* g, int x, int y, int button) {
	int lx = x - win->info->x;
	int ly = y - win->info->y;
	bool inside = _in_cluster(win, lx, ly);
	if (win->styleState == KNOT_TIED) {
		/* first contact unties; a touch tap only unties, never acts */
		if (inside && g->hover)
			_untie(win);
		g->clicked = false;
		return;
	}
	if (!inside) {
		_tie(win, true);
		return;
	}
	if (!button)
		g->clicked = false;
	_knot_paint_ctl(win, g);
	int cx = _slot_cx(_slot_of(g));
	ChWindowUpdate(win, cx - KNOT_PITCH / 2, 4, KNOT_PITCH, KNOT_BAR_H - 5, false, true);
	bool overThis = lx >= g->x && lx < g->x + g->w;
	if (button && g->hover && overThis && g->ChGlobalActionEvent) {
		g->clicked = false;
		if (g->ChGlobalActionEvent == ChGlobalMinimiseAction)
			_tie(win, false); /* come back tied after a restore */
		g->ChGlobalActionEvent(win, g);
	}
}

/* ------------------------------------------------------------ install */

static void KnotInstall(ChWindow* win) {
	if (win->color == 0xFFD2D2D2) /* the classic default grey */
		win->color = KNOT_BASE_BG;
	win->ChWinPaint = KnotWinPaint;
	win->styleState = KNOT_TIED;
	int index = 0;
	for (int i = 0; i < (int)win->GlobalControls->pointer; i++) {
		ChWinGlobalControl* g = (ChWinGlobalControl*)list_get_at(win->GlobalControls, i);
		if (!g || g->type == WINDOW_GLOBAL_CONTROL_CUSTOM)
			continue;
		/* ChCreateWindow adds close, maximise, minimise in that order */
		if (index == 0)
			g->type = WINDOW_GLOBAL_CONTROL_CLOSE;
		else if (index == 1)
			g->type = WINDOW_GLOBAL_CONTROL_MAXIMIZE;
		else
			g->type = WINDOW_GLOBAL_CONTROL_MINIMIZE;
		index++;
		g->hover = g->clicked = false;
		g->ChGlobalButtonPaint = _knot_paint_ctl;
		g->ChGlobalMouseEvent = _knot_mouse;
	}
	_set_rects(win);
}

const ChWindowStyleOps ChKnotWindowStyle = {
	"knot", 4, KnotInstall, KnotPaintTitlebar, KnotFocus,
};
