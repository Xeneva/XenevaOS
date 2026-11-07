/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2023, Manas Kamal Choudhury
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

#include "font.h"
#include <sys/mman.h>
#include <sys/_kefile.h>
#include <stdlib.h>
#include "ttf.h"
#include <sys/_keftmngr.h>
#include <ft2build.h>
#include "draw.h"
#include "color.h"
#include <math.h>
#include FT_FREETYPE_H
#if defined(ARCH_ARM64)
#include <arm_neon.h>
#endif

#ifndef _USE_FREETYPE
/* our libc only has acosf (float), no acos (double). stb hides STBTT_cos
 * and STBTT_acos behind the same #ifndef STBTT_cos guard so if you only
 * define one, stb silently clobbers the other with its default.
 * learned that one the hard way --axiss */
#define STBTT_cos(x)	cos(x)
#define STBTT_acos(x)	((double)acosf((float)(x)))
#define STBTT_assert(x) ((void)0)
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#endif

#ifndef _USE_FREETYPE
static void ChFontClearGlyphCache(ChFont* font) {
	for (int i = 0; i < CH_FONT_GLYPH_CACHE_SIZE; ++i) {
		if (font->glyphCache[i].bitmap)
			stbtt_FreeBitmap(font->glyphCache[i].bitmap, NULL);
		memset(&font->glyphCache[i], 0, sizeof(font->glyphCache[i]));
	}
}

static ChFontGlyphCacheEntry* ChFontGetCachedGlyph(ChFont* font, unsigned char codepoint) {
	ChFontGlyphCacheEntry* glyph = &font->glyphCache[codepoint];
	if (glyph->loaded)
		return glyph;

	int advance = 0;
	int lsb = 0;
	stbtt_GetCodepointHMetrics(&font->stbFont, codepoint, &advance, &lsb);
	int width = 0;
	int height = 0;
	int xOffset = 0;
	int yOffset = 0;
	glyph->bitmap = stbtt_GetCodepointBitmap(&font->stbFont,
											 font->stbScale,
											 font->stbScale,
											 codepoint,
											 &width,
											 &height,
											 &xOffset,
											 &yOffset);
	glyph->width = (int16_t)width;
	glyph->height = (int16_t)height;
	glyph->xOffset = (int16_t)xOffset;
	glyph->yOffset = (int16_t)yOffset;
	glyph->advance = (int16_t)(advance * font->stbScale);
	glyph->loaded = 1;
	return glyph;
}

static inline uint32_t ChFontBlendCoverage(uint32_t dst, uint32_t src, uint8_t coverage) {
	if (coverage == 255)
		return 0xFF000000U | (src & 0x00FFFFFFU);
	uint32_t inv = 255U - coverage;
	uint32_t red =
		((((src >> 16) & 0xFFU) * coverage) + (((dst >> 16) & 0xFFU) * inv) + 127U) / 255U;
	uint32_t green =
		((((src >> 8) & 0xFFU) * coverage) + (((dst >> 8) & 0xFFU) * inv) + 127U) / 255U;
	uint32_t blue = (((src & 0xFFU) * coverage) + ((dst & 0xFFU) * inv) + 127U) / 255U;
	return 0xFF000000U | (red << 16) | (green << 8) | blue;
}

#if defined(ARCH_ARM64)
/* Blend atlas coverage four pixels at a time on the compositor's hot text path. --axiss */
static void
ChFontBlendFour(uint32_t* dest, uint16x4_t cov, uint16x4_t cr, uint16x4_t cg, uint16x4_t cb) {
	uint32x4_t dst4 = vld1q_u32(dest);
	uint16x4_t dr = vmovn_u32(vshrq_n_u32(vandq_u32(dst4, vdupq_n_u32(0x00FF0000)), 16));
	uint16x4_t dg = vmovn_u32(vshrq_n_u32(vandq_u32(dst4, vdupq_n_u32(0x0000FF00)), 8));
	uint16x4_t db = vmovn_u32(vandq_u32(dst4, vdupq_n_u32(0x000000FF)));
	uint16x4_t inv = vsub_u16(vdup_n_u16(255), cov);
	uint16x4_t or_ = vshr_n_u16(vmla_u16(vmul_u16(cr, cov), dr, inv), 8);
	uint16x4_t og = vshr_n_u16(vmla_u16(vmul_u16(cg, cov), dg, inv), 8);
	uint16x4_t ob = vshr_n_u16(vmla_u16(vmul_u16(cb, cov), db, inv), 8);
	uint32x4_t out = vorrq_u32(vdupq_n_u32(0xFF000000), vshlq_n_u32(vmovl_u16(or_), 16));
	out = vorrq_u32(out, vshlq_n_u32(vmovl_u16(og), 8));
	out = vorrq_u32(out, vmovl_u16(ob));
	vst1q_u32(dest, out);
}
#endif

static void
ChFontBlitCoverageRow(uint32_t* dest, const uint8_t* source, int width, uint32_t color) {
	int x = 0;
#if defined(ARCH_ARM64)
	uint32x4_t color4 = vdupq_n_u32(0xFF000000U | (color & 0x00FFFFFFU));
	uint16x4_t cr = vdup_n_u16((color >> 16) & 0xFF);
	uint16x4_t cg = vdup_n_u16((color >> 8) & 0xFF);
	uint16x4_t cb = vdup_n_u16(color & 0xFF);
	for (; x + 8 <= width; x += 8) {
		uint8x8_t cov8 = vld1_u8(source + x);
		uint64_t bits = vget_lane_u64(vreinterpret_u64_u8(cov8), 0);
		if (bits == 0)
			continue;
		if (bits == ~0ULL) {
			vst1q_u32(dest + x, color4);
			vst1q_u32(dest + x + 4, color4);
			continue;
		}
		uint16x8_t cov16 = vmovl_u8(cov8);
		ChFontBlendFour(dest + x, vget_low_u16(cov16), cr, cg, cb);
		ChFontBlendFour(dest + x + 4, vget_high_u16(cov16), cr, cg, cb);
	}
#endif
	for (; x < width; x++) {
		if (source[x])
			dest[x] = ChFontBlendCoverage(dest[x], color, source[x]);
	}
}

static void ChFontFreeAtlas(ChFont* font) {
	if (font->atlasPixels) {
		free(font->atlasPixels);
		font->atlasPixels = NULL;
	}
	font->atlasReady = 0;
	font->atlasW = 0;
	font->atlasH = 0;
}

static void ChFontBakeAtlas(ChFont* font) {
	/* Bake printable ASCII once per size; uncached codepoints keep the glyph fallback. --axiss */
	ChFontFreeAtlas(font);
	int w = CH_FONT_ATLAS_W;
	int h = CH_FONT_ATLAS_H;
	uint8_t* pixels = (uint8_t*)malloc((size_t)w * (size_t)h);
	if (!pixels)
		return;
	memset(pixels, 0, (size_t)w * (size_t)h);
	int used = stbtt_BakeFontBitmap(font->buffer,
									0,
									(float)font->fontSz,
									pixels,
									w,
									h,
									CH_FONT_ATLAS_FIRST,
									CH_FONT_ATLAS_COUNT,
									font->atlasChars);
	if (used <= 0) {
		free(pixels);
		return;
	}
	font->atlasPixels = pixels;
	font->atlasW = w;
	font->atlasH = h;
	font->atlasReady = 1;
}

static int ChFontBlitBaked(ChCanvas* canv,
						   ChFont* font,
						   unsigned char cp,
						   int penx,
						   int peny,
						   uint32_t color,
						   const ChRect* clip,
						   int* advance_out) {
	if (!font->atlasReady || cp < CH_FONT_ATLAS_FIRST ||
		cp >= CH_FONT_ATLAS_FIRST + CH_FONT_ATLAS_COUNT)
		return 0;
	const stbtt_bakedchar* baked = &font->atlasChars[cp - CH_FONT_ATLAS_FIRST];
	int gw = (int)baked->x1 - (int)baked->x0;
	int gh = (int)baked->y1 - (int)baked->y0;
	if (advance_out)
		*advance_out = (int)baked->xadvance;
	if (gw <= 0 || gh <= 0)
		return 1;

	int left = penx + (int)floorf(baked->xoff);
	int top = peny + (int)floorf(baked->yoff);
	int right = left + gw;
	int bottom = top + gh;
	int clipLeft = 0;
	int clipTop = 0;
	int clipRight = canv->canvasWidth;
	int clipBottom = canv->canvasHeight;
	if (clip) {
		if (clip->x > clipLeft)
			clipLeft = clip->x;
		if (clip->y > clipTop)
			clipTop = clip->y;
		if (clip->x + clip->w < clipRight)
			clipRight = clip->x + clip->w;
		if (clip->y + clip->h < clipBottom)
			clipBottom = clip->y + clip->h;
	}
	int src_x = (int)baked->x0;
	int src_y = (int)baked->y0;
	/* A negative side bearing paints this glyph over the previous one,
	 * so "pl" collapses into one mark. Keep the ink at the pen. */
	if (left < penx) {
		src_x += penx - left;
		left = penx;
	}
	if (left < clipLeft) {
		src_x += clipLeft - left;
		left = clipLeft;
	}
	if (top < clipTop) {
		src_y += clipTop - top;
		top = clipTop;
	}
	if (right > clipRight)
		right = clipRight;
	if (bottom > clipBottom)
		bottom = clipBottom;
	if (left >= right || top >= bottom)
		return 1;

	for (int y = top; y < bottom; y++) {
		const uint8_t* source = font->atlasPixels + (src_y + (y - top)) * font->atlasW + src_x;
		uint32_t* dest = canv->buffer + y * canv->canvasWidth + left;
		ChFontBlitCoverageRow(dest, source, right - left, color);
	}
	return 1;
}

static void ChFontBlitGlyph(ChCanvas* canv,
							const ChFontGlyphCacheEntry* glyph,
							int penx,
							int peny,
							uint32_t color,
							const ChRect* clip) {
	if (!glyph->bitmap || glyph->width <= 0 || glyph->height <= 0)
		return;

	int left = penx + glyph->xOffset;
	int top = peny + glyph->yOffset;
	int right = left + glyph->width;
	int bottom = top + glyph->height;
	/* A negative side bearing paints this glyph over the previous one.
	 * Keep the ink at the pen. right stays the original edge so the
	 * source index does not read past the bitmap. */
	if (left < penx)
		left = penx;
	int clipLeft = 0;
	int clipTop = 0;
	int clipRight = canv->canvasWidth;
	int clipBottom = canv->canvasHeight;
	if (clip) {
		if (clip->x > clipLeft)
			clipLeft = clip->x;
		if (clip->y > clipTop)
			clipTop = clip->y;
		if (clip->x + clip->w < clipRight)
			clipRight = clip->x + clip->w;
		if (clip->y + clip->h < clipBottom)
			clipBottom = clip->y + clip->h;
	}
	if (left < clipLeft)
		left = clipLeft;
	if (top < clipTop)
		top = clipTop;
	if (right > clipRight)
		right = clipRight;
	if (bottom > clipBottom)
		bottom = clipBottom;
	if (left >= right || top >= bottom)
		return;

	for (int y = top; y < bottom; ++y) {
		const uint8_t* source = glyph->bitmap + (y - (peny + glyph->yOffset)) * glyph->width +
								(left - (penx + glyph->xOffset));
		uint32_t* destination = canv->buffer + y * canv->canvasWidth + left;
		ChFontBlitCoverageRow(destination, source, right - left, color);
	}
}
#endif

#define UTF8_INVALID 0xFFFD

size_t ChFontDecodeUTF8(const uint8_t* s, size_t maxLen, uint32_t* out_cp) {
	if (maxLen == 0) {
		*out_cp = UTF8_INVALID;
		return 0;
	}

	uint8_t b0 = s[0];

	/**
	 * Check if character is ASCII or UTF8 formatted
	 */
	if (b0 < 0x80) {
		*out_cp = b0;
		return 1;
	}

	int len;
	uint32_t cp;
	uint32_t min_cp;

	if ((b0 & 0xE0) == 0xC0) {
		len = 2;
		cp = b0 & 0x1F;
		min_cp = 0x80;
	} else if ((b0 & 0xF0) == 0xE0) {
		len = 3;
		cp = b0 & 0x0F;
		min_cp = 0x800;
	} else if ((b0 & 0xF8) == 0xF0) {
		len = 4;
		cp = b0 & 0x07;
		min_cp = 0x10000;
	} else {
		*out_cp = UTF8_INVALID;
		return 1;
	}

	if ((size_t)len > maxLen) {
		*out_cp = UTF8_INVALID;
		return 1;
	}

	for (int i = 1; i < len; i++) {
		uint8_t b = s[i];
		if ((b & 0xC0) != 0x80) {
			*out_cp = UTF8_INVALID;
			return 1;
		}
		cp = (cp << 6) | (b & 0x3F);
	}

	if (cp < min_cp) {
		*out_cp = UTF8_INVALID;
		return 1;
	}

	if (cp >= 0xD800 && cp <= 0xDFFF) {
		*out_cp = UTF8_INVALID;
		return 1;
	}

	if (cp > 0x10FFFF) {
		*out_cp = UTF8_INVALID;
		return 1;
	}

	*out_cp = cp;
	return len;
}

/* 
 * ChInitialiseFont -- initialise a font by a name
 * @param fontname -- name of the font
 */
ChFont* ChInitialiseFont(char* fontname) {
	int id = _KeGetFontID(fontname);
	/* AuFTMngrGetFontID returns -1 when it cant find the font, not 0. this
	 * was only checking == 0 so -1 slipped right through as "valid", then
	 * unpacked into garbage _font_id/_font_key. _KeObtainSharedMem usually
	 * failed on that garbage but not always, which made this a fun one to
	 * chase down --axiss */
	if (id <= 0)
		return NULL;
	int _font_id = (id >> 16) & UINT16_MAX;
	int _font_key = id & UINT16_MAX;
	void* buff = _KeObtainSharedMem(_font_id, NULL, 0);
	if (!buff)
		return NULL;

	/* same -1 vs 0 bug as above. AuFTMngrGetFontSize returns -1 on failure
	 * but it was getting shoved straight into a uint32_t, so -1 becomes
	 * 0xFFFFFFFF. never zero, so the `!fileSz` check below never caught it --axiss */
	int _fileSz = _KeGetFontSize(fontname);
	if (_fileSz <= 0)
		return NULL;
	uint32_t fileSz = (uint32_t)_fileSz;

	ChFont* font = (ChFont*)malloc(sizeof(ChFont));
	memset(font, 0, sizeof(ChFont));
	font->buffer = (uint8_t*)buff;
	font->fileSz = fileSz;
	font->fontSz = 32 / 72.f * 96;
	font->key = _font_key;
	font->kern = 0;
#ifdef _USE_FREETYPE
	/* none of these three error checks existed before. if any of them failed,
	 * font->face (or ->size) stays NULL and the code below just dereferenced
	 * it anyway, crashing whatever app happened to be loading a font right
	 * then --axiss */
	FT_Error err = 0;
	err = FT_Init_FreeType(&font->lib);
	if (err) {
		free(font);
		return NULL;
	}
	err = FT_New_Memory_Face(font->lib, font->buffer, font->fileSz, 0, &font->face);
	if (err) {
		free(font);
		return NULL;
	}

	err = FT_Set_Pixel_Sizes(font->face, 0, 32);
	if (err) {
		free(font);
		return NULL;
	}
	font->slot = font->face->glyph;
	font->lineHeight = font->face->size->metrics.height / 64;
	font->fontHeight = 32 / 72.f * 96;
#else
	int stbOffset = stbtt_GetFontOffsetForIndex(font->buffer, 0);
	if (stbOffset < 0 || !stbtt_InitFont(&font->stbFont, font->buffer, stbOffset)) {
		free(font);
		return NULL;
	}
	font->fontHeight = (uint32_t)(32 / 72.f * 96);
	font->stbScale = stbtt_ScaleForPixelHeight(&font->stbFont, (float)font->fontHeight);
	stbtt_GetFontVMetrics(&font->stbFont, &font->stbAscent, &font->stbDescent, &font->stbLineGap);
	font->lineHeight =
		(uint32_t)((font->stbAscent - font->stbDescent + font->stbLineGap) * font->stbScale);
	ChFontBakeAtlas(font);
#endif
	/* start decoding true type font */
	//TTFLoadFont(canv,font->buffer);
	return font;
}

/*
 * ChFontSetSize -- set a font size
 * @param font -- Pointer to font
 * @param size -- size of the font
 */
void ChFontSetSize(ChFont* font, int size) {
	if (!font)
		return;
	uint32_t pixelSize = (uint32_t)(size / 72.f * 96);
	if (pixelSize == 0)
		pixelSize = 1;
	if (font->fontSz == pixelSize)
		return;
	font->fontSz = pixelSize;
#ifdef _USE_FREETYPE
	FT_Set_Pixel_Sizes(font->face, 0, font->fontSz);
#else
	ChFontClearGlyphCache(font);
	font->stbScale = stbtt_ScaleForPixelHeight(&font->stbFont, (float)font->fontSz);
	font->lineHeight =
		(uint32_t)((font->stbAscent - font->stbDescent + font->stbLineGap) * font->stbScale);
	ChFontBakeAtlas(font);
#endif
	font->fontHeight = font->fontSz;
}

/*
 * ChFontDrawText -- draws a text using desired font
 * @param canv -- Pointer to canvas
 * @param font -- Pointer to font
 * @param string -- string to draw
 * @param penx -- x coordinate
 * @param peny -- y coordinate
 * @param sz -- font size
 * @param color -- color of the font
 */
void ChFontDrawText(
	ChCanvas* canv, ChFont* font, char* string, int penx, int peny, uint32_t sz, uint32_t color) {
#ifdef _USE_FREETYPE
	if (!font)
		return;
	int w = font->face->glyph->metrics.width;
	int h = font->face->glyph->metrics.height;
	FT_Bool use_kerning = FT_HAS_KERNING(font->face);
	uint32_t prev = 0;
	FT_UInt glyfIndx;
	FT_Error err = 0;

	const uint8_t* p8 = (const uint8_t*)string;
	size_t remaining = strlen(string);
	while (*p8) {
		uint32_t codePoint;
		size_t n = ChFontDecodeUTF8(p8, remaining, &codePoint);
		p8 += n;
		remaining -= n;

		glyfIndx = FT_Get_Char_Index(font->face, codePoint);
		err = FT_Load_Glyph(font->face, glyfIndx, FT_LOAD_RENDER);

		if (err)
			continue;

		if (use_kerning && prev && glyfIndx) {
			FT_Vector delta;
			FT_Get_Kerning(font->face, prev, glyfIndx, FT_KERNING_DEFAULT, &delta);
			penx += delta.x >> 6;
		}

		int x_v = penx + font->face->glyph->bitmap_left;
		int y_v = peny - font->face->glyph->bitmap_top;

		for (int i = x_v, p = 0;
			 i < x_v + font->face->glyph->bitmap.width && p < font->face->glyph->bitmap.width;
			 i++, p++) {
			for (int j = y_v, q = 0;
				 j < y_v + font->face->glyph->bitmap.rows && q < font->face->glyph->bitmap.rows;
				 j++, q++) {
				if (font->face->glyph->bitmap.buffer[q * font->face->glyph->bitmap.width + p] > 0) {
					double val =
						font->face->glyph->bitmap.buffer[q * font->face->glyph->bitmap.width + p] *
						1.0 / 255;
					canv->buffer[i + j * canv->canvasWidth] =
						ChColorAlphaBlend(canv->buffer[i + j * canv->canvasWidth],
										  color,
										  val); //canv->buffer[i + j * canv->canvasWidth]
				} else if (font->face->glyph->bitmap.buffer[q * font->face->glyph->bitmap.width] ==
						   255)
					canv->buffer[i + j * canv->canvasWidth] = color;
			}
		}

		penx += font->face->glyph->advance.x >> 6;
		peny += font->face->glyph->advance.y >> 6;
		prev = glyfIndx;
		string++;
	}
#else
	if (!font)
		return;
	int prevCp = 0;
	while (*string) {
		unsigned char cp = (unsigned char)*string;
		if (prevCp) {
			int kern = stbtt_GetCodepointKernAdvance(&font->stbFont, prevCp, cp);
			penx += (int)(kern * font->stbScale);
		}
		int advance = 0;
		if (!ChFontBlitBaked(canv, font, cp, penx, peny, color, NULL, &advance)) {
			ChFontGlyphCacheEntry* glyph = ChFontGetCachedGlyph(font, cp);
			ChFontBlitGlyph(canv, glyph, penx, peny, color, NULL);
			advance = glyph->advance;
		}
		penx += advance;
		prevCp = cp;
		string++;
	}
#endif
}

/*
* ChFontDrawChar -- draws a character using desired font
* @param canv -- Pointer to canvas
* @param font -- Pointer to font
* @param string -- string to draw
* @param penx -- x coordinate
* @param peny -- y coordinate
* @param sz -- font size
* @param color -- color of the font
*/
void ChFontDrawChar(
	ChCanvas* canv, ChFont* font, char c, int penx, int peny, uint32_t sz, uint32_t color) {
#ifdef _USE_FREETYPE
	if (!font)
		return;
	if (penx >= canv->canvasWidth)
		return;
	if (peny >= canv->canvasHeight)
		return;

	int w = font->face->glyph->metrics.width;
	int h = font->face->glyph->metrics.height;
	FT_Bool use_kerning = FT_HAS_KERNING(font->face);
	FT_UInt glyfIndx;
	FT_Error err = 0;

	uint8_t b = (uint8_t)c;

	if (font->utf8_remaining > 0) {
		if ((b & 0xC0) == 0x80) {
			font->utf8_cp = (font->utf8_cp << 6) | (b & 0x3F);
			font->utf8_remaining--;
			if (font->utf8_remaining > 0)
				return;
		} else {
			font->utf8_remaining = 0;
			font->utf8_cp = 0;
			ChFontDrawChar(canv, font, c, penx, peny, sz, color);
			return;
		}
	} else {
		if (b < 0x80) {
			font->utf8_cp = b;
			font->utf8_remaining = 0;
		} else if ((b & 0xE0) == 0xC0) {
			font->utf8_cp = b & 0x1F;
			font->utf8_remaining = 1;
			return;
		} else if ((b & 0xF0) == 0xE0) {
			font->utf8_cp = b & 0x0F;
			font->utf8_remaining = 2;
			return;
		} else if ((b & 0xF8) == 0xF0) {
			font->utf8_cp = b & 0x07;
			font->utf8_remaining = 3;
			return;
		} else {
			font->utf8_cp = 0xFFFD;
			font->utf8_remaining = 0;
		}
	}

	uint32_t cp = font->utf8_cp;
	font->utf8_cp = 0;
	font->utf8_remaining = 0;
	glyfIndx = FT_Get_Char_Index(font->face, cp);
	err = FT_Load_Glyph(font->face, glyfIndx, FT_LOAD_RENDER);
	if (err)
		return;

	if (use_kerning && font->kern && glyfIndx) {
		FT_Vector delta;
		FT_Get_Kerning(font->face, font->kern, glyfIndx, FT_KERNING_DEFAULT, &delta);
		penx += delta.x >> 6;
	}

	int x_v = penx + font->face->glyph->bitmap_left;
	int y_v = peny - font->face->glyph->bitmap_top;
	int b_w = font->face->glyph->bitmap.width;

	for (int j = y_v, q = 0; j < y_v + font->face->glyph->bitmap.rows; j++, q++) {
		for (int i = x_v, p = 0; i < x_v + font->face->glyph->bitmap.width; i++, p++) {
			if (i < 0 || j < 0 || i >= canv->canvasWidth || j >= canv->canvasHeight)
				continue;
			if (font->face->glyph->bitmap.buffer[q * font->face->glyph->bitmap.width + p] > 0) {
				double val =
					font->face->glyph->bitmap.buffer[q * font->face->glyph->bitmap.width + p] *
					1.0 / 255;
				canv->buffer[i + j * canv->canvasWidth] =
					ChColorAlphaBlend(canv->buffer[i + j * canv->canvasWidth], color, val);
			} else if (font->face->glyph->bitmap.buffer[q * font->face->glyph->bitmap.width] == 255)
				canv->buffer[i + j * canv->canvasWidth] = color;
		}
	}
	font->kern = glyfIndx;
#else
	if (!font)
		return;
	if (penx >= canv->canvasWidth)
		return;
	if (peny >= canv->canvasHeight)
		return;

	unsigned char cp = (unsigned char)c;
	/* Width is measured one character at a time, with no previous glyph.
	 * A kern shift here paints this glyph on top of the previous one. */
	if (!ChFontBlitBaked(canv, font, cp, penx, peny, color, NULL, NULL)) {
		ChFontGlyphCacheEntry* glyph = ChFontGetCachedGlyph(font, cp);
		ChFontBlitGlyph(canv, glyph, penx, peny, color, NULL);
	}
	font->kern = (uint32_t)cp;
#endif
}

/*
* ChFontDrawCharClipped -- draws a character using desired font
* @param canv -- Pointer to canvas
* @param font -- Pointer to font
* @param string -- string to draw
* @param penx -- x coordinate
* @param peny -- y coordinate
* @param color -- color of the font
* @param limit -- clipping limits
*/
void ChFontDrawCharClipped(
	ChCanvas* canv, ChFont* font, char c, int penx, int peny, uint32_t color, ChRect* limit) {
#ifdef _USE_FREETYPE
	if (!font)
		return;
	if (!limit)
		return;
	if (penx >= canv->canvasWidth)
		return;
	if (peny >= canv->canvasHeight)
		return;

	if (!font)
		return;

	/*if ((penx >= canv->canvasWidth) || (peny >= canv->canvasHeight))
		return 1;*/

	if (peny < limit->y)
		return;

	if (penx < limit->x)
		return;

	uint8_t b = (uint8_t)c;

	if (font->utf8_remaining > 0) {
		if ((b & 0xC0) == 0x80) {
			font->utf8_cp = (font->utf8_cp << 6) | (b & 0x3F);
			font->utf8_remaining--;
			if (font->utf8_remaining > 0)
				return;
		} else {
			font->utf8_remaining = 0;
			font->utf8_cp = 0;
			ChFontDrawCharClipped(canv, font, c, penx, peny, color, limit);
			return;
		}
	} else {
		if (b < 0x80) {
			font->utf8_cp = b;
			font->utf8_remaining = 0;
		} else if ((b & 0xE0) == 0xC0) {
			font->utf8_cp = b & 0x1F;
			font->utf8_remaining = 1;
			return;
		} else if ((b & 0xF0) == 0xE0) {
			font->utf8_cp = b & 0x0F;
			font->utf8_remaining = 2;
			return;
		} else if ((b & 0xF8) == 0xF0) {
			font->utf8_cp = b & 0x07;
			font->utf8_remaining = 3;
			return;
		} else {
			font->utf8_cp = 0xFFFD;
			font->utf8_remaining = 0;
		}
	}

	uint32_t cp = font->utf8_cp;
	font->utf8_cp = 0;
	font->utf8_remaining = 0;

	int w = font->face->glyph->metrics.width;
	int h = font->face->glyph->metrics.height;
	FT_Bool use_kerning = FT_HAS_KERNING(font->face);
	FT_UInt glyfIndx;
	FT_Error err = 0;
	glyfIndx = FT_Get_Char_Index(font->face, cp);
	err = FT_Load_Glyph(font->face, glyfIndx, FT_LOAD_RENDER);
	if (err)
		return;

	if (use_kerning && font->kern && glyfIndx) {
		FT_Vector delta;
		FT_Get_Kerning(font->face, font->kern, glyfIndx, FT_KERNING_DEFAULT, &delta);
		penx += delta.x >> 6;
	}

	int x_v = penx + font->face->glyph->bitmap_left;
	int y_v = peny - font->face->glyph->bitmap_top;
	int b_w = font->face->glyph->bitmap.width;
	int draw_width = font->face->glyph->bitmap.width;
	int draw_height = font->face->glyph->bitmap.rows;

	/* here p = x and q = y*/
	int buff_p_off = 0;
	int buff_q_off = 0;

	/* Clip the text within clip boundary*/
	if (limit->x > x_v) {
		buff_p_off = limit->x - x_v;
		x_v = limit->x;
	}

	if (limit->y > y_v) {
		buff_q_off = limit->y - y_v;
		y_v = limit->y;
	}

	int screen_draw_height = draw_height - buff_q_off;
	if ((y_v + screen_draw_height) > (limit->y + limit->h))
		screen_draw_height = (limit->y + limit->h) - y_v;

	if (screen_draw_height < 0)
		screen_draw_height = 0;

	int screen_draw_width = draw_width - buff_p_off;
	if ((x_v + screen_draw_width) > (limit->x + limit->w))
		screen_draw_width = (limit->x + limit->w) - x_v;

	if (screen_draw_width < 0)
		screen_draw_width = 0;

	for (int j = y_v, q = buff_q_off; j < y_v + screen_draw_height && q < draw_height; j++, q++) {
		for (int i = x_v, p = buff_p_off; i < x_v + screen_draw_width && p < draw_width; i++, p++) {
			if (i < 0 || j < 0 || i >= canv->canvasWidth || j >= canv->canvasHeight)
				continue;
			if (font->face->glyph->bitmap.buffer[q * font->face->glyph->bitmap.width + p] > 0) {
				double val =
					font->face->glyph->bitmap.buffer[q * font->face->glyph->bitmap.width + p] *
					1.0 / 255;
				canv->buffer[i + j * canv->canvasWidth] =
					ChColorAlphaBlend(canv->buffer[i + j * canv->canvasWidth], color, val);
			} else if (font->face->glyph->bitmap.buffer[q * font->face->glyph->bitmap.width] == 255)
				canv->buffer[i + j * canv->canvasWidth] = color;
		}
	}
	font->kern = glyfIndx;
	penx += font->face->glyph->advance.x >> 6;
	peny += font->face->glyph->advance.y >> 6;
#else
	if (!font)
		return;
	if (!limit)
		return;
	unsigned char cp = (unsigned char)c;
	/* Same as ChFontDrawChar: the caller already advanced by
	 * ChFontGetWidthChar, so a second kern shift stacks 'l' onto 'c'. */
	if (!ChFontBlitBaked(canv, font, cp, penx, peny, color, limit, NULL)) {
		ChFontGlyphCacheEntry* glyph = ChFontGetCachedGlyph(font, cp);
		ChFontBlitGlyph(canv, glyph, penx, peny, color, limit);
	}
	font->kern = (uint32_t)cp;
#endif
}

/*
 * ChFontGetWidth -- return the total width of font in
 * pixel size
 * @param font -- Pointer to font
 * @param string -- total string
 */
int64_t ChFontGetWidth(ChFont* font, char* string) {
#ifdef _USE_FREETYPE
	if (!font)
		return -1;
	size_t font_width = 0;
	size_t penx = 0;
	int string_width = 0;
	FT_Error err = 0;
	while (*string) {
		err = FT_Load_Char(font->face, *string, FT_LOAD_ADVANCE_ONLY);
		if (err)
			continue;
		penx += font->face->glyph->advance.x >> 6;
		string++;
	}
	font_width = penx;
	if (font_width == 0) {
		int64_t bbox_xmax =
			FT_MulFix(font->face->bbox.xMax, font->face->size->metrics.x_scale) >> 6;
		int64_t bbox_xmin =
			FT_MulFix(font->face->bbox.xMin, font->face->size->metrics.x_scale) >> 6;
		font_width = bbox_xmax - bbox_xmin;
	}
	return font_width;
#else
	if (!font)
		return -1;
	int64_t width = 0;
	int prevCp = 0;
	while (*string) {
		int cp = (unsigned char)*string;
		if (prevCp)
			width +=
				(int)(stbtt_GetCodepointKernAdvance(&font->stbFont, prevCp, cp) * font->stbScale);
		if (font->atlasReady && cp >= CH_FONT_ATLAS_FIRST &&
			cp < CH_FONT_ATLAS_FIRST + CH_FONT_ATLAS_COUNT)
			width += (int64_t)font->atlasChars[cp - CH_FONT_ATLAS_FIRST].xadvance;
		else {
			int advance, lsb;
			stbtt_GetCodepointHMetrics(&font->stbFont, cp, &advance, &lsb);
			width += (int64_t)(advance * font->stbScale);
		}
		prevCp = cp;
		string++;
	}
	return width;
#endif
}

/*
* ChFontGetWidthChar -- return the total width of font in
* pixel size of one character
* @param font -- Pointer to font
* @param c -- character
*/
int64_t ChFontGetWidthChar(ChFont* font, char c) {
#ifdef _USE_FREETYPE
	if (!font)
		return -1;
	size_t font_width = 0;
	size_t penx = 0;
	int string_width = 0;
	FT_Error err = 0;
	err = FT_Load_Char(font->face, c, FT_LOAD_ADVANCE_ONLY);
	if (err)
		return 0;
	penx += font->face->glyph->advance.x >> 6;
	font_width = penx;
	if (font_width == 0) {
		int64_t bbox_xmax =
			FT_MulFix(font->face->bbox.xMax, font->face->size->metrics.x_scale) >> 6;
		int64_t bbox_xmin =
			FT_MulFix(font->face->bbox.xMin, font->face->size->metrics.x_scale) >> 6;
		font_width = bbox_xmax - bbox_xmin;
	}
	return font_width;
#else
	if (!font)
		return -1;
	unsigned char cp = (unsigned char)c;
	if (font->atlasReady && cp >= CH_FONT_ATLAS_FIRST &&
		cp < CH_FONT_ATLAS_FIRST + CH_FONT_ATLAS_COUNT) {
		const stbtt_bakedchar* b = &font->atlasChars[cp - CH_FONT_ATLAS_FIRST];
		int gw = (int)b->x1 - (int)b->x0;
		float adv = b->xadvance;
		float right = b->xoff + (float)gw;
		/* Truncating 5.95 to 5 puts the next stem inside 'c', so "cl"
		 * reads as "d". Keep one pixel of air when the ink is wider
		 * than the advance. */
		if (gw > 0 && right > adv)
			adv = right + 1.f;
		if (adv < 0.f)
			adv = 0.f;
		return (int64_t)(adv + 0.5f);
	}
	int advance, lsb;
	stbtt_GetCodepointHMetrics(&font->stbFont, cp, &advance, &lsb);
	{
		float adv = advance * font->stbScale;
		if (adv < 0.f)
			adv = 0.f;
		return (int64_t)(adv + 0.5f);
	}
#endif
}

/*
 * ChFontGetHeight -- return the total height of font
 * in pixel size
 * @param font -- Pointer to font
 * @param string -- total string
 */
int64_t ChFontGetHeight(ChFont* font, char* string) {
#ifdef _USE_FREETYPE
	if (!font)
		return -1;
	size_t font_height = 0;
	size_t peny = 0;
	FT_Error err = 0;
	while (*string) {
		err = FT_Load_Char(font->face, *string, FT_LOAD_ADVANCE_ONLY);
		if (err)
			continue;
		peny += font->face->glyph->advance.y >> 6;
		string++;
	}
	font_height = peny;
	if (font_height == 0) {
		int64_t bbox_ymax =
			FT_MulFix(font->face->bbox.yMax, font->face->size->metrics.y_scale) >> 6;
		int64_t bbox_ymin =
			FT_MulFix(font->face->bbox.yMin, font->face->size->metrics.y_scale) >> 6;
		font_height = bbox_ymax - bbox_ymin;
	}
	return font_height;
#else
	if (!font)
		return -1;
	return font->lineHeight;
#endif
}

/*
* ChFontGetHeightChar -- return the total width of font in
* pixel size of one character
* @param font -- Pointer to font
* @param c -- character
*/
int64_t ChFontGetHeightChar(ChFont* font, char c) {
#ifdef _USE_FREETYPE
	if (!font)
		return -1;
	size_t font_h = 0;
	size_t peny = 0;
	int string_width = 0;
	FT_Error err = 0;
	err = FT_Load_Char(font->face, c, FT_LOAD_ADVANCE_ONLY);
	if (err)
		return 0;
	peny += font->face->glyph->advance.y >> 6;
	font_h = peny;
	if (font_h == 0) {
		int64_t bbox_ymax =
			FT_MulFix(font->face->bbox.yMax, font->face->size->metrics.y_scale) >> 6;
		int64_t bbox_ymin =
			FT_MulFix(font->face->bbox.yMin, font->face->size->metrics.y_scale) >> 6;
		font_h = bbox_ymax - bbox_ymin;
	}
	return font_h;
#else
	if (!font)
		return -1;
	return font->lineHeight;
#endif
}

int ChFontClamp(int val, int min, int max) {
	if (val < min)
		return min;
	if (val > max)
		return max;
	return val;
}

/*
 * ChFontDrawTextClipped -- draws text using specific font within
 * a clipped boundary
 * @param canv -- Pointer to Canvas
 * @param font -- Pointer to font to use
 * @param string -- string to draw
 * @param penx -- x position
 * @param peny -- y position
 * @param color -- color to use
 * @param limit -- boundary of the rectangle
 */
int ChFontDrawTextClipped(
	ChCanvas* canv, ChFont* font, char* string, int penx, int peny, uint32_t color, ChRect* limit) {
#ifdef _USE_FREETYPE
	if (!font)
		return 1;
	if (!limit)
		return 1;

	/*if ((penx >= canv->canvasWidth) || (peny >= canv->canvasHeight))
		return 1;*/

	if (peny < limit->y)
		return 1;

	if (penx < limit->x)
		return 1;

	FT_Bool use_kerning = FT_HAS_KERNING(font->face);
	uint32_t prev = 0;
	FT_UInt glyfIndx;
	FT_Error err = 0;
	const uint8_t* p8 = (const uint8_t*)string;
	size_t remaining = strlen(string);

	while (*p8) {
		uint32_t codePoint;
		size_t n = ChFontDecodeUTF8(p8, remaining, &codePoint);
		p8 += n;
		remaining -= n;
		glyfIndx = FT_Get_Char_Index(font->face, codePoint);
		err = FT_Load_Glyph(font->face, glyfIndx, FT_LOAD_RENDER);
		if (err)
			continue;

		if (use_kerning && prev && glyfIndx) {
			FT_Vector delta;
			FT_Get_Kerning(font->face, prev, glyfIndx, FT_KERNING_DEFAULT, &delta);
			penx += delta.x >> 6;
		}

		int x_v = penx + font->face->glyph->bitmap_left;
		int y_v = peny - font->face->glyph->bitmap_top;
		int draw_width = font->face->glyph->bitmap.width;
		int draw_height = font->face->glyph->bitmap.rows;

		/* here p = x and q = y*/
		int buff_p_off = 0;
		int buff_q_off = 0;

		/* Clip the text within clip boundary*/
		if (limit->x > x_v) {
			buff_p_off = limit->x - x_v;
			x_v = limit->x;
		}

		if (limit->y > y_v) {
			buff_q_off = limit->y - y_v;
			y_v = limit->y;
		}

		int screen_draw_height = draw_height - buff_q_off;
		if ((y_v + screen_draw_height) > (limit->y + limit->h))
			screen_draw_height = (limit->y + limit->h) - y_v;

		if (screen_draw_height < 0)
			screen_draw_height = 0;

		int screen_draw_width = draw_width - buff_p_off;
		if ((x_v + screen_draw_width) > (limit->x + limit->w))
			screen_draw_width = (limit->x + limit->w) - x_v;

		if (screen_draw_width < 0)
			screen_draw_width = 0;

		for (int i = x_v, p = buff_p_off; i < x_v + screen_draw_width && p < draw_width; i++, p++) {
			for (int j = y_v, q = buff_q_off; j < y_v + screen_draw_height && q < draw_height; j++, q++) {
				if (font->face->glyph->bitmap.buffer[q * font->face->glyph->bitmap.width + p] > 0) {
					double val =
						font->face->glyph->bitmap
							.buffer[static_cast<uint64_t>(q) * font->face->glyph->bitmap.width +
									p] *
						1.0 / 255;
					canv->buffer[static_cast<uint64_t>(i) +
								 static_cast<uint64_t>(j) * canv->canvasWidth] =
						ChColorAlphaBlend(
							canv->buffer[static_cast<uint64_t>(i) +
										 static_cast<uint64_t>(j) * canv->canvasWidth],
							color,
							val);
				} else if (font->face->glyph->bitmap.buffer[q * font->face->glyph->bitmap.width] ==
						   255)
					canv->buffer[i + j * canv->canvasWidth] = color;
			}
		}

		penx += font->face->glyph->advance.x >> 6;
		peny += font->face->glyph->advance.y >> 6;

		prev = glyfIndx;
		string++;
	}
	return 0;
#else
	if (!font)
		return 1;
	if (!limit)
		return 1;

	int prevCp = 0;
	while (*string) {
		unsigned char cp = (unsigned char)*string;
		if (prevCp) {
			int kern = stbtt_GetCodepointKernAdvance(&font->stbFont, prevCp, cp);
			penx += (int)(kern * font->stbScale);
		}
		int advance = 0;
		if (!ChFontBlitBaked(canv, font, cp, penx, peny, color, limit, &advance)) {
			ChFontGlyphCacheEntry* glyph = ChFontGetCachedGlyph(font, cp);
			ChFontBlitGlyph(canv, glyph, penx, peny, color, limit);
			advance = glyph->advance;
		}
		penx += advance;
		prevCp = cp;
		string++;
	}
	return 0;
#endif
}

/*
 * ChFontClose -- closes an opened font
 * @param font -- Pointer to font
 */
int ChFontClose(ChFont* font) {
	if (!font)
		return -1;
	//FT_Done_Face(font->face);
	//FT_Done_FreeType(font->lib);
#ifndef _USE_FREETYPE
	ChFontClearGlyphCache(font);
	ChFontFreeAtlas(font);
#endif
	_KeUnmapSharedMem(font->key);
	free(font);
	return 0;
}
