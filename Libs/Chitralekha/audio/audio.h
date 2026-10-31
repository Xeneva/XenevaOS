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

#ifndef __DEODHAI_AUDIO__
#define __DEODHAI_AUDIO__

#include <_xeneva.h>

#ifdef __cplusplus
XE_EXTERN {
#endif

#include <stdint.h>

#include <stdio.h>
#include <sys/_keproc.h>
#include <sys/_kefile.h>
#include <sys/mman.h>>
#include <sys/iocodes.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/_keipcpostbox.h>

/* Standard Deodhai Request message defined*/
#define DEODHAI_GET_AUDIO_CONNECTION		"DeodhaiGetAudioConnection"
#define DEODHAI_AUDIO_GET_GLOBAL_CONNECTION "DeodhaiAudioGetGlobalConnection"

#define DEODHAI_AUDIO_MONO	 1
#define DEODHAI_AUDIO_STEREO 2

#define DEODHAI_CONNECTION_TYPE_NORMAL 0
#define DEODHAI_CONNECTION_TYPE_GLOBAL 1

#pragma pack(push, 1)
	typedef struct _deodhai_audio_msg_ {
		char message[60];
		uint16_t fromProcessId;
		uint16_t toProcessId;
	} DeodhaiAudioMessage;
#pragma pack(pop)

/*
 * AudioControlPanel is the main way to
 * control each individual stream, feautres
 * like attack, release, Eq, compression,
 * reverb to be added in future
 */
#pragma pack(push, 1)
	typedef struct _audio_control_panel_ {
		uint8_t numChannel;
		float gain;
		float leftSpeakerScale;
		float rightSpeakerScale;
		bool Samplefull;
		bool ready;
		bool close;
		bool global;
		bool dirty;
	} DeodhaiAudioControlPanel;
#pragma pack(pop)

	typedef struct _deodhai_audio_box_ {
		DeodhaiAudioControlPanel* ctlPanel;
		uint16_t controlPanelKey;
		uint16_t sampleBufferKey;
		void* sampleBuffer;
		void* ctlPanelBuffer;
		int pipe;
	} DeodhaiAudioBox;

	/*
 * DeodhaiAudioOpenConnection -- Creates a new connection with Deodhai
 * audio server
 * @param postbox -- postbox file descriptor
 * @param numChannel -- number of channel -- 1 for MONO, 2 for STEREO
 * @param connType -- 0 - NORMAL connection, 1 -- GLOBAL connection
 */
	XE_LIB DeodhaiAudioBox* DeodhaiAudioOpenConnection(
		int postbox, uint8_t numChannel, uint8_t connType);

	/*
 * DeodhaiAudioWrite -- write audio samples to
 * audio box
 * @param box -- Pointer to audio box
 * @param buffer -- sample buffer
 */
	XE_LIB void DeodhaiAudioWrite(DeodhaiAudioBox * box, void* buffer);
	/*
 * DeodhaiAudioCloseConnection -- close an opened audio connection
 * @param box -- Pointer to Deodhai Audio box
 */
	XE_LIB void DeodhaiAudioCloseConnection(DeodhaiAudioBox * box);

#ifdef __cplusplus
}
#endif

#endif
