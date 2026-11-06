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
 * gmstyle.h -- shared look for the flat "Gamusa" widget modules
 * (knot window chrome, flat buttons, flat sidebar).
 *
 * Paper and thread: cotton white surfaces, one red thread for focus and
 * selection, muga gold as the second accent, and the lozenge (the
 * diamond woven into every gamusa) standing in for checks and markers.
 * Every helper writes pixels straight into a ChCanvas, clipped to it,
 * and touches only the pixels it draws.
 */

#ifndef __CH_GMSTYLE_H__
#define __CH_GMSTYLE_H__

#include <stdint.h>
#include "../chitralekha.h"
#include "../draw.h"
#include "../font.h"

/* palette (ARGB) */
#define CH_GM_PAPER		 0xFFDFD2C7 //0xFFFFFEFA /* window body                     */
#define CH_GM_COTTON	 0xFFFAF7F0 /* panels, sidebars                */
#define CH_GM_LINEN		 0xFFF6EEE4 /* hover wash                      */
#define CH_GM_LINEN_DEEP 0xFFECE2D4 /* pressed wash                    */
#define CH_GM_HAIRLINE	 0xFFECE4D6 /* separators                      */
#define CH_GM_EDGE		 0xFFDCCFB4 /* window / field edge             */
#define CH_GM_SAND		 0xFFE2D6C0 /* inactive knot, disabled fill    */
#define CH_GM_INK		 0xFF231F1C
#define CH_GM_INK_SOFT	 0xFF6B6259
#define CH_GM_RED		 0xFFC8102E /* gamusa red                      */
#define CH_GM_RED_HOVER	 0xFFD62440
#define CH_GM_RED_DEEP	 0xFF8E1A22
#define CH_GM_RED_THREAD 0xFFE6A9AE /* the light companion thread      */
#define CH_GM_MUGA		 0xFFC99A2E /* muga silk gold                  */
#define CH_GM_MUGA_DEEP	 0xFF7A5A12
#define CH_GM_CHARCOAL	 0xFF2B2622
#define CH_GM_WHITE		 0xFFFFFFFF

/* one text size for every flat module: Chitralekha re-bakes a font's
 * glyph atlas whenever its size changes, so sharing one size keeps
 * hover repaints from re-rasterising the font */
#define CH_GM_TEXT_SIZE 11

#ifdef __cplusplus
XE_EXTERN {
#endif

	/* src over dst, a = 0..255, integer only */
	XE_LIB uint32_t ChGmBlend(uint32_t dst, uint32_t src, uint32_t a);
	/* filled rect, clipped to the canvas */
	XE_LIB void ChGmFill(ChCanvas * c, int x, int y, int w, int h, uint32_t col);
	/* 1px frame */
	XE_LIB void ChGmFrame(ChCanvas * c, int x, int y, int w, int h, uint32_t col);
	/* solid lozenge (diamond) of radius r centred on (cx,cy) */
	XE_LIB void ChGmLozenge(ChCanvas * c, int cx, int cy, int r, uint32_t col);
	/* outlined lozenge */
	XE_LIB void ChGmLozengeOutline(ChCanvas * c, int cx, int cy, int r, uint32_t col);
	/*
	 * embroidered lozenge: satin stitched fill (diagonal threads with a
	 * soft sheen), a back-stitch edge one shade deeper, optional eye
	 */
	XE_LIB void ChGmLozengeStitched(
		ChCanvas * c, int cx, int cy, int r, uint32_t thread, uint32_t edge, uint32_t sheen);
	/* the "thread" underline: 2px red + a 1px light thread at 60% width */
	XE_LIB void ChGmThread(ChCanvas * c, int x, int y, int w, uint32_t col, uint32_t col2);
	/* text helpers on the shared size */
	XE_LIB int ChGmTextWidth(ChFont * f, const char* s);
	XE_LIB void ChGmText(
		ChCanvas * c, ChFont * f, const char* s, int x, int baseline, uint32_t col);
	XE_LIB void ChGmTextClipped(
		ChCanvas * c, ChFont * f, const char* s, int x, int baseline, uint32_t col, ChRect* clip);

#ifdef __cplusplus
}
#endif

#endif
