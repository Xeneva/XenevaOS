/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2024, Manas Kamal Choudhury
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
#include "view.h"
#include <math.h>
#include "../draw.h"
#include "../color.h"
#include "icon.h"

/*
* ChDefaultListViewPainter -- default list view painter
*/
void ChDefaultListViewPainter(ChWidget* wid, ChWindow* win) {
	ChListView* lv = (ChListView*)wid;
	ChDrawRect(win->canv, lv->wid.x, lv->wid.y, lv->wid.w, lv->wid.h, WHITE);
	ChRect viewRect;
	viewRect.x = lv->wid.x;
	viewRect.y = lv->wid.y;
	viewRect.w = lv->wid.w;
	viewRect.h = lv->wid.h;
	ChFontSetSize(win->app->baseFont, 14);
	int nodePaddingHeight = LIST_VIEW_ITEM_HEIGHT;
	int max_visible_items =
		lv->currentStartIndex + lv->wid.h / LIST_VIEW_ITEM_HEIGHT + 2 * nodePaddingHeight;

	int ypos = -((int)lv->scrollpane->vScrollBar.scrollOffset %
				 LIST_VIEW_ITEM_HEIGHT); //(lv->wid.y + win->app->baseFont->fontHeight) +
	int xpos = (int)lv->scrollpane->hScrollBar.scrollOffset;
#define PADDING_Y 5

	int totalYpos = lv->wid.y + PADDING_Y + ypos;
	/*if ((lv->lastNodeIndex == lv->currentStartIndex))
		ypos  -= (int)lv->scrollpane->vScrollBar.scrollAmount;*/

	for (int i = lv->currentStartIndex; i < lv->itemList->pointer /*&& i < max_visible_items*/;
		 i++) {
		ChListItem* li = (ChListItem*)list_get_at(lv->itemList, i);
		int originalXpos = li->xPos;
		//li->xPos = originalXpos - xpos;
		if (li) {
			li->yPos = totalYpos;
			if (li->selected)
				ChDrawRectClipped(win->canv,
								  lv->wid.x + li->xPos,
								  li->yPos,
								  li->width,
								  li->height - 2,
								  &viewRect,
								  0xFF6982B7);

			if (li->icon) {
				ChDrawIconClipped(
					win->canv, li->icon, lv->wid.x + li->xPos + 10, li->yPos, &viewRect);
				ChFontDrawTextClipped(win->canv,
									  win->app->baseFont,
									  li->itemText,
									  lv->wid.x + li->xPos + 40,
									  totalYpos + (li->height / 2) + 5,
									  BLACK,
									  &viewRect); //
			} else
				ChFontDrawTextClipped(win->canv,
									  win->app->baseFont,
									  li->itemText,
									  lv->wid.x + li->xPos + 10,
									  totalYpos,
									  BLACK,
									  &viewRect); //
			totalYpos += LIST_VIEW_ITEM_HEIGHT;
		}
	}
}