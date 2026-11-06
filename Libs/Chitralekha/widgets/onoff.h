/**
* BSD 2-Clause License
*
* Copyright (c) 2025, Manas Kamal Choudhury
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

#ifndef __ONOFF_H__
#define __ONOFF_H__

#include <stdint.h>
#include "base.h"
#include "../chitralekha.h"
#include "window.h"

#define CH_ONOFF_VALUE_ON  1
#define CH_ONOFF_VALUE_OFF 0

typedef struct _onoff_ {
	ChWidget wid;
	uint8_t value;
} ChOnOffButton;

/*
 * ChCreateOnOffButton -- creates a onoff button
 * @param x -- X location on the window
 * @param y -- Y location on the window
 * @param defaultState -- default state of the button
 */
XE_EXTERN XE_LIB ChOnOffButton* ChCreateOnOffButton(int x, int y, uint8_t defaultState);

/*
 * ChOnOffGetValue -- return onoff buttons value
 * @param onoff -- Pointer onoff button
 */
XE_EXTERN XE_LIB uint8_t ChOnOffGetValue(ChOnOffButton* onoff);

#endif