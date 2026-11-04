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
 * anim.cpp -- Metro "turnstile" for the tile grid.
 *
 * Every visible tile swings about the grid's left edge, in perspective,
 * with a per-tile delay that grows down the rows and across the columns
 * (top-left leaves first). On launch the tapped tile is held back and
 * leaves last, like Windows Phone.
 *
 *   exit : 0 -> 90 deg, ease-in  (t^2),     170 ms per tile
 *   enter: 90 -> 0 deg, ease-out ((1-t)^3), 260 ms per tile
 *
 * Frames are time based (elapsed ms), so a slow frame skips ahead rather
 * than stretching the animation. Cost per frame: O(visible tile pixels);
 * no allocation (the item table lives on the stack, <= 64 entries).
 */

#include "launcher.h"
#include <sys/_keproc.h>
#include <sys/_ketime.h>
#include <stdlib.h>

#define XL_ANIM_ROW_MS	 26
#define XL_ANIM_COL_MS	 12
#define XL_ANIM_EXIT_MS	 170
#define XL_ANIM_ENTER_MS 260
#define XL_ANIM_LAUNCH_HOLD_MS 70
#define XL_FRAME_MS		 12
#define XL_MAX_FRAMES	 90

typedef struct {
	uint8_t app;
	int16_t x, y;
	int16_t delay;
} XlAnimItem;

static int _cmp_delay(const void* a, const void* b) {
	return ((const XlAnimItem*)a)->delay - ((const XlAnimItem*)b)->delay;
}

void XlAnimTiles(int mode, int launchApp) {
	const XlMetrics* m = &xlnch.m;
	const XlRect* v = &m->view;
	if (xlnch.view != XL_VIEW_TILES || xlnch.pinCount == 0) {
		if (mode == XL_ANIM_ENTER)
			XlPaintView();
		return;
	}

	XlAnimItem items[XL_MAX_APPS];
	int n = 0, maxDelay = 0, launchItem = -1;
	int firstRow = xlnch.scrollTiles / m->pitch;
	for (int i = 0; i < xlnch.pinCount; i++) {
		uint8_t ai = xlnch.pinOrder[i];
		XlApp* a = &xlnch.apps[ai];
		if (a->row < 0)
			continue;
		XlRect t;
		XlLayoutTileRect(a, &t);
		if (t.y >= v->y + v->h || t.y + t.h <= v->y)
			continue;
		if (a->cacheDirty)
			XlTileRender(a);
		int r = a->row - firstRow;
		if (r < 0)
			r = 0;
		items[n].app = ai;
		items[n].x = (int16_t)t.x;
		items[n].y = (int16_t)t.y;
		items[n].delay = (int16_t)(r * XL_ANIM_ROW_MS + a->col * XL_ANIM_COL_MS);
		if (items[n].delay > maxDelay)
			maxDelay = items[n].delay;
		if (ai == launchApp)
			launchItem = n;
		n++;
	}
	int dur = mode == XL_ANIM_EXIT ? XL_ANIM_EXIT_MS : XL_ANIM_ENTER_MS;
	if (launchItem >= 0) {
		items[launchItem].delay = (int16_t)(maxDelay + XL_ANIM_LAUNCH_HOLD_MS);
		maxDelay = items[launchItem].delay;
	}
	int total = maxDelay + dur;
	/* painter's order: the tile turned furthest away is drawn first.
	 * exit -> ascending delay (early tiles have turned most),
	 * enter -> descending delay (early tiles are already flat). O(n log n) */
	qsort(items, n, sizeof(XlAnimItem), _cmp_delay);

	xlnch.animating = true;
	uint64_t t0 = _KeGetCurrentMS();
	for (int frame = 0; frame < XL_MAX_FRAMES; frame++) {
		int el = (int)(_KeGetCurrentMS() - t0);
		XlViewClear();
		for (int k = 0; k < n; k++) {
			int i = mode == XL_ANIM_EXIT ? k : n - 1 - k;
			int lt = el - items[i].delay;
			int ang;
			if (mode == XL_ANIM_EXIT) {
				if (lt >= dur)
					continue; /* already gone */
				int p = lt <= 0 ? 0 : lt * 1000 / dur;
				ang = 900 * p / 1000 * p / 1000;
			} else {
				if (lt <= 0)
					continue; /* not arrived yet */
				int p = lt >= dur ? 1000 : lt * 1000 / dur;
				int q = 1000 - p;
				ang = (int)((int64_t)900 * q * q * q / 1000000000LL);
			}
			XlTileBlitTurn(xlnch.canv,
						   &xlnch.apps[items[i].app],
						   items[i].x,
						   items[i].y,
						   v->x,
						   ang,
						   XL_BG,
						   v);
		}
		XlPresent(v->x, v->y, v->w, v->h);
		if (el >= total)
			break;
		_KeProcessSleep(XL_FRAME_MS);
	}
	xlnch.animating = false;

	if (mode == XL_ANIM_EXIT) {
		XlViewClear();
		XlPresent(v->x, v->y, v->w, v->h);
	} else {
		XlPaintView(); /* exact final frame, incl. hover overlay */
	}
}
