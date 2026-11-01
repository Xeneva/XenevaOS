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

#ifndef __PAPERASH_H__
#define __PAPERASH_H__

/**
 * Paper ash: a closing window crumbles from its top edge into small
 * flakes. Each flake heats up (ember glow), breaks off, chars to grey ash,
 * tumbles, shrinks, rises faster and faster with a sideways wobble and
 * fades out. Flakes that have not broken off yet are the untouched window,
 * so the first frame is identical to the window that was just closed.
 *
 * The window's pixels are snapshotted by DeodhaiPaperAshStart, which the
 * close handler calls before DeodhaiCloseWindow unmaps the shared memory;
 * the close itself (and the client's DEODHAI_REPLY_WINDOW_CLOSED) is not
 * delayed. Everything after that plays from the snapshot.
 *
 * Shared by both compositors:
 *  - 2D: XRComposeFrame -> DeodhaiComposePaperAsh (main.cpp)
 *  - 3D: XRComposeFrame3D passes a projector; flakes stay on the window's
 *    plane (rising along its own "up"), with perspective
 *
 * Cost per frame, P flakes:
 *  - O(P) motion, four flakes per NEON step (SoA arrays); flakes are kept
 *    sorted by break-off time (counting sort, O(P) once), so only the
 *    flakes in the air are touched, and per column the attached part is
 *    one pointer that only moves forward
 *  - O(flake pixels) to draw: every flake is an octagon whose spans are
 *    solved per row from 4 slab inequalities (no per-pixel inside test);
 *    texels are gathered with NEON index math, coloured (ember / char /
 *    fade) and blended four pixels per NEON step
 *  - repaint area via a 32 px tile bitmap (dirtytiles.h): marking is
 *    O(tiles touched), emitting rectangles is a bit scan, O(tiles / 64 +
 *    rectangles) -- independent of how many flakes overlap
 */

#include <stdint.h>
#include <chitralekha.h>
#include "window.h"

#define DEODHAI_PAPERASH_MAX 4 /* windows burning at once */

/* 3D only: the window plane in world space at the moment it was closed --
 * window texel (u, v) sits at o + u * u_axis + v * v_axis; n points away
 * from the viewer */
typedef struct _paper_ash_plane_ {
	bool valid;
	float o[3];
	float u[3];
	float v[3];
	float n[3];
} PaperAshPlane;

/**
 * Projects a point of the window plane: local (lx, ly) in window texels,
 * lz along the plane normal (negative = towards the viewer).
 * out[0..1] = screen position, out[2..5] = d(screen)/d(lx, ly) as
 * {dsx/dlx, dsx/dly, dsy/dlx, dsy/dly}. false: behind the near plane.
 */
typedef bool (*PaperAshProjectFn)(const PaperAshPlane* plane, float lx, float ly, float lz, float* out);

/* per flake colour transform, applied as
 *   rgb   = sat(sat(c * (ms + 1) >> 8 + luma * (ml + 1) >> 8) + add)
 *   alpha = a * (am + 1) >> 8
 * ms = 255, ml = 0, add = 0, am = 255 is the identity */
typedef struct _paper_ash_tint_ {
	uint8_t ms;
	uint8_t ml;
	uint8_t am;
	uint8_t add[3]; /* b, g, r */
} PaperAshTint;

typedef struct _paper_ash_ {
	bool used;
	int dying; /* 0 burning, 1 last footprint reported -- freed on the next step */
	uint32_t handle;
	int x, y, w, h; /* the window's 2D rectangle when it was closed */
	uint32_t* snap; /* w x h */
	size_t snapBytes;
	PaperAshPlane plane;
	/* where the ash sits in the painter order: windows above it are drawn
	 * over it, like they were over the window */
	bool hasAbove;		  /* 2D: a window was stacked right above */
	uint32_t aboveHandle; /* ... this one */
	float depth;		  /* 3D: distance of the plane centre from the viewer */
	bool projected;		  /* drawn through the 3D projector this frame */
	float t;		/* ms since the close */
	uint64_t last;
	int S;			  /* flake size in texels */
	int cols, rows, n; /* flake grid */
	/* flake SoA, sorted by break-off time (counting sort) */
	float *detach, *invLife, *hx, *hy, *hu, *hv, *vx, *vy, *acc, *spin, *flip, *phase, *wob,
		*dz, *glow;
	/* this frame */
	float *ox, *oy, *oz, *rc, *rs, *fl, *k;
	float *sx, *sy, *a00, *a01, *a10, *a11; /* screen centre and local -> screen */
	int16_t *bx0, *by0, *bx1, *by1;			/* screen footprint (bx1 <= bx0: none) */
	int32_t* grid;							/* row * cols + col -> flake index */
	int32_t* attach;						/* per column: first row still attached */
	int32_t* heat;							/* per column: first row not glowing yet */
	int first, next; /* [first, next): flakes that broke off and may still fly */
	void* block;
	size_t blockBytes;
	Rect bbox; /* screen bounds of everything drawn this frame */
	bool bboxValid;
} PaperAsh;

/**
 * @brief DeodhaiPaperAshInitialise -- reset the module
 */
extern void DeodhaiPaperAshInitialise();

/**
 * @brief DeodhaiPaperAshEligible -- windows that burn: visible, drawn at
 * least once, not always-on-top, popup, static or zoomed
 */
extern bool DeodhaiPaperAshEligible(Window* win);

/**
 * @brief DeodhaiPaperAshStart -- snapshot a closing window and start its
 * ash animation. Call before DeodhaiCloseWindow.
 * @param win -- Pointer to the window being closed
 * @param plane -- its 3D plane (Deodhai3DWindowPlane), or NULL / invalid
 * @return true if it burns; false: not eligible or no free slot
 */
extern bool DeodhaiPaperAshStart(Window* win, const PaperAshPlane* plane);

/**
 * @brief DeodhaiPaperAshActive -- true while anything needs drawing
 */
extern bool DeodhaiPaperAshActive();

/**
 * @brief DeodhaiPaperAshStep -- advance every animation to `now` and mark
 * the old and new footprints; call once per frame, before drawing
 * @param project -- 3D projector, NULL for 2D (planes are then ignored)
 */
extern void DeodhaiPaperAshStep(uint64_t now, int screen_w, int screen_h, PaperAshProjectFn project);

/**
 * @brief DeodhaiPaperAshDirty -- screen rectangles to repaint this frame
 * (clears the marks)
 * @param rects -- receives a pointer to the module's rectangle list
 * @return number of rectangles
 */
extern int DeodhaiPaperAshDirty(const Rect** rects);

/**
 * @brief DeodhaiPaperAshDraw -- draw every animation, clipped to `clip`
 * (NULL = the screen). Call once after every dirty rectangle has been
 * repainted: everything drawn lies inside them.
 */
extern void DeodhaiPaperAshDraw(ChCanvas* canvas, const Rect* clip);

/**
 * @brief DeodhaiPaperAshDrawOne -- draw slot `index` only (compositors
 * that interleave the ash with the windows above it)
 */
extern void DeodhaiPaperAshDrawOne(ChCanvas* canvas, int index, const Rect* clip);

/* a slot that is drawn this frame */
static inline bool DeodhaiPaperAshDrawn(const PaperAsh* a) {
	return a && a->used && !a->dying && a->bboxValid;
}

/**
 * @brief DeodhaiPaperAshGet -- slot `index` (0..MAX-1), NULL if free
 */
extern PaperAsh* DeodhaiPaperAshGet(int index);

/**
 * @brief DeodhaiPaperAshTintRun -- apply a flake colour transform to n
 * pixels in place; four pixels per NEON step
 */
extern void DeodhaiPaperAshTintRun(uint32_t* run, int n, const PaperAshTint* tint);

#endif
