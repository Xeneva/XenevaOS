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

#include <ftmngr.h>
#include <_null.h>
#include <Fs/vfs.h>
#include <Mm/vmmngr.h>
#include <Mm/pmmngr.h>
#include <Hal/serial.h>
#include <aucon.h>
#include <Mm/kmalloc.h>
#include <Mm/shm.h>
#include <stdio.h>

#define FONTMGR_KEY  0x1234
FontSeg* firstSeg;
FontSeg* lastSeg;
uint8_t* font_conf_data;
uint16_t fontKey;
int totalSysFonts;

/*
 * FontManagerAddSegment -- add a font segment
 * @param seg -- font segment
 */
void FontManagerAddSegment(FontSeg* seg) {
	seg->next = NULL;
	seg->prev = NULL;
	if (firstSeg == NULL) {
		lastSeg = seg;
		firstSeg = seg;
	}
	else {
		lastSeg->next = seg;
		seg->prev = lastSeg;
	}
	lastSeg = seg;
}

/*
 * FontManagerRemoveSegment -- remove a font segment
 * @param seg -- Pointer to font segment
 */
void FontManagerRemoveSegment(FontSeg* seg) {
	if (firstSeg == NULL)
		return;

	if (seg == firstSeg) {
		firstSeg = firstSeg->next;
	}
	else {
		seg->prev->next = seg->next;
	}

	if (seg == lastSeg) {
		lastSeg = seg->prev;
	}
	else {
		seg->next->prev = seg->prev;
	}
}

/*
 * FontManagerGetKey -- obtain a key 
 */
uint16_t FontManagerGetKey(){
	uint16_t key = fontKey;
	fontKey = fontKey + 10;
	return key;
}



/*
 * FontManagerAllocateSegment -- allocate a font segment
 * @param fontfile -- Pointer to fontfile
 * @param fontname -- name of the font
 */
FontSeg* FontManagerAllocateSegment(AuVFSNode* fontfile, char* fontname) {
	if (!fontfile)
		return NULL;
	FontSeg* seg = (FontSeg*)kmalloc(sizeof(FontSeg));
	memset(seg, 0, sizeof(FontSeg));
	strcpy(seg->fontname, fontname);
	uint32_t alignedSz = ALIGN_UP(fontfile->size, 4096);
	int id = AuCreateSHM(NULL, FontManagerGetKey(), alignedSz, 0);
	seg->sharedSeg = AuGetSHMByID(id);
	seg->fontFileSz = fontfile->size;
	FontManagerAddSegment(seg);
	return seg;
}

/*
 * FontManagerOpenFontFile-- opens a font file
 * from disk
 */
AuVFSNode* FontManagerOpenFontFile(char* filename) {
	AuVFSNode* font = AuVFSOpen(filename);
	if (!font)
		return NULL;
	return font;
}

/*
 * FontManagerIterateFontList-- iterates all fonts
 * from the font config file
 * @param fontlst -- pointer to font list buffer
 */
void FontManagerIterateFontList(uint8_t* fontlst) {
	char* fbuf = (char*)fontlst;
search:
	char* p = strchr(fbuf, '[');
	if (p){
		p++;
		fbuf++;
	}
	else{
		return;
	}

	char fontname[32];
	int i = 0;
	for (i = 0; i < 32; i++) {
		if (p[i] == ']'){
			fbuf = p + i;
			fbuf++;
			break;
		}
		fontname[i] = p[i];
		fbuf++;
	}
	fontname[i] = 0;

	p = fbuf;
	char filename[32];
	for (i = 0; i < 32; i++) {
		if (p[i] == '|')
			break;
		filename[i] = p[i];
		fbuf++;
	}
	filename[i] = 0;

	AuVFSNode* fs = AuVFSFind("/");
	AuVFSNode* fontfile = FontManagerOpenFontFile(filename);
	if (fontfile) {
		FontSeg* seg = FontManagerAllocateSegment(fontfile, fontname);
		uint64_t* firstFrame = (uint64_t*)seg->sharedSeg->frames[0];
		size_t ret = AuVFSNodeRead(fs, fontfile, (uint64_t*)P2V((size_t)firstFrame), ALIGN_UP(fontfile->size, 4096));
		kfree(fontfile);
	}
	goto search;
	
}

/*
 * FontManagerGetFontCount -- returns number of font counts
 * @param fontlst -- font list buffer
 */
int FontManagerGetFontCount(uint8_t* fontlst) {
	char* fbuf = (char*)fontlst;
	char* p = strchr(fbuf,'(');
	if (p){
		p++;
	}
	else {
		return 0;
	}
	char num[3];
	int i = 0;
	for (i = 0; i < 3; i++) {
		if (p[i] == ')')
			break;
		num[i] = p[i];
	}
	num[i] = 0;
	int number = atoi(num);
	return number;
}

/*
 * FontManagerInitialise -- initialise
 * font manager
 */
void FontManagerInitialise() {
	AuTextOut("[aurora]: Loading system fonts \n");
	firstSeg = NULL;
	lastSeg = NULL;
	fontKey = FONTMGR_KEY;
	AuVFSNode* fs = AuVFSFind("/");
	AuVFSNode* fontconf = AuVFSOpen("/ftlst.cnf");
	if (!fontconf) {
		AuTextOut("[Aurora]: Font Manager failed to open ftlst.cnf, ftlst.cnf file not found \n");
		for (;;);
		return;
	}
	int num_pages = fontconf->size / PAGE_SIZE;
	if ((num_pages % PAGE_SIZE) != 0)
		num_pages++;

	SeTextOut("Font this \r\n");
	uint64_t* first_addr = NULL;
	for (int i = 0; i < num_pages; i++) {
		uint64_t* addr = (uint64_t*)P2V((size_t)AuPmmngrAlloc());
		if (!first_addr)
			first_addr = addr;
	}
	
	memset(first_addr, 0, fontconf->size);
	AuVFSNodeRead(fs, fontconf, first_addr, fontconf->size);
	totalSysFonts = 0;
	font_conf_data = (uint8_t*)first_addr;
	totalSysFonts = FontManagerGetFontCount(font_conf_data);
	SeTextOut("Font Count ->%d \r\n", totalSysFonts);
	FontManagerIterateFontList(font_conf_data);
}


/*
 * some system call
 */
int AuFTMngrGetFontID(char* fontname) {
	int font_id = 0;
	for (FontSeg* seg = firstSeg; seg != NULL; seg = seg->next) {
		if (strcmp(fontname, seg->fontname) == 0) {
			font_id =  (seg->sharedSeg->id << 16) | seg->sharedSeg->key & UINT16_MAX;
			return font_id;
		}
	}
	return -1;
}

/*
 * AuFTMngrGetFontSize -- returns font size
 * @param fontname -- name of the font
 */
int AuFTMngrGetFontSize(char* fontname) {
	for (FontSeg* seg = firstSeg; seg != NULL; seg = seg->next) {
		if (strcmp(fontname, seg->fontname) == 0) {
			return seg->fontFileSz;
		}
	}
	return -1;
}

/*
 * AuFTMngrGetNumFonts -- return number
 * system fonts installed
 */
int AuFTMngrGetNumFonts() {
	return totalSysFonts;
}
