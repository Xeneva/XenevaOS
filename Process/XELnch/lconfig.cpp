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

#include "launcher.h"
#include <sys/_kefile.h>
#include <sys/mman.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "button.h"
#include "appgrid.h"

uint8_t* launcher_config_file;

/*
 * LauncherConfigInitialise -- initialise the config file
 */
void LauncherConfigInitialise() {
	launcher_config_file = NULL;
	int fd = _KeOpenFile("/lncher.cnf", FILE_OPEN_READ_ONLY);
	if (fd == -1) {
		printf("failed to open launcher config file \n");
		return;
	}
	XEFileStatus stat;
	_KeFileStat(fd, &stat);
	printf("Launcher config file sz -> %d bytes \n", stat.size);
	launcher_config_file = (uint8*)_KeMemMap(NULL, stat.size, 0, 0, MEMMAP_NO_FILEDESC, 0);
	memset(launcher_config_file, 0, stat.size);

	_KeReadFile(fd, launcher_config_file, stat.size);
	printf("Launcher config file read \n");
	/* now read is completed, close the file */
	//_KeCloseFile(fd);
}

/*
 * LauncherSetupByConfigFile -- setup the launcher
 * by config file
 */
void LauncherSetupByConfigFile() {
	if (!launcher_config_file) {
		printf("Config file buffer is zero \n");
		return;
	}
	bool _last_entry_ = false;
	AppGrid* grid = XELauncherGetAppGrid();
	char* fbuf = (char*)launcher_config_file;
	while (1) {
		char* p = strchr(fbuf, '{'); //beginning of an entry
		if (!p)
			break;
		p += 8; //skip '['

		fbuf = p;

		/* get the title */
		char title[42];
		memset(title, 0, 42);
		for (int i = 0; i < 42; i++) {
			if (p[i] == '|') {
				fbuf++;
				break;
			}
			title[i] = p[i];
			fbuf++;
		}

		p = strchr(fbuf, ']'); //end of a keyword line, skip to icon name
		if (p)
			p++;

		fbuf = p;
		char icon[42];
		memset(icon, 0, 42);
		for (int i = 0; i < 42; i++) {
			if (p[i] == '|') {
				fbuf++;
				break;
			}
			icon[i] = p[i];
			fbuf++;
		}

		p = strchr(fbuf, ']');
		if (p)
			p++;

		fbuf = p;

		char param[42];
		memset(param, 0, 42);
		for (int i = 0; i < 42; i++) {
			if (p[i] == '|') {
				fbuf++;
				break;
			}
			param[i] = p[i];
			fbuf++;
		}

		p = strchr(fbuf, ']');
		if (p)
			p++;

		/* appname is the last keyword entry */
		char app[42];
		memset(app, 0, 42);
		for (int i = 0; i < 42; i++) {
			if (p[i] == '}') {
				fbuf++;
				break;
			}
			if (p[i] == '>') {
				_last_entry_ = true;
				break;
			}
			app[i] = p[i];
			fbuf++;
		}

		LaunchButton* button =
			CreateLaunchButton(0, 0, LAUNCH_BUTTON_W, LAUNCH_BUTTON_H, title, app);
		button->param = (char*)malloc(strlen(param) + 1);
		strcpy(button->param, param);
		ButtonIcon* ico = CreateLaunchButtonIcon(icon, button);
		AppGridAddButton(grid, button);

		if (_last_entry_)
			break;
	}
}
