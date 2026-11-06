/**
* @file virtiokbd.c
* 
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

#include <pcie.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Hal/AA64/gic.h>
#include <Fs/Dev/devinput.h>
#include <Drivers/virtio.h>
#include <Drivers/uart.h>
#include <Mm/pmmngr.h>
#include <aucon.h>
#include <Mm/vmmngr.h>
#include <Hal/AA64/sched.h>
#include <string.h>
#include <aubuiltin.h>

struct VirtioQueue* queue;
struct VirtioInputEvent* input;
static uint16_t index;
static int queueSize;
static struct VirtioCommonCfg* _kybrdCfg;
/* what this driver bound, so it can be released and taken back (aubuiltin.h) */
static int _kbdSpi = -1;
static uint64_t _kbdDevice;
static int _kbdBus, _kbdDev, _kbdFunc;
void AuVirtioKbdInitialize(uint64_t device, int bus, int dev, int func);
extern void AuGICDeallocateSPI(int spiID);

/* Let go of the keyboard: reset the device (it stops DMA and events), stop routing
 * its interrupt here and give the number back. The queue pages stay allocated and
 * are not reused; a reinit makes new ones. */
static void AuVirtioKbdRelease(void) {
	if (!_kybrdCfg)
		return;
	_kybrdCfg->DeviceStatus = 0;
	isb_flush();
	dsb_ish();
	if (_kbdSpi >= 0) {
		GICClearSPIHandler(_kbdSpi);
		AuGICDeallocateSPI(_kbdSpi);
		_kbdSpi = -1;
	}
	_kybrdCfg = 0;
}

static void AuVirtioKbdReinit(void) {
	AuVirtioKbdInitialize(_kbdDevice, _kbdBus, _kbdDev, _kbdFunc);
}
/* temporary input-freeze diagnostics counter */
volatile uint32_t kbd_dbg_irqs;

static const uint8_t ext_key_map[256] = {
	[0x60] = 0x1c, //kpenter
	[0x62] = 0x35, //kpslash
	[0x63] = 0x37, //print screen
	[0x66] = 0x47, //home
	[0x67] = 0x48, //up
	[0x68] = 0x49, //page up
	[0x6c] = 0x50, //down
	[0x69] = 0x4B, //left
	[0x6a] = 0x4D, //right
	[0x6b] = 0x4F, //end
	[0x6d] = 0x51, //page down
	[0x6e] = 0x52, //insert
	[0x6f] = 0x53, //delete
	[0x7d] = 0x5b, //left super
};

/**
 * @brief Virtio-keyboard interrupt handler
 */
void AuVirtioKbdHandler(int spinum) {
	(void)spinum;
	kbd_dbg_irqs++;
	dc_ivac((uint64_t)&queue->used.index);
	dsb_sy_barrier();
	uint16_t them = queue->used.index;
	for (; index < them; index++) {
		uint16_t slot = index % queueSize;
		dc_ivac((uint64_t)&input[slot]);
		dsb_sy_barrier();
		struct VirtioInputEvent evt = input[slot];
		if (evt.type == 0xFF) {
			/* already consumed, spurious IRQ -- do not leak a buffer */
			continue;
		}
		input[slot].type = 0xFF;
		isb_flush();
		dsb_sy_barrier();
		if (evt.type == 1) {
			{
				/* a few lines, so a serial log shows which driver is handling input */
				static uint32_t dbg_keys;
				if (dbg_keys < 8) {
					dbg_keys++;
					UARTDebugOut("[virtio-keyboard]: kernel driver got key code %d value %d\r\n",
								 evt.code,
								 evt.value);
				}
			}
			/* Linux KEY_ENTER=28, KEY_KPENTER=96 */
			if (evt.code == 28 || evt.code == 96) {
				uint8_t scancode = 0x1c;
				if (evt.value == 0)
					scancode |= 0x80;
				AuInputMessage msg;
				memset(&msg, 0, sizeof(AuInputMessage));
				msg.type = AU_INPUT_KEYBOARD;
				msg.code = scancode;
				AuDevWriteKybrd(&msg);
			} else if (evt.code < 0x59) {
				uint8_t scancode = (uint8_t)evt.code;
				if (evt.value == 0)
					scancode |= 0x80;
				AuInputMessage msg;
				memset(&msg, 0, sizeof(AuInputMessage));
				msg.type = AU_INPUT_KEYBOARD;
				msg.code = scancode;
				AuDevWriteKybrd(&msg);
			} else if (evt.code < 256 && ext_key_map[evt.code]) {
				uint8_t make_code = ext_key_map[evt.code];
				if (evt.value == 0)
					make_code |= 0x80;
				AuInputMessage msg;
				memset(&msg, 0, sizeof(AuInputMessage));
				msg.type = AU_INPUT_KEYBOARD;
				msg.code = (0xE0u << 8) | make_code;
				AuDevWriteKybrd(&msg);
			}
		}
		/* recycle the buffer or the device runs out after queueSize events
		 * and the keyboard appears to "freeze after a few seconds" */
		queue->available.ring[queue->available.index % queueSize] = slot;
		dsb_sy_barrier();
		queue->available.index++;
		dsb_sy_barrier();
	}
}

/* temporary input-freeze diagnostics: thread-context only, never call from IRQ */
void AuVirtioKbdDebug() {
	/**dc_ivac((uint64_t)&queue->used.index);
	dsb_sy_barrier();
	UARTDebugOut("[kbd-dbg]: idx=%d used=%d avail=%d qsz=%d irqs=%d \n",
				 (int)index,
				 (int)queue->used.index,
				 (int)queue->available.index,
				 (int)queueSize,
				 (int)kbd_dbg_irqs);**/
}

void AuVirtioKbdDown() {
	if (!_kybrdCfg)
		return;

	/* Reset the device */
	_kybrdCfg->DeviceStatus = 0;

	isb_flush();
	dsb_ish();

	_kybrdCfg->DeviceStatus = 0x01;
	isb_flush();
	dsb_ish();

	_kybrdCfg->DeviceStatus |= 0x02;
	isb_flush();
	dsb_ish();

	UARTDebugOut("[virtio-keyboard]: reset completed successfully \r\n");
}
/**
 * @brief AuVirtioKbdInitialize -- initialize the virtio keyboard
 */
void AuVirtioKbdInitialize(uint64_t device, int bus, int dev, int func) {
#if 0
	index = 0;
	if (device == 0 || device == 0xFFFFFFFF)
		return;
	UARTDebugOut("[aurora]: Virtio Keyboard device found \n");
	uint16_t command = AuPCIERead(device, PCI_COMMAND, bus, dev, func);
	command |= 4;
	command |= 2;
	command |= 1;
	AuPCIEWrite(device, PCI_COMMAND, command, bus, dev, func);
	isb_flush();
	dsb_ish();

	uint64_t barLo = AuPCIERead(device, PCI_BAR4, bus, dev, func);
	uint64_t barHi = AuPCIERead(device, PCI_BAR5, bus, dev, func);
	uint64_t bar = ((uint64_t)barHi << 32) | (barLo & ~0xFULL);

	uint64_t finalAddr = (uint64_t)AuMapMMIO(bar, 16);
	struct VirtioDeviceConfig* cfg = (struct VirtioDeviceConfig*)(finalAddr + 0x2000);
	cfg->select = 1;
	cfg->subsel = 0;
	isb_flush();
	dsb_ish();
	char kbd_name[128];
	memset(kbd_name, 0, 128);
	int name_len = cfg->size;
	if (name_len > 127)
		name_len = 127;
	for (int i = 0; i < name_len; i++)
		kbd_name[i] = cfg->data.str[i];
	UARTDebugOut("VIRTIO Keyboard Name : %s \n", kbd_name);

	int spiID = AuGICAllocateSPI();
	_kbdSpi = spiID;
	_kbdDevice = device;
	_kbdBus = bus;
	_kbdDev = dev;
	_kbdFunc = func;
	UARTDebugOut("VIRTIO Keyboard SPI ID: %d \n", spiID);
	if (AuPCIEAllocMSI(device, spiID, bus, dev, func)) {
		UARTDebugOut("VIRTIO Keyboard MSI allocated \n");
	}

	GICEnableSPIIRQ(spiID);
	//GICSetTargetCPU(spiID);
	isb_flush();
	dsb_ish();

	GICRegisterSPIHandler(&AuVirtioKbdHandler, spiID);

	cfg->select = 0;
	cfg->subsel = 0;
	isb_flush();
	dsb_ish();

	struct VirtioCommonCfg* common = (struct VirtioCommonCfg*)finalAddr;
	_kybrdCfg = common;
	common->DeviceStatus = 0;

	isb_flush();
	dsb_ish();

	int queueSz = common->QueueSize;
	if (queueSz > 64)
		queueSz = 64;
	if (queueSz < 1)
		queueSz = 1;
	common->QueueSize = (uint16_t)queueSz;
	isb_flush();
	dsb_ish();
	queueSize = queueSz;
	UARTDebugOut("virtio: queue sz : %d \n", queueSz);

	uint64_t queuePhys = (uint64_t)AuPmmngrAllocPage(
		AURORA_PAGE_NORMAL); //AuPmmngrAllocBlocks(((sizeof(struct VirtioQueue) * queueSz))/0x1000);

	queue = (struct VirtioQueue*)AuMapMMIO(queuePhys,
										   1 /*((sizeof(struct VirtioQueue)*queueSz))/0x1000*/);
	/* zero through the kernel mapping: queuePhys is physical and is not
	 * reachable by identity when a user process's page tables are active */
	memset((void*)queue, 0, 0x1000);

	size_t desc_size = queueSz * sizeof(struct VirtioQueue);
	common->QueueSelect = 0;
	common->QueueDesc = queuePhys;
	common->QueueAvail = (queuePhys) + OFFSETOF(struct VirtioQueue, available);
	common->QueueUsed = (queuePhys) + OFFSETOF(struct VirtioQueue, used);
	common->MSix = 0;
	common->QueueMSixVector = 0;
	isb_flush();
	dsb_ish();

	uint64_t bufferBase = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	input = (struct VirtioInputEvent*)AuMapMMIO(bufferBase, 1);

	for (int i = 0; i < queueSz; ++i) {
		queue->buffers[i].Addr = bufferBase + i * sizeof(struct VirtioInputEvent);
		queue->buffers[i].Length = sizeof(struct VirtioInputEvent);
		queue->buffers[i].Flags = 2;
		queue->buffers[i].Next = 0;
		queue->available.ring[i] = i;
	}

	queue->available.index = 0;
	isb_flush();
	dsb_ish();
	common->QueueEnable = 1;
	isb_flush();
	dsb_ish();
	common->DeviceStatus = 4;
	isb_flush();
	dsb_ish();

	queue->available.index = (uint16_t)queueSz;
	isb_flush();
	dsb_ish();
	AuBuiltinRegister("virtio-kbd", bus, dev, func, AuVirtioKbdRelease, AuVirtioKbdReinit);
#endif
}
