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

#ifndef __PAPERFLY_H__
#define __PAPERFLY_H__

/**
 * Paper flight: hiding a window rolls it up from its left edge into a paper
 * tube that flies off to the right of the screen on a gentle arc; unhiding
 * plays the same flight backwards -- the tube flies in from the right and
 * unrolls onto the window's place, and only then is the window shown again.
 * Toggling mid-flight turns the paper around from where it is.
 *
 * The module is shared by both compositors:
 *  - 2D: XRComposeFrame calls DeodhaiComposePaperFly (main.cpp), which
 *    repaints DeodhaiPaperFlyDirty() and draws with DeodhaiPaperFlyDraw()
 *  - 3D: XRComposeFrame3D applies the same per-column roll table through
 *    the window's own perspective mapping, so the paper rolls and flies in
 *    world space
 *
 * The window is snapshotted when the flight starts, so a client closing
 * mid-flight cannot pull the pixels away.
 *
 * Cost per frame for one paper of w x h:
 *  - O(w) to rebuild the column table (roll geometry, lighting)
 *  - O(painted area) to draw: the flat part of each row is one NEON blend
 *    straight from the snapshot row, the tube (about 2 x radius columns)
 *    is gathered and shaded four pixels per NEON step
 */

#include <stdint.h>
#include <chitralekha.h>
#include "window.h"

#define DEODHAI_PAPERFLY_MAX 4 /* papers in the air at once */

/* one output column of the paper: which window column it shows (-1 =
 * nothing), lighting (255 = as is) and whether it is the paper's back */
typedef struct _paper_column_ {
	int16_t u;
	uint8_t shade;
	uint8_t back;
} PaperColumn;

typedef struct _paper_fly_ {
	bool used;
	int dying; /* 0 flying, 2 landed (drawn once more), 1 cancelled,
				* 3 last footprint reported -- freed on the next step */
	Window* win;
	uint32_t handle;
	int x, y, w, h; /* window rectangle the paper leaves from / lands on */
	uint32_t* snap; /* w x h snapshot */
	size_t snapBytes;
	float t;  /* 0 = flat in place .. 1 = gone off the right edge */
	int dir;  /* +1 hiding, -1 showing */
	uint64_t last;
	float span; /* 2D flight distance in pixels */
	/* geometry of the current frame, in paper columns (0 = left edge) */
	float rolled; /* paper length wound onto the tube */
	float radius; /* tube radius */
	int uMin, uMax; /* visible column range */
	int flatStart, flatEnd; /* columns that are the window as is */
	float flyK;	 /* 0..1 along the flight */
	float liftK; /* 0..1 of the arc */
	int dx, dy;	 /* 2D screen offset this frame */
	float arc;	 /* arc height in pixels */
	PaperColumn* col; /* uMax - uMin entries */
	int colCap;
	Rect bbox;
	bool bboxValid;
	Rect lastBBox;
	bool lastValid;
} PaperFly;

/**
 * @brief DeodhaiPaperFlyInitialise -- reset the module
 */
extern void DeodhaiPaperFlyInitialise();

/**
 * @brief DeodhaiPaperFlyStart -- start a flight for a window, or turn an
 * existing one around
 * @param win -- Pointer to window
 * @param hiding -- true: roll up and fly out, false: fly in and unroll
 * @return true if the window is animating now. false: not eligible
 * (always-on-top, popup, static, zoomed) or no slot -- act instantly.
 */
extern bool DeodhaiPaperFlyStart(Window* win, bool hiding);

/**
 * @brief DeodhaiPaperFlyEligible -- true for windows that fly (not
 * always-on-top, popup, static or zoomed)
 */
extern bool DeodhaiPaperFlyEligible(Window* win);

/**
 * @brief DeodhaiPaperFlyBusy -- true while the window is in the air
 */
extern bool DeodhaiPaperFlyBusy(Window* win);

/**
 * @brief DeodhaiPaperFlyCancel -- the window is being closed: a paper
 * flying in is dropped, one flying out keeps going on its snapshot
 */
extern void DeodhaiPaperFlyCancel(Window* win);

/**
 * @brief DeodhaiPaperFlyActive -- true while anything needs drawing
 */
extern bool DeodhaiPaperFlyActive();

/**
 * @brief DeodhaiPaperFlyStep -- advance every flight to `now`; call once
 * per frame, before drawing
 * @param shown -- receives windows whose fly-in has landed (show them now)
 * @return number of entries written to `shown`
 */
extern int DeodhaiPaperFlyStep(
	uint64_t now, int screen_w, int screen_h, Window** shown, int max_shown);

/**
 * @brief DeodhaiPaperFlyDirty -- 2D screen rectangles to repaint this
 * frame (old and new footprint of every paper)
 */
extern int DeodhaiPaperFlyDirty(Rect* rects, int max);

/**
 * @brief DeodhaiPaperFlyDraw -- 2D: draw every paper clipped to `clip`
 */
extern void DeodhaiPaperFlyDraw(ChCanvas* canvas, const Rect* clip);

/**
 * @brief DeodhaiPaperFlyGet -- flight slot `index` (0..MAX-1), NULL if free
 */
extern PaperFly* DeodhaiPaperFlyGet(int index);

/* a slot whose paper is still drawn this frame */
static inline bool DeodhaiPaperFlyDrawn(const PaperFly* fly) {
	return fly && (fly->dying == 0 || fly->dying == 2);
}

/**
 * @brief DeodhaiPaperFlyTexel -- the pixel a paper column shows on row v
 * (0 = transparent); the back of the paper is paper white with the
 * content faintly showing through
 */
extern uint32_t DeodhaiPaperFlyTexel(const PaperFly* fly, const PaperColumn* col, int v);

/**
 * @brief DeodhaiPaperFlyShadeRun -- multiply n pixels by per-pixel shade
 * (0..255, alpha kept); four pixels per NEON step
 */
extern void DeodhaiPaperFlyShadeRun(uint32_t* run, const uint8_t* shade, int n);

#endif
