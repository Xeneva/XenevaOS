/**
* BSD 2-Clause License
*
* @file compose.cpp
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

#ifndef __COMPOSE_H__
#define __COMPOSE_H__

#include "deodxr.h"
#include "backdirty.h"
#include "dirty.h"
#include "window.h"
#include "alpha.h"
#include "animation.h"
#include "clip.h"
#include "surface.h"

/**
 * @brief Check for small area updates !! not entire window
*/
extern void
_compose_dirty_area_(ChCanvas* canvas, Window* win, Window* focusedWin, WinSharedInfo* info);
/**
 * @brief _compose_entire_window_ -- composes an entire window to canvas
 * @param canvas -- Pointer to Deodhai canvas
 * @param win -- Pointer to main window to compose
 * @param  _window_update_all_ -- update all bit
 * @param info -- Pointer to main window info struct
 * @param focusedWin -- focused window pointer
 * @param _window_moving_ -- window moving bit
 * @param _shadow_update -- shadow update bit which tells if we need to update
 * shadows or not
 */
extern void _compose_entire_window(ChCanvas* canvas,
								   Window* win,
								   bool _window_update_all_,
								   WinSharedInfo* info,
								   Window* focusedWin,
								   bool _window_moving_,
								   bool _shadow_update);

/**
 * @brief compose_window_zoomed -- stretched-fill bilinear upscale of a
 * window buffer to the full scanout (WinSharedInfo->zoomed)
 */
extern void compose_window_zoomed(ChCanvas* canvas, Window* win, WinSharedInfo* info);

/**
 * @brief compose_window_scaled_to -- scales the window's own buffer
 * (src_w x src_h) to (dw x dh) and alpha-blends it onto the canvas at
 * (dst_x, dst_y). Clipped to the canvas, NEON fast path kept.
 * @param src_w, src_h -- ORIGINAL size of win->backBuffer (not info->width/height,
 *                        those change while resizing)
 * @param dst_x, dst_y -- top-left of the scaled window on the canvas
 * @param dw, dh -- size to scale to
 */
void compose_window_scaled_to(
	ChCanvas* canvas, Window* win, int src_w, int src_h, int dst_x, int dst_y, int dw, int dh);

/**
 * @brief compose_window_scaled_to_clip -- scales the window's own buffer
 * (src_w x src_h) to (dw x dh) and alpha-blends it onto the canvas at
 * (dst_x, dst_y). Clipped to the canvas, NEON fast path kept.
 * @param src_w, src_h -- ORIGINAL size of win->backBuffer (not info->width/height,
 *                        those change while resizing)
 * @param dst_x, dst_y -- top-left of the scaled window on the canvas
 * @param dw, dh -- size to scale to
 */
void compose_window_scaled_to_clip(ChCanvas* canvas,
								   Window* win,
								   int src_w,
								   int src_h,
								   int dst_x,
								   int dst_y,
								   int dw,
								   int dh,
								   int clip_x,
								   int clip_y,
								   int clip_w,
								   int clip_h);

/**
 * @brief _compose_always_on_top_dirty -- compose always on top window's dirty rectangles
 * @param canvas -- Pointer to canvas
 * @param info -- Pointer to window's shared info
 * @param _window_moving_ -- window moving bit
 * @param focusedWin -- Pointer to focused Window
 * @param win -- Pointer to main window
 */
extern void _compose_always_on_top_dirty(
	ChCanvas* canvas, WinSharedInfo* info, bool _window_moving_, Window* focusedWin, Window* win);

/**
 * @brief _compose_always_on_top_entire -- compose entire always on top window
 */
extern void _compose_always_on_top_entire(ChCanvas* canvas,
										  Window* win,
										  bool _always_on_top_update,
										  bool _window_moving_,
										  WinSharedInfo* info,
										  Window* rootWin);

#endif