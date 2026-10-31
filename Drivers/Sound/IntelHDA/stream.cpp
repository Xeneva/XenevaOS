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

#include "stream.h"

uint64_t *strm_buf = 0;

/*
 * HDAInitOutput -- initialise the output stream
 */
int HDAInitOutput() {
	uint64_t pos = 0xFFFFD00004000000;
	uint64_t phys_buf = 0;
	strm_buf = (uint64_t*)pos;
	for (int i = 0; i < (BDL_SIZE*BUFFER_SIZE / PAGE_SIZE); i++) {
		void* p = (void*)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
		if (phys_buf == 0)
			phys_buf = (uint64_t)p;
		AuMapPage((size_t)p, pos + static_cast<uint64_t>(i) * 4096, X86_64_PAGING_NO_CACHING | X86_64_PAGING_NO_EXECUTE |
			X86_64_PAGING_WRITE_THROUGH);
	}


	/* set the sample buffer to hdaudio struc*/
	HDAudioSetSampleBuffer(pos);

	_aud_outb_(REG_O0_CTLL, 0 << 1);


	/* Now reset the stream */
	int timeout = 0;
	_aud_outb_(REG_O0_CTLL, 1); //reset
	while ((_aud_inl_(REG_O0_CTLL) & 0x1) == 0)
		;

	_aud_outb_(REG_O0_CTLL, 0);
	while ((_aud_inl_(REG_O0_CTLL) & 0x1) != 0)
		;

	_aud_outb_(REG_O0_STS, HDAC_SDSTS_DESE | HDAC_SDSTS_FIFOE | HDAC_SDSTS_BCIS);

	uint64_t bdl_base = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);   //get_physical_address  ((uint64_t) 0x0000000000000000);
	HDABDLEntry *bdl = (HDABDLEntry*)bdl_base;  //(_ihd_audio.corb + 3072);

	int j = 0;
	for (j = 0; j < BDL_SIZE; j++) {
		bdl[j].paddr = (uint64_t)(phys_buf + static_cast<uint64_t>(j) * BUFFER_SIZE);
		bdl[j].length = BUFFER_SIZE;
		bdl[j].flags = 0;
	}
	bdl[j - 1].flags = 1;

	SeTextOut("Reset completed \r\n");
	_aud_outw_(REG_O0_CTLU, (1 << 4) | (1 << 19));
	_aud_outl_(REG_O0_CTLL, (1 << 16));

	_aud_outl_(REG_O0_CBL, BDL_SIZE*BUFFER_SIZE);
	_aud_outw_(REG_O0_STLVI, BDL_SIZE - 1);

	_aud_outl_(REG_O0_BDLPL, bdl_base);
	_aud_outl_(REG_O0_BDLPU, bdl_base >> 32);


	uint16_t format = SR_48_KHZ | BITS_16 | 1;
	_aud_outw_(REG_O0_FMT, format);


	uint64_t* dma_pos = (uint64_t*)P2V((size_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	memset(dma_pos, 0, 4096);

	//_ihd_audio.dma_pos = dma_pos;
	uint64_t dma_val = (uint64_t)V2P((size_t)dma_pos);
	AuMapPage(dma_val, 0xFFFFD00005000000, X86_64_PAGING_NO_CACHING | X86_64_PAGING_NO_EXECUTE);
	dma_pos = (uint64_t*)0xFFFFD00005000000;

	_aud_outl_(DPIBLBASE, dma_val);
	_aud_outl_(DPIBUBASE, dma_val >> 32);

	HDAudioSetDMAPos((uint64_t)dma_pos);

	/*uint32_t strm = _aud_inl_(REG_O0_CTLL);
	strm |= (4<<20);
	_aud_outl_ (REG_O0_CTLL,strm);*/
	uint16_t sample_fifo = _aud_inw_(REG_O0_FIFOD);
	return 0;
}

/*
 * HDAInitInputStream -- start an input stream
 */
int HDAInitInputStream() {

	uint64_t pos = 0xFFFFD00008000000;
	uint64_t phys_buf = 0;
	
	for (int i = 0; i < (4 * BUFFER_SIZE / 4096); i++) {
		void* p = (void*)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
		if (phys_buf == 0)
			phys_buf = (uint64_t)p;
		AuMapPage((uint64_t)p, pos + static_cast<uint64_t>(i) * 4096, (1 << 4));
	}


	//hda_set_sample_buffer(pos);
	_aud_outl_(REG_I0_CTLL, 0 << 1);
	/* Now reset the stream */
	int timeout = 0;
	_aud_outl_(REG_I0_CTLL, 1); //reset
	while ((_aud_inl_(REG_I0_CTLL) & 0x1) == 0)
		;


	_aud_outl_(REG_I0_CTLL, 0);
	while ((_aud_inl_(REG_I0_CTLL) & 0x1) != 0)
		;

	_aud_outb_(REG_I0_STS, HDAC_SDSTS_DESE | HDAC_SDSTS_FIFOE | HDAC_SDSTS_BCIS);

	uint64_t bdl_base = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);   //get_physical_address  ((uint64_t) 0x0000000000000000);
	HDABDLEntry *bdl = (HDABDLEntry*)bdl_base;  //(_ihd_audio.corb + 3072);

	int j = 0;
	for (j = 0; j < 4; j++) {
		bdl[j].paddr = (uint64_t)(phys_buf + static_cast<uint64_t>(j) * BUFFER_SIZE);
		bdl[j].length = BUFFER_SIZE;
		bdl[j].flags = 1;
	}

	//	_aud_outb_ (REG_O0_CTLL, (1<<20));

	_aud_outl_(REG_I0_CBL, 4 * BUFFER_SIZE);
	_aud_outw_(REG_I0_LVI, 4 - 1);

	_aud_outl_(REG_I0_BDPL, bdl_base);
	_aud_outl_(REG_I0_BDPU, bdl_base >> 32);

	//uint16_t format =  (1<<14) | (0<<11)  | (1<<4) | 1;
	uint16_t format = SR_48_KHZ | BITS_24 | 1;
	_aud_outw_(REG_I0_FMT, format);



	uint64_t* dma_pos = (uint64_t*)P2V((size_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	memset(dma_pos, 0, 4096);

	//_ihd_audio.dma_pos = dma_pos;
	uint64_t dma_val = (uint64_t)V2P((size_t)dma_pos);


	_aud_outl_(DPIBLBASE, dma_val | 0x1);
	_aud_outl_(DPIBUBASE, dma_val >> 32);

	//hda_audio_set_dma_pos((uint64_t)dma_pos);

	uint32_t strm = _aud_inl_(REG_I0_CTLL);
	strm |= (2 << 20);
	_aud_outl_(REG_I0_CTLL, strm);
	uint16_t sample_fifo = _aud_inw_(REG_I0_FIFOD);
	return 0;
}

/*
 * HDAudioStartOutput -- starts the output
 * stream
 */
void HDAudioStartOutput() {
	uint8_t sts = _aud_inb_(REG_O0_STS);
	sts |= (1 << 2);
	sts |= (1 << 3);
	sts |= (1 << 4);
	_aud_outb_(REG_O0_STS, sts);

	uint8_t value = _aud_inb_(REG_O0_CTLL);
	value |= (1 << 4);
	value |= (1 << 3);
	value |= HDAC_SDCTL_IOCE; 
	value |= (1 << 1);
	_aud_outb_(REG_O0_CTLL, value);
}

/*
* HDAudioStopOutput -- stops the output stream
*/
void HDAudioStopOutput() {
	uint32_t value = _aud_inl_(REG_O0_CTLL);
	value &= ~(1 << 1);
	_aud_outw_(REG_O0_CTLL, value);
}
