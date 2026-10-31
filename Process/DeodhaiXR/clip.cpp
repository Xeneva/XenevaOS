/**
* BSD 2-Clause License
* 
* @file clip.cpp
*
* Copyright (c) 2022-2026, Manas Kamal Choudhury
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

#include "deodxr.h"
#include <stdint.h>
#include <string.h>
#include "clip.h"
#include "rect.h"

bool ClipValueInRange(int value, int min, int max) {
	return (value >= min) && (value <= max);
}

#define CLIP_MAX_RECTS 100

void clip_emit(Rect* list, int* n, int cap, int l, int t, int r, int b) {
	if (r <= l || b <= t || *n >= cap)
		return;
	list[*n].x = l;
	list[*n].y = t;
	list[*n].w = r - l;
	list[*n].h = b - t;
	(*n)++;
}

/**
* @brief Check if two rectangle intersects
* @param r1 -- rectangle one
* @param r2 -- rectangle two
*/
bool ClipCheckIntersect(Rect* r1, Rect* r2) {
	bool xOverlap = ClipValueInRange(r1->x, r2->x, r2->x + r2->w) ||
					ClipValueInRange(r2->x, r1->x, r1->x + r1->w);

	bool yOverlap = ClipValueInRange(r1->y, r2->y, r2->y + r2->h) ||
					ClipValueInRange(r2->y, r1->y, r1->y + r1->h);

	return xOverlap && yOverlap;
}

/**
* @brief ClipCalculateRect -- calculate visible rectanlges of current window
* @param sub_rect -- subject rectangle
* @param cut_rect -- cutting rectangle
* @param list -- pointer to the list, where to store all
* visible rectangles
* @param count -- number of rectangles stored in the list
*/
void ClipCalculateRect(Rect* sub_rect, Rect* cut_rect, Rect* list, int* count) {
	int sl = sub_rect->x, st = sub_rect->y;
	int sr = sl + sub_rect->w, sb = st + sub_rect->h;
	int cl = cut_rect->x > sl ? cut_rect->x : sl;
	int ct = cut_rect->y > st ? cut_rect->y : st;
	int cr = cut_rect->x + cut_rect->w < sr ? cut_rect->x + cut_rect->w : sr;
	int cb = cut_rect->y + cut_rect->h < sb ? cut_rect->y + cut_rect->h : sb;

	if (cl >= cr || ct >= cb) {
		clip_emit(list, count, CLIP_MAX_RECTS, sl, st, sr, sb);
		return;
	}

	clip_emit(list, count, CLIP_MAX_RECTS, sl, st, cl, sb); //left
	clip_emit(list, count, CLIP_MAX_RECTS, cr, st, sr, sb); //right
	clip_emit(list, count, CLIP_MAX_RECTS, cl, st, cr, ct); //top
	clip_emit(list, count, CLIP_MAX_RECTS, cl, cb, cr, sb); //bottom

	/**int r_count = *count;

	Rect subcopy;
	subcopy.x = sub_rect->x;
	subcopy.y = sub_rect->y;
	subcopy.w = sub_rect->w;
	subcopy.h = sub_rect->h;

	if (RectGetLeft(cut_rect) >= RectGetLeft(&subcopy) &&
		RectGetLeft(cut_rect) <= RectGetRight(&subcopy)) {
		Rect r;
		memcpy(&r, &subcopy, sizeof(Rect));
		int top = RectSetTop(&r, RectGetTop(&subcopy));
		int left = RectSetLeft(&r, RectGetLeft(&subcopy));
		int bottom = RectSetBottom(&r, RectGetBottom(&subcopy));
		int right = RectSetRight(&r, RectGetLeft(cut_rect));

		list[r_count].x = left;
		list[r_count].y = top;
		list[r_count].w = right - left;
		list[r_count].h = bottom - top;
		r_count++;

		int n_l = RectSetLeft(&subcopy, RectGetLeft(cut_rect));
		subcopy.x = n_l;
	}

	if (RectGetTop(cut_rect) >= RectGetTop(&subcopy) &&
		RectGetTop(cut_rect) <= RectGetBottom(&subcopy)) {
		Rect r;
		memcpy(&r, &subcopy, sizeof(Rect));
		int top = RectSetTop(&r, RectGetTop(&subcopy));
		int left = RectSetLeft(&r, RectGetLeft(&subcopy));
		int bottom = RectSetBottom(&r, RectGetTop(cut_rect));
		int right = RectSetRight(&r, RectGetRight(&subcopy));

		list[r_count].x = left;
		list[r_count].y = top;
		list[r_count].w = right - left;
		list[r_count].h = bottom - top;
		r_count++;
		int n_t = RectSetTop(&subcopy, RectGetTop(cut_rect));
		subcopy.y = n_t;
	}

	if (RectGetRight(cut_rect) >= RectGetLeft(&subcopy) &&
		RectGetRight(cut_rect) < RectGetRight(&subcopy)) {
		Rect r;
		memcpy(&r, &subcopy, sizeof(Rect));
		int top = RectSetTop(&r, RectGetTop(&subcopy));
		int left = RectSetLeft(&r, RectGetRight(cut_rect));
		int bottom = RectSetBottom(&r, RectGetBottom(&subcopy));
		int right = RectSetRight(&r, RectGetRight(&subcopy));

		list[r_count].x = left;
		list[r_count].y = top;
		list[r_count].w = right - left;
		list[r_count].h = bottom - top;
		r_count++;

		RectSetRight(&subcopy, RectGetRight(cut_rect));
	}

	if (RectGetBottom(cut_rect) >= RectGetTop(&subcopy) &&
		RectGetBottom(cut_rect) <= RectGetBottom(&subcopy)) {
		Rect r;
		memcpy(&r, &subcopy, sizeof(Rect));
		int top = RectSetTop(&r, RectGetBottom(cut_rect));
		int left = RectSetLeft(&r, RectGetLeft(&subcopy));
		int bottom = RectSetBottom(&r, RectGetBottom(&subcopy));
		int right = RectSetRight(&r, RectGetRight(&subcopy));

		list[r_count].x = left;
		list[r_count].y = top;
		list[r_count].w = right - left;
		list[r_count].h = bottom - top;
		r_count++;
		RectSetBottom(&subcopy, RectGetBottom(cut_rect));
	}

	*count = r_count;**/
}

/**
* @brief ClipSubtractRect -- calculate visible rectanlges of current window
* @param sub_rect -- subject rectangle
* @param cut_rect -- cutting rectangle
* @param list -- pointer to the list, where to store all
* visible rectangles
* @param count -- number of rectangles stored in the list
*/
void ClipSubtractRect(Rect* sub_rect, Rect* cut_rect, Rect* list, int r_count) {
	if (RectGetLeft(cut_rect) >= RectGetLeft(sub_rect) &&
		RectGetLeft(cut_rect) <= RectGetRight(sub_rect)) {
		Rect r;
		memcpy(&r, sub_rect, sizeof(Rect));
		int top = RectSetTop(&r, RectGetTop(sub_rect));
		int left = RectSetLeft(&r, RectGetLeft(sub_rect));
		int bottom = RectSetBottom(&r, RectGetBottom(sub_rect));
		int right = RectSetRight(&r, RectGetLeft(cut_rect));

		list[r_count].x = left;
		list[r_count].y = top;
		list[r_count].w = right - left;
		list[r_count].h = bottom - top;

		int n_l = RectSetLeft(sub_rect, RectGetLeft(cut_rect));
		sub_rect->x = n_l;
	}

	if (RectGetTop(cut_rect) >= RectGetTop(sub_rect) &&
		RectGetTop(cut_rect) <= RectGetBottom(sub_rect)) {
		Rect r;
		memcpy(&r, sub_rect, sizeof(Rect));
		int top = RectSetTop(&r, RectGetTop(sub_rect));
		int left = RectSetLeft(&r, RectGetLeft(sub_rect));
		int bottom = RectSetBottom(&r, RectGetTop(cut_rect));
		int right = RectSetRight(&r, RectGetRight(sub_rect));

		list[r_count].x = left;
		list[r_count].y = top;
		list[r_count].w = right - left;
		list[r_count].h = bottom - top;

		int n_t = RectSetTop(sub_rect, RectGetTop(cut_rect));
		sub_rect->y = n_t;
	}

	if (RectGetRight(cut_rect) >= RectGetLeft(sub_rect) &&
		RectGetRight(cut_rect) < RectGetRight(sub_rect)) {
		Rect r;
		memcpy(&r, sub_rect, sizeof(Rect));
		int top = RectSetTop(&r, RectGetTop(sub_rect));
		int left = RectSetLeft(&r, RectGetRight(cut_rect));
		int bottom = RectSetBottom(&r, RectGetBottom(sub_rect));
		int right = RectSetRight(&r, RectGetRight(sub_rect));

		list[r_count].x = left;
		list[r_count].y = top;
		list[r_count].w = right - left;
		list[r_count].h = bottom - top;

		RectSetRight(sub_rect, RectGetRight(cut_rect));
	}

	if (RectGetBottom(cut_rect) >= RectGetTop(sub_rect) &&
		RectGetBottom(cut_rect) <= RectGetBottom(sub_rect)) {
		Rect r;
		memcpy(&r, sub_rect, sizeof(Rect));
		int top = RectSetTop(&r, RectGetBottom(cut_rect));
		int left = RectSetLeft(&r, RectGetLeft(sub_rect));
		int bottom = RectSetBottom(&r, RectGetBottom(sub_rect));
		int right = RectSetRight(&r, RectGetRight(sub_rect));

		list[r_count].x = left;
		list[r_count].y = top;
		list[r_count].w = right - left;
		list[r_count].h = bottom - top;

		RectSetBottom(sub_rect, RectGetBottom(cut_rect));
	}
}

void ClipGetBehindRect(Rect* sub_rect, Rect* cut_rect, Rect* list, int* r_count) {
	int count = *r_count;
	int x1 = (sub_rect->x > cut_rect->x) ? sub_rect->x : cut_rect->x;
	int y1 = (sub_rect->y > cut_rect->y) ? sub_rect->y : cut_rect->y;
	int x2 = (sub_rect->x + sub_rect->w < cut_rect->x + cut_rect->w) ? (sub_rect->x + sub_rect->w)
																	 : (cut_rect->x + cut_rect->w);
	int y2 = (sub_rect->y + sub_rect->h < cut_rect->y + cut_rect->h) ? (sub_rect->y + sub_rect->h)
																	 : (cut_rect->y + cut_rect->h);

	if (x2 <= x1 || y2 <= y1 || count >= 100)
		return;

	list[count].x = x1;
	list[count].y = y1;
	list[count].w = x2 - x1;
	list[count].h = y2 - y1;
	*r_count = count + 1;
}

void ClipSubtractRectList(Rect* inputList, int* inputCount, Rect* cut_rect) {
	Rect outputList[100];
	int outputCount = 0;

	for (int i = 0; i < *inputCount; i++) {
		if (ClipCheckIntersect(&inputList[i], cut_rect) && outputCount + 4 <= CLIP_MAX_RECTS) {
			Rect sub = inputList[i];
			ClipCalculateRect(&sub, cut_rect, outputList, &outputCount);
		} else {
			if (outputCount < CLIP_MAX_RECTS) {
				outputList[outputCount++] = inputList[i];
			}
		}
	}

	memcpy(inputList, outputList, outputCount * sizeof(Rect));
	*inputCount = outputCount;
}
