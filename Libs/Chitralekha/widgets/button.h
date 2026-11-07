/**
* @file button.h
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

#ifndef __BUTTON_H__
#define __BUTTON_H__

#include "window.h"
#include "base.h"
#include <_xeneva.h>

#ifdef __cplusplus
XE_EXTERN {
#endif

	/* style flags (ChButtonSetStyle / ChCreateButtonEx). 0 keeps the
	 * classic painter in button.cpp untouched. */
#define CH_BUTTON_STYLE_DEFAULT	  0
#define CH_BUTTON_FLAT			  (1 << 0) /* flat Gamusa painter (flatbutton.cpp)  */
#define CH_BUTTON_FLAT_SECONDARY  (1 << 1) /* ink outline on paper                 */
#define CH_BUTTON_FLAT_QUIET	  (1 << 2) /* text only, a red thread on hover     */
#define CH_BUTTON_FLAT_LOZENGE	  (1 << 3) /* small lozenge marker before the label */
#define CH_BUTTON_DISABLED		  (1 << 4) /* sand, no hover, no action            */
	/* no variant bit = primary: gamusa red plate, white label */

	typedef struct _CHButton_ {
		ChWidget base;
		char* title;
		/* kept last: apps built against the older layout still work */
		uint16_t styleFlags;
	} ChButton;

	/**
	* @brief ChCreateButton -- Create a button widget
	* @param x -- x coord of the widget
	* @param y -- y coord of the widget
	* @param w -- width of the button
	* @param h -- height of the button
	* @param text -- button text
	*/
	XE_EXPORT ChButton* ChCreateButton(int x, int y, int w, int h, char* text);

	/**
	* @brief ChCreateButtonEx -- create a button with style flags
	* @param flags -- CH_BUTTON_* style flags, e.g. CH_BUTTON_FLAT | CH_BUTTON_FLAT_QUIET
	*/
	XE_EXPORT ChButton* ChCreateButtonEx(int x, int y, int w, int h, char* text, uint16_t flags);

	/**
	* @brief ChButtonSetStyle -- change the painter / variant of a button
	* @param flags -- CH_BUTTON_* style flags, 0 for the classic look
	*/
	XE_EXPORT void ChButtonSetStyle(ChButton * button, uint16_t flags);

#ifdef __cplusplus
}
#endif

#endif