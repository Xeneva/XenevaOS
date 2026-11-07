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

#include "backdirty.h"
#include <string.h>

int _back_dirty_count;

Rect _back_dirty_rect[512];

/*
 * BackDirtyInitialise -- initialise the back
 * dirty count
 */
void BackDirtyInitialise() {
	_back_dirty_count = 0;
	for (int i = 0; i < 512; i++) {
		_back_dirty_rect[i].x = 0;
		_back_dirty_rect[i].y = 0;
		_back_dirty_rect[i].w = 0;
		_back_dirty_rect[i].h = 0;
	}
}

/*
 * BackDirtyAdd -- add a dirty rect to the list
 * @param x -- X coord of the rect
 * @param y -- Y coord of the rect
 * @param w -- Width of the rect
 * @param h -- Height of the rect
 */
void BackDirtyAdd(int x, int y, int w, int h) {
	if (_back_dirty_count >= 512)
		_back_dirty_count = 0;
	if (x < 0)
		_KePrint("BackDirty x < 0 -> %d \r\n", x);
	if (y < 0)
		_KePrint("BackDirty y < 0 -> %d \r\n", y);
	if (w < 0)
		_KePrint("BackDirty w is corrupted \r\n");
	if (h < 0)
		_KePrint("BackDirty h is corrupted \r\n");

	_back_dirty_rect[_back_dirty_count].x = x;
	_back_dirty_rect[_back_dirty_count].y = y;
	_back_dirty_rect[_back_dirty_count].w = w;
	_back_dirty_rect[_back_dirty_count].h = h;
	_back_dirty_count++;
}

/*
 * BackDirtyGetRect -- get a rect from the list
 * @param x -- Pointer where to store x
 * @param y -- Pointer where to store y
 * @param w -- mem pointer where to store w
 * @param h -- mem pointer where to store h
 * @param index -- index 
 */
void BackDirtyGetRect(int* x, int* y, int* w, int* h, int index) {
	*x = _back_dirty_rect[index].x;
	*y = _back_dirty_rect[index].y;
	*w = _back_dirty_rect[index].w;
	*h = _back_dirty_rect[index].h;
}

int BackDirtyGetDirtyCount() {
	return _back_dirty_count;
}

void BackDirtyCountReset() {
	_back_dirty_count = 0;
}