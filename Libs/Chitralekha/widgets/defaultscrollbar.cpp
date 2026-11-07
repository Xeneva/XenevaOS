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
#include "../draw.h"
#include "../color.h"

#define SCROLLBAR_BACKGROUND_COLOR 0xFF3D3A3A
#define SCROLLTHUMB_COLOR_DARK	   0xFF425C95
#define SCROLLTHUMB_COLOR_LIGHT	   0xFF6982B7
#define SCROLLTHUMB_HOVER_LIGHT	   0xFF879CCB
#define SCROLLTHUMB_HOVER_DARK	   0xFF718DCA
/*
 * ChDefaultScrollPanePainter -- default scroll pane painter
 */
void ChDefaultScrollPanePainter(ChWidget* wid, ChWindow* win) {
	ChScrollPane* sp = (ChScrollPane*)wid;

	if (sp->hScrollBar.update) {
		/* draw the horizontal scroll bar first*/
		ChDrawRect(win->canv,
				   sp->hScrollBar.bar_x,
				   sp->hScrollBar.bar_y,
				   sp->hScrollBar.bar_w,
				   sp->hScrollBar.bar_h,
				   SCROLLBAR_BACKGROUND_COLOR);

		/* thumb */
		if (sp->hScrollBar.thumbHover) {
			ChColorDrawVerticalGradient(win->canv,
										sp->hScrollBar.bar_x + sp->hScrollBar.thumb_posx,
										sp->hScrollBar.bar_y + sp->hScrollBar.thumb_posy,
										sp->hScrollBar.thumb_width,
										sp->hScrollBar.thumb_height,
										SCROLLTHUMB_HOVER_LIGHT,
										SCROLLTHUMB_HOVER_DARK);
		} else {
			ChColorDrawVerticalGradient(win->canv,
										sp->hScrollBar.bar_x + sp->hScrollBar.thumb_posx,
										sp->hScrollBar.bar_y + sp->hScrollBar.thumb_posy,
										sp->hScrollBar.thumb_width,
										sp->hScrollBar.thumb_height,
										SCROLLTHUMB_COLOR_LIGHT,
										SCROLLTHUMB_COLOR_DARK);
		}
		sp->hScrollBar.update = false;
	}

	if (sp->vScrollBar.update) {
		/* draw the vertical scroll bar*/
		ChDrawRect(win->canv,
				   sp->vScrollBar.bar_x,
				   sp->vScrollBar.bar_y,
				   sp->vScrollBar.bar_w,
				   sp->vScrollBar.bar_h,
				   SCROLLBAR_BACKGROUND_COLOR);

		/* thumb */
		if (sp->vScrollBar.thumbHover) {
			ChColorDrawHorizontalGradient(win->canv,
										  sp->vScrollBar.bar_x + sp->vScrollBar.thumb_posx,
										  sp->vScrollBar.bar_y + sp->vScrollBar.thumb_posy,
										  sp->vScrollBar.thumb_width,
										  sp->vScrollBar.thumb_height,
										  SCROLLTHUMB_HOVER_LIGHT,
										  SCROLLTHUMB_HOVER_DARK);
		} else {
			ChColorDrawHorizontalGradient(win->canv,
										  sp->vScrollBar.bar_x + sp->vScrollBar.thumb_posx,
										  sp->vScrollBar.bar_y + sp->vScrollBar.thumb_posy,
										  sp->vScrollBar.thumb_width,
										  sp->vScrollBar.thumb_height,
										  SCROLLTHUMB_COLOR_LIGHT,
										  SCROLLTHUMB_COLOR_DARK);
		}
		sp->vScrollBar.update = false;
	}
}