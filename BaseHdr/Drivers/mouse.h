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

/*
 * PS2 mouse -- system 
 */

#ifndef __MOUSE_H__
#define __MOUSE_H__

#include <stdint.h>
#include <aurora.h>

#pragma pack(push,1)
typedef struct __ps2mouse__ {
	uint8_t mouse_cycle;
	uint8_t mouse_byte[5];
	int32_t mouse_x;
	int32_t mouse_y;
	int32_t mouse_x_diff;
	int32_t mouse_y_diff;
	uint8_t prev_button[3];
	uint8_t curr_button[3];
	uint32_t mouse_butt_state;
	uint8_t mouse_mode;
	volatile int32_t mouse_button;
}PS2Mouse;
#pragma pack(pop)

/*
* AuPS2MouseInitialise -- initialise the ps2 mouse system
*/
extern void AuPS2MouseInitialise();

/*
* AuPS2MouseSetPos -- set custom mouse position
* rather than default (0,0) position
* @param x -- x position
* @param y -- y position
*/
extern void AuPS2MouseSetPos(int32_t x, int32_t y);

AU_EXTERN AU_EXPORT void AuPS2DisableMouse();
#endif