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

#ifndef __FTMNGR_H__
#define __FTMNGR_H__

#include <stdint.h>
#include <string.h>
#include <Mm/shm.h>
/*
 * ftmngr -- font manager for aurora kernel, loads 
 * true type fonts from font.conf file
 */

//#pragma pack(push,1)
typedef struct _font_seg_ {
	char fontname[32];
	AuSHM* sharedSeg;
	uint32_t fontFileSz;
	struct _font_seg_ *next;
	struct _font_seg_ *prev;
}FontSeg;
//#pragma pack(pop)

/*
* FontManagerInitialise -- initialise
* font manager
*/
extern void FontManagerInitialise();

/*
* some system call
*/
extern int AuFTMngrGetFontID(char* fontname);

/*
* AuFTMngrGetNumFonts -- return number
* system fonts installed
*/
extern int AuFTMngrGetNumFonts();

/*
* AuFTMngrGetFontSize -- returns font size
* @param fontname -- name of the font
*/
extern int AuFTMngrGetFontSize(char* fontname);

#endif