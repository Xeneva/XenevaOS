/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2023, Manas Kamal Choudhury
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

#ifndef __CODEC_H__
#define __CODEC_H__

#include <stdint.h>

/* vendors of codecs*/
#define CODEC_VENDOR_VMWARE    0x15AD   /*vmware: emulates RealTek ALC885 codec*/
#define CODEC_VENDOR_SIGMATEL  0x8384   /*sigmatel */

/* vendors of devices */
#define CODEC_DEVICE_VMWARE    0x1975   /*vmware: emulates RealTek ALC885 codec */
#define CODEC_DEVICE_SIGMATEL  0x7680


/**
* one time function for sending command and reading response
*/
extern uint32_t HDACodecQuery(int codec, int nid, uint32_t payload);
/*
* HDACodecEnumerateWidgets -- enumerate every widgets
*/
extern int HDACodecEnumerateWidgets(int codec);

/*
* hda_get_supported_stream_format -- returns the supported
* stream format
* @param codec -- codec id
* @param nid -- node id
*/
extern uint32_t HDAGetSupportedStreamFormat(int codec, int nid);

/*
* HDAGetPCMRates -- collects supported PCM Size / rates
* @param codec -- destination codec
* @param nid -- desired node id
*/
extern uint32_t HDAGetPCMRates(int codec, int nid);

/*
* HDACodecInitOutputConv -- initialise output converter
* @param codec -- codec id
* @param nid -- node id
* @param deinit -- if true, initialise or else stop the
* converter
*/
void HDACodecInitOutputConv(int codec, int nid, bool deinit);

#endif