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

#include "window.h"
#include "../_fastcpy.h"
#include <sys/_kefile.h>
#include <sys/_keproc.h>
#include "menu.h"
#include <sys/mman.h>

#define WINDOW_DEFAULT_TITLEBAR_HEIGHT 26
#define WINDOW_DEFAULT_BACKGROUND	   0xFFD2D2D2

extern void ChDefaultWinPaint(ChWindow* win);
extern void ChWindowPaintCloseButton(ChWindow* win, ChWinGlobalControl* button);
extern void ChWindowPaintMaximButton(ChWindow* win, ChWinGlobalControl* button);
extern void ChWindowPaintMinimButton(ChWindow* win, ChWinGlobalControl* button);
extern void ChWindowPaintTitlebar(ChWindow* win);
extern void ChDefaultPopupWinPaint(ChWindow* win);
extern void _apply_rounded_corner(uint32_t* backbuff, int radius, int winw, int winh);

/*
 * ChRequestWindow -- the base of window creation, it request window manager
 * a new window to be created
 * @param app -- Pointer to Chitralekha app
 * @param x -- X position of the window
 * @param y -- Y position of the window
 * @param w -- Width of the window
 * @param h -- Height of the window
 * @param title -- title of the window
 * @param handle -- handle of parent window 
 */
void ChRequestWindow(
	ChitralekhaApp* app, int x, int y, int w, int h, char* title, uint16_t attrib, int handle) {
	PostEvent e;
	e.type = DEODHAI_MESSAGE_CREATEWIN;
	e.dword = x;
	e.dword2 = y;
	e.dword3 = w;
	e.dword4 = h;
	e.dword5 = attrib;
	e.dword6 = handle;
	e.to_id = POSTBOX_ROOT_ID;
	strcpy(e.charValue3, title);
	e.from_id = app->currentID;
	_KeFileIoControl(app->postboxfd, POSTBOX_PUT_EVENT, &e);
	memset(&e, 0, sizeof(PostEvent));
	while (1) {
		//_KePrint("[Chitralekha]: Getting event \r\n");
		int err = _KeFileIoControl(app->postboxfd, POSTBOX_GET_EVENT, &e);
		if (e.type == DEODHAI_REPLY_WINCREATED) {
			uint16_t shkey = e.dword;
			uint32_t buffKey = e.dword2;
			if (buffKey < 100)
				_KePrint("Chitralekha: buffer key problem \n");
			uint32_t handle = e.dword3;
			uint16_t shid = _KeCreateSharedMem(shkey, 0, 0);
			uint16_t backid = _KeCreateSharedMem(buffKey, NULL, 0);
			void* sharedwinaddr = _KeObtainSharedMem(shid, NULL, 0);
			void* backbuff = _KeObtainSharedMem(backid, NULL, 0);
			app->sharedWinkey = shkey;
			app->backbufkey = buffKey;
			app->buffer_width = w;
			app->buffer_height = h;
			app->fb = backbuff;
			app->shwinbuf = sharedwinaddr;
			app->windowHandle = handle;
			memset(&e, 0, sizeof(PostEvent));
			break;
		}

		if (err == -1) {
			//_KePauseThread();
			//_KePrint("Polling post event \r\n");
			_KeProcessSleep(100);
		}
	}
}

void ChGlobalCtlMouseHandler(ChWindow* win, ChWinGlobalControl* global, int x, int y, int button) {
	/* handle painting */
	if (global->ChGlobalButtonPaint) {
		global->ChGlobalButtonPaint(win, global);
		ChWindowUpdate(win, global->x, global->y, global->w, global->h, false, true);
	}

	/* handle action event */
	if (button)
		if (global->ChGlobalActionEvent)
			global->ChGlobalActionEvent(win, global);
}

/*
 * ChCreateGlobalButton -- create global control button of windows
 * @param win -- Pointer to window
 * @param x -- X position within the window titlebar
 * @param y -- Y position within the window titlebar
 * @param w -- width of the rectangular bound
 * @param h -- height of the rectangular bound
 */
XE_EXTERN XE_EXPORT ChWinGlobalControl*
ChCreateGlobalButton(ChWindow* win, int x, int y, int w, int h, uint8_t type) {
	ChWinGlobalControl* glbl = (ChWinGlobalControl*)malloc(sizeof(ChWinGlobalControl));
	glbl->x = x;
	glbl->y = y;
	glbl->w = w;
	glbl->h = h;
	glbl->clicked = false;
	glbl->hover = false;
	glbl->type = type;
	list_add(win->GlobalControls, glbl);
	return glbl;
}

void ChGlobalCloseAction(ChWindow* win, ChWinGlobalControl* ctl) {
	//ChFontClose(win->app->baseFont);
	ChWindowCloseWindow(win);
}

void ChGlobalMaximAction(ChWindow* win, ChWinGlobalControl* ctl) {
	if (win->info->zoomed == false)
		win->info->zoomed = true;
	else
		win->info->zoomed = false;

	/* just give the compositor a space to do the things */
	_KeProcessSleep(10);
}

void ChGlobalMinimiseAction(ChWindow* win, ChWinGlobalControl* ctl) {
	ChWindowHide(win);
}

/*
 * ChCreateWindow -- create a new chitralekha window
 * @param app -- pointer to Chitralekha app
 * @param attrib -- window attributes
 * @param title -- title of the window
 * @param x -- x position of the window
 * @param y -- y position of the window
 * @param w -- width of the window
 * @param h -- height of the window
 */
XE_EXTERN XE_EXPORT ChWindow*
ChCreateWindow(ChitralekhaApp* app, uint16_t attrib, char* title, int x, int y, int w, int h) {
	/* request a new window from window manager */
	ChRequestWindow(app, x, y, w, h, title, attrib, 0);
	ChWindow* win = (ChWindow*)malloc(sizeof(ChWindow));
	memset(win, 0, sizeof(ChWindow));
	ChCanvas* canv = ChCreateCanvas(w, h);
#ifdef __XENEVA_BLEED__
	/* The shared compositor backbuffer is already writable by the client. Draw
	 * into it directly instead of allocating a same-sized staging buffer. */
	canv->buffer = (uint32_t*)app->fb;
	canv->bufferSz = 0;
#else
	ChAllocateBuffer(canv);
#endif
	win->app = app;
	win->flags = attrib;
	win->buffer = (uint32_t*)app->fb;
	win->canv = canv;
	win->sharedwin = app->shwinbuf;
	win->info = (ChSharedWinInfo*)win->sharedwin;
	win->title = (char*)malloc(strlen(title) + 1);
	memset(win->title, 0, strlen(title) + 1);
	strcpy(win->title, title);
	win->info->x = x;
	win->info->y = y;
	win->info->width = w;
	win->info->height = h;
	win->info->rect_count = 0;
	win->info->updateEntireWindow = 0;
	win->info->hide = 0;
	win->info->zoomed = 0;
	win->focused = true;
	win->color = WINDOW_DEFAULT_BACKGROUND;
	win->GlobalControls = initialize_list();
	win->widgets = initialize_list();
	win->subwindow = initialize_list();
	win->popup = initialize_list();
	win->ChWinPaint = ChDefaultWinPaint;
	win->handle = app->windowHandle;
	ChWinGlobalControl* close = ChCreateGlobalButton(win,
													 win->info->width - 25,
													 WINDOW_DEFAULT_TITLEBAR_HEIGHT / 2 - 20 / 2,
													 20,
													 20,
													 WINDOW_GLOBAL_CONTROL_CLOSE);
	close->ChGlobalButtonPaint = ChWindowPaintCloseButton;
	close->ChGlobalMouseEvent = ChGlobalCtlMouseHandler;
	close->outlineColor = 0xFF868686;
	close->hoverOutlineColor = 0xFFAF2E17;
	close->clickedOutlineColor = 0xFF63291F;
	close->ChGlobalActionEvent = ChGlobalCloseAction;

	ChWinGlobalControl* maxim = ChCreateGlobalButton(win,
													 win->info->width - 25 - 20,
													 WINDOW_DEFAULT_TITLEBAR_HEIGHT / 2 - 20 / 2,
													 20,
													 20,
													 WINDOW_GLOBAL_CONTROL_MAXIMIZE);
	maxim->ChGlobalButtonPaint = ChWindowPaintMaximButton;
	maxim->outlineColor = 0xFF868686;
	maxim->hoverOutlineColor = 0xFFAFA3A3;
	maxim->clickedOutlineColor = 0xFF444444;
	maxim->ChGlobalMouseEvent = ChGlobalCtlMouseHandler;
	maxim->ChGlobalActionEvent = ChGlobalMaximAction;

	ChWinGlobalControl* minim = ChCreateGlobalButton(win,
													 win->info->width - 25 - 20 * 2,
													 WINDOW_DEFAULT_TITLEBAR_HEIGHT / 2 - 20 / 2,
													 20,
													 20,
													 WINDOW_GLOBAL_CONTROL_MAXIMIZE);
	minim->ChGlobalButtonPaint = ChWindowPaintMinimButton;
	minim->outlineColor = 0xFF868686;
	minim->hoverOutlineColor = 0xFFAFA3A3;
	minim->clickedOutlineColor = 0xFF444444;
	minim->ChGlobalMouseEvent = ChGlobalCtlMouseHandler;
	minim->ChGlobalActionEvent = ChGlobalMinimiseAction;
	return win;
}

/*
 * ChWindowBroadcastIcon -- broadcast icon information to 
 * broadcast listener
 * @param app -- pointer to chitralekha application
 * @param iconfile -- path of the icon file, supported formats
 * are : 32bit- bmp file 
 */
XE_EXTERN XE_EXPORT void ChWindowBroadcastIcon(ChitralekhaApp* app, char* iconfile) {
	PostEvent e;
	memset(&e, 0, sizeof(PostEvent));
	e.type = DEODHAI_MESSAGE_BROADCAST_ICON;
	e.dword = app->windowHandle;
	e.to_id = POSTBOX_ROOT_ID;
	e.from_id = app->currentID;
	strcpy(e.charValue3, iconfile);
	_KeFileIoControl(app->postboxfd, POSTBOX_PUT_EVENT, &e);
}

/*
 * ChWindowUpdate -- update a portion or whole window
 * @param win -- Pointer to window
 * @param x -- x position of the dirty area
 * @param y -- y position of the dirty area
 * @param w - width of the dirty area
 * @param h -- height of the dirty area
 */
XE_EXTERN XE_EXPORT void
ChWindowUpdate(ChWindow* win, int x, int y, int w, int h, bool updateEntireWin, bool dirty) {
	uint32_t* lfb = win->buffer;
	uint32_t* canvaddr = win->canv->buffer;
	if (x < 0) {
		_KePrint("Corrupted x -> %d \r\n", x);
		x = 0;
	}

	if (y < 0) {
		_KePrint("Corrupted y -> %d \r\n", y);
		y = 0;
	}

	if (w > win->info->width) {
		_KePrint("Corrupted w -> %d \r\n", w);
		w = win->info->width;
	}
	if (h > win->info->height) {
		_KePrint("Corrupted h -> %d \r\n", h);
		h = win->info->height;
	}

	/* Clip the dirty rect to the window. Widgets that extend past the
	 * canvas (e.g. a textbox taller than its window) otherwise make the
	 * row copy below read past the canvas mapping and fault the app
	 * with an EL0 data abort. */
	if (x + w > win->info->width) {
		_KePrint("Clipping w -> %d \r\n", w);
		w = win->info->width - x;
	}
	if (y + h > win->info->height) {
		_KePrint("Clipping h -> %d \r\n", h);
		h = win->info->height - y;
	}
	if (w <= 0 || h <= 0)
		return;

	if (lfb != canvaddr) {
		for (int i = 0; i < h; i++)
			_fastcpy(lfb + (static_cast<int64_t>(y) + i) * win->info->width + x,
					 canvaddr + (static_cast<int64_t>(y) + i) * win->info->width + x,
					 (static_cast<int64_t>(w) * 4));
	}

	if (dirty) {
		uint32_t rectIndex = win->info->rect_count;
		if (rectIndex < 256) {
			win->info->rect[rectIndex].x = x;
			win->info->rect[rectIndex].y = y;
			win->info->rect[rectIndex].w = w;
			win->info->rect[rectIndex].h = h;
			//win->info->rect_count = rectIndex + 1;
			__atomic_store_n(&win->info->rect_count, rectIndex + 1, __ATOMIC_RELEASE);
		}
	}

	/* Publish metadata only after every pixel and dirty rectangle write is
	 * visible to the compositor. */
	ChSharedFlagStore(&win->info->updateEntireWindow, updateEntireWin);
	if (dirty)
		ChSharedFlagStore(&win->info->dirty, true);

#ifdef __XENEVA_BLEED__
	/* With a single shared draw/scanout buffer, wait for the compositor to
	 * acknowledge this update before the client can start painting the next
	 * one. This keeps the memory saving without racing a later frame. */
	if (dirty || updateEntireWin) {
		int timeout = 250;
		while (timeout-- > 0 && (ChSharedFlagLoad(&win->info->dirty) ||
								 ChSharedFlagLoad(&win->info->updateEntireWindow)))
			_KeProcessSleep(1);
	}
#endif
}

/*
 * ChWindowPaint -- paint the entire window
 * @param win -- Pointer to window
 */
XE_EXTERN XE_EXPORT void ChWindowPaint(ChWindow* win) {
	if (win->ChWinPaint) {
		win->ChWinPaint(win);
	}

	if (win->flags & WINDOW_FLAG_GLASS)
		_apply_rounded_corner(win->buffer, 4, win->info->width, win->info->height);
	else
		_apply_rounded_corner(win->buffer, 8, win->info->width, win->info->height);
	int timeout = 5000;
	/* Wait untill the window is ready on server side */
	while (!win->info->windowReady) {
		_KeProcessSleep(10);
		if (--timeout)
			break;
	}
}

/*
 * ChWindowSetGlassMorphism -- apply the glassmorphic look to a window.
 * @param win -- Pointer to the window
 * @param alpha -- opacity of the glass pane, 0 (invisible) .. 255 (opaque)
 *
 * The compositor's blur buffers are allocated at window creation, so the
 * window must have been created with WINDOW_FLAG_GLASS. This call makes the
 * surface actually read as glass: it paints a translucent pane over the whole
 * buffer (so the blurred backdrop shows through) and clears the uniform
 * window fade (info->alpha), because glass blending is per-pixel and the two
 * would otherwise double-blend. Content drawn afterwards -- terminal cells,
 * widgets -- keeps whatever alpha its painter chooses.
 */
XE_EXTERN XE_EXPORT void ChWindowSetGlassMorphism(ChWindow* win, uint8_t alpha) {
	if (!win || !win->canv || !win->info)
		return;
	win->flags |= WINDOW_FLAG_GLASS;
	win->info->alpha = false;
	uint32_t tint = SET_ALPHA(win->color, alpha);
	ChDrawRect(win->canv, 0, 0, win->info->width, win->info->height, tint);
	ChWindowUpdate(win, 0, 0, win->info->width, win->info->height, 1, 0);
}

/*
 * ChWindowAddSubWindow -- add sub window to parent window list
 * @param parent -- Pointer to main window
 * @param win -- Pointer to sub window
 */
XE_EXTERN XE_EXPORT void ChWindowAddSubWindow(ChWindow* parent, ChWindow* win) {
	if (!win)
		return;
	list_add(parent->subwindow, win);
	win->parent = parent;
}

/*
 * ChGetWindowByHandle -- returns window by looking sub windows list
 * @param mainWin -- pointer to main window
 * @param handle -- Handle of the window
 */
XE_EXTERN XE_EXPORT ChWindow* ChGetWindowByHandle(ChWindow* mainWin, int handle) {
	ChWindow* returnable = NULL;
	for (int i = 0; i < mainWin->subwindow->pointer; i++) {
		ChWindow* sub = (ChWindow*)list_get_at(mainWin->subwindow, i);
		if (sub->handle == handle) {
			returnable = sub;
			break;
		}
	}

	if (!returnable)
		return mainWin;

	return returnable;
}

/*
 * ChGetPopupWindowByHandle -- looks for popup window by its handle
 * @param mainWin -- Pointer to main window
 * @param handle -- Handle of the popup window
 */
XE_EXTERN XE_EXPORT ChWindow* ChGetPopupWindowByHandle(ChWindow* mainWin, int handle) {
	for (int i = 0; i < mainWin->popup->pointer; i++) {
		ChWindow* pw = (ChWindow*)list_get_at(mainWin->popup, i);
		if (pw->handle == handle) {
			return pw;
		}
	}
	return NULL;
}

/*
 * ChWindowHide -- hide the window
 * basically it sends command to deodhai
 * @param win -- Pointer to window structure
 */
XE_EXTERN XE_EXPORT void ChWindowHide(ChWindow* win) {
	PostEvent e;
	e.type = DEODHAI_MESSAGE_WINDOW_HIDE;
	e.dword = win->app->currentID;
	e.dword2 = win->handle;
	e.to_id = POSTBOX_ROOT_ID;
	_KeFileIoControl(win->app->postboxfd, POSTBOX_PUT_EVENT, &e);
}

/*
 * ChGetWindowHandle -- get a specific window handle from
 * the system looking by its name
 * @param app -- Pointer to application instance
 * @param title -- desired window title
 */
XE_EXTERN XE_EXPORT uint32_t ChGetWindowHandle(ChitralekhaApp* app, char* title) {
	uint32_t handle = 0;
	PostEvent e;
	e.type = DEODHAI_MESSAGE_GETWINDOW;
	e.to_id = POSTBOX_ROOT_ID;
	e.from_id = app->currentID;
	strcpy(e.charValue3, title);
	_KeFileIoControl(app->postboxfd, POSTBOX_PUT_EVENT, &e);
	memset(&e, 0, sizeof(PostEvent));
	while (1) {
		int err = _KeFileIoControl(app->postboxfd, POSTBOX_GET_EVENT, &e);
		if (e.type == DEODHAI_REPLY_WINDOW_ID) {
			handle = e.dword2;
			memset(&e, 0, sizeof(PostEvent));
			break;
		}
		if (err == POSTBOX_NO_EVENT)
			_KePauseThread();
	}

	return handle;
}

/*
 * ChWindowSetFocused -- bring a window to focused state
 * @param win -- Pointer to window
 */
XE_EXTERN XE_EXPORT void ChWindowSetFocused(ChWindow* win) {
	PostEvent e;
	memset(&e, 0, sizeof(PostEvent));
	e.type = DEODHAI_MESSAGE_WINDOW_BRING_FRONT;
	e.dword = win->app->currentID;
	e.dword2 = win->handle;
	e.to_id = POSTBOX_ROOT_ID;
	_KeFileIoControl(win->app->postboxfd, POSTBOX_PUT_EVENT, &e);
}

/*
 * ChWindowHandleMouse -- handle mouse event 
 * @param win -- Pointer to window
 * @param x -- X coord of the mouse
 * @param y -- Y coord of the mouse
 * @param button -- button state of the mouse
 */
XE_EXTERN XE_EXPORT void ChWindowHandleMouse(ChWindow* win, int x, int y, int button) {
	bool _popup_was_active = false;

	if (button != 0) {
		for (int i = 0; i < win->popup->pointer; i++) {
			ChWindow* popup = (ChWindow*)list_get_at(win->popup, i);
			if (popup->info->hide == 0) {
				_KePrint("A popup window is not hidden \r\n");
				/* ChWindowHide flips info->hide in shared memory, which
				 * the compositor reads directly every frame (60fps) --
				 * no IPC round-trip to wait out here --axiss */
				ChWindowHide(popup);
				_popup_was_active = true;
			}
		}
		if (_popup_was_active) {
			/* ChWindowSetFlags/ChWindowSetFocused send DEODHAI_MESSAGE_SET_FLAGS
			 * / WINDOW_BRING_FRONT, which DeodhaiXR's event loop does not
			 * handle at all (dropped on the floor) -- the sleeps here used
			 * to wait ~0.5-2s for a compositor round-trip that never
			 * happens, on every single popup dismissal --axiss */
			ChWindowSetFlags(win, (win->flags & ~(WINDOW_FLAG_STATIC)));
			ChWindowSetFocused(win);
			return;
		}
	}

	for (int i = 0; i < win->popup->pointer; i++) {
		ChWindow* popup = (ChWindow*)list_get_at(win->popup, i);
		if (popup->info->hide == 0) {
			return;
		}
	}

	/* first check the title bar bound */
	if (y > win->info->y && y < win->info->y + WINDOW_DEFAULT_TITLEBAR_HEIGHT) {
		for (int i = 0; i < win->GlobalControls->pointer; i++) {
			ChWinGlobalControl* globalCtrl =
				(ChWinGlobalControl*)list_get_at(win->GlobalControls, i);
			if (x > win->info->x + globalCtrl->x &&
				x < (win->info->x + globalCtrl->x + globalCtrl->w) &&
				(y > win->info->y + globalCtrl->y &&
				 y < (win->info->y + globalCtrl->y + globalCtrl->h))) {
				globalCtrl->hover = true;
				if (button)
					globalCtrl->clicked = true;
				if (globalCtrl->ChGlobalMouseEvent)
					globalCtrl->ChGlobalMouseEvent(win, globalCtrl, x, y, button);
			} else {
				if (globalCtrl->hover) {
					globalCtrl->hover = false;
					globalCtrl->clicked = false;
					if (globalCtrl->ChGlobalMouseEvent)
						globalCtrl->ChGlobalMouseEvent(win, globalCtrl, x, y, button);
				}
			}
		}
	}

	if (win->focusedWidget) {
		ChWidget* focusedWidget = (ChWidget*)win->focusedWidget;
		if (focusedWidget->ChMouseEvent)
			focusedWidget->ChMouseEvent(focusedWidget, win, x, y, button);
	} else {
		/* activity area */
		if (y > win->info->y + 26 && y < (win->info->y + 26 + win->info->height - 26)) {
			for (int i = 0; i < win->widgets->pointer; i++) {
				ChWidget* widget = (ChWidget*)list_get_at(win->widgets, i);
				if (!widget->visible)
					continue;
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
		}
	}

	if (!button) {
		win->focusedWidget = NULL;
	}
}

/*
 * ChWindowHandleTouch -- handle touch event
 * @param win -- Pointer to window
 * @param x -- X coord of the touch
 * @param y -- Y coord of the touch
 * @param button -- button state of the touch
 */
XE_EXTERN XE_EXPORT void ChWindowHandleTouch(ChWindow* win, int x, int y, int button) {
	bool _popup_was_active = false;

	if (button != 0) {
		for (int i = 0; i < win->popup->pointer; i++) {
			ChWindow* popup = (ChWindow*)list_get_at(win->popup, i);
			if (popup->info->hide == 0) {
				_KePrint("A popup window is not hidden \r\n");
				/* ChWindowHide flips info->hide in shared memory, which
				 * the compositor reads directly every frame (60fps) --
				 * no IPC round-trip to wait out here --axiss */
				ChWindowHide(popup);
				_popup_was_active = true;
			}
		}
		if (_popup_was_active) {
			/* ChWindowSetFlags/ChWindowSetFocused send DEODHAI_MESSAGE_SET_FLAGS
			 * / WINDOW_BRING_FRONT, which DeodhaiXR's event loop does not
			 * handle at all (dropped on the floor) -- the sleeps here used
			 * to wait ~0.5-2s for a compositor round-trip that never
			 * happens, on every single popup dismissal --axiss */
			ChWindowSetFlags(win, (win->flags & ~(WINDOW_FLAG_STATIC)));
			ChWindowSetFocused(win);
			return;
		}
	}

	for (int i = 0; i < win->popup->pointer; i++) {
		ChWindow* popup = (ChWindow*)list_get_at(win->popup, i);
		if (popup->info->hide == 0) {
			return;
		}
	}

	/* first check the title bar bound */
	if (y > win->info->y && y < win->info->y + WINDOW_DEFAULT_TITLEBAR_HEIGHT) {
		for (int i = 0; i < win->GlobalControls->pointer; i++) {
			ChWinGlobalControl* globalCtrl =
				(ChWinGlobalControl*)list_get_at(win->GlobalControls, i);
			if (x > win->info->x + globalCtrl->x &&
				x < (win->info->x + globalCtrl->x + globalCtrl->w) &&
				(y > win->info->y + globalCtrl->y &&
				 y < (win->info->y + globalCtrl->y + globalCtrl->h))) {
				globalCtrl->hover = true;
				if (button)
					globalCtrl->clicked = true;
				if (globalCtrl->ChGlobalMouseEvent)
					globalCtrl->ChGlobalMouseEvent(win, globalCtrl, x, y, button);
			} else {
				if (globalCtrl->hover) {
					globalCtrl->hover = false;
					globalCtrl->clicked = false;
					if (globalCtrl->ChGlobalMouseEvent)
						globalCtrl->ChGlobalMouseEvent(win, globalCtrl, x, y, button);
				}
			}
		}
	}

	if (win->focusedWidget) {
		ChWidget* focusedWidget = (ChWidget*)win->focusedWidget;
		if (focusedWidget->ChTouchEvent)
			focusedWidget->ChTouchEvent(focusedWidget, win, x, y);
	} else {
		/* activity area */
		if (y > win->info->y + 26 && y < (win->info->y + 26 + win->info->height - 26)) {
			for (int i = 0; i < win->widgets->pointer; i++) {
				ChWidget* widget = (ChWidget*)list_get_at(win->widgets, i);
				if (x > win->info->x + widget->x && x < (win->info->x + widget->x + widget->w) &&
					(y > win->info->y + widget->y && y < (win->info->y + widget->y + widget->h))) {
					widget->hover = true;
					widget->KillFocus = false;
					if (widget->ChTouchEvent)
						widget->ChTouchEvent(widget, win, x, y);
				} else {
					if (widget->hover) {
						widget->hover = false;
						widget->KillFocus = true;
						if (widget->ChTouchEvent)
							widget->ChTouchEvent(widget, win, x, y);
					}
				}
			}
		}
	}

	win->focusedWidget = NULL;
}

/*
 * ChPopupWindowHandleMouse -- Handle popup window's mouse event externally
 * @param win -- Pointer to popup window
 * @param mainWin -- Pointer to main Window
 * @param x -- Mouse x coordinate
 * @param y -- Mouse y coordinate
 * @param button -- Mouse button state
 */
XE_EXTERN XE_EXPORT void ChPopupWindowHandleMouse(ChWindow* win, int x, int y, int button) {
	if (!win)
		return;
	for (int i = 0; i < win->widgets->pointer; i++) {
		ChWidget* pwid = (ChWidget*)list_get_at(win->widgets, i);
		if (x >= (win->info->x + pwid->x) && x < (win->info->x + pwid->x + pwid->w) &&
			y >= (win->info->y + pwid->y) && y < (win->info->y + pwid->y + pwid->h)) {
			pwid->hover = true;
			pwid->KillFocus = false;
			if (pwid->ChMouseEvent)
				pwid->ChMouseEvent(pwid, win, x, y, button);
		} else {
			if (pwid->hover) {
				pwid->hover = false;
				pwid->KillFocus = true;
				if (pwid->ChMouseEvent)
					pwid->ChMouseEvent(pwid, win, x, y, button);
			}
		}
	}

	/*if (button) {
		ChMenuHide((ChPopupMenu*)mainWin->currentPopupMenu);
		_KeProcessSleep(20);
		if (mainWin->selectedMenuItem){
			ChMenuItem* lmi = (ChMenuItem*)mainWin->selectedMenuItem;
			if (lmi->wid.ChActionHandler)
				lmi->wid.ChActionHandler((ChWidget*)lmi, mainWin);
		}
	}*/
}

/*
 * ChWindowHandleFocus -- handle focus changed events
 * @param win -- Pointer to window
 * @param focus_val -- focus bit, 1 -- focused, 0 -- not focused
 * @param handle -- handle number of the window
 */
XE_EXTERN XE_EXPORT void ChWindowHandleFocus(ChWindow* win, bool focus_val, uint32_t handle) {
	win->focused = focus_val;
	ChWindowPaintTitlebar(win);
	ChDrawHorizontalLine(win->canv, 0, 0, win->info->width, GRAY);
	ChDrawVerticalLine(win->canv, 0, 0, 26, GRAY);
	ChDrawVerticalLine(win->canv, win->info->width - 1, 0, 26, GRAY);
	ChWindowUpdate(win, 0, 0, win->info->width, 26, 0, 1);
}

/*
 * ChWindowAddWidget -- adds a widget to window
 * @param win -- Pointer to root window
 * @param wid -- Pointer to widget needs to be added
 */
XE_EXTERN XE_EXPORT void ChWindowAddWidget(ChWindow* win, ChWidget* wid) {
	list_add(win->widgets, wid);
}

/*
 * ChWindowRegisterJump -- register long jump address
 * @param win -- Pointer to main window
 */
XE_EXTERN XE_EXPORT void ChWindowRegisterJump(ChWindow* win) {
	setjmp(win->jump);
}

/*
 * ChWindowSetFlags -- set window flags
 * @param win -- Pointer to window
 * @param flags -- flags to set
 */
XE_EXTERN XE_EXPORT void ChWindowSetFlags(ChWindow* win, uint16_t flags) {
	PostEvent e;
	e.type = DEODHAI_MESSAGE_SET_FLAGS;
	e.dword = win->handle;
	e.dword2 = flags;
	e.to_id = POSTBOX_ROOT_ID;
	e.from_id = win->app->currentID;
	_KeFileIoControl(win->app->postboxfd, POSTBOX_PUT_EVENT, &e);
	win->flags = flags;
}

/*
 * ChWindowMove -- moves target window to a new location
 * @param win -- Pointer to the window
 * @param newX -- new x location relative to screen coord
 * @param newY -- new y location relative to screen coord
 */
XE_EXTERN XE_EXPORT void ChWindowMove(ChWindow* win, int newX, int newY) {
	PostEvent e;
	e.type = DEODHAI_MESSAGE_MOVE_WINDOW;
	e.dword = newX;
	e.dword2 = newY;
	e.to_id = POSTBOX_ROOT_ID;
	e.from_id = win->app->currentID;
	_KeFileIoControl(win->app->postboxfd, POSTBOX_PUT_EVENT, &e);
}

/*
 * ChFreeWindowResources -- free up all allocated resources
 * including global controls, widgets.. etc
 * @param win -- Pointer to window
 */
void ChFreeWindowResources(ChWindow* win) {
	/* free up global controls*/
	for (int i = 0; i < win->GlobalControls->pointer; i++) {
		ChWinGlobalControl* glbl_ = (ChWinGlobalControl*)list_remove(win->GlobalControls, i);
		free(glbl_);
	}
	free(win->GlobalControls);
	_KePrint("Freeing up widgets \r\n");
	for (int i = 0; i < win->widgets->pointer; i++) {
		ChWidget* wid = (ChWidget*)list_get_at(win->widgets, i);
		if (wid->ChDestroy)
			wid->ChDestroy(wid, win);
	}
	_KePrint("Widgets cleared \r\n");
	list_clear_all(win->widgets);

	_KePrint("Window Resource cleared -> %d \r\n", win->widgets->pointer);
	free(win->widgets);
	ChDeAllocateBuffer(win->canv);
	free(win->title);
	free(win->subwindow);
}

XE_EXTERN XE_EXPORT void ChPopupWindowClose(ChWindow* win) {
	/* call the close callback before closing the window*/
	if (win->ChCloseWin)
		win->ChCloseWin(win);

	_KeUnmapSharedMem(win->app->sharedWinkey);
	_KeUnmapSharedMem(win->app->backbufkey);

	/*
 * Close all popup window
 */
	for (int i = 0; i < win->popup->pointer; i++) {
		ChWindow* pw = (ChWindow*)list_get_at(win->popup, i);
		ChPopupWindowClose(pw);
	}
	list_clear_all(win->popup);
	free(win->popup);
	_KePrint("Popup Window closed \r\n");
	/* free up window resources */
	ChFreeWindowResources(win);

	/* send close window command to deodhai */
	int handle = win->handle;
	PostEvent e;
	e.type = DEODHAI_MESSAGE_CLOSE_WINDOW;
	e.dword = handle;
	e.from_id = win->app->currentID;
	e.to_id = POSTBOX_ROOT_ID;
	_KeFileIoControl(win->app->postboxfd, POSTBOX_PUT_EVENT, &e);
	memset(&e, 0, sizeof(PostEvent));

	/* wait for a closed reply window */
	while (1) {
		int err = _KeFileIoControl(win->app->postboxfd, POSTBOX_GET_EVENT, &e);
		if (err == POSTBOX_NO_EVENT)
			_KePauseThread();
		if (e.type == DEODHAI_REPLY_WINDOW_CLOSED) {
			break;
		}
	}

	free(win->app);
	free(win);
}
/*
 * ChWindowCloseWindow -- clears window related data and 
 * sends close message to deodhai
 * @param win -- Pointer to window data
 */
XE_EXTERN XE_EXPORT void ChWindowCloseWindow(ChWindow* win) {
	bool subWindow = false;
	ChWindow* parent = NULL;
	uint16_t flags = win->flags;
	/* call the close callback before closing the window*/
	if (win->ChCloseWin)
		win->ChCloseWin(win);

	/* check if this was a sub window */
	if (win->parent && !(win->flags & WINDOW_FLAG_POPUP)) {
		parent = win->parent;
		subWindow = true;
		for (int i = 0; i < win->parent->subwindow->pointer; i++) {
			ChWindow* win_ = (ChWindow*)list_get_at(win->parent->subwindow, i);
			if (win_ == win) {
				list_remove(win->parent->subwindow, i);
				break;
			}
		}
	}

	/* send close window command to deodhai */
	int handle = win->handle;
	PostEvent e;
	e.type = DEODHAI_MESSAGE_CLOSE_WINDOW;
	e.dword = handle;
	e.from_id = win->app->currentID;
	e.to_id = POSTBOX_ROOT_ID;
	_KeFileIoControl(win->app->postboxfd, POSTBOX_PUT_EVENT, &e);
	memset(&e, 0, sizeof(PostEvent));

	/* wait for a closed reply window */
	while (1) {
		int err = _KeFileIoControl(win->app->postboxfd, POSTBOX_GET_EVENT, &e);
		if (err == POSTBOX_NO_EVENT)
			_KePauseThread();
		if (e.type == DEODHAI_REPLY_WINDOW_CLOSED) {
			break;
		}
	}

	_KePrint("Closed reply received \r\n");

	/* free up window's shared buffers, backbuffer and 
	 * shared window info buffer
	 */
	_KeUnmapSharedMem(win->app->sharedWinkey);
	_KeUnmapSharedMem(win->app->backbufkey);

	/*
	 * Close all popup window
	 */
	for (int i = 0; i < win->popup->pointer; i++) {
		ChWindow* pw = (ChWindow*)list_get_at(win->popup, i);
		ChPopupWindowClose(pw);
		_KePrint("pOPUP CLOSED \r\n");
	}
	list_clear_all(win->popup);
	free(win->popup);
	_KePrint("All popup window closed \r\n");

	/* free up window resources */
	ChFreeWindowResources(win);

	free(win->app);
	free(win);

	_KePrint("Window freed \r\n");
	/* just jump to post event loop of main
	 * window */
	if (subWindow && parent && !(flags & WINDOW_FLAG_POPUP))
		longjmp(parent->jump, 1);

	/* if this window was the main window, simply exit the
	 * application
	 */
#ifdef ARCH_X64
	if (!subWindow && !(flags & WINDOW_FLAG_POPUP))
		_KeProcessExit();
#endif
	_KePrint("about to exit \r\n");
	_KeProcessExit();
}

/*
 * ChWindowGetBackgroundColor -- returns the current
 * background color
 * @param win -- Pointer to Chitralekha Window
 */
uint32_t ChWindowGetBackgroundColor(ChWindow* win) {
	return win->color;
}

/*
 * ChCreatePopupWindow -- *Create a popup window 
 * @param app -- Pointer to application
 * @param win -- Pointer to Window
 * @param x -- X coordinate 
 * @param y -- Y coordinate
 * @param w -- Width of the window
 * @param h -- Height of the window
 * @param type -- type of the window
 */
XE_EXTERN XE_EXPORT ChWindow*
ChCreatePopupWindow(ChWindow* win, int x, int y, int w, int h, uint16_t flags, char* title) {
	if (!(flags & WINDOW_FLAG_POPUP))
		flags |= WINDOW_FLAG_POPUP;
	ChitralekhaApp* app = ChitralekhaStartSubApp(win->app);
	ChRequestWindow(app, (win->info->x + x), (win->info->y + y), w, h, title, flags, win->handle);
	ChWindow* popup = (ChWindow*)malloc(sizeof(ChWindow));
	memset(popup, 0, sizeof(ChWindow));
	ChCanvas* canv = ChCreateCanvas(w, h);
#ifdef __XENEVA_BLEED__
	canv->buffer = (uint32_t*)app->fb;
	canv->bufferSz = 0;
#else
	ChAllocateBuffer(canv);
#endif
	popup->app = app;
	popup->flags = flags;
	popup->buffer = (uint32_t*)app->fb;
	popup->canv = canv;
	popup->sharedwin = app->shwinbuf;
	popup->info = (ChSharedWinInfo*)popup->sharedwin;
	popup->title = (char*)malloc(strlen(title) + 1);
	memset(popup->title, 0, strlen(title) + 1);
	strcpy(popup->title, title);
	popup->info->x = win->info->x + x;
	popup->info->y = win->info->y + y;
	popup->info->width = w;
	popup->info->height = h;
	popup->info->rect_count = 0;
	popup->info->updateEntireWindow = 0;
	popup->info->hide = true;
	popup->handle = app->windowHandle;
	popup->focused = true;
	popup->ChWinPaint = ChDefaultPopupWinPaint;
	popup->widgets = initialize_list();
	popup->popup = initialize_list();
	popup->GlobalControls = initialize_list();
	popup->parent = win;
	list_add(win->popup, popup);
	return popup;
}

/*
 * ChPopupWindowUpdate -- update the popup window
 * @param pw -- Pointer to Chitralekha Popup Window
 * @param x -- X location
 * @param y -- Y location
 * @param w -- Width of the popup window
 * @param h -- Height of the popup window
 */
XE_EXTERN XE_EXPORT void ChPopupWindowUpdate(ChPopupWindow* pw, int x, int y, int w, int h) {
	uint32_t* lfb = pw->buffer;
	uint32_t* canvaddr = pw->canv->buffer;
	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	if (w >= pw->shwin->w)
		w = pw->shwin->w;
	if (h >= pw->shwin->h)
		h = pw->shwin->h;

	if (x > pw->shwin->w)
		return;
	if (y > pw->shwin->h)
		return;

	for (int i = 0; i < h; i++)
		_fastcpy(lfb + (static_cast<int64_t>(y) + i) * pw->shwin->w + x,
				 canvaddr + (static_cast<int64_t>(y) + i) * pw->shwin->w + x,
				 (static_cast<int64_t>(w) * 4));
	pw->shwin->dirty = 1;
}

/*
 * ChPopupWindowShow -- show the popup window
 * @param pw -- Pointer to Popup Window
 * @param win -- Pointer to Chitralekha Main Window
 */
XE_EXTERN XE_EXPORT void ChPopupWindowShow(ChWindow* pw, ChWindow* win) {
	/* info->hide is shared memory the compositor reads every frame
	 * (60fps); DEODHAI_MESSAGE_SET_FLAGS isn't handled by DeodhaiXR at
	 * all, so the second call is currently a no-op on the compositor
	 * side either way. Neither needs a 1s wait --axiss */
	pw->info->hide = 0;
	ChWindowSetFlags(win, (win->flags | WINDOW_FLAG_STATIC));
}

/*
 * ChPopupWindowUpdateLocation -- update the location of popup window relative to
 * main window
 * @param pwin -- Pointer to Popup Window
 * @param win -- Pointer to Main Window
 * @param x -- X location
 * @param y -- Y location
 */
XE_EXTERN XE_EXPORT void ChPopupWindowUpdateLocation(ChWindow* pwin, ChWindow* win, int x, int y) {
	pwin->info->x = win->info->x + x;
	pwin->info->y = win->info->y + y;
}

/*
 * ChPopupWindowHide -- hide the popup window
 * @param pw -- Pointer to Popup Window
 * @param parent -- Root window
 */
XE_EXTERN XE_EXPORT void ChPopupWindowHide(ChWindow* pw, ChWindow* parent) {
	pw->info->hide = true;
}

/*
 * @brief ChWindowHandleDestroyBuffer -- handle destroy buffer
 * message from server
 * @param win -- Pointer to main window
 */
XE_EXTERN XE_EXPORT void ChWindowHandleDestroyBuffer(ChWindow* win) {
	if (!win)
		return;
	_KeUnmapSharedMem(win->app->backbufkey);
	PostEvent e1;
	memset(&e1, 0, sizeof(e1));
	e1.type = DEODHAI_MESSAGE_BUFFER_DESTROYED;
	e1.to_id = POSTBOX_ROOT_ID;
	e1.from_id = win->app->currentID;
	e1.dword = win->app->backbufkey;
	e1.dword4 = win->handle;
	e1.dword5 =
		(win->flags & WINDOW_FLAG_POPUP) ? WINDOW_HANDLE_TYPE_POPUP : WINDOW_HANDLE_TYPE_NORMAL;
	_KeFileIoControl(win->app->postboxfd, POSTBOX_PUT_EVENT, &e1);
}

/**
 * @brief ChWindowHandleReinitBuffer -- reinitialize back buffer
 * with requested size
 * @param win -- Pointer to window
 * @param bufferKey -- shared memory key
 */
void ChWindowHandleReinitBuffer(ChWindow* win, int bufferkey) {
	if (!win)
		return;
	int backbufkey = bufferkey;
	int id = _KeCreateSharedMem(backbufkey, 0, 0);

	void* buffer = _KeObtainSharedMem(id, 0, 0);
	win->app->backbufkey = backbufkey;
	win->app->fb = buffer;
	win->buffer = (uint32_t*)buffer;
	win->info->windowReady = 1;

	_KeProcessSleep(50);
	ChDeAllocateBuffer(win->canv);
	win->canv->canvasWidth = win->info->width;
	win->canv->canvasHeight = win->info->height;
	ChAllocateBuffer(win->canv);
	ChWindowPaint(win);
}
