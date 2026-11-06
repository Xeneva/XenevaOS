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

#ifndef __CURSOR_H__
#define __CURSOR_H__

#include <stdint.h>
#include <chitralekha.h>

#define CURSOR_TYPE_POINTER			 1
#define CURSOR_TYPE_LOADING			 2
#define CURSOR_TYPE_TEXT			 3
#define CURSOR_TYPE_MOVE			 4
#define CURSOR_TYPE_RESIZE_UPDOWN	 5
#define CURSOR_TYPE_RESIZE_RIGHTLEFT 6

typedef struct _cursor_ {
	uint8_t type;
	uint8_t* imageData;
	int width;
	int height;
	int bpp;
	int cursorFD;
	int xpos;
	int ypos;
	int oldXPos;
	int oldYPos;
	uint8_t* fileBuffer;
	uint32_t* cursorBack;
	size_t cursorFileSize;
} Cursor;

/*
* CursorOpen -- open a cursor from file
* @param path -- path of the cursor
* @param type -- cursor type
*/
extern Cursor* CursorOpen(char* path, uint8_t type);

/* CursorRead -- read the cursor file
* @param cur -- Pointer to cursor file
*/
extern void CursorRead(Cursor* cur);

/*
* CursorDraw -- draw the cursor to canvas
* @param canv -- Pointer to canvas
* @param cur - Pointer to cursor
* @param x -- X position
* @param y -- Y position
*/
extern void CursorDraw(ChCanvas* canv, Cursor* cur, unsigned int x, unsigned int y);

#endif