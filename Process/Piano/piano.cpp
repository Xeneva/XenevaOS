/**
* BSD 2-Clause License
*
* Copyright (c) 2023-2024, Manas Kamal Choudhury
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
#include <math.h>

typedef struct _note_ {
	float frequency;
	float duration;
} Note;

#define SAMPLE_RATE 48000
#define AMPLITUDE	32767

void PianoPlaySineWave(DeodhaiAudioBox* box, Note* note, int16_t* buffer) {
	int sampleCount = (int)(note->duration * SAMPLE_RATE);
	memset(buffer, 0, 4096);

	for (int i = 0; i < 4096; ++i) {
		float t = (float)i / SAMPLE_RATE;
		buffer[i] = (int16_t)(AMPLITUDE * sin(2.0 * M_PI * note->frequency * t));
	}

	if (!box->ctlPanel->Samplefull) {
		DeodhaiAudioWrite(box, buffer);
	}
}

/*
* main -- piano applications main
*/
int main(int argc, char* arv[]) {
	printf("\nPiano v1.0 : Copyright (C) Manas Kamal Choudhury 2024 \n");
	int thrID = _KeGetThreadID();

	int postbox = _KeOpenFile("/dev/postbox", FILE_OPEN_READ_ONLY);
	_KeFileIoControl(postbox, POSTBOX_CREATE, NULL);

	DeodhaiAudioBox* audioBox =
		DeodhaiAudioOpenConnection(postbox, DEODHAI_AUDIO_STEREO, DEODHAI_CONNECTION_TYPE_NORMAL);
	if (!audioBox) {
		printf("piano : audio daemon unavailable \n");
		return -1;
	}
	printf("piano : audio connection initiated successfully \n");

	int sampleCount = (int)(1.0f * SAMPLE_RATE);
	int16_t* buffer = (int16_t*)malloc(4096);

	printf("piano : A minor scale, start note A4 \n");
	printf("piano : using standard tuning for western musical \n");
	printf("piano : use keyboard keys a-to-k to play piano \n");

	Note note;
	memset(&note, 0, sizeof(Note));
	while (1) {
		char key = getchar();
		/*frequencies are from Standard tuning of Western Musical scale*/
		switch (key) {
		case 'w':
			note.frequency = 391.99;
			note.duration = 1.0;
			PianoPlaySineWave(audioBox, &note, buffer);
			break; //G4
		case 'a':
			note.frequency = 440.0;
			note.duration = 1.0;
			PianoPlaySineWave(audioBox, &note, buffer);
			break; //A4
		case 's':
			note.frequency = 493.88;
			note.duration = 1.0;
			PianoPlaySineWave(audioBox, &note, buffer);
			break; //B4
		case 'd':
			note.frequency = 523.25;
			note.duration = 1.0;
			PianoPlaySineWave(audioBox, &note, buffer);
			break; //C5
		case 'f':
			note.frequency = 587.33;
			note.duration = 1.0;
			PianoPlaySineWave(audioBox, &note, buffer);
			break; //D5
		case 'g':
			note.frequency = 659.25;
			note.duration = 1.0;
			PianoPlaySineWave(audioBox, &note, buffer);
			break; //E5
		case 'h':
			note.frequency = 698.46;
			note.duration = 1.0;
			PianoPlaySineWave(audioBox, &note, buffer);
			break; //F5
		case 'j':
			note.frequency = 783.99;
			note.duration = 1.0;
			PianoPlaySineWave(audioBox, &note, buffer);
			break; //F5
		case 'k':
			note.frequency = 880.00;
			note.duration = 1.0;
			PianoPlaySineWave(audioBox, &note, buffer);
			break; //G5
		default:
			note.frequency = 0.0;
			note.duration = 0.0;
			break;
		}
		//
		_KeProcessSleep(500);
	}
}