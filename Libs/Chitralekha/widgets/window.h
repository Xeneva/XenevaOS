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

#ifndef __CH_WINDOW_H__
#define __CH_WINDOW_H__

#include <stdint.h>
#include "../chitralekha.h"
#include "../color.h"
#include "../draw.h"
#include "base.h"
#include "list.h"
#include <setjmp.h>

#define CHITRALEKHA_DEFAULT_WIN_WIDTH	 400
#define CHITRALEKHA_DEFAULT_WIN_HEIGHT	 300
#define CHITRALEKHA_WINDOW_DEFAULT_PAD_Y 26
#define CHITRALEKHA_WINDOW_DEFAULT_PAD_X 1

#define WINDOW_GLOBAL_CONTROL_CLOSE	   1
#define WINDOW_GLOBAL_CONTROL_MAXIMIZE 2
#define WINDOW_GLOBAL_CONTROL_MINIMIZE 3
#define WINDOW_GLOBAL_CONTROL_CUSTOM   4

#define WINDOW_FLAG_MOVABLE			   (1 << 0)
#define WINDOW_FLAG_STATIC			   (1 << 1)
#define WINDOW_FLAG_ALWAYS_ON_TOP	   (1 << 2)
#define WINDOW_FLAG_NON_RESIZABLE	   (1 << 3)
#define WINDOW_FLAG_BROADCAST_LISTENER (1 << 4)
#define WINDOW_FLAG_ANIMATED		   (1 << 5)
#define WINDOW_FLAG_BLOCKED			   (1 << 6)
#define WINDOW_FLAG_MESSAGEBOX		   (1 << 7)
#define WINDOW_FLAG_DIALOGBOX		   (1 << 8)
#define WINDOW_FLAG_ANIMATION_FADE_IN  (1 << 9)
#define WINDOW_FLAG_ANIMATION_FADE_OUT (1 << 10)
#define WINDOW_FLAG_POPUP			   (1 << 11)
/* Opt in to the compositor's glassmorphic pipeline. The blur buffers are
 * allocated by the compositor when the window is created, so this flag MUST
 * be passed to ChCreateWindow. The compositor then blends every translucent
 * pixel against the blurred desktop backdrop (DeodhaiXR WINDOW_FLAG_GLASS).
 * Pair it with ChWindowSetGlassMorphism() (chrome) and translucent fills in
 * the client paint code (content). */
#define WINDOW_FLAG_GLASS (1 << 12)

#define CHITRALEKHA_WIDGET_TYPE_CONTROL (1 << 1)
#define CHITRALEKHA_WIDGET_TYPE_POPUP	(1 << 2)

#define CHITRALEKHA_WIDGET_SCROLL_HORIZONTAL 0
#define CHITRALEKHA_WIDGET_SCROLL_VERTICAL	 1

#define WINDOW_HANDLE_TYPE_NORMAL 1
#define WINDOW_HANDLE_TYPE_POPUP  2

//#pragma pack(push,1)
typedef struct _ChSharedWin_ {
	ChRect rect[256];
	uint32_t rect_count;
	bool dirty;
	bool updateEntireWindow;
	int x;
	int y;
	int width;
	int height;
	bool alpha;
	bool hide;
	double alphaValue;
	bool windowReady;
	bool zoomed;
} ChSharedWinInfo;

static inline bool ChSharedFlagLoad(const bool* flag) {
	return __atomic_load_n(flag, __ATOMIC_ACQUIRE);
}

static inline void ChSharedFlagStore(bool* flag, bool value) {
	__atomic_store_n(flag, value, __ATOMIC_RELEASE);
}
//#pragma pack(pop)

typedef struct _chwin_ {
	uint16_t flags;
	uint32_t* buffer;
	void* sharedwin;
	ChCanvas* canv;
	char* title;
	ChSharedWinInfo* info;
	ChitralekhaApp* app;
	uint32_t handle;
	uint32_t color;
	bool focused;
	list_t* GlobalControls;
	list_t* widgets;
	list_t* subwindow;
	list_t* popup;
	struct _chwin_* parent;
	void* currentPopupMenu; //points to currently working menu
	void* selectedMenuItem;
	void* focusedWidget;
	jmp_buf jump;
	void (*ChWinPaint)(struct _chwin_* win);
	void (*ChCloseWin)(struct _chwin_* win);
} ChWindow;

typedef struct _ChWidget_ {
	int x;
	int y;
	int w;
	int h;
	int lastMouseX;
	int lastMouseY;
	bool clicked;
	bool hover;
	bool hoverPainted;
	bool KillFocus;
	bool visible;
	bool touched;
	uint8_t type;
	void (*ChActionHandler)(struct _ChWidget_* widget, ChWindow* win);
	void (*ChMouseEvent)(struct _ChWidget_* widget, ChWindow* win, int x, int y, int button);
	void (*ChTouchEvent)(struct _ChWidget_* widget, ChWindow* win, int x, int y);
	void (*ChScrollEvent)(struct _ChWidget_* widget,
						  ChWindow* win,
						  int scrollval,
						  uint8_t scrolltype);
	void (*ChPaintHandler)(struct _ChWidget_* widget, ChWindow* win);
	void (*ChDestroy)(struct _ChWidget_* widget, ChWindow* win);
} ChWidget;

typedef struct _global_ctrl_ {
	int x;
	int y;
	int w;
	int h;
	uint8_t type;
	bool hover;
	bool clicked;
	uint32_t fillColor;
	uint32_t outlineColor;
	uint32_t hoverOutlineColor;
	uint32_t clickedFillColor;
	uint32_t clickedOutlineColor;
	void (*ChGlobalButtonPaint)(ChWindow* win, struct _global_ctrl_* glbl);
	void (*ChGlobalMouseEvent)(ChWindow* win, struct _global_ctrl_* glbl, int x, int y, int button);
	void (*ChGlobalTouchEvent)(ChWindow* win, struct _global_ctrl_* glbl, int x, int y);
	void (*ChGlobalActionEvent)(ChWindow* win, struct _global_ctrl_* glbl);
} ChWinGlobalControl;

#pragma pack(push, 1)
typedef struct _popup_sh_win_ {
	ChRect rect[100];
	uint32_t rect_count;
	int x;
	int y;
	int w;
	int h;
	bool dirty;
	bool close;
	bool hide;
	bool popuped;
	bool alpha;
} ChPopupSharedWin;
#pragma pack(pop)

typedef struct _popup_win_ {
	ChWidget wid;
	ChPopupSharedWin* shwin;
	uint32_t* buffer;
	uint16_t shwinKey;
	uint16_t buffWinKey;
	list_t* widgets;
	int handle;
	ChCanvas* canv;
	bool hidden;
	void (*ChPopupWindowPaint)(struct _popup_win_* pwin, ChWindow* win);
} ChPopupWindow;

#ifdef __cplusplus
XE_EXTERN {
#endif

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
	XE_LIB ChWindow* ChCreateWindow(
		ChitralekhaApp * app, uint16_t attrib, char* title, int x, int y, int w, int h);

	/*
	* ChWindowAddSubWindow -- add sub window to parent window list
	* @param parent -- Pointer to main window
	* @param win -- Pointer to sub window
	*/
	XE_LIB void ChWindowAddSubWindow(ChWindow * parent, ChWindow * win);

	/*
	* ChWindowBroadcastIcon -- broadcast icon information to
	* broadcast listener
	* @param app -- pointer to chitralekha application
	* @param iconfile -- path of the icon file, supported formats
	* are : 32bit- bmp file
	*/
	XE_LIB void ChWindowBroadcastIcon(ChitralekhaApp * app, char* iconfile);

	/*
	* ChWindowPaint -- paint the entire window
	* @param win -- Pointer to window
	*/
	XE_LIB void ChWindowPaint(ChWindow * win);

	/*
	* ChWindowUpdate -- update a portion or whole window
	* @param win -- Pointer to window
	* @param x -- x position of the dirty area
	* @param y -- y position of the dirty area
	* @param w - width of the dirty area
	* @param h -- height of the dirty area
	* @param updateEntireWin -- update entire window
	* @param dirty -- dirty specifies small areas of the window
	*/
	XE_LIB void ChWindowUpdate(
		ChWindow * win, int x, int y, int w, int h, bool updateEntireWin, bool dirty);

	/*
	* ChWindowHide -- hide the window
	* basically it sends command to deodhai
	* @param win -- Pointer to window structure
	*/
	XE_LIB void ChWindowHide(ChWindow * win);

	/*
	* ChGetWindowHandle -- get a specific window handle from
	* the system looking by its name
	* @param app -- Pointer to application instance
	* @param title -- desired window title
	*/
	XE_LIB uint32_t ChGetWindowHandle(ChitralekhaApp * app, char* title);
	/*
	* ChWindowHandleMouse -- handle mouse event
	* @param win -- Pointer to window
	* @param x -- X coord of the mouse
	* @param y -- Y coord of the mouse
	* @param button -- button state of the mouse
	*/
	XE_LIB void ChWindowHandleMouse(ChWindow * win, int x, int y, int button);

	/*
     * ChWindowHandleTouch -- handle touch event
     * @param win -- Pointer to window
     * @param x -- X coord of the mouse
     * @param y -- Y coord of the mouse
     * @param button -- button state of the mouse
     */
	XE_LIB void ChWindowHandleTouch(ChWindow * win, int x, int y, int button);

	/*
 * ChWindowSetFocused -- bring a window to focused state
 * @param win -- Pointer to window
 */
	XE_LIB void ChWindowSetFocused(ChWindow * win);
	/*
	* ChWindowHandleFocus -- handle focus changed events
	* @param win -- Pointer to window
	* @param focus_val -- focus bit, 1 -- focused, 0 -- not focused
	* @param handle -- handle number of the window
	*/
	XE_LIB void ChWindowHandleFocus(ChWindow * win, bool focus_val, uint32_t handle);

	/*
	* ChWindowGetBackgroundColor -- returns the current
	* background color
	* @param win -- Pointer to Chitralekha Window
	*/
	XE_LIB uint32_t ChWindowGetBackgroundColor(ChWindow * win);

	/*
	* ChWindowAddWidget -- adds a widget to window
	* @param win -- Pointer to root window
	* @param wid -- Pointer to widget needs to be added
	*/
	XE_LIB void ChWindowAddWidget(ChWindow * win, ChWidget * wid);

	/*
	* ChWindowAddSubWindow -- add sub window to parent window list
	* @param parent -- Pointer to main window
	* @param win -- Pointer to sub window
	*/
	XE_LIB void ChWindowAddSubWindow(ChWindow * parent, ChWindow * win);

	/*
	* ChGetWindowByHandle -- returns window by looking sub windows list
	* @param mainWin -- pointer to main window
	* @param handle -- Handle of the window
	*/
	XE_LIB ChWindow* ChGetWindowByHandle(ChWindow * mainWin, int handle);

	/*
	* ChGetPopupWindowByHandle -- looks for popup window by its handle
	* @param mainWin -- Pointer to main window
	* @param handle -- Handle of the popup window
	*/
	XE_LIB ChWindow* ChGetPopupWindowByHandle(ChWindow * mainWin, int handle);

	/*
	* ChPopupWindowHandleMouse -- Handle popup window's mouse event externally
	* @param win -- Pointer to popup window
	* @param mainWin -- Pointer to main Window
	* @param x -- Mouse x coordinate
	* @param y -- Mouse y coordinate
	* @param button -- Mouse button state
	*/
	XE_LIB void ChPopupWindowHandleMouse(ChWindow * win, int x, int y, int button);

	/*
	* ChWindowSetFlags -- set window flags
	* @param win -- Pointer to window
	* @param flags -- flags to set
	*/
	XE_LIB void ChWindowSetFlags(ChWindow * win, uint16_t flags);

	/*
	* ChWindowSetGlassMorphism -- apply the glassmorphic look to a window
	* @param win -- Pointer to the window
	* @param alpha -- opacity of the glass pane, 0 (invisible) .. 255 (opaque)
	*
	* Toolkit-level opt-in for the deodhai glass pipeline. The window must be
	* created with WINDOW_FLAG_GLASS (the compositor allocates its backdrop
	* blur buffers at creation time); this call then paints a translucent pane
	* over the whole surface so the blurred desktop shows through, and turns the
	* uniform window fade off so glass pixels are not blended twice.
	*/
	XE_LIB void ChWindowSetGlassMorphism(ChWindow * win, uint8_t alpha);

	/*
     * ChWindowMove -- moves target window to a new location
     * @param win -- Pointer to the window
     * @param newX -- new x location relative to screen coord
     * @param newY -- new y location relative to screen coord
     */
	XE_LIB void ChWindowMove(ChWindow * win, int newX, int newY);

	/*
	* ChWindowRegisterJump -- register long jump address
	* @param win -- Pointer to main window
	*/
	XE_LIB void ChWindowRegisterJump(ChWindow * win);

	/*
	* ChWindowCloseWindow -- clears window related data and
	* sends close message to deodhai
	* @param win -- Pointer to window data
	*/
	XE_LIB void ChWindowCloseWindow(ChWindow * win);

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
	XE_LIB ChWindow* ChCreatePopupWindow(
		ChWindow * win, int x, int y, int w, int h, uint16_t flags, char* title);

	/*
	* ChPopupWindowUpdate -- update the popup window
	* @param pw -- Pointer to Chitralekha Popup Window
	* @param x -- X location
	* @param y -- Y location
	* @param w -- Width of the popup window
	* @param h -- Height of the popup window
	*/
	XE_LIB void ChPopupWindowUpdate(ChPopupWindow * pw, int x, int y, int w, int h);

	/*
	* ChPopupWindowShow -- show the popup window
	* @param pw -- Pointer to Popup Window
	* @param win -- Pointer to Chitralekha Main Window
	*/
	XE_LIB void ChPopupWindowShow(ChWindow * pw, ChWindow * win);

	/*
	* ChPopupWindowUpdateLocation -- update the location of popup window relative to
	* main window
	* @param pwin -- Pointer to Popup Window
	* @param win -- Pointer to Main Window
	* @param x -- X location
	* @param y -- Y location
	*/
	XE_LIB void ChPopupWindowUpdateLocation(ChWindow * pwin, ChWindow * win, int x, int y);

	/*
	* ChPopupWindowHide -- hide the popup window
	* @param pw -- Pointer to Popup Window
	* @param perent -- Parent Window
	*/
	XE_LIB void ChPopupWindowHide(ChWindow * pw, ChWindow * parent);

	/*
     * @brief ChWindowHandleDestroyBuffer -- handle destroy buffer
     * message from server
     * @param win -- Pointer to main window
     */
	XE_LIB void ChWindowHandleDestroyBuffer(ChWindow * win);

	/**
     * @brief ChWindowHandleReinitBuffer -- reinitialize back buffer
     * with requested size
     * @param win -- Pointer to window
     * @param bufferKey -- shared memory key
     */
	XE_LIB void ChWindowHandleReinitBuffer(ChWindow * win, int bufferkey);

#ifdef __cplusplus
}
#endif

#endif
