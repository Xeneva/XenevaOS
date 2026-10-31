/**
* @file search.cpp
*
* BSD 2-Clause License
*
* Copyright (c) 2022-2026, Manas Kamal Choudhury
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
#include "button.h"
#include "launcher.h"
#include <string.h>
#include <ctype.h>

typedef struct _button_match_ {
	LaunchButton* lb;
	int score;
} ButtonMatch;

extern AppGrid* xelaunch_get_app_grid();

void _app_title_to_lower(char* dest, const char* src) {
	while (*src) {
		*dest++ = tolower(*src++);
	}
	*dest = '\0';
}

int fuzzy_match(const char* text, const char* pattern) {
	int score = 0;
	while (*text && *pattern) {
		if (*text == *pattern) {
			score++;
			pattern++;
		}
		text++;
	}
	return score;
}

int compute_score(const char* title, const char* query) {
	char t[1024];
	char q[1024];
	_app_title_to_lower(t, title);
	_app_title_to_lower(q, query);

	if (strcmp(t, q) == 0)
		return 100;

	if (strncmp(t, q, strlen(q)) == 0)
		return 75;

	if (strstr(t, q))
		return 50;

	int fuzzy = fuzzy_match(t, q);
	if (fuzzy > 0)
		return 10 + fuzzy;
	return 0;
}

int compare(const void* a, const void* b) {
	ButtonMatch* lb = (ButtonMatch*)a;
	ButtonMatch* lb2 = (ButtonMatch*)b;
	return lb->score - lb2->score;
}

void _match_string(char* string, list_t* lbutton_list) {
	ButtonMatch* bm = (ButtonMatch*)malloc(lbutton_list->pointer * sizeof(ButtonMatch));
	for (int i = 0; i < lbutton_list->pointer; i++) {
		LaunchButton* lb = (LaunchButton*)list_get_at(lbutton_list, i);
		bm[i].lb = lb;
		bm[i].score = compute_score(lb->title, string);
	}

	qsort(bm, lbutton_list->pointer, sizeof(ButtonMatch), compare);

	for (int i = lbutton_list->pointer - 1; i > 0; i--) {
		if (bm[i].score > 0) {
			//_KePrint("%s (score : %d) \r\n", bm[i].lb->title, bm[i].score);
			AppGridAddButtonInSearch(xelaunch_get_app_grid(), bm[i].lb);
		}
	}
	free(bm);
}