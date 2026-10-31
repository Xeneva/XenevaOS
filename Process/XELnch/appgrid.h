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

#ifndef __APP_GRID_H__
#define __APP_GRID_H__

#include <widgets/list.h>
#include <stdint.h>
#include <chitralekha.h>
#include <widgets/window.h>
#include "button.h"

#define ENTRIES_PER_ROW 8
#define ROWS_PER_PAGE	4
#define ROWS_PAD_Y		20
#define BUTTONS_PAD_X	20

#define APP_GRID_DEFAULT_WIDTH	500
#define APP_GRID_DEFAULT_HEIGHT 500

typedef struct _page_ {
	int start_x;
	int start_y;
	int pageNumber;
	bool hasItem;
} Page;

#define MAX_PAGE_COUNT 8

typedef struct _app_grid_ {
	list_t* lbbuttonlist;
	int start_pos_x;
	int start_pos_y;
	/* it's own boundary*/
	int x;
	int y;
	int w;
	int h;
	/* number of pages for
	 * each application entry */
	int numPageCount;
	int numEntriesInEachRow;
	int numRowsInOnePage;
	int activePageNumber;
	bool show_search_result;
	int search_pos_x;
	int search_pos_y;
	Page page[MAX_PAGE_COUNT];
	list_t* searchResultList;
	bool show_search;
	void (*PaintAppGrid)(_app_grid_* grid, ChWindow* win);
} AppGrid;

/*
* LauncherCreateAppGrid -- creates app grid
* @param x -- x position of the grid
* @param y -- y position of the grid
* @param w -- width of the grid
* @param h -- height of the grid
*/
extern AppGrid* LauncherCreateAppGrid(int x, int y, int w, int h);

/*
* AppGridAddButton -- adds a button to specific grid
* @param grid -- Pointer to grid
* @param button -- Pointer to launch button needs to
* be added
*/
extern void AppGridAddButton(AppGrid* grid, LaunchButton* button);

/*
 * AppGridAddButtonInSearch -- adds a button to specific grid
 * @param grid -- Pointer to grid
 * @param button -- Pointer to launch button needs to
 * be added
 */
extern void AppGridAddButtonInSearch(AppGrid* grid, LaunchButton* button);

/*
*AppGridPaint -- paints the entire grid
* @param grid -- Pointer to grid to be painted
* @param win -- Pointer to root window
*/
extern void AppGridPaint(AppGrid* grid, ChWindow* win);

/**
 * @brief AppGridSearchReset -- reset the search list
 * @param grid -- Pointer to app grid
 */
extern void AppGridSearchReset(AppGrid* grid);

extern int AppGridGetTotalNumberOfPage(AppGrid* grid);
#endif