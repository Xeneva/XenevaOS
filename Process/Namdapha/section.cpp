/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2025, Manas Kamal Choudhury
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

#include "nmdapha.h"
#include "section.h"

int section_start_x;
int section_start_y;
list_t* sectionsList;
int section_default_h;

void SectionPaint(NamdaphaSections* sect, ChWindow* win);

/*
 * SectionModuleInitialize -- initialize the 
 * section module
 */
void SectionModuleInitialize(ChWindow* namdaphaWin) {
	section_start_x = 0;
	section_start_y = 10;
	sectionsList = initialize_list();
	section_default_h = (namdaphaWin->info->height - (60 + NAMDAPHA_BUTTON_YPAD));
}
NamdaphaSections* CreateSection(char* title) {
	NamdaphaSections* sect = (NamdaphaSections*)malloc(sizeof(NamdaphaSections));
	memset(sect, 0, sizeof(NamdaphaSections));
	sect->x = section_start_x;
	sect->y = section_start_y;
	sect->width = NAMDAPHA_WIDTH;
	sect->height = section_default_h;
	sect->title = (char*)malloc(strlen(title) + 1);
	memset(sect->title, 0, strlen(title) + 1);
	strcpy(sect->title, title);
	sect->buttonList = initialize_list();
	sect->paint = SectionPaint;
	list_add(sectionsList, sect);
	return sect;
}

void SectionPaint(NamdaphaSections* sect, ChWindow* win) {
	ChDrawRect(win->canv, sect->x, sect->y, sect->width, sect->height, WHITE);
}
/*
 * SectionsDraw -- draw all sections
 */
void SectionsDraw(ChWindow* win) {
	for (int i = 0; i < sectionsList->pointer; i++) {
		NamdaphaSections* sect = (NamdaphaSections*)list_get_at(sectionsList, i);
		if (sect->paint)
			sect->paint(sect, win);
	}
	ChWindowUpdate(win, win->info->x, win->info->y, win->info->width, win->info->height, 1, 0);
}
/*
 * SectionRedraw -- draw only given section
 * @param sect -- Section to draw
 */
void SectionRedraw(NamdaphaSections* sect, ChWindow* win) {
	if (!sect)
		SectionsDraw(win);
	else {
		sect->paint(sect, win);
		ChWindowUpdate(win, sect->x, sect->y, sect->width, sect->height, 0, 1);
	}
}

list_t* SectionsGetList() {
	return sectionsList;
}
