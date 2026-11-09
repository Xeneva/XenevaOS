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

/*
 * Phulbari -- the XenevaOS front end of the cut-motion demo.
 *
 * Frame 1: a slow walk forward along a path through a flower garden.
 * Frame 2: a hard cut to a closer shot; a butterfly is suddenly there on
 *          a flower, holds still for a second, then flies off.
 * Frame 3: the camera follows the butterfly; after two seconds it darts
 *          off screen and "Welcome", stitched in thread, rises out of
 *          the flowers to the centre.
 * Frame 4: the word breaks into butterflies flying every way.
 * Frame 5: "Aurora Bordoisila: Kernel written from scratch" rises out
 *          of the flowers the same way, and holds for five seconds.
 * Frame 6: a gamusa flies across from the left, rippling, and takes the
 *          garden away behind it.
 * Frame 7: a bamboo garden is drawn stroke by stroke; the Xeneva bird
 *          flies right to left and "DeodhaiXR / XR Native Compositor"
 *          is written in its wake, held for seven seconds.
 * Frame 8: the words come unstitched; a boy runs in, puts on his XR
 *          glasses and works the apps floating round him with his hands
 *          for four seconds, then runs off to the right.
 * Frame 9: the bamboo is taken down; Chitralekha, at her easel, draws
 *          Aniruddha, and "Chitralekha / Graphics and Widget Library for
 *          Apps" is stitched in above her, held for five seconds.
 *
 * Memory: the window is screen sized, so it is drawn straight into the
 * compositor's shared buffer (no private full-size canvas, as Seek does)
 * and the engine keeps one byte per woven cell. Every allocation is made
 * once at start; nothing is allocated per cut.
 */

#include <stdint.h>
#include <_xeneva.h>
#include <sys/_keproc.h>
#include <sys/_kefile.h>
#include <sys/iocodes.h>
#include <sys/mman.h>
#include <chitralekha.h>
#include <widgets/base.h>
#include <widgets/window.h>
#include <keycode.h>
#include <font.h>
#include <string.h>
#include <stdlib.h>
#include "phulbari.h"

/* Namdapha, the taskbar, runs along the bottom of the screen
 * (NAMDAPHA_WIDTH in Process/Namdapha/nmdapha.h, horizontal build) */
#define NAMDAPHA_SIZE 65
/* the gamusa hem left below the scene, in win->color; it also keeps the
 * window's rounded bottom corners out of the drawing area */
#define HEM_PX 8

static ChitralekhaApp* app;
static ChWindow* win;
static void (*_chromePaint)(ChWindow* win);

/* drawing area inside the window */
static int areaX, areaY, areaW, areaH;
static int _capVersion = -1;
static bool _paused;
static bool _ready;

static inline uint32_t* AreaBuffer() {
	return win->canv->buffer + (size_t)areaY * win->canv->canvasWidth + areaX;
}

/* small helpers, no printf needed */
static char* PutStr(char* d, const char* s) {
	while (*s)
		*d++ = *s++;
	*d = 0;
	return d;
}

static char* PutUInt(char* d, uint32_t v) {
	char tmp[12];
	int n = 0;
	do {
		tmp[n++] = (char)('0' + v % 10);
		v /= 10;
	} while (v && n < 11);
	while (n)
		*d++ = tmp[--n];
	*d = 0;
	return d;
}

/*
 * DrawCaption -- the one-line strip under the scene: the frame's title
 * on the left, the demo's memory use on the right
 */
static void DrawCaption() {
	const PbCaption* cap = PbGetCaption();
	int cx, cy, cw, ch;
	PbCaptionRect(&cx, &cy, &cw, &ch);
	PbRenderCaptionBackground(AreaBuffer(), win->canv->canvasWidth);

	ChFont* font = app->baseFont;
	int y = areaY + cy + ch / 2 + 5; /* baseline, centred in the strip */
	char line[96];

	if (cap->title) {
		ChFontSetSize(font, 12);
		ChFontDrawText(win->canv, font, (char*)cap->title, areaX + cx + 16, y, 12, PB_C_RED_DEEP);
	}

	/* the demo's own memory, which is fixed once it starts */
	char* p = PutStr(line, "heap ");
	p = PutUInt(p, (uint32_t)((PbHeapBytes() + 1023) / 1024));
	p = PutStr(p, " KB, fixed  |  a cut every ");
	p = PutUInt(p, PB_CUT_MS);
	p = PutStr(p, " ms");
	if (_paused)
		PutStr(p, "  |  paused");
	ChFontSetSize(font, 10);
	int w = (int)ChFontGetWidth(font, line);
	ChFontDrawText(win->canv, font, line, areaX + cx + cw - w - 16, y, 10, PB_C_INK_SOFT);

	ChWindowUpdate(win, areaX + cx, areaY + cy, cw, ch, false, true);
	_capVersion = cap->version;
}

/*
 * PhulbariPaint -- whole-window paint: the window chrome first, then the
 * shown cut on top (a plain repaint must not advance the animation)
 */
static void PhulbariPaint(ChWindow* w) {
	if (_chromePaint)
		_chromePaint(w);
	if (!_ready)
		return;
	PbRepaint(AreaBuffer(), win->canv->canvasWidth);
	ChWindowUpdate(win, areaX, areaY, areaW, areaH, false, true);
	DrawCaption();
}

/* PhulbariCut -- advance and show one cut */
static void PhulbariCut() {
	PbStep();
	int x, y, w, h;
	if (PbRender(AreaBuffer(), win->canv->canvasWidth, false, &x, &y, &w, &h))
		ChWindowUpdate(win, areaX + x, areaY + y, w, h, false, true);
	if (PbGetCaption()->version != _capVersion)
		DrawCaption();

	/* a heartbeat for the serial log every ten seconds */
	if (PbCutNumber() % 80 == 0)
		_KePrint("[phulbari]: cut %d, heap %d bytes \r\n", (int)PbCutNumber(), (int)PbHeapBytes());
}

/* draw into the compositor's buffer directly, as Seek does */
static void UseSharedBuffer() {
	if (win->canv->buffer != win->buffer) {
		ChDeAllocateBuffer(win->canv);
		win->canv->buffer = win->buffer;
		win->canv->bufferSz = 0;
	}
	win->canv->canvasWidth = win->info->width;
	win->canv->canvasHeight = win->info->height;
}

static void PhulbariHandleMessage(PostEvent* e) {
	switch (e->type) {
	case DEODHAI_REPLY_MOUSE_EVENT: {
		if (e->dword5 == WINDOW_HANDLE_TYPE_NORMAL) {
			ChWindow* mouseWin = ChGetWindowByHandle(win, e->dword4);
			ChWindowHandleMouse(mouseWin, e->dword, e->dword2, e->dword3);
		}
		break;
	}
	case DEODHAI_REPLY_KEY_EVENT: {
		int code = e->dword;
		ChitralekhaProcessKey(code);
		/* space pauses and resumes */
		if (ChitralekhaKeyToASCII(code) == ' ') {
			_paused = !_paused;
			_capVersion = -1;
			DrawCaption();
		}
		break;
	}
	case DEODHAI_REPLY_FOCUS_CHANGED: {
		ChWindow* focWin = ChGetWindowByHandle(win, e->dword2);
		ChWindowHandleFocus(focWin, e->dword, e->dword2);
		break;
	}
	case DEODHAI_REPLY_DESTROY_BUFFER: {
		ChWindowHandleDestroyBuffer(win);
		break;
	}
	case DEODHAI_REPLY_REINIT_BUFFER: {
		int backbufkey = e->dword;
		int id = _KeCreateSharedMem(backbufkey, 0, 0);
		void* buffer = _KeObtainSharedMem(id, 0, 0);
		win->app->backbufkey = backbufkey;
		win->app->fb = buffer;
		win->buffer = (uint32_t*)buffer;
		win->info->windowReady = 1;
		UseSharedBuffer();
		ChWindowPaint(win);
		break;
	}
	default:
		break;
	}
	memset(e, 0, sizeof(PostEvent));
}

int main(int argc, char* argv[]) {
	app = ChitralekhaStartApp(argc, argv);

	/* a throwaway canvas, only to learn the screen size */
	ChCanvas* canv = ChCreateCanvas(100, 100);
	int screen_w = canv->screenWidth;
	int screen_h = canv->screenHeight;
	free(canv);

	int winW = screen_w;
	int winH = screen_h - NAMDAPHA_SIZE;
	win = ChCreateWindow(app,
						 WINDOW_FLAG_MOVABLE | WINDOW_FLAG_NON_RESIZABLE | WINDOW_FLAG_STYLE_KNOT,
						 (char*)"Phulbari",
						 0,
						 0,
						 winW,
						 winH);
	/* the hem below the scene */
	win->color = PB_C_RED_DEEP;
	UseSharedBuffer();

	areaX = CHITRALEKHA_WINDOW_DEFAULT_PAD_X;
	areaY = CHITRALEKHA_WINDOW_DEFAULT_PAD_Y;
	areaW = winW - CHITRALEKHA_WINDOW_DEFAULT_PAD_X * 2;
	areaH = winH - CHITRALEKHA_WINDOW_DEFAULT_PAD_Y - HEM_PX;

	if (!PbInit(areaW, areaH)) {
		_KePrint("[phulbari]: screen too small or out of memory \r\n");
		ChWindowCloseWindow(win);
		return 1;
	}
	_KePrint("[phulbari]: %dx%d, demo heap %d bytes \r\n", areaW, areaH, (int)PbHeapBytes());

	_chromePaint = win->ChWinPaint;
	win->ChWinPaint = PhulbariPaint;
	_ready = true;
	ChWindowPaint(win);

	PostEvent e;
	memset(&e, 0, sizeof(PostEvent));
	setjmp(win->jump);

	while (1) {
		int err = _KeFileIoControl(app->postboxfd, POSTBOX_GET_EVENT, &e);
		if (e.type)
			PhulbariHandleMessage(&e);
		if (err != POSTBOX_NO_EVENT)
			continue; /* drain every queued event first */

		/* no cuts while minimised or paused: nothing is drawn or composed */
		if (!win->info->hide && !_paused)
			PhulbariCut();
		_KeProcessSleep(PB_CUT_MS);
	}
}
