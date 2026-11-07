/**
* @file base.cpp
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

#include "base.h"
#include <sys/_kefile.h>
#include <sys/_keproc.h>
#include "../font.h"
#include "../keycode.h"
#include "icon.h"

ChitralekhaApp* baseapp;

/**
 * @brief ChitralekhaStartApp -- start an application instance
 */
ChitralekhaApp* ChitralekhaStartApp(int argc, char* argv[]) {
	int postboxfd = _KeOpenFile("/dev/postbox", FILE_OPEN_READ_ONLY);
	_KePrint("[Chitralekha]: postboxfd : %d \r\n", postboxfd);
	int fd = postboxfd; // _KeOpenFile("/dev/graph", FILE_OPEN_READ_ONLY);
	_KePrint("[Chitralekha]: graph : %d \r\n", fd);
#ifdef ARCH_ARM64
	_KeFileIoControl(fd, POSTBOX_CREATE, NULL);
#else
	_KeFileIoControl(postboxfd, POSTBOX_CREATE, NULL);
#endif

	//#ifdef ARCH_X64
	ChFont* font = ChInitialiseFont(XENEVA_DEFAULT_FONT);
	//#else
	//ChFont* font = 0;
	//#endif
	ChitralekhaApp* app = (ChitralekhaApp*)malloc(sizeof(ChitralekhaApp));
	memset(app, 0, sizeof(ChitralekhaApp));
	app->baseFont = font;
#ifdef ARCH_ARM64
	app->postboxfd = fd;
#else
	app->postboxfd = postboxfd;
#endif
	uint16_t thr_id = _KeGetThreadID();
	app->currentID = thr_id;

	ChitralekhaKeyInitialise();
	ChInitialiseDefaultIcons();
	return app;
}

/*
* ChitralekhaStartApp -- start an application instance
*/
ChitralekhaApp* ChitralekhaStartSubApp(ChitralekhaApp* parent) {
	ChitralekhaApp* app = (ChitralekhaApp*)malloc(sizeof(ChitralekhaApp));
	memset(app, 0, sizeof(ChitralekhaApp));
	app->baseFont = parent->baseFont;
	app->postboxfd = parent->postboxfd;
	uint16_t thr_id = parent->currentID;
	app->currentID = thr_id;
	app->parent = parent;
	return app;
}
/**
 * @brief ChitralekhaGetApp -- return running application instance
 */
ChitralekhaApp* ChitralekhaGetApp() {
	return baseapp;
}