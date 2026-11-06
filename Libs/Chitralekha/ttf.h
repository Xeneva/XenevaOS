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

#ifndef __TTF_H__
#define __TTF_H__

#include <stdint.h>
#include <_xeneva.h>
#include "font.h"
#include "chitralekha.h"

/* ttf table tags */
#define TTF_TABLE_CMAP 'cmap' //character to glyph mapping
#define TTF_TABLE_GLYF 'glyf' //glyph data
#define TTF_TABLE_HEAD 'head' //font header
#define TTF_TABLE_HHEA 'hhea' //horizontal header
#define TTF_TABLE_HMTX 'hmtx' //horizontal metrics
#define TTF_TABLE_LOCA 'loca' //index to location
#define TTF_TABLE_MAXP 'maxp' //maximum profile
#define TTF_TABLE_NAME 'name' //naming
#define TTF_TABLE_POST 'post' //PostScript

/* true type --data type definitions*/
typedef int16_t shortFrac;
typedef int32_t Fixed;
typedef int16_t FWord;
typedef uint16_t uFWord;
typedef int16_t F2Dot14;
typedef uint64_t longDateTime;

#pragma pack(push, 1)
typedef struct _ttftable_ {
	unsigned char* base;
	unsigned int len;
} TTFTable;
#pragma pack(pop)

typedef struct _ttf_ {
	unsigned char* base;
	TTFTable cmap;
	TTFTable glyf;
	TTFTable head;
	TTFTable hhea;
	TTFTable hmtx;
	TTFTable loca;
	TTFTable maxp;
	TTFTable name;
	TTFTable post;
	uint16_t unitsPerEm;
	unsigned char* cmapStart;
	uint16_t loca_type;
	unsigned char* memptr;
	float scale;
	ChCanvas* canv;
} TTFont;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _off_sub_ {
	uint32_t scalerType;
	uint16_t numTables;
	uint16_t searchRange;
	uint16_t entrySelector;
	uint16_t rangeShift;
} TTFOffsetSubtable;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _table_dir_ {
	uint32_t tag;
	uint32_t checksum;
	uint32_t offset;
	uint32_t length;
} TTFTableDirectory;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _head_tbl_ {
	Fixed version;
	Fixed fontRevision;
	uint32_t checkSumAdjustment;
	uint32_t magicNumber;
	uint16_t flags;
	uint16_t unitsPerEm;
	longDateTime created;
	longDateTime modified;
	FWord xMin;
	FWord yMin;
	FWord xMax;
	FWord yMax;
	uint16_t macStyle;
	uint16_t lowestRecPPEM;
	int16_t fontDirectionHint;
	int16_t indexToLocFormat;
	int16_t glyphDataFormat;
} TTFHead;
#pragma pack(pop)

#pragma pack(push, 1)
/* CMAP Tables */
typedef struct _ttf_cmap_ {
	uint16_t version;
	uint16_t numberSubtable;
} TTFCmap;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _ttf_cmap_sub_ {
	uint16_t platformID;
	uint16_t platformSpecificID;
	uint32_t offset;
} TTFCmapSubtable;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _cmap_format_ {
	uint16_t format;
	uint16_t len;
	uint16_t language;
} TTFCmapFormat;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _cmap_format12_ {
	uint16_t format;
	uint16_t reserved;
	uint32_t len;
	uint32_t language;
	uint32_t nGroups;
} TTFCmapFormat12;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _cmap_format12_grp_ {
	uint32_t startCharCode;
	uint32_t endCharCode;
	uint32_t startGlyphCode;
} TTFCmapFormat12Group;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _cmap_format4_ {
	uint16_t format;
	uint16_t len;
	uint16_t language;
	uint16_t segCountX2;
	uint16_t searchRange;
	uint16_t entrySelector;
	//uint16_t rangeShift;
} TTFCmapFormat4;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _maxp_ {
	Fixed version;
	uint16_t numGlyphs;
	uint16_t maxPoints;
	uint16_t maxContours;
	uint16_t maxComponentPoints;
	uint16_t maxComponentContours;
	uint16_t maxZones;
	uint16_t maxTwilightPoints;
	uint16_t maxStorage;
	uint16_t maxFunctionDefs;
	uint16_t maxInstructionDefs;
	uint16_t maxStackElements;
	uint16_t maxSizeOfInstructions;
	uint16_t maxComponentElements;
	uint16_t maxComponentDepth;
} TTFMaxp;

typedef struct _glyph_desc_ {
	int16_t numContours;
	FWord xMin;
	FWord yMin;
	FWord xMax;
	FWord yMax;
} TTFGlyphDesc;

#pragma pack(pop)

/*
* TTFLoadFont -- load and start decoding ttf font
* @param buffer -- pointer to font file buffer
*/
TTFont* TTFLoadFont(ChCanvas* canv, unsigned char* buffer);

/*
* TTFSetFontSize -- scale font for output on display device
* @param font -- Pointer to font object
* @param pointSz -- size of the point
*/
void TTFSetFontSize(TTFont* font, float pointSz);
#endif