/**
* @file ftmngr.c
* 
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
#include <Drivers/uart.h>
#include <aucon.h>
#include <Mm/kmalloc.h>
#include <Mm/shm.h>
#include <stdio.h>
#include <Hal/AA64/aa64lowlevel.h>

#define FONTMGR_KEY 0x1234
static FontSeg* firstSeg = NULL;
static FontSeg* lastSeg = NULL;
uint8_t* font_conf_data;
uint16_t fontKey;
int totalSysFonts;

/**
 * @brief FontManagerAddSegment -- add a font segment
 * @param seg -- font segment
 */
void FontManagerAddSegment(FontSeg* seg) {
	seg->next = NULL;
	seg->prev = NULL;
	if (firstSeg == NULL) {
		lastSeg = seg;
		firstSeg = seg;
		dmb_ish();
	} else {
		lastSeg->next = seg;
		dmb_ish();
		seg->prev = lastSeg;
		dmb_ish();
	}
	lastSeg = seg;
}

/**
 * @brief FontManagerRemoveSegment -- remove a font segment
 * @param seg -- Pointer to font segment
 */
void FontManagerRemoveSegment(FontSeg* seg) {
	if (firstSeg == NULL)
		return;

	if (seg == firstSeg) {
		firstSeg = firstSeg->next;
	} else {
		seg->prev->next = seg->next;
	}

	if (seg == lastSeg) {
		lastSeg = seg->prev;
	} else {
		seg->next->prev = seg->prev;
	}
	AuSHMRelease(seg->sharedSeg);
	seg->sharedSeg = NULL;
}

/**
 * @brief FontManagerGetKey -- obtain a key
 * @return key allocated by font manager
 */
uint16_t FontManagerGetKey() {
	uint16_t key = fontKey;
	fontKey = fontKey + 10;
	return key;
}

/**
 * @brief FontManagerAllocateSegment -- allocate a font segment
 * @param fontfile -- Pointer to fontfile
 * @param fontname -- name of the font
 * @return shared memory segment allocated by font manager
 */
FontSeg* FontManagerAllocateSegment(AuVFSNode* fontfile, char* fontname) {
	if (!fontfile)
		return NULL;
	FontSeg* seg = (FontSeg*)kmalloc(sizeof(FontSeg));
	memset(seg, 0, sizeof(FontSeg));
	strncpy(seg->fontname, fontname, 32);
	uint32_t alignedSz = ALIGN_UP(fontfile->size, 4096);
	UARTDebugOut("Creating SHM \r\n");
	int id = AuCreateSHM(NULL, FontManagerGetKey(), alignedSz, 0);
	seg->sharedSeg = AuGetSHMByID(id);
	/* The font registry is an owner even when the last client exits. */
	AuSHMRetain(seg->sharedSeg);
	seg->fontFileSz = fontfile->size;
	FontManagerAddSegment(seg);
	return seg;
}

/**
 * @brief FontManagerReadSegment -- read a font segment
 * @param fs -- file system
 * @param file -- font file
 * @param seg -- font segment
 * @return true if successful, false otherwise
 */
 // need better rasterization algorithm for font rendering, currently it is just a simple bitmap rendering --axiss
static bool FontManagerReadSegment(AuVFSNode* fs, AuVFSNode* file, FontSeg* seg) {
	if (!fs || !file || !seg || !seg->sharedSeg)
		return false;

	uint64_t remaining = file->size;
	for (uint64_t i = 0; i < seg->sharedSeg->num_frames && remaining; ++i) {
		uint64_t chunk = remaining > PAGE_SIZE ? PAGE_SIZE : remaining;
		uint64_t phys = seg->sharedSeg->frames[i];
		if (phys == PMM_INVALID_PHYS)
			return false;
		void* destination = (void*)P2V(phys);
		memset(destination, 0, PAGE_SIZE);
		size_t read = AuVFSNodeRead(fs, file, destination, chunk);
		if (read != chunk)
			return false;
		remaining -= chunk;
	}
	return remaining == 0;
}

/**
 * @brief FontManagerOpenFontFile-- opens a font file
 * from disk
 * @return font file opened by font manager
 */
AuVFSNode* FontManagerOpenFontFile(char* filename) {
	AuVFSNode* font = AuVFSOpen(filename);
	if (!font)
		return NULL;
	return font;
}

/**
 * @brief FontManagerIterateFontList-- iterates all fonts
 * from the font config file
 * @param fontlst -- pointer to font list buffer
 */
void FontManagerIterateFontList(uint8_t* fontlst) {
	char* fbuf = (char*)fontlst;
	int configured_count = totalSysFonts;
	int parsed_count = 0;
	int loaded_count = 0;
search:
	if (parsed_count >= configured_count) {
		totalSysFonts = loaded_count;
		return;
	}
	char* p = strchr(fbuf, '[');
	if (p) {
		p++;
		fbuf++;
	} else {
		return;
	}

	char fontname[33];
	int i = 0;
	for (i = 0; i < 32; i++) {
		if (p[i] == ']') {
			fbuf = p + i;
			fbuf++;
			break;
		}
		fontname[i] = p[i];
		fbuf++;
	}
	fontname[i] = 0;

	p = fbuf;
	char filename[33];
	for (i = 0; i < 32; i++) {
		if (p[i] == '|')
			break;
		filename[i] = p[i];
		fbuf++;
	}
	filename[i] = 0;
	parsed_count++;

#ifdef __XENEVA_BLEED__
	/* Keep the UI and bundled optional applications usable without pinning every
	 * configured typeface in physical memory during boot. */
	if (strcmp(fontname, "Calibri") != 0 &&
		strcmp(fontname, "Forte") != 0 &&
		strcmp(fontname, "Consolas") != 0)
		goto search;
#endif

	UARTDebugOut("Opening font file : %s \r\n", filename);
	AuVFSNode* fs = AuVFSFind("/");
	AuVFSNode* fontfile = AuVFSOpen(filename); //FontManagerOpenFontFile(filename);
	if (fontfile) {
		UARTDebugOut("Font file present \r\n");
		FontSeg* seg = FontManagerAllocateSegment(fontfile, fontname);
		UARTDebugOut("fontfile -> %s sz : %d \r\n", fontfile->filename, fontfile->size);
		if (!FontManagerReadSegment(fs, fontfile, seg)) {
			UARTDebugOut("[ftmngr]: failed to populate %s\r\n", fontfile->filename);
			kfree(fontfile);
			return;
		}
		loaded_count++;
		kfree(fontfile); //avoiding this, because we need more powerful heap memory allocator
	}

	goto search;
}

/**
 * @brief FontManagerGetFontCount -- returns number of font counts
 * @param fontlst -- font list buffer
 * @return the number of total font available in the system
 */
int FontManagerGetFontCount(uint8_t* fontlst) {
	char* fbuf = (char*)fontlst;
	char* p = strchr(fbuf, '(');
	if (p) {
		p++;
	} else {
		return 0;
	}
	char num[3];
	int i = 0;
	for (i = 0; i < 2; i++) {
		if (p[i] == ')')
			break;
		num[i] = p[i];
	}
	num[i] = 0;
	int number = atoi(num);
	return number;
}

/**
 * @brief FontManagerInitialise -- initialise
 * font manager
 */
void FontManagerInitialise() {
	AuTextOut("[aurora]: Loading system fonts \r\n");
	firstSeg = NULL;
	lastSeg = NULL;
	fontKey = FONTMGR_KEY;
	AuVFSNode* fs = AuVFSFind("/");
	AuVFSNode* fontconf = AuVFSOpen("/ftlst.cnf");
	if (!fontconf) {
		AuTextOut("[Aurora]: Font Manager failed to open ftlst.cnf, ftlst.cnf file not found \r\n");
		return;
	}
	int num_pages = fontconf->size / PAGE_SIZE;
	if ((fontconf->size % PAGE_SIZE) != 0)
		num_pages++;

	UARTDebugOut("Font this %d bytes numPage: %d \r\n", fontconf->size, num_pages);
	uint64_t* first_addr = NULL;
	for (int i = 0; i < num_pages; i++) {
		uint64_t* addr = (uint64_t*)P2V((size_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
		if (!first_addr)
			first_addr = addr;
	}

	memset(first_addr, 0, fontconf->size);
	AuVFSNodeRead(fs, fontconf, first_addr, fontconf->size);
	totalSysFonts = 0;
	font_conf_data = (uint8_t*)first_addr;
	totalSysFonts = FontManagerGetFontCount(font_conf_data);
	UARTDebugOut("Font Count ->%d \r\n", totalSysFonts);
	FontManagerIterateFontList(font_conf_data);
}

/**
 * @brief AuFTMngrGetFontID -- returns the font id of 
 * given font name
 * @param fontname -- name of the desired font
 * @return id of the desired font
 */
int AuFTMngrGetFontID(char* fontname) {
	int font_id = 0;
	//UARTDebugOut("Getting font id : %s \n", fontname);
	FontSeg* seg = firstSeg;
	//Bugg ??
	for (seg = firstSeg; seg != NULL; seg = seg->next) {
		if (strcmp(fontname, seg->fontname) == 0) {
			UARTDebugOut("Found font id : %d \n", seg->sharedSeg->id);
			font_id = (seg->sharedSeg->id << 16) | (seg->sharedSeg->key & UINT16_MAX);
			UARTDebugOut("Font ID value : %x \r\n", font_id);
			return font_id;
		}
	}
	return -1;
}

/**
 * @brief AuFTMngrGetFontSize -- returns font size
 * @param fontname -- name of the font
 * @return the size of the font in bytes
 */
int AuFTMngrGetFontSize(char* fontname) {
	for (FontSeg* seg = firstSeg; seg != NULL; seg = seg->next) {
		if (strcmp(fontname, seg->fontname) == 0) {
			return seg->fontFileSz;
		}
	}
	return -1;
}

/**
 * @brief AuFTMngrGetNumFonts -- return number
 * system fonts installed
 * @return total count of system font
 * FontManagerGetFontCount does the same thing except it uses
 * the font config file to get the number of total font count
 */
int AuFTMngrGetNumFonts() {
	return totalSysFonts;
}
