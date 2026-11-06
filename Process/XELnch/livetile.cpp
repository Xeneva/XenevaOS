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
 * An update costs: O(log n) lookup, a copy into the app's live slot and
 * the LuitFlow ring, and -- only while the launcher is visible -- one
 * card-sized repaint (a changed line slides in as a ticker). While hidden
 * nothing is painted; the next show paints the current state.
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

/* the one line a card shows: "line1  .  line2" */
void XlLiveLine(const XlApp* a, char* out, int size) {
	out[0] = 0;
	if (!a->live.valid)
		return;
	strncpy(out, a->live.line1, size - 1);
	out[size - 1] = 0;
	if (a->live.line2[0] && (int)(strlen(out) + strlen(a->live.line2) + 6) < size) {
		if (out[0])
			strcat(out, "  .  ");
		strcat(out, a->live.line2);
	}
}

static bool _is_calendar(const XlApp* a) {
	static uint32_t h;
	if (!h)
		h = XelnchHash("/calendr.exe");
	return a->pathHash == h;
}

/* repaint after a change: a changed line slides in as a ticker */
static void _show(XlApp* a, bool changed) {
	uint8_t ai = (uint8_t)(a - xlnch.apps);
	if (!XlVisible() || xlnch.animating) {
		a->tick = 255;
		return;
	}
	if (!a->pinned || xlnch.view != XL_VIEW_CARDS)
		return;
	if (changed && a->tickOld[0])
		XlTickStart(ai);
	else
		XlPaintCard(ai);
}

/* apply new live content; returns true when the shown line changed */
static bool
_apply(XlApp* a, bool clear, int layout, const char* l1, const char* l2, int badge, int progress) {
	XlLive* lv = &a->live;
	char before[sizeof(a->tickOld)];
	XlLiveLine(a, before, sizeof(before));
	uint16_t oldBadge = lv->valid ? lv->badge : 0;
	uint16_t oldProg = lv->valid ? lv->progress : XELNCH_NO_PROGRESS;
	if (clear) {
		memset(lv, 0, sizeof(XlLive));
		lv->progress = XELNCH_NO_PROGRESS;
	} else {
		lv->valid = 1;
		lv->layout = (uint8_t)(layout <= XELNCH_TILE_NOWPLAYING ? layout : XELNCH_TILE_TEXT);
		lv->badge = (uint16_t)(badge > 0xFFFF ? 0xFFFF : (badge < 0 ? 0 : badge));
		lv->progress = (uint16_t)(progress > 0xFFFF ? 0xFFFF : progress);
		strncpy(lv->line1, l1 ? l1 : "", XL_LIVE_TEXT_MAX - 1);
		lv->line1[XL_LIVE_TEXT_MAX - 1] = 0;
		strncpy(lv->line2, l2 ? l2 : "", XL_LIVE_TEXT_MAX - 1);
		lv->line2[XL_LIVE_TEXT_MAX - 1] = 0;
	}
	char after[sizeof(a->tickOld)];
	XlLiveLine(a, after, sizeof(after));
	bool changed = strcmp(before, after) != 0;
	if (changed)
		strcpy(a->tickOld, before); /* the ticker slides this one away */
	return changed || oldBadge != (lv->valid ? lv->badge : 0) ||
		   oldProg != (lv->valid ? lv->progress : XELNCH_NO_PROGRESS);
}

/* set from inside the launcher (the calendar card reads /CALEVT itself) */
void XlLiveSet(uint8_t ai, int layout, const char* l1, const char* l2, int badge, int progress) {
	XlApp* a = &xlnch.apps[ai];
	bool clear = !l1 && !l2;
	if (clear && !a->live.valid)
		return;
	char before[sizeof(a->tickOld)];
	XlLiveLine(a, before, sizeof(before));
	if (_apply(a, clear, layout, l1, l2, badge, progress)) {
		char after[sizeof(a->tickOld)];
		XlLiveLine(a, after, sizeof(after));
		_show(a, strcmp(before, after) != 0);
	}
}

void XlLiveHandle(PostEvent* e) {
	int pos = XlFindByPathHashFirst(e->dword);
	if (pos < 0)
		return;
	for (int i = pos; i < xlnch.appCount; i++) {
		XlApp* a = &xlnch.apps[xlnch.byPath[i]];
		if (a->pathHash != e->dword)
			break;
		bool clear = e->type == XELNCH_MSG_LIVE_CLEAR;
		if (clear && !a->live.valid)
			continue;
		char l1[XL_LIVE_TEXT_MAX], l2[XL_LIVE_TEXT_MAX];
		l1[0] = l2[0] = 0;
		if (!clear) {
			const int cap = (int)sizeof(e->charValue3);
			int used = _take(e->charValue3, cap, l1, XL_LIVE_TEXT_MAX);
			_take(e->charValue3 + used, cap - used, l2, XL_LIVE_TEXT_MAX);
		}
		char before[sizeof(a->tickOld)];
		XlLiveLine(a, before, sizeof(before));
		bool any = _apply(a, clear, (int)e->dword2, l1, l2, (int)e->dword3, (int)e->dword4);
		/* calendar events reach the ribbon from /CALEVT, not as history */
		if (!_is_calendar(a) && a->live.valid)
			XlFlowLive((uint8_t)(a - xlnch.apps), &a->live);
		if (any) {
			char after[sizeof(a->tickOld)];
			XlLiveLine(a, after, sizeof(after));
			_show(a, strcmp(before, after) != 0);
		}
	}
	if (!XlVisible() || xlnch.animating)
		return;
	if (xlnch.flowOpen) {
		XlFlowRefresh(); /* also re-reads /CALEVT when the calendar changed */
		XlFlowPaint(true);
	} else {
		XlFlowPaintToggle(true);
	}
}
