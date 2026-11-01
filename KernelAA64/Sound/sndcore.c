/**
* @file sndcore.c
*
* BSD 2-Clause License
*
* Copyright (c) 2022-2026, Manas Kamal Choudhury
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
#include <Mm/kmalloc.h>
#include <Fs/vfs.h>
#include <Fs/Dev/devfs.h>
#include <Cred/cred.h>
#include <Cred/group.h>
#include <aucon.h>
#include <_null.h>
#include <Drivers/uart.h>
#include <Mm/vmmngr.h>
#include <Mm/pmmngr.h>
#include <string.h>

#define AURORA_MAX_SOUND_CARDS 256

AuSound* _cards[AURORA_MAX_SOUND_CARDS];
int au_sound_last_id = -1;

static AuDSP* dsp_first;
static AuDSP* dsp_last;
static uint8_t* mixbuf;

static bool _audio_started_;
static bool _audio_stopped_;

#define SOUND_REGISTER_SNDPLR	100
#define SOUND_START_OUTPUT		102
#define SOUND_STOP_OUTPUT		103
#define SOUND_START_INPUT		104
#define SOUND_STOP_INPUT		105
#define SOUND_SET_VOLUME		106
#define SOUND_GET_VOLUME		107
#define SOUND_MUTE_ENABLE		108
#define SOUND_MUTE_DISABLE		109
#define SOUND_READ_AVAIL		110
#define SOUND_UNREGISTER_SNDPLR 111
#define SOUND_REGISTER_CARD		112
#define SOUND_GET_CARD_LIST		113
#define SOUND_GET_CARD_TOTALNUM 114

#define SND_BUFF_SZ PAGE_SIZE
/**
 * TODO: Support for controlling jack pins
 * ADC/DAC add removal support
 */

typedef struct _sound_card_list {
	char name[32];
	int cardID;
	struct _sound_card_list* next;
} aurora_snd_card_list;

/**
* @brief AuSoundAddDSP -- adds a dsp to the dsp list
* @param dsp -- dsp to add
*/
void AuSoundAddDSP(AuDSP* dsp) {
	dsp->next = NULL;
	dsp->prev = NULL;
	if (dsp_first == NULL) {
		dsp_first = dsp;
		dsp_last = dsp;
	} else {
		dsp_last->next = dsp;
		dsp->prev = dsp_last;
		dsp_last = dsp;
	}
}

/**
* @brief AuRemoveDSP -- removes a dsp from the dsp list
* @param dsp -- dsp to remove
*/
void AuRemoveDSP(AuDSP* dsp) {
	if (dsp_first == NULL)
		return;

	if (dsp == dsp_first) {
		dsp_first = dsp_first->next;
	} else {
		dsp->prev->next = dsp->next;
	}

	if (dsp == dsp_last) {
		dsp_last = dsp->prev;
	} else {
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

/**
 * @brief _AuSoundGetFreeID -- searches for free id
 * inside the _cards slot
 */
static int _AuSoundGetFreeID() {
	for (int index = 0; index < AURORA_MAX_SOUND_CARDS; index++) {
		if (_cards[index] == 0)
			return index;
	}
	return -1;
}

/**
 * @brief AuSoundIOControl -- io control for sound file
 */
int AuSoundIOControl(AuVFSNode* node, int code, void* arg) {
	AuFileIOControl* _ioctl = (AuFileIOControl*)arg;

	AA64Thread* thr = AuGetCurrentThread();
	switch (code) {
	case SOUND_REGISTER_SNDPLR: {
		UARTDebugOut("Registering sound player \r\n");
		AuDSP* dsp = (AuDSP*)kmalloc(sizeof(AuDSP));
		memset(dsp, 0, sizeof(AuDSP));
		uint8_t* buffer = (uint8_t*)P2V((size_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));

		UARTDebugOut("buffer : %x \r\n", buffer);
		memset(buffer, 0, PAGE_SIZE);
		dsp->buffer = AuCircBufInitialise(buffer, SND_BUFF_SZ);
		dsp->_dsp_id = thr->thread_id;
		dsp->SndThread = thr;
		dsp->sleep_time = _ioctl->uint_1;
		dsp->available = true;
		UARTDebugOut("UINT_2 value : %d \r\n", _ioctl->uint_2);
		if (_ioctl->uint_2 != UINT32_MAX) {
			dsp->_cardID = _ioctl->uint_2;
			if (dsp->_cardID >= 0 && dsp->_cardID < AURORA_MAX_SOUND_CARDS &&
				_cards[dsp->_cardID]) {
				strncpy(dsp->card_name, _cards[dsp->_cardID]->name, sizeof(dsp->card_name) - 1);
				dsp->card_name[sizeof(dsp->card_name) - 1] = 0;
			}
		} else
			dsp->_cardID = -1;
		UARTDebugOut("Sound registered successfully \r\n");
		AuSoundAddDSP(dsp);
		break;
	}
	case SOUND_READ_AVAIL: {
		AuDSP* dsp_ = AuSoundGetDSP(thr->thread_id);
		_ioctl->uchar_2 = dsp_->available;
		break;
	}
	case SOUND_SET_VOLUME: {
		break;
	}
	case SOUND_UNREGISTER_SNDPLR: {
		AuSoundRemoveDSP(thr->thread_id);
		break;
	}
	case SOUND_GET_CARD_LIST: {
		if (_ioctl->ulong_1 == 0)
			return 1;
		aurora_snd_card_list* list = (aurora_snd_card_list*)_ioctl->ulong_1;
		int num_card_count = _ioctl->uint_1;

		for (int i = 0; i < num_card_count; i++) {
			strcpy(list->name, _cards[i]->name);
			list->cardID = i;
			list->next = (aurora_snd_card_list*)((size_t)list + sizeof(aurora_snd_card_list));
			list = list->next;
		}
		list->next = NULL;
		break;
	}

	case SOUND_GET_CARD_TOTALNUM: {
		int total_card_count = 0;
		for (int i = 0; i < AURORA_MAX_SOUND_CARDS; i++) {
			if (_cards[i])
				total_card_count++;
		}
		return total_card_count;
	}

	default:
		break;
	}
	return 0;
}

size_t AuSoundRead(AuVFSNode* fsys, AuVFSNode* file, uint64_t* buffer, uint32_t length) {
	/** NOT implemented yet **/
	return 0;
}

/* virtio-sound completes a TX buffer only after the host has played
 * that period. bredr0's write already waits the same 21 ms on the
 * sample clock. Doing both turns one chunk into two. */
static int snd_is_virtio(AuSound* card) {
	return card && card->name[0] == 'v' && card->name[1] == 'i' && card->name[2] == 'r';
}

static int snd_is_bredr(AuSound* card) {
	return card && card->name[0] == 'b' && card->name[1] == 'r' && card->name[2] == 'e' &&
		   card->name[3] == 'd' && card->name[4] == 'r';
}

size_t AuSoundWrite(AuVFSNode* fsys, AuVFSNode* file, uint64_t* buffer, uint32_t length) {
	AA64Thread* t = AuGetCurrentThread();
	AuDSP* dsp = AuSoundGetDSP(t->thread_id);
	if (!dsp)
		return 0;
	uint8_t* aligned_buf = (uint8_t*)buffer;
	/** see if dps has card attached to it, also
	 * verify if the card is in force write mode
	 */
	if (dsp->_cardID != -1) {
		AuSound* card = _cards[dsp->_cardID];
		int i;
		int bredr = 0;
		for (i = 0; i < AURORA_MAX_SOUND_CARDS; i++) {
			if (snd_is_bredr(_cards[i]) && _cards[i]->_force_write)
				bredr = 1;
		}
		/* The headset write already waits out the 21 ms period. virtio
		 * waits that same period again, so the first seconds run slow
		 * and then rush to catch the sample clock. btctl and the mixer
		 * both skip the speaker while bredr0 is up. */
		if (bredr) {
			for (i = 0; i < AURORA_MAX_SOUND_CARDS; i++) {
				AuSound* other = _cards[i];
				if (!other || !other->_force_write || !other->write)
					continue;
				if (snd_is_virtio(other))
					continue;
				other->write((uint8_t*)buffer, length);
			}
			return length;
		}
		if (card && card->_force_write && card->write)
			card->write((uint8_t*)buffer, length);
		for (i = 0; i < AURORA_MAX_SOUND_CARDS; i++) {
			AuSound* other;
			if (i == (int)dsp->_cardID)
				continue;
			other = _cards[i];
			if (other && other->_force_write && other->write)
				other->write((uint8_t*)buffer, length);
		}
		if (card && card->_force_write)
			return length;
	}

	if (CircBufFull(dsp->buffer)) {
		/*AuBlockThread(dsp->SndThread);
		AuForceScheduler();*/
		return -1;
	}
	for (int i = 0; i < SND_BUFF_SZ; i++) { //
		AuCircBufPut(dsp->buffer, aligned_buf[i]);
	}
	return SND_BUFF_SZ;
}
/**
* @brief AuSoundInitialize -- Initialized the Aurora sound system
*/
void AuSoundInitialise() {
	AuVFSNode* fsys = AuVFSFind("/dev");
	AuVFSNode* dsp = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(dsp, 0, sizeof(AuVFSNode));
	strcpy(dsp->filename, "sound");
	dsp->flags = FS_FLAG_DEVICE;
	dsp->uid = 0;
	dsp->gid = 0;
	dsp->device = fsys;
	dsp->read = AuSoundRead;
	dsp->write = AuSoundWrite;
	dsp->iocontrol = AuSoundIOControl;
	dsp->mode = 0660;
	AuDevFSAddFile(fsys, "/", dsp);

	/** clear up the card data structure */
	for (int i = 0; i < AURORA_MAX_SOUND_CARDS; i++)
		_cards[i] = 0;

	dsp_first = NULL;
	dsp_last = NULL;

	_audio_started_ = false;
	_audio_stopped_ = false;

	AuTextOut("[aurora]: sound initialized \r\n");
}

/**
 * @brief AuSoundRemoveDSP -- remove the dsp from
 * dsp list
 */
void AuSoundRemoveDSP(uint16_t id) {
	AuDSP* dsp_ = AuSoundGetDSP(id);
	if (dsp_) {
		AuPmmngrReleasePage((uint64_t)V2P((size_t)dsp_->buffer->buffer));
		AuCircBufFree(dsp_->buffer);
		AuRemoveDSP(dsp_);
		kfree(dsp_);
	}
}

/**
 * @brief AuSoundRegisterCard -- register a new sound card
 * to sound layer
 * @param snd -- pointer to sound card
 */
/**
 * @brief AuSoundUnregisterCard -- remove a card from the registry (hot-unplug).
 * DSP clients bound to the card are detached (_cardID = -1) rather than the
 * removal being refused: a client such as init holds its binding for the whole
 * uptime, so refusing would make a sound driver permanently un-unloadable.
 * Detached clients fall back to the software buffer path and must re-select a
 * card after the driver is loaded again. Every user of _cards[] already
 * tolerates an empty slot. Returns 0, or non-zero if snd was not registered.
 */
int AuSoundUnregisterCard(AuSound* snd) {
	for (int i = 0; i < AURORA_MAX_SOUND_CARDS; i++) {
		if (_cards[i] != snd)
			continue;
		_cards[i] = 0;
		int detached = 0;
		for (AuDSP* dsp = dsp_first; dsp; dsp = dsp->next) {
			if (dsp->_cardID == i) {
				dsp->_cardID = -1;
				detached++;
			}
		}
		UARTDebugOut("[sound]: card %d removed, %d client(s) detached\r\n", i, detached);
		return 0;
	}
	return 2; /* not registered */
}

int AuSoundRegisterCard(AuSound* snd) {
	int index = _AuSoundGetFreeID();
	if (index == -1) {
		UARTDebugOut("[aurora]: sound no free slot available for new card \r\n");
		return 1;
	}
	_cards[index] = snd;
	au_sound_last_id = index;
	/* clients that lost a card of this name come back to it */
	int rebound = 0;
	for (AuDSP* dsp = dsp_first; dsp; dsp = dsp->next) {
		if (dsp->_cardID == -1 && dsp->card_name[0] && strcmp(dsp->card_name, snd->name) == 0) {
			dsp->_cardID = index;
			rebound++;
		}
	}
	if (rebound)
		UARTDebugOut("[sound]: card %d (%s) registered, %d client(s) rebound\r\n",
					 index,
					 snd->name,
					 rebound);
	return 0;
}
