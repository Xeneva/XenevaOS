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

#include <_xeneva.h>
#include <sys/_keproc.h>
#include <sys/_kefile.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <sys/iocodes.h>
#include <sys/_keipcpostbox.h>
#include <time.h>
#include <string.h>
#include <chitralekha.h>
#include <stdlib.h>
#include <sys/_kesignal.h>
#include "deodhai.h"
#include "cursor.h"
#include "nanojpg.h"
#include "dirty.h"
#include "color.h"
#include <font.h>
#include "window.h"
#include "_fastcpy.h"
#include "clip.h"
#include "backdirty.h"
#include "draw.h"
#include "animation.h"
#include "popup.h"
#include <sys/socket.h>
#include <boxblur.h>
#include <sys/types.h>
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize.h>

/******************************************
*   DEODHAI SUPPORTED WINDOW_TYPES:
*        1> Normal Application Window's
*        2> Always On Top Window's
*        3> Popup Window's
******************************************
*/

Cursor* currentCursor;
int mouse_fd;
int kybrd_fd;
int postbox_fd;
int lastMouseButton;
uint32_t winHandles;
uint32_t* CursorBack;
bool _window_update_all_;
bool _window_broadcast_mouse_;
bool _skip_disable_;
bool _always_on_top_update;
bool _window_moving_;
Window* focusedWin;
Window* focusedLast;
Window* topWin;
Window* dragWin;
Window* reszWin;
Window* rootWin;
Window* lastWin;
Window* alwaysOnTop;
Window* alwaysOnTopLast;
Window* mouseLastHovered;
ChCanvas* canvas;
Cursor* arrow;
Cursor* move;
Cursor* resizeUpDown;
Cursor* resizeLeftRight;
uint32_t* surfaceBuffer;
bool _shadow_update;
bool _clients_advice;
int gpu_fd;
int gpu_display_id;
bool _gpu_enabled;
uint64_t startTime;
uint64_t startSubTime;

void CursorStoreBack(ChCanvas* canv, Cursor* cur, unsigned x, unsigned y);
void CursorDrawBack(ChCanvas* canv, Cursor* cur, unsigned x, unsigned y);
void ChangeCursor(Cursor* newCursor);

/*
 * DeodhaiInitialiseData -- initialise all data
 */
void DeodhaiInitialiseData() {
	_window_update_all_ = false;
	_window_broadcast_mouse_ = false;
	focusedWin = focusedLast = topWin = dragWin = NULL;
	reszWin = rootWin = lastWin = NULL;
	canvas = NULL;
	lastMouseButton = 0;
	alwaysOnTop = NULL;
	alwaysOnTopLast = NULL;
	winHandles = 100;
}

/*
 * DeodhaiAllocateNewHandle -- get a new window handle
 */
uint32_t DeodhaiAllocateNewHandle() {
	uint32_t handle = winHandles;
	winHandles += 1;
	return handle;
}
/*
 * DeodhaiAddWindow -- add a window to window list
 * @param win -- Pointer to window
 */
void DeodhaiAddWindow(Window* win) {
	win->next = NULL;
	win->prev = NULL;
	if (rootWin == NULL) {
		rootWin = win;
		lastWin = win;
	} else {
		lastWin->next = win;
		win->prev = lastWin;
		lastWin = win;
	}
}

void DeodhaiRemoveWindow(Window* win) {
	if (rootWin == NULL)
		return;

	if (win == rootWin)
		rootWin = rootWin->next;
	else
		win->prev->next = win->next;

	if (win == lastWin)
		lastWin = win->prev;
	else
		win->next->prev = win->prev;
}

/*
* DeodhaiAddWindow -- add a window to window list
* @param win -- Pointer to window
*/
void DeodhaiAddWindowAlwaysOnTop(Window* win) {
	win->next = NULL;
	win->prev = NULL;
	if (alwaysOnTop == NULL) {
		alwaysOnTop = win;
		alwaysOnTopLast = win;
	} else {
		alwaysOnTopLast->next = win;
		win->prev = alwaysOnTopLast;
		alwaysOnTopLast = win;
	}
}

void DeodhaiRemoveWindowAlwaysOnTop(Window* win) {
	if (alwaysOnTop == NULL)
		return;

	if (win == alwaysOnTop)
		alwaysOnTop = alwaysOnTop->next;
	else
		win->prev->next = win->next;

	if (win == alwaysOnTopLast)
		alwaysOnTopLast = win->prev;
	else
		win->next->prev = win->prev;
}

/*
 * DeodhaiBackSurfaceUpdate -- update the back surface
 */
void DeodhaiBackSurfaceUpdate(ChCanvas* canv, int x, int y, int w, int h) {
	uint32_t* lfb = (uint32_t*)canv->buffer;
	uint32_t* wallp = (uint32_t*)surfaceBuffer;

	int64_t x_ = x, y_ = y, w_ = w, h_ = h;

	if (w > canv->canvasWidth)
		w = canv->canvasWidth;

	if (h > canv->canvasHeight)
		h = canv->canvasHeight;

	if (x > canv->canvasWidth)
		return;

	if (y > canv->canvasHeight)
		return;

	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;

	for (int j = 0; j < h; j++) {
		_fastcpy(canv->buffer + (y_ + j) * canv->canvasWidth + x_,
				 wallp + (y_ + j) * canv->canvasWidth + x_,
				 w_ * 4);
	}
}

/*
 * DeodhaiCreateWindow -- create a new deodhai window
 */
Window*
DeodhaiCreateWindow(int x, int y, int w, int h, uint16_t flags, uint16_t ownerId, char* title) {
	Window* win = CreateWindow(x, y, w, h, flags, ownerId, title);
	if (flags & WINDOW_FLAG_ALWAYS_ON_TOP) {
		DeodhaiAddWindowAlwaysOnTop(win);
	} else
		DeodhaiAddWindow(win);
	return win;
}

/*
* DeodhaiBrodcastMessage -- broadcast a message to every window
* @param e -- PostEvent to broadcast
*/
void DeodhaiBroadcastMessage(PostEvent* e, Window* skippablewin) {
	for (Window* win = rootWin; win != NULL; win = win->next) {
		if (skippablewin && win == skippablewin)
			continue;
		if (win->flags & WINDOW_FLAG_BROADCAST_LISTENER) {
			e->to_id = win->ownerId;
			_KeFileIoControl(postbox_fd, POSTBOX_PUT_EVENT, e);
		}
	}

	for (Window* win = alwaysOnTop; win != NULL; win = win->next) {
		if (skippablewin && win == skippablewin)
			continue;
		if (win->flags & WINDOW_FLAG_BROADCAST_LISTENER) {
			e->to_id = win->ownerId;
			_KeFileIoControl(postbox_fd, POSTBOX_PUT_EVENT, e);
		}
	}
}

/*
 * DeodhaiSendFocusMessage -- send focus changed message to
 * each and every windows
 * @param e -- Pointer to post event message
 */
void DeodhaiSendFocusMessage(PostEvent* e) {
	for (Window* win = rootWin; win != NULL; win = win->next) {
		e->to_id = win->ownerId;
		if (focusedWin == win) {
			e->dword = 1;
			e->dword2 = win->handle;
		} else {
			e->dword = 0;
			e->dword2 = win->handle;
		}
		_KeFileIoControl(postbox_fd, POSTBOX_PUT_EVENT, e);
		//_KeProcessSleep(1);
	}
}

/* DeodhaiWindowMakeTop -- brings a window to front
 * @param win -- window to brin front
 */
void DeodhaiWindowMakeTop(Window* win) {
	if (win->flags & WINDOW_FLAG_STATIC)
		return;
	if (rootWin == win && lastWin == win)
		return;

	DeodhaiRemoveWindow(win);
	DeodhaiAddWindow(win);

#ifdef SHADOW_ENABLE
	/* add a back dirty rect to behind windows, because of
	* its shadows to be undrawn
	*/
	for (Window* back = rootWin; back != NULL; back = back->next) {
		WinSharedInfo* backinfo = (WinSharedInfo*)back->sharedInfo;
		if (back == win)
			break;
		int x = backinfo->x - SHADOW_SIZE;
		int y = backinfo->y - SHADOW_SIZE;
		int w = backinfo->width + SHADOW_SIZE * 2;
		int h = backinfo->height + SHADOW_SIZE * 2;
		if (x < 0)
			x = 0;
		if (y < 0)
			y = 0;

		BackDirtyAdd((backinfo->x - SHADOW_SIZE),
					 (backinfo->y - SHADOW_SIZE),
					 (backinfo->width + SHADOW_SIZE * 2),
					 (backinfo->height + SHADOW_SIZE * 2));
	}
#endif
}

/*
 * DeodhaiWindowSetFocused -- cast focus to a new window
 */
void DeodhaiWindowSetFocused(Window* win, bool notify) {
	if (focusedWin == win)
		return;
	focusedWin = win;
	_shadow_update = true;
	WinSharedInfo* info = (WinSharedInfo*)focusedWin->sharedInfo;
	if (info->hide) {
		info->hide = false;
		_window_update_all_ = true;
		_always_on_top_update = true;
	}

	if (notify && !(win->flags & WINDOW_FLAG_POPUP)) {
		PostEvent e;
		e.type = DEODHAI_BROADCAST_FOCUS_CHANGED;
		e.dword = focusedWin->ownerId;
		DeodhaiBroadcastMessage(&e, NULL);

		//_KeProcessSleep();

		e.type = DEODHAI_REPLY_FOCUS_CHANGED;
		DeodhaiSendFocusMessage(&e);
	}

	DeodhaiWindowMakeTop(win);
}

/*
 * DeodhaiDrawShadow -- draws box shadow around the
 * window -- not completed
 */
void DeodhaiDrawShadow(ChCanvas* canv, Window* win) {
	WinSharedInfo* info = (WinSharedInfo*)win->sharedInfo;
}

/*
 * DeodhaiRemoveShadow -- remove unfocussed winodw's 
 * shadow
 * @param canv -- Pointer to deodhai canvas
 * @param win -- Pointer to unfoccused window
 */
void DeodhaiRemoveShadow(ChCanvas* canv, Window* win) {
	WinSharedInfo* info = (WinSharedInfo*)win->sharedInfo;
	int s_x = info->x - 6;
	int s_y = info->y + 26;

	BackDirtyAdd(s_x, s_y, 6, info->height);
}
/*
 * DeodhaiWindowMove -- move an window to a new location
 * @param win -- Pointer to window
 * @param x -- new x position
 * @param y -- new y position
 */
void DeodhaiWindowMove(Window* win, int x, int y) {
	if (_clients_advice)
		goto _move_win;

	if (win->flags & WINDOW_FLAG_STATIC)
		return;

	if (win->flags & WINDOW_FLAG_ALWAYS_ON_TOP)
		return;

	if (win->flags & WINDOW_FLAG_POPUP)
		return;

	if (focusedWin != win)
		DeodhaiWindowSetFocused(win, true);

_move_win:

	_window_moving_ = true;

	WinSharedInfo* info = (WinSharedInfo*)win->sharedInfo;
	int wx = info->x - SHADOW_SIZE;
	int wy = info->y - SHADOW_SIZE;
	int ww = info->width + SHADOW_SIZE * 2;
	int wh = info->height + SHADOW_SIZE * 2;

	if (wx > canvas->screenWidth)
		return;

	if (wy > canvas->screenHeight)
		return;

	if (wx <= 0)
		wx = SHADOW_SIZE + 5;

	if (wy <= 0)
		wy = SHADOW_SIZE + 5;

	if ((wx + ww) >= canvas->screenWidth)
		ww = canvas->screenWidth - info->x + SHADOW_SIZE;

	if ((wy + wh) >= canvas->screenHeight)
		wh = canvas->screenHeight - info->y + SHADOW_SIZE;
	BackDirtyAdd(wx, wy, ww, wh);
_skip:
	if (x <= 0)
		x = 0;
	if (y < 0)
		y = 0;
	if (x >= canvas->screenWidth)
		x = canvas->screenWidth - ww;
	if (y >= canvas->screenHeight)
		y = canvas->screenHeight - wh;

	info->x = x;
	info->y = y;
	_window_update_all_ = true;
	_always_on_top_update = true;
	_shadow_update = true;
	_clients_advice = false;
	//currentCursor = arrow;
}

/*
 * DeodhaiWindowHide -- hides a window
 * @param win -- Pointer to window to hide
 */
void DeodhaiWindowHide(Window* win) {
	WinSharedInfo* info = (WinSharedInfo*)win->sharedInfo;
	BackDirtyAdd(info->x, info->y, info->width, info->height);
	if (info->hide) {
		/* UNHIDE the window , if its already hidden */
		info->hide = false;
		info->updateEntireWindow = true;
		info->dirty = 1;
		focusedWin = win;
	} else {
		/* HIDE the window, if its not hidden */
		info->hide = true;
		focusedWin = NULL;
	}

	_window_update_all_ = true;
	_always_on_top_update = true;
}

/*
 * DeodhaiCheckWindowPointOcclusion -- checks if given x and y of point
 * of a window is occluded by another window , from given window to 
 * front window of the list
 * @param win -- Pointer to window to use for point check
 * @param x -- x coord of the point
 * @param y -- y coord of the point
 */
bool DeodhaiCheckWindowPointOcclusion(Window* win, int x, int y) {
	bool occluded = false;
	for (Window* check = win; check != NULL; check = check->next) {
		WinSharedInfo* info = (WinSharedInfo*)check->sharedInfo;
		if (check == win)
			continue;
		if (x >= info->x && x < (info->x + info->width) && y >= info->y &&
			y < (info->y + info->height)) {
			occluded = true;
			break;
		}
	}

	for (Window* check = alwaysOnTop; check != NULL; check = check->next) {
		WinSharedInfo* info = (WinSharedInfo*)check->sharedInfo;
		if (check == win)
			continue;
		if (info->hide)
			continue;
		if (x >= info->x && x < (info->x + info->width) && y >= info->y &&
			y < (info->y + info->height)) {
			occluded = true;
			break;
		}
	}
	return occluded;
}
/*
 * DeodhaiWindowCheckDraggable -- check for draggable windows
 * @param x -- mouse x pos
 * @param y -- mouse y pos
 * @param button -- mouse button state
 */
void DeodhaiWindowCheckDraggable(int x, int y, int button) {
	for (Window* win = lastWin; win != NULL; win = win->prev) {
		WinSharedInfo* info = (WinSharedInfo*)win->sharedInfo;
		//_KePrint("INFO->x %d, mx -> %d \r\n", info->x, x);
		if (!(x >= (info->x + DeodhaiDragZoneX0(win->flags)) &&
			  x < (info->x + DeodhaiDragZoneX1(win->flags, info->width)) && y >= info->y &&
			  y < (info->y + info->height)))
			continue;

		if (button && !lastMouseButton) {
			if (y >= info->y && y < (info->y + 26)) {
				/* check if the point is occluded */
				if (DeodhaiCheckWindowPointOcclusion(win, x, y))
					return;
				if (win->flags & WINDOW_FLAG_STATIC)
					return;
				DeodhaiWindowSetFocused(win, true);
				dragWin = win;
				dragWin->dragX = x - info->x;
				dragWin->dragY = y - info->y;
				break;
			}
		}
	}

	if (dragWin) {
		_window_broadcast_mouse_ = false;
		WinSharedInfo* winInfo = (WinSharedInfo*)dragWin->sharedInfo;
		int posx = x - dragWin->dragX;
		int posy = y - dragWin->dragY;
		ChangeCursor(move);
		DeodhaiWindowMove(dragWin, posx, posy);
	}

	if (!button) {
		if (dragWin)
			ChangeCursor(arrow);
		dragWin = NULL;
		reszWin = NULL;
		_window_broadcast_mouse_ = true;
	}

	lastMouseButton = button;
}

void CursorStoreBack(ChCanvas* canv, Cursor* cur, unsigned x, unsigned y) {
	for (int w = 0; w < 24; w++) {
		for (int h = 0; h < 24; h++) {
			cur->cursorBack[h * 24 + w] = ChGetPixel(canv, x + w, y + h);
		}
	}
}

void CursorDrawBack(ChCanvas* canv, Cursor* cur, unsigned x, unsigned y) {
	for (int w = 0; w < 24; w++) {
		for (int h = 0; h < 24; h++) {
			ChDrawPixelRAW(canv, x + w, y + h, cur->cursorBack[h * 24 + w]);
		}
	}
}

void ChangeCursor(Cursor* newCursor) {
	if (!newCursor)
		return;
	newCursor->xpos = currentCursor->xpos;
	newCursor->ypos = currentCursor->ypos;
	newCursor->oldXPos = currentCursor->oldXPos;
	newCursor->oldYPos = currentCursor->oldYPos;
	memcpy(newCursor->cursorBack, currentCursor->cursorBack, 24 * 24 * 32);
	currentCursor = newCursor;
}

/* ComposeFrame -- composes a single frame 
 * @param canvas -- Pointer to canvas data structure
 */
void ComposeFrame(ChCanvas* canvas) {
	CursorDrawBack(canvas, currentCursor, currentCursor->oldXPos, currentCursor->oldYPos);
	AddDirtyClip(currentCursor->oldXPos, currentCursor->oldYPos, 24, 24);

	int _back_d_count_ = BackDirtyGetDirtyCount();

	/* here we redraw all dirty surface area*/
	if (_back_d_count_ > 0) {
		int x, y, w, h = 0;
		for (int i = 0; i < _back_d_count_; i++) {
			BackDirtyGetRect(&x, &y, &w, &h, i);
			DeodhaiBackSurfaceUpdate(canvas, x, y, w, h);
			AddDirtyClip(x, y, w, h);
		}
		BackDirtyCountReset();
	}

	for (Window* win = rootWin; win != NULL; win = win->next) {
		WinSharedInfo* info = (WinSharedInfo*)win->sharedInfo;

		if (info->hide)
			continue;

		/*
		 * Check for small area updates !! not entire window 
		 */
		if ((info->rect_count > 0) && (info->dirty)) {
			for (int k = 0; k < info->rect_count; k++) {
				int64_t r_x = info->rect[k].x;
				int64_t r_y = info->rect[k].y;
				int64_t r_w = info->rect[k].w;
				int64_t r_h = info->rect[k].h;

				if (r_x < 0)
					r_x = 0;

				if (r_y < 0)
					r_y = 0;

				if ((info->x + r_x + r_w) >= canvas->canvasWidth)
					r_w = canvas->canvasWidth - (info->x + r_x);

				if ((info->y + r_y + r_h) >= canvas->canvasHeight)
					r_h = canvas->canvasHeight - (info->y + r_y);

				/* from here, we check if the small rectangle is
				 * covered by a window or another rectangle */

				Rect r1;
				Rect r2;
				r1.x = info->x + r_x;
				r1.y = info->y + r_y;
				r1.w = r_w;
				r1.h = r_h;
				bool overlap = false;
				Rect clipRect[100];
				int clipCount = 0;
				Window* clipWin = NULL;
				WinSharedInfo* clipInfo = NULL;

				if (info->alpha) {
					for (int j = 0; j < r_h; j++) {
						for (int i = 0; i < r_w; i++) {
							*(uint32_t*)(canvas->buffer +
										 (static_cast<int64_t>(info->y) + r_y + j) *
											 canvas->canvasWidth +
										 (static_cast<int64_t>(info->x) + r_x + i)) =
								ChColorAlphaBlend(
									*(uint32_t*)(canvas->buffer +
												 (static_cast<int64_t>(info->y) + r_y + j) *
													 canvas->canvasWidth +
												 (static_cast<int64_t>(info->x) + r_x + i)),
									*(uint32_t*)(win->backBuffer +
												 (static_cast<int64_t>(r_y) + j) * info->width +
												 (static_cast<int64_t>(r_x) + i)),
									info->alphaValue);
						}
					}
					AddDirtyClip(info->x + r_x, info->y + r_y, r_w, r_h);
				} else {
					if (focusedWin != win) {
						/* first check for normal windows */
						for (clipWin = win; clipWin != NULL; clipWin = clipWin->next) {
							clipInfo = (WinSharedInfo*)clipWin->sharedInfo;
							if (clipWin == win)
								continue;
							r2.x = clipInfo->x;
							r2.y = clipInfo->y;
							r2.w = clipInfo->width;
							r2.h = clipInfo->height;

							if (ClipCheckIntersect(&r1, &r2)) {
								overlap = true;
								ClipCalculateRect(&r1, &r2, clipRect, &clipCount);
							}
						}

						///* check for always on top windows */
						for (clipWin = alwaysOnTop; clipWin != NULL; clipWin = clipWin->next) {
							clipInfo = (WinSharedInfo*)clipWin->sharedInfo;
							if (clipInfo->hide)
								continue;
							r2.x = clipInfo->x;
							r2.y = clipInfo->y;
							r2.w = clipInfo->width;
							r2.h = clipInfo->height;

							if (ClipCheckIntersect(&r1, &r2)) {
								clipInfo->updateEntireWindow = 1;
							}
						}

						for (Window* cutt = win; cutt != NULL; cutt = cutt->next) {
							WinSharedInfo* cuttinfo = (WinSharedInfo*)cutt->sharedInfo;
							if (cutt == win)
								continue;
							Rect cuttingr;
							cuttingr.x = cuttinfo->x;
							cuttingr.y = cuttinfo->y;
							cuttingr.w = cuttinfo->width;
							cuttingr.h = cuttinfo->height;
							for (int m = 0; m < clipCount; m++) {
								Rect cu;
								cu.x = clipRect[m].x;
								cu.y = clipRect[m].y;
								cu.w = clipRect[m].w;
								cu.h = clipRect[m].h;
								if (cuttingr.x <= (cu.x + cu.w - 1) &&
									(cuttingr.x + cuttingr.w - 1) >= cu.x &&
									cuttingr.y <= (cu.y + cu.h - 1) &&
									(cuttingr.y + cuttingr.h - 1) >= cu.y) {
									ClipSubtractRect(&cu, &cuttingr, clipRect, m);
								}
							}
						}
					}

					if (clipCount == 0 && !overlap) {
						for (int i = 0; i < r_h; i++) {
							void* canvas_mem =
								(canvas->buffer + (info->y + r_y + i) * canvas->canvasWidth +
								 info->x + r_x);
							void* win_mem = (win->backBuffer + (r_y + i) * info->width + r_x);
							_fastcpy(canvas_mem, win_mem, static_cast<size_t>(r_w) * 4);
						}
						AddDirtyClip(info->x + r_x, info->y + r_y, r_w, r_h);
					}

					for (int l = 0; l < clipCount; l++) {
						int64_t k_x = clipRect[l].x;
						int64_t k_y = clipRect[l].y;
						int64_t k_w = clipRect[l].w;
						int64_t k_h = clipRect[l].h;

						if (k_x < 0)
							k_x = 0;
						if (k_y < 0)
							k_y = 0;
						if ((k_x + k_w) >= canvas->screenWidth)
							k_w = canvas->screenWidth - k_x;
						if ((k_y + k_h) >= canvas->screenHeight)
							k_h = canvas->screenHeight - k_y;

						int offset_x = info->x + r_x;

						int diffx = k_x - offset_x;
						int64_t update_r_x = r_x + diffx;

						int offset_y = info->y + r_y;
						int diffy = k_y - offset_y;
						int64_t update_r_y = r_y + diffy;

						for (int64_t j = 0; j < k_h; j++) {
							void* canvas_mem =
								(canvas->buffer + (k_y + j) * canvas->canvasWidth + k_x);
							void* win_mem =
								(win->backBuffer + (update_r_y + j) * info->width + update_r_x);
							_fastcpy(canvas_mem, win_mem, k_w * 4);
						}

						AddDirtyClip(k_x, k_y, k_w, k_h);
					}

					clipCount = 0;
					info->rect[k].x = 0, info->rect[k].y = 0, info->rect[k].w = 0,
					info->rect[k].h = 0;
				}
			}
			info->rect_count = 0;
			info->dirty = 0;
			info->updateEntireWindow = 0;
		}

		/* If no small areas, update entire window */

		if (win != NULL && _window_update_all_ ||
			(info->rect_count == 0 && info->updateEntireWindow == 1)) {
			int64_t winx = 0;
			int64_t winy = 0;
			winx = info->x;
			winy = info->y;

			int64_t width = info->width;
			int64_t height = info->height;
			int64_t shad_w = width + SHADOW_SIZE * 2;
			int64_t shad_h = height + SHADOW_SIZE * 2;

			if ((info->x + info->width) >= canvas->screenWidth)
				width = static_cast<int64_t>(canvas->screenWidth) - info->x;

			if ((info->y + info->height) >= canvas->screenHeight)
				height = static_cast<int64_t>(canvas->screenHeight) - info->y;

			if ((info->x - SHADOW_SIZE) <= 0) {
				info->x = 5 + SHADOW_SIZE;
				winx = info->x;
			}

			if ((info->y - SHADOW_SIZE) <= 0) {
				info->y = 5 + SHADOW_SIZE;
				winy = info->y;
			}

			if ((info->x + 24) >= canvas->screenWidth)
				info->x = canvas->screenWidth - 24;

			if ((info->y + 24) >= canvas->screenHeight)
				info->y = canvas->screenHeight - 24;

#ifdef SHADOW_ENABLED
			if (((static_cast<int64_t>(info->x) - SHADOW_SIZE) + shad_w) >= canvas->screenWidth)
				shad_w = static_cast<int64_t>(canvas->screenWidth) -
						 (static_cast<int64_t>(info->x) - SHADOW_SIZE);

			if (((static_cast<int64_t>(info->y) - SHADOW_SIZE) + shad_h) >= canvas->screenHeight)
				shad_h = static_cast<int64_t>(canvas->screenHeight) -
						 (static_cast<int64_t>(info->y) - SHADOW_SIZE);
#endif
			if ((win->flags & WINDOW_FLAG_ANIMATED)) {
				if (win->flags & WINDOW_FLAG_ANIMATION_FADE_IN)
					FadeInAnimationWindow(canvas, win, info, winx, winy, shad_w, shad_h);

				if (win->flags & WINDOW_FLAG_ANIMATION_FADE_OUT)
					FadeOutAnimationWindow(canvas, win, info, winx, winy, shad_w, shad_h);
			} else {
				Rect r1;
				Rect r2;
				r1.x = winx - SHADOW_SIZE;
				r1.y = winy - SHADOW_SIZE;
				r1.w = width + SHADOW_SIZE * 2;
				r1.h = height + SHADOW_SIZE * 2;

				Rect clip[100];
				int clipCount = 0;
				Window* clipWin = NULL;
				WinSharedInfo* clipInfo = NULL;

				if (info->alpha) {
					for (int j = 0; j < height; j++) {
						for (int i = 0; i < width; i++) {
							*(uint32_t*)(canvas->buffer +
										 (static_cast<int64_t>(info->y) + j) * canvas->canvasWidth +
										 (static_cast<int64_t>(info->x) + i)) =
								ChColorAlphaBlend(
									*(uint32_t*)(canvas->buffer +
												 (static_cast<int64_t>(info->y) + j) *
													 canvas->canvasWidth +
												 (static_cast<int64_t>(info->x) + i)),
									*(uint32_t*)(win->backBuffer +
												 (static_cast<int64_t>(0) + j) * info->width +
												 (static_cast<int64_t>(0) + i)),
									info->alphaValue);
						}
					}
					AddDirtyClip(info->x, info->y, width, height);

				} else {
					for (clipWin = win; clipWin != NULL; clipWin = clipWin->next) {
						clipInfo = (WinSharedInfo*)clipWin->sharedInfo;
						if (clipWin == win)
							continue;

						r2.x = clipInfo->x;
						r2.y = clipInfo->y;
						r2.w = clipInfo->width;
						r2.h = clipInfo->height;

						if (ClipCheckIntersect(&r1, &r2)) {
							ClipCalculateRect(&r1, &r2, clip, &clipCount);
						}
					}

					/* always on top list */
					for (clipWin = alwaysOnTop; clipWin != NULL; clipWin = clipWin->next) {
						clipInfo = (WinSharedInfo*)clipWin->sharedInfo;
						if (clipWin == win)
							continue;
						r2.x = clipInfo->x;
						r2.y = clipInfo->y;
						r2.w = clipInfo->width;
						r2.h = clipInfo->height;

						if (ClipCheckIntersect(&r1, &r2)) {
							ClipCalculateRect(&r1, &r2, clip, &clipCount);
						}
					}

					if (focusedWin == win) {
						if (_shadow_update) {
#ifdef SHADOW_ENABLED
							for (int64_t j = 0; j < shad_h; j++) {
								for (int64_t q = 0; q < shad_w; q++) {
									*(uint32_t*)(canvas->buffer +
												 ((winy - SHADOW_SIZE) + j) * canvas->canvasWidth +
												 ((winx - SHADOW_SIZE) + q)) =
										ChColorAlphaBlend2(
											*(uint32_t*)(canvas->buffer +
														 ((winy - SHADOW_SIZE) + j) *
															 canvas->canvasWidth +
														 ((winx - SHADOW_SIZE) + q)),
											*(uint32_t*)(win->shadowBuffers +
														 j * (static_cast<int64_t>(info->width) +
															  SHADOW_SIZE * 2) +
														 q));
								}
							}
#endif
							_shadow_update = false;
						}
					}

					for (int64_t i = 0; i < height; i++) {
						_fastcpy(canvas->buffer + (winy + i) * canvas->canvasWidth + winx,
								 win->backBuffer + (0 + i) * info->width + 0,
								 width * 4);
					}

					/*
					 * Here we check the moving bit because, if any behind
					 * windows is not intersected by moving window, so during
					 * _window_update_all_ process its clipped rect count will
					 * be zero, so moving window prevents its from redrawing
					 * non intersected window with clip count = 0
					 */
					if (clipCount == 0 && !_window_moving_) {
						AddDirtyClip(winx - SHADOW_SIZE, winy - SHADOW_SIZE, shad_w, shad_h);
					}

					for (int k = 0; k < clipCount; k++) {
						int k_x = clip[k].x;
						int k_y = clip[k].y;
						int k_w = clip[k].w;
						int k_h = clip[k].h;

						if (k_x < 0)
							k_x = 0;
						if (k_y < 0)
							k_y = 0;
						if ((k_x + k_w) >= canvas->screenWidth)
							k_w = canvas->screenWidth - k_x;
						if ((k_y + k_h) >= canvas->screenHeight)
							k_h = canvas->screenHeight - k_y;

						AddDirtyClip(k_x, k_y, k_w, k_h);
					}
					clipCount = 0;
				}
				if (!(win->flags & WINDOW_FLAG_ANIMATED)) {
					if (info->updateEntireWindow)
						info->updateEntireWindow = 0;
					if (!info->windowReady)
						info->windowReady = 1;
				}
			}
		}
	}

	/* //------- Always on Top Windows follow, another data structure ----------// */
	/* // ----------------------------------------------------------------------// */

	for (Window* win = alwaysOnTop; win != NULL; win = win->next) {
		WinSharedInfo* info = (WinSharedInfo*)win->sharedInfo;
		if (info->hide)
			continue;
		/*
		* Check for small area updates !! not entire window
		*/
		if (info->rect_count > 0) {
			for (int k = 0; k < info->rect_count; k++) {
				int r_x = info->rect[k].x;
				int r_y = info->rect[k].y;
				int r_w = info->rect[k].w;
				int r_h = info->rect[k].h;

				if (r_x < 0)
					r_x = 0;

				if (r_y < 0)
					r_y = 0;

				if ((info->x + r_x + r_w) >= canvas->canvasWidth)
					r_w = canvas->canvasWidth - (info->x + r_x);

				if ((info->y + r_y + r_h) >= canvas->canvasHeight)
					r_h = canvas->canvasHeight - (info->y + r_y);

				/* from here, we check if the small rectangle is
				* covered by a window or another rectangle */

				Rect r1;
				Rect r2;
				r1.x = info->x + r_x;
				r1.y = info->y + r_y;
				r1.w = r_w;
				r1.h = r_h;
				bool overlap = false;
				Rect clipRect[100];
				int clipCount = 0;
				Window* clipWin = NULL;
				WinSharedInfo* clipInfo = NULL;

				if (info->alpha && !_window_moving_) {
					for (int j = 0; j < r_h; j++) {
						for (int i = 0; i < r_w; i++) {
							*(uint32_t*)(canvas->buffer +
										 (static_cast<int64_t>(info->y) + r_y + j) *
											 canvas->canvasWidth +
										 (static_cast<int64_t>(info->x) + r_x + i)) =
								ChColorAlphaBlend2(
									*(uint32_t*)(surfaceBuffer +
												 (static_cast<int64_t>(info->y) + r_y + j) *
													 canvas->canvasWidth +
												 (static_cast<int64_t>(info->x) + r_x + i)),
									*(uint32_t*)(win->backBuffer +
												 (static_cast<int64_t>(r_y) + j) * info->width +
												 (static_cast<int64_t>(r_x) + i)));
						}
					}
					AddDirtyClip(info->x + r_x, info->y + r_y, r_w, r_h);
				} else {
					if (focusedWin != win) {
						for (clipWin = win; clipWin != NULL; clipWin = clipWin->next) {
							clipInfo = (WinSharedInfo*)clipWin->sharedInfo;
							if (clipWin == win)
								continue;
							r2.x = clipInfo->x;
							r2.y = clipInfo->y;
							r2.w = clipInfo->width;
							r2.h = clipInfo->height;

							if (ClipCheckIntersect(&r1, &r2)) {
								overlap = true;
								ClipCalculateRect(&r1, &r2, clipRect, &clipCount);
							}
						}
					}

					if (clipCount == 0 && !overlap) {
						for (int i = 0; i < r_h; i++) {
							void* canv_buff =
								(canvas->buffer +
								 (static_cast<int64_t>(info->y) + r_y + i) * canvas->canvasWidth +
								 info->x + r_x);
							void* backbuff = (win->backBuffer +
											  (static_cast<int64_t>(r_y) + i) * info->width + r_x);
							_fastcpy(canv_buff, backbuff, static_cast<int64_t>(r_w) * 4);
						}
						AddDirtyClip(info->x + r_x, info->y + r_y, r_w, r_h);
					}

					for (int l = 0; l < clipCount; l++) {
						int k_x = clipRect[l].x;
						int k_y = clipRect[l].y;
						int k_w = clipRect[l].w;
						int k_h = clipRect[l].h;

						if (k_x < 0)
							k_x = 0;
						if (k_y < 0)
							k_y = 0;
						if ((k_x + k_w) >= canvas->screenWidth)
							k_w = canvas->screenWidth - k_x;
						if ((k_y + k_h) >= canvas->screenHeight)
							k_h = canvas->screenHeight - k_y;

						int offset_x = info->x + r_x;

						int diffx = k_x - offset_x;
						int update_r_x = r_x + diffx;

						int offset_y = info->y + r_y;
						int diffy = k_y - offset_y;
						int update_r_y = r_y + diffy;

						for (int j = 0; j < k_h; j++) {
							_fastcpy((canvas->buffer +
									  (static_cast<int64_t>(k_y) + j) * canvas->canvasWidth + k_x),
									 (win->backBuffer +
									  (static_cast<int64_t>(update_r_y) + j) * info->width +
									  update_r_x),
									 static_cast<int64_t>(k_w) * 4);
						}
						AddDirtyClip(k_x, k_y, k_w, k_h);
					}
					clipCount = 0;
				}
			}
			info->rect_count = 0;
			info->dirty = 0;
			info->updateEntireWindow = 0;
		}

		/* If no small areas, update entire window */

		if (win != NULL && _always_on_top_update ||
			(info->rect_count == 0 && info->updateEntireWindow == 1)) {
			int winx = 0;
			int winy = 0;
			winx = info->x;
			winy = info->y;

			int width = info->width;
			int height = info->height;

			if (info->x < 0) {
				info->x = 5;
				winx = info->x;
			}

			if (info->y < 0) {
				info->y = 5;
				winy = info->y;
			}

			if (info->x + info->width >= canvas->screenWidth)
				width = canvas->screenWidth - info->x;

			if (info->y + info->height >= canvas->screenHeight)
				height = canvas->screenHeight - info->y;

			Rect r1;
			Rect r2;
			r1.x = winx;
			r1.y = winy;
			r1.w = width;
			r1.h = height;

			Rect clip[100];
			int clipCount = 0;
			Window* clipWin = NULL;
			WinSharedInfo* clipInfo = NULL;
			bool _intersected_ = false;

			for (clipWin = rootWin; clipWin != NULL; clipWin = clipWin->next) {
				clipInfo = (WinSharedInfo*)clipWin->sharedInfo;
				if (clipWin == win)
					continue;
				r2.x = clipInfo->x;
				r2.y = clipInfo->y;
				r2.w = clipInfo->width;
				r2.h = clipInfo->height;

				if (ClipCheckIntersect(&r1, &r2)) {
					_intersected_ = true;
				}
			}

			/* alpha is only used for fade animation right now */
			if ((info->alpha && info->updateEntireWindow) || (info->alpha && _intersected_)) {
				for (int j = 0; j < height; j++) {
					for (int i = 0; i < width; i++) {
						*(uint32_t*)(canvas->buffer +
									 (static_cast<int64_t>(winy) + j) * canvas->canvasWidth +
									 (static_cast<int64_t>(winx) + i)) =
							ChColorAlphaBlend2(*(uint32_t*)(surfaceBuffer +
															(static_cast<int64_t>(winy) + j) *
																canvas->canvasWidth +
															(static_cast<int64_t>(winx) + i)),
											   *(uint32_t*)(win->backBuffer +
															static_cast<int64_t>(j) * info->width +
															i));
					}
				}
				AddDirtyClip(winx, winy, width, height);
			} else {
				for (clipWin = rootWin; clipWin != NULL; clipWin = clipWin->next) {
					clipInfo = (WinSharedInfo*)clipWin->sharedInfo;
					if (clipWin == win)
						continue;
					r2.x = clipInfo->x;
					r2.y = clipInfo->y;
					r2.w = clipInfo->width;
					r2.h = clipInfo->height;

					if (ClipCheckIntersect(&r1, &r2)) {
						_intersected_ = true;
						ClipCalculateRect(&r1, &r2, clip, &clipCount);
					}
				}

				for (int i = 0; i < height; i++) {
					void* canvas_buff =
						(canvas->buffer + (static_cast<int64_t>(winy) + i) * canvas->canvasWidth +
						 winx);
					void* winbuff =
						(win->backBuffer + (0 + static_cast<int64_t>(i)) * info->width + 0);
					_fastcpy(canvas_buff, winbuff, static_cast<int64_t>(width) * 4);
				}

				if ((clipCount == 0 && info->updateEntireWindow) ||
					(clipCount == 0 && !_window_moving_))
					AddDirtyClip(winx, winy, width, height);

				for (int m = 0; m < clipCount; m++) {
					int k_x = clip[m].x;
					int k_y = clip[m].y;
					int k_w = clip[m].w;
					int k_h = clip[m].h;

					if (k_x < 0)
						k_x = 0;
					if (k_y < 0)
						k_y = 0;
					if ((k_x + k_w) >= canvas->screenWidth)
						k_w = canvas->screenWidth - k_x;
					if ((k_y + k_h) >= canvas->screenHeight)
						k_h = canvas->screenHeight - k_y;

					AddDirtyClip(k_x, k_y, k_w, k_h);
					clipCount = 0;
				}
			}

			if (win->animFrameCount == 0)
				info->updateEntireWindow = 0;

			if (!info->windowReady)
				info->windowReady = 1;
		}
	}

	CursorStoreBack(canvas, currentCursor, currentCursor->xpos, currentCursor->ypos);

	CursorDraw(canvas, currentCursor, currentCursor->xpos, currentCursor->ypos);
	AddDirtyClip(currentCursor->xpos, currentCursor->ypos, 24, 24);

	/* finally present all updates to framebuffer */
	DirtyScreenUpdate(canvas);

	if (_window_update_all_)
		_window_update_all_ = false;

	if (_always_on_top_update)
		_always_on_top_update = false;

	if (_window_moving_)
		_window_moving_ = false;

	if (_skip_disable_)
		_skip_disable_ = false;

	currentCursor->oldXPos = currentCursor->xpos;
	currentCursor->oldYPos = currentCursor->ypos;
}

/*
 * DeodhaiSendMouseEvent -- send mouse event to desired window
 * @param win -- Pointer to window
 * @param eventType -- either MOUSE_EVENT or MOUSE_LEAVE 
 * @param x -- Mouse x location
 * @param y -- Mouse y location
 * @param button -- Mouse button state
 */
void DeodhaiSendMouseEvent(
	int handle, int ownerId, uint8_t handleType, uint8_t eventType, int x, int y, int button) {
	PostEvent e;
	memset(&e, 0, sizeof(PostEvent));
	e.type = eventType;
	e.dword = x;
	e.dword2 = y;
	e.dword3 = button;
	e.dword4 = handle;
	e.dword5 = handleType;
	e.to_id = ownerId;
	e.from_id = POSTBOX_ROOT_ID;
	_KeFileIoControl(postbox_fd, POSTBOX_PUT_EVENT, &e);
}

/*
 * DeodhaiResizeCursorUpdate -- update the resize cursor
 */
void DeodhaiResizeCursorUpdate(int mouse_x, int mouse_y, WinSharedInfo* info) {
	int left = info->x;
	int right = info->x + info->width;
	int top = info->y;
	int bottom = info->y + info->height;

	bool onLeftEdge = (mouse_x >= left && mouse_x <= left + 5);
	bool onRightEdge = (mouse_x <= right && mouse_x >= right - 12);
	bool onTop = (mouse_y >= top && mouse_y <= top + 5);
	bool onBottom = (mouse_y <= bottom && mouse_y >= bottom - 12);

	if (onLeftEdge || onRightEdge)
		ChangeCursor(resizeLeftRight);
	else if (onTop || onBottom)
		ChangeCursor(resizeUpDown);
	else
		ChangeCursor(arrow);
}
/*
 * DeodhaiBroadcastMouse -- broadcast mouse event to all window
 * @param mouse_x -- mouse x location
 * @param mouse_y -- mouse y location
 * @param button -- mouse button state
 */
void DeodhaiBroadcastMouse(int mouse_x, int mouse_y, int button) {
	Window* mouseWin = NULL;
	bool cursorRestore;

	if (focusedWin) {
		WinSharedInfo* info = (WinSharedInfo*)focusedWin->sharedInfo;
		if (!info->hide) {
			if (mouse_x >= info->x && (mouse_x < (info->x + info->width)) && mouse_y >= info->y &&
				(mouse_y < (info->y + info->height))) {
				mouseWin = focusedWin;
				/* skip others */
				goto broadcast;
			}
		}
	}
	if (!mouseWin) {
		/* check for normal windows */
		for (Window* win = rootWin; win != NULL; win = win->next) {
			WinSharedInfo* info = (WinSharedInfo*)win->sharedInfo;
			if (info->hide)
				continue;
			if (mouse_x >= info->x && (mouse_x < (info->x + info->width)) && mouse_y >= info->y &&
				(mouse_y < (info->y + info->height))) {
				if (DeodhaiCheckWindowPointOcclusion(win, mouse_x, mouse_y))
					continue;
				if (win->flags & WINDOW_FLAG_BLOCKED)
					continue;

				/* PHILOSOPHY: if mouse event was sent to unfocused window
				 * and if the mouse points goes to some kind of widget or object
				 * it will be an hover message, if mouse left button was clicked
				 * within than hovered object of unfocused window, make that
				 * window focused and bring it to front and update all window
				 * and shadow effects
				 */
				if (focusedWin != win && button) {
					DeodhaiWindowSetFocused(win, 1);
					_window_update_all_ = true;
					_shadow_update = true;
				}
				mouseWin = win;
				break;
			}
		}

		/* check for always on top windows */
		for (Window* win = alwaysOnTop; win != NULL; win = win->next) {
			WinSharedInfo* info = (WinSharedInfo*)win->sharedInfo;
			if (info->hide)
				continue;
			if (mouse_x >= info->x && (mouse_x < (info->x + info->width)) && mouse_y >= info->y &&
				(mouse_y < (info->y + info->height))) {
				mouseWin = win;
				break;
			}
		}
	}

broadcast:
	if (mouseWin) {
		WinSharedInfo* info = (WinSharedInfo*)mouseWin->sharedInfo;
		DeodhaiResizeCursorUpdate(mouse_x, mouse_y, info);

		int handle = mouseWin->handle;
		uint8_t handleType = HANDLE_TYPE_NORMAL_WINDOW;
		if ((mouseWin->flags & WINDOW_FLAG_POPUP))
			handleType = HANDLE_TYPE_POPUP_WINDOW;

		/* handle mouse last window hover */
		if (mouseLastHovered) {
			if (mouseLastHovered != mouseWin) {
				int lastWinHandleType = HANDLE_TYPE_NORMAL_WINDOW;
				if ((mouseLastHovered->flags & WINDOW_FLAG_POPUP))
					lastWinHandleType = HANDLE_TYPE_POPUP_WINDOW;
				DeodhaiSendMouseEvent(mouseLastHovered->handle,
									  mouseLastHovered->ownerId,
									  lastWinHandleType,
									  DEODHAI_REPLY_MOUSE_LEAVE,
									  mouse_x,
									  mouse_y,
									  button);
				//	_KeProcessSleep(100);
			}
		}

		mouseLastHovered = mouseWin;
		DeodhaiSendMouseEvent(handle,
							  mouseWin->ownerId,
							  handleType,
							  DEODHAI_REPLY_MOUSE_EVENT,
							  mouse_x,
							  mouse_y,
							  button);
	}

	if (!mouseWin) {
		if (currentCursor->type == CURSOR_TYPE_RESIZE_RIGHTLEFT ||
			currentCursor->type == CURSOR_TYPE_RESIZE_UPDOWN)
			ChangeCursor(arrow);
	}
}

/*
 * DeodhaiBroadcastMouse -- broadcast mouse event to all window
 * @param mouse_x -- mouse x location
 * @param mouse_y -- mouse y location
 * @param button -- mouse button state
 */
void DeodhaiBroadcastTouch(int t_x, int t_y) {
	Window* mouseWin = NULL;

	if (focusedWin) {
		WinSharedInfo* info = (WinSharedInfo*)focusedWin->sharedInfo;
		if (!info->hide) {
			if (t_x >= info->x && (t_x < (info->x + info->width)) && t_y >= info->y &&
				(t_y < (info->y + info->height))) {
				mouseWin = focusedWin;
				/* skip others */
				goto broadcast;
			}
		}
	}
	if (!mouseWin) {
		/* check for normal windows */
		for (Window* win = rootWin; win != NULL; win = win->next) {
			WinSharedInfo* info = (WinSharedInfo*)win->sharedInfo;
			if (info->hide)
				continue;
			if (t_x >= info->x && (t_x < (info->x + info->width)) && t_y >= info->y &&
				(t_y < (info->y + info->height))) {
				if (DeodhaiCheckWindowPointOcclusion(win, t_x, t_y))
					continue;
				if (win->flags & WINDOW_FLAG_BLOCKED)
					continue;

				/* PHILOSOPHY: if mouse event was sent to unfocused window
				 * and if the mouse points goes to some kind of widget or object
				 * it will be an hover message, if mouse left button was clicked
				 * within than hovered object of unfocused window, make that
				 * window focused and bring it to front and update all window
				 * and shadow effects
				 */
				if (focusedWin != win) {
					DeodhaiWindowSetFocused(win, 1);
					_window_update_all_ = true;
					_shadow_update = true;
				}
				mouseWin = win;
				break;
			}
		}

		/* check for always on top windows */
		for (Window* win = alwaysOnTop; win != NULL; win = win->next) {
			WinSharedInfo* info = (WinSharedInfo*)win->sharedInfo;
			if (info->hide)
				continue;
			if (t_x >= info->x && (t_x < (info->x + info->width)) && t_y >= info->y &&
				(t_y < (info->y + info->height))) {
				mouseWin = win;
				break;
			}
		}
	}

broadcast:
	if (mouseWin) {
		WinSharedInfo* info = (WinSharedInfo*)mouseWin->sharedInfo;

		int handle = mouseWin->handle;
		uint8_t handleType = HANDLE_TYPE_NORMAL_WINDOW;
		if ((mouseWin->flags & WINDOW_FLAG_POPUP))
			handleType = HANDLE_TYPE_POPUP_WINDOW;

		DeodhaiSendMouseEvent(
			handle, mouseWin->ownerId, handleType, DEODHAI_REPLY_TOUCH_EVENT, t_x, t_y, 0x1);
	}
}

/*
 * DeodhaiBrodcastKey -- sends key event
 * to focused window
 * @param code -- key code
 */
void DeodhaiBroadcastKey(int code) {
	if (!focusedWin)
		return;
	PostEvent e;
	memset(&e, 0, sizeof(PostEvent));
	e.type = DEODHAI_REPLY_KEY_EVENT;
	e.dword = code;
	e.dword2 = focusedWin->handle;
	e.to_id = focusedWin->ownerId;
	e.from_id = POSTBOX_ROOT_ID;
	_KeFileIoControl(postbox_fd, POSTBOX_PUT_EVENT, &e);
}

/*
 * DeodhaiCloseWindow -- closes and cleanup an opened
 * window
 * @param win -- Pointer to window to be closed
 */
void DeodhaiCloseWindow(Window* win) {
	int ownerId = win->ownerId;
	int handle = win->handle;
	uint16_t flags = win->flags;
	WinSharedInfo* info = (WinSharedInfo*)win->sharedInfo;
	int width = info->width;
	int height = info->height;
	int x = info->x;
	int y = info->y;
	_KePrint("[Deodhai]:CloseWindow : %s \r\n", win->title);
	//free(win->title);

	/* iterate all popup window and close them */
	for (Window* popup = win->firstPopupWin; popup != NULL; popup = popup->next) {
		//close all
		free(popup->title);
		_KeUnmapSharedMem(popup->shWinKey);
		_KeUnmapSharedMem(popup->backBufferKey);
#ifdef SHADOW_ENABLED
		_KeMemUnmap(popup->shadowBuffers,
					(static_cast<size_t>(width) + SHADOW_SIZE * 2) * (height + SHADOW_SIZE * 2) *
						4);
#endif
		free(popup);
	}

	_KePrint("Unmapping shared mems \r\n");
	_KeUnmapSharedMem(win->shWinKey);
	_KeUnmapSharedMem(win->backBufferKey);
	_KePrint("Unmapped all shared mems from deodhai side for process\r\n");
#ifdef SHADOW_ENABLED
	_KeMemUnmap(win->shadowBuffers,
				(static_cast<size_t>(width) + SHADOW_SIZE * 2) * (height + SHADOW_SIZE * 2) * 4);
#endif
	BackDirtyAdd(
		x - SHADOW_SIZE, y - SHADOW_SIZE, width + SHADOW_SIZE * 2, height + SHADOW_SIZE * 2);
	DeodhaiRemoveWindow(win);
	_KePrint("Removing window \r\n");
	//free(win);
	PostEvent e;
	e.to_id = ownerId;
	e.type = DEODHAI_REPLY_WINDOW_CLOSED;
	_KeFileIoControl(postbox_fd, POSTBOX_PUT_EVENT, &e);
	_KePrint("Putting post box event \r\n");
	if (!(flags & WINDOW_FLAG_MESSAGEBOX)) {
		/* now broadcast this information, that a
		 * specific window has been destroyed
		 */
		memset(&e, 0, sizeof(PostEvent));
		e.type = DEODHAI_BROADCAST_WINDESTROYED;
		e.dword = ownerId;
		e.dword2 = handle;
		DeodhaiBroadcastMessage(&e, NULL);
		_KeProcessSleep(100);
	}
	_always_on_top_update = 1;
	_window_update_all_ = 1;
}

/* DrawWallpaper for getting jpeg image as wallpaper
 * fully jpeg encoder is needed, i use synfig studio 
 * for jpeg encoder
 */
void DrawWallpaper(ChCanvas* canv, char* filename) {
	int image = _KeOpenFile(filename, FILE_OPEN_READ_ONLY);
	XEFileStatus stat;
	memset(&stat, 0, sizeof(XEFileStatus));
	_KeFileStat(image, &stat);
	_KePrint("Wallpaper sz : %d \r\n", stat.size);
	void* data_ = _KeMemMap(NULL, stat.size, 0, 0, -1, 0);
	memset(data_, 0, ALIGN_UP(stat.size, 4096));
	_KeReadFile(image, data_, ALIGN_UP(stat.size, 4096));

	uint8_t* data1 = (uint8_t*)data_;

	Jpeg::Decoder* decor =
		new Jpeg::Decoder((uint8_t*)data1, ALIGN_UP(stat.size, 4096), malloc, free);
	if (decor->GetResult() != Jpeg::Decoder::OK) {
		_KePrint("Decoder error \n");
		for (;;)
			;
		return;
	}
	int w = decor->GetWidth();
	int h = decor->GetHeight();
	_KePrint("Wallpaper width : %d \r\n", w);
	_KePrint("Wallpaper height : %d \r\n", h);
	uint32_t* swapable_buff = canv->buffer;
	canv->buffer = surfaceBuffer;
	uint8_t* data = decor->GetImage();

	unsigned x = 0;
	unsigned y = 0;
	for (int i = 0; i < h; i++) {
		for (int k = 0; k < w; k++) {
			int j = k + i * w;
			uint8_t r = data[j * 3];
			uint8_t g = data[j * 3 + 1];
			uint8_t b = data[j * 3 + 2];
			uint32_t rgba = ((r << 16) | (g << 8) | (b)) & 0x00ffffff;
			rgba = rgba | 0xff000000;
			ChDrawPixelRAW(canv, x + k, y + i, rgba);
			j++;
		}
	}

	canv->buffer = swapable_buff;
}

ChCanvas* DeodhaiGetMainCanvas() {
	return canvas;
}
uint64_t DeodhaiCurrentTime() {
	timeval tm;
	gettimeofday(&tm);

	time_t sec_diff = tm.tv_sec - startTime;
	long usec_diff = tm.tv_usec - startSubTime;

	if (tm.tv_usec < startSubTime) {
		sec_diff -= 1;
		usec_diff = (1000000 + tm.tv_usec) - startSubTime;
	}

	return (uint64_t)(static_cast<uint64_t>(sec_diff) * 1000 + usec_diff / 1000);
}

uint64_t DeodhaiTimeSince(uint64_t startTime) {
	uint64_t now = DeodhaiCurrentTime();
	uint64_t diff = now - startTime;
	return diff;
}

uint64_t DeodhaiClickCurrentTime() {
	timeval t;
	gettimeofday(&t);
	time_t sec_diff = t.tv_sec;
	suseconds_t usec_diff = t.tv_usec;
	return (uint64_t)(static_cast<uint64_t>(sec_diff) * 1000 + (usec_diff / 1000));
}

/*DeodhaiClickTimeSince -- Return the time in ms*/
uint64_t DeodhaiClickTimeSince(uint64_t start_time) {
	uint64_t now = DeodhaiClickCurrentTime();
	uint64_t diff = now - start_time;
	return diff;
}

/*
 * DeodhaiUpdateBits -- update specific deodhai bits
 */
void DeodhaiUpdateBits(bool window_update, bool skip_disable) {
	_window_update_all_ = window_update;
	_skip_disable_ = skip_disable;
}

extern "C" void _AuConOut(const char* text, ...);
/*
 * main -- deodhai compositor
 */
int main(int argc, char* arv[]) {
	int pid = _KeGetThreadID();

	_KePrint("Argc == 10 %x\r\n", argc);
	_KePrint("Deodhai v1.0 running %d\r\n", pid);

	startTime = 0;
	startSubTime = 0;
	timeval tm;
	//gettimeofday(&tm);
	startTime = tm.tv_sec;
	startSubTime = tm.tv_usec;

	/* first of all get screen width and screen height */
	XEFileIOControl graphctl;
	memset(&graphctl, 0, sizeof(XEFileIOControl));
	graphctl.syscall_magic = AURORA_SYSCALL_MAGIC;

	DeodhaiInitialiseData();
	BackDirtyInitialise();

	_KePrint("Deodhai : Till here we're initialized \r\n");

	int ret = 0;
	int screen_w = 0;
	_gpu_enabled = 0;
	int screen_h = 0;
	_window_moving_ = false;
	_clients_advice = false;
	/* create a demo canvas just for getting the graphics
	 * file descriptor 
	 */
	ChCanvas* canv = ChCreateCanvas(100, 100);

	canvas = canv;
	ret = _KeFileIoControl(canv->graphics_fd, SCREEN_GETWIDTH, &graphctl);
	screen_w = graphctl.uint_1;
	ret = _KeFileIoControl(canv->graphics_fd, SCREEN_GETHEIGHT, &graphctl);
	screen_h = graphctl.uint_1;

	/* now modify the canvas size with screen size */
	canv->canvasWidth = screen_w;
	canv->canvasHeight = screen_h;

	gpu_fd = _KeOpenFile("/dev/virtiogpu", FILE_OPEN_READ_ONLY);

	if (gpu_fd != -1) {
		_gpu_enabled = 1;
		gpu_display_id = _KeFileIoControl(gpu_fd, 0x204, NULL);
	}

	_KePrint("Canv->screen_w -> %d, screenh -> %d \r\n", screen_w, screen_h);
	/* now allocate a back buffer with respected canvas size
	 * and fill it with light-black color */
	ChAllocateBuffer(canv);
	/* allocate a surface buffer */
	surfaceBuffer =
		(uint32_t*)_KeMemMap(NULL,
							 static_cast<size_t>(canv->screenWidth) * canv->screenHeight * 4,
							 0,
							 0,
							 MEMMAP_NO_FILEDESC,
							 0);
	for (int i = 0; i < screen_w; i++)
		for (int j = 0; j < screen_h; j++)
			surfaceBuffer[j * canv->canvasWidth + i] = GRAY; //0xFF938585;

	DeodhaiBackSurfaceUpdate(canv, 0, 0, screen_w, screen_h);
	if (screen_w == 1024 && screen_h == 768) {
		DrawWallpaper(canv, "/mtnr1.jpg");
		DeodhaiBackSurfaceUpdate(canv, 0, 0, screen_w, screen_h);
	} else if (screen_w == 1920 && screen_h == 1080) {
		DrawWallpaper(canv, "/mtnr2.jpg");
		DeodhaiBackSurfaceUpdate(canv, 0, 0, screen_w, screen_h);
	} else if (screen_w == 480 && screen_h == 320) {
		DrawWallpaper(canv, "/mntr1.jpg");
		DeodhaiBackSurfaceUpdate(canv, 0, 0, screen_w, screen_h);
	} else if (screen_w == 800 && screen_h == 480) {
		DrawWallpaper(canv, "/flora1.jpg");
		DeodhaiBackSurfaceUpdate(canv, 0, 0, screen_w, screen_h);
	}

	ChCanvasScreenUpdate(canv, 0, 0, canv->canvasWidth, canv->canvasHeight);
	if (_gpu_enabled) {
		XEFileIOControl ctl;
		ctl.uint_1 = gpu_display_id;
		ctl.ushort_1 = 0;
		ctl.ushort_2 = 0;
		ctl.ulong_1 = canv->canvasWidth;
		ctl.ulong_2 = canv->canvasHeight;
		_KeFileIoControl(gpu_fd, 0x202, &ctl);
		canv->buffer = canv->framebuff;
	}

	InitialiseDirtyClipList();

	arrow = CursorOpen("/pointer.bmp", CURSOR_TYPE_POINTER);
	CursorRead(arrow);
	/*move = CursorOpen("/move.bmp", CURSOR_TYPE_MOVE);
	CursorRead(move);
	resizeUpDown = CursorOpen("/rzupdn.bmp", CURSOR_TYPE_RESIZE_UPDOWN);
	CursorRead(resizeUpDown);
	resizeLeftRight = CursorOpen("/rzrl.bmp", CURSOR_TYPE_RESIZE_RIGHTLEFT);
	CursorRead(resizeLeftRight);*/
	currentCursor = arrow;
	CursorStoreBack(canv, currentCursor, 0, 0);

	/* Open all required device file */
	mouse_fd = _KeOpenFile("/dev/mice", FILE_OPEN_READ_ONLY);
	kybrd_fd = _KeOpenFile("/dev/kybrd", FILE_OPEN_READ_ONLY);

	AuInputMessage mice_input;
	AuInputMessage kybrd_input;
	memset(&mice_input, 0, sizeof(AuInputMessage));
	memset(&kybrd_input, 0, sizeof(AuInputMessage));
	postbox_fd = _KeOpenFile("/dev/postbox", FILE_OPEN_READ_ONLY);
	_KePrint("postboxfd : %d \r\n", postbox_fd);

	//	postbox_fd = canv->graphics_fd;

	_KeFileIoControl(postbox_fd, POSTBOX_CREATE_ROOT, NULL);

	mouseLastHovered = NULL;

	PostEvent event;
	uint64_t frame_tick = 0;
	uint64_t diff_tick = 0;
	uint64_t last_click_time = 0;
	uint64_t last_redraw = 0;

	int proc = _KeCreateProcess(0, "xelnch");
	_KeProcessLoadExec(proc, "/xelnch.exe", 0, NULL);

	_KeProcessSleep(100);
	proc = _KeCreateProcess(0, "nmdapha");
	_KeProcessLoadExec(proc, "/nmdapha.exe", NULL, NULL);

	//_KeProcessSleep(200);
	/*int proc = _KeCreateProcess(0, "file");
	_KeProcessLoadExec(proc, "/file.exe", NULL, NULL);

	_KeProcessSleep(100);
	proc = _KeCreateProcess(0, "calc");
	_KeProcessLoadExec(proc, "/calc.exe", NULL, NULL);*/

	/* launch the session manager directly from here */
#ifdef ARCH_X64
	int proc = _KeCreateProcess(0, "xelnch");
	_KeProcessLoadExec(proc, "/xelnch.exe", 0, NULL);

	_KeProcessSleep(100);

	proc = _KeCreateProcess(0, "nmdapha");
	_KeProcessLoadExec(proc, "/nmdapha.exe", 0, NULL);
#endif

	_KePrint("Postbox created for deodhai \r\n");
	while (1) {
#ifdef ARCH_X64
		unsigned long frameTime = DeodhaiTimeSince(last_redraw);
		if (frameTime > 15) {
			ComposeFrame(canv);
			last_redraw = DeodhaiCurrentTime();
			frameTime = 0;
		}
#elif ARCH_ARM64
		ComposeFrame(canv);
#endif

		_KeFileIoControl(postbox_fd, POSTBOX_GET_EVENT_ROOT, &event);

		_KeReadFile(mouse_fd, &mice_input, sizeof(AuInputMessage));

		_KeReadFile(kybrd_fd, &kybrd_input, sizeof(AuInputMessage));

		if (mice_input.type == AU_INPUT_MOUSE) {
			int32_t cursor_x = mice_input.xpos;
			int32_t cursor_y = mice_input.ypos;

#ifdef ARCH_X64
			double scale_x, scale_y;
			if (mice_input.code4 != 0) {
				/*scaling is needed*/
				scale_x = (double)canvas->screenWidth / (double)mice_input.code4;
				cursor_x = mice_input.xpos * (double)scale_x;
				scale_y = (double)canvas->screenHeight / (double)mice_input.code4;
				cursor_y = mice_input.ypos * (double)scale_y;
			}
#endif
			currentCursor->xpos = cursor_x;
			currentCursor->ypos = cursor_y;
			int button = mice_input.button_state;

			if ((currentCursor->xpos) <= 0)
				currentCursor->xpos = 0;

			if ((currentCursor->ypos) <= 0)
				currentCursor->ypos = 0;

			if ((currentCursor->xpos + 24) >= canvas->screenWidth)
				currentCursor->xpos = canvas->screenWidth - 24;

			if ((currentCursor->ypos + 24) >= canvas->screenHeight)
				currentCursor->ypos = canvas->screenHeight - 24;

			if (currentCursor->xpos >= canvas->screenWidth)
				currentCursor->xpos = 0;

			if (currentCursor->ypos >= canvas->screenHeight)
				currentCursor->ypos = 0;

			DeodhaiWindowCheckDraggable(currentCursor->xpos, currentCursor->ypos, button);

			/*
			 * TODO: bug fixing
			 */
#ifdef ARCH_X64
			if (button) {
				if (DeodhaiClickTimeSince(last_click_time) < 400) {
					button = DEODHAI_MESSAGE_MOUSE_DBLCLK;
					last_click_time = 0;
				} else {
					last_click_time = DeodhaiClickCurrentTime();
				}
			}
#endif
			if (_window_broadcast_mouse_)
				DeodhaiBroadcastMouse(currentCursor->xpos, currentCursor->ypos, button);

			/* ensure clipping within the screen */
			if (currentCursor->xpos <= 0)
				currentCursor->xpos = 0;
			if (currentCursor->ypos <= 0)
				currentCursor->ypos = 0;

			if (currentCursor->xpos + currentCursor->width >= screen_w)
				currentCursor->xpos = screen_w - currentCursor->width;
			if (currentCursor->ypos + currentCursor->height >= screen_h)
				currentCursor->ypos = screen_h - currentCursor->height;

			memset(&mice_input, 0, sizeof(AuInputMessage));
		}

		/*
		 * Handle touch screen events
		 */
		if (mice_input.type == AU_INPUT_TOUCH) {
			int32_t cursor_x = mice_input.xpos;
			int32_t cursor_y = mice_input.ypos;

#ifdef ARCH_X64
			double scale_x, scale_y;
			if (mice_input.code4 != 0) {
				/*scaling is needed*/
				scale_x = (double)canvas->screenWidth / (double)mice_input.code4;
				cursor_x = mice_input.xpos * (double)scale_x;
				scale_y = (double)canvas->screenHeight / (double)mice_input.code4;
				cursor_y = mice_input.ypos * (double)scale_y;
			}
#endif
			currentCursor->xpos = cursor_x;
			currentCursor->ypos = cursor_y;

			if ((currentCursor->xpos) <= 0)
				currentCursor->xpos = 0;

			if ((currentCursor->ypos) <= 0)
				currentCursor->ypos = 0;

			if ((currentCursor->xpos + 24) >= canvas->screenWidth)
				currentCursor->xpos = canvas->screenWidth - 24;

			if ((currentCursor->ypos + 24) >= canvas->screenHeight)
				currentCursor->ypos = canvas->screenHeight - 24;

			if (currentCursor->xpos >= canvas->screenWidth)
				currentCursor->xpos = 0;

			if (currentCursor->ypos >= canvas->screenHeight)
				currentCursor->ypos = 0;

			DeodhaiBroadcastTouch(cursor_x, cursor_y);
			/* ensure clipping within the screen */
			if (currentCursor->xpos <= 0)
				currentCursor->xpos = 0;
			if (currentCursor->ypos <= 0)
				currentCursor->ypos = 0;

			if (currentCursor->xpos + currentCursor->width >= screen_w)
				currentCursor->xpos = screen_w - currentCursor->width;
			if (currentCursor->ypos + currentCursor->height >= screen_h)
				currentCursor->ypos = screen_h - currentCursor->height;

			memset(&mice_input, 0, sizeof(AuInputMessage));
		}

		if (kybrd_input.type == AU_INPUT_KEYBOARD) {
			_KePrint("[Deodhai]: keyinput : %x \r\n", kybrd_input.code);
			DeodhaiBroadcastKey(kybrd_input.code);
			memset(&kybrd_input, 0, sizeof(AuInputMessage));
		}

		if (event.type == DEODHAI_MESSAGE_CREATEWIN) {
			int x = event.dword;
			int y = event.dword2;
			int w = event.dword3;
			int h = event.dword4;
			_KePrint("[Deodhai]: create win message received \r\n");
			_KePrint("[Deodhai]: x : %d y : %d \r\n", x, y);
			_KePrint("[Doedhai]: from id : %d \r\n", event.from_id);
			/* if this create window is creating a popup window
			 * then we will need it's parent window handle
			 */
			int parent_handle = event.dword6;
			uint16_t flags = event.dword5;

			Window* win = DeodhaiCreateWindow(x, y, w, h, flags, event.from_id, event.charValue3);

			if ((win->flags & WINDOW_FLAG_POPUP)) {
				for (Window* parentWin = rootWin; parentWin != NULL; parentWin = parentWin->next) {
					if (parentWin->handle == parent_handle) {
						win->parent = parentWin;
						_KePrint("[Deodhai]: popup added \r\n");
						break;
					}
				}
				_KePrint("[Deodhai]:Popup window created \r\n");
			}
			PostEvent e;
			memset(&e, 0, sizeof(PostEvent));

			e.type = DEODHAI_REPLY_WINCREATED;
			e.dword = win->shWinKey;
			e.dword2 = win->backBufferKey;
			e.dword3 = win->handle;
			e.to_id = event.from_id;

			_KeFileIoControl(postbox_fd, POSTBOX_PUT_EVENT, &e);
			_KePrint("Msg sent to e.toid : %d \n", e.to_id);
			_KeProcessSleep(180);
			if (!(win->flags & WINDOW_FLAG_MESSAGEBOX || win->flags & WINDOW_FLAG_POPUP ||
				  win->flags & WINDOW_FLAG_BROADCAST_LISTENER)) {
				/* broadcast it to all broadcast listener windows, about this news*/
				memset(&e, 0, sizeof(PostEvent));
				e.type = DEODHAI_BROADCAST_WINCREATED;
				e.dword = win->ownerId;
				e.dword2 = win->handle;
				strcpy(e.charValue3, win->title);
				DeodhaiBroadcastMessage(&e, win);
			}

			if (!(win->flags & WINDOW_FLAG_ALWAYS_ON_TOP)) {
				win->flags |= WINDOW_FLAG_ANIMATED | WINDOW_FLAG_ANIMATION_FADE_IN;
				win->animAlphaVal = 0;
			}

			_KePrint("[Deodhai]: Window created \r\n");
			/*	_window_update_all_ = true;
			_always_on_top_update = true;*/
			focusedWin = win;
			memset(&event, 0, sizeof(PostEvent));
		}

		/* broadcast icon message */
		if (event.type == DEODHAI_MESSAGE_BROADCAST_ICON) {
			Window* skippable = NULL;
			for (Window* win = rootWin; win != NULL; win = win->next) {
				if (win->ownerId == event.from_id) {
					skippable = win;
					break;
				}
			}

			for (Window* win = alwaysOnTop; win != NULL; win = win->next) {
				if (win->ownerId == event.from_id) {
					skippable = win;
					break;
				}
			}

			event.type = 174;
			DeodhaiBroadcastMessage(&event, skippable);
			_window_update_all_ = 1;
			_always_on_top_update = 1;
			_KeProcessSleep(20);
			memset(&event, 0, sizeof(PostEvent));
		}

		if (event.type == DEODHAI_MESSAGE_WINDOW_HIDE) {
			uint16_t ownerId = event.dword;
			uint32_t handle = event.dword2;
			Window* hideable_win = NULL;
			for (Window* win = rootWin; win != NULL; win = win->next) {
				if (win->handle == handle) {
					hideable_win = win;
					break;
				}
			}

			if (!hideable_win) {
				for (Window* win = alwaysOnTop; win != NULL; win = win->next) {
					if (win->handle == handle) {
						hideable_win = win;
						break;
					}
				}
			}

			if (hideable_win)
				DeodhaiWindowHide(hideable_win);
			_KeProcessSleep(10);
			memset(&event, 0, sizeof(PostEvent));
		}

		/* brings back a window to front */
		if (event.type == DEODHAI_MESSAGE_WINDOW_BRING_FRONT) {
			uint32_t handle = event.dword2;
			Window* frontableWin = NULL;
			for (Window* win = rootWin; win != NULL; win = win->next) {
				if (win->handle == handle) {
					frontableWin = win;
					break;
				}
			}

			if (frontableWin) {
				if (!(frontableWin->flags & WINDOW_FLAG_BLOCKED))
					DeodhaiWindowSetFocused(frontableWin, true);
			}
			_window_update_all_ = true;
			_always_on_top_update = true;
			memset(&event, 0, sizeof(PostEvent));
		}

		if (event.type == DEODHAI_MESSAGE_GETWINDOW) {
			uint16_t ownerID = 0;
			uint32_t handle = 0;
			bool _not_found = true;
			for (Window* win = rootWin; win != NULL; win = win->next) {
				if (strcmp(win->title, event.charValue3) == 0) {
					ownerID = win->ownerId;
					handle = win->handle;
					_not_found = false;
					break;
				}
			}

			if (_not_found) {
				for (Window* win = alwaysOnTop; win != NULL; win = win->next) {
					if (strcmp(win->title, event.charValue3) == 0) {
						ownerID = win->ownerId;
						handle = win->handle;
						_not_found = false;
						break;
					}
				}
			}

			PostEvent e;
			memset(&e, 0, sizeof(PostEvent));
			e.type = DEODHAI_REPLY_WINDOW_ID;
			e.dword = ownerID;
			e.dword2 = handle;
			e.to_id = event.from_id;
			e.from_id = POSTBOX_ROOT_ID;
			_KeFileIoControl(postbox_fd, POSTBOX_PUT_EVENT, &e);
			memset(&event, 0, sizeof(PostEvent));
		}

		if (event.type == DEODHAI_MESSAGE_CLOSE_WINDOW) {
			/* deodhai close window commands needs every datas
			 * to be cleared from client side, in server side
			 * only window related datas will get cleared, 
			 * like, root-window, sub-windows and popup-list
			 */
			int handle = event.dword;
			int ownerId = event.from_id;
			Window* removable = NULL;
			for (Window* win = rootWin; win != NULL; win = win->next) {
				if (win->handle == handle && win->ownerId == ownerId) {
					removable = win;
					break;
				}
			}

			if (removable) {
				DeodhaiCloseWindow(removable);
				focusedWin = NULL;
				focusedLast = NULL;
				//_window_update_all_ = true;
				//_always_on_top_update = true;
			}
			memset(&event, 0, sizeof(PostEvent));
		}

		if (event.type == DEODHAI_MESSAGE_SET_FLAGS) {
			int handle = event.dword;
			int ownerId = event.from_id;
			int flags = event.dword2;
			for (Window* win = rootWin; win != NULL; win = win->next) {
				if (win->handle == handle && win->ownerId == ownerId) {
					win->flags = flags;
					break;
				}
			}
			memset(&event, 0, sizeof(PostEvent));
		}

		/*
		 * DEODHAI_MESSAGE_MOVE_WINDOW -- moves target window
		 * to new location
		 */
		if (event.type == DEODHAI_MESSAGE_MOVE_WINDOW) {
			int newX = event.dword;
			int newY = event.dword2;
			int ownerID = event.from_id;
			Window* win_ = NULL;
			_clients_advice = true;
			for (Window* win = rootWin; win != NULL; win = win->next) {
				if (win->ownerId == ownerID) {
					win_ = win;
					break;
				}
			}

			for (Window* win = alwaysOnTop; win != NULL; win = win->next) {
				if (win->ownerId == ownerID) {
					win_ = win;
					break;
				}
			}
			if (win_)
				DeodhaiWindowMove(win_, newX, newY);

			memset(&event, 0, sizeof(PostEvent));
		}

#ifdef ARCH_X64
		_KeProcessSleep((16 - frameTime));
#elif ARCH_ARM64
		_KeProcessSleep(2);
#endif
	}
}

int _get_gpu_fd() {
	return gpu_fd;
}

int _get_gpu_display_id() {
	return gpu_display_id;
}

bool _is_gpu_enabled() {
	return _gpu_enabled;
}