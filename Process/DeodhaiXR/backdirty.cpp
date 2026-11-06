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

static bool back_rects_touch_or_overlap(const Rect* a, const Rect* b) {
	int64_t a_right = (int64_t)a->x + a->w;
	int64_t a_bottom = (int64_t)a->y + a->h;
	int64_t b_right = (int64_t)b->x + b->w;
	int64_t b_bottom = (int64_t)b->y + b->h;
	return (int64_t)a->x <= b_right && (int64_t)b->x <= a_right &&
		   (int64_t)a->y <= b_bottom && (int64_t)b->y <= a_bottom;
}

static Rect back_rect_union(const Rect* a, const Rect* b) {
	int64_t left = a->x < b->x ? a->x : b->x;
	int64_t top = a->y < b->y ? a->y : b->y;
	int64_t right_a = (int64_t)a->x + a->w;
	int64_t right_b = (int64_t)b->x + b->w;
	int64_t bottom_a = (int64_t)a->y + a->h;
	int64_t bottom_b = (int64_t)b->y + b->h;
	int64_t right = right_a > right_b ? right_a : right_b;
	int64_t bottom = bottom_a > bottom_b ? bottom_a : bottom_b;
	Rect result = {(int)left, (int)top, (int)(right - left), (int)(bottom - top)};
	return result;
}

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
	if (w <= 0 || h <= 0)
		return;

	Rect incoming = {x, y, w, h};
	for (int i = 0; i < _back_dirty_count;) {
		if (!back_rects_touch_or_overlap(&incoming, &_back_dirty_rect[i])) {
			i++;
			continue;
		}
		incoming = back_rect_union(&incoming, &_back_dirty_rect[i]);
		_back_dirty_rect[i] = _back_dirty_rect[--_back_dirty_count];
		i = 0;
	}

	if (_back_dirty_count < 512) {
		_back_dirty_rect[_back_dirty_count++] = incoming;
		return;
	}

	for (int i = 0; i < _back_dirty_count; i++)
		incoming = back_rect_union(&incoming, &_back_dirty_rect[i]);
	_back_dirty_rect[0] = incoming;
	_back_dirty_count = 1;
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
