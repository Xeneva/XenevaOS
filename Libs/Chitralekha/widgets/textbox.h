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

#ifndef __TEXT_BOX_H__
#define __TEXT_BOX_H__

#include <stdint.h>
#include "base.h"
#include "window.h"

#define CH_TEXTBOX_MAX_STRING 32767 //~32KiB

typedef struct _text_box_ {
	ChWidget wid;
	bool editable;
	char* text;
	//to PlainText, toHTML properties
	//font format
	ChFont* font;
	uint32_t textColor;
	uint32_t textBackgroundColor;
	int textWidth;
	int textHeight;
	//cursor
	int textCursorPosX;
	int textCursorPosY; // future use
} ChTextBox;

#ifdef __cplusplus
XE_EXTERN {
#endif

	/*
	 * ChCreateTextBox -- create a new instance of textbox widget
	 * @param x -- X position
	 * @param y -- Y position
	 * @param width -- Width of the textbox
	 * @param height -- Height of the textbox
	 */
	XE_EXPORT ChTextBox* ChCreateTextBox(ChWindow * win, int x, int y, int width, int height);

	/*
     * ChTextBoxSetText -- set text to textbox
     * @param tb -- Pointer to textbox
     * @param text -- text to set
     */
	XE_EXPORT void ChTextBoxSetText(ChTextBox * tb, char* text);

	/*
    * ChTextBoxUpdate -- updates the textbox with
    * current graphical content
    * @param tb -- Pointer to textbox
    * @param mainWin -- Pointer to main window
    */
	XE_EXPORT void ChTextBoxUpdate(ChTextBox * tb, ChWindow * mainWin);

	/*
     * ChTextBoxSetFont -- set font for particular given
     * text box
     * @param tb -- Pointer to textbox
     * @param font -- Font to set
     */
	XE_EXPORT void ChTextBoxSetFont(ChTextBox * tb, ChFont * font);

	/*
    * ChTextBoxSetFontSize -- set font size for the text box
    * @param tb -- Pointer to teh textbox
    * @param sz -- font sz in pixels
    */
	XE_EXPORT void ChTextBoxSetFontSize(ChTextBox * tb, int sz);

#ifdef __cplusplus
}
#endif

#endif
