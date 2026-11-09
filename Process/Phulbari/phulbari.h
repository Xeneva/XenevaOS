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
 * Phulbari -- a cut-motion demo for XenevaOS.
 *
 * The scene is drawn on a coarse grid of palette indices, one byte per
 * cell, and every cell is shown as a small woven swatch (a warp or weft
 * thread, alternating like plain-weave cloth). Only cells that changed
 * since the last cut are written to the window, so the cost of a cut is
 * the cost of what moved.
 *
 * This engine has no Xeneva dependencies: it draws into any 32-bit ARGB
 * buffer. main.cpp is the XenevaOS / Chitralekha front end.
 */

#ifndef __PHULBARI_H__
#define __PHULBARI_H__

#include <stdint.h>
#include <stddef.h>

/* gamusa palette (ARGB), shared with Seek (Process/XELnch/launcher.h) */
#define PB_C_INK		0xFF231F1Cu
#define PB_C_INK_SOFT	0xFF6B6259u
#define PB_C_RED		0xFFC8102Eu
#define PB_C_RED_DEEP	0xFF8E1A22u
#define PB_C_MUGA		0xFFC99A2Eu
#define PB_C_MUGA_DEEP	0xFF7A5A12u
#define PB_C_IVORY		0xFFEDE3CFu
#define PB_C_IVORY_SHADE 0xFFDDD0B6u
#define PB_C_WEAVE_LINE 0xFFE6A9AEu
#define PB_C_CHARCOAL	0xFF2B2622u
#define PB_C_LEAF		0xFF7D8A4Eu
#define PB_C_WHITE		0xFFFFFFFFu
#define PB_C_SKIN		0xFFD39B73u

/* one "cut" of the animation: about 6.7 drawings a second, a little
 * slower than the 8 of cut-out and stop-motion films */
#define PB_CUT_MS 150

/* height of the one-line strip at the bottom of the scene, in pixels */
#define PB_CAPTION_PX 26

/* the text shown in the caption strip; lines[] are typed in a few
 * characters per cut, visible[] says how many are showing */
typedef struct _pb_caption_ {
	const char* title;
	const char* lines[2];
	int visible[2];
	int version; /* bumped whenever anything above changes */
} PbCaption;

/*
 * PbInit -- lay out the grid for a drawing area of w x h pixels.
 * Allocates the grids once; returns false when memory ran out.
 */
bool PbInit(int w, int h);

/* PbShutdown -- free everything PbInit allocated */
void PbShutdown();

/* PbStep -- advance the animation by one cut */
void PbStep();

/*
 * PbRender -- show the current cut in dst (pitch in pixels). Only cells
 * that differ from the previous cut are written, unless force is set.
 * The changed area (pixels, relative to dst) comes back in rx/ry/rw/rh.
 * @return true when anything was written
 */
bool PbRender(uint32_t* dst, int pitch, bool force, int* rx, int* ry, int* rw, int* rh);

/*
 * PbRepaint -- write the whole cut that is currently shown, e.g. after
 * the window was painted over. Unlike PbRender(force) it does not move
 * on to a new cut.
 */
void PbRepaint(uint32_t* dst, int pitch);

/*
 * PbRenderCaptionBackground -- repaint just the caption strip's woven
 * paper (before drawing new caption text on it)
 */
void PbRenderCaptionBackground(uint32_t* dst, int pitch);

/* PbCaptionRect -- caption strip in pixels, relative to dst */
void PbCaptionRect(int* x, int* y, int* w, int* h);

/* PbGetCaption -- the caption text for the current cut */
const PbCaption* PbGetCaption();

/* PbHeapBytes -- bytes the engine has allocated (fixed after PbInit) */
size_t PbHeapBytes();

/* PbCutNumber -- cuts shown since the start */
uint32_t PbCutNumber();

#endif
