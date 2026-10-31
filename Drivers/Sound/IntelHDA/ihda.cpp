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

#include <aurora.h>
#include <pcie.h>
#include <stdio.h>
#include <Hal/x86_64_hal.h>
#include <Hal/serial.h>
#include "ihda.h"
#include "stream.h"
#include "widget.h"
#include "codec.h"
#include <Mm/vmmngr.h>
#include <Mm/pmmngr.h>
#include <Mm/kmalloc.h>
#include <aucon.h>
#include <Sound/sound.h>

HDAudio* _hdaudio;
uint16_t rirbrp = 0;
uint16_t corbwp = 0;
HDAAudioPath *path_first;
HDAAudioPath* path_last;

void(*CodecInitializeOutput)(int codec, int nid);
void(*CodecSetVolume) (uint8_t vol, int codec);


/*
 * TODO : Codec power management
 * /

/*
 * _aud_outl_ -- writes a value to mmio register in dword
 * @param reg -- register
 * @param value -- value to write
 */
void _aud_outl_(int reg, uint32_t value) {
	volatile uint32_t* mmio = (uint32_t*)(_hdaudio->mmio + reg);
	*mmio = value;
}

/*
 * _aud_inl_ -- reads a value from mmio register in dword
 * @param reg -- register
 */
uint32_t _aud_inl_(int reg) {
	volatile uint32_t* mmio = (uint32_t*)(_hdaudio->mmio + reg);
	return *mmio;
}

/*
 * _aud_outw_ -- writes a value to mmio register in word
 * @param reg -- register
 * @param value -- value to write
 */
void _aud_outw_(int reg, uint16_t value) {
	volatile uint16_t* mmio = (uint16_t*)(_hdaudio->mmio + reg);
	*mmio = value;
}

/*
 * _aud_inw_ -- reads a value from mmio register in word
 * @param reg -- register
 */
uint16_t _aud_inw_(int reg) {
	volatile uint16_t* mmio = (uint16_t*)(_hdaudio->mmio + reg);
	return *mmio;
}

/*
 * _aud_outb_ -- writes a value to mmio register in byte
 * @param reg -- register
 * @param value -- value to write
 */
void _aud_outb_(int reg, uint8_t value) {

	volatile uint8_t* mmio = (uint8_t*)(_hdaudio->mmio + reg);
	*mmio = value;
}

/*
* _aud_inb_ -- reads a value from mmio register in byte
* @param reg -- register
*/
uint8_t _aud_inb_(int reg) {
	volatile uint8_t* mmio = (uint8_t*)(_hdaudio->mmio + reg);
	return *mmio;
}

/*
* SetupCORB -- setup the corb engine
*/
void SetupCORB() {
	uint8_t reg;
	uint64_t corb_base;
	unsigned int corb_entries = 0;

	reg = _aud_inb_(CORBSIZE);
	/* Check CORB size capabilities and choose the largest size */
	if (reg & (1 << 6)) {
		_hdaudio->corb_entries = 256;
		reg |= 0x2;
	}
	else if (reg & (1 << 5)) {
		_hdaudio->corb_entries = 16;
		reg |= 0x1;
	}
	else if (reg & (1 << 4)) {
		_hdaudio->corb_entries = 2;
		reg |= 0x0;
	}
	else {
		printf("[driver]: hd-audio no supported corb size found \n");
	}

	_aud_outb_(CORBSIZE, reg);

	/* Set CORB Base Address */
	corb_base = (uint64_t)V2P((size_t)_hdaudio->corb);

	if ((corb_base % 128) != 0) {
		AuTextOut("CORB-Base is not aligned to 128 byte boundary \n");
		printf("CORB-Base is not aligned to 128 byte boundary \n");
		for (;;);
	}

	_aud_outl_(CORBLBASE, corb_base);
	_aud_outl_(CORBUBASE, corb_base >> 32);

	_aud_outw_(CORBWP, corbwp);
	_aud_outw_(CORBRP, 0x8000);
	_aud_outw_(CORBRP, 0x0);

	/* Start DMA engine */
	//_aud_outb_(CORBCTL, 0x1);
	uint8_t corbctl = _aud_inb_(CORBCTL);
	corbctl |= 0x2;
	_aud_outb_(CORBCTL, corbctl);
}


/*
* SetupRIRB -- Setup the RIRB engine
*/
void SetupRIRB() {
	uint8_t reg;
	uint64_t rirb_base;
	uint32_t rirb_entries = 0;

	reg = _aud_inb_(RIRBSIZE);
	/* Check RIRB size capabilities and choose the largest size */
	if (reg & (1 << 6)) {
		_hdaudio->rirb_entries = 256;
		reg |= 0x2;
	}
	else if (reg & (1 << 5)) {
		_hdaudio->rirb_entries = 16;
		reg |= 0x1;
	}
	else if (reg & (1 << 4)) {
		_hdaudio->rirb_entries = 2;
		reg |= 0x0;
	}
	else {
		printf("[driver]: hdaudio no supported rirb size found \n");
	}

	_aud_outb_(RIRBSIZE, reg);

	/* Set RIRB Base address */
	rirb_base = (uint64_t)V2P((size_t)_hdaudio->rirb);

	if ((rirb_base % 128) != 0)
		printf("RirbBase is not aligned to 128 byte \n");
	printf("rirb base -> %x \n", rirb_base);
	_aud_outl_(RIRBLBASE, rirb_base);
	_aud_outl_(RIRBUBASE, rirb_base >> 32);

	_aud_outw_(RIRBWP, 0x8000);
	_aud_outw_(RINTCNT, _hdaudio->rirb_entries / 2);
	_aud_outb_(RIRBCTL, 0x1);
	/* Start DMA Engine */
	uint8_t rirbctl = _aud_inb_(RIRBCTL);
	rirbctl |= 0x2;
	_aud_outb_(RIRBCTL, rirbctl);

}

/*
* CORBWrite -- write verb to corb
* @param verb -- verb to write
*/
void CORBWrite(uint32_t verb) {

	//! Check for immediate use
	if (_hdaudio->immediate_use) {
		_aud_outl_(ICOI, verb);
		_aud_outl_(ICIS, 1);
		return;
	}

	//! else use standard command transmitting method
	uint16_t wp = _aud_inw_(CORBWP) & 0xff;
	uint16_t rp = _aud_inw_(CORBRP) & 0xff;
	corbwp = 0;
	corbwp = (rp + 1);
	corbwp %= _hdaudio->corb_entries;

	/*Wait until there's a free entry in the CORB */
	do {
		rp = _aud_inw_(CORBRP) & 0xff;
	} while (rp == corbwp);

	/* Write to CORB */
	_hdaudio->corb[corbwp] = verb;
	_aud_outw_(CORBWP, corbwp);

}


/**
* RIRBRead - read a response from RIRB
* @param response - address to where the controller will write response
*/
void RIRBRead(uint64_t *response) {
	uint16_t wp = _aud_inb_(RIRBWP);
	uint16_t rp = rirbrp;
	uint16_t old_rp = rp;


	/*Wait for an unread entry in the RIRB */
	while (rirbrp == wp) {
		wp = _aud_inw_(RIRBWP);  //_aud_inb_(RIRBWP);
	}

	rp = (rp + 1) % _hdaudio->rirb_entries;
	rirbrp = rp;

	*response = _hdaudio->rirb[rp];
	_aud_outb_(RIRBSTS, 0x5);
	return;
}


/*
 * HDAReset -- reset the sound card
 * to its default state
 */
void HDAReset() {
	_aud_outl_(CORBCTL, 0);
	_aud_outl_(RIRBCTL, 0);


	while ((_aud_inl_(CORBCTL) & CORBCTL_CORBRUN) ||
		(_aud_inl_(RIRBCTL) & RIRBCTL_RIRBRUN));

	_aud_outl_(DPIBLBASE, 0);
	_aud_outl_(DPIBLBASE, 0);

	uint32_t ctl = _aud_inl_(GCTL);
	_aud_outl_(GCTL, ctl & ~GCTL_RESET);
	while ((_aud_inl_(GCTL) & GCTL_RESET) == 1);

	/* Delay */
	for (int i = 0; i < 10000000; i++)
		;


	_aud_outl_(GCTL, GCTL_RESET);
	while ((_aud_inl_(GCTL) & GCTL_RESET) == 0);

	for (int i = 0; i < 10000000; i++)
		;

	//_aud_outw_(WAKEEN, 0xffff);
	uint32_t intctl = _aud_inl_(INTCTL);
	intctl |= (1 << 31);
	intctl |= (1 << 30);
	intctl |= (1 << 4);
	_aud_outl_(INTCTL, 0x800000ff);

	SetupCORB();
	SetupRIRB();


}


#define HDAC_INTSTS_SIS_MASK		0x3fffffff


/*
 * HDAHandler -- high definition audio handler
 */
void HDAHandler(size_t v, void* p) {
	AuDisableInterrupt();
	uint32_t isr = _aud_inl_(INTSTS);
	uint8_t sts = _aud_inb_(REG_O0_STS);

	/* handler code goes here */
	if (isr & HDAC_INTSTS_SIS_MASK) {
		if (sts & 0x4) {
			//codec_query(0, 1,VERB_SET_POWER_STATE | 0x4);
			uint64_t *dma = (uint64_t*)_hdaudio->dma_pos_buff;
			uint32_t pos = dma[4] & 0xffffffff;
			pos /= BUFFER_SIZE;
			pos %= BDL_SIZE;
			AuSoundGetBlock((uint64_t*)(_hdaudio->sample_buffer + 
				static_cast<uint64_t>(pos) * BUFFER_SIZE));
		}

	}

	_aud_outb_(REG_O0_STS, sts);
	AuEnableInterrupt();
	AuInterruptEnd(_hdaudio->irq);

}

/*
 * HDAPathInitOutput -- initialise the output path
 */
void HDAPathInitOutput() {
	int codec, nid = 0;
	
	HDAAudioPath *p = path_first;
	for (HDAWidget *wid = p->root; wid != NULL; wid = wid->next) {
		const char* s;
		if (wid) {
			switch (wid->type){
			case HDA_WIDGET_TYPE_PIN:
				s = "pin";
				HDAWidgetSetupPin(wid->codec, wid->nid);
				HDACodecQuery(wid->codec, wid->nid, VERB_SET_AMP_GAIN_MUTE | 0xb000 | 127);
				break;
			case HDA_WIDGET_TYPE_MIXER:
				s = "mixer";
				HDACodecQuery(wid->codec, wid->nid, VERB_SET_POWER_STATE | 0x00000);
				HDACodecQuery(wid->codec, wid->nid, VERB_SET_AMP_GAIN_MUTE | 0xb000 | 127);
				break;
			case HDA_WIDGET_TYPE_DAC:
				s = "DAC";
				if (wid->nid == 0)
					continue;
				HDACodecInitOutputConv(wid->codec, wid->nid, false);
				break;
			case HDA_WIDGET_TYPE_LINE_OUT:
				s = "line out";
				break;
			}
		}
	}
}

/*
* AuDriverUnload -- Frees and clear up everthing from the 
* driver
*/
AU_EXTERN AU_EXPORT int AuDriverUnload() {
	return 0;
}


/*
* AuDriverMain -- Main entry for hda driver
*/
AU_EXTERN AU_EXPORT int AuDriverMain() {
	int bus, func, dev;

	uint64_t device = AuPCIEScanClass(0x04, 0x03, &bus, &dev, &func);
	if (device == 0xFFFFFFFF){
		SeTextOut("[driver]: intel hda not found \r\n");
		return 1;
	}


	uint32_t dev_vendid = AuPCIERead(device, PCI_VENDOR_ID, bus, dev, func);
	uint32_t dev_devid = AuPCIERead(device, PCI_DEVICE_ID, bus, dev, func);

	_hdaudio = (HDAudio*)kmalloc(sizeof(HDAudio));
	memset(_hdaudio, 0, sizeof(HDAudio));

	uint8_t tcsel;
	tcsel = AuPCIERead64(device, 0x44, 1, bus, dev, func);
	AuPCIEWrite64(device, 0x44, (tcsel & 0xf8), 1, bus, dev, func);


	uint64_t command = AuPCIERead(device, PCI_COMMAND, bus, dev, func);
	command |= (1 << 10);
	command |= 0x6;

	//clear the Interrupt disable
	AuPCIEWrite(device, PCI_COMMAND, command, bus, dev, func);


	uint64_t com2 = AuPCIERead(device, PCI_COMMAND, bus, dev, func);

	if ((com2 & (1 << 10)) != 1){
		SeTextOut("[ihda]: Interrupt enabled \r\n");
	}



	if (!AuPCIEAllocMSI(device, 80, bus, dev, func)) {
		SeTextOut("[ihda]: Failed to allocate MSI\MSI-X \r\n");
		kfree(_hdaudio);
		return 1;
	}

	setvect(80, HDAHandler);

	CodecInitializeOutput = NULL;
	CodecSetVolume = NULL;
	path_first = NULL;
	path_last = NULL;

	uintptr_t mmio = AuPCIERead(device, PCI_BAR0, bus, dev, func);
	_hdaudio->mmio = (uint64_t)AuMapMMIO(mmio, 1);
	_hdaudio->corb = (uint32_t*)P2V((size_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	_hdaudio->rirb = (uint64_t*)P2V((size_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	memset((void*)_hdaudio->corb, 0, PAGE_SIZE);
	memset((void*)_hdaudio->rirb, 0, PAGE_SIZE);

	_hdaudio->immediate_use = false;
	_hdaudio->output_ptr = 0;

	uint16_t gcap = _aud_inw_(GCAP);
	uint16_t num_oss = HDA_GCAP_OSS(gcap);
	uint16_t num_iss = HDA_GCAP_ISS(gcap);

	_hdaudio->num_iss = num_iss;
	_hdaudio->num_oss = num_oss;

	SeTextOut("Reseting HDA \r\n");
	HDAReset();

	SeTextOut("Initializing Output \r\n");
	if (HDAInitOutput()) {
		SeTextOut("[HDAudio]: Failed to initialize output \r\n");
		return 1;
	}
	SeTextOut("Initializing Input \r\n");
	if (HDAInitInputStream())
		SeTextOut("[HDAudio]: Failed to initialize input \r\n");
	

	uint16_t statests = _aud_inw_(STATESTS);
	for (int i = 0; i < 15; i++) {
		if (statests & (1 >> i)){
			HDACodecEnumerateWidgets(i);
		}
	}

	HDAPathInitOutput();
	HDASetVolume(100);

	AuSound *sound = (AuSound*)kmalloc(sizeof(AuSound));
	memset(sound, 0, sizeof(AuSound));
	strcpy(sound->name, "intelhda");
	sound->read = 0;
	sound->set_vol = HDASetVolume;
	sound->start_output = HDAudioStartOutput;
	sound->stop_output = HDAudioStopOutput;
	sound->write = 0;
	AuSoundSetCard(sound);

	HDAudioStartOutput();
	SeTextOut("[driver]: intel hda audio initialized vendor: %x device: %x \r\n",dev_vendid, dev_devid);
}


/*
* hda_set_codec_init_func -- sets function pointer to each codecs
* initialisation code
*/
void HDASetCodecInitFunc(void(*init_func)(int codec, int nid)) {
	if (CodecInitializeOutput)
		return;
	CodecInitializeOutput = init_func;
}

/*
* HDASetVolumeFunc -- sets function pointer to each codecs volume
* function
*/
void HDASetVolumeFunc(void(*set_vol) (uint8_t volume, int codec)) {
	if (CodecSetVolume)
		return;
	CodecSetVolume = set_vol;
}

void HDAudioSetDMAPos(uint64_t dma_buff) {
	_hdaudio->dma_pos_buff = dma_buff;
}

uint64_t HDAudioGetDMAPos() {
	return _hdaudio->dma_pos_buff;
}

void HDAudioSetSampleBuffer(uint64_t buffer) {
	_hdaudio->sample_buffer = buffer;
}

/*
* HDAAddPath -- adds widget to widget list
* @param path -- pointer to path object
*/
void HDAAddPath(HDAAudioPath *path) {
	path->next = NULL;
	path->prev = NULL;
	if (path_first == NULL){
		path_first = path;
		path_last = path;
	}
	else {
		path_last->next = path;
		path->prev = path_last;
		path_last = path;
	}
}

/*
* HDASetVolume -- sets volume to output codec
* @param volume -- volume level
*/
void HDASetVolume(uint8_t volume) {
	if (CodecSetVolume) {
		CodecSetVolume(volume, 0);
		return;
	}

	if (volume == 0)
		volume = (1 << 7);

	HDAAudioPath *p = path_first;
	for (HDAWidget *wid = p->root; wid != NULL; wid = wid->next) {
		const char* s;
		switch (wid->type){
		case HDA_WIDGET_TYPE_PIN:
			s = "pin";
			HDAWidgetSetupPin(wid->codec, wid->nid);
			HDACodecQuery(wid->codec, wid->nid, VERB_SET_AMP_GAIN_MUTE | 0xb000 | volume);
			break;
		case HDA_WIDGET_TYPE_MIXER:
			s = "mixer";
			HDACodecQuery(wid->codec, wid->nid, VERB_SET_POWER_STATE | 0x00000);
			HDACodecQuery(wid->codec, wid->nid, VERB_SET_AMP_GAIN_MUTE | 0xb000 | volume);
			break;
		case HDA_WIDGET_TYPE_DAC:
			s = "DAC";
			HDACodecQuery(wid->codec, wid->nid, VERB_SET_AMP_GAIN_MUTE | 0xb000 | volume);
			break;
		case HDA_WIDGET_TYPE_LINE_OUT:
			s = "line out";
			break;
		}
	}
}