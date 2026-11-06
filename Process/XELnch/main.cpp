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
 * main.cpp -- XELnch, the Xeneva start panel (Gamusa: AHEntryList bento
 * spread + LuitFlow day ribbon).
 *
 * Owns the window, the event loop, input and app launching. Painting is
 * in xlui.cpp / cards.cpp / luitflow.cpp, the layout in bento.cpp, motion
 * in anim.cpp.
 *
 * Show / hide: Namdapha's Go button sends XELNCH_MSG_TOGGLE to this
 * thread (see livetile.h) instead of toggling the window in Deodhai, so
 * the cards can fold up *before* the window disappears and unroll
 * *after* it appears.
 *
 * The loop sleeps in _KePauseThread whenever nothing moves; while a hover,
 * ticker or ribbon animation runs it polls the postbox between 16 ms
 * frames, so input is never blocked by a hover.
 */

#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/_keproc.h>
#include <sys/_kefile.h>
#include <sys/_ketimer.h>
#include <sys/_ketime.h>
#include <sys/iocodes.h>
#include <chitralekha.h>
#include <keycode.h>
#include <widgets/base.h>
#include <widgets/window.h>
#include <livetile.h>
#include "launcher.h"

#ifdef __XENEVA_UNIKERNEL__
int XELnchMain(int argc, char* arv[]);
extern "C" void XELnchThread() {
	XELnchMain(0, NULL);
}
#endif

/* these names are remapped by unikernel_syms.h in the DeodhaiXR build */
ChitralekhaApp* app;
ChWindow* win;
int screen_h;
int screen_w;
int bpp;
int graphicsFd;

XlLauncher xlnch;
static int _initpipe = -1;
static bool _shown; /* cards are on screen (enter animation finished) */

ChWindow* XELauncherGetMainWin() {
	return win;
}

int xelaunch_get_pipe() {
	return _initpipe;
}

uint64_t XlNowMs() {
	return _KeGetCurrentMS();
}

/* XlPresent -- push a canvas rect to Deodhai (clipped to the window) */
void XlPresent(int x, int y, int w, int h) {
	if (!win || xlnch.presentHold)
		return;
	int W = (int)win->info->width, H = (int)win->info->height;
	if (x < 0) {
		w += x;
		x = 0;
	}
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (x + w > W)
		w = W - x;
	if (y + h > H)
		h = H - y;
	if (w <= 0 || h <= 0)
		return;
	ChWindowUpdate(win, x, y, w, h, false, true);
}

static bool _hidden() {
	return __atomic_load_n(&win->info->hide, __ATOMIC_ACQUIRE);
}

bool XlVisible() {
	return win && !_hidden() && _shown;
}

/* Deodhai flips the shared hide flag asynchronously; wait <= ~100 ms */
static void _wait_hidden(bool want) {
	for (int i = 0; i < 50 && _hidden() != want; i++)
		_KeProcessSleep(2);
}

static const XlHit _nohit = {XL_HIT_NONE, 0};

static bool _same(XlHit a, XlHit b) {
	return a.kind == b.kind && a.index == b.index;
}

static void _reset_input() {
	xlnch.hover = _nohit;
	xlnch.press = _nohit;
	xlnch.mouseDown = false;
	xlnch.dragging = false;
	xlnch.scrubbing = false;
	xlnch.pointerX = -1;
	xlnch.springStart = 0;
	if (xlnch.hoverApp != XL_NONE)
		xlnch.apps[xlnch.hoverApp].hov = 0;
	if (xlnch.hoverPrevApp != XL_NONE)
		xlnch.apps[xlnch.hoverPrevApp].hov = 0;
	xlnch.hoverApp = xlnch.hoverPrevApp = XL_NONE;
}

/* ---------------------------------------------------------------- show/hide */

/* back to the resting state: cards, no search, top of the spread */
static void _reset_view() {
	_reset_input();
	xlnch.query[0] = '\0';
	xlnch.queryLen = 0;
	xlnch.resultCount = 0;
	xlnch.view = XL_VIEW_CARDS;
	xlnch.scrollCards = 0;
	xlnch.scrollList = 0;
}

static void XlShow() {
	if (!_hidden())
		return;
	_reset_view();
	/* the ribbon reads the calendar once per show */
	XlFlowRefresh();
	/* XlHide left the canvas as chrome with an empty spread; refresh the
	 * parts that may have changed while hidden, then map the window and
	 * unroll the cards on top of it */
	XlPaintClock();
	XlFlowPaintToggle(true);
	XlFlowPaint(true);
	ChWindowHide(win); /* Deodhai toggles: hidden -> shown */
	_wait_hidden(false);
	XlAnimCards(XL_ANIM_ENTER);
	_shown = true;
}

static void XlHide(int launchApp) {
	if (_hidden())
		return;
	xlnch.dragging = false;
	XlLayoutSave();
	_reset_input();
	if (launchApp >= 0)
		XlAnimLaunch(launchApp);
	else if (_shown && xlnch.view == XL_VIEW_CARDS)
		XlAnimCards(XL_ANIM_EXIT);
	/* leave the canvas in the state the next show starts from */
	_reset_view();
	XlPaintAll(false);
	_shown = false;
	ChWindowHide(win); /* shown -> hidden */
	_wait_hidden(true);
}

/* ---------------------------------------------------------------- actions */

static void XlLaunch(uint8_t ai) {
	XlApp* a = &xlnch.apps[ai];
	XlHide(ai);
	int id = _KeCreateProcess(0, a->title);
	if (id == -1) {
		_KePrint("[XELnch]: failed to create process for %s \r\n", a->title);
		return;
	}
	char* argv[1];
	int argc = 0;
	if (a->param[0] && strcmp(a->param, "Null") != 0) {
		argv[0] = a->param;
		argc = 1;
	}
	_KeProcessLoadExec(id, a->path, argc, argc ? argv : NULL);
}

static void XlPowerRequest(const char* what) {
	if (_initpipe == -1) {
		_KePrint("[XELnch]: no init pipe \r\n");
		return;
	}
	InitRequestMsg msg;
	memset(&msg, 0, sizeof(msg));
	strncpy(msg.message, what, sizeof(msg.message) - 1);
	_KeWriteFile(_initpipe, &msg, sizeof(InitRequestMsg));
}

/* ---------------------------------------------------------------- input */

static bool _is_row(XlHit h) {
	return h.kind == XL_HIT_ROW || h.kind == XL_HIT_ROW_PIN;
}

static bool _is_card(XlHit h) {
	return h.kind == XL_HIT_CARD || h.kind == XL_HIT_CARD_UNPIN;
}

/* repaint exactly the element a hit names */
static void _repaint_hit(XlHit h) {
	if (_is_row(h))
		XlListPaintRow(xlnch.canv, h.index, true);
	else if (h.kind == XL_HIT_POWER || h.kind == XL_HIT_RESTART)
		XlPaintClock();
	else if (h.kind == XL_HIT_LINK)
		XlPaintLink(h.index);
	else if (h.kind == XL_HIT_TOGGLE)
		XlFlowPaintToggle(true);
}

/*
 * _set_hover -- the only place hover changes. Only the previous and the
 * new element repaint; cards fade through the stepped hover animation
 * (which also repaints just those two cards).
 */
static void _set_hover(XlHit h) {
	if (_same(h, xlnch.hover))
		return;
	XlHit old = xlnch.hover;
	xlnch.hover = h;
	uint8_t oldCard = _is_card(old) ? old.index : XL_NONE;
	uint8_t newCard = _is_card(h) ? h.index : XL_NONE;
	if (oldCard != newCard)
		XlHoverSet(newCard);
	if (!_is_card(old))
		_repaint_hit(old);
	if (!_is_card(h))
		_repaint_hit(h);
}

static void _scroll(int dir) {
	const XlMetrics* m = &xlnch.m;
	int *s, content, step;
	if (xlnch.view == XL_VIEW_CARDS) {
		s = &xlnch.scrollCards;
		content = xlnch.contentH;
		step = m->rowShort;
	} else {
		s = &xlnch.scrollList;
		content = ((XlListCount() + m->listCols - 1) / m->listCols) * m->listRowH;
		step = m->listRowH * 2;
	}
	int max = content - m->view.h;
	if (max < 0)
		max = 0;
	int v = *s + dir * step;
	if (v > max)
		v = max;
	if (v < 0)
		v = 0;
	if (v != *s) {
		*s = v;
		XlPaintView();
	}
}

static void _set_view(int view) {
	if (view == xlnch.view)
		return;
	xlnch.view = view;
	xlnch.scrollList = 0;
	if (view == XL_VIEW_CARDS && xlnch.queryLen) {
		xlnch.queryLen = 0;
		xlnch.query[0] = '\0';
		xlnch.resultCount = 0;
		XlPaintSearch();
	}
	_set_hover(_nohit);
	XlPaintMast();
	XlPaintView();
}

static void _drag_update(int lx, int ly) {
	const XlRect* v = &xlnch.m.view;
	XlApp* d = &xlnch.apps[xlnch.press.index];
	if (d->card == XL_NONE)
		return;
	const XlCard* k = &xlnch.cards[d->card];
	xlnch.dragX = lx - xlnch.dragOffX;
	xlnch.dragY = ly - xlnch.dragOffY;
	if (ly < v->y + 6)
		_scroll(-1);
	else if (ly > v->y + v->h - 6)
		_scroll(1);
	int cx = xlnch.dragX + k->w / 2, cy = xlnch.dragY + k->h / 2;
	int target = XlCardAt(cx, cy);
	if (target >= 0)
		XlCardMove(xlnch.press.index, target);
	XlPaintView();
}

static void _release(int lx, int ly) {
	xlnch.mouseDown = false;
	if (xlnch.dragging) {
		xlnch.dragging = false;
		xlnch.press = _nohit;
		XlLayoutSave();
		xlnch.hover = XlHitTest(lx, ly);
		XlPaintView();
		return;
	}
	if (xlnch.scrubbing) {
		XlHit p = xlnch.press;
		xlnch.press = _nohit;
		XlFlowRelease();
		XlHit h = XlHitTest(lx, ly);
		/* a click (no drag) on a ribbon item opens its app */
		int dx = lx - xlnch.pressX, dy = ly - xlnch.pressY;
		if (p.kind == XL_HIT_FLOW_ITEM && _same(h, p) && dx * dx + dy * dy < 36) {
			int ai = XlFlowItemApp(p.index);
			if (ai >= 0)
				XlLaunch((uint8_t)ai);
		}
		return;
	}
	XlHit p = xlnch.press;
	XlHit h = XlHitTest(lx, ly);
	xlnch.press = _nohit;
	if (!_same(h, p)) {
		if (_is_card(p))
			XlPaintCard(p.index);
		_repaint_hit(p);
		_set_hover(h);
		return;
	}
	switch (p.kind) {
	case XL_HIT_CARD:
		XlLaunch(p.index);
		break;
	case XL_HIT_ROW: {
		int ai = XlListAt(p.index);
		if (ai >= 0)
			XlLaunch((uint8_t)ai);
		break;
	}
	case XL_HIT_CARD_UNPIN:
		XlUnpin(p.index);
		XlLayoutSave();
		xlnch.hover = _nohit;
		xlnch.hoverApp = xlnch.hoverPrevApp = XL_NONE;
		XlPaintView();
		break;
	case XL_HIT_ROW_PIN: {
		int ai = XlListAt(p.index);
		if (ai < 0)
			break;
		if (xlnch.apps[ai].pinned)
			XlUnpin((uint8_t)ai);
		else
			XlPin((uint8_t)ai);
		XlLayoutSave();
		XlListPaintRow(xlnch.canv, p.index, true);
		break;
	}
	case XL_HIT_POWER:
		XlPowerRequest(INIT_REQUEST_PW_DOWN);
		break;
	case XL_HIT_RESTART:
		XlPowerRequest(INIT_REQUEST_PW_REBOOT);
		break;
	case XL_HIT_LINK: {
		uint8_t ai = xlnch.linkApp[p.index];
		if (ai == XL_NONE)
			_set_view(xlnch.view == XL_VIEW_CARDS ? XL_VIEW_LIST : XL_VIEW_CARDS);
		else
			XlLaunch(ai);
		break;
	}
	case XL_HIT_TOGGLE:
		XlFlowSetOpen(!xlnch.flowOpen, true);
		break;
	default:
		break;
	}
}

static void XlMouse(int sx, int sy, int button) {
	if (_hidden() || xlnch.animating)
		return;
	if (!_shown) {
		/* made visible behind our back (fallback toggle path): recover */
		XlPaintAll(true);
		_shown = true;
	}
	int lx = sx - win->info->x;
	int ly = sy - win->info->y;
	if (button == DEODHAI_MOUSE_MSG_SCROLL_UP || button == DEODHAI_MOUSE_MSG_SCROLL_DOWN) {
		_scroll(button == DEODHAI_MOUSE_MSG_SCROLL_UP ? -1 : 1);
		return;
	}
	bool left = (button & 1) || button == DEODHAI_MESSAGE_MOUSE_DBLCLK;

	if (left && !xlnch.mouseDown) {
		xlnch.mouseDown = true;
		xlnch.press = XlHitTest(lx, ly);
		xlnch.pressX = lx;
		xlnch.pressY = ly;
		if (xlnch.press.kind == XL_HIT_CARD)
			XlPaintCard(xlnch.press.index); /* pressed look */
		else if (xlnch.press.kind == XL_HIT_TOGGLE)
			XlFlowPaintToggle(true);
		else if (xlnch.press.kind == XL_HIT_FLOW || xlnch.press.kind == XL_HIT_FLOW_ITEM) {
			xlnch.scrubbing = true;
			if (xlnch.press.kind == XL_HIT_FLOW)
				XlFlowScrub(lx);
		}
		return;
	}
	if (left && xlnch.mouseDown) {
		if (xlnch.scrubbing) {
			XlFlowScrub(lx);
			return;
		}
		if (xlnch.press.kind != XL_HIT_CARD || xlnch.view != XL_VIEW_CARDS)
			return;
		if (!xlnch.dragging) {
			int dx = lx - xlnch.pressX, dy = ly - xlnch.pressY;
			if (dx * dx + dy * dy < 64)
				return;
			XlApp* a = &xlnch.apps[xlnch.press.index];
			if (a->card == XL_NONE)
				return;
			XlRect t;
			XlCardRect(&xlnch.cards[a->card], &t);
			xlnch.dragging = true;
			XlHoverSet(XL_NONE);
			a->hov = 0;
			xlnch.dragOffX = xlnch.pressX - t.x;
			xlnch.dragOffY = xlnch.pressY - t.y;
		}
		_drag_update(lx, ly);
		return;
	}
	if (!left && xlnch.mouseDown) {
		_release(lx, ly);
		return;
	}
	_set_hover(XlHitTest(lx, ly));
}

static void XlMouseLeave() {
	if (_hidden() || xlnch.animating)
		return;
	if (xlnch.mouseDown) {
		bool wasDragging = xlnch.dragging;
		xlnch.mouseDown = false;
		xlnch.dragging = false;
		xlnch.press = _nohit;
		if (xlnch.scrubbing)
			XlFlowRelease();
		if (wasDragging) {
			XlLayoutSave();
			XlPaintView();
		}
	}
	_set_hover(_nohit);
}

static bool _control_key(int key) {
	if (key == 0x3A) { /* capslock press */
		ChitralekhaKeySetCapslock(!ChitralekhaKeyGetCapslock());
		return true;
	}
	return key == 0xBA; /* capslock release */
}

static void XlKey(int code) {
	if (_hidden() || xlnch.animating || _control_key(code) || (code & 0x80))
		return;
	if (code == 0x01) { /* esc: clear the search, else close */
		if (xlnch.queryLen) {
			xlnch.queryLen = 0;
			xlnch.query[0] = 0;
			XlSearchUpdate();
			XlPaintSearch();
			_set_view(XL_VIEW_CARDS);
		} else {
			XlHide(-1);
		}
		return;
	}
	char c = ChitralekhaGetKeyPress(code);
	if (c == 0x08) {
		if (xlnch.queryLen == 0)
			return;
		xlnch.query[--xlnch.queryLen] = '\0';
	} else if (c == '\n') {
		if (xlnch.queryLen && xlnch.resultCount)
			XlLaunch(xlnch.results[0]);
		return;
	} else if (c >= 32 && c < 127) {
		if (xlnch.queryLen >= XL_QUERY_MAX - 1)
			return;
		if (ChitralekhaKeyGetCapslock())
			c = (char)toupper(c);
		xlnch.query[xlnch.queryLen++] = c;
		xlnch.query[xlnch.queryLen] = '\0';
	} else {
		return;
	}
	XlSearchUpdate();
	bool wasList = xlnch.view == XL_VIEW_LIST;
	xlnch.view = xlnch.queryLen ? XL_VIEW_LIST : XL_VIEW_CARDS;
	xlnch.scrollList = 0;
	xlnch.hover = _nohit;
	XlHoverSet(XL_NONE);
	XlPaintSearch();
	if (wasList != (xlnch.view == XL_VIEW_LIST) || xlnch.view == XL_VIEW_LIST)
		XlPaintMast();
	XlPaintView();
}

/* ---------------------------------------------------------------- loop */

static void XlHandleMessage(PostEvent* e) {
	switch (e->type) {
	case DEODHAI_REPLY_MOUSE_EVENT: {
		ChWindow* mw = ChGetWindowByHandle(win, e->dword4);
		if (mw == win)
			XlMouse((int)e->dword, (int)e->dword2, (int)e->dword3);
		else if (mw)
			ChWindowHandleMouse(mw, e->dword, e->dword2, e->dword3);
		break;
	}
	case DEODHAI_REPLY_MOUSE_LEAVE:
		XlMouseLeave();
		break;
	case DEODHAI_REPLY_KEY_EVENT:
		XlKey((int)e->dword);
		break;
	case TIMER_MESSAGE_CODE: /* minute tick */
		if (XlVisible())
			XlPaintClock();
		break;
	case XELNCH_MSG_TOGGLE:
		if (_hidden())
			XlShow();
		else
			XlHide(-1);
		break;
	case XELNCH_MSG_HIDE:
		XlHide(-1);
		break;
	case XELNCH_MSG_LIVE_UPDATE:
	case XELNCH_MSG_LIVE_CLEAR:
		XlLiveHandle(e);
		break;
	default: /* DEODHAI_REPLY_FOCUS_CHANGED, ... */
		break;
	}
	memset(e, 0, sizeof(PostEvent));
}

static bool _stepping() {
	return XlVisible() && (XlHoverAnimating() || XlTickAnimating() || XlFlowAnimating());
}

static void _step() {
	uint64_t now = XlNowMs();
	if (XlHoverAnimating())
		XlHoverStep(now);
	if (XlTickAnimating())
		XlTickStep(now);
	if (XlFlowAnimating())
		XlFlowStep(now);
}

static void XlWinPaint(ChWindow* w) {
	(void)w;
	XlPaintAll(_shown);
}

/* the masthead links: Controls, Xeneva (when present) and all apps */
static void _links_init() {
	//const char* titles[2] = { "Controls", "Xeneva" };
	xlnch.linkCount = 0;
	/**for (int i = 0; i < 2; i++) {
		int ai = XlFindByTitleHash(XelnchHash(titles[i]), titles[i]);
		if (ai >= 0)
			xlnch.linkApp[xlnch.linkCount++] = (uint8_t)ai;
	}**/
	xlnch.linkApp[xlnch.linkCount++] = XL_NONE;
}

int main(int argc, char* arv[]) {
	app = ChitralekhaStartApp(argc, arv);

	_initpipe = _KeOpenFile((char*)"/pipe/init", FILE_OPEN_READ_ONLY | FILE_OPEN_WRITE);
	if (_initpipe == -1)
		_KePrint("[XELnch]: failed to open the init pipe \r\n");

	/* temporary canvas, only to learn the screen size */
	ChCanvas* canv = ChCreateCanvas(100, 100);
	screen_w = canv->screenWidth;
	screen_h = canv->screenHeight;
	bpp = canv->bpp;
	graphicsFd = canv->graphics_fd;
	free(canv);

	memset(&xlnch, 0, sizeof(XlLauncher));
	xlnch.hoverApp = xlnch.hoverPrevApp = XL_NONE;
	XlMetricsInit(screen_w, screen_h);
	XlTextInit(app->baseFont);

	XlConfigLoad(XL_CONFIG_FILE);
	XlIndexBuild();
	XlListBuild();
	for (int i = 0; i < xlnch.appCount; i++)
		XlIconLoad(xlnch.apps[i].icon, 24, 24, &xlnch.apps[i].small);
	XlLayoutLoad();
	for (int i = 0; i < xlnch.pinCount; i++) {
		XlApp* a = &xlnch.apps[xlnch.pinOrder[i]];
		if (!a->big.px)
			XlIconLoad(a->icon, 36, 36, &a->big);
	}
	_links_init();
	//XlIconLoad("/icons/pwoff.bmp", 14, 14, &xlnch.powerIcon);
	//XlIconLoad("/icons/reboot.bmp", 14, 14, &xlnch.restartIcon);
	//XlIconTint(&xlnch.powerIcon, XL_WHITE);
	//XlIconTint(&xlnch.restartIcon, XL_WHITE);

	/* "Xeneva Launcher" is how Namdapha finds this window, keep the title */
	win = ChCreateWindow(app,
						 WINDOW_FLAG_STATIC | WINDOW_FLAG_ALWAYS_ON_TOP,
						 (char*)"Xeneva Launcher",
						 10,
						 10,
						 xlnch.m.panelW,
						 xlnch.m.panelH);
	win->color = XL_BG;
	win->ChWinPaint = XlWinPaint;
	win->info->hide = true;
	win->info->alpha = false;
	/* draw straight into the compositor's buffer: the panel is never
	 * scrolled or composed off screen, so a private full-size copy would
	 * only double the memory (ChWindowUpdate skips the copy itself) */
	if (win->canv->buffer != win->buffer) {
		ChDeAllocateBuffer(win->canv);
		win->canv->buffer = win->buffer;
		win->canv->bufferSz = 0;
	}
	xlnch.canv = win->canv;
	XlBufferInit(); /* 32 rows of the panel width for flicker-free repaints */

	XlFlowInit();
	XlLiveInit();
#ifndef __XENEVA_UNIKERNEL__
	/* minute tick for the clock (only painted while visible) */
	int tid = _KeGetThreadID();
	_KeCreateTimer(tid, _KE_TIMER_UNDIFINED_MAXCOUNT, _KE_TIMER_UPDATE_ORDER_MINUTE);
	_KeStartTimer(tid);
#endif
	_shown = false;
	XlFlowRefresh();
	XlPaintAll(false); /* chrome only; the first show unrolls the cards */

	_KeProcessTokenAddSelf(PROCESS_TOKEN_LAUNCHER);

	PostEvent e;
	memset(&e, 0, sizeof(PostEvent));
	while (1) {
		int err = _KeFileIoControl(app->postboxfd, POSTBOX_GET_EVENT, &e);
		if (e.type)
			XlHandleMessage(&e);
		if (err != POSTBOX_NO_EVENT)
			continue;
		if (_stepping()) {
			_step();
			_KeProcessSleep(16);
		} else {
			_KePauseThread();
		}
	}
}
