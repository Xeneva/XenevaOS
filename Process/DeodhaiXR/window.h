
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
#include "deodxr.h"

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
#define WINDOW_FLAG_GLASS			   (1 << 12)

#ifdef SHADOW_ENABLED
#define SHADOW_SIZE		20
#define SHADOW_BLUR		8
#define SHADOW_ALPHA	180
#define SHADOW_OFFSET_X 4
#define SHADOW_OFFSET_Y 6
#else
#define SHADOW_SIZE		0
#define SHADOW_BLUR		0
#define SHADOW_ALPHA	0
#define SHADOW_OFFSET_X 0
#define SHADOW_OFFSET_Y 0
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

static inline bool WinSharedFlagLoad(const bool* flag) {
	return __atomic_load_n(flag, __ATOMIC_ACQUIRE);
}

static inline void WinSharedFlagStore(bool* flag, bool value) {
	__atomic_store_n(flag, value, __ATOMIC_RELEASE);
}
//#pragma pack(pop)

typedef struct _win_ {
	uint16_t flags;
	uint16_t ownerId;
	uint32_t* backBuffer;
	uint32_t* sharedInfo;
	uint32_t* shadowBuffers;
	uint32_t* shadowTmp;
	uint32_t* glassTmp;
	uint32_t* glassBlur;
	bool glassBlurValid;
	int glassBlurX;
	int glassBlurY;
	int glassBlurW;
	int glassBlurH;
	uint16_t shWinKey;
	uint16_t backBufferKey;
	uint32_t handle;
	int dragX;
	int dragY;
	int resz_h;
	int resz_b;
	int originalW;
	int originalH;
	bool resizePending;
	uint64_t resizeDeadline;
	int resizeX;
	int resizeY;
	int resizeW;
	int resizeH;
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

#define WINDOW_TITLEBAR_HEIGHT 26
#define WINDOW_GRAB_LEFT	   10
#define WINDOW_GRAB_RIGHT	   74
#define WINDOW_GRAB_KEEP	   32
#define WINDOW_TITLEBAR_KEEP   8
#define WINDOW_CURSOR_SIZE	   24
#define WINDOW_DOCK_RESERVE	   70

static inline void WindowClampToScreen(int* x, int* y, int w, int h, int screen_w, int screen_h) {
	(void)h;
	int min_x = WINDOW_GRAB_RIGHT + WINDOW_GRAB_KEEP - w;
	if (min_x > 0)
		min_x = 0;
	int max_x = screen_w - WINDOW_CURSOR_SIZE - WINDOW_GRAB_LEFT - WINDOW_GRAB_KEEP;
	int min_y = WINDOW_TITLEBAR_KEEP - WINDOW_TITLEBAR_HEIGHT;
	int max_y = screen_h - WINDOW_DOCK_RESERVE - WINDOW_TITLEBAR_KEEP;

	if (max_x < min_x)
		max_x = min_x;
	if (max_y < min_y)
		max_y = min_y;
	if (*x < min_x)
		*x = min_x;
	else if (*x > max_x)
		*x = max_x;
	if (*y < min_y)
		*y = min_y;
	else if (*y > max_y)
		*y = max_y;
}

extern bool ResizeWindowBackBuffer(Window* win, int width, int height);
extern void ReleaseWindowEffects(Window* win);
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
