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

#ifndef __LAUNCHER_H__
#define __LAUNCHER_H__

#include "appgrid.h"

#define LAUNCH_BUTTON_W 100
#define LAUNCH_BUTTON_H 100

#define LAUNCH_BUTTON_ICON_W	  75
#define LAUNCH_BUTTON_ICON_H	  70
#define LAUNCHER_BACKGROUND_COLOR 0x1A48494B //3A3A3A

/** Init Request msgs **/
#define INIT_REQUEST_PW_DOWN   "init.request.powerdown"
#define INIT_REQUEST_PW_REBOOT "init.request.reboot"

#pragma pack(push, 1)
typedef struct _bmp_ {
	unsigned short type;
	unsigned int size;
	unsigned short resv1;
	unsigned short resv2;
	unsigned int off_bits;
} BMP;

typedef struct _info_ {
	unsigned int biSize;
	long biWidth;
	long biHeight;
	unsigned short biPlanes;
	unsigned short biBitCount;
	unsigned int biCompression;
	unsigned int biSizeImage;
	long biXPelsPerMeter;
	long biYPelsPerMeter;
	unsigned int biClrUsed;
	unsigned int biClrImportant;
} BMPInfo;
#pragma pack(pop)

typedef struct _init_request_msg_ {
	char message[60];
	uint16_t fromProcessId;
	uint16_t toProcessId;
} InitRequestMsg;

/*
* LauncherConfigInitialise -- initialise the config file
*/
extern void LauncherConfigInitialise();

/*
* LauncherSetupByConfigFile -- setup the launcher
* by config file
*/
extern void LauncherSetupByConfigFile();

/*
* XELauncherGetAppGrid -- returns the main
* application grid
*/
extern AppGrid* XELauncherGetAppGrid();

extern ChWindow* XELauncherGetMainWin();

extern int xelaunch_get_pipe();

#endif