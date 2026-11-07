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
 * gmstyle.cpp -- pixel helpers shared by the flat Gamusa modules.
 * All O(pixels drawn), integer only, clipped to the canvas.
 */

#include "gmstyle.h"
#include <string.h>

uint32_t ChGmBlend(uint32_t dst, uint32_t src, uint32_t a) {
	if (a >= 255)
		return src | 0xFF000000u;
	if (a == 0)
		return dst | 0xFF000000u;
	uint32_t ia = 255 - a;
	uint32_t r = ((src >> 16) & 0xFF) * a + ((dst >> 16) & 0xFF) * ia;
	uint32_t g = ((src >> 8) & 0xFF) * a + ((dst >> 8) & 0xFF) * ia;
	uint32_t b = (src & 0xFF) * a + (dst & 0xFF) * ia;
	r = (r + 1 + (r >> 8)) >> 8;
	g = (g + 1 + (g >> 8)) >> 8;
	b = (b + 1 + (b >> 8)) >> 8;
	return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static inline void _px(ChCanvas* c, int x, int y, uint32_t col) {
	if (x < 0 || y < 0 || x >= (int)c->canvasWidth || y >= (int)c->canvasHeight)
		return;
	c->buffer[(size_t)y * c->canvasWidth + x] = col;
}

void ChGmFill(ChCanvas* c, int x, int y, int w, int h, uint32_t col) {
	if (!c || !c->buffer)
		return;
	int x1 = x + w, y1 = y + h;
	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	if (x1 > (int)c->canvasWidth)
		x1 = (int)c->canvasWidth;
	if (y1 > (int)c->canvasHeight)
		y1 = (int)c->canvasHeight;
	if (x1 <= x || y1 <= y)
		return;
	uint32_t* row = c->buffer + (size_t)y * c->canvasWidth;
	for (int j = y; j < y1; j++, row += c->canvasWidth)
		for (int i = x; i < x1; i++)
			row[i] = col;
}

void ChGmFrame(ChCanvas* c, int x, int y, int w, int h, uint32_t col) {
	if (w <= 0 || h <= 0)
		return;
	ChGmFill(c, x, y, w, 1, col);
	ChGmFill(c, x, y + h - 1, w, 1, col);
	ChGmFill(c, x, y + 1, 1, h - 2, col);
	ChGmFill(c, x + w - 1, y + 1, 1, h - 2, col);
}

/* a lozenge is a run per row: half width = r - |dy| */
void ChGmLozenge(ChCanvas* c, int cx, int cy, int r, uint32_t col) {
	for (int dy = -r; dy <= r; dy++) {
		int hw = r - (dy < 0 ? -dy : dy);
		ChGmFill(c, cx - hw, cy + dy, 2 * hw + 1, 1, col);
	}
}

void ChGmLozengeOutline(ChCanvas* c, int cx, int cy, int r, uint32_t col) {
	for (int dy = -r; dy <= r; dy++) {
		int hw = r - (dy < 0 ? -dy : dy);
		_px(c, cx - hw, cy + dy, col);
		_px(c, cx + hw, cy + dy, col);
	}
}

/*
 * ChGmLozengeStitched -- satin stitch: the threads run along one
 * diagonal and every third one catches the light (sheen); the edge is a
 * back-stitch one shade deeper whose every third stitch shows a gap.
 */
void ChGmLozengeStitched(
	ChCanvas* c, int cx, int cy, int r, uint32_t thread, uint32_t edge, uint32_t sheen) {
	uint32_t gap = ChGmBlend(edge, thread, 128);
	for (int dy = -r; dy <= r; dy++) {
		int ady = dy < 0 ? -dy : dy;
		int hw = r - ady;
		for (int dx = -hw; dx <= hw; dx++) {
			uint32_t col;
			if (dx == -hw || dx == hw)
				col = (ady % 3 == 1) ? gap : edge;
			else
				col = (((dx - dy) % 3 + 3) % 3 == 0) ? sheen : thread;
			_px(c, cx + dx, cy + dy, col);
		}
	}
}

void ChGmThread(ChCanvas* c, int x, int y, int w, uint32_t col, uint32_t col2) {
	ChGmFill(c, x, y, w, 2, col);
	ChGmFill(c, x, y + 3, (w * 3) / 5, 1, col2);
}

int ChGmTextWidth(ChFont* f, const char* s) {
	if (!f || !s)
		return 0;
	ChFontSetSize(f, CH_GM_TEXT_SIZE);
	return (int)ChFontGetWidth(f, (char*)s);
}

void ChGmText(ChCanvas* c, ChFont* f, const char* s, int x, int baseline, uint32_t col) {
	if (!f || !s || !s[0])
		return;
	ChFontSetSize(f, CH_GM_TEXT_SIZE);
	ChFontDrawText(c, f, (char*)s, x, baseline, CH_GM_TEXT_SIZE, col);
}

void ChGmTextClipped(
	ChCanvas* c, ChFont* f, const char* s, int x, int baseline, uint32_t col, ChRect* clip) {
	if (!f || !s || !s[0])
		return;
	ChFontSetSize(f, CH_GM_TEXT_SIZE);
	ChFontDrawTextClipped(c, f, (char*)s, x, baseline, col, clip);
}
