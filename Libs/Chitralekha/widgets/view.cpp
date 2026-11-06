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

#include "view.h"
#include "scrollpane.h"
#include <math.h>
#include "sys/_keproc.h"

extern void ChDefaultListViewPainter(ChWidget* wid, ChWindow* win);

void ChListViewMouseEvent(ChWidget* widget, ChWindow* win, int x, int y, int button) {
	ChListView* lv = (ChListView*)widget;
	int display_idx =
		((lv->scrollpane->vScrollBar.thumb_posy * lv->scrollpane->vScrollBar.scrollAmount)) /
		LIST_VIEW_ITEM_HEIGHT;
	//display_idx *= lv->scrollpane->vScrollBar.scrollAmount;
	bool _view_update = false;
	/* this messages applies only to vertical scrollbar*/
	if (button == DEODHAI_MOUSE_MSG_SCROLL_UP) {
		if (lv->scrollpane) {
			lv->scrollpane->vScrollBar.thumb_posy -= lv->scrollpane->vScrollBar.scrollAmount;
			lv->scrollpane->vScrollBar.update = 1;
			lv->currentStartIndex = display_idx;
			if (lv->scrollpane->wid.ChPaintHandler)
				lv->scrollpane->wid.ChPaintHandler((ChWidget*)lv->scrollpane, win);
			_view_update = true;
#ifdef ARCH_X64
			_KeProcessSleep(100);
#elif ARCH_ARM64
			_KeProcessSleep(1);
#endif
		}
	}

	if (button == DEODHAI_MOUSE_MSG_SCROLL_DOWN) {
		if (lv->scrollpane) {
			lv->scrollpane->vScrollBar.thumb_posy += lv->scrollpane->vScrollBar.scrollAmount;
			//_KePrint("ListView Scroll down msg display idx -> %d \r\n", display_idx);
			lv->currentStartIndex = display_idx;
			lv->scrollpane->vScrollBar.update = 1;
			if (lv->scrollpane->wid.ChPaintHandler)
				lv->scrollpane->wid.ChPaintHandler((ChWidget*)lv->scrollpane, win);
			_view_update = true;
#ifdef ARCH_X64
			_KeProcessSleep(100);
#elif ARCH_ARM64
			_KeProcessSleep(1);
#endif
		}
	}

	if (button &&
		((button != DEODHAI_MOUSE_MSG_SCROLL_UP) && (button != DEODHAI_MOUSE_MSG_SCROLL_DOWN))) {
		for (int i = 0; i < lv->itemList->pointer; i++) {
			ChListItem* li = (ChListItem*)list_get_at(lv->itemList, i);
			li->selected = false;
			if (x >= (win->info->x + li->xPos) && x < (win->info->x + li->xPos + li->width) &&
				y >= (win->info->y + li->yPos) && y < (win->info->y + li->yPos + li->height)) {
				if (button == DEODHAI_MESSAGE_MOUSE_DBLCLK) {
					if (li->ChListItemAction)
						li->ChListItemAction(lv, li);
				}
				lv->selectedItem = li;
				li->selected = true;
				_view_update = true;
			}
		}
	}

	if (_view_update) {
		if (lv->wid.ChPaintHandler)
			lv->wid.ChPaintHandler((ChWidget*)lv, win);
		int update_w = lv->wid.w + lv->scrollpane->vScrollBar.bar_w;
		int update_h = lv->wid.h + lv->scrollpane->vScrollBar.bar_h;
		ChWindowUpdate(win, lv->wid.x, lv->wid.y, lv->wid.w, lv->wid.h, 0, 1);
	}
}

void ChListViewScrollEvent(ChWidget* wid, ChWindow* win, int scollVal, uint8_t type) {
	ChListView* lv = (ChListView*)wid;

	int display_idx = lv->scrollpane->vScrollBar.scrollOffset / LIST_VIEW_ITEM_HEIGHT;

	bool _view_update = false;
	if (type == CHITRALEKHA_SCROLL_TYPE_VERTICAL) {
		lv->currentStartIndex = display_idx;
		//lv->horizontalRenderY = (lv->wid.y - ((lv->wid.y + lv->scrollpane->vScrollBar.thumb_posy) - lv->wid.y)) + win->app->baseFont->fontHeight; //LIST_VIEW_ITEM_HEIGHT;
		_view_update = true;
	}

	if (type == CHITRALEKHA_SCROLL_TYPE_HORIZONTAL) {
		lv->currentStartIndex = display_idx;
		_view_update = true;
	}

	if (_view_update) {
		if (lv->wid.ChPaintHandler)
			lv->wid.ChPaintHandler((ChWidget*)lv, win);
		int update_w = lv->wid.w; // +lv->scrollpane->vScrollBar.bar_w;
		int update_h = lv->wid.h; // +lv->scrollpane->hScrollBar.bar_h;
		ChWindowUpdate(win, lv->wid.x, lv->wid.y, update_w, update_h, 0, 1);
		lv->lastNodeIndex = lv->currentStartIndex;
	}
}

/*
 * ChListViewDestroy -- destroy callback for list view widget
 * @param wid -- Pointer to list view widget
 * @param win -- Pointer to main window
 */
void ChListViewDestroy(ChWidget* wid, ChWindow* win) {
	ChListView* lv = (ChListView*)wid;
	for (int i = 0; i < lv->itemList->pointer; i++) {
		ChListItem* li = (ChListItem*)list_remove(lv->itemList, i);
		free(li->itemText);
		free(li);
	}
	free(lv);
	_KePrint("ListView destroyed \r\n");
}
/*
 * ChCreateListView -- create a new list view widget
 * @param x -- x location
 * @param y -- y location
 * @param w -- width of the list
 * @param h -- height of the list
 */
ChListView* ChCreateListView(int x, int y, int w, int h) {
	ChListView* lv = (ChListView*)malloc(sizeof(ChListView));
	memset(lv, 0, sizeof(ChListView));
	lv->wid.x = x;
	lv->wid.y = y;
	lv->wid.w = w - SCROLLBAR_SIZE;
	lv->wid.h = h - SCROLLBAR_SIZE;
	lv->itemList = initialize_list();
	//lv->horizontalRenderY = (lv->wid.y - LIST_VIEW_ITEM_HEIGHT) + 21;
	lv->currentStartIndex = 0; //
	lv->lastNodeIndex = lv->currentStartIndex;
	lv->wid.ChPaintHandler = ChDefaultListViewPainter;
	lv->wid.ChMouseEvent = ChListViewMouseEvent;
	lv->wid.ChScrollEvent = ChListViewScrollEvent;
	lv->wid.ChDestroy = ChListViewDestroy;
	lv->firstItemPlaced = false;

	return lv;
}

/*
 * ChListViewSetScrollpane -- assigns a scrollpane to given list view
 * @param view -- Pointer to list view
 * @param pane -- Pointer to scroll pane
 */
void ChListViewSetScrollpane(ChListView* view, ChScrollPane* pane) {
	pane->hScrollBar.bar_x = view->wid.x;
	pane->hScrollBar.bar_y = view->wid.y + view->wid.h;
	pane->hScrollBar.bar_w = view->wid.w;
	pane->hScrollBar.bar_h = SCROLLBAR_SIZE;
	pane->vScrollBar.bar_x = view->wid.x + view->wid.w;
	pane->vScrollBar.bar_y = view->wid.y;
	pane->vScrollBar.bar_w = SCROLLBAR_SIZE;
	pane->vScrollBar.bar_h = view->wid.h;
	view->currentStartIndex = 0;
	view->scrollpane = pane;
	pane->scrollableView = (ChWidget*)view;
}

/*
 * ChListViewAddItem -- add list item to list view
 * @param lv -- Pointer to list view
 * @param itemText -- item text
 */
ChListItem* ChListViewAddItem(ChWindow* win, ChListView* lv, char* itemText) {
	ChListItem* li = (ChListItem*)malloc(sizeof(ChListItem));
	memset(li, 0, sizeof(ChListItem));
	li->itemText = (char*)malloc(strlen(itemText) + 1);
	strcpy(li->itemText, itemText);
	int xpos = 0;
	int ypos = 0;
	for (int i = 0; i < lv->itemList->pointer; i++) {
		ChListItem* item = (ChListItem*)list_get_at(lv->itemList, i);
		ypos = item->yPos + LIST_VIEW_ITEM_HEIGHT + LIST_VIEW_ITEM_YPADDING;
	}
	li->xPos = xpos;
	li->yPos = ypos;
	int width = lv->wid.w;
	int height = LIST_VIEW_ITEM_HEIGHT;
	li->width = width;
	li->height = height;
	li->selected = false;
	list_add(lv->itemList, li);
	lv->numRows += 1;
	lv->horizontalRenderY = (lv->wid.y - LIST_VIEW_ITEM_HEIGHT) + win->app->baseFont->fontHeight;
	ChRect view;
	view.x = lv->wid.x;
	view.y = lv->wid.y;
	view.w = lv->wid.w;
	view.h = lv->wid.h;
	if ((lv->numRows * LIST_VIEW_ITEM_HEIGHT) >= lv->wid.h) {
		if (lv->scrollpane)
			ChScrollUpdateVerticalScroll(
				lv->scrollpane,
				&view,
				(lv->numRows * LIST_VIEW_ITEM_HEIGHT + LIST_VIEW_ITEM_YPADDING));
	}
	if (width > lv->wid.w) {
		ChScrollUpdateHorizontalScroll(lv->scrollpane, &view, width);
	}
	lv->contentSizeY = (lv->numRows * LIST_VIEW_ITEM_HEIGHT + LIST_VIEW_ITEM_YPADDING);
	/*lv->currentStartIndex = floor((lv->scrollpane->vScrollBar.thumb_posy / LIST_VIEW_ITEM_HEIGHT) - LIST_VIEW_ITEM_HEIGHT);
	lv->currentStartIndex = max(0, lv->currentStartIndex);*/
	return li;
}

/*
 * ChListViewSetListItemIcon -- sets icon for list items
 * @param li -- Pointer to list item
 * @param icon -- Pointer to icon structure
 */
void ChListViewSetListItemIcon(ChListItem* li, ChIcon* icon) {
	if (!li)
		return;

	if (!icon)
		return;
	li->icon = icon;
}

/*
 * ChListViewClear -- clears all list view items
 * @param lv -- Pointer to list view
 */
void ChListViewClear(ChListView* lv) {
	for (int i = 0; i < lv->itemList->pointer; i++) {
		ChListItem* li = (ChListItem*)list_remove(lv->itemList, i);
		free(li->itemText);
		free(li);
	}
	memset(lv->itemList, 0, sizeof(list_t));
	lv->numRows = 0;
	lv->currentStartIndex = 0;
	lv->selectedItem = 0;
	ChRect rect;
	rect.x = lv->wid.x;
	rect.y = lv->wid.y;
	rect.w = lv->wid.w;
	rect.h = lv->wid.h;
	ChScrollUpdateVerticalScroll(lv->scrollpane, &rect, (lv->numRows * LIST_VIEW_ITEM_HEIGHT));
}

/*
 * ChListViewRepaint -- repaint the entire list view
 * @param win -- Pointer to main window
 * @param lv -- Pointer to list view
 */
void ChListViewRepaint(ChWindow* win, ChListView* lv) {
	if (lv->scrollpane->wid.ChPaintHandler)
		lv->scrollpane->wid.ChPaintHandler((ChWidget*)lv->scrollpane, win);
	ChWindowUpdate(win,
				   lv->scrollpane->vScrollBar.bar_x,
				   lv->scrollpane->vScrollBar.bar_y,
				   lv->scrollpane->vScrollBar.bar_w,
				   lv->scrollpane->vScrollBar.bar_h,
				   0,
				   1);
#ifdef ARCH_X64
	_KeProcessSleep(30);
#endif
	ChWindowUpdate(win,
				   lv->scrollpane->hScrollBar.bar_x,
				   lv->scrollpane->hScrollBar.bar_y,
				   lv->scrollpane->hScrollBar.bar_w,
				   lv->scrollpane->hScrollBar.bar_h,
				   0,
				   1);
#ifdef ARCH_X64
	_KeProcessSleep(30);
#endif
	if (lv->wid.ChPaintHandler)
		lv->wid.ChPaintHandler((ChWidget*)lv, win);
	ChWindowUpdate(win, lv->wid.x, lv->wid.y, lv->wid.w, lv->wid.h, 0, 1);
}

/*
 * ChListViewGetSelectedItem -- returns the current selected item
 * @param lv -- Pointer to ChListView
 */
ChListItem* ChListViewGetSelectedItem(ChListView* lv) {
	if (lv->selectedItem)
		_KePrint("[ListView]: Maximum selected item-> %s \r\n", lv->selectedItem->itemText);
	return lv->selectedItem;
}
