/**
* BSD 2-Clause License
* 
* @file alpha.cpp
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

#include "deodxr.h"
#include "alpha.h"
#if defined(ARCH_ARM64)
#include <arm_neon.h>
#endif
#include "window.h"
#include <stdlib.h>

void __pixel_blend_neon(uint32_t* dst, const uint32_t* src, int width) {
#if defined(ARCH_ARM64)
	int x = 0;
	for (; x <= width - 4; x += 4) {
		uint32x4_t src4 = vld1q_u32(src + x);
		uint32x4_t alpha = vshrq_n_u32(src4, 24);
		uint64x2_t opaque_pairs =
			vreinterpretq_u64_u32(vceqq_u32(alpha, vdupq_n_u32(255)));
		if (vgetq_lane_u64(opaque_pairs, 0) == UINT64_MAX &&
			vgetq_lane_u64(opaque_pairs, 1) == UINT64_MAX) {
			vst1q_u32(dst + x, src4);
			continue;
		}
		uint64x2_t transparent_pairs =
			vreinterpretq_u64_u32(vceqq_u32(alpha, vdupq_n_u32(0)));
		if (vgetq_lane_u64(transparent_pairs, 0) == UINT64_MAX &&
			vgetq_lane_u64(transparent_pairs, 1) == UINT64_MAX)
			continue;

		/* Mixed-alpha groups are uncommon in the mostly opaque desktop. Handle
		 * them once from the original destination; the old path first stored a
		 * vector blend and then blended the same pixels a second time. */
		for (int i = x; i < x + 4 && i < width; i++) {
			uint32_t sp = src[i], dp = dst[i];
			uint8_t sa = sp >> 24;
			if (sa == 255) {
				dst[i] = sp;
				continue;
			}
			if (sa == 0) {
				continue;
			}
			uint32_t inv = 255 - sa;
			uint8_t r =
				((uint32_t)((sp >> 16) & 0xFF) * sa + (uint32_t)((dp >> 16) & 0xFF) * inv) >> 8;
			uint8_t g =
				((uint32_t)((sp >> 8) & 0xFF) * sa + (uint32_t)((dp >> 8) & 0xFF) * inv) >> 8;
			uint8_t b = ((uint32_t)((sp) & 0xFF) * sa + (uint32_t)((dp) & 0xFF) * inv) >> 8;
			dst[i] = (0xFF << 24) | (r << 16) | (g << 8) | b;
		}
	}

	// scalar tail
	for (; x < width; x++) {
		uint32_t sp = src[x], dp = dst[x];
		uint8_t sa = sp >> 24;
		if (sa == 255) {
			dst[x] = sp;
		} else if (sa > 0) {
			uint32_t inv = 255 - sa;
			uint8_t r =
				((uint32_t)((sp >> 16) & 0xFF) * sa + (uint32_t)((dp >> 16) & 0xFF) * inv) >> 8;
			uint8_t g =
				((uint32_t)((sp >> 8) & 0xFF) * sa + (uint32_t)((dp >> 8) & 0xFF) * inv) >> 8;
			uint8_t b = ((uint32_t)((sp) & 0xFF) * sa + (uint32_t)((dp) & 0xFF) * inv) >> 8;
			dst[x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
		}
	}
#else
	for (int x = 0; x < width; x++) {
		uint32_t sp = src[x], dp = dst[x];
		uint8_t sa = sp >> 24;
		if (sa == 255) {
			dst[x] = sp;
			continue;
		}
		if (sa == 0) {
			continue;
		}
		uint32_t inv = 255 - sa;
		uint8_t r = ((uint32_t)((sp >> 16) & 0xFF) * sa + (uint32_t)((dp >> 16) & 0xFF) * inv) >> 8;
		uint8_t g = ((uint32_t)((sp >> 8) & 0xFF) * sa + (uint32_t)((dp >> 8) & 0xFF) * inv) >> 8;
		uint8_t b = ((uint32_t)((sp) & 0xFF) * sa + (uint32_t)((dp) & 0xFF) * inv) >> 8;
		dst[x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
	}
#endif
}

#define GLASS_BLUR_RADIUS 6

static int clamp_coord(int value, int lo, int hi) {
	if (value < lo)
		return lo;
	if (value > hi)
		return hi;
	return value;
}

static void __blur_pass_horizontal(uint32_t* tmp,
								   const uint32_t* src,
								   int src_w,
								   int src_h,
								   int rx,
								   int ry,
								   int rw,
								   int r_h,
								   int radius) {
	int x_max = src_w > 0 ? src_w - 1 : 0;
	int y_max = src_h > 0 ? src_h - 1 : 0;
	int diameter = 2 * radius + 1;

	for (int row = 0; row < r_h; row++) {
		/* Channel deltas must stay signed. Unsigned 8-bit extraction wraps a
		 * darker incoming pixel through 0 and lands in the green channel. --axiss */
		int32_t sum_r = 0, sum_g = 0, sum_b = 0;
		int sy = clamp_coord(ry + row, 0, y_max);

		for (int k = -radius; k <= radius; k++) {
			int sx = clamp_coord(rx + k, 0, x_max);
			uint32_t px = src[sy * src_w + sx];
			sum_r += (int32_t)((px >> 16) & 0xFF);
			sum_g += (int32_t)((px >> 8) & 0xFF);
			sum_b += (int32_t)((px >> 0) & 0xFF);
		}
		for (int col = 0; col < rw; col++) {
			int32_t ar = sum_r / diameter;
			int32_t ag = sum_g / diameter;
			int32_t ab = sum_b / diameter;
			if (ar < 0)
				ar = 0;
			if (ag < 0)
				ag = 0;
			if (ab < 0)
				ab = 0;
			if (ar > 255)
				ar = 255;
			if (ag > 255)
				ag = 255;
			if (ab > 255)
				ab = 255;
			tmp[row * rw + col] =
				(0xFFu << 24) | ((uint32_t)ar << 16) | ((uint32_t)ag << 8) | (uint32_t)ab;

			int remove_x = clamp_coord(rx + col - radius, 0, x_max);
			int add_x = clamp_coord(rx + col + radius + 1, 0, x_max);
			uint32_t rem = src[sy * src_w + remove_x];
			uint32_t add = src[sy * src_w + add_x];
			sum_r += (int32_t)((add >> 16) & 0xFF) - (int32_t)((rem >> 16) & 0xFF);
			sum_g += (int32_t)((add >> 8) & 0xFF) - (int32_t)((rem >> 8) & 0xFF);
			sum_b += (int32_t)((add >> 0) & 0xFF) - (int32_t)((rem >> 0) & 0xFF);
		}
	}
}

static void
blur_pass_vertical_neon(uint32_t* out, const uint32_t* tmp, int rw, int rh, int radius) {
	int diameter = 2 * radius + 1;

	for (int col = 0; col < rw; col++) {
		int32_t sum_r = 0, sum_g = 0, sum_b = 0;

		for (int k = -radius; k <= radius; k++) {
			int sr = k;
			if (sr < 0)
				sr = 0;
			if (sr >= rh)
				sr = rh - 1;
			uint32_t px = tmp[sr * rw + col];
			sum_r += (px >> 16) & 0xFF;
			sum_g += (px >> 8) & 0xFF;
			sum_b += (px >> 0) & 0xFF;
		}

		for (int row = 0; row < rh; row++) {
			int32_t ar = sum_r / diameter;
			int32_t ag = sum_g / diameter;
			int32_t ab = sum_b / diameter;
			if (ar < 0)
				ar = 0;
			if (ag < 0)
				ag = 0;
			if (ab < 0)
				ab = 0;
			if (ar > 255)
				ar = 255;
			if (ag > 255)
				ag = 255;
			if (ab > 255)
				ab = 255;
			out[row * rw + col] =
				(0xFFu << 24) | ((uint32_t)ar << 16) | ((uint32_t)ag << 8) | (uint32_t)ab;

			int remove_y = row - radius;
			int add_y = row + radius + 1;
			if (remove_y < 0)
				remove_y = 0;
			if (remove_y >= rh)
				remove_y = rh - 1;
			if (add_y < 0)
				add_y = 0;
			if (add_y >= rh)
				add_y = rh - 1;

			uint32_t rem = tmp[remove_y * rw + col];
			uint32_t add = tmp[add_y * rw + col];
			sum_r += (int32_t)((add >> 16) & 0xFF) - (int32_t)((rem >> 16) & 0xFF);
			sum_g += (int32_t)((add >> 8) & 0xFF) - (int32_t)((rem >> 8) & 0xFF);
			sum_b += (int32_t)((add >> 0) & 0xFF) - (int32_t)((rem >> 0) & 0xFF);
		}
	}
}

void glass_precompute_blur(uint32_t* out_blur,
						   uint32_t* tmp,
						   const uint32_t* canvas,
						   int canvas_w,
						   int canvas_h,
						   int rx,
						   int ry,
						   int rw,
						   int rh,
						   int radius) {
	if (!out_blur || !tmp || !canvas || canvas_w <= 0 || canvas_h <= 0 || rw <= 0 || rh <= 0)
		return;
	if (radius < 0)
		radius = 0;
	__blur_pass_horizontal(tmp, canvas, canvas_w, canvas_h, rx, ry, rw, rh, radius);
	blur_pass_vertical_neon(out_blur, tmp, rw, rh, radius);
}

void glass_invalidate(Window* win) {
	if (win)
		win->glassBlurValid = false;
}

void glass_prepare_window(Window* win,
						  WinSharedInfo* info,
						  const uint32_t* back_surface,
						  int canvas_w,
						  int canvas_h) {
	if (!win || !info || !back_surface)
		return;
	if (!(win->flags & WINDOW_FLAG_GLASS) || !win->glassBlur || !win->glassTmp)
		return;
	if (info->width <= 0 || info->height <= 0)
		return;
	if (win->glassBlurValid && win->glassBlurX == info->x && win->glassBlurY == info->y &&
		win->glassBlurW == info->width && win->glassBlurH == info->height)
		return;

	/* Blur the wallpaper behind the whole window once, stored at window
	 * stride. Dirty and clipped compose then index this buffer with
	 * window-local coordinates instead of re-blurring each fragment. --axiss */
	glass_precompute_blur(win->glassBlur,
						  win->glassTmp,
						  back_surface,
						  canvas_w,
						  canvas_h,
						  info->x,
						  info->y,
						  info->width,
						  info->height,
						  4);
	win->glassBlurValid = true;
	win->glassBlurX = info->x;
	win->glassBlurY = info->y;
	win->glassBlurW = info->width;
	win->glassBlurH = info->height;
}

void _blend_scanline_glass_neon(uint32_t* canvas_row,
								const uint32_t* win_row,
								const uint32_t* blur_row,
								int width) {
	int x = 0;
#if defined(ARCH_ARM64)
	for (; x <= width - 4; x += 4) {
		uint32x4_t src4 = vld1q_u32(win_row + x);
		uint32x4_t alpha = vshrq_n_u32(src4, 24);
		uint64x2_t opaque_pairs = vreinterpretq_u64_u32(vceqq_u32(alpha, vdupq_n_u32(255)));
		if (vgetq_lane_u64(opaque_pairs, 0) == UINT64_MAX &&
			vgetq_lane_u64(opaque_pairs, 1) == UINT64_MAX) {
			vst1q_u32(canvas_row + x, src4);
			continue;
		}
		for (int i = 0; i < 4; i++) {
			uint32_t sp = win_row[x + i];
			uint32_t bp = blur_row[x + i];
			uint8_t sa = (uint8_t)(sp >> 24);
			if (sa == 255) {
				canvas_row[x + i] = sp;
			} else if (sa == 0) {
				canvas_row[x + i] = bp;
			} else {
				uint32_t inv = 255 - sa;
				uint8_t r =
					(uint8_t)(((sp >> 16 & 0xFF) * sa + (bp >> 16 & 0xFF) * inv) >> 8);
				uint8_t g =
					(uint8_t)(((sp >> 8 & 0xFF) * sa + (bp >> 8 & 0xFF) * inv) >> 8);
				uint8_t b =
					(uint8_t)(((sp >> 0 & 0xFF) * sa + (bp >> 0 & 0xFF) * inv) >> 8);
				canvas_row[x + i] = (0xFFu << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
			}
		}
	}
#endif
	for (; x < width; x++) {
		uint32_t sp = win_row[x];
		uint32_t bp = blur_row[x];
		uint8_t sa = (uint8_t)(sp >> 24);
		if (sa == 255) {
			canvas_row[x] = sp;
		} else if (sa == 0) {
			canvas_row[x] = bp;
		} else {
			uint32_t inv = 255 - sa;
			uint8_t r = (uint8_t)(((sp >> 16 & 0xFF) * sa + (bp >> 16 & 0xFF) * inv) >> 8);
			uint8_t g = (uint8_t)(((sp >> 8 & 0xFF) * sa + (bp >> 8 & 0xFF) * inv) >> 8);
			uint8_t b = (uint8_t)(((sp >> 0 & 0xFF) * sa + (bp >> 0 & 0xFF) * inv) >> 8);
			canvas_row[x] = (0xFFu << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
		}
	}
}

void _shadow_blur_horizontal(uint32_t* dst, const uint32_t* src, int w, int h, int radius) {
	int diameter = 2 * radius + 1;

	for (int y = 0; y < h; y++) {
		uint32_t sum_a = 0;

		//prime sliding window
		for (int k = -radius; k <= radius; k++) {
			int sx = k;
			if (sx < 0)
				sx = 0;
			if (sx >= w)
				sx = w - 1;
			sum_a += (src[y * w + sx] >> 24) & 0xFF;
		}

		for (int x = 0; x < w; x++) {
			uint8_t a = (uint8_t)(sum_a / diameter);
			dst[y * w + x] = ((uint32_t)a << 24);
			int rem_x = x - radius;
			int add_x = x + radius + 1;
			if (rem_x < 0)
				rem_x = 0;
			if (rem_x >= w)
				rem_x = w - 1;
			if (add_x < 0)
				add_x = 0;
			if (add_x >= w)
				add_x = w - 1;

			sum_a += ((src[y * w + add_x] >> 24) & 0xFF) - ((src[y * w + rem_x] >> 24) & 0xFF);
		}
	}
}

void _shadow_blur_vertical(uint32_t* dst, const uint32_t* src, int w, int h, int radius) {
	int diameter = 2 * radius + 1;

	for (int x = 0; x < w; x++) {
		uint32_t sum_a = 0;

		for (int k = -radius; k <= radius; k++) {
			int sy = k;
			if (sy < 0)
				sy = 0;
			if (sy >= h)
				sy = h - 1;
			sum_a += (src[sy * w + x] >> 24) & 0xFF;
		}

		for (int y = 0; y < h; y++) {
			uint8_t a = (uint8_t)(sum_a / diameter);
			dst[y * w + x] = ((uint32_t)a << 24);

			int rem_y = y - radius;
			int add_y = y + radius + 1;
			if (rem_y < 0)
				rem_y = 0;
			if (rem_y >= h)
				rem_y = h - 1;
			if (add_y < 0)
				add_y = 0;
			if (add_y >= h)
				add_y = h - 1;

			sum_a += ((src[add_y * w + x] >> 24) & 0xFF) - ((src[rem_y * w + x] >> 24));
		}
	}
}

void _shadow_compose_neon(uint32_t* canv,
						  int canvas_w,
						  int canvas_h,
						  const uint32_t* shadow_buf,
						  int shadow_w,
						  int shadow_h,
						  int win_x,
						  int win_y) {
	int draw_x = win_x - SHADOW_SIZE;
	int draw_y = win_y - SHADOW_SIZE;

	for (int j = 0; j < shadow_h; j++) {
		int cy = draw_y + j;
		if (cy < 0 || cy >= canvas_h)
			continue;

		int cx_start = draw_x;
		int sx_start = 0;
		int row_w = shadow_w;

		if (cx_start < 0) {
			sx_start -= cx_start;
			row_w += cx_start;
			cx_start = 0;
		}

		if (cx_start + row_w > canvas_w)
			row_w = canvas_w - cx_start;

		if (row_w <= 0)
			continue;

		uint32_t* dst = canv + cy * canvas_w + cx_start;
		const uint32_t* src = shadow_buf + j * shadow_w + sx_start;

		int x = 0;
		for (; x <= row_w - 4; x += 4) {
#if 0 /* WIP: NEON shadow compose path — unused scaffolding, type errors pending fix */
			uint32x4_t s4 = vld1q_u32(src + x);
			uint32x4_t d4 = vld1q_u32(dst + x);

			uint32x4_t sa4 = vshrq_n_u32(s4, 24);
			uint32x4_t inv4 = vsubq_u32(vdupq_n_u32(255), sa4);

			uint8x16_t db = vreinterpretq_u8_u32(d4);

			uint8x8_t inv_lo = vmovn_u16(vmovl_u32(vget_low_u32(inv4)));
			uint8x8_t inv_hi = vmovn_u16(vmovl_u32(vget_high_u32(inv4)));

			uint8x8_t inv_lo4 = vzip1_u8(inv_lo, inv_lo);
#endif

			for (int i = x; i < x + 4; i++) {
				uint32_t dp = dst[i];
				uint8_t sa = (uint8_t)(src[i] >> 24);
				if (sa == 0)
					continue;
				uint32_t inv = 255 - sa;
				uint8_t r = (uint8_t)(((dp >> 16 & 0xFF) * inv) >> 8);
				uint8_t g = (uint8_t)(((dp >> 8 & 0xFF) * inv) >> 8);
				uint8_t b = (uint8_t)(((dp >> 0 & 0xFF) * inv) >> 8);
				dst[i] = (0xFFu << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
			}
		}

		for (; x < row_w; x++) {
			uint32_t dp = dst[x];
			uint8_t sa = (uint8_t)(src[x] >> 24);
			if (sa == 0)
				continue;
			uint32_t inv = 255 - sa;
			uint8_t r = (uint8_t)(((dp >> 16 & 0xFF) * inv) >> 8);
			uint8_t g = (uint8_t)(((dp >> 8 & 0xFF) * inv) >> 8);
			uint8_t b = (uint8_t)(((dp >> 0 & 0xFF) * inv) >> 8);
			dst[x] = (0xFFu << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
		}
	}
}
