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

#ifndef __SYS_TRAY_H__
#define __SYS_TRAY_H__

#include <stdint.h>
#include <widgets/base.h>
#include <widgets/icon.h>
#include <widgets/window.h>

#define TRAY_BUTTON_TYPE_ONOFF	1
#define TRAY_BUTTON_TYPE_NORMAL 0

typedef struct _tray_but_ {
	ChWidget base;
	ChIcon* icon;
	char* title;
	uint8_t frameCount;
	uint8_t currentFrame;
	uint8_t type;
	bool onoff;
} TrayButton;

extern void TrayButtonInitialize();
/*
 * TrayCreateButton -- create a tray button
 * @param title -- title of the button
 * @param x -- X location of the button
 * @param y -- Y location of the button
 * @param width -- Width of the button
 * @param height -- Height of the button
 */
extern TrayButton* TrayCreateButton(char* title, int x, int y, int width, int height);
#endif
