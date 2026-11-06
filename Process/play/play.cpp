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
#include <sys/mman.h>
#include <sys/_kefile.h>
#include <sys/iocodes.h>
#include <string.h>
#include <stdlib.h>
#include <sys/_keipcpostbox.h>
#include <sys/socket.h>
#include <audio/audio.h>

void drawAnim() {
	int i = 0;
	while (1) {
		printf("\r");
		printf("Animation :%d", i++);
		_KeProcessSleep(10000);
	}
}
/*
* main -- terminal emulator
*/
int main(int argc, char* arv[]) {
	/* demo track shipped in the image root */
	char* filename = "/snd.wav";
	if (argc > 1) {
		if (strcmp(arv[1], "-help") == 0) {
			printf("\n Play v1.0 -- A simple wave file player \n");
			printf("Copyright (C) Manas Kamal Choudhury 2024 \n");
			printf("Available commands : '-help', '-file' \n");
			printf("To play a file: \n");
			printf("      play -file /<yourfilename>.wav \n");
			return 0;
		}
		if (strcmp(arv[1], "-file") == 0) {
			if (argc < 3 || !arv[2]) {
				printf("No file specified to play \n");
				return -1;
			}
			filename = arv[2];
		}
	}
	int thrID = _KeGetThreadID();

	int postbox = _KeOpenFile("/dev/postbox", FILE_OPEN_READ_ONLY);
	_KeFileIoControl(postbox, POSTBOX_CREATE, NULL);
	printf("\n");

	DeodhaiAudioBox* audioBox =
		DeodhaiAudioOpenConnection(postbox, DEODHAI_AUDIO_STEREO, DEODHAI_CONNECTION_TYPE_NORMAL);
	if (!audioBox) {
		printf("play: audio daemon unavailable \n");
		return -1;
	}
	printf("play: audio connection initiated successfully \n");

	/* now open your sound file, note that here demo is playing
	* a raw wave file with 48kHZ-16bit format, to play mp3 or
	* other format, one needs another conversion layer of samples */

	int song = _KeOpenFile(filename, FILE_OPEN_READ_ONLY);
	if (song == -1) {
		printf("No file found \n");
		return -1;
	}

	void* songbuf = malloc(4096);
	memset(songbuf, 0, 4096);
	uint8_t* alignedSongBuf = (uint8_t*)songbuf;

	XEFileStatus fs;
	_KeFileStat(song, &fs);
	bool finished = 0;
	bool primed = 0;

	while (1) {
		/* 4096 bytes is one mixer period (~21 ms at 48 kHz).
		 * Deodhai's card write is what waits that period out. */
		_KeFileStat(song, &fs);

		if (fs.eof) {
			finished = 1;
			DeodhaiAudioCloseConnection(audioBox);
			_KeCloseFile(song);
			_KeProcessExit();
		}

		if (!finished) {
			if (!audioBox->ctlPanel->Samplefull) {
				size_t n = _KeReadFile(song, songbuf, 4096);
				if (n == 0 || n > 4096)
					n = 4096;
				if (!primed && n >= 12 &&
					alignedSongBuf[0] == 'R' && alignedSongBuf[1] == 'I' &&
					alignedSongBuf[2] == 'F' && alignedSongBuf[3] == 'F') {
					/* Standard PCM wav: skip the header so the card
					 * receives samples, not the RIFF chunk. */
					int off = 12;
					while (off + 8 <= (int)n) {
						int csz = alignedSongBuf[off + 4] |
								  (alignedSongBuf[off + 5] << 8) |
								  (alignedSongBuf[off + 6] << 16) |
								  (alignedSongBuf[off + 7] << 24);
						if (alignedSongBuf[off] == 'd' && alignedSongBuf[off + 1] == 'a' &&
							alignedSongBuf[off + 2] == 't' && alignedSongBuf[off + 3] == 'a') {
							off += 8;
							break;
						}
						off += 8 + csz;
						if (csz & 1)
							off++;
					}
					if (off < (int)n && off > 0) {
						memmove(alignedSongBuf, alignedSongBuf + off, n - (size_t)off);
						memset(alignedSongBuf + (n - (size_t)off), 0, (size_t)off);
					}
				}
				primed = 1;
				DeodhaiAudioWrite(audioBox, songbuf);
			} else {
				/* One shared slot. The mixer clears Samplefull before
				 * its card write, and that write already blocks for the
				 * ~21 ms this chunk lasts. A long sleep here leaves the
				 * headset idle between periods; btctl has no such wait. */
				_KeProcessSleep(2);
			}
		}
	}
}