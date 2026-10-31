/**
* BSD 2-Clause License
*
* Copyright (c) 2023-2024, Manas Kamal Choudhury
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

#include "deodhai.h"
#include "backdirty.h"
#include "animation.h"

void FadeInAnimationWindow(ChCanvas* canv,
						   Window* win,
						   WinSharedInfo* info,
						   int64_t win_x,
						   int64_t win_y,
						   int64_t win_w,
						   int64_t win_h) {
	/* just to avoid floating point arithmetic issues */
	double opacity = win->animAlphaVal / 10.0;
	if (win->animAlphaVal == 10) {
		win->flags &= ~WINDOW_FLAG_ANIMATION_FADE_IN;
		win->flags &= ~WINDOW_FLAG_ANIMATED;
		info->updateEntireWindow = 1;
	}

	for (int j = 0; j < win_h; j++) {
		for (int i = 0; i < win_w; i++) {
			*(uint32_t*)(canv->buffer + ((win_y - SHADOW_SIZE) + j) * canv->canvasWidth +
						 ((win_x - SHADOW_SIZE) + i)) =
				ChColorAlphaBlend(*(uint32_t*)(canv->buffer +
											   ((win_y - SHADOW_SIZE) + j) * canv->canvasWidth +
											   ((win_x - SHADOW_SIZE) + i)),
								  *(uint32_t*)(win->backBuffer + j * (win_w + SHADOW_SIZE * 2) + i),
								  opacity);
		}
	}

	if (win->animAlphaVal != 10) {
		win->animAlphaVal += 1;
		info->updateEntireWindow = 1;
		AddDirtyClip(win_x, win_y, win_w, win_h);
	}
}

void FadeOutAnimationWindow(ChCanvas* canv,
							Window* win,
							WinSharedInfo* info,
							int64_t win_x,
							int64_t win_y,
							int64_t win_w,
							int64_t win_h) {
	/* just to avoid floating point arithmetic issues */
	double opacity = win->animAlphaVal / 10.0;
	BackDirtyAdd(win_x, win_y, win_w, win_h);
	if (win->animAlphaVal == 0) {
		win->flags &= ~WINDOW_FLAG_ANIMATION_FADE_OUT;
		win->flags &= ~WINDOW_FLAG_ANIMATED;
	}

	for (int j = 0; j < win_h; j++) {
		for (int i = 0; i < win_w; i++) {
			*(uint32_t*)(canv->buffer + ((win_y - SHADOW_SIZE) + j) * canv->canvasWidth +
						 ((win_x - SHADOW_SIZE) + i)) =
				ChColorAlphaBlend(*(uint32_t*)(canv->buffer +
											   ((win_y - SHADOW_SIZE) + j) * canv->canvasWidth +
											   ((win_x - SHADOW_SIZE) + i)),
								  *(uint32_t*)(win->backBuffer + j * (win_w + SHADOW_SIZE * 2) + i),
								  opacity);
		}
	}

	if (win->animAlphaVal != 0) {
		win->animAlphaVal -= 1;
		info->updateEntireWindow = 1;
		AddDirtyClip(win_x, win_y, win_w, win_h);
	}
	DeodhaiUpdateBits(true, true);
}