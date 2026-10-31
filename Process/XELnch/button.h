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

#ifndef __BUTTON_H__
#define __BUTTON_H__

#include <stdint.h>
#include <stdarg.h>
#include <chitralekha.h>
#include <widgets/window.h>

typedef struct _ButtonIcon_ {
	char* filename;
	int iconWidth;
	int iconHeight;
	int iconBpp;
	int iconFd;
	uint8_t* fileBuffer;
	uint8_t* imageData;
	uint32_t fileSize;
	int usageCount;
} ButtonIcon;

typedef struct _button_ {
	int x;
	int y;
	int w;
	int h;
	int last_mouse_x;
	int last_mouse_y;
	bool clicked;
	bool hover;
	bool hover_painted;
	bool kill_focus;
	char* title;
	char* appname;
	char* param;
	bool focused;
	int scratch_x;
	int scratch_y;
	int page_number;
	ButtonIcon* buttonIcon;
	void (*actionHandler)(_button_* button, ChWindow*);
	void (*mouseEvent)(_button_* button, ChWindow* win, int x, int y, int but);
	void (*drawLaunchButton)(_button_* button, ChWindow* win);
	void (*destroy)(_button_* button, ChWindow* win);
} LaunchButton;

/*
* CreateLaunchButton -- creates a new launch button
* @param x -- X coordinate, if no grid, it needs to set manually
* @param y -- Y coordinate, if no grid, it needs to set manually
* @param w -- width of the button
* @param h -- height of the button
* @param title -- title of the button
* @param appname -- name of the application associated with this
* button
*/
LaunchButton* CreateLaunchButton(int x, int y, int w, int h, char* title, char* appname);

ButtonIcon* CreateLaunchButtonIcon(char* iconfile, LaunchButton* button);

#endif