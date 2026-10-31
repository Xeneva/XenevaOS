/**
* @file _ketty.h
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

#ifndef __KE_TTY_H__
#define __KE_TTY_H__

#include <stdint.h>
#include <_xeneva.h>

#ifdef __cplusplus
XE_EXTERN {
#endif

	/* iocontrol codes */
#define TIOCGWINSZ 0x5401
#define TIOCSWINSZ 0x5402
#define TIOCFLUSH  0x5403
#define TIOCGATTR  0x5404
#define TIOSPGRP   0x5405

	typedef struct _win_size_ {
		uint16_t ws_row;
		uint16_t ws_col;
		uint16_t ws_xpixel;
		uint16_t ws_ypixel;
	} WinSize;

	/**
	 * @brief _KeCreateTTY -- create a new terminal
	 * @param master_fd -- Pointer to a memory area where to
	 * store master file descriptor
	 * @param slave_fd -- Pointer to a memory area where to
	 * store slave file descriptor
	 */
	XE_LIB int _KeCreateTTY(int* master_fd, int* slave_fd);

#ifdef __cplusplus
}
#endif

#endif