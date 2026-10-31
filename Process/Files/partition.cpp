/**
* BSD 2-Clause License
*
* Copyright (c) 2024, Manas Kamal Choudhury
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

#include <stdint.h>
#include <_xeneva.h>
#include <stdio.h>
#include <sys/_keproc.h>
#include <sys/_kefile.h>
#include <sys/iocodes.h>
#include <chitralekha.h>
#include <widgets/base.h>
#include <widgets/button.h>
#include <widgets/scrollpane.h>
#include <widgets/slider.h>
#include <widgets/progress.h>
#include <keycode.h>
#include <widgets/window.h>
#include <widgets/msgbox.h>
#include <widgets/view.h>
#include <string.h>
#include <widgets/menubar.h>
#include <stdlib.h>
#include <time.h>
#include "partition.h"
#include <widgets/menu.h>

#define PARTITION_LIST_BUTTON_HEIGHT 55

void FileManagerPartitionButtonPaintHandler(ChWidget* wid, ChWindow* win) {
	FileManagerPartitionButton* pbut = (FileManagerPartitionButton*)wid;
	int text_x = 2;
	uint32_t default_text_col = BLACK;
	uint32_t mounted_text_col = LIGHTBLACK;
	if (wid->hover) {
		default_text_col = WHITE;
		mounted_text_col = WHITE;
		ChColorDrawVerticalGradient(
			win->canv, wid->x, wid->y, wid->w, wid->h, 0xFF6E8BD9, 0xFF3561D6);
	} else {
		ChDrawRect(win->canv, wid->x, wid->y, wid->w, wid->h, 0xFFFFFFFF);
	}

	if (pbut->icon) {
		ChDrawIcon(win->canv,
				   pbut->icon,
				   pbut->wid.x + 2,
				   (pbut->wid.y + (pbut->wid.h - pbut->icon->image.height) / 2));
		text_x += pbut->icon->image.width + 10;
	}
	ChFontSetSize(win->app->baseFont, 12);
	ChRect limit;
	limit.x = wid->x;
	limit.y = wid->y;
	limit.w = wid->w;
	limit.h = wid->h;
	ChFontDrawTextClipped(win->canv,
						  win->app->baseFont,
						  pbut->partitionName,
						  text_x,
						  pbut->wid.y + ((pbut->wid.h - 5) / 2),
						  default_text_col,
						  &limit);
	ChFontSetSize(win->app->baseFont, 10);
	int last_text_length = 0;
	if (pbut->mounted) {
		ChFontDrawTextClipped(win->canv,
							  win->app->baseFont,
							  "Location: ",
							  text_x + 2,
							  pbut->wid.y + ((pbut->wid.h + 25) / 2),
							  mounted_text_col,
							  &limit);
		last_text_length = ChFontGetWidth(win->app->baseFont, "Location: ") + 2;
	}
	ChFontDrawTextClipped(win->canv,
						  win->app->baseFont,
						  pbut->guidString,
						  text_x + 2 + last_text_length,
						  pbut->wid.y + ((pbut->wid.h + 25) / 2),
						  default_text_col,
						  &limit);
}

#define PARTITION_VIEW_COLOR 0xFF808080
/*
 * FileManagerPaintHandler -- paints the file manager
 * partition list
 */
void FileManagerPartitionListPaintHandler(ChWidget* wid, ChWindow* win) {
	FileManagerPartitionList* part = (FileManagerPartitionList*)wid;
	ChDrawRect(win->canv, wid->x, wid->y, wid->w, wid->h, PARTITION_VIEW_COLOR);
	for (int i = 0; i < part->partitionButtons->pointer; i++) {
		FileManagerPartitionButton* pbut =
			(FileManagerPartitionButton*)list_get_at(part->partitionButtons, i);
		if (pbut->wid.ChPaintHandler)
			pbut->wid.ChPaintHandler((ChWidget*)pbut, win);
	}
}

void FileManagerPartitionListMouseEvent(ChWidget* wid, ChWindow* win, int x, int y, int button) {
	FileManagerPartitionList* plist = (FileManagerPartitionList*)wid;
	for (int i = 0; i < plist->partitionButtons->pointer; i++) {
		FileManagerPartitionButton* pbut =
			(FileManagerPartitionButton*)list_get_at(plist->partitionButtons, i);
		if ((x >= (win->info->x + pbut->wid.x) && x < (win->info->x + pbut->wid.x + pbut->wid.w)) &&
			(y >= (win->info->y + pbut->wid.y) && y < (win->info->y + pbut->wid.y + pbut->wid.h))) {
			pbut->wid.hover = true;
			if (pbut->wid.ChPaintHandler)
				pbut->wid.ChPaintHandler((ChWidget*)pbut, win);
			ChWindowUpdate(win, wid->x, wid->y, wid->w, wid->h, 0, 1);
		} else {
			if (pbut->wid.hover) {
				pbut->wid.hover = false;
				if (pbut->wid.ChPaintHandler)
					pbut->wid.ChPaintHandler((ChWidget*)pbut, win);
				ChWindowUpdate(win, wid->x, wid->y, wid->w, wid->h, 0, 1);
			}
		}
	}
}

/*
 * FileManagerCreatePartitionList -- create a new partition list widget
 * for file manager
 * @param x -- X location
 * @param y -- Y location
 * @param w -- Width of the widget
 * @param h -- Height of the widget
 */
FileManagerPartitionList* FileManagerCreatePartitionList(int x, int y, int w, int h) {
	FileManagerPartitionList* list =
		(FileManagerPartitionList*)malloc(sizeof(FileManagerPartitionList));
	memset(list, 0, sizeof(FileManagerPartitionList));
	list->wid.x = x;
	list->wid.y = y;
	list->wid.w = w;
	list->wid.h = h;
	list->wid.ChPaintHandler = FileManagerPartitionListPaintHandler;
	list->wid.ChMouseEvent = FileManagerPartitionListMouseEvent;
	list->partitionButtons = initialize_list();
	list->button_pos_y = 5;
	list->button_pos_x = 0;
	return list;
}

FileManagerPartitionButton*
FileManageCreatePartitionButton(FileManagerPartitionList* partitionList) {
	FileManagerPartitionButton* pbut =
		(FileManagerPartitionButton*)malloc(sizeof(FileManagerPartitionButton));
	pbut->wid.x = partitionList->wid.x + partitionList->button_pos_x;
	pbut->wid.y = partitionList->wid.y + partitionList->button_pos_y;
	pbut->wid.w = partitionList->wid.w - partitionList->button_pos_x * 2;
	pbut->wid.h = PARTITION_LIST_BUTTON_HEIGHT;
	pbut->wid.ChPaintHandler = FileManagerPartitionButtonPaintHandler;
	partitionList->button_pos_y += pbut->wid.h + 5;
	list_add(partitionList->partitionButtons, pbut);
	return pbut;
}