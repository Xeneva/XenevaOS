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
 * xlicon.cpp -- minimal BMP decoder for launcher icons.
 *
 * Supports 24 bpp and 32 bpp (BI_RGB / BI_BITFIELDS, any header size,
 * bottom-up or top-down). The icon is decoded straight into the size it
 * will be drawn at with an area-average (box) filter in premultiplied
 * space, so the BMP file buffer is freed immediately and only
 * w * h * 4 bytes stay resident per icon.
 *
 * Time: O(srcW * srcH) once per icon. Memory: file size (transient) +
 * dstW * dstH * 4.
 */

#include "launcher.h"
#include <stdlib.h>
#include <string.h>
#include <sys/_kefile.h>

typedef struct _bmp_view_ {
	const uint8_t* px;
	int w, h;
	int bpp;
	int pitch;
	bool topDown;
	bool bitfields;
	uint32_t mask[4]; /* r g b a */
	int shift[4];
	int bits[4];
	bool keyBlackWhite; /* 24 bpp assets use pure black/white as transparent */
	bool forceOpaque;	/* 32 bpp BI_RGB files with an all-zero alpha channel */
} BmpView;

static inline uint32_t _rd32(const uint8_t* p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint16_t _rd16(const uint8_t* p) {
	return (uint16_t)(p[0] | (p[1] << 8));
}

static void _mask_info(uint32_t m, int* shift, int* bits) {
	int s = 0, b = 0;
	if (m) {
		while (!(m & 1)) {
			m >>= 1;
			s++;
		}
		while (m & 1) {
			m >>= 1;
			b++;
		}
	}
	*shift = s;
	*bits = b;
}

static inline uint32_t _expand(uint32_t v, int bits) {
	if (bits <= 0)
		return 0;
	if (bits >= 8)
		return (v >> (bits - 8)) & 0xFF;
	/* replicate high bits into the low end */
	uint32_t out = v << (8 - bits);
	return (out | (out >> bits)) & 0xFF;
}

/* fetch one source pixel as ARGB (non premultiplied) */
static inline uint32_t _fetch(const BmpView* v, int x, int y) {
	int row = v->topDown ? y : (v->h - 1 - y);
	const uint8_t* p = v->px + (size_t)row * v->pitch + (size_t)x * (v->bpp / 8);
	uint32_t r, g, b, a;
	if (v->bpp == 24) {
		b = p[0];
		g = p[1];
		r = p[2];
		a = 255;
		if (v->keyBlackWhite && ((r == 255 && g == 255 && b == 255) || (r == 0 && g == 0 && b == 0)))
			a = 0;
	} else if (v->bitfields) {
		uint32_t raw = _rd32(p);
		r = _expand((raw & v->mask[0]) >> v->shift[0], v->bits[0]);
		g = _expand((raw & v->mask[1]) >> v->shift[1], v->bits[1]);
		b = _expand((raw & v->mask[2]) >> v->shift[2], v->bits[2]);
		a = v->mask[3] ? _expand((raw & v->mask[3]) >> v->shift[3], v->bits[3]) : 255;
	} else {
		b = p[0];
		g = p[1];
		r = p[2];
		a = p[3];
	}
	if (v->forceOpaque)
		a = 255;
	return (a << 24) | (r << 16) | (g << 8) | b;
}

static bool _parse(const uint8_t* buf, size_t size, BmpView* v) {
	if (size < 54 || buf[0] != 'B' || buf[1] != 'M')
		return false;
	uint32_t off = _rd32(buf + 10);
	uint32_t hdr = _rd32(buf + 14);
	int32_t w = (int32_t)_rd32(buf + 18);
	int32_t h = (int32_t)_rd32(buf + 22);
	int bpp = _rd16(buf + 28);
	uint32_t comp = _rd32(buf + 30);
	if (w <= 0 || h == 0 || (bpp != 24 && bpp != 32) || (comp != 0 && comp != 3))
		return false;
	memset(v, 0, sizeof(BmpView));
	v->w = w;
	v->topDown = h < 0;
	v->h = h < 0 ? -h : h;
	v->bpp = bpp;
	v->pitch = ((w * bpp + 31) / 32) * 4;
	if ((size_t)off + (size_t)v->pitch * v->h > size)
		return false;
	v->px = buf + off;
	if (comp == 3 && bpp == 32) {
		if (size < 66)
			return false;
		v->bitfields = true;
		v->mask[0] = _rd32(buf + 54);
		v->mask[1] = _rd32(buf + 58);
		v->mask[2] = _rd32(buf + 62);
		v->mask[3] = (hdr >= 56 && size >= 70) ? _rd32(buf + 66) : 0;
		for (int i = 0; i < 4; i++)
			_mask_info(v->mask[i], &v->shift[i], &v->bits[i]);
	}
	v->keyBlackWhite = (bpp == 24);
	if (bpp == 32) {
		/* BI_RGB 32 bpp often carries a zero alpha channel: treat as opaque */
		bool anyAlpha = false;
		for (int y = 0; y < v->h && !anyAlpha; y++)
			for (int x = 0; x < v->w; x++)
				if (_fetch(v, x, y) >> 24) {
					anyAlpha = true;
					break;
				}
		v->forceOpaque = !anyAlpha;
	}
	return true;
}

/*
 * XlIconLoad -- load a BMP and scale it to fit inside maxW x maxH
 * (never upscales). Returns false and leaves *out empty on failure.
 */
bool XlIconLoad(const char* path, int maxW, int maxH, XlIcon* out) {
	memset(out, 0, sizeof(XlIcon));
	if (!path || !path[0] || maxW <= 0 || maxH <= 0)
		return false;
	int fd = _KeOpenFile((char*)path, FILE_OPEN_READ_ONLY);
	if (fd == -1)
		return false;
	XEFileStatus st;
	memset(&st, 0, sizeof(st));
	_KeFileStat(fd, &st);
	size_t size = st.size;
	if (size < 54 || size > (4u << 20)) {
		_KeCloseFile(fd);
		return false;
	}
	uint8_t* buf = (uint8_t*)malloc(size);
	if (!buf) {
		_KeCloseFile(fd);
		return false;
	}
	_KeReadFile(fd, buf, size);
	_KeCloseFile(fd);

	BmpView v;
	if (!_parse(buf, size, &v)) {
		free(buf);
		return false;
	}

	/* fit, keep aspect, no upscale */
	int dw = v.w, dh = v.h;
	if (dw > maxW || dh > maxH) {
		if ((int64_t)v.w * maxH > (int64_t)v.h * maxW) {
			dw = maxW;
			dh = (int)((int64_t)v.h * maxW / v.w);
		} else {
			dh = maxH;
			dw = (int)((int64_t)v.w * maxH / v.h);
		}
		if (dw < 1)
			dw = 1;
		if (dh < 1)
			dh = 1;
	}
	uint32_t* px = (uint32_t*)malloc((size_t)dw * dh * 4);
	if (!px) {
		free(buf);
		return false;
	}

	/* area average in premultiplied space: each source pixel is visited
	 * once per destination pixel it overlaps -> O(srcW * srcH) */
	for (int dy = 0; dy < dh; dy++) {
		int sy0 = dy * v.h / dh;
		int sy1 = (dy + 1) * v.h / dh;
		if (sy1 <= sy0)
			sy1 = sy0 + 1;
		for (int dx = 0; dx < dw; dx++) {
			int sx0 = dx * v.w / dw;
			int sx1 = (dx + 1) * v.w / dw;
			if (sx1 <= sx0)
				sx1 = sx0 + 1;
			uint32_t sa = 0, sr = 0, sg = 0, sb = 0, n = 0;
			for (int sy = sy0; sy < sy1; sy++) {
				for (int sx = sx0; sx < sx1; sx++) {
					uint32_t p = _fetch(&v, sx, sy);
					uint32_t a = p >> 24;
					sa += a;
					sr += ((p >> 16) & 0xFF) * a;
					sg += ((p >> 8) & 0xFF) * a;
					sb += (p & 0xFF) * a;
					n++;
				}
			}
			uint32_t outp = 0;
			if (sa) {
				uint32_t a = sa / n;
				outp = (a << 24) | ((sr / sa) << 16) | ((sg / sa) << 8) | (sb / sa);
			}
			px[dy * dw + dx] = outp;
		}
	}
	free(buf);
	out->w = (uint16_t)dw;
	out->h = (uint16_t)dh;
	out->px = px;
	return true;
}

void XlIconFree(XlIcon* ico) {
	if (ico && ico->px) {
		free(ico->px);
		ico->px = NULL;
	}
	if (ico)
		ico->w = ico->h = 0;
}

/* XlIconTint -- recolour a glyph icon, keeping its alpha (button glyphs) */
void XlIconTint(XlIcon* ico, uint32_t rgb) {
	if (!ico || !ico->px)
		return;
	rgb &= 0x00FFFFFFu;
	for (int i = 0; i < ico->w * ico->h; i++)
		ico->px[i] = (ico->px[i] & 0xFF000000u) | rgb;
}

/*
 * XlIconDraw -- alpha-composite an icon into an ARGB surface
 * @param dst, dstW, dstH -- target surface
 * @param clip -- optional clip in target coordinates
 */
void XlIconDraw(uint32_t* dst, int dstW, int dstH, const XlIcon* ico, int x, int y, const XlRect* clip) {
	if (!dst || !ico || !ico->px)
		return;
	int x0 = x, y0 = y, x1 = x + ico->w, y1 = y + ico->h;
	int cx0 = 0, cy0 = 0, cx1 = dstW, cy1 = dstH;
	if (clip) {
		if (clip->x > cx0)
			cx0 = clip->x;
		if (clip->y > cy0)
			cy0 = clip->y;
		if (clip->x + clip->w < cx1)
			cx1 = clip->x + clip->w;
		if (clip->y + clip->h < cy1)
			cy1 = clip->y + clip->h;
	}
	if (x0 < cx0)
		x0 = cx0;
	if (y0 < cy0)
		y0 = cy0;
	if (x1 > cx1)
		x1 = cx1;
	if (y1 > cy1)
		y1 = cy1;
	for (int j = y0; j < y1; j++) {
		const uint32_t* s = ico->px + (size_t)(j - y) * ico->w + (x0 - x);
		uint32_t* d = dst + (size_t)j * dstW + x0;
		for (int i = x0; i < x1; i++, s++, d++) {
			uint32_t a = *s >> 24;
			if (a)
				*d = XlBlend(*d, *s, a);
		}
	}
}
