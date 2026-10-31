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

#ifndef __NAMDAPHA_H__
#define __NAMDAPHA_H__

#include <stdint.h>
#include <widgets/window.h>
#include <widgets/list.h>

#define NAMDAPHA_TIME_BUTTON_COLOR 0xFF7B7878
/* The date string is 10 chars (~71px at the date size); the old 65px-wide
 * clock button clipped it. --axiss */
#define NAMDAPHA_TIME_BUTTON_W 100
#ifdef NAMDAPHA_HORIZONTAL
#define NAMDAPHA_WIDTH 65
#else
#define NAMDAPHA_WIDTH 75
#endif

#define NAMDAPHA_COLOR				  0x00404040 // 0xFF87A3AC
#define NAMDAPHA_COLOR_DARK			  0x33322D2D
#define NAMDAPHA_COLOR_LIGHT		  0x33646262
#define GO_BUTTON_WIDTH				  (NAMDAPHA_WIDTH - 20)
#define GO_BUTTON_XCOORD			  10
#define GO_BUTTON_COLOR				  0xFF7B7878
#define GO_BUTTON_HOVER				  0x33CDE8F0
#define GO_BUTTON_PRESSED			  0x33345862
#define NAMDAPHA_FOCUSED_BUTTON_DARK  0x33658096
#define NAMDAPHA_FOCUSED_BUTTON_LIGHT 0x338CA2B4

/* GEOMETRY information*/
#define NAMDAPHA_BUTTON_YPAD 10
#ifdef NAMDAPHA_VERTICAL
#define NAMDAPHA_BUTTON_XPAD 0
#elif NAMDAPHA_HORIZONTAL
#define NAMDAPHA_BUTTON_XPAD 15
#endif

#define NAMDAPHA_BUTTON_WIDTH  55
#define NAMDAPHA_BUTTON_HEIGHT 50

/* tasklet definitions */
#define NAMDAPHA_TASKLET_AUDIO		 1
#define NAMDAPHA_TASKLET_WEATHER	 2
#define NAMDAPHA_TASKLET_GEOLOCATION 3

typedef struct _buttoninfo_ {
	char* filename;
	int iconWidth;
	int iconHeight;
	int iconBpp;
	int iconFd;
	uint8_t* fileBuffer;
	uint8_t* imageData;
	uint32_t fileSize;
	int usageCount;
} ButtonInfo;

/* Namdapha Button structure
 * very similar to Chitralekha Widget
 */
typedef struct _NamdaphaButton_ {
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
	uint64_t last_action_ms;
	uint16_t ownerId;
	uint32_t winHandle;
	char* title;
	bool focused;
	ButtonInfo* nmbuttoninfo;
	void (*actionHandler)(_NamdaphaButton_* button, ChWindow*);
	void (*mouseEvent)(_NamdaphaButton_* button, ChWindow* win, int x, int y, int but);
	void (*drawNamdaphaButton)(_NamdaphaButton_* button, ChWindow* win);
	void (*destroy)(_NamdaphaButton_* button, ChWindow* win);
} NamdaphaButton;

/*
* NmCreateButton -- creates a namdapha button
* @param x -- X coordinate
* @param y -- Y coordinate
* @param w -- Width of the button bound
* @param h -- Height of the button bound
* @param text -- title of the button
*/
extern NamdaphaButton* NmCreateButton(int x, int y, int w, int h, char* text);

/*
* NmCreateButtonInfo -- create a button info, here button
* info means button icon information
* @param filename -- icon file path
* supported formats are only 32 bit alpha based BMP file
*/
extern ButtonInfo* NmCreateButtonInfo(char* filename);

/* NmButtonInfoRead-- read the button info file
* @param btninfo -- Pointer to Button information
*/
extern void NmButtonInfoRead(ButtonInfo* btninfo);

/*
* NmButtonInfoDrawIcon -- draws the application icon to canvas
* @param info -- Pointer to button info
* @param canv -- Pointer to window canvas
* @param x -- X coordinate
* @param y -- Y coordinate
*/
extern void NmButtonInfoDrawIcon(ButtonInfo* info, ChCanvas* canv, int x, int y);

extern NamdaphaButton* NamdaphaInitialiseGoButton(ChWindow* win);

/*
* NamdaphaChangeFocus -- changes a focus of window
* @param button -- window's button
*/
extern void NamdaphaChangeFocus(NamdaphaButton* button);

/* NamdaphaHideWindow -- send a hide command to deodhai
* @param button -- window's button
*/
extern void NamdaphaHideWindow(NamdaphaButton* button);

/* NamdaphaGetScreenWidth -- returns the screen width */
extern int NamdaphaGetScreenWidth();

/* NamdaphaGetScreenHeight -- returns the screen height */
extern int NamdaphaGetScreenHeight();

/**
 * @brief NamdaphaGetYOD -- get year, month and date
 * @param year -- memory location where to store year
 * @param month -- memory location where to store month
 * @param day -- memory location where to store day
 * @param tz_offset -- timezone offset in second
 */
extern void NamdaphaGetYMD(uint32_t* year, uint32_t* month, uint32_t* day, int32_t tz_offset);

/**
 * NamdaphaGetWallTime -- get the walltime update
 * @param hour -- memory location to store hour
 * @param minute -- memory location to store minute
 * @param sec -- memory location to store second
 * @param tz_offset -- timezone offset
 */
extern void NamdaphaGetWallTime(int* hour, int* minute, int* sec, int tz_offset);

#endif