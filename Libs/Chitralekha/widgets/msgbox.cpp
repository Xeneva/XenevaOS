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

#include "msgbox.h"
#include <sys/_keproc.h>
#include "button.h"

#define MESSAGEBOX_WIDTH	  400
#define MESSAGEBOX_HEIGHT	  150
#define MESSAGEBOX_BACKGROUND 0xFFD2D2D2
/*
 * ChMessageBoxDestroy -- destroy message box
 * @param wid -- System data structure
 * @param win -- Pointer to its parent window
 */
void ChMessageBoxDestroy(ChWidget* wid, ChWindow* win) {
	ChMessageBox* mb = (ChMessageBox*)wid;
	free(mb->message);
	free(mb);
}

/*
 * ChMessageBoxPaint -- draw the content of message box
 */
void ChMessageBoxPaint(ChWidget* wid, ChWindow* win) {
	ChMessageBox* mb = (ChMessageBox*)wid;
	ChDrawRect(win->canv, 0, mb->wid.y, mb->wid.w, mb->wid.h, MESSAGEBOX_BACKGROUND);
	ChFontSetSize(win->app->baseFont, 13);
	int msgw = ChFontGetWidth(win->app->baseFont, mb->message);
	int msgh = ChFontGetHeight(win->app->baseFont, mb->message);
	ChFontDrawText(win->canv,
				   win->app->baseFont,
				   mb->message,
				   mb->wid.w / 2 - msgw / 2,
				   mb->wid.h / 2 /*- msgh / 2*/,
				   12,
				   BLACK);
	ChDrawRect(win->canv, 0, mb->wid.y + mb->wid.h - 60, mb->wid.w, 60, 0xFFC0C8CF);
	ChIcon* icon = ChIconGetSystemIcon(mb->icon);
	if (icon)
		ChDrawIcon(win->canv, icon, 10, ((mb->wid.y + 60) / 2) - 15);
	ChDrawRectUnfilled(win->canv, 0, mb->wid.y + mb->wid.h - 60, mb->wid.w, 60, 0xFF324352);
}

void ChMessageBoxClose(ChWindow* win, ChWinGlobalControl* ctl) {
	uint8_t flags = WINDOW_FLAG_MOVABLE;
	ChWindowSetFlags(win->parent, flags);
	_KeProcessSleep(50);
	ChWindowCloseWindow(win);
}

/*
 * ChMessageBoxPrepareButtons -- prepare message box buttons
 * @param win -- Pointer to message box backed window
 * @param mb -- Pointer to message box widget
 * @param buttons -- type of buttons
 */
void ChMessageBoxPrepareButtons(ChWindow* win, ChMessageBox* mb, uint8_t buttons) {
	switch (buttons) {
	case MSGBOX_TYPE_YESNO: {
		ChButton* yes = ChCreateButton(
			mb->wid.x + mb->wid.w - 100 * 2 - 10, mb->wid.y + mb->wid.h - 60, 100, 30, "Yes"); //
		ChWindowAddWidget(win, (ChWidget*)yes);
		ChButton* no = ChCreateButton(
			mb->wid.x + mb->wid.w - 100 - 5, mb->wid.y + mb->wid.h - 60, 100, 30, "No");
		ChWindowAddWidget(win, (ChWidget*)no);
		break;
	}
	case MSGBOX_TYPE_OKCANCEL: {
		ChButton* Ok = ChCreateButton(
			mb->wid.x + mb->wid.w - 100 * 2 - 10, mb->wid.y + mb->wid.h - 60, 100, 30, "Okay"); //
		ChWindowAddWidget(win, (ChWidget*)Ok);
		ChButton* cancel = ChCreateButton(
			mb->wid.x + mb->wid.w - 100 - 5, mb->wid.y + mb->wid.h - 60, 100, 30, "Cancel");
		ChWindowAddWidget(win, (ChWidget*)cancel);
		break;
	}
	case MSGBOX_TYPE_ONLYCLOSE: {
		ChButton* Close = ChCreateButton(
			mb->wid.x + mb->wid.w - 100 - 5, mb->wid.y + mb->wid.h - 60, 100, 30, "Close");
		ChWindowAddWidget(win, (ChWidget*)Close);
		break;
	}
	case MSGBOX_TYPE_ONLYCANCEL: {
		ChButton* cancel = ChCreateButton(
			mb->wid.x + mb->wid.w - 100 - 5, mb->wid.y + mb->wid.h - 60, 100, 30, "Cancel");
		ChWindowAddWidget(win, (ChWidget*)cancel);
		break;
	}
	}
}
/*
 * ChCreateMessageBox -- create a chitralekha message box
 * @param mainWin -- pointer to main window
 * @param title -- title of the message box
 * @param msg -- message to show
 * @param buttons -- button type
 * @param icon -- icon to show
 */
XE_EXTERN XE_EXPORT ChMessageBox*
ChCreateMessageBox(ChWindow* mainWin, char* title, char* msg, uint8_t buttons, uint8_t icon) {
	ChMessageBox* mb = (ChMessageBox*)malloc(sizeof(ChMessageBox));
	memset(mb, 0, sizeof(ChMessageBox));
	mb->message = (char*)malloc(strlen(msg) + 1);
	memset(mb->message, 0, (strlen(msg) + 1));
	strcpy(mb->message, msg);
	mb->icon = icon;
	mb->type = buttons;
	mb->wid.ChDestroy = ChMessageBoxDestroy;
	mb->wid.x = 0;
	mb->wid.y = 26;
	mb->wid.ChPaintHandler = ChMessageBoxPaint;
	ChitralekhaApp* app = ChitralekhaStartSubApp(mainWin->app);
	ChFontSetSize(mainWin->app->baseFont, 13);

	/* calculate message box width */
	int msgboxWidth = ChFontGetWidth(mainWin->app->baseFont, msg) + 65 * 2;
	if (msgboxWidth <= 100)
		msgboxWidth = MESSAGEBOX_WIDTH;

	ChWindow* msgwin =
		ChCreateWindow(app,
					   WINDOW_FLAG_MOVABLE | WINDOW_FLAG_MESSAGEBOX,
					   title,
					   mainWin->info->x + mainWin->info->width / 2 - msgboxWidth / 2,
					   mainWin->info->y + mainWin->info->height / 2 - MESSAGEBOX_HEIGHT / 2,
					   msgboxWidth,
					   MESSAGEBOX_HEIGHT);
	mb->wid.w = msgboxWidth;
	mb->wid.h = MESSAGEBOX_HEIGHT - 26;
	ChWindowAddSubWindow(mainWin, msgwin);
	ChWindowAddWidget(msgwin, (ChWidget*)mb);

	for (int i = 0; i < msgwin->GlobalControls->pointer; i++) {
		ChWinGlobalControl* glbl = (ChWinGlobalControl*)list_get_at(msgwin->GlobalControls, i);
		if (glbl->type == WINDOW_GLOBAL_CONTROL_CLOSE) {
			glbl->ChGlobalActionEvent = ChMessageBoxClose;
			break;
		}
	}
	mb->backedWindow = msgwin;
	ChMessageBoxPrepareButtons(msgwin, mb, buttons);
	return mb;
}

/*
 * ChMessageBoxShow - shows the message box
 * @param mb -- Pointer to message box
 */
XE_EXTERN XE_EXPORT void ChMessageBoxShow(ChMessageBox* mb) {
	ChWindow* win = (ChWindow*)mb->backedWindow;
	ChWindowPaint(win);
	_KeProcessSleep(20);
	uint8_t flags = WINDOW_FLAG_STATIC | WINDOW_FLAG_NON_RESIZABLE | WINDOW_FLAG_BLOCKED;
	ChWindowSetFlags(win->parent, flags);
	_KeProcessSleep(60);
}