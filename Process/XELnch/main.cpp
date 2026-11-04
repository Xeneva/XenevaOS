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
 * main.cpp -- XELnch, the Xeneva start screen (Gamusa x Kinetic).
 *
 * Owns the window, the event loop, input and app launching. Painting is
 * in xlui.cpp, the grid in layout.cpp, the animation in anim.cpp.
 *
 * Show / hide: Namdapha's Go button sends XELNCH_MSG_TOGGLE to this
 * thread (see livetile.h) instead of toggling the window in Deodhai, so
 * the tiles can turnstile out *before* the window disappears and
 * turnstile in *after* it appears.
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
static bool _shown; /* tiles are on screen (enter animation finished) */

ChWindow* XELauncherGetMainWin() {
	return win;
}

int xelaunch_get_pipe() {
	return _initpipe;
}

/* XlPresent -- push a canvas rect to Deodhai (clipped to the window) */
void XlPresent(int x, int y, int w, int h) {
	if (!win)
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

/* Deodhai flips the shared hide flag asynchronously; wait <= ~100 ms */
static void _wait_hidden(bool want) {
	for (int i = 0; i < 50 && _hidden() != want; i++)
		_KeProcessSleep(2);
}

static const XlHit _nohit = { XL_HIT_NONE, 0 };

static bool _same(XlHit a, XlHit b) {
	return a.kind == b.kind && a.index == b.index;
}

static void _reset_input() {
	xlnch.hover = _nohit;
	xlnch.press = _nohit;
	xlnch.mouseDown = false;
	xlnch.dragging = false;
}

/* ---------------------------------------------------------------- show/hide */

/* back to the resting state: tiles view, no search, top of the grid */
static void _reset_view() {
	_reset_input();
	xlnch.query[0] = '\0';
	xlnch.queryLen = 0;
	xlnch.resultCount = 0;
	xlnch.view = XL_VIEW_TILES;
	xlnch.scrollTiles = 0;
	xlnch.scrollList = 0;
}

static void XlShow() {
	if (!_hidden())
		return;
	_reset_view();
	/* XlHide left the canvas as "chrome + empty grid" (and so did the
	 * startup paint), so the window is mapped first and the tiles swing in
	 * on top of it -- nothing is painted while the window is hidden */
	ChWindowHide(win); /* Deodhai toggles: hidden -> shown */
	_wait_hidden(false);
	XlAnimTiles(XL_ANIM_ENTER, -1);
	_shown = true;
}

static void XlHide(int launchApp) {
	if (_hidden())
		return;
	if (xlnch.dragging)
		xlnch.dragging = false;
	XlLayoutSave();
	_reset_input();
	if (_shown && xlnch.view == XL_VIEW_TILES)
		XlAnimTiles(XL_ANIM_EXIT, launchApp);
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
	XlHide(xlnch.view == XL_VIEW_TILES ? ai : -1);
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

static bool _is_tile(XlHit h) {
	return h.kind == XL_HIT_TILE || h.kind == XL_HIT_TILE_PIN;
}

static void _repaint_hit(XlHit h) {
	if (_is_tile(h))
		XlPaintTileAt(h.index);
	else if (_is_row(h))
		XlPaintView();
	else if (h.kind == XL_HIT_POWER || h.kind == XL_HIT_RESTART)
		XlPaintHeader();
	else if (h.kind == XL_HIT_TOGGLE)
		XlPaintSection();
}

static void _set_hover(XlHit h) {
	if (_same(h, xlnch.hover))
		return;
	XlHit old = xlnch.hover;
	xlnch.hover = h;
	if (_is_row(old) || _is_row(h)) {
		XlPaintView(); /* one list repaint covers both rows */
		if (!_is_row(old))
			_repaint_hit(old);
		if (!_is_row(h))
			_repaint_hit(h);
		return;
	}
	if (_is_tile(old) && _is_tile(h) && old.index == h.index) {
		XlPaintTileAt(h.index);
		return;
	}
	_repaint_hit(old);
	_repaint_hit(h);
}

static void _scroll(int dir) {
	const XlMetrics* m = &xlnch.m;
	int* s;
	int content, step;
	if (xlnch.view == XL_VIEW_TILES) {
		s = &xlnch.scrollTiles;
		content = XlLayoutContentHeight();
		step = m->pitch;
	} else {
		s = &xlnch.scrollList;
		content = XlListCount() * m->listRowH;
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

static void _toggle_view() {
	if (xlnch.view == XL_VIEW_TILES) {
		xlnch.view = XL_VIEW_LIST;
		xlnch.scrollList = 0;
	} else {
		xlnch.view = XL_VIEW_TILES;
		xlnch.queryLen = 0;
		xlnch.query[0] = '\0';
		xlnch.resultCount = 0;
		XlPaintSearch();
	}
	xlnch.hover = _nohit;
	XlPaintSection();
	XlPaintView();
}

static void _drag_update(int lx, int ly) {
	XlApp* d = &xlnch.apps[xlnch.press.index];
	const XlRect* v = &xlnch.m.view;
	xlnch.dragX = lx - xlnch.dragOffX;
	xlnch.dragY = ly - xlnch.dragOffY;
	/* auto scroll when the tile is pushed past the viewport edge */
	if (ly < v->y + 6)
		_scroll(-1);
	else if (ly > v->y + v->h - 6)
		_scroll(1);
	int cx = xlnch.dragX + d->cacheW / 2;
	int cy = xlnch.dragY + d->cacheH / 2;
	if (cx < v->x)
		cx = v->x;
	if (cx >= v->x + v->w)
		cx = v->x + v->w - 1;
	if (cy < v->y)
		cy = v->y;
	if (cy >= v->y + v->h)
		cy = v->y + v->h - 1;
	int slot = XlLayoutSlotAt(cx, cy);
	if (slot >= 0)
		XlLayoutMove(xlnch.press.index, slot);
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
	XlHit p = xlnch.press;
	XlHit h = XlHitTest(lx, ly);
	xlnch.press = _nohit;
	if (!_same(h, p)) {
		if (_is_tile(p))
			XlPaintTileAt(p.index);
		_set_hover(h);
		return;
	}
	switch (p.kind) {
	case XL_HIT_TILE:
	case XL_HIT_ROW:
		XlLaunch(p.index);
		break;
	case XL_HIT_TILE_PIN:
		XlUnpin(p.index);
		XlLayoutSave();
		xlnch.hover = XlHitTest(lx, ly);
		XlPaintView();
		break;
	case XL_HIT_ROW_PIN:
		if (xlnch.apps[p.index].pinned)
			XlUnpin(p.index);
		else
			XlPin(p.index);
		XlLayoutSave();
		XlPaintView();
		break;
	case XL_HIT_POWER:
		XlPowerRequest(INIT_REQUEST_PW_DOWN);
		break;
	case XL_HIT_RESTART:
		XlPowerRequest(INIT_REQUEST_PW_REBOOT);
		break;
	case XL_HIT_TOGGLE:
		_toggle_view();
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
	if (button == DEODHAI_MOUSE_MSG_SCROLL_UP) {
		_scroll(-1);
		return;
	}
	if (button == DEODHAI_MOUSE_MSG_SCROLL_DOWN) {
		_scroll(1);
		return;
	}
	bool left = (button & 1) || button == DEODHAI_MESSAGE_MOUSE_DBLCLK;

	if (left && !xlnch.mouseDown) {
		xlnch.mouseDown = true;
		xlnch.press = XlHitTest(lx, ly);
		xlnch.pressX = lx;
		xlnch.pressY = ly;
		if (xlnch.press.kind == XL_HIT_TILE)
			XlPaintTileAt(xlnch.press.index); /* pressed look */
		return;
	}
	if (left && xlnch.mouseDown) {
		if (xlnch.press.kind != XL_HIT_TILE || xlnch.view != XL_VIEW_TILES)
			return;
		if (!xlnch.dragging) {
			int dx = lx - xlnch.pressX, dy = ly - xlnch.pressY;
			if (dx * dx + dy * dy < 64)
				return;
			XlRect t;
			XlLayoutTileRect(&xlnch.apps[xlnch.press.index], &t);
			xlnch.dragging = true;
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
	xlnch.view = xlnch.queryLen ? XL_VIEW_LIST : XL_VIEW_TILES;
	xlnch.scrollList = 0;
	xlnch.hover = _nohit;
	XlPaintSearch();
	XlPaintSection();
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
	default: /* TIMER_MESSAGE_CODE, DEODHAI_REPLY_FOCUS_CHANGED, ... */
		break;
	}
	memset(e, 0, sizeof(PostEvent));
}

static void XlWinPaint(ChWindow* w) {
	(void)w;
	XlPaintAll(_shown);
}

int main(int argc, char* arv[]) {
	app = ChitralekhaStartApp(argc, arv);
	ChFontSetSize(app->baseFont, 12);

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
	xlnch.font = app->baseFont;
	XlMetricsInit(screen_w, screen_h);

	XlConfigLoad(XL_CONFIG_FILE);
	XlIndexBuild();
	XlListBuild();
	for (int i = 0; i < xlnch.appCount; i++)
		XlIconLoad(xlnch.apps[i].icon, xlnch.m.listIcon, xlnch.m.listIcon, &xlnch.apps[i].listIcon);
	XlLayoutLoad();
	for (int i = 0; i < xlnch.pinCount; i++)
		XlTileEnsure(&xlnch.apps[xlnch.pinOrder[i]]);
	int b = xlnch.m.power.w / 2;
	XlIconLoad("/icons/pwoff.bmp", b, b, &xlnch.powerIcon);
	XlIconLoad("/icons/reboot.bmp", b, b, &xlnch.restartIcon);
	XlIconTint(&xlnch.powerIcon, XL_WHITE);
	XlIconTint(&xlnch.restartIcon, XL_WHITE);

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
	xlnch.canv = win->canv;

	XlLiveInit();
	_shown = false;
	XlPaintAll(false); /* chrome only; the first show swings the tiles in */

	PostEvent e;
	memset(&e, 0, sizeof(PostEvent));
	while (1) {
		int err = _KeFileIoControl(app->postboxfd, POSTBOX_GET_EVENT, &e);
		XlHandleMessage(&e);
		if (err == POSTBOX_NO_EVENT)
			_KePauseThread();
	}
}
