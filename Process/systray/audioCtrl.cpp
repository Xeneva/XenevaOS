/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2025, Manas Kamal Choudhury
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

#include "audioCtrl.h"
#include <math.h>

/*
 * TrayGetSoundControl -- obtains a sound control from 
 * sound server
 * @param app -- Pointer to Chitralekha App substance
 */
DeodhaiAudioBox* TrayGetSoundControl(ChitralekhaApp* app) {
	int threadID = app->currentID;
	int postBox = app->postboxfd;
	DeodhaiAudioBox* audioBox =
		DeodhaiAudioOpenConnection(postBox, DEODHAI_AUDIO_STEREO, DEODHAI_CONNECTION_TYPE_GLOBAL);
	if (audioBox->sampleBuffer != NULL) {
		_KePrint("Global audio connection was successfull , global -> %d \r\n",
				 audioBox->ctlPanel->global);

		audioBox->ctlPanel->ready = 1;
		return audioBox;
	} else
		return NULL;
}

float exp(float x) {
	return 1.0f + x + (x * x) / 2.0f + (x * x * x) / 6.0f;
}

float pow10(float x) {
	return exp(x * 2.302585093f);
}

float TrayAudioStepToGain(float sliderVal, float sliderMaxVal) {
	float minDB = -60.0f;
	float maxDB = 0.0f;

	if (sliderVal <= 0.0)
		return 0.0f;
	_KePrint("sliderval -> %f \r\n", sliderVal);
	float norm = sliderVal / sliderMaxVal;
	float dB = minDB + (maxDB - minDB) * norm;
	return pow10(dB / 20.0f);
}