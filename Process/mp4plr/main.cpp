/**
* BSD 2-Clause License
*
* Copyright (c) 2022, Manas Kamal Choudhury
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
#include <widgets/window.h>
#include <string.h>
#include <stdlib.h>
#include "minimp4.h"

FILE* _file;

int mp4_read_callb(int64_t offset, void* buffer, size_t sz, void* token) {
	_KePrint("MP4Read callback -> %x \r\n", token);
	_KePrint("MP4Read buffer -> %x \r\n", buffer);
	FILE* f = _file;
	_KePrint("FILE f -> %d \r\n", f->size);
	fseek(f, offset, SEEK_SET);
	return fread(token, sz, 1, f);
}
/*
* main -- main entry
*/
int main(int argc, char* argv[]) {
	FILE* f = fopen("/isr2.mp4", "rb");
	if (!f) {
		printf("Couldn't open mp4 file \n");
		return 1;
	}

	fseek(f, 0, SEEK_END);
	size_t size = ftell(f);
	printf("Size -> %d bytes \n", size);
	_file = f;

	uint8_t* data = (uint8_t*)malloc(4096);
	_KePrint("DATAA BUFFER -> %x \n", data);
	//fread(data, 4096, 1, f);
	MP4D_demux_t demux;
	if (!MP4D_open(&demux, mp4_read_callb, data, size)) {
		printf("Failed to read mp4 file \n");
	}

	while (1) {
		_KePauseThread();
	}
}