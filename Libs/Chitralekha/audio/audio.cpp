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

#include "audio.h"

/*
 * DeodhaiAudioOpenConnection -- Creates a new connection with Deodhai
 * audio server
 * @param postbox -- postbox file descriptor 
 * @param numChannel -- number of channel -- 1 for MONO, 2 for STEREO
 * @param connectionType -- 0 -- NORMAL connection, 1 -- GLOBAL connection
 */
DeodhaiAudioBox*
DeodhaiAudioOpenConnection(int postbox, uint8_t numChannel, uint8_t connectionType) {
	int pipe = _KeOpenFile("/pipe/DeodhaiAudio", FILE_OPEN_READ_ONLY);
	if (pipe == -1) {
		printf("DeodhaiAudo Connection failed : pipe -> %d \n", pipe);
		return NULL;
	}

	DeodhaiAudioBox* audioBox = (DeodhaiAudioBox*)malloc(sizeof(DeodhaiAudioBox));
	memset(audioBox, 0, sizeof(DeodhaiAudioBox));
	audioBox->pipe = pipe;

	uint16_t threadID = _KeGetThreadID();

	/* Open up the connection */
	DeodhaiAudioMessage* msg = (DeodhaiAudioMessage*)malloc(sizeof(DeodhaiAudioMessage));
	if (connectionType == DEODHAI_CONNECTION_TYPE_NORMAL)
		strcpy(msg->message, DEODHAI_GET_AUDIO_CONNECTION);
	else if (connectionType == DEODHAI_CONNECTION_TYPE_GLOBAL)
		strcpy(msg->message, DEODHAI_AUDIO_GET_GLOBAL_CONNECTION);

	msg->fromProcessId = threadID;
	msg->toProcessId = 0;
	_KeWriteFile(pipe, msg, sizeof(DeodhaiAudioMessage));

	PostEvent e;
	while (1) {
		_KeFileIoControl(postbox, POSTBOX_GET_EVENT, &e);
		if (e.type != 0) {
			if (e.type == 11) {
				_KePrint("DeodhaiAudio Handshake received \n");
				uint16_t controlPanelKey = e.dword;
				uint16_t sampleBufferKey = e.dword2;
				int id = _KeCreateSharedMem(controlPanelKey, 0, 0);
				int id2 = _KeCreateSharedMem(sampleBufferKey, 0, 0);
				void* controlPanelBuff = _KeObtainSharedMem(id, 0, 0);
				void* sampleBuff = _KeObtainSharedMem(id2, 0, 0);
				DeodhaiAudioControlPanel* panel = (DeodhaiAudioControlPanel*)controlPanelBuff;
				audioBox->sampleBuffer = sampleBuff;
				audioBox->ctlPanelBuffer = controlPanelBuff;
				audioBox->controlPanelKey = controlPanelKey;
				audioBox->sampleBufferKey = sampleBufferKey;
				audioBox->ctlPanel = panel;
				break;
			}
		}
		_KeProcessSleep(1000);
	}

	audioBox->ctlPanel->numChannel = numChannel;
	audioBox->ctlPanel->gain = 1.0;
	audioBox->ctlPanel->leftSpeakerScale = 1.0;
	audioBox->ctlPanel->rightSpeakerScale = 1.0;
	audioBox->ctlPanel->Samplefull = false;
	audioBox->ctlPanel->ready = true;

	return audioBox;
}

/*
 * DeodhaiAudioWrite -- write audio samples to 
 * audio box
 * @param box -- Pointer to audio box
 * @param buffer -- sample buffer
 */
void DeodhaiAudioWrite(DeodhaiAudioBox* box, void* buffer) {
	memcpy(box->sampleBuffer, buffer, 4096);
	box->ctlPanel->Samplefull = true;
}

/*
 * DeodhaiAudioCloseConnection -- close an opened audio connection
 * @param box -- Pointer to Deodhai Audio box
 */
void DeodhaiAudioCloseConnection(DeodhaiAudioBox* box) {
	box->ctlPanel->close = true;
	_KeCloseFile(box->pipe);
	_KeUnmapSharedMem(box->sampleBufferKey);
	_KeUnmapSharedMem(box->controlPanelKey);
	free(box);
}
