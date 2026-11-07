/**
* @file base.h
* 
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

#ifndef __CH_WIDGET_BASE_H__
#define __CH_WIDGET_BASE_H__

#include <stdint.h>
#include <sys/_keipcpostbox.h>
#include "../font.h"
#include <_xeneva.h>

/* commands to deodhai */
#define DEODHAI_MESSAGE_CREATEWIN		   50
#define DEODHAI_MESSAGE_WINDESTROY		   51
#define DEODHAI_MESSAGE_BROADCAST_ICON	   52
#define DEODHAI_MESSAGE_WINDOW_BRING_FRONT 53
#define DEODHAI_MESSAGE_WINDOW_HIDE		   54
#define DEODHAI_MESSAGE_GETWINDOW		   55
#define DEODHAI_MESSAGE_CLOSE_WINDOW	   56
#define DEODHAI_MESSAGE_SET_FLAGS		   57
#define DEODHAI_MESSAGE_CREATE_POPUP	   58
#define DEODHAI_MESSAGE_MOUSE_DBLCLK	   59
#define DEODHAI_MESSAGE_MOVE_WINDOW		   60
#define DEODHAI_MESSAGE_BUFFER_DESTROYED   61

#define DEODHAI_REPLY_WINCREATED	 150
#define DEODHAI_REPLY_MOUSE_EVENT	 151
#define DEODHAI_REPLY_KEY_EVENT		 152
#define DEODHAI_REPLY_WINDOW_ID		 153
#define DEODHAI_REPLY_FOCUS_CHANGED	 154
#define DEODHAI_REPLY_WINDOW_CLOSED	 156
#define DEODHAI_REPLY_MOUSE_LEAVE	 157
#define DEODHAI_REPLY_TOUCH_EVENT	 158
#define DEODHAI_REPLY_DESTROY_BUFFER 159
#define DEODHAI_REPLY_REINIT_BUFFER	 160

/* deodhai broadcast definitions, reply from
	 * deodhai */
#define DEODHAI_BROADCAST_WINCREATED	170
#define DEODHAI_BROADCAST_WINDESTROYED	171
#define DEODHAI_BROADCAST_ICON			174
#define DEODHAI_BROADCAST_FOCUS_CHANGED 173

#define DEODHAI_MOUSE_MSG_SCROLL_UP	  0x05
#define DEODHAI_MOUSE_MSG_SCROLL_DOWN 0x06

/** SPECIAL MESSAGE CODE -- are responded by
 *  broadcast listener
 */
#define DEODHAI_FORCE_OPEN_LAUNCHER		400
#define DEODHAI_FORCE_OPEN_SETTINGS		401
#define DEODHAI_FORCE_OPEN_SEARCH		402
#define DEODHAI_FORCE_OPEN_CALENDER		403
#define DEODHAI_FORCE_OPEN_TERMINAL		404
#define DEODHAI_FORCE_OPEN_NOTIFICATION 405

typedef struct _ChApp_ {
	int postboxfd;
	int sharedWinkey;
	int backbufkey;
	void* fb;
	void* shwinbuf;
	int buffer_width;
	int buffer_height;
	uint32_t windowHandle;
	ChFont* baseFont;
	uint16_t currentID;
	struct _ChApp_* parent;
} ChitralekhaApp;

#ifdef __cplusplus
XE_EXTERN {
#endif

	/**
	* @brief ChitralekhaStartApp -- start an application instance
	*/
	XE_LIB ChitralekhaApp* ChitralekhaStartApp(int argc, char* argv[]);

	/**
	* @brief ChitralekhaStartApp -- start an application instance
	*/
	XE_LIB ChitralekhaApp* ChitralekhaStartSubApp(ChitralekhaApp * parent);

	/**
	* @brief ChitralekhaGetApp -- return running application instance
	*/
	XE_LIB ChitralekhaApp* ChitralekhaGetApp();

#ifdef __cplusplus
}
#endif

#endif