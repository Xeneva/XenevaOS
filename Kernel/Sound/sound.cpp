/**
* BSD 2-Clause License
*
* Copyright (c) 2021, Manas Kamal Choudhury
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

#include <Sound/sound.h>
#include <_null.h>
#include <Mm/kmalloc.h>
#include <Mm/pmmngr.h>
#include <Mm/vmmngr.h>
#include <Fs/vfs.h>
#include <Fs/dev/devfs.h>
#include <aurora.h>
#include <Serv/sysserv.h>
#include <string.h>
#include <aucon.h>
#include <Hal/serial.h>
#include <Hal/x86_64_hal.h>


AuSound *_Registered_dev;

AuDSP* dsp_first;
AuDSP* dsp_last;
uint8_t* mixbuf;

bool _audio_started_;
bool _audio_stopped_;

#define SOUND_REGISTER_SNDPLR 100
#define SOUND_START_OUTPUT    102
#define SOUND_STOP_OUTPUT     103
#define SOUND_START_INPUT     104
#define SOUND_STOP_INPUT      105
#define SOUND_SET_VOLUME      106
#define SOUND_GET_VOLUME      107
#define SOUND_MUTE_ENABLE     108
#define SOUND_MUTE_DISABLE    109
#define SOUND_READ_AVAIL      110
#define SOUND_UNREGISTER_SNDPLR 111

#define SND_BUFF_SZ  PAGE_SIZE
/*
* AuSoundAddDSP -- adds a dsp to the dsp list
* @param dsp -- dsp to add
*/
void AuSoundAddDSP(AuDSP *dsp) {
	dsp->next = NULL;
	dsp->prev = NULL;
	if (dsp_first == NULL) {
		dsp_first = dsp;
		dsp_last = dsp;
	}
	else {
		dsp_last->next = dsp;
		dsp->prev = dsp_last;
		dsp_last = dsp;
	}
}

/*
* AuRemoveDSP -- removes a dsp from the dsp list
* @param dsp -- dsp to remove
*/
void AuRemoveDSP(AuDSP *dsp) {
	if (dsp_first == NULL)
		return;

	if (dsp == dsp_first) {
		dsp_first = dsp_first->next;
	}
	else {
		dsp->prev->next = dsp->next;
	}

	if (dsp == dsp_last) {
		dsp_last = dsp->prev;
	}
	else {
		dsp->next->prev = dsp->prev;
	}
}

AuDSP* AuSoundGetDSP(uint16_t id) {
	for (AuDSP* dsp = dsp_first; dsp != NULL; dsp = dsp->next) {
		if (dsp->_dsp_id == id)
			return dsp;
	}

	return NULL;
}


/*
 * AuSoundGetBlock -- the main heart of aurora sound
 * system -- called by sound card
 */
void AuSoundGetBlock(uint64_t *buffer) {

	if (dsp_first == NULL)
		return;
	
	if (mixbuf == NULL)
		return;

	int16_t* hw_buffer = (int16_t*)buffer;

	for (int i = 0; i < SND_BUFF_SZ / sizeof(int16_t); i++)
		hw_buffer[i] = 0;
	
	for (AuDSP* dsp = dsp_first; dsp != NULL; dsp = dsp->next) {
		uint8_t* mixing_zone = mixbuf;
		memset(mixing_zone, 0, SND_BUFF_SZ);
		for (int i = 0; i < SND_BUFF_SZ; i++){
			AuCircBufGet(dsp->buffer, mixing_zone);
			mixing_zone++;
		}

		int16_t *data_16 = (int16_t*)mixbuf;

		for (int i = 0; i < SND_BUFF_SZ / sizeof(int16_t); i++)
			data_16[i] /= 2;

		/* experiments for stereo panning and gain control over
		 * each channel */
		for (int i = 0; i < SND_BUFF_SZ / sizeof(int16_t); i++){
			hw_buffer[i] += data_16[i];
		}
	}

	for (AuDSP *dsp = dsp_first; dsp != NULL; dsp = dsp->next) {
		if (dsp->SndThread) {
			if (dsp->SndThread->pendingSigCount > 0)
				return;
			AuUnblockThread(dsp->SndThread);
		}
	}

}


size_t AuSoundRead(AuVFSNode* fsys, AuVFSNode* file, uint64_t* buffer, uint32_t length) {
	if (_Registered_dev == NULL)
		return -1;
	return 0;
}

size_t AuSoundWrite(AuVFSNode* fsys, AuVFSNode* file, uint64_t* buffer, uint32_t length) {
	x64_cli();
	if (!_Registered_dev)
		return 0;
	AuThread* t = AuGetCurrentThread();
	AuDSP* dsp = AuSoundGetDSP(t->id);
	uint8_t *aligned_buf = (uint8_t*)buffer;
	
	if (CircBufFull(dsp->buffer)){
		if (dsp->SndThread->pendingSigCount > 0)
			return 0;
		AuBlockThread(dsp->SndThread);
		AuForceScheduler();	
	}
	for (int i = 0; i < SND_BUFF_SZ; i++) { //
		AuCircBufPut(dsp->buffer, aligned_buf[i]);
	}

	return SND_BUFF_SZ;
}

/*
 * AuSoundSetCard -- registers a new
 * sound card
 * @param dev -- sound card device
 */
void AuSoundSetCard(AuSound* dev) {
	if (_Registered_dev)
		return;
	_Registered_dev = dev;
}

/*
* AuSoundStart -- Starts the Sound card
*/
void AuSoundStart() {
	if (_Registered_dev == NULL)
		return;
	_Registered_dev->start_output();
}

/*
* AuSoundStop -- Stop Sound card
*/
void AuSoundStop() {
	if (_Registered_dev == NULL)
		return;
	_Registered_dev->stop_output();
}


int AuSoundIOControl(AuVFSNode* node, int code, void* arg) {
	AuFileIOControl *_ioctl = (AuFileIOControl*)arg;


	AuThread *thr = AuGetCurrentThread();
	switch (code)
	{
	case SOUND_REGISTER_SNDPLR: {
									
									AuDSP* dsp = (AuDSP*)kmalloc(sizeof(AuDSP));
									memset(dsp, 0, sizeof(AuDSP));
									uint8_t* buffer = (uint8_t*)P2V((size_t)AuPmmngrAlloc());
									memset(buffer, 0, PAGE_SIZE);
									dsp->buffer = AuCircBufInitialise(buffer, SND_BUFF_SZ);
									dsp->_dsp_id = thr->id;
									dsp->SndThread = thr;
									dsp->sleep_time = _ioctl->uint_1;
									dsp->available = true;
									AuSoundAddDSP(dsp);
									break;
	}
	case SOUND_READ_AVAIL: {
							   AuDSP *dsp_ = AuSoundGetDSP(thr->id);
							   _ioctl->uchar_2 = dsp_->available;
							   break;

	}
	case SOUND_SET_VOLUME: {
							   if (!_Registered_dev)
								   return -1;
							   _Registered_dev->set_vol(_ioctl->uchar_1);
							   break;
	}
	case SOUND_UNREGISTER_SNDPLR:{
									 AuSoundRemoveDSP(thr->id);
									 break;

	}
	default:
		break;
	}
	return 0;
}

/**
* @brief AuSoundInitialize -- Initialized the Aurora sound system
*/
void AuSoundInitialise() {
	AuVFSNode* fsys = AuVFSFind("/dev");
	AuVFSNode *dsp = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(dsp, 0, sizeof(AuVFSNode));
	strcpy(dsp->filename, "sound");
	dsp->flags =  FS_FLAG_DEVICE;
	dsp->device = fsys;
	dsp->read = AuSoundRead;
	dsp->write = AuSoundWrite;
	dsp->iocontrol = AuSoundIOControl;
	AuDevFSAddFile(fsys, "/", dsp);

	dsp_first = NULL;
	dsp_last = NULL;
	_audio_started_ = false;
	_audio_stopped_ = false;
	mixbuf = (uint8_t*)P2V((uint64_t)AuPmmngrAlloc());
	memset(mixbuf, 0, PAGE_SIZE);

	AuDSP* dsp_ = (AuDSP*)kmalloc(sizeof(AuDSP));
	memset(dsp_, 0, sizeof(AuDSP));
	uint8_t* buffer = (uint8_t*)P2V((size_t)AuPmmngrAlloc());
	dsp_->buffer = AuCircBufInitialise(buffer, SND_BUFF_SZ);
	dsp_->_dsp_id = 0;
	dsp_->SndThread = 0;
	dsp_->sleep_time = 0;
	dsp_->available = false;
	AuSoundAddDSP(dsp_);
}

/*
 * AuSoundRemoveDSP -- remove the dsp from
 * dsp list
 */
void AuSoundRemoveDSP(uint16_t id) {
	AuDSP* dsp_ = AuSoundGetDSP(id);
	if (dsp_) {
		AuPmmngrFree((void*)V2P((size_t)dsp_->buffer->buffer));
		AuCircBufFree(dsp_->buffer);
		AuRemoveDSP(dsp_);
		kfree(dsp_);
	}
}
