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

#include "dirty.h"
#include <sys/iocodes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/_kefile.h>

static uint32_t _dirty_count = 0;
Rect dirtyRect[100];
XEFileIOControl ioctl;

void InitialiseDirtyClipList() {
	for (int i = 0; i < 100; i++) {
		dirtyRect[i].x = 0;
		dirtyRect[i].y = 0;
		dirtyRect[i].w = 0;
		dirtyRect[i].h = 0;
	}
	_dirty_count = 0;
	memset(&ioctl, 0, sizeof(XEFileIOControl));
}
/*
 * AddDirtyClip -- add a dirty clip rectangle
 * @param x -- x position
 * @param y -- y position
 * @param w -- width of the rect
 * @param h -- height of the rect
 */
void AddDirtyClip(int x, int y, int w, int h) {
	if (_dirty_count >= 100)
		_dirty_count = 0;

	if (x < 0)
		_KePrint("AddDirtyClip: corrupted x value %d\r\n", x);
	if (y < 0)
		_KePrint("AddDirtyClip: corrupted y value %d\r\n", y);

	dirtyRect[_dirty_count].x = x;
	dirtyRect[_dirty_count].y = y;
	dirtyRect[_dirty_count].w = w;
	dirtyRect[_dirty_count].h = h;
	_dirty_count++;
	//_KePrint("Dirty clip updated %d %d \r\n", dirtyRect[_dirty_count].x,
	//	dirtyRect[_dirty_count].y);
	//_KePrint("DC -> %d \r\n", _dirty_count);
}

extern int _get_gpu_fd();
extern int _get_gpu_display_id();
extern bool _is_gpu_enabled();

/*
 * DirtyScreenUpdate -- update the screen with respect
 * to dirty rect boundary
 * @param canvas -- pointer to canvas
 */
void DirtyScreenUpdate(ChCanvas* canvas) {
	int display_id = _get_gpu_display_id();

	bool gpu_update = false;
	for (int i = 0; i < _dirty_count; i++) {
		gpu_update = 1;
		if (dirtyRect[i].x < 0) {
			_KePrint("DirtyR -x %d\r\n", dirtyRect[i].x);
			dirtyRect[i].x = 0;
		}
		if (dirtyRect[i].y < 0) {
			_KePrint("DirtyRect -y %d\r\n", dirtyRect[i].y);
			dirtyRect[i].y = 0;
		}
		if (dirtyRect[i].w > canvas->canvasWidth) {
			_KePrint("Dirty Rect w - %d \r\n", dirtyRect[i].w);
			dirtyRect[i].w = canvas->canvasWidth;
		}
		if (dirtyRect[i].h > canvas->canvasHeight) {
			_KePrint("Dirty Rect h - %d \r\n", dirtyRect[i].h);
			dirtyRect[i].h = canvas->canvasHeight;
		}

		if (!_is_gpu_enabled())
			ChCanvasScreenUpdate(
				canvas, dirtyRect[i].x, dirtyRect[i].y, dirtyRect[i].w, dirtyRect[i].h);

		//ioctl.uint_1 = display_id;
		//_KeFileIoControl(_get_gpu_fd(), 0x203, &ioctl);
	}

	if (gpu_update && _is_gpu_enabled()) {
		ioctl.uint_1 = display_id;
		ioctl.ushort_1 = 0;
		ioctl.ushort_2 = 0;
		ioctl.ulong_1 = canvas->screenWidth;
		ioctl.ulong_2 = canvas->screenHeight;
		_KeFileIoControl(_get_gpu_fd(), 0x202, &ioctl);
	}

	_dirty_count = 0;
}

/*
 * GetDirtyRectCount -- returns the number of dirty rect
 */
uint32_t GetDirtyRectCount() {
	return _dirty_count;
}