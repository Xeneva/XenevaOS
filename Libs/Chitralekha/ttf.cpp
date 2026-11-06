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

#include <sys/utf.h>
#include "ttf.h"

/* simple glyf flags definitions*/
#define GLYF_FLAG_ONCURVE			   (1 << 0)
#define GLYF_FLAG_XSHORT_VECT		   (1 << 1)
#define GLYF_FLAG_YSHORT_VECT		   (1 << 2)
#define GLYF_FLAG_REPEAT			   (1 << 3)
#define GLYF_FLAG_POSITIVE_XSHORT_VECT (1 << 4)
#define GLYF_FLAG_POSITIVE_YSHORT_VECT (1 << 5)

/* compound glyf flags definitions */
#define ARG_1_AND_2_ARE_WORDS	 (1 << 0)
#define ARGS_ARE_XY_VALUES		 (1 << 1)
#define ROUND_XY_TO_GRID		 (1 << 2)
#define WE_HAVE_A_SCALE			 (1 << 3)
#define MORE_COMPONENTS			 (1 << 5)
#define WE_HAVE_AN_X_AND_Y_SCALE (1 << 6)
#define WE_HAVE_A_TWO_BY_TWO	 (1 << 7)
#define WE_HAVE_INSTRUCTIONS	 (1 << 8)
#define USE_MY_METRICS			 (1 << 9)
#define OVERLAP_COMPUND			 (1 << 10)

typedef struct _ttf_vertices_ {
	uint8_t flags;
	int x;
	int y;
} TTFVertices;
/*
 * TTFSwap -- swap endianness
 * @param value -- value to swap
 */
int TTFSwap32(int value) {
	int leftmostbyte, leftmiddlebyte, rightmiddlebyte, rightmostbyte;
	int result;

	leftmostbyte = (value & 0x000000FF) >> 0;
	leftmiddlebyte = (value & 0x0000FF00) >> 8;
	rightmiddlebyte = (value & 0x00ff0000) >> 16;
	rightmostbyte = (value & 0xff000000) >> 24;

	result = ((leftmostbyte & 0xff) << 24 | (leftmiddlebyte & 0xff) << 16 |
			  (rightmiddlebyte & 0xff) << 8 | rightmostbyte & 0xff);
	return result;
}

/*
 * TTFSwap16 -- swaps a 16 bit value
 * @param value -- value to swap
 */
uint16_t TTFSwap16(uint16_t value) {
	int left_most_byte = (value & 0x000000FF);
	int left_middle_byte = (value & 0x0000ff00) >> 8;
	int result = ((left_most_byte & 0xff) << 8 | left_middle_byte & 0xff);
	return result;
}

uint8_t TTF_READ_8(TTFont* f) {
	return *(f->memptr++);
}

uint16_t TTF_READ_16(TTFont* f) {
	int a = TTF_READ_8(f);
	int b = TTF_READ_8(f);
	if (a < 0 || b < 0)
		return 0;
	return (((a & 0xff) << 8) | (b & 0xff));
}

/* 
 * UTF8toUnicode -- converts utf-8 character to
 * unicode codepoint
 * @param c -- one byte character
 */
uint32_t UTF8toUnicode(uint8_t c) {
	uint32_t mask;
	if (c > 0x7f) {
		mask = (c <= 0x00EFBFBF) ? 0x000F0000 : 0x003F0000;
		c = ((c & 0x07000000) >> 6) | ((c & mask) >> 4) | ((c & 0x00003F00) >> 2) |
			(c & 0x0000003F);
	}
	return c;
}

/*
 * TTFSetFontSize -- scale font for output on display device
 * @param font -- Pointer to font object
 * @param pointSz -- size of the point
 */
void TTFSetFontSize(TTFont* font, float pointSz) {
	pointSz *= 4.0 / 3.0;
	font->scale =
		pointSz /
		font->unitsPerEm; //(pointSz * ChGetScreenDPI(font->canv) * 0.3937 / 72 * font->unitsPerEm);
}

/*
 * TTFGetGlyph -- returns the glyph index for the given
 * codepoint
 * @param font -- Pointer to true type font
 * @param codepoint -- Unicode code point
 */
uint32_t TTFGetGlyph(TTFont* font, uint32_t codepoint) {
	uint32_t glyph;
	TTFCmapFormat* format = (TTFCmapFormat*)font->cmapStart;
	uint16_t format_ = TTFSwap16(format->format);

	/* handle cmap format 4*/
	if (format_ == 4) {
		if (codepoint > 0xFFFF)
			return 0;
		TTFCmapFormat4* format4 = (TTFCmapFormat4*)font->cmapStart;
		uint16_t numSeg = TTFSwap16(format4->segCountX2) / 2;
		uint16_t segx2 = numSeg * 2;
		uint16_t* endcode_ = (uint16_t*)(font->cmapStart + sizeof(TTFCmapFormat4));
		uint16_t* startCode_ = (uint16_t*)(font->cmapStart + sizeof(TTFCmapFormat4) + segx2 + 2);
		uint16_t* reservedPad = (uint16_t*)(font->cmapStart + sizeof(TTFCmapFormat4) + segx2);
		uint16_t* idDelta_ =
			(uint16_t*)(font->cmapStart + sizeof(TTFCmapFormat4) + segx2 + 2 + segx2);
		uint16_t* idRangeOffset_ =
			(uint16_t*)(font->cmapStart + sizeof(TTFCmapFormat4) + segx2 + 2 + segx2 * 2);
		uint16_t* glyphIndexArray =
			(uint16_t*)(font->cmapStart + sizeof(TTFCmapFormat4) + segx2 + 2 + (segx2) * 3);

		for (int i = 0; i < numSeg; i++) {
			uint16_t endcode = TTFSwap16(endcode_[i]);
			if (endcode >= codepoint) {
				uint16_t startcode = TTFSwap16(startCode_[i]);
				if (startcode > codepoint) {
					_KePrint("start code is greater than code point %d \n", codepoint);
					return 0;
				}
				int16_t idDelta = TTFSwap16(idDelta_[i]);
				uint16_t idRangeOffset = TTFSwap16(idRangeOffset_[i]);
				if (idRangeOffset == 0)
					return idDelta + codepoint;
				else {
					_KePrint("Index -> %d \n", idRangeOffset + (codepoint - startcode) * 2);
					return glyphIndexArray[idRangeOffset + (codepoint - startcode) * 2];
				}
			}
		}

	} else if (format_ == 12) { /* handle cmap format 12 */
		TTFCmapFormat12* format12 = (TTFCmapFormat12*)font->cmapStart;
		uint32_t ngrps = TTFSwap32(format12->nGroups);
		TTFCmapFormat12Group* grp =
			(TTFCmapFormat12Group*)(font->cmapStart + sizeof(TTFCmapFormat12));
		for (int i = 0; i < ngrps; i++) {
			uint32_t startChar = grp[i].startCharCode;
			uint32_t endChar = grp[i].endCharCode;
			uint32_t glyph = grp[i].startGlyphCode;

			if (codepoint >= startChar && codepoint <= endChar) {
				return glyph + (codepoint - startChar);
			}
		}
	}

	/* more formats to go */
	return glyph;
}

/*
 * TTFDrawSimpleGlyf -- parse the simple glyf table and get all points of the glyf
 * @param font -- Pointer to true type font
 * @param glyphOffset -- offset of the glyph from glyph base address
 */
void TTFDrawSimpleGlyf(TTFont* font, uint32_t glyphOffset, float xoff, float yoff) {
	TTFGlyphDesc* glyfdesc = (TTFGlyphDesc*)(font->glyf.base + glyphOffset);
	int16_t numContours = TTFSwap16(glyfdesc->numContours);
	uint16_t* endPointsContours = (uint16_t*)(font->glyf.base + glyphOffset + sizeof(TTFGlyphDesc));
	uint16_t* intLen =
		(uint16_t*)(font->glyf.base + glyphOffset + sizeof(TTFGlyphDesc) + numContours * 2);
	uint16_t instructionLen = TTFSwap16(*intLen);
	uint8_t* instructions =
		(uint8_t*)(font->glyf.base + glyphOffset + sizeof(TTFGlyphDesc) + numContours * 2 + 2);
	uint8_t* flags = (uint8_t*)(font->glyf.base + glyphOffset + sizeof(TTFGlyphDesc) +
								numContours * 2 + 2 + instructionLen);

	uint16_t endPoint = TTFSwap16(endPointsContours[numContours - 1]);

	uint8_t* buffer_tag = flags; // we will increament this
	font->memptr = buffer_tag;

	int last_x = 0;
	int last_y = 0;

	/* read all flags */
	TTFVertices* vert = (TTFVertices*)malloc(sizeof(TTFVertices) * endPoint + 1);
	memset(vert, 0, sizeof(TTFVertices) * endPoint + 1);
	for (int i = 0; i < endPoint + 1;) {
		uint8_t flag = TTF_READ_8(font);
		vert[i].flags = flag;
		if (flag & GLYF_FLAG_REPEAT) {
			uint8_t repeatval = TTF_READ_8(font);
			while (repeatval) {
				vert[i].flags = flag;
				repeatval--;
				i++;
			}
		}
	}

	/* read all X-Coords */
	for (int i = 0; i < endPoint + 1; i++) {
		uint8_t flag = vert[i].flags;
		if (flag & GLYF_FLAG_ONCURVE) {
			if (flag & GLYF_FLAG_POSITIVE_XSHORT_VECT)
				vert[i].x = last_x + TTF_READ_8(font);
			else {
				vert[i].x = last_x - TTF_READ_8(font);
			}
		} else {
			if (flag & GLYF_FLAG_POSITIVE_XSHORT_VECT)
				vert[i].x = last_x;
			else {
				int16_t diff = TTF_READ_16(font);
				vert[i].x = last_x + diff;
			}
		}
		last_x = vert[i].x;
	}

	/* Y coord */
	for (int i = 0; i < endPoint + 1; i++) {
		uint8_t flag = vert[i].flags;
		if (flag & GLYF_FLAG_ONCURVE) {
			if (flag & GLYF_FLAG_POSITIVE_YSHORT_VECT)
				vert[i].y = last_y + TTF_READ_8(font);
			else
				vert[i].y = last_y - TTF_READ_8(font);
		} else {
			if (flag & GLYF_FLAG_POSITIVE_YSHORT_VECT)
				vert[i].y = last_y;
			else {
				int16_t diff = TTF_READ_16(font);
				vert[i].y = last_y + diff;
			}
		}
		last_y = vert[i].y;
	}

	font->memptr = (font->glyf.base + glyphOffset + 10);
	//int next_end = TTFSwap16(endPointsContours[0]);
	//int next_end1 = TTFSwap16(endPointsContours[1]);
	//_KePrint("Next end -> %d %d\n", next_end, next_end1);

	//for (int i = 0; i < endPoint + 1; i++) {
	//	float x = ((float)vert[i].x * font->scale + xoff);
	//	float y = (-(float)vert[i].y * font->scale + yoff);
	//	_KePrint("x -> %f, y -> %f \n", x, y);
	//	/*ChDrawPixel(font->canv, x, y, WHITE);*/
	//}
}

void TTFDrawGlyph(TTFont* font, uint32_t glyph, int xOff, int yOff) {
	uint32_t glyph_offset = 0;
	if (font->loca_type == 0) {
		uint16_t* loca = (uint16_t*)font->loca.base;
		glyph_offset = TTFSwap16(loca[glyph]) * 2;
	} else {
		uint32_t* longTable = (uint32_t*)font->loca.base;
		glyph_offset = longTable[2];
	}

	TTFGlyphDesc* glyfdesc = (TTFGlyphDesc*)(font->glyf.base + glyph_offset);

	int16_t numContours = TTFSwap16(glyfdesc->numContours);
	if (numContours >= 0)
		TTFDrawSimpleGlyf(font, glyph_offset, xOff, yOff);
	//else
	//draw compund glyf
}
/*
 * TTFLoadFont -- load and start decoding ttf font
 * @param buffer -- pointer to font file buffer
 */
TTFont* TTFLoadFont(ChCanvas* canv, unsigned char* buffer) {
	TTFOffsetSubtable* offtable = (TTFOffsetSubtable*)buffer;
	TTFTableDirectory* tabledir = (TTFTableDirectory*)(buffer + sizeof(TTFOffsetSubtable));
	uint16_t numTable = TTFSwap16(offtable->numTables);
	TTFont* font = (TTFont*)malloc(sizeof(TTFont));
	memset(font, 0, sizeof(TTFont));
	font->canv = canv;
	font->base = buffer;
	for (int i = 0; i < numTable; ++i) {
		uint32_t tag = TTFSwap32(tabledir[i].tag);
		uint32_t offset = TTFSwap32(tabledir[i].offset);
		uint32_t len = TTFSwap32(tabledir[i].length);
		switch (tag) {
		case TTF_TABLE_CMAP:
			font->cmap.base = buffer + offset;
			font->cmap.len = len;
			break;
		case TTF_TABLE_GLYF:
			font->glyf.base = buffer + offset;
			font->glyf.len = len;
			break;
		case TTF_TABLE_HEAD:
			font->head.base = buffer + offset;
			font->head.len = len;
			break;
		case TTF_TABLE_HHEA:
			font->hhea.base = buffer + offset;
			font->hhea.len = len;
			break;
		case TTF_TABLE_HMTX:
			font->hmtx.base = buffer + offset;
			font->hmtx.len = len;
			break;
		case TTF_TABLE_LOCA:
			font->loca.base = buffer + offset;
			font->loca.len = len;
			break;
		case TTF_TABLE_MAXP:
			font->maxp.base = buffer + offset;
			font->maxp.len = len;
			break;
		case TTF_TABLE_NAME:
			font->name.base = buffer + offset;
			font->name.len = len;
			break;
		case TTF_TABLE_POST:
			font->post.base = buffer + offset;
			font->post.len = len;
			break;
		default:
			break;
		}
	}

	TTFHead* head = (TTFHead*)font->head.base;
	font->unitsPerEm = TTFSwap16(head->unitsPerEm);
	TTFCmap* cmap = (TTFCmap*)font->cmap.base;

	TTFCmapSubtable* sub = (TTFCmapSubtable*)(font->cmap.base + sizeof(TTFCmap));

	uint32_t best = 0;
	int best_score = 0;
	for (int i = 0; i < TTFSwap16(cmap->numberSubtable); i++) {
		uint16_t platformID = TTFSwap16(sub[i].platformID);
		uint16_t platformSpecificID = TTFSwap16(sub[i].platformSpecificID);
		uint32_t offset = TTFSwap32(sub[i].offset);

		if ((platformID == 3 || platformID == 0) && platformSpecificID == 10) {
			best = offset;
			best_score = 4;
		} else if (platformID == 0 && platformSpecificID == 4) {
			best = offset;
			best_score = 4;
		} else if (((platformID == 0 && platformSpecificID == 3) ||
					(platformID == 3 && platformSpecificID == 1)) &&
				   best_score < 2) {
			best = offset;
			best_score = 2;
		}
	}

	TTFCmapFormat* cmap_format = (TTFCmapFormat*)(font->cmap.base + best);
	font->cmapStart = (unsigned char*)cmap_format;
	int16_t* locatype = (int16_t*)(font->head.base + 50);
	font->loca_type = TTFSwap16(head->indexToLocFormat);
	uint32_t glyph = TTFGetGlyph(font, 'I');
	TTFMaxp* maxp = (TTFMaxp*)font->maxp.base;
	TTFSetFontSize(font, 10);
	TTFDrawGlyph(font, glyph, 100, 100);
	return font;
}