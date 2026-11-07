/**
* BSD 2-Clause License
*
* @file alpha.h
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

#ifndef __ALPHA_H__
#define __ALPHA_H__

#include <stdint.h>
#include "window.h"

extern void glass_precompute_blur(uint32_t* out_blur,
								  uint32_t* tmp,
								  const uint32_t* canvas,
								  int canvas_w,
								  int canvas_h,
								  int rx,
								  int ry,
								  int rw,
								  int rh,
								  int radius);
extern void glass_invalidate(Window* win);
extern void glass_prepare_window(Window* win,
								 WinSharedInfo* info,
								 const uint32_t* back_surface,
								 int canvas_w,
								 int canvas_h);
extern void __pixel_blend_neon(uint32_t* dst, const uint32_t* src, int width);

extern void _blend_scanline_glass_neon(uint32_t* canvas_row,
									   const uint32_t* win_row,
									   const uint32_t* blur_row,
									   int width);

extern void _shadow_blur_horizontal(uint32_t* dst, const uint32_t* src, int w, int h, int radius);
extern void _shadow_blur_vertical(uint32_t* dst, const uint32_t* src, int w, int h, int radius);

extern void _shadow_compose_neon(uint32_t* canv,
								 int canvas_w,
								 int canvas_h,
								 const uint32_t* shadow_buf,
								 int shadow_w,
								 int shadow_h,
								 int win_x,
								 int win_y);

#endif
