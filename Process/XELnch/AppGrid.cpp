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

#include "appgrid.h"
#include <chitralekha.h>
#include "button.h"

/* LauncherPaintAppGrid -- default paint handler for
 * grid widget
 */
void LauncherPaintAppGrid(AppGrid* grid, ChWindow* win) {
	if (grid->show_search) {
		ChDrawRect(win->canv, grid->x, grid->y, grid->w, grid->h, 0x1A48494B);
		for (int i = 0; i < grid->searchResultList->pointer; i++) {
			LaunchButton* button = (LaunchButton*)list_get_at(grid->searchResultList, i);
			if (button) {
				if (button->drawLaunchButton)
					button->drawLaunchButton(button, win);
			}
		}
	} else {
		ChDrawRect(win->canv, grid->x, grid->y, grid->w, grid->h, 0x1A48494B);
		for (int i = 0; i < grid->lbbuttonlist->pointer; i++) {
			LaunchButton* button = (LaunchButton*)list_get_at(grid->lbbuttonlist, i);
			if (button->drawLaunchButton && (button->page_number == grid->activePageNumber))
				button->drawLaunchButton(button, win);
		}
	}
	//ChDrawRectUnfilled(win->canv, grid->x, grid->y, grid->w, grid->h, GREEN);
}

/*
 * LauncherCreateAppGrid -- creates app grid
 * @param x -- x position of the grid
 * @param y -- y position of the grid
 * @param w -- width of the grid
 * @param h -- height of the grid
 */
AppGrid* LauncherCreateAppGrid(int x, int y, int w, int h) {
	AppGrid* grid = (AppGrid*)malloc(sizeof(AppGrid));
	memset(grid, 0, sizeof(AppGrid));
	grid->start_pos_x = x + BUTTONS_PAD_X;
	grid->start_pos_y = y + ROWS_PAD_Y;
	grid->numEntriesInEachRow = ENTRIES_PER_ROW;
	grid->numPageCount = 1;
	grid->numRowsInOnePage = ROWS_PER_PAGE;
	grid->PaintAppGrid = LauncherPaintAppGrid;
	grid->x = x;
	grid->y = y;
	grid->w = w;
	grid->h = h;
	grid->lbbuttonlist = initialize_list();
	grid->searchResultList = initialize_list();
	grid->show_search = false;
	grid->search_pos_x = grid->start_pos_x;
	grid->search_pos_y = grid->start_pos_y;
	grid->activePageNumber = 1;
	for (int i = 0; i < MAX_PAGE_COUNT; i++) {
		grid->page[i].start_x = grid->start_pos_x;
		grid->page[i].start_y = grid->start_pos_y;
		grid->page[i].pageNumber = i + 1;
	}
	return grid;
}

void AppGridAddButtonToPage(AppGrid* grid, LaunchButton* button) {
	button->x = grid->page[button->page_number].start_x;
	button->y = grid->page[button->page_number].start_y;

	if ((button->x + button->w) >= (grid->x + grid->w)) {
		grid->page[button->page_number].start_x = grid->x + BUTTONS_PAD_X;
		grid->page[button->page_number].start_y += button->h + ROWS_PAD_Y;
		button->x = grid->page[button->page_number].start_x;
		button->y = grid->page[button->page_number].start_y;
	}

	if ((button->y + button->h) >= (grid->y + grid->h)) {
		button->page_number += 1;
		AppGridAddButtonToPage(grid, button);
	}
	grid->page[button->page_number].start_x += button->w + BUTTONS_PAD_X;
	grid->page[button->page_number].hasItem = 1;
	button->scratch_x = button->x;
	button->scratch_y = button->y;
}

/*
 * AppGridAddButton -- adds a button to specific grid
 * @param grid -- Pointer to grid
 * @param button -- Pointer to launch button needs to 
 * be added
 */
void AppGridAddButton(AppGrid* grid, LaunchButton* button) {
	button->x = grid->start_pos_x;
	button->y = grid->start_pos_y;
	if ((button->x + button->w) >= (grid->x + grid->w)) {
		grid->start_pos_x = grid->x + BUTTONS_PAD_X;
		grid->start_pos_y += button->h + ROWS_PAD_Y;
		button->x = grid->start_pos_x;
		button->y = grid->start_pos_y;
	}

	if ((button->y + button->h) >= (grid->y + grid->h)) {
		button->page_number += 1;
		AppGridAddButtonToPage(grid, button);
	}
	grid->start_pos_x += button->w + BUTTONS_PAD_X;
	button->scratch_x = button->x;
	button->scratch_y = button->y;
	list_add(grid->lbbuttonlist, button);
}

/*
 * AppGridAddButtonInSearch -- adds a button to specific grid
 * @param grid -- Pointer to grid
 * @param button -- Pointer to launch button needs to
 * be added
 */
void AppGridAddButtonInSearch(AppGrid* grid, LaunchButton* button) {
	button->x = grid->search_pos_x;
	button->y = grid->search_pos_y;
	if ((button->x + button->w) >= (grid->x + grid->w)) {
		grid->search_pos_x = grid->x + BUTTONS_PAD_X;
		grid->search_pos_y += button->h + ROWS_PAD_Y;
		button->x = grid->search_pos_x;
		button->y = grid->search_pos_y;
	}
	grid->search_pos_x += button->w + BUTTONS_PAD_X;
	list_add(grid->searchResultList, button);
}

/**
 * @brief AppGridSearchReset -- reset the search list
 * @param grid -- Pointer to app grid
 */
void AppGridSearchReset(AppGrid* grid) {
	for (int i = 0; i < grid->searchResultList->pointer; i++) {
		LaunchButton* lb = (LaunchButton*)list_remove(grid->searchResultList, i);
		//lb->x = lb->scratch_x;
		//lb->y = lb->scratch_y;
	}

	for (int i = 0; i < grid->lbbuttonlist->pointer; i++) {
		LaunchButton* lb = (LaunchButton*)list_get_at(grid->lbbuttonlist, i);
		lb->x = lb->scratch_x;
		lb->y = lb->scratch_y;
	}
	grid->search_pos_x = grid->x + BUTTONS_PAD_X;
	grid->search_pos_y = grid->y + ROWS_PAD_Y;
}

/*
 *AppGridPaint -- paints the entire grid
 * @param grid -- Pointer to grid to be painted
 * @param win -- Pointer to root window
 */
void AppGridPaint(AppGrid* grid, ChWindow* win) {
	if (grid->PaintAppGrid)
		grid->PaintAppGrid(grid, win);
}

int AppGridGetTotalNumberOfPage(AppGrid* grid) {
	int numPage = 1;
	for (int i = 0; i < 8; i++) {
		if (grid->page[i].hasItem)
			numPage += 1;
	}
	return numPage;
}