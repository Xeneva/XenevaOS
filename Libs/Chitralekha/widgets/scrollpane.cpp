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

#include "scrollpane.h"
#include <stdlib.h>
#include <math.h>
#include <sys/_keproc.h>

/*
* ChDefaultScrollPanePainter -- default scroll pane painter
*/
void ChDefaultScrollPanePainter(ChWidget* wid, ChWindow* win);

void ChDefaultScrollPaneMouseEvent(ChWidget* wid, ChWindow* win, int x, int y, int button) {
	ChScrollPane* sp = (ChScrollPane*)wid;

	if (button == DEODHAI_MOUSE_MSG_SCROLL_UP) {
		sp->vScrollBar.thumb_posy -= sp->vScrollBar.scrollAmount;

		if ((sp->vScrollBar.bar_y + sp->vScrollBar.thumb_posy) <= sp->vScrollBar.bar_y)
			sp->vScrollBar.thumb_posy = 0; // sp->vScrollBar.bar_y;

		sp->vScrollBar.scrollOffset = sp->vScrollBar.thumb_posy;
		if (sp->wid.ChPaintHandler)
			sp->wid.ChPaintHandler((ChWidget*)sp, win);

		if (!sp->scrollableView)
			ChWindowUpdate(win,
						   sp->vScrollBar.bar_x,
						   sp->vScrollBar.bar_y,
						   sp->vScrollBar.bar_w,
						   sp->vScrollBar.bar_h,
						   0,
						   1);

#ifdef ARCH_X64
		_KeProcessSleep(100);
#elif ARCH_ARM64
		_KeProcessSleep(1);
#endif

		if (sp->scrollableView)
			sp->scrollableView->ChScrollEvent(sp->scrollableView,
											  win,
											  sp->vScrollBar.thumb_posy,
											  CHITRALEKHA_SCROLL_TYPE_VERTICAL);

#ifdef ARCH_X64
		_KeProcessSleep(200);
#elif ARCH_ARM64
		_KeProcessSleep(1);
#endif
	}

	if (button == DEODHAI_MOUSE_MSG_SCROLL_DOWN) {
		sp->vScrollBar.thumb_posy += sp->vScrollBar.scrollAmount;

		if ((sp->vScrollBar.bar_y + sp->vScrollBar.thumb_posy + sp->vScrollBar.thumb_height) >=
			sp->vScrollBar.bar_y + sp->vScrollBar.bar_h) {
			sp->vScrollBar.thumb_posy =
				(win->info->y + sp->vScrollBar.bar_y + sp->vScrollBar.bar_h) -
				(win->info->y + sp->vScrollBar.bar_y + sp->vScrollBar.thumb_height) - 2;
		}

		if ((sp->vScrollBar.bar_y + sp->vScrollBar.thumb_posy) <= sp->vScrollBar.bar_y)
			sp->vScrollBar.thumb_posy = 1; // sp->vScrollBar.bar_y;

		sp->vScrollBar.scrollOffset = sp->vScrollBar.thumb_posy;
		if (sp->wid.ChPaintHandler)
			sp->wid.ChPaintHandler((ChWidget*)sp, win);

		if (!sp->scrollableView)
			ChWindowUpdate(win,
						   sp->vScrollBar.bar_x,
						   sp->vScrollBar.bar_y,
						   sp->vScrollBar.bar_w,
						   sp->vScrollBar.bar_h,
						   0,
						   1);

#ifdef ARCH_X64
		_KeProcessSleep(100);
#elif ARCH_ARM64
		_KeProcessSleep(1);
#endif

		if (sp->scrollableView)
			sp->scrollableView->ChScrollEvent(sp->scrollableView,
											  win,
											  sp->vScrollBar.thumb_posy,
											  CHITRALEKHA_SCROLL_TYPE_VERTICAL);

#ifdef ARCH_X64
		_KeProcessSleep(200);
#elif ARCH_ARM64
		_KeProcessSleep(1);
#endif
	}

	/* check for any scrollbar pending updates */
	if (x >= (win->info->x + sp->hScrollBar.bar_x) &&
		x < (win->info->x + sp->hScrollBar.bar_x + sp->hScrollBar.bar_w) &&
		y >= (win->info->y + sp->hScrollBar.bar_y) &&
		y < (win->info->y + sp->hScrollBar.bar_y + sp->hScrollBar.bar_h)) {
		if (sp->hScrollBar.thumbHover) {
			sp->hScrollBar.update = 1;
			sp->hScrollBar.thumbHover = 0;
			if (sp->wid.ChPaintHandler)
				sp->wid.ChPaintHandler((ChWidget*)sp, win);
			ChWindowUpdate(win,
						   sp->hScrollBar.bar_x,
						   sp->hScrollBar.bar_y,
						   sp->hScrollBar.bar_w,
						   sp->hScrollBar.bar_h,
						   0,
						   1);
		}
	}

	if (x >= (win->info->x + sp->vScrollBar.bar_x) &&
		x < (win->info->x + sp->vScrollBar.bar_x + sp->vScrollBar.bar_w) &&
		y >= (win->info->y + sp->vScrollBar.bar_y) &&
		y < (win->info->y + sp->vScrollBar.bar_y + sp->vScrollBar.bar_h)) {
		if (sp->vScrollBar.thumbHover) {
			sp->vScrollBar.update = 1;
			sp->vScrollBar.thumbHover = 0;
			if (sp->wid.ChPaintHandler)
				sp->wid.ChPaintHandler((ChWidget*)sp, win);
			ChWindowUpdate(win,
						   sp->vScrollBar.bar_x,
						   sp->vScrollBar.bar_y,
						   sp->vScrollBar.bar_w,
						   sp->vScrollBar.bar_h,
						   0,
						   1);
		}
	}

	/* Horizontal Scroll bars mouse events */
	if (x >= (win->info->x + sp->hScrollBar.bar_x + sp->hScrollBar.thumb_posx) &&
		x < (win->info->x + sp->hScrollBar.bar_x + sp->hScrollBar.thumb_posx +
			 sp->hScrollBar.thumb_width) &&
		y >= (win->info->y + sp->hScrollBar.bar_y + sp->hScrollBar.thumb_posy) &&
		y < (win->info->y + sp->hScrollBar.bar_y + sp->hScrollBar.thumb_posy +
			 sp->hScrollBar.thumb_height)) {
		sp->hScrollBar.update = 1;
		sp->hScrollBar.thumbHover = 1;
		bool _scrolled = false;

		if (button) {
			sp->hScrollBar.thumb_posx =
				x - (win->info->x + sp->hScrollBar.bar_x + sp->hScrollBar.thumb_width / 2);

			if ((sp->hScrollBar.bar_x + sp->hScrollBar.thumb_posx + sp->hScrollBar.thumb_width) >=
				(sp->hScrollBar.bar_x + sp->hScrollBar.bar_w)) {
				sp->hScrollBar.thumb_posx =
					(win->info->x + sp->hScrollBar.bar_x + sp->hScrollBar.bar_w) -
					(win->info->x + sp->hScrollBar.bar_x + sp->hScrollBar.thumb_width);
			}

			if ((sp->hScrollBar.thumb_posx <= sp->hScrollBar.bar_x))
				sp->hScrollBar.thumb_posx = 0; // sp->hScrollBar.bar_x;

			int maxThumbX = sp->hScrollBar.bar_w - sp->hScrollBar.thumb_width;
			//double scrollSpeed = ((double)sp->vScrollBar.bar_h / (double)sp->vScrollBar.contentSize) * 3.0;
			//_KePrint("maxTHUMBY -> %d \r\n", maxThumbY);
			int maxScrollOffset = sp->hScrollBar.contentSize - sp->scrollableView->w;
			sp->hScrollBar.scrollOffset =
				((double)sp->hScrollBar.thumb_posx / (double)maxThumbX) * (double)maxScrollOffset;
			_scrolled = true;
		}

		if (sp->wid.ChPaintHandler)
			sp->wid.ChPaintHandler((ChWidget*)sp, win);

		if (!sp->scrollableView)
			ChWindowUpdate(win,
						   sp->hScrollBar.bar_x,
						   sp->hScrollBar.bar_y,
						   sp->hScrollBar.bar_w,
						   sp->hScrollBar.bar_h,
						   0,
						   1);

		if (_scrolled) {
			if (sp->scrollableView)
				sp->scrollableView->ChScrollEvent(sp->scrollableView,
												  win,
												  sp->hScrollBar.thumb_posx,
												  CHITRALEKHA_SCROLL_TYPE_HORIZONTAL);
			/*Process sleep because, slowing down the process little bit,
			 * because freetype frequenctly allocates and frees up memory
			 * which effects the kernel system HeapUnmapProcess due to
			 * heavy speed of scroll, TODO: fix this speed problem
			 * alternative solution is: we can use bitmap fonts for rendering
			 * fast widgets
			 */
#ifdef ARCH_X64
			_KeProcessSleep(200);
#elif ARCH_ARM64
			_KeProcessSleep(1);
#endif
		}
	}

	/*
	 * Vertical Scrollbars mouse event 
	 */
	if (x >= (win->info->x + sp->vScrollBar.bar_x + sp->vScrollBar.thumb_posx) &&
		x < (win->info->x + sp->vScrollBar.bar_x + sp->vScrollBar.thumb_posx +
			 sp->vScrollBar.thumb_width) &&
		y >= (win->info->y + sp->vScrollBar.bar_y + sp->vScrollBar.thumb_posy) &&
		y < (win->info->y + sp->vScrollBar.bar_y + sp->vScrollBar.thumb_posy +
			 sp->vScrollBar.thumb_height)) {
		sp->vScrollBar.update = 1;
		sp->vScrollBar.thumbHover = 1;
		int thumbDragOffset = 0;
		bool _scrolled = false;
		bool isDragging = false;

		if (button && !((button == DEODHAI_MOUSE_MSG_SCROLL_DOWN) ||
						(button == DEODHAI_MOUSE_MSG_SCROLL_UP))) {
			sp->vScrollBar.thumb_posy =
				y - (win->info->y + sp->vScrollBar.bar_y + sp->vScrollBar.thumb_height / 2);

			if ((sp->vScrollBar.bar_y + sp->vScrollBar.thumb_posy + sp->vScrollBar.thumb_height) >=
				sp->vScrollBar.bar_y + sp->vScrollBar.bar_h) {
				sp->vScrollBar.thumb_posy =
					(win->info->y + sp->vScrollBar.bar_y + sp->vScrollBar.bar_h) -
					(win->info->y + sp->vScrollBar.bar_y + sp->vScrollBar.thumb_height); //-2;
			}

			if ((sp->vScrollBar.bar_y + sp->vScrollBar.thumb_posy) <= sp->vScrollBar.bar_y)
				sp->vScrollBar.thumb_posy = 0; // sp->vScrollBar.bar_y;

			int maxThumbY = sp->vScrollBar.bar_h - sp->vScrollBar.thumb_height;
			//double scrollSpeed = ((double)sp->vScrollBar.bar_h / (double)sp->vScrollBar.contentSize) * 3.0;
			//_KePrint("maxTHUMBY -> %d \r\n", maxThumbY);
			int maxScrollOffset = sp->vScrollBar.contentSize - sp->scrollableView->h;
			sp->vScrollBar.scrollOffset =
				((double)sp->vScrollBar.thumb_posy / (double)maxThumbY) * (double)maxScrollOffset;
			//sp->vScrollBar.scrollOffset *= scrollSpeed;
			_scrolled = true;
		}

		if (sp->wid.ChPaintHandler)
			sp->wid.ChPaintHandler((ChWidget*)sp, win);

		if (!sp->scrollableView)
			ChWindowUpdate(win,
						   sp->vScrollBar.bar_x,
						   sp->vScrollBar.bar_y,
						   sp->vScrollBar.bar_w,
						   sp->vScrollBar.bar_h,
						   0,
						   1);

		if (_scrolled) {
			if (sp->scrollableView)
				sp->scrollableView->ChScrollEvent(sp->scrollableView,
												  win,
												  sp->vScrollBar.thumb_posy,
												  CHITRALEKHA_SCROLL_TYPE_VERTICAL);
			/*Process sleep because, slowing down the process little bit,
			 * because freetype frequenctly allocates and frees up memory
			 * which effects the kernel system HeapUnmapProcess due to
			 * heavy speed of scroll, TODO: fix this speed problem 
			 * alternative solution : we can use bitmap fonts for
			 * rendering fast widgets
			 */
#ifdef ARCH_X64
			_KeProcessSleep(200);
#elif ARCH_ARM64
			_KeProcessSleep(1);
#endif
		}
	}
}

void ChScrollpaneDestroy(ChWidget* wid, ChWindow* win) {
	ChScrollPane* sp = (ChScrollPane*)wid;
	free(sp);
	_KePrint("Scrollpane destroyed \r\n");
}

/*
 * ChCreateScrollPane -- Create a new scroll pane
 * @param win -- Pointer to main Window
 * @param x -- X position of the scroll pane
 * @param y -- Y position of the scroll pane
 * @param width -- Width of the scroll pane
 * @param height -- Height of the scroll pane
 */
ChScrollPane* ChCreateScrollPane(ChWindow* win, int x, int y, int width, int height) {
	ChScrollPane* sp = (ChScrollPane*)malloc(sizeof(ChScrollPane));
	memset(sp, 0, sizeof(ChScrollPane));
	sp->wid.ChPaintHandler = ChDefaultScrollPanePainter;
	sp->wid.ChMouseEvent = ChDefaultScrollPaneMouseEvent;
	sp->wid.ChDestroy = ChScrollpaneDestroy;
	sp->wid.x = x;
	sp->wid.y = y;
	sp->wid.w = width;
	sp->wid.h = height;
	sp->hScrollBar.bar_x = x;
	sp->hScrollBar.bar_y = y + height - SCROLLBAR_SIZE;
	sp->hScrollBar.bar_w = width - SCROLLBAR_SIZE;
	sp->hScrollBar.bar_h = SCROLLBAR_SIZE;
	sp->hScrollBar.thumb_posx = 1;
	sp->hScrollBar.thumb_posy = 2;
	sp->hScrollBar.thumb_width = 0;
	sp->hScrollBar.thumb_height = sp->hScrollBar.bar_h - 4;
	sp->hScrollBar.type = CHITRALEKHA_SCROLL_TYPE_HORIZONTAL;
	sp->hScrollBar.scrollAmount = 1.8;
	sp->hScrollBar.scrollOffset = 0;
	sp->hScrollBar.update = 1;

	sp->vScrollBar.bar_x = x + width - SCROLLBAR_SIZE;
	sp->vScrollBar.bar_y = y;
	sp->vScrollBar.bar_w = SCROLLBAR_SIZE;
	sp->vScrollBar.bar_h = height - SCROLLBAR_SIZE;
	sp->vScrollBar.scrollOffset = 0;
	double scrollDisplayRange = ((double)height / win->canv->screenHeight);
	sp->vScrollBar.scrollAmount = ceil(scrollDisplayRange);
	sp->vScrollBar.type = CHITRALEKHA_SCROLL_TYPE_VERTICAL;
	sp->vScrollBar.update = 1;
	sp->vScrollBar.thumb_posx = 2;
	sp->vScrollBar.thumb_posy = 1;
	sp->vScrollBar.thumb_width = sp->vScrollBar.bar_w - 4;
	sp->vScrollBar.thumb_height = 0;

	return sp;
}

/*
 * ChScrollUpdateVerticalScroll -- updates the vertical scroll bar thumb
 * @param sp -- Pointer to scrollpane
 * @param viewport -- viewport
 * @param contentsz -- content size
 */
void ChScrollUpdateVerticalScroll(ChScrollPane* sp, ChRect* viewport, int contentSz) {
	if (contentSz == 0) {
		sp->vScrollBar.thumb_height = 0;
		return;
	}

	if (contentSz < (viewport->y + viewport->h)) {
		sp->vScrollBar.thumb_height = 0;
		return;
	}

	double range = ((double)contentSz) / (double)viewport->h;

	//double range = ((viewport->h - viewport->y) / contentSz);
	sp->vScrollBar.scrollAmount = ceil(range);
	double viewableRatio = (double)viewport->h / (double)contentSz;
	double thumbHeight = ((double)viewport->h / (double)contentSz) *
						 sp->vScrollBar.bar_h; //(double)viewport->h * viewableRatio;
	sp->vScrollBar.thumb_height = thumbHeight;
	if (sp->vScrollBar.thumb_height <= 0) {
		sp->vScrollBar.thumb_height = 30;
	}
	sp->vScrollBar.contentSize = contentSz;
}

/*
 * ChScrollUpdateHorizontalScroll -- updates the horizontal scroll bar thumb
 * @param sp -- Pointer to scrollpane
 * @param viewport -- Pointer to viewport geometry
 * @param contentSz -- content size
 */
void ChScrollUpdateHorizontalScroll(ChScrollPane* sp, ChRect* viewport, int contentSz) {
	//sp->hScrollBar.thumb_width = viewport->x - viewport->w / contentSz;

	if (contentSz == 0) {
		sp->vScrollBar.thumb_height = 0;
		return;
	}

	if (contentSz < viewport->w) {
		sp->hScrollBar.thumb_width = 0;
		return;
	}

	double range = ((double)contentSz) / (double)viewport->w;

	//double range = ((viewport->h - viewport->y) / contentSz);
	sp->hScrollBar.scrollAmount = ceil(range);
	double viewableRatio = (double)viewport->w / (double)contentSz;
	double thumbWidth = ((double)viewport->w / (double)contentSz) *
						sp->hScrollBar.bar_w; //(double)viewport->h * viewableRatio;
	sp->hScrollBar.thumb_width = thumbWidth;
	if (sp->hScrollBar.thumb_width <= 0) {
		sp->hScrollBar.thumb_width = 30;
	}
	sp->hScrollBar.contentSize = contentSz;
}