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

#ifndef __MSG_BOX_H__
#define __MSG_BOX_H__

#include "../chitralekha.h"
#include "window.h"
#include "icon.h"

#ifdef __cplusplus
XE_EXTERN {
#endif

	//! msg box types
#define MSGBOX_TYPE_YESNO	   1
#define MSGBOX_TYPE_OKCANCEL   2
#define MSGBOX_TYPE_ONLYCANCEL 3
#define MSGBOX_TYPE_ONLYCLOSE  4

#define MSGBOX_ICON_WARNING		CHITRALEKHA_ICON_WARNING
#define MSGBOX_ICON_ERROR		0
#define MSGBOX_ICON_SUCCESS		CHITRALEKHA_ICON_SUCCESS
#define MSGBOX_ICON_FAILED		0
#define MSGBOX_ICON_INFORMATION CHITRALEKHA_ICON_INFORMATION

	typedef struct _msgbox_ {
		ChWidget wid;
		uint8_t type;
		char* message;
		uint8_t icon;
		void* backedWindow;
	} ChMessageBox;

	/*
	* ChCreateMessageBox -- create a chitralekha message box
	* @param mainWin -- pointer to main window
	* @param title -- title of the message box
	* @param msg -- message to show
	* @param buttons -- button type
	* @param icon -- icon to show
	*/
	XE_EXPORT ChMessageBox* ChCreateMessageBox(
		ChWindow * mainWin, char* title, char* msg, uint8_t buttons, uint8_t icon);
	/*
	* ChMessageBoxShow - shows the message box
	* @param mb -- Pointer to message box
	*/
	XE_EXPORT void ChMessageBoxShow(ChMessageBox * mb);

#ifdef __cplusplus
}
#endif

#endif