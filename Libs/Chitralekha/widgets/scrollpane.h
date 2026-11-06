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

#ifndef __SCROLLPANE_H__
#define __SCROLLPANE_H__

#include <stdint.h>
#include "../chitralekha.h"
#include "base.h"
#include "window.h"

#define CHITRALEKHA_SCROLL_TYPE_HORIZONTAL 1
#define CHITRALEKHA_SCROLL_TYPE_VERTICAL   2

#define SCROLLBAR_SIZE 18

typedef struct _scrollbar_ {
	uint8_t type;
	bool update;
	int bar_x;
	int bar_y;
	int bar_w;
	int bar_h;
	int thumb_posx;
	int thumb_posy;
	int thumb_width;
	int thumb_height;
	int thumb_dragx;
	int thumb_dragy;
	int thumb_lasty;
	int thumb_lastx;
	bool thumbHover;
	double scrollAmount;
	double scrollOffset;
	int contentSize;
} ChScrollbar;

typedef struct _scrollpane_ {
	ChWidget wid;
	ChScrollbar hScrollBar;
	ChScrollbar vScrollBar;
	bool paintCallback;
	int lastMouseButton;
	ChWidget* scrollableView;
} ChScrollPane;

/*
* ChCreateScrollPane -- Create a new scroll pane
* @param win -- Pointer to main Window
* @param x -- X position of the scroll pane
* @param y -- Y position of the scroll pane
* @param width -- Width of the scroll pane
* @param height -- Height of the scroll pane
*/
XE_EXTERN XE_LIB ChScrollPane*
ChCreateScrollPane(ChWindow* win, int x, int y, int width, int height);

/*
* ChScrollUpdateVerticalScroll -- updates the vertical scroll bur thumb
* @param sp -- Pointer to scrollpane
* @param viewport -- viewport
* @param contentsz -- content size
*/
XE_EXTERN XE_LIB void
ChScrollUpdateVerticalScroll(ChScrollPane* sp, ChRect* viewport, int contentSz);

/*
* ChScrollUpdateHorizontalScroll -- updates the horizontal scroll bar thumb
* @param sp -- Pointer to scrollpane
* @param viewport -- Pointer to viewport geometry
* @param contentSz -- content size
*/
XE_EXTERN XE_LIB void
ChScrollUpdateHorizontalScroll(ChScrollPane* sp, ChRect* viewport, int contentSz);

#endif