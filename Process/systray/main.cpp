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

#include <stdint.h>
#include <_xeneva.h>
#include <stdio.h>
#include <sys/_keproc.h>
#include <sys/_kefile.h>
#include <sys/iocodes.h>
#include <sys/_ketimer.h>
#include <chitralekha.h>
#include <widgets/base.h>
#include <widgets/button.h>
#include <widgets/window.h>
#include <widgets/slider.h>
#include <string.h>
#include <stdlib.h>
#include "systray.h"
#include "traygraphics.h"
#include "audioCtrl.h"

ChitralekhaApp* app;
ChWindow* mainWin;
ChWindow* soundWindow;
ChWindow* bluetoothWin;
DeodhaiAudioBox* audioControl;
list_t* windowList;
int frameCount;
bool _hidden;
int default_win_x;
int threadID;
int timerTick;
int screenW;
int screenH;
int guessW;
int winy;
bool TrayHiddenLock;
unsigned char* img = NULL;
void TrayMouseHandle(ChWindow* win, int x, int y, int button, int scroll);
void TrayHideWindow(ChWindow* win);
void TrayShowWindow(ChWindow* win);

#define SYSTRAY_GEOM_WIDTH			   35
#define SYSTRAY_GEOM_HEIGHT(x)		   x
#define SYSTRAY_GEOM_BUTTON_WIDTH(win) (win->info->width - 10)
#define SYSTRAY_GEOM_BUTTON_HEIGHT	   25
#define SYSTRAY_GEOM_BUTTON_XLOC	   5

/*
 * WindowHandleMessage -- handles incoming deodhai messages
 * @param e -- PostBox event message structure
 */
void WindowHandleMessage(PostEvent* e) {
	switch (e->type) {
	case TIMER_MESSAGE_CODE: {
		if (timerTick++ == 5)
			TrayHideWindow(mainWin);
		memset(e, 0, sizeof(PostEvent));
		break;
	}
	/* handle mouse event from deodhai */
	case DEODHAI_REPLY_MOUSE_EVENT: {
		ChWindow* eventWindow = ChGetWindowByHandle(mainWin, e->dword4);
		if (eventWindow)
			TrayMouseHandle(eventWindow, e->dword, e->dword2, e->dword3, 0);
		memset(e, 0, sizeof(PostEvent));
		break;
	}
		/* handle key events from deodhai */
	case DEODHAI_REPLY_KEY_EVENT: {
		int code = e->dword;
		memset(e, 0, sizeof(PostEvent));
		break;
	}
	case DEODHAI_REPLY_MOUSE_LEAVE: {
		TrayHideWindow(mainWin);
		memset(e, 0, sizeof(PostEvent));
		break;
	}
	}
}

void SVGToFB(ChCanvas* canv, unsigned char* pixbuf, int iw, int ih, int dstx, int dsty) {
	_KePrint("IW -> %d, IH -> %d \r\n", iw, ih);
	for (int y = 0; y < ih; ++y) {
		if (y + dsty >= canv->canvasHeight)
			break;
		for (int x = 0; x < iw; ++x) {
			if (x + dstx >= canv->canvasWidth)
				break;

			int imgIdx = (y * iw * x) * 4;
			int fbIdx = (y + dsty) * canv->canvasWidth + (x + dstx);
			uint8_t r = pixbuf[imgIdx + 0] & 0xff;
			uint8_t g = pixbuf[imgIdx + 1] & 0xff;
			uint8_t b = pixbuf[imgIdx + 2] & 0xff;
			uint8_t a = pixbuf[imgIdx + 3] & 0xff;

			uint32_t rgb = ((a << 24) | (r << 16) | (g << 8) | (b));
			if (rgb & 0xFF000000)
				ChDrawPixel(canv, dstx + x, dsty + y, rgb);
		}
	}
}

void TrayPaint(ChWindow* win) {
	ChDrawRect(win->canv, 0, 0, win->info->width, win->info->height, LIGHTBLACK);
	for (int i = 0; i < win->widgets->pointer; i++) {
		ChWidget* wid = (ChWidget*)list_get_at(win->widgets, i);
		if (wid)
			if (wid->ChPaintHandler)
				wid->ChPaintHandler(wid, win);
	}

	ChWindowUpdate(win, 0, 0, win->info->width, win->info->height, 1, 0);
}

/*
 * TrayMouseHandle -- handle incoming mouse event
 * @param win -- Pointer to Main Window
 * @param x -- Mouse x position
 * @param y -- Mouse y position
 * @param button -- Mouse button state
 * @param scroll -- Mouse scroll event
 */
void TrayMouseHandle(ChWindow* win, int x, int y, int button, int scroll) {
	if (_hidden)
		TrayShowWindow(win);
	for (int i = 0; i < win->widgets->pointer; i++) {
		ChWidget* widget = (ChWidget*)list_get_at(win->widgets, i);
		if (x > win->info->x + widget->x && x < (win->info->x + widget->x + widget->w) &&
			(y > win->info->y + widget->y && y < (win->info->y + widget->y + widget->h))) {
			widget->hover = true;
			widget->KillFocus = false;
			if (widget->ChMouseEvent)
				widget->ChMouseEvent(widget, win, x, y, button);
		} else {
			if (widget->hover) {
				widget->hover = false;
				widget->KillFocus = true;
				if (widget->ChMouseEvent)
					widget->ChMouseEvent(widget, win, x, y, button);
			}
		}
	}
	_hidden = false;
}

void TrayShowWindow(ChWindow* win) {
	int count = 0;
	int winx = 0;
	while (1) {
		winx = win->info->x;
		if (winx < default_win_x)
			break;
		ChWindowMove(win, winx - 1, winy);
		_KeProcessSleep(9888);
	}
	//_KeStartTimer(threadID);
	_hidden = false;
}

void TrayHideWindow(ChWindow* win) {
	if (TrayHiddenLock)
		return;
	while (1) {
		int x = win->info->x;
		ChWindowMove(win, x + 1, winy);
		_KeProcessSleep(9888);
		if ((x + 34) == screenW)
			break;
	}
	//_KeProcessSleep(100);
	/*_KeStopTimer(threadID);
	timerTick = 0;*/
	_hidden = true;
}

void VolumeSliderActionHandler(ChWidget* wid, ChWindow* win_) {
	ChSlider* slider = (ChSlider*)wid;
	audioControl->ctlPanel->dirty = true;
	audioControl->ctlPanel->gain = TrayAudioStepToGain(slider->currentVal, slider->max);
	_KePrint("AudioControl->gain-> %f \r\n", audioControl->ctlPanel->gain);
	_KeProcessSleep(10);
}
/*
 * TrayCreateSoundWindow -- create the sound window
 */
void TrayCreateSoundWindow(ChWindow* mainWin, int x, int y) {
	ChitralekhaApp* sndApp = ChitralekhaStartSubApp(mainWin->app);
	soundWindow = ChCreateWindow(sndApp,
								 WINDOW_FLAG_STATIC | WINDOW_FLAG_ALWAYS_ON_TOP |
									 WINDOW_FLAG_BROADCAST_LISTENER | WINDOW_FLAG_ANIMATED,
								 "sound",
								 /* 70 is the x location of systray unhidden from screen edge
		 * 35 is the height of the systray
		 */
								 screenW - 70 - 35 + 1,
								 y,
								 SYSTRAY_GEOM_WIDTH,
								 250); //Height=380

	/* free up the global control,because it might be using memory resource */
	for (int i = 0; i < soundWindow->GlobalControls->pointer; i++) {
		ChWinGlobalControl* glb = (ChWinGlobalControl*)list_remove(soundWindow->GlobalControls, i);
		if (glb)
			free(glb);
	}
	/* free up the main list also */
	free(soundWindow->GlobalControls);
	soundWindow->color = LIGHTBLACK;

	ChSlider* soundVol =
		ChCreateSlider(CHITRALEKHA_SLIDER_VERTICAL, soundWindow->info->width / 2 - 10 / 2, 5, 200);
	soundVol->customColor2 = 0xFF799749;
	soundVol->customColor1 = 0xFF7EB02B;
	soundVol->useCustomColor = true;
	soundVol->outlineColor = 0xFFDE86C1;
	soundVol->base.ChActionHandler = VolumeSliderActionHandler;
	ChSliderSetMin(soundVol, 77.0f);
	soundVol->thumbY = 1;
	soundVol->progressPixel = soundVol->thumbY;
	ChWindowAddWidget(soundWindow, (ChWidget*)soundVol);
	soundWindow->ChWinPaint = SoundWindowPainter;

	ChWindowPaint(soundWindow);
	ChWindowAddSubWindow(mainWin, soundWindow);
	TrayHiddenLock = true;
}

void SoundButtonActionHandler(ChWidget* wid, ChWindow* win) {
	TrayButton* tb = (TrayButton*)wid;
	if (soundWindow == NULL) {
		TrayCreateSoundWindow(win, 0, win->info->y + tb->base.y);
	} else {
		ChWindowHide(soundWindow);
		if (TrayHiddenLock)
			TrayHiddenLock = false;
		else
			TrayHiddenLock = true;
	}
}
/*
* main -- main entry
*/
int main(int argc, char* argv[]) {
	app = ChitralekhaStartApp(argc, argv);

	ChFontSetSize(app->baseFont, 13);

	XEFileIOControl graphctl;
	memset(&graphctl, 0, sizeof(XEFileIOControl));
	graphctl.syscall_magic = AURORA_SYSCALL_MAGIC;

	ChCanvas* canv = ChCreateCanvas(100, 100);
	int screen_w = canv->screenWidth;
	int screen_h = canv->screenHeight;
	screenW = screen_w;
	screenH = screen_h;
	int bpp = canv->bpp;
	int graphicsFd = canv->graphics_fd;
	timerTick = 0;
	_hidden = false;
	frameCount = 30;
	TrayHiddenLock = false;
	threadID = _KeGetThreadID();
	free(canv);

	mainWin = ChCreateWindow(app,
							 WINDOW_FLAG_STATIC | WINDOW_FLAG_ALWAYS_ON_TOP |
								 WINDOW_FLAG_BROADCAST_LISTENER | WINDOW_FLAG_ANIMATED,
							 "tray",
							 screen_w - 70,
							 (screen_h / 2) - (210 / 2),
							 SYSTRAY_GEOM_WIDTH,
							 210); //Height=380
	mainWin->ChWinPaint = TrayPaint;
	default_win_x = mainWin->info->x;
	winy = mainWin->info->y;
	/* free up the global control,because it might be using memory resource */
	for (int i = 0; i < mainWin->GlobalControls->pointer; i++) {
		ChWinGlobalControl* glb = (ChWinGlobalControl*)list_remove(mainWin->GlobalControls, i);
		if (glb)
			free(glb);
	}
	/* free up the main list also */
	free(mainWin->GlobalControls);

	windowList = initialize_list();

	ChIcon* btIcon = ChCreateIcon();
	ChIconOpen(btIcon, "/icons/bt25.bmp");
	ChIconRead(btIcon);

	ChIcon* nonetIcon = ChCreateIcon();
	ChIconOpen(nonetIcon, "/icons/plug25.bmp");
	ChIconRead(nonetIcon);

	ChIcon* sndIcon = ChCreateIcon();
	ChIconOpen(sndIcon, "/icons/snd25.bmp");
	ChIconRead(sndIcon);

	ChIcon* gearIcon = ChCreateIcon();
	ChIconOpen(gearIcon, "/icons/gear25.bmp");
	ChIconRead(gearIcon);

	TrayButtonInitialize();

	TrayButton* tb = TrayCreateButton("Bluetooth",
									  SYSTRAY_GEOM_BUTTON_XLOC,
									  10,
									  SYSTRAY_GEOM_BUTTON_WIDTH(mainWin),
									  SYSTRAY_GEOM_BUTTON_HEIGHT);
	tb->icon = btIcon;
	tb->type = TRAY_BUTTON_TYPE_ONOFF;
	tb->onoff = 0;
	ChWindowAddWidget(mainWin, (ChWidget*)tb);

	TrayButton* network = TrayCreateButton("Internet",
										   SYSTRAY_GEOM_BUTTON_XLOC,
										   0,
										   SYSTRAY_GEOM_BUTTON_WIDTH(mainWin),
										   SYSTRAY_GEOM_BUTTON_HEIGHT);
	network->icon = nonetIcon;
	network->type = TRAY_BUTTON_TYPE_NORMAL;
	ChWindowAddWidget(mainWin, (ChWidget*)network);

	TrayButton* sndBut = TrayCreateButton("Sound",
										  SYSTRAY_GEOM_BUTTON_XLOC,
										  0,
										  SYSTRAY_GEOM_BUTTON_WIDTH(mainWin),
										  SYSTRAY_GEOM_BUTTON_HEIGHT);
	sndBut->icon = sndIcon;
	sndBut->type = TRAY_BUTTON_TYPE_NORMAL;
	sndBut->base.ChActionHandler = SoundButtonActionHandler;
	ChWindowAddWidget(mainWin, (ChWidget*)sndBut);

	TrayButton* settingBut = TrayCreateButton("Settings",
											  SYSTRAY_GEOM_BUTTON_XLOC,
											  0,
											  SYSTRAY_GEOM_BUTTON_WIDTH(mainWin),
											  SYSTRAY_GEOM_BUTTON_HEIGHT);
	settingBut->icon = gearIcon;
	settingBut->type = TRAY_BUTTON_TYPE_NORMAL;
	ChWindowAddWidget(mainWin, (ChWidget*)settingBut);

	ChWindowPaint(mainWin);

	TrayHideWindow(mainWin);

	audioControl = TrayGetSoundControl(app);

	PostEvent e;
	memset(&e, 0, sizeof(PostEvent));

	while (1) {
		int err = _KeFileIoControl(app->postboxfd, POSTBOX_GET_EVENT, &e);
		WindowHandleMessage(&e);
		if (err == POSTBOX_NO_EVENT)
			_KePauseThread();
	}
	_KePauseThread();
}