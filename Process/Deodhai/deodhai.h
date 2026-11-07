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

#ifndef __DEODHAI_H__
#define __DEODHAI_H__

#include <stdint.h>
#include <chitralekha.h>

#define DEODHAI_MESSAGE_CREATEWIN		   50
#define DEODHAI_MESSAGE_WINDESTROY		   51
#define DEODHAI_MESSAGE_BROADCAST_ICON	   52
#define DEODHAI_MESSAGE_WINDOW_BRING_FRONT 53
#define DEODHAI_MESSAGE_WINDOW_HIDE		   54
#define DEODHAI_MESSAGE_GETWINDOW		   55
#define DEODHAI_MESSAGE_CLOSE_WINDOW	   56
#define DEODHAI_MESSAGE_SET_FLAGS		   57
#define DEODHAI_MESSAGE_CREATE_POPUP	   58
#define DEODHAI_MESSAGE_MOUSE_DBLCLK	   59
#define DEODHAI_MESSAGE_MOVE_WINDOW		   60

#define DEODHAI_REPLY_WINCREATED	150
#define DEODHAI_REPLY_MOUSE_EVENT	151
#define DEODHAI_REPLY_KEY_EVENT		152
#define DEODHAI_REPLY_WINDOW_ID		153
#define DEODHAI_REPLY_FOCUS_CHANGED 154
#define DEODHAI_REPLY_WINDOW_CLOSED 156
#define DEODHAI_REPLY_MOUSE_LEAVE	157
#define DEODHAI_REPLY_TOUCH_EVENT	158

#define DEODHAI_BROADCAST_WINCREATED	170
#define DEODHAI_BROADCAST_WINDESTROYED	171
#define DEODHAI_BROADCAST_ICON			174
#define DEODHAI_BROADCAST_FOCUS_CHANGED 173

#define HANDLE_TYPE_NORMAL_WINDOW 1
#define HANDLE_TYPE_POPUP_WINDOW  2

#pragma pack(push, 1)
typedef struct _rect_ {
	int32_t x;
	int32_t y;
	int32_t w;
	int32_t h;
} Rect;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _bmp_ {
	unsigned short type;
	unsigned int size;
	unsigned short resv1;
	unsigned short resv2;
	unsigned int off_bits;
} BMP;

typedef struct _info_ {
	unsigned int biSize;
	long biWidth;
	long biHeight;
	unsigned short biPlanes;
	unsigned short biBitCount;
	unsigned int biCompression;
	unsigned int biSizeImage;
	long biXPelsPerMeter;
	long biYPelsPerMeter;
	unsigned int biClrUsed;
	unsigned int biClrImportant;
} BMPInfo;
#pragma pack(pop)

#define AU_INPUT_MOUSE	  1
#define AU_INPUT_KEYBOARD 2
#define AU_INPUT_TOUCH	  3

#ifdef ARCH_X64
#pragma pack(push, 1)
#endif
/* Copied from kernel*/
typedef struct _au_input_msg_ {
	uint8_t type;
	int32_t xpos;
	int32_t ypos;
	uint8_t button_state;
	uint32_t code;
	uint32_t code1;
	uint32_t code3;
	uint32_t code4;
} AuInputMessage;

#ifdef ARCH_X64
#pragma pack(pop)
#endif

/*
* DeodhaiAllocateNewHandle -- get a new window handle
*/
extern uint32_t DeodhaiAllocateNewHandle();
extern ChCanvas* DeodhaiGetMainCanvas();
/*
 * DeodhaiUpdateBits -- update specific deodhai bits
 */
extern void DeodhaiUpdateBits(bool window_update, bool skip);
#endif