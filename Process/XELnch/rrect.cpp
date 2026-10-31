#include <arm_neon.h>
#include <stdint.h>
#include <chitralekha.h>

static inline int isqrt32(uint32_t n) {
	if (n == 0)
		return 0;
	uint32_t x = n, y = (x + 1) >> 1;
	while (y < x) {
		x = y;
		y = (x + n / x) >> 1;
	}
	return (int)x;
}

static inline uint32_t blend_over_scalar(uint32_t dst, uint32_t src) {
	uint8_t sa = (src >> 24) & 0xFF;
	if (sa == 0)
		return dst;
	if (sa == 255)
		return src;
	uint8_t inv = 255 - sa;
	uint8_t sr = (src >> 16) & 0xFF;
	uint8_t sg = (src >> 8) & 0xFF;
	uint8_t sb = (src >> 0) & 0xFF;
	uint8_t dr = (dst >> 16) & 0xFF;
	uint8_t dg = (dst >> 8) & 0xFF;
	uint8_t db = (dst >> 0) & 0xFF;
	return 0xFF000000u | (uint32_t)((sr * sa + dr * inv) >> 8) << 16 |
		   (uint32_t)((sg * sa + dg * inv) >> 8) << 8 | (uint32_t)((sb * sa + db * inv) >> 8);
}

static inline uint32x4_t blend_over_neon(uint32x4_t dst4, uint32x4_t src4, uint8x8_t alpha4) {
	uint8x16_t s8 = vreinterpretq_u8_u32(src4);
	uint8x16_t d8 = vreinterpretq_u8_u32(dst4);

	/* Expand to 16-bit for multiply */
	uint16x8_t s16_lo = vmovl_u8(vget_low_u8(s8));	/* px0, px1 */
	uint16x8_t s16_hi = vmovl_u8(vget_high_u8(s8)); /* px2, px3 */
	uint16x8_t d16_lo = vmovl_u8(vget_low_u8(d8));
	uint16x8_t d16_hi = vmovl_u8(vget_high_u8(d8));

	uint16x8_t a16 = vmovl_u8(alpha4); /* 8 lanes, low 4 used */

	uint16_t a0 = vget_lane_u16(vget_low_u16(a16), 0);
	uint16_t a1 = vget_lane_u16(vget_low_u16(a16), 1);
	uint16_t a2 = vget_lane_u16(vget_low_u16(a16), 2);
	uint16_t a3 = vget_lane_u16(vget_low_u16(a16), 3);

	/* inverse alpha (255 - a) */
	uint16_t ia0 = 255 - a0, ia1 = 255 - a1;
	uint16_t ia2 = 255 - a2, ia3 = 255 - a3;

	/* Build broadcast vectors: [ a a a a  ia ia ia ia ] for px0|px1 */
	uint16x4_t va0 = vdup_n_u16(a0);
	uint16x4_t va1 = vdup_n_u16(a1);
	uint16x4_t via0 = vdup_n_u16(ia0);
	uint16x4_t via1 = vdup_n_u16(ia1);
	uint16x4_t va2 = vdup_n_u16(a2);
	uint16x4_t va3 = vdup_n_u16(a3);
	uint16x4_t via2 = vdup_n_u16(ia2);
	uint16x4_t via3 = vdup_n_u16(ia3);

	/* src * alpha + dst * (255 - alpha) for px0 and px1 */
	uint16x4_t res0 = vshr_n_u16(
		vadd_u16(vmul_u16(vget_low_u16(s16_lo), va0), vmul_u16(vget_low_u16(d16_lo), via0)), 8);

	uint16x4_t res1 = vshr_n_u16(
		vadd_u16(vmul_u16(vget_high_u16(s16_lo), va1), vmul_u16(vget_high_u16(d16_lo), via1)), 8);

	uint16x4_t res2 = vshr_n_u16(
		vadd_u16(vmul_u16(vget_low_u16(s16_hi), va2), vmul_u16(vget_low_u16(d16_hi), via2)), 8);

	uint16x4_t res3 = vshr_n_u16(
		vadd_u16(vmul_u16(vget_high_u16(s16_hi), va3), vmul_u16(vget_high_u16(d16_hi), via3)), 8);

	/* Narrow back to 8-bit and pack */
	uint8x8_t narrow_lo = vmovn_u16(vcombine_u16(res0, res1));
	uint8x8_t narrow_hi = vmovn_u16(vcombine_u16(res2, res3));
	uint8x16_t result8 = vcombine_u8(narrow_lo, narrow_hi);

	/* Force alpha channel of output to 0xFF (opaque framebuffer) */
	/* Alpha byte is index 3,7,11,15 in ARGB little-endian layout   */
	static const uint8_t alpha_mask_bytes[16] = {
		0, 0, 0, 0xFF, 0, 0, 0, 0xFF, 0, 0, 0, 0xFF, 0, 0, 0, 0xFF};
	uint8x16_t amask = vld1q_u8(alpha_mask_bytes);
	result8 = vorrq_u8(result8, amask);

	return vreinterpretq_u32_u8(result8);
}

void fill_rrect_neon(
	uint32_t* fb, int fb_stride, int x, int y, int w, int h, int radius, uint32_t color) {
	/* ---- clamp radius ---------------------------------------------- */
	int max_r = (w < h ? w : h) / 2;
	if (radius > max_r)
		radius = max_r;
	if (radius < 0)
		radius = 0;

	/* ---- colour components ----------------------------------------- */
	uint8_t ca = (color >> 24) & 0xFF;
	uint8_t cr = (color >> 16) & 0xFF;
	uint8_t cg = (color >> 8) & 0xFF;
	uint8_t cb = (color >> 0) & 0xFF;

	/* Fully opaque fast path: no blending needed, just store directly  */
	int opaque = (ca == 0xFF);

	/* NEON splat: four copies of the colour */
	uint32x4_t color4 = vdupq_n_u32(color | 0xFF000000u);

	/* Alpha splat for blending path */
	uint8x8_t alpha4 = vdup_n_u8(ca);

	int r = radius;
	int r2 = r * r;

	int y0 = y;
	int y1 = y + h - 1;
	int x0 = x;
	int x1 = x + w - 1;

	/* Vertical band boundaries */
	int band_a_end = y0 + r - 1; /* last row of top-corner band    */
	int band_b_end = y1 - r;	 /* last row of middle band         */
	/* band C: band_b_end+1 .. y1 */

	/* Corner circle Y-centres */
	int cy_top = y0 + r;
	int cy_bot = y1 - r;

	/* Corner circle X-centres */
	int cx_left = x0 + r;
	int cx_right = x1 - r;

	for (int py = y0; py <= y1; py++) {
		int span_x0, span_x1;

		if (py <= band_a_end) {
			/* Top-corner band: compute chord width from circle equation */
			int dy = cy_top - py; /* always >= 0 here */
			int dx = isqrt32(r2 - dy * dy);
			span_x0 = cx_left - dx;
			span_x1 = cx_right + dx;
		} else if (py <= band_b_end) {
			/* Middle band: full width */
			span_x0 = x0;
			span_x1 = x1;
		} else {
			/* Bottom-corner band */
			int dy = py - cy_bot; /* always >= 0 here */
			int dx = isqrt32(r2 - dy * dy);
			span_x0 = cx_left - dx;
			span_x1 = cx_right + dx;
		}

		/* ---- fill span [span_x0, span_x1] on row py --------------- */
		uint32_t* row = fb + py * fb_stride;
		int count = span_x1 - span_x0 + 1;
		int px = span_x0;

		if (opaque) {
			/* ---- Fully opaque: NEON store loop --------------------- */
			/* Head: align to 4-pixel boundary */
			while (count > 0 && (px & 3)) {
				row[px++] = color | 0xFF000000u;
				count--;
			}
			/* Bulk: 4 pixels per iteration */
			while (count >= 4) {
				vst1q_u32(row + px, color4);
				px += 4;
				count -= 4;
			}
			/* Tail */
			while (count-- > 0)
				row[px++] = color | 0xFF000000u;

		} else {
			/* ---- Translucent: NEON blend loop ---------------------- */
			/* Head */
			while (count > 0 && (px & 3)) {
				row[px] = blend_over_scalar(row[px], color);
				px++;
				count--;
			}
			/* Bulk: 4 pixels per iteration */
			while (count >= 4) {
				uint32x4_t dst4 = vld1q_u32(row + px);
				uint32x4_t out4 = blend_over_neon(dst4, color4, alpha4);
				vst1q_u32(row + px, out4);
				px += 4;
				count -= 4;
			}
			/* Tail */
			while (count-- > 0) {
				row[px] = blend_over_scalar(row[px], color);
				px++;
			}
		}
	}
}

void fill_rrect_neon_aa(
	uint32_t* fb, int fb_stride, int x, int y, int w, int h, int radius, uint32_t color) {
	/* First draw the solid interior with radius shrunk by 1 px       */
	if (radius > 1)
		fill_rrect_neon(fb, fb_stride, x, y, w, h, radius - 1, color);
	else
		fill_rrect_neon(fb, fb_stride, x, y, w, h, 0, color);

	if (radius <= 0)
		return;

	int r = radius;
	int r2 = r * r;

	/* Corner centres */
	int cx[4] = {x + r, x + w - 1 - r, x + r, x + w - 1 - r};
	int cy[4] = {y + r, y + r, y + h - 1 - r, y + h - 1 - r};

	uint8_t ca = (color >> 24) & 0xFF;
	uint8_t cr = (color >> 16) & 0xFF;
	uint8_t cg = (color >> 8) & 0xFF;
	uint8_t cb = (color >> 0) & 0xFF;

	for (int c = 0; c < 4; c++) {
		int ccx = cx[c], ccy = cy[c];

		/* Scan a (radius+1)² box around the corner centre */
		for (int dy = -r - 1; dy <= r + 1; dy++) {
			for (int dx = -r - 1; dx <= r + 1; dx++) {
				int px = ccx + dx, py = ccy + dy;

				/* Only process pixels in this corner's quadrant */
				/* (avoids processing pixels belonging to adjacent corners) */
				int qx = (c & 1) ? (dx >= 0) : (dx <= 0);
				int qy = (c & 2) ? (dy >= 0) : (dy <= 0);
				if (!qx || !qy)
					continue;

				/* Distance from corner centre */
				int d2 = dx * dx + dy * dy;
				int d = isqrt32(d2);

				/* Only process pixels on the circle boundary ±1 px */
				if (d < r - 1 || d > r + 1)
					continue;

				/* Sub-pixel coverage: how much of the pixel lies inside */
				/* Simple approximation: linear ramp over [r-1, r+1]     */
				int cov;
				if (d <= r)
					cov = 255; /* fully inside */
				else
					cov = 255 - (d - r) * 255; /* fade outside */

				if (cov <= 0)
					continue;

				/* Modulate by the colour's own alpha */
				int final_a = ((int)ca * cov) >> 8;
				if (final_a <= 0)
					continue;

				uint32_t src = ((uint32_t)final_a << 24) | ((uint32_t)cr << 16) |
							   ((uint32_t)cg << 8) | (uint32_t)cb;

				fb[py * fb_stride + px] = blend_over_scalar(fb[py * fb_stride + px], src);
			}
		}
	}
}

/**
 * @brief draw_rrect_filled -- draws rounded rectangle filled 
 * @param canv -- Pointer to canvas
 * @param x -- X coordinate
 * @param y -- Y coordinate
 * @param w -- width of the rounded rect
 * @param h -- Height of the rounded rectangle
 */
void draw_rrect_filled(ChCanvas* canv, int x, int y, int w, int h, int radius, uint32_t color) {
	fill_rrect_neon(canv->buffer, canv->canvasWidth, x, y, w, h, radius, color);
}
