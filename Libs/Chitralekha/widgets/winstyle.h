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
 * winstyle.h -- window chrome style modules (internal to Chitralekha).
 *
 * A window picks its chrome with bits 13-14 of its flags
 * (WINDOW_STYLE_MASK, see window.h). Style 0 is the classic chrome in
 * defaultwindow.cpp and is never routed through here, so the default
 * paint path stays exactly as it was. Every other style is one table of
 * function pointers; lookup is a single array index.
 */

#ifndef __CH_WINSTYLE_H__
#define __CH_WINSTYLE_H__

#include "window.h"

typedef struct _ch_win_style_ops_ {
	const char* name;
	int cornerRadius; /* rounded corner radius applied by ChWindowPaint */
	/* take over the window: paint hook, global controls, colours */
	void (*Install)(ChWindow* win);
	/* paint the 26px title bar into the canvas (no present) */
	void (*PaintTitlebar)(ChWindow* win);
	/* win->focused has changed: repaint and present the title bar */
	void (*Focus)(ChWindow* win);
} ChWindowStyleOps;

/* NULL for the classic chrome */
const ChWindowStyleOps* ChWindowStyleOf(ChWindow* win);
/* called once by ChCreateWindow, after the classic controls exist */
void ChWindowStyleInstall(ChWindow* win);

/* knotwindow.cpp */
extern const ChWindowStyleOps ChKnotWindowStyle;

#endif
