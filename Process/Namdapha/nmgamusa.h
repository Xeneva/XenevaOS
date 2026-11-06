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
 * nmgamusa.h -- the Gamusa look for Namdapha (the default).
 *
 * Build with -DNAMDAPHA_CLASSIC to get the old glass bar back; both looks
 * are compiled in and NmGamusa() picks one, so nothing else changes.
 *
 * The bar is cotton paper with a woven border: a red band and its light
 * companion thread along the top, a row of small lozenges along the
 * bottom. Running apps carry a sand lozenge, the focused one the red
 * thread, the Go logo is dyed red. The bar is opaque, so it is created
 * without WINDOW_FLAG_GLASS (no blur buffers in the compositor).
 */

#ifndef __NM_GAMUSA_H__
#define __NM_GAMUSA_H__

#include <stdint.h>
#include <chitralekha.h>
#include <widgets/window.h>

struct _NamdaphaButton_;
struct _buttoninfo_;

#ifdef NAMDAPHA_CLASSIC
static inline bool NmGamusa() {
	return false;
}
#else
static inline bool NmGamusa() {
	return true;
}
#endif

/* geometry inside the 65 px bar */
#define NM_GM_RADIUS	6  /* top corners                           */
#define NM_GM_ICON_MAXW 46 /* icons are shrunk once to fit this box  */
#define NM_GM_ICON_MAXH 40
#define NM_GM_CLOCK_Y	6
#define NM_GM_CLOCK_H	50

#define NM_COLOR_CREAM 0xFFDFD2C7

/* start-up: rasterise the two glyph sets (clock digits, small text) and
 * close the extra face again; ~4 KB stays resident */
void NmGmInit(ChFont* baseFont);

/* the bar: paper, woven border, separator; the buttons paint on top */
void NmGmPaintBar(ChWindow* win);

/* one task button (icon + running / focused marker) */
void NmGmButtonPaint(struct _NamdaphaButton_* button, ChWindow* win);

/* the clock: "9:34 am" over "thu, 5 nov" */
void NmGmClockPaint(struct _NamdaphaButton_* button,
					ChWindow* win,
					int hour24,
					int minute,
					int day,
					int month,
					int year);

/* the Go logo dyed with the red thread, keeping its own shading */
void NmGmGoPaint(struct _NamdaphaButton_* button, ChWindow* win, struct _buttoninfo_* logo);

/* drop an icon's shrunk copy (called when the icon is released) */
void NmGmIconRelease(struct _buttoninfo_* info);

#endif
