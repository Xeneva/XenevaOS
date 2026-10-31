/**
* @file scrollbar.h
*
* BSD 2-Clause License
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

#ifndef __SCROLLBAR_H__
#define __SCROLLBAR_H__

#include "base.h"
#include "../chitralekha.h"
#include "window.h"
#include <_xeneva.h>

#ifdef __cplusplus
XE_EXTERN {
#endif

	enum _orientation_ {
		SCROLLBAR_ORIENTATION_VERTICAL,
		SCROLLBAR_ORIENTATION_HORIZONTAL,
	};

	enum {
		SB_STATE_IDLE,
		SB_STATE_HOVER_THUMB,
		SB_STATE_DRAGGING,
		SB_STATE_HOVER_ARRDEC,
		SB_STATE_HOVER_ARRINC
	};

	typedef struct _scroll_bar_ {
		ChWidget base;
		uint8_t orientation;
		uint8_t state;
		/* Range and value */
		int minValue;
		int maxValue;
		int currentValue;
		int pageSize;

		// thumb
		int thumbPos;
		int thumbSize;
		int trackLen;

		int dragAnchor;
		int dragAnchorValue;

		uint8_t prevButton;
		void* attachedTo;

		//theming
		uint32_t color_track;
		uint32_t color_thumb;
		uint32_t color_thumb_hover;
		uint32_t color_thumb_drag;
		uint32_t color_arrow_bg;
		uint32_t color_arrow_fg;
	} ChScrollBar;

	XE_EXPORT ChScrollBar* ChCreateScrollBar(
		int x, int y, int width, int height, uint8_t orientation);
	XE_EXPORT void ChScrollBarInit(
		ChScrollBar * sb, int x, int y, int width, int height, uint8_t orientation);
	XE_EXPORT void ChScrollBarSetValue(ChScrollBar * sb, int value);
	XE_EXPORT void ChScrollBarSetRange(ChScrollBar * sb, int min_val, int max_val, int page_size);

#ifdef __cplusplus
}
#endif

#endif
