/**
* @file sound.h
* 
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
*
**/

#ifndef __AU_SOUND_H__
#define __AU_SOUND_H__

#include <stdint.h>
#include <circbuf.h>

#ifdef ARCH_X64
#include <Hal/x86_64_sched.h>
#elif ARCH_ARM64
#include <Hal/AA64/sched.h>
#endif

#include <aurora.h>

#ifdef ARCH_X64
#pragma pack(push,1)
#endif
typedef struct __au_dsp__ {
	CircBuffer *buffer;
	uint16_t _dsp_id;
	int16_t _cardID;
#ifdef ARCH_X64
	AuThread *SndThread;
#elif ARCH_ARM64
	AA64Thread* SndThread;
#endif
	uint64_t sleep_time;
	bool available;
	struct __au_dsp__ *next;
	struct __au_dsp__ *prev;
	/* name of the card this client chose; kept when the card is removed so the
	 * client reattaches by itself when a card with that name comes back (driver
	 * restart or failover to the in-kernel driver) */
	char card_name[32];
}AuDSP;

#ifdef ARCH_X64
#pragma pack(pop)
#endif


typedef struct _SoundDev_ {
	char name[32];
	int(*write) (uint8_t* buffer, size_t length);
	int(*read) (uint8_t* buffer, size_t length);
	int(*stop_output)();
	int(*start_output)();
	int(*set_vol)(uint8_t vol);
	int(*control)(void* data, int code);
	bool _force_write;
}AuSound;


/**
* @brief AuSoundInitialize -- Initialized the Aurora sound system
*/
extern void AuSoundInitialise();

/**
* @brief AuSoundSetCard -- registers a new
* sound card
* @param dev -- sound card device
*/
AU_EXTERN AU_EXPORT void AuSoundSetCard(AuSound* dev);

/**
* @brief AuSoundGetBlock -- the main heart of aurora sound
* system -- called by sound card
*/
AU_EXTERN AU_EXPORT void AuSoundGetBlock(uint64_t *buffer);

/**
* @brief AuSoundRemoveDSP -- remove the dsp from
* dsp list
*/
AU_EXTERN AU_EXPORT void AuSoundRemoveDSP(uint16_t id);

/**
 * @brief AuSoundRegisterCard -- register a new sound card
 * to sound layer
 * @param snd -- pointer to sound card
 */
AU_EXTERN AU_EXPORT int AuSoundRegisterCard(AuSound* snd);

/**
 * @brief AuSoundUnregisterCard -- remove a sound card (hot-unplug); DSP clients
 * bound to it are detached. Non-zero only if the card was not registered.
 */
AU_EXTERN AU_EXPORT int AuSoundUnregisterCard(AuSound* snd);

/**
 * @brief au_sound_last_id -- card index assigned by last AuSoundRegisterCard call
 */
extern int au_sound_last_id;

/*
* AuSoundStart -- Starts the Sound card
*/
extern void AuSoundStart();

/*
* AuSoundStop -- Stop Sound card
*/
extern void AuSoundStop();
#endif