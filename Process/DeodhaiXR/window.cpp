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
#include "deodxr.h"
#include <sys/mman.h>
#include <stdlib.h>
#include <color.h>
#include <string.h>
#include "boxblur.h"
#include "alpha.h"

uint16_t shared_win_key_prefix = 1000;
uint16_t back_buffer_key_prefix = 400;

#define DEODHAI_SHM_KEY_FIRST 1000
#define DEODHAI_SHM_KEY_LAST  4600
static uint16_t next_shm_key = DEODHAI_SHM_KEY_FIRST;

static uint16_t DeodhaiNextShmKey() {
	uint16_t key = next_shm_key++;
	if (next_shm_key > DEODHAI_SHM_KEY_LAST)
		next_shm_key = DEODHAI_SHM_KEY_FIRST;
	return key;
}

/*
 * CreateSharedWinSpace -- Create a shared window space
 * @param shkey -- location where to store the window key
 * @param ownerId -- owning process id
 */
uint32_t* CreateSharedWinSpace(uint16_t* shkey, uint16_t ownerId) {
	(void)ownerId;
	uint16_t key = DeodhaiNextShmKey(); //shared_win_key_prefix + ownerId;
	int id = _KeCreateSharedMem(key, sizeof(WinSharedInfo), 0);
	if (id < 0)
		return NULL;
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
	uint16_t key_prefix = DeodhaiNextShmKey(); //back_buffer_key_prefix + ownerId;
	int id = _KeCreateSharedMem(key_prefix, sz, 0);
	void* ptr = _KeObtainSharedMem(id, NULL, 0);
	*key = key_prefix;
	back_buffer_key_prefix += 10;
	return ptr;
}

void _window_generate_shadow(Window* win, int winw, int winh);

void ReleaseWindowEffects(Window* win) {
	size_t bytes = (size_t)win->originalW * win->originalH * sizeof(uint32_t);
	if (win->glassBlur)
		_KeMemUnmap(win->glassBlur, bytes);
	if (win->glassTmp)
		_KeMemUnmap(win->glassTmp, bytes);
	win->glassBlur = win->glassTmp = NULL;
	win->glassBlurValid = false;
#ifdef SHADOW_ENABLED
	size_t shadowBytes = (size_t)(win->originalW + 2 * SHADOW_SIZE) *
						 (win->originalH + 2 * SHADOW_SIZE) * sizeof(uint32_t);
	if (win->shadowBuffers)
		_KeMemUnmap(win->shadowBuffers, shadowBytes);
	if (win->shadowTmp)
		_KeMemUnmap(win->shadowTmp, shadowBytes);
	win->shadowBuffers = win->shadowTmp = NULL;
#endif
}

bool ResizeWindowBackBuffer(Window* win, int width, int height) {
	uint16_t key = 0;
	size_t bytes = (size_t)width * height * sizeof(uint32_t);
	uint32_t* buffer = (uint32_t*)CreateNewBackBuffer(win->ownerId, bytes, &key);
	if (!buffer)
		return false;
	memset(buffer, 0, bytes);
	_KeUnmapSharedMem(win->backBufferKey);
	win->backBuffer = buffer;
	win->backBufferKey = key;
	ReleaseWindowEffects(win);

	if (win->flags & WINDOW_FLAG_GLASS) {
		win->glassBlur = (uint32_t*)_KeMemMap(NULL, bytes, 0, 0, MEMMAP_NO_FILEDESC, 0);
		win->glassTmp = (uint32_t*)_KeMemMap(NULL, bytes, 0, 0, MEMMAP_NO_FILEDESC, 0);
		win->glassBlurValid = false;
	}
#ifdef SHADOW_ENABLED
	size_t shadowBytes =
		(size_t)(width + 2 * SHADOW_SIZE) * (height + 2 * SHADOW_SIZE) * sizeof(uint32_t);
	win->shadowBuffers = (uint32_t*)_KeMemMap(NULL, shadowBytes, 0, 0, MEMMAP_NO_FILEDESC, 0);
	win->shadowTmp = (uint32_t*)_KeMemMap(NULL, shadowBytes, 0, 0, MEMMAP_NO_FILEDESC, 0);
	_window_generate_shadow(win, width, height);
#endif
	win->originalW = width;
	win->originalH = height;
	WinSharedInfo* info = (WinSharedInfo*)win->sharedInfo;
	info->width = width;
	info->height = height;
	info->rect_count = 0;
	WinSharedFlagStore(&info->dirty, false);
	WinSharedFlagStore(&info->updateEntireWindow, true);
	return true;
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

void _window_generate_shadow(Window* win, int winw, int winh) {
	int sw = winw + 2 * SHADOW_SIZE;
	int sh = winh + 2 * SHADOW_SIZE;

	memset(win->shadowBuffers, 0, sw * sh * sizeof(uint32_t));

	int fillx = SHADOW_SIZE + SHADOW_OFFSET_X;
	int filly = SHADOW_SIZE + SHADOW_OFFSET_Y;

	for (int y = filly; filly + winh && y < sh; y++) {
		for (int x = fillx; x < fillx + winw && x < sw; x++) {
			win->shadowBuffers[y * sw + x] = ((uint32_t)SHADOW_ALPHA << 24) | 0x00000000;
		}
	}

	_shadow_blur_horizontal(win->shadowTmp, win->shadowBuffers, sw, sh, SHADOW_BLUR);
	_shadow_blur_vertical(win->shadowBuffers, win->shadowTmp, sw, sh, SHADOW_BLUR);
}

#define SHADOW_SIZE_S 0
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

	if (w <= 0 || h <= 0 || (uint64_t)w * (uint64_t)h > UINT32_MAX / 4)
		return NULL;

	Window* win = (Window*)malloc(sizeof(Window));
	memset(win, 0, sizeof(Window));
	win->flags = flags;
	win->backBuffer = (uint32_t*)CreateNewBackBuffer(
		ownerId, ((w_ * h_ * 4 + 0x1F) & (~0x1FULL)), &backBufferKey);
	win->ownerId = ownerId;
	win->backBufferKey = backBufferKey;
	win->sharedInfo = CreateSharedWinSpace(&shKey, ownerId);
	win->shWinKey = shKey;
	win->title = (char*)malloc(strlen(title) + 1);
	memset(win->title, 0, strlen(title) + 1);
	strcpy(win->title, title);
	win->originalW = w;
	win->originalH = h;
	WinSharedInfo* shwin = (WinSharedInfo*)win->sharedInfo;
	shwin->x = x;
	shwin->y = y;
	shwin->width = w;
	shwin->height = h;
	shwin->rect_count = 0;
	shwin->alpha = false;
	shwin->dirty = false;
	shwin->windowReady = false;
	/* Shared segments come from recycled physical pages (AuCreateSHM does
	 * not zero them). Every flag must be initialised here, otherwise a new
	 * window inherits the previous owner's state -- e.g. zoomed=true from
	 * a maximised window makes the next window open fullscreen. */
	shwin->updateEntireWindow = false;
	shwin->hide = false;
	shwin->alphaValue = 1.0;
	shwin->zoomed = false;
	win->handle = DeodhaiAllocateNewHandle();
	if (flags & WINDOW_FLAG_GLASS) {
		win->glassBlur = (uint32_t*)_KeMemMap(
			NULL,
			(((w_ + SHADOW_SIZE_S * 2) * (h_ + SHADOW_SIZE_S * 2) * 4 + 0x1F) & (~0x1FULL)),
			0,
			0,
			MEMMAP_NO_FILEDESC,
			0);
		win->glassTmp = (uint32_t*)_KeMemMap(
			NULL,
			(((w_ + SHADOW_SIZE_S * 2) * (h_ + SHADOW_SIZE_S * 2) * 4 + 0x1F) & (~0x1FULL)),
			0,
			0,
			MEMMAP_NO_FILEDESC,
			0);
	}
#ifdef SHADOW_ENABLED
	int sw = w + 2 * SHADOW_SIZE;
	int sh = h + 2 * SHADOW_SIZE;
	win->shadowBuffers = (uint32_t*)_KeMemMap(
		NULL, ((sw * sh * 4 + 0x1F) & (~0x1FULL)), 0, 0, MEMMAP_NO_FILEDESC, 0);

	win->shadowTmp = (uint32_t*)_KeMemMap(
		NULL, ((sw * sh * 4 + 0x1F) & (~0x1FULL)), 0, 0, MEMMAP_NO_FILEDESC, 0);

	_window_generate_shadow(win, w, h);
#endif
	return win;
}
