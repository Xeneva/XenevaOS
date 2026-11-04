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
 * livetile.cpp -- launcher side of the live tile protocol (see
 * Libs/Chitralekha/livetile.h for the client API and event layout).
 *
 * An update costs: O(log n) lookup + one tile re-render (only if the
 * tile is pinned) + one tile-sized present (only if the launcher is
 * visible). While hidden the tile is just marked dirty and re-rendered
 * lazily on the next blit, so background apps never cause paints.
 */

#include "launcher.h"
#include <livetile.h>
#include <string.h>
#include <sys/_keproc.h>

void XlLiveInit() {
	if (_KeProcessTokenAddSelf(XELNCH_PROCESS_TOKEN) != 0)
		_KePrint("[XELnch]: live tile token %d already taken \r\n", XELNCH_PROCESS_TOKEN);
}

/* copy one NUL terminated string out of src[0..srcLen), returns bytes used */
static int _take(const char* src, int srcLen, char* dst, int dstSize) {
	int i = 0;
	while (i < srcLen && src[i]) {
		if (i < dstSize - 1)
			dst[i] = src[i];
		i++;
	}
	dst[i < dstSize - 1 ? i : dstSize - 1] = '\0';
	return i < srcLen ? i + 1 : i;
}

static void _refresh(XlApp* a) {
	a->cacheDirty = 1;
	if (!a->pinned)
		return;
	ChWindow* w = XELauncherGetMainWin();
	bool visible = w && w->info && !__atomic_load_n(&w->info->hide, __ATOMIC_ACQUIRE);
	if (visible && !xlnch.animating && xlnch.view == XL_VIEW_TILES)
		XlPaintTileAt((uint8_t)(a - xlnch.apps));
}

void XlLiveHandle(PostEvent* e) {
	int pos = XlFindByPathHashFirst(e->dword);
	if (pos < 0)
		return;
	for (int i = pos; i < xlnch.appCount; i++) {
		XlApp* a = &xlnch.apps[xlnch.byPath[i]];
		if (a->pathHash != e->dword)
			break;
		XlLive* lv = &a->live;
		if (e->type == XELNCH_MSG_LIVE_CLEAR) {
			if (!lv->valid)
				continue;
			memset(lv, 0, sizeof(XlLive));
		} else {
			const int cap = (int)sizeof(e->charValue3);
			lv->valid = 1;
			lv->layout = (uint8_t)(e->dword2 <= XELNCH_TILE_NOWPLAYING ? e->dword2 : XELNCH_TILE_TEXT);
			lv->badge = (uint16_t)(e->dword3 > 0xFFFF ? 0xFFFF : e->dword3);
			lv->progress = (uint16_t)(e->dword4 > 0xFFFF ? 0xFFFF : e->dword4);
			int used = _take(e->charValue3, cap, lv->line1, XL_LIVE_TEXT_MAX);
			_take(e->charValue3 + used, cap - used, lv->line2, XL_LIVE_TEXT_MAX);
		}
		_refresh(a);
	}
}
