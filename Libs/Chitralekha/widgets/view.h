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

#ifndef __VIEW_H__
#define __VIEW_H__

#include <stdint.h>
#include "list.h"
#include "window.h"
#include "scrollpane.h"
#include "icon.h"

#define LIST_VIEW_ITEM_HEIGHT	24 //30
#define LIST_VIEW_ITEM_YPADDING 2

typedef struct _list_item_ ChListItem;

typedef struct _list_view_ {
	ChWidget wid;
	list_t* itemList;
	bool scrollable;
	ChScrollPane* scrollpane;
	int numRows;
	int horizontalRenderY;
	int horizontalRenderX;
	int currentStartIndex;
	int lastNodeIndex;
	int lastNodePosY;
	bool firstItemPlaced;
	ChListItem* selectedItem;
	int contentSizeY;
	int contentSizeX;
} ChListView;

typedef struct _list_item_ {
	//Geometry
	int xPos;
	int yPos;
	bool selected;
	int width;
	int height;
	//item string, icon
	ChIcon* icon;
	char* itemText;
	void (*ChListItemAction)(ChListView* lv, _list_item_* li);
} ChListItem;

/*
* ChCreateListView -- create a new list view widget
* @param x -- x location
* @param y -- y location
* @param w -- width of the list
* @param h -- height of the list
*/
XE_EXTERN XE_LIB ChListView* ChCreateListView(int x, int y, int w, int h);

/*
* ChListViewSetScrollpane -- assigns a scrollpane to given list view
* @param view -- Pointer to list view
* @param pane -- Pointer to scroll pane
*/
XE_EXTERN XE_LIB void ChListViewSetScrollpane(ChListView* view, ChScrollPane* pane);

/*
* ChListViewAddItem -- add list item to list view
* @param lv -- Pointer to list view
* @param itemText -- item text
*/
XE_EXTERN XE_LIB ChListItem* ChListViewAddItem(ChWindow* win, ChListView* lv, char* itemText);

/*
* ChListViewSetListItemIcon -- sets icon for list items
* @param li -- Pointer to list item
* @param icon -- Pointer to icon structure
*/
XE_EXTERN XE_LIB void ChListViewSetListItemIcon(ChListItem* li, ChIcon* icon);

/*
* ChListViewClear -- clears all list view items
* @param lv -- Pointer to list view
*/
XE_EXTERN XE_LIB void ChListViewClear(ChListView* lv);

/*
* ChListViewRepaint -- repaint the entire list view
* @param win -- Pointer to main window
* @param lv -- Pointer to list view
*/
XE_EXTERN XE_LIB void ChListViewRepaint(ChWindow* win, ChListView* lv);

/*
* ChListViewGetSelectedItem -- returns the current selected item
* @param lv -- Pointer to ChListView
*/
XE_EXTERN XE_LIB ChListItem* ChListViewGetSelectedItem(ChListView* lv);

#endif