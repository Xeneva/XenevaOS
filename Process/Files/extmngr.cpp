/**
* BSD 2-Clause License
*
* Copyright (c) 2024, Manas Kamal Choudhury
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

#include <stdint.h>
#include <_xeneva.h>
#include <stdio.h>
#include <sys/_keproc.h>
#include <sys/_kefile.h>
#include <sys/iocodes.h>
#include <chitralekha.h>
#include <widgets/base.h>
#include <widgets/button.h>
#include <widgets/scrollpane.h>
#include <widgets/slider.h>
#include <widgets/progress.h>
#include <keycode.h>
#include <widgets/window.h>
#include <widgets/msgbox.h>
#include <widgets/view.h>
#include <string.h>
#include <widgets/menubar.h>
#include <stdlib.h>
#include <time.h>
#include <widgets/menu.h>
#include <ctype.h>
#include "partition.h"

/*
 * VerifyDocExtension -- checks if given document name
 * matches with given extension
 * @param filename -- filename
 * @param extension -- desired extension
 */
extern bool VerifyDocExtension(char* filename, char* extension);
/*
 * FileManagerGetCurrentPath -- get the current path
 */
extern char* FileManagerGetCurrentPath();

void ExtensionManagerLaunch(char* pname, char* app_path, char* filename) {
	char** argv = (char**)malloc(sizeof(char*));
	memset(argv, 0, sizeof(char*));
	char* path = FileManagerGetCurrentPath();
	printf("extension manager: passing path - %s \r\n", path);
	char* fname = (char*)malloc(strlen(path) + strlen(filename));
	memset(fname, 0, strlen(path) + strlen(filename));
	strcpy(fname, path);
	int offset = strlen(fname);
	strcpy(fname + offset, filename);
	argv[0] = fname;
	int pid = _KeCreateProcess(0, pname);
	_KeProcessLoadExec(pid, app_path, 1, argv);
	free(fname);
	free(argv);
}

/*
 * ExtensionManager -- manages launching of applications
 * that is related to desired extension 
 */
void ExtensionManagerSpawn(ChListItem* li) {
	if (VerifyDocExtension(li->itemText, ".JPG") || VerifyDocExtension(li->itemText, ".jpg")) {
		_KePrint("Launching ImageViewer for %s \r\n", li->itemText);
	} else if (VerifyDocExtension(li->itemText, ".BMP") ||
			   VerifyDocExtension(li->itemText, ".bmp")) {
		_KePrint("Launching ImageViewer for %s \r\n", li->itemText);
	} else if (VerifyDocExtension(li->itemText, ".WAV") ||
			   VerifyDocExtension(li->itemText, ".wav")) {
		_KePrint("Launching Accent for %s \r\n", li->itemText);
		ExtensionManagerLaunch("accent", "/audplr.exe", li->itemText);
	} else if (VerifyDocExtension(li->itemText, ".MP3") ||
			   VerifyDocExtension(li->itemText, ".mp3")) {
		_KePrint("Launching Accent for %s \r\n", li->itemText);
	} else if (VerifyDocExtension(li->itemText, ".EXE") ||
			   VerifyDocExtension(li->itemText, ".exe")) {
		char* path = FileManagerGetCurrentPath();
		char* filename = (char*)malloc(strlen(path) + strlen(li->itemText));
		strcpy(filename, path);
		int offset = strlen(filename);
		strcpy(filename + offset, li->itemText);

		for (int i = 0; i < strlen(filename); i++) {
			if (isupper(filename[i]))
				filename[i] = tolower(filename[i]);
		}
		int pid = _KeCreateProcess(0, li->itemText);
		_KeProcessLoadExec(pid, filename, 0, NULL);
		free(filename);
	}
}