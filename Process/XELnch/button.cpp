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

#include "button.h"
#include "launcher.h"
#include <stdlib.h>
#include <sys/_kefile.h>
#include <sys/_keipcpostbox.h>
#include <sys/iocodes.h>
#include <sys/mman.h>
#include <sys/_keproc.h>
#include <widgets/msgbox.h>

#define LAUNCHER_BUTTON_HOVER_DARK	0xCC658096
#define LAUNCHER_BUTTON_HOVER_LIGHT 0xCC8CA2B4
#define LAUNCHER_BUTTON_CLICKED		0xCC1D1D1D

void ButtonIconRead(ButtonIcon* btninfo);
void ButtonIconDraw(ButtonIcon* info, ChCanvas* canv, int x, int y, ChRect* limit);

/* LaunchButtonPaint -- default paint handler for launch button */
void LaunchButtonPaint(LaunchButton* lb, ChWindow* win) {
	ChDrawRect(win->canv, lb->x, lb->y, lb->w, lb->h, LAUNCHER_BACKGROUND_COLOR);
	if (lb->hover)
		ChColorDrawHorizontalGradient(win->canv,
									  lb->x,
									  lb->y,
									  lb->w,
									  lb->h,
									  LAUNCHER_BUTTON_HOVER_DARK,
									  LAUNCHER_BUTTON_HOVER_LIGHT);

	if (lb->clicked)
		ChDrawRect(win->canv, lb->x, lb->y, lb->w, lb->h, LAUNCHER_BUTTON_CLICKED);

	AppGrid* grid = XELauncherGetAppGrid();
	ChRect limit;
	limit.x = grid->x;
	limit.y = grid->y;
	limit.w = grid->w;
	limit.h = grid->h;
	if (lb->buttonIcon && lb->buttonIcon->imageData)
		ButtonIconDraw(lb->buttonIcon,
					   win->canv,
					   lb->x + lb->w / 2 - lb->buttonIcon->iconWidth / 2,
					   lb->y + lb->h / 2 - lb->buttonIcon->iconHeight / 2,
					   &limit);
	ChFontSetSize(win->app->baseFont, 11);
	int font_length = (int)ChFontGetWidth(win->app->baseFont, lb->title);
	int text_x = lb->x + lb->w / 2 - font_length / 2;
	int text_y = lb->y + lb->h - 5;
	/* White face over a hard opaque shadow. No scrim: the glass fill stays
	 * untouched so bright wallpaper shows through around the glyphs. */
	ChFontDrawTextClipped(win->canv,
						  win->app->baseFont,
						  lb->title,
						  text_x + 1,
						  text_y + 1,
						  0xFF000000u,
						  &limit);
	ChFontDrawTextClipped(win->canv,
						  win->app->baseFont,
						  lb->title,
						  text_x,
						  text_y,
						  0xFFFFFFFFu,
						  &limit);
}

/*
 * LaunchButtonMouseEvent -- mouse event handler
 * @param wid -- Launch Button widget
 * @param win -- Pointer to Window structure
 * @param x -- mouse x information
 * @param y -- mouse y information
 */
void LaunchButtonMouseEvent(LaunchButton* wid, ChWindow* win, int x, int y, int button) {
	bool pressed = button && !wid->kill_focus;

	if (!wid->hover_painted && wid->hover) {
		if (wid->drawLaunchButton)
			wid->drawLaunchButton(wid, win);
		ChWindowUpdate(win, wid->x, wid->y, wid->w, wid->h, false, true);
		wid->hover_painted = true;
	}

	if (!wid->hover && !pressed) {
		wid->hover_painted = false;
		if (wid->drawLaunchButton)
			wid->drawLaunchButton(wid, win);
		ChWindowUpdate(win, wid->x, wid->y, wid->w, wid->h, false, true);
	}

	/* rising edge only; do not sleep here -- the compositor is waiting --axiss */
	if (pressed && !wid->clicked) {
		wid->clicked = true;
		if (wid->drawLaunchButton)
			wid->drawLaunchButton(wid, win);
		ChWindowUpdate(win, wid->x, wid->y, wid->w, wid->h, false, true);
		if (wid->actionHandler)
			wid->actionHandler(wid, win);
	}
	if (!pressed && wid->clicked) {
		/* release while still hovering never hit the "!hover && !pressed"
		 * reset branch above (hover stayed true), so the button kept
		 * showing its pressed artwork until hover state next changed --
		 * repaint back to the resting/hover look on release too. --axiss */
		wid->clicked = false;
		if (wid->drawLaunchButton)
			wid->drawLaunchButton(wid, win);
		ChWindowUpdate(win, wid->x, wid->y, wid->w, wid->h, false, true);
	}

	wid->last_mouse_x = x;
	wid->last_mouse_y = y;
}

/*
 * LauncherButtonDefaultAction -- default action handler for launcher buttons
 * @param lbutton -- Pointer to launcher button passed by system
 * @param win -- Pointer to window structure
 */
void LauncherButtonDefaultAction(LaunchButton* lbutton, ChWindow* win) {
	ChWindowHide(win);
	_KeProcessSleep(100);
	int id = _KeCreateProcess(0, lbutton->title);
	if (id == -1) {
		_KePrint("Failed to open app -> %s \r\n", lbutton->title);
	}
	char** argvs = NULL;
	int numarg = 0;
	if (strcmp(lbutton->param, "Null") == 1) {
		char* p = (char*)malloc(strlen(lbutton->param) + 1);
		strcpy(p, lbutton->param);
		numarg++;
		argvs = (char**)malloc(numarg * sizeof(char*));
		memset(argvs, 0, 1);
		argvs[0] = p;
	}
	int status = _KeProcessLoadExec(id, lbutton->appname, 0, NULL);
	if (argvs) {
		free(argvs[0]);
		free(argvs);
	}
}

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
LaunchButton* CreateLaunchButton(int x, int y, int w, int h, char* title, char* appname) {
	LaunchButton* lb = (LaunchButton*)malloc(sizeof(LaunchButton));
	memset(lb, 0, sizeof(LaunchButton));
	lb->x = x;
	lb->y = y;
	lb->w = w;
	lb->h = h;
	lb->scratch_x = x;
	lb->scratch_y = y;
	lb->title = (char*)malloc(strlen(title) + 1);
	lb->appname = (char*)malloc(strlen(appname) + 1);
	strcpy(lb->title, title);
	strcpy(lb->appname, appname);
	lb->buttonIcon = 0;
	lb->drawLaunchButton = LaunchButtonPaint;
	lb->mouseEvent = LaunchButtonMouseEvent;
	lb->actionHandler = LauncherButtonDefaultAction;
	lb->page_number = 1;
	return lb;
}

ButtonIcon* CreateLaunchButtonIcon(char* iconfile, LaunchButton* button) {
	if (strlen(iconfile) == 1)
		return NULL;
	if (!button)
		return NULL;

	ButtonIcon* icon = (ButtonIcon*)malloc(sizeof(ButtonIcon));
	memset(icon, 0, sizeof(ButtonIcon));
	int fd = _KeOpenFile(iconfile, FILE_OPEN_READ_ONLY);
	if (fd == -1) {
		free(icon);
		button->buttonIcon = 0;
		return NULL;
	}

	_KePrint("Icon fd : %d \r\n", fd);
	XEFileStatus stat;
	_KeFileStat(fd, &stat);

	icon->filename = (char*)malloc(strlen(iconfile) + 1);
	strcpy(icon->filename, iconfile);
	icon->fileBuffer = (uint8_t*)_KeMemMap(NULL, stat.size, 0, 0, MEMMAP_NO_FILEDESC, 0);
	icon->iconFd = fd;
	icon->fileSize = stat.size;
	icon->usageCount = 0;
	button->buttonIcon = icon;
	ButtonIconRead(icon);
	return icon;
}

/* ButtonIconRead-- read the button icon file
* @param btninfo -- Pointer to Button information
*/
void ButtonIconRead(ButtonIcon* btninfo) {
	if (!btninfo)
		return;
	int read_bytes = _KeReadFile(btninfo->iconFd, btninfo->fileBuffer, btninfo->fileSize);

	uint8_t* buffer = (uint8_t*)btninfo->fileBuffer;

	unsigned int offset = 0;
	memcpy(&offset, buffer + 10, sizeof(int));

	uint8_t* info = (uint8_t*)(buffer + sizeof(BMP));
	int width = 0;
	memcpy(&width, info + 4, sizeof(int));
	int height = 0;
	memcpy(&height, info + 8, sizeof(int));
	int bpp = 0;
	memcpy(&bpp, info + 14, sizeof(unsigned short));

	// printf("ButtonIconRead: fd=%d size=%d read=%d width=%d height=%d offset=%d\n", btninfo->iconFd, btninfo->fileSize, read_bytes, width, height, offset);

	btninfo->iconBpp = bpp;
	btninfo->iconWidth = width;
	btninfo->iconHeight = height;

	void* image_bytes = (void*)(buffer + offset);
	btninfo->imageData = (uint8_t*)image_bytes;
	btninfo->iconWidth = width;
	btninfo->iconHeight = height;
	btninfo->iconBpp = bpp;
	//_KeCloseFile(btninfo->iconFd);
	btninfo->iconFd = -1;
}

/*
*  ButtonIconDraw -- draws the application icon
* @param info -- Pointer to button info
* @param canv -- Pointer to window canvas
* @param x -- X coordinate
* @param y -- Y coordinate
*/
void ButtonIconDraw(ButtonIcon* info, ChCanvas* canv, int x, int y, ChRect* limit) {
	int width = info->iconWidth;
	int height = info->iconHeight;

	if (x > (limit->x + limit->w))
		return;

	if (y > (limit->y + limit->h))
		return;

	if ((y + height) <= limit->y)
		return;

	if ((x + width) <= limit->x)
		return;

	int diff_y = 0;
	if (y <= limit->y) {
		diff_y = limit->y - y;
		y = limit->y;
		height -= diff_y;
	}

	int diff_x = 0;
	if (x <= limit->x) {
		diff_x = limit->x - x;
		x = limit->x;
		width -= diff_x;
	}

	if ((x + width) > (limit->x + limit->w))
		width = (limit->x + limit->w) - x;

	if ((y + height) > (limit->y + limit->h))
		height = (limit->y + limit->h) - y;

	uint8_t* image = info->imageData;
	int bytes_per_pixel = info->iconBpp / 8;
	if (bytes_per_pixel == 0)
		bytes_per_pixel = 3;
	int row_pitch = ((info->iconWidth * info->iconBpp + 31) / 32) * 4;

	for (int i = 0; i < height; i++) {
		int image_y = diff_y + i;
		int bmp_row = info->iconHeight - 1 - image_y;
		if (bmp_row < 0 || bmp_row >= info->iconHeight)
			continue;

		uint8_t* image_row = image + bmp_row * row_pitch;
		for (int k = 0; k < width; k++) {
			int image_x = diff_x + k;
			if (image_x < 0 || image_x >= info->iconWidth)
				continue;

			uint8_t* pixel = image_row + image_x * bytes_per_pixel;
			uint32_t b = pixel[0];
			uint32_t g = pixel[1];
			uint32_t r = pixel[2];

			if (bytes_per_pixel == 3) {
				if ((r == 255 && g == 255 && b == 255) || (r == 0 && g == 0 && b == 0))
					continue;
				/* 24-bit BMPs have no alpha. Glass compose treats sa==0 as
				 * "show only the wallpaper blur", so an RGB-only write made
				 * launcher icons vanish into the glass. --axiss */
				ChDrawPixel(canv, x + k, y + i, 0xFF000000u | (r << 16) | (g << 8) | b);
			} else {
				uint32_t a = pixel[3];
				/* The 32-bit assets store a drop shadow as near-black with
				 * partial alpha. On a photo wallpaper it disappears; on the
				 * flat 640x480 glass it reads as a black square behind every
				 * icon. Skip those texels so the glass fill shows through. --axiss */
				if (a == 0)
					continue;
				if (a < 255 && r < 32 && g < 32 && b < 32)
					continue;
				ChDrawPixel(canv, x + k, y + i, (a << 24) | (r << 16) | (g << 8) | b);
			}
		}
	}
}
