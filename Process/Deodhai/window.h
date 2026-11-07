/**
* @file window.h
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

#ifndef __WINDOW_H__
#define __WINDOW_H__

#include <stdint.h>
#include <widgets/list.h>
#include "deodhai.h"

#define WINDOW_FLAG_MOVABLE			   (1 << 0)
#define WINDOW_FLAG_STATIC			   (1 << 1)
#define WINDOW_FLAG_ALWAYS_ON_TOP	   (1 << 2)
#define WINDOW_FLAG_NON_RESIZABLE	   (1 << 3)
#define WINDOW_FLAG_BROADCAST_LISTENER (1 << 4)
#define WINDOW_FLAG_ANIMATED		   (1 << 5)
#define WINDOW_FLAG_BLOCKED			   (1 << 6)
#define WINDOW_FLAG_MESSAGEBOX		   (1 << 7)
#define WINDOW_FLAG_DIALOGBOX		   (1 << 8)
#define WINDOW_FLAG_ANIMATION_FADE_IN  (1 << 9)
#define WINDOW_FLAG_ANIMATION_FADE_OUT (1 << 10)
#define WINDOW_FLAG_POPUP			   (1 << 11)

/* Title bar drag zone, in pixels from the window's left edge. The classic
 * chrome keeps its controls in the right 74 px. A Chitralekha chrome
 * style rides in window flag bits 13-14 (WINDOW_STYLE_MASK in
 * widgets/window.h); style 1, the knot, keeps its controls in the left
 * 68 px instead, so presses there must reach the app, not start a drag. */
#define WINDOW_STYLE_OF_FLAGS(f) (((f) >> 13) & 3)
#define WINDOW_STYLE_KNOT_ID	 1
static inline int DeodhaiDragZoneX0(uint16_t flags) {
	return WINDOW_STYLE_OF_FLAGS(flags) == WINDOW_STYLE_KNOT_ID ? 68 : 10;
}
static inline int DeodhaiDragZoneX1(uint16_t flags, int width) {
	return WINDOW_STYLE_OF_FLAGS(flags) == WINDOW_STYLE_KNOT_ID ? width - 8 : width - 74;
}

#ifdef SHADOW_ENABLED
#define SHADOW_SIZE	 11
#define SHADOW_COLOR 0xFF827474
#else
#define SHADOW_SIZE	 0 //11
#define SHADOW_COLOR 0
#endif

//#pragma pack(push,1)
typedef struct _win_info_ {
	Rect rect[256];
	uint32_t rect_count;
	bool dirty;
	bool updateEntireWindow;
	int x;
	int y;
	int width;
	int height;
	bool alpha;
	bool hide;
	double alphaValue;
	bool windowReady;
	bool zoomed;
} WinSharedInfo;
//#pragma pack(pop)

typedef struct _win_ {
	uint16_t flags;
	uint16_t ownerId;
	uint32_t* backBuffer;
	uint32_t* sharedInfo;
	uint32_t* shadowBuffers;
	uint16_t shWinKey;
	uint16_t backBufferKey;
	uint32_t handle;
	int dragX;
	int dragY;
	int resz_h;
	int resz_b;
	bool markForClose;
	uint8_t animFrameCount;
	int animAlphaVal;
	bool animdirection;
	char* title;
	struct _win_* firstPopupWin;
	struct _win_* lastPopupWin;
	struct _win_* parent;
	_win_* next;
	_win_* prev;
} Window;

/**
* @brief CreateSharedWinSpace -- Create a shared window space
* @param shkey -- location where to store the window key
* @param ownerId -- owning process id
*/
extern uint32_t* CreateSharedWinSpace(uint16_t* shkey, uint16_t ownerId);

/**
* @brief CreateNewBackBuffer --Create a back buffer window
* @param ownerId -- owner id
* @param sz -- Size of the buffer
* @param key -- location where to store the buffer key
*/
extern void* CreateNewBackBuffer(uint16_t ownerId, uint32_t sz, uint16_t* key);
/**
* @brief CreateWindow -- create a new window
* @param x -- X position of the window
* @param y -- Y position of the window
* @param w -- Width of the window
* @param h -- Height of the window
* @param flags -- Flags for thr window
* @param ownerId -- process owner id of the window
* @param title -- title of the window
*/
extern Window*
CreateWindow(int x, int y, int w, int h, uint16_t flags, uint16_t ownerId, char* title);
#endif