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

#include "codec.h"
#include "ihda.h"
#include "widget.h"
#include <stdio.h>
#include "sigmatel.h"

/**
* HDACodecQuery -- one time function for sending command and reading response
*/
uint32_t HDACodecQuery(int codec, int nid, uint32_t payload) {
	uint64_t response;// = (uint32_t)pmmngr_alloc();
	uint32_t icount = 10000;
	uint32_t verb = ((codec & 0xf) << 28) |
		((nid & 0xff) << 20) |
		(payload & 0xfffff);


	CORBWrite(verb);
	uint8_t rirb_status;
	do {
		rirb_status = _aud_inb_(RIRBSTS);
	} while ((rirb_status & 1) == 0);

	RIRBRead(&response);
	return response & 0xffffffff;
}


/*
* HDAGetPCMRates -- collects supported PCM Size / rates
* @param codec -- destination codec
* @param nid -- desired node id
*/
uint32_t HDAGetPCMRates(int codec, int nid) {
	uint32_t pcm_rates = 0;
	pcm_rates = HDACodecQuery(codec, nid, VERB_GET_PARAMETER | 0x0A);
#ifdef _HDA_DEBUG_
	if ((pcm_rates & (1 << 0)) != 0) {
		SeTextOut("Supported 8.0 kHZ, Mult/Div-> 1/6 * 48 \r\n");
	}
	if ((pcm_rates & (1 << 1)) != 0) {
		SeTextOut("Supported 11.025 kHZ, Mult/Div -> 1/4 * 44.1 \r\n");
	}
	if ((pcm_rates & (1 << 2)) != 0) {
		SeTextOut("Suported 16.0 kHZ, Mult/Div -> 1/3 * 48 \r\n");
	}
	if ((pcm_rates & (1 << 3)) != 0) {
		SeTextOut("Supported 22.05 kHZ, Mult/Div -> 1/2*44.1 \r\n");
	}
	if ((pcm_rates & (1 << 4)) != 0) {
		SeTextOut("Supported 32.0 kHZ, Mult/Div -> 2/3*48 \r\n");
	}
	if ((pcm_rates & (1 << 5)) != 0) {
		SeTextOut("Supported 44.1 kHZ, Mult/Div -> 0 \r\n");
	}
	if ((pcm_rates & (1 << 6)) != 0) {
		SeTextOut("Supported 48.0 kHZ, Mult/Div -> 0 \r\n");
	}
	if ((pcm_rates & (1 << 7)) != 0) {
		SeTextOut("Supported 88.2 kHZ, Mult/Div -> 2/1*44.1 \r\n");
	}
	if ((pcm_rates & (1 << 8)) != 0) {
		SeTextOut("Supported 96.0 kHZ, Mult/Div -> 2/1*48 \r\n");
	}
	if ((pcm_rates & (1 << 9)) != 0) {
		SeTextOut("Supported 176.4 kHZ, Mult/Div -> 4/1 * 44.1 \r\n");
	}
	if ((pcm_rates & (1 << 10)) != 0) {
		SeTextOut("Supported 192.0 kHZ, Mult/Div -> 4/1 * 48 \r\n");
	}
	else if ((pcm_rates & (1 << 11)) != 0) {
		SeTextOut("Supported 384 kHZ, Mult/Div -> 8/1 * 48 \r\n");
	}

	if ((pcm_rates & (1 << 16)) != 0) {
		SeTextOut("8 bit audio formats are supported \r\n");
	}
	if ((pcm_rates & (1 << 17)) != 0) {
		SeTextOut("16 bit audio formats are supported \r\n");
	}
	if ((pcm_rates & (1 << 18)) != 0) {
		SeTextOut("20 bit audio formats are supported \r\n");
	}
	if ((pcm_rates & (1 << 19)) != 0) {
		SeTextOut("24 bit audio formats are supported \r\n");
	}
	if ((pcm_rates & (1 << 20)) != 0) {
		SeTextOut("32 bit audio formats are supported \r\n");
	}
#endif
	return pcm_rates;
}

/*
* hda_get_supported_stream_format -- returns the supported
* stream format
* @param codec -- codec id
* @param nid -- node id
*/
uint32_t HDAGetSupportedStreamFormat(int codec, int nid) {
	uint32_t format = 0;
	format = HDACodecQuery(codec, nid, VERB_GET_PARAMETER | 0x0B);
#ifdef _HDA_DEBUG_
	if ((format & (1 << 0)) != 0) {
		SeTextOut("PCM is supported \r\n");
	}

	if ((format & (1 << 1)) != 0) {
		SeTextOut("Float32 audio is supported \r\n");
	}

	if ((format & (1 << 2)) != 0) {
		SeTextOut("Dolby AC-3 audio is supported \r\n");
	}
#endif
	return format;
}

/*
* hda_enable_unsol -- enables unsolicited response for
* nodes, unsolicited response are responses send by
* widgets to controller and finally reach the
* hda_interrupt_handler
* @param codec -- codec id
* @param nid -- node id
*/
void HDAEnableUnsol(int codec, int nid) {
	/* enable unsol for this codec */
	HDACodecQuery(codec, nid, 0x70800 | (1 << 7) | nid);
}

/*
* hda_get_unsol_node -- returns the node id of
* given unsolicited response
* @return node_id
*/
int HDAGetUnsolNode(uint32_t resp) {
	return (resp & 0xf);
}


/*
* HDACodecEnumerateWidgets -- enumerate every widgets
*/
int HDACodecEnumerateWidgets(int codec) {

	uint32_t param;
	int num_fg, num_widgets;
	int fg_start, widgets_start;
	int i, j;


	param = HDACodecQuery(codec, 0, VERB_GET_PARAMETER | PARAM_NODE_COUNT);

	num_fg = param & 0x000000ff;
	fg_start = (param >> 16) & 0xff;
	param = 0;

	SeTextOut("[driver]: hdaudio num function group -> %d, fg_start -> %d \r\n", num_fg, fg_start);

	uint32_t vendor_id = HDACodecQuery(codec, 0, VERB_GET_PARAMETER | PARAM_VENDOR_ID);
	uint32_t devid = vendor_id & 0xffff;
	uint32_t vendid = vendor_id >> 16;

	/*register function pointers */
	if (devid == 0x7680 && vendid == 0x8384) {
		/* Sigmatel Codec */
		HDASetCodecInitFunc(SigmatelInit);
		HDASetVolumeFunc(SigmatelSetVolume);
		SeTextOut("[hda]: sigmatel codec registered \r\n");
	}

	SeTextOut("[driver]: hdaudio widget device id -> 0x%x, vendor id -> 0x%x \r\n", devid, vendid);

	uint32_t rev_id = HDACodecQuery(codec, 0, VERB_GET_PARAMETER | PARAM_REV_ID);
	SeTextOut("[driver]: widget version -> %d.%d, r0%d \r\n", rev_id >> 20, rev_id >> 16, rev_id >> 8);


	for (i = 0; i < num_fg; i++) {

		param = HDACodecQuery(codec, fg_start + i,
			VERB_GET_PARAMETER | PARAM_NODE_COUNT);

		num_widgets = param & 0xff;
		widgets_start = (param >> 16) & 0xff;
		SeTextOut("Widget start -> %d, num widgets -> %d \r\n", widgets_start, num_widgets);

		param = HDACodecQuery(codec, fg_start + i, VERB_GET_PARAMETER | PARAM_FN_GROUP_TYPE);
		param &= 0x000000ff;
		if (param != FN_GROUP_AUDIO) {
			SeTextOut("FG not audio group \r\n");
			continue;
		}

		HDACodecQuery(codec, fg_start + i, 0x7FF00);
		for (int i = 0; i < 100000; i++)
			;
		HDACodecQuery(codec, fg_start + i, 0x7FF00);
		for (int i = 0; i < 100000; i++)
			;

		HDACodecQuery(codec, fg_start + i, VERB_SET_POWER_STATE | 0x00000);

		uint32_t amp_param = 0;
		amp_param = HDACodecQuery(codec, fg_start + i, VERB_GET_PARAMETER | 0x12000);
		SeTextOut("Step Size -----> %d NumStep -> %d \r\n", ((amp_param >> 31) & 0xff), ((amp_param >> 8) & 0xff));


		//HDAGetPCMRates (codec, fg_start + i);
		//HDAGetSupportedStreamFormat(codec, fg_start + i);

		for (int j = 0; j < num_widgets; j++) {
			HDAWidgetInit(codec, widgets_start + j);
		}
	}


	return 1; //_ihd_audio.output->nid ? 0 : -1;
}

/*
 * HDACodecInitOutputConv -- initialise output converter
 * @param codec -- codec id
 * @param nid -- node id
 * @param deinit -- if true, initialise or else stop the
 * converter
 */
void HDACodecInitOutputConv(int codec, int nid, bool deinit) {
	uint16_t format = 0;
	if (!deinit) {
		//codec_query(codec, nid, VERB_SET_EAPD_BTL |(1<<2) | (1<<1) | (1<<0));
		HDACodecQuery(codec, nid, VERB_SET_POWER_STATE | 0x00000);
		/* first output channel is 0x10 */
		HDACodecQuery(codec, nid, VERB_SET_STREAM_CHANNEL | 0x10);
		format = SR_48_KHZ | BITS_16 | 1;
		HDACodecQuery(codec, nid, VERB_SET_CONV_CHANNEL_COUNT | 0);
		HDACodecQuery(codec, nid, VERB_SET_AMP_GAIN_MUTE | 0xb000 | 127);
		HDACodecQuery(codec, nid, VERB_SET_FORMAT | format);
	}
	else if (deinit){
		HDACodecQuery(codec, nid, VERB_SET_POWER_STATE | 0x00000);
		HDACodecQuery(codec, nid, VERB_SET_STREAM_CHANNEL | 0);
		format = 0;
		HDACodecQuery(codec, nid, VERB_SET_FORMAT | 0);
	}
}