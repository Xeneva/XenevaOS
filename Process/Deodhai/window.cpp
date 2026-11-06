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

#include "window.h"
#include "deodhai.h"
#include <sys/mman.h>
#include <stdlib.h>
#include <color.h>
#include <string.h>
#include "boxblur.h"

uint16_t shared_win_key_prefix = 1000;
uint16_t back_buffer_key_prefix = 400;

/*
 * CreateSharedWinSpace -- Create a shared window space
 * @param shkey -- location where to store the window key
 * @param ownerId -- owning process id
 */
uint32_t* CreateSharedWinSpace(uint16_t* shkey, uint16_t ownerId) {
	uint32_t key = shared_win_key_prefix + ownerId;
	int id = _KeCreateSharedMem(key, sizeof(WinSharedInfo), 0);
	void* addr = _KeObtainSharedMem(id, NULL, 0);
	*shkey = key;
	shared_win_key_prefix += 10;
	return (uint32_t*)addr;
}

/*
 * CreateNewBackBuffer --Create a back buffer window
 * @param ownerId -- owner id
 * @param sz -- Size of the buffer
 * @param key -- location where to store the buffer key
 */
void* CreateNewBackBuffer(uint16_t ownerId, uint32_t sz, uint16_t* key) {
	uint32_t key_prefix = back_buffer_key_prefix + ownerId;
	int id = _KeCreateSharedMem(key_prefix, sz, 0);
	void* ptr = _KeObtainSharedMem(id, 0, NULL);
	*key = key_prefix;
	back_buffer_key_prefix += 10;
	return ptr;
}

void WindowShadowPutPixel(Window* win, int x, int y, int shadow_w, uint32_t color) {
	unsigned int* lfb = win->shadowBuffers;
	lfb[y * shadow_w + x] = color;
}

void WindowShadowFillColor(Window* win, int shadow_w, int x, int y, int w, int h, uint32_t col) {
	for (int i = 0; i < w; i++)
		for (int j = 0; j < h; j++)
			WindowShadowPutPixel(win, x + i, y + j, shadow_w, col);
}

/*
 * CreateWindow -- create a new window
 * @param x -- X position of the window
 * @param y -- Y position of the window
 * @param w -- Width of the window
 * @param h -- Height of the window
 * @param flags -- Flags for thr window
 * @param ownerId -- process owner id of the window
 * @param title -- title of the window
 */
Window* CreateWindow(int x, int y, int w, int h, uint16_t flags, uint16_t ownerId, char* title) {
	uint16_t shKey = 0;
	uint16_t backBufferKey = 0;
	int64_t w_ = w, h_ = h, x_ = x, y_ = y;
	Window* win = (Window*)malloc(sizeof(Window));
	memset(win, 0, sizeof(Window));
	win->flags = flags;
	win->backBuffer = (uint32_t*)CreateNewBackBuffer(
		ownerId, ((w_ * h_ * 4 + 0x1F) & (~0x1FULL)), &backBufferKey);
	_KePrint("[deodhai]: backbuffer created \r\n");
	win->ownerId = ownerId;
	win->backBufferKey = backBufferKey;
	win->sharedInfo = CreateSharedWinSpace(&shKey, ownerId);
	_KePrint("[deodhai]: shared win space created \r\n");
	win->shWinKey = shKey;
	win->title = (char*)malloc(strlen(title));
	memset(win->title, 0, strlen(title));
	strcpy(win->title, title);
	WinSharedInfo* shwin = (WinSharedInfo*)win->sharedInfo;
	shwin->x = x;
	shwin->y = y;
	shwin->width = w;
	shwin->height = h;
	shwin->rect_count = 0;
	shwin->alpha = false;
	shwin->dirty = false;
	shwin->windowReady = false;
	win->handle = DeodhaiAllocateNewHandle();

#ifdef SHADOW_ENABLED
	if (!(win->flags & WINDOW_FLAG_ALWAYS_ON_TOP)) {
		win->shadowBuffers = (uint32_t*)_KeMemMap(
			NULL,
			(((w_ + SHADOW_SIZE * 2) * (h_ + SHADOW_SIZE * 2) * 4 + 0x1F) & (~0x1FULL)),
			0,
			0,
			MEMMAP_NO_FILEDESC,
			0);
		for (int i = 0; i < shwin->width + SHADOW_SIZE * 2; i++) {
			for (int j = 0; j < shwin->height + SHADOW_SIZE * 2; j++) {
				win->shadowBuffers[j * (shwin->width + SHADOW_SIZE * 2) + i] = 0x00000000;
			}
		}

		WindowShadowFillColor(win,
							  (shwin->width + SHADOW_SIZE * 2),
							  8,
							  10,
							  (shwin->width + SHADOW_SIZE * 2) - 8 * 2,
							  (shwin->height + SHADOW_SIZE * 2) - 8 * 2,
							  SHADOW_COLOR);

		for (int i = 0; i < 10; i++)
			ChBoxBlur(DeodhaiGetMainCanvas(),
					  win->shadowBuffers,
					  win->shadowBuffers,
					  1,
					  1,
					  (shwin->width + SHADOW_SIZE * 2) - 1,
					  (shwin->height + SHADOW_SIZE * 2) - 2);
	}
#endif
	return win;
}
