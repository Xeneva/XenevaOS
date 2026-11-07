/**
* @file virtiotablet.c
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

struct VirtioQueue* TabletQueue;
uint16_t tabletIndex;
int tabletQueueSz;
int mouseX;
int mouseY;
int resX;
int resY;
int maxX;
int maxY;
int buttonLeft;
int buttonMiddle;
int buttonRight;
int buttonScrollUp;
int buttonScrollDown;
struct VirtioInputEvent* TabletInput;
static struct VirtioCommonCfg* _tabletCfg;
static int _tabletSpi = -1;
static uint64_t _tabletDevice;
static int _tabletBus, _tabletDev, _tabletFunc;
void AuVirtioTabletInitialize(uint64_t device, int bus, int dev, int func);
extern void AuGICDeallocateSPI(int spiID);

/* see AuVirtioKbdRelease: reset the device, drop its interrupt */
static void AuVirtioTabletRelease(void) {
	if (!_tabletCfg)
		return;
	_tabletCfg->DeviceStatus = 0;
	isb_flush();
	dsb_ish();
	if (_tabletSpi >= 0) {
		GICClearSPIHandler(_tabletSpi);
		AuGICDeallocateSPI(_tabletSpi);
		_tabletSpi = -1;
	}
	_tabletCfg = 0;
}

static void AuVirtioTabletReinit(void) {
	AuVirtioTabletInitialize(_tabletDevice, _tabletBus, _tabletDev, _tabletFunc);
}

#define MOUSE_LEFT_CLICK		0x01
#define MOUSE_RIGHT_CLICK		0x02
#define MOUSE_MIDDLE_CLICK		0x04
#define MOUSE_MOUSE_SCROLL_UP	0x10
#define MOUSE_MOUSE_SCROLL_DOWN 0x20

/**
 * @brief AuVirtioTabletHandler -- interrupt handler for 
 * virtio tablet
 * @param spiNum -- shared peripheral interrupt number
 * passed by system
 */
void AuVirtioTabletHandler(int spiNum) {
	dc_ivac((uint64_t)&TabletQueue->used.index);
	dsb_sy_barrier();
	uint16_t them = TabletQueue->used.index;

	/* The console is repointed to a different scanout size after this
	 * driver initializes (the GPU console hand-off), so the absolute
	 * pointer range must follow the live desktop size. Caching it at
	 * init left the cursor stuck at the boot mode's edges. --axiss */
	uint32_t liveW = AuConsoleGetScreenWidth();
	uint32_t liveH = AuConsoleGetScreenHeight();
	if (liveW && liveH) {
		resX = (int)liveW;
		resY = (int)liveH;
	}

	for (; tabletIndex < them; tabletIndex++) {
		uint16_t slot = tabletIndex % tabletQueueSz;
		dc_ivac((uint64_t)&TabletInput[slot]);
		dsb_sy_barrier();
		struct VirtioInputEvent evt = TabletInput[slot];
		/* never spin or UART in IRQ: a 0xFF slot is already consumed --axiss */
		if (evt.type == 0xFF) {
			/* recycle the buffer or the device runs out after
			 * tabletQueueSz events and the mouse dies */
			TabletQueue->available.ring[TabletQueue->available.index % tabletQueueSz] = slot;
			dsb_sy_barrier();
			TabletQueue->available.index++;
			dsb_sy_barrier();
			continue;
		}
		TabletInput[slot].type = 0xFF;
		isb_flush();
		dsb_sy_barrier();
		if (evt.type == 3) {
			if (maxX != 0 && evt.code == 0) {
				mouseX = (evt.value * resX) / maxX;
			} else if (maxY != 0 && evt.code == 1) {
				mouseY = (evt.value * resY) / maxY;
			}
		} else if (evt.type == 1) {
			if (evt.code == 0x110) {
				buttonLeft = evt.value;
			} else if (evt.code == 0x111) {
				buttonRight = evt.value;
			} else if (evt.code == 0x112) {
				buttonMiddle = evt.value;
			} else if (evt.code == 0x150) {
				buttonScrollDown = 1;
			} else if (evt.code == 0x151) {
				buttonScrollUp = 1;
			}
		} else if (evt.type == 2) {
			/* QEMU's default tablet sends the wheel as EV_REL
			 * REL_WHEEL (code 8): positive is up, negative is down.
			 * The gear-button codes above are the other QEMU mode. */
			if (evt.code == 8 && evt.value) {
				if (evt.value < 0x80000000u)
					buttonScrollUp = 1;
				else
					buttonScrollDown = 1;
			}
		} else if (evt.type == 0) {
			AuInputMessage newmsg;
			memset(&newmsg, 0, sizeof(AuInputMessage));
			newmsg.type = AU_INPUT_MOUSE;
			newmsg.xpos = mouseX;
			newmsg.ypos = mouseY;
			newmsg.button_state |=
				(buttonLeft ? MOUSE_LEFT_CLICK : 0) | (buttonRight ? MOUSE_RIGHT_CLICK : 0) |
				(buttonMiddle ? MOUSE_MIDDLE_CLICK : 0) |
				(buttonScrollDown ? MOUSE_SCROLL_DOWN : 0) | (buttonScrollUp ? MOUSE_SCROLL_UP : 0);

			{
				static uint32_t dbg_ptr;
				if (dbg_ptr < 8) {
					dbg_ptr++;
					UARTDebugOut("[virtio-tablet]: kernel driver pointer %d,%d buttons %x\r\n",
								 mouseX,
								 mouseY,
								 newmsg.button_state);
				}
			}
			AuDevWriteMice(&newmsg);
			buttonScrollDown = buttonScrollUp = 0;
		}
		/* recycle the buffer or the device runs out after
		 * tabletQueueSz events and the mouse dies */
		TabletQueue->available.ring[TabletQueue->available.index % tabletQueueSz] = slot;
		dsb_sy_barrier();
		TabletQueue->available.index++;
		dsb_sy_barrier();
	}
}

void AuVirtioTabletDown() {
	if (!_tabletCfg)
		return;

	/* Reset the device */
	_tabletCfg->DeviceStatus = 0;

	isb_flush();
	dsb_ish();

	_tabletCfg->DeviceStatus = 0x1;
	isb_flush();
	dsb_ish();

	_tabletCfg->DeviceStatus |= 0x2;
	isb_flush();
	dsb_ish();

	UARTDebugOut("[virtio-tablet]: reset completed successfully \r\n");
}
/**
 * @brief AuVirtioTabletInitialize -- initialize the virtio tablet
 */
void AuVirtioTabletInitialize(uint64_t device, int bus, int dev, int func) {
#if 0
	if (device == 0 || device == 0xFFFFFFFF)
		return;

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
	char tablet_name[128];
	memset(tablet_name, 0, 128);
	int name_len2 = cfg->size;
	if (name_len2 > 127)
		name_len2 = 127;
	for (int i = 0; i < name_len2; i++)
		tablet_name[i] = cfg->data.str[i];
	UARTDebugOut("VIRTIO Tablet Name : %s \n", tablet_name);

	cfg->select = 0x12;
	cfg->subsel = 0;
	isb_flush();
	dsb_ish();
	uint32_t max_x = cfg->data.tablet_data.max;
	cfg->select = 0x12;
	cfg->subsel = 1;
	isb_flush();
	dsb_ish();
	uint32_t max_y = cfg->data.tablet_data.max;

	maxX = max_x;
	maxY = max_y;

	cfg->select = 0;
	cfg->subsel = 0;
	isb_flush();
	dsb_ish();

	int spiID = AuGICAllocateSPI();
	_tabletSpi = spiID;
	_tabletDevice = device;
	_tabletBus = bus;
	_tabletDev = dev;
	_tabletFunc = func;
	UARTDebugOut("VIRTIO Tablet SPI ID: %d \n", spiID);
	if (AuPCIEAllocMSI(device, spiID, bus, dev, func)) {
		UARTDebugOut("VIRTIO Tablet MSI allocated \n");
	}
	GICEnableSPIIRQ(spiID);
	//GICSetTargetCPU(spiID);
	isb_flush();
	dsb_ish();

	GICRegisterSPIHandler(&AuVirtioTabletHandler, spiID);

	struct VirtioCommonCfg* common = (struct VirtioCommonCfg*)finalAddr;
	_tabletCfg = common;
	common->DeviceStatus = 0;
	isb_flush();

	tabletQueueSz = common->QueueSize;
	uint64_t queuePhys = (uint64_t)AuPmmngrAllocPage(
		AURORA_PAGE_NORMAL); //AuPmmngrAllocBlocks(((sizeof(struct VirtioQueue) * tabletQueueSz)) / 0x1000);
	TabletQueue = (struct VirtioQueue*)AuMapMMIO(
		queuePhys, 1 /*((sizeof(struct VirtioQueue) * tabletQueueSz)) / 0x1000*/);
	/* zero through the kernel mapping: queuePhys is physical and is not
	 * reachable by identity when a user process's page tables are active */
	memset((void*)TabletQueue, 0, 0x1000);
	UARTDebugOut("Queue Phys : %x \r\n", queuePhys);
	UARTDebugOut("Queue Size : %d \r\n", tabletQueueSz);

	size_t desc_size = tabletQueueSz * sizeof(struct VirtioQueue);
	common->QueueSelect = 0;
	common->QueueDesc = queuePhys;
	common->QueueAvail = (queuePhys) + OFFSETOF(struct VirtioQueue, available);
	common->QueueUsed = (queuePhys) + OFFSETOF(struct VirtioQueue, used);
	common->MSix = 0;
	common->QueueMSixVector = 0;
	isb_flush();
	dsb_ish();

	uint64_t bufferBase = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	TabletInput = (struct VirtioInputEvent*)AuMapMMIO(bufferBase, 1);

	for (int i = 0; i < tabletQueueSz; ++i) {
		TabletQueue->buffers[i].Addr = bufferBase + i * sizeof(struct VirtioInputEvent);
		TabletQueue->buffers[i].Length = sizeof(struct VirtioInputEvent);
		TabletQueue->buffers[i].Flags = 2;
		TabletQueue->buffers[i].Next = 0;
		TabletQueue->available.ring[i] = i;
	}

	TabletQueue->available.index = 0;
	isb_flush();
	dsb_ish();
	common->QueueEnable = 1;
	isb_flush();
	dsb_ish();
	common->DeviceStatus = 4;
	isb_flush();
	dsb_ish();

	tabletIndex = 0;
	TabletQueue->available.index = tabletQueueSz;
	isb_flush();
	dsb_ish();

	resX = AuConsoleGetScreenWidth();
	resY = AuConsoleGetScreenHeight();
	buttonLeft = buttonMiddle = buttonRight = buttonScrollDown = buttonScrollUp = 0;
	mouseX = mouseY = 0;
	AuBuiltinRegister("virtio-tablet", bus, dev, func, AuVirtioTabletRelease, AuVirtioTabletReinit);
#endif
}