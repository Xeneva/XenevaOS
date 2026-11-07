/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2024, Manas Kamal Choudhury
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
#include <aucon.h>
#include <audrv.h>
#include "xhci.h"
#include <Mm/vmmngr.h>
#include <Mm/kmalloc.h>
#include <_null.h>
#include <Mm/pmmngr.h>
#include <string.h>
#include <Hal/serial.h>
#include "port.h"
#include <Drivers/usb.h>
#include <aucon.h>
#include <Hal/apic.h>
#include <Drivers/ps2kybrd.h>
#include <Hal/x86_64_cpu.h>

XHCIDevice* xhcidev;

/* usb thread msg codes */
#define USB_THREAD_MSG_PORT_CHANGE 10
uint64_t usbThreadCmd;
uint32_t portNum;
list_t* deviceHotPlugList;

/* Main USB thread, which is responsible for
 * handling device connects/disconnects events
 * starting the specific changed port,
 * in future: it will be moved to new file
 */
void AnuvabUSB3Thread(uint64_t value) {
	/* Initialize all ports */
	while (1) {
		if (usbThreadCmd == USB_THREAD_MSG_PORT_CHANGE) {
			XHCIPortInitialise(xhcidev, portNum);
			usbThreadCmd = NULL;
		}
		AuBlockThread(AuGetCurrentThread());
		AuForceScheduler();
	}
}

/*
 * ==================================================
 * NOTE: XHCIDevice struct and XHCISlot struct shares
 * some fields, but the difference is its uses.
 * XHCIDevice is used for entire XHCI Controller 
 * whereas XHCISlot is used only for each USB Device
 */

/*
* AuDriverUnload -- deattach the driver from
* aurora system
*/
AU_EXTERN AU_EXPORT int AuDriverUnload() {

	return 0;
}


/*
 * XHCIReset -- reset the xhci controller
 * @param dev -- pointer to USB device
 * structure
 */
void XHCIReset(XHCIDevice* dev) {
	dev->op_regs->op_usbcmd &= ~(1 << 0);
	while (!(dev->op_regs->op_usbsts & (1 << 0)));
	dev->op_regs->op_usbcmd |= (1 << 1);

	if (dev->vendor == XHCI_VENDOR_INTEL) {
		AuEnableInterrupt();
		x86_64_udelay(1000*500);
		AuDisableInterrupt();
	}
	while ((dev->op_regs->op_usbcmd & (1 << 1)));
	while ((dev->op_regs->op_usbsts & (1 << 11))); //The CNR flag check in USBSTS
}

/*
 * XHCIDeviceContextInit -- initialise the
 * device context base array pointer (DCBAAP)
 * @param dev -- Pointer to XHCI device
 * structure
 */
void XHCIDeviceContextInit(XHCIDevice* dev) {
	uint64_t phys = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	memset((void*)phys, 0, PAGE_SIZE);
	uint64_t* dcbaa = (uint64_t*)AuMapMMIO(phys, 1);
	dev->dev_ctx_base_array = dcbaa;
	dev->op_regs->op_dcbaap_lo = phys;
	dev->op_regs->op_dcbaap_hi = (phys >> 32);
	uint32_t scratch_hi = dev->cap_regs->cap_hccparams2 >> 21;
	uint32_t scratch_lo = dev->cap_regs->cap_hccparams2 >> 27;
	uint32_t max_scratchpad = (scratch_hi << 5) | scratch_lo;

	if (max_scratchpad) {
		uint64_t* spad = (uint64_t*)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
		memset(spad, 0, PAGE_SIZE);

		for (unsigned i = 0; i < max_scratchpad; i++) {
			spad[i] = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
			memset(&spad[i], 0, PAGE_SIZE);
		}
	}
}


/*
 * XHCICommandRingInit -- initialise the command ring
 * buffer
 * @param dev -- Pointer to usb device
 */
void XHCICommandRingInit(XHCIDevice* dev) {
	uint64_t cmd_ring_phys = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	memset((void*)cmd_ring_phys, 0, PAGE_SIZE);
	dev->cmd_ring = (xhci_trb_t*)AuMapMMIO(cmd_ring_phys, 1);

	uint64_t crcr_val = (cmd_ring_phys & ~0x3F) | 1;

	dev->op_regs->op_crcr = crcr_val;
	dev->op_regs->op_dnctrl = 4;
	dev->cmd_ring_index = 0;
	dev->cmd_ring_max = 128;
	dev->cmd_ring_cycle = 1;
	dev->cmd_ring_phys = cmd_ring_phys;

	/* insert a link trb at the last of cmd ring */
	dev->cmd_ring[dev->cmd_ring_max].trb_param_1 = cmd_ring_phys & UINT32_MAX;
	dev->cmd_ring[dev->cmd_ring_max].trb_param_2 = (cmd_ring_phys >> 32) & UINT32_MAX;
	dev->cmd_ring[dev->cmd_ring_max].trb_status = 0;
	dev->cmd_ring[dev->cmd_ring_max].trb_control = TRB_TRANSFER_LINK << 10 | (1 << 5);
}

/*
* xhci_event_ring_init -- initialize the
* default event ring
* @param dev -- Pointer to usb device
*/
void XHCIEventRingInit(XHCIDevice* dev) {
	uint64_t e_ring_seg_table = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	uint64_t* event_ring_seg_table_v = (uint64_t*)AuMapMMIO(e_ring_seg_table, 1);
	memset(event_ring_seg_table_v, 0, PAGE_SIZE);

	uint64_t event_ring_seg = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	uint64_t* event_ring_seg_v = (uint64_t*)AuMapMMIO(event_ring_seg, 1);
	memset(event_ring_seg_v, 0, PAGE_SIZE);
	dev->event_ring_segment = event_ring_seg_v;
	dev->event_ring_seg_phys = (uint64_t*)event_ring_seg;

	xhci_erst_t* erst = (xhci_erst_t*)e_ring_seg_table;
	erst->event_ring_segment_lo = event_ring_seg & UINT32_MAX;
	erst->event_ring_seg_hi = (event_ring_seg >> 32) & UINT32_MAX;
	erst->ring_seg_size = 4096 / 16;


	dev->evnt_ring_cycle = 1;
	dev->evnt_ring_index = 0;
	dev->evnt_ring_max = erst->ring_seg_size;

	dev->rt_regs->intr_reg[0].evtRngSegTabSz = 1;
	dev->rt_regs->intr_reg[0].evtRngDeqPtrLo = (event_ring_seg << 4) | (1 << 3);
	dev->rt_regs->intr_reg[0].evtRngDeqPtrHi = (event_ring_seg >> 32);
	dev->rt_regs->intr_reg[0].evtRngSegBaseLo = e_ring_seg_table;
	dev->rt_regs->intr_reg[0].evtRngSegBaseHi = (e_ring_seg_table >> 32);
	dev->rt_regs->intr_reg[0].intr_mod = (0 << 16) | 500;
	dev->op_regs->op_usbcmd = (1 << 3) | (1 << 2) | 0;
	dev->rt_regs->intr_reg[0].intr_man |= (1 << 1) | 1;
	dev->op_regs->op_usbcmd |= 1;
	while ((dev->op_regs->op_usbsts & (1 << 0)) != 0)
		;

}

extern bool hotPlug;
/*
 * XHCIHandleEventInterrupt -- handle event interrupts
 */
void XHCIHandleEventInterrupt() {
	xhci_trb_t* event = (xhci_trb_t*)xhcidev->event_ring_segment;
	xhci_event_trb_t* evt = (xhci_event_trb_t*)xhcidev->event_ring_segment;
	uint64_t erdp = (uint64_t)xhcidev->event_ring_seg_phys;

	while ((event[xhcidev->evnt_ring_index].trb_control & (1 << 0)) == xhcidev->evnt_ring_cycle) {
		//SeTextOut("Event ring interrupt %d\r\n", evt[xhcidev->evnt_ring_index].trbType);

		/*  TRANSFER EVENT INTERRUPT */
		if (evt[xhcidev->evnt_ring_index].trbType == TRB_EVENT_TRANSFER) {
			uint8_t endpoint_id = (event[xhcidev->evnt_ring_index].trb_control >> 16) & 0x1f;
			uint8_t comp_code = (event[xhcidev->evnt_ring_index].trb_status >> 24) & 0xff;
			uint8_t slot_id = (event[xhcidev->evnt_ring_index].trb_control >> 24) & 0xff;
			uint64_t data = (event[xhcidev->evnt_ring_index].trb_param_1 | event[xhcidev->evnt_ring_index].trb_param_2);
			xhcidev->event_available = true;
			xhcidev->poll_return_trb_type = TRB_EVENT_TRANSFER;
			xhcidev->trb_event_index = xhcidev->evnt_ring_index;
			xhci_trb_t* trb = (xhci_trb_t*)&evt[xhcidev->evnt_ring_index];

			/* check if this event belongs to endpoint */
			if (endpoint_id >= 2) {
				XHCISlot* slot = XHCIGetSlotByID(xhcidev, slot_id);
				XHCIEndpoint* ep = XHCISlotGetEP_DCI(slot, endpoint_id);

				/* if belongs, call the handler of that endpoint */
				if (ep->callback)
					ep->callback(xhcidev, slot, ep);
			}
		}

		/*
		 * PORT Connection/disconnection handling
		 */
		if (evt[xhcidev->evnt_ring_index].trbType == TRB_EVENT_PORT_STATUS_CHANGE) {
			if (xhcidev->initialised) {

				uint32_t port = event[xhcidev->evnt_ring_index].trb_param_1 >> 24;
				//hotPlug = true;
				usbThreadCmd = USB_THREAD_MSG_PORT_CHANGE;
				portNum = port;

				xhci_port_regs_t* this_port = &xhcidev->ports[port - 1];

				bool csc = this_port->port_sc & (1 << 17);
				bool pec = this_port->port_sc & (1 << 18); //Port enable change
				bool wrc = this_port->port_sc & (1 << 19); //Warm reset change
				bool occ = this_port->port_sc & (1 << 20); //over current change
				bool prc = this_port->port_sc & (1 << 21); //port reset change
				bool plc = this_port->port_sc & (1 << 22); //port link change

				if (pec)
					AuTextOut("[xhci]: port changed %d due to PORT ENABLE Change\n", port);
				else if (wrc)
					AuTextOut("[xhci]: port changed %d due to WARM RESET \n", port);
				else if (occ)
					AuTextOut("[xhci]: port changed %d due to OVER-CURRENT CHANGE\n", port);
				else if (prc)
					AuTextOut("[xhci]: port changed %d due to PORT RESET CHANGE\n", port);
				else if(plc)
					AuTextOut("[xhci]: port changed %d due to PORT LINK CHANGE\n", port);

				if (csc) {
					USBHotPlug* hp = (USBHotPlug*)kmalloc(sizeof(USBHotPlug));
					hp->portNum = port;
					hp->initialized = false;
					list_add(deviceHotPlugList, hp);
					/*
					 * Practical approach ! Here we need to queue up hotplugged
					 * devices then let the usb thread initialize it in background
					 * so that kernel initialization doesn't get interrupted
					 */
					if (AuIsSchedulerInitialised()) {
						if (xhcidev->usbThread->state == THREAD_STATE_BLOCKED) {
							//AuTextOut("USB Thread was blocked, unblocking \n");
							//if (AuIsSchedulerInitialised())
							AuUnblockThread(xhcidev->usbThread);
						}
					}

				}
			}
		}


		/*
		 * COMMAND COMPLETION EVENT
		 */
		if (evt[xhcidev->evnt_ring_index].trbType == TRB_EVENT_CMD_COMPLETION) {
			xhcidev->event_available = true;
			xhcidev->poll_return_trb_type = TRB_EVENT_CMD_COMPLETION;
			xhcidev->trb_event_index = xhcidev->evnt_ring_index;
			xhci_trb_t* trb = (xhci_trb_t*)&evt[xhcidev->evnt_ring_index];
		}

		xhcidev->evnt_ring_index++;

		if (xhcidev->evnt_ring_index == xhcidev->evnt_ring_max) {
			xhcidev->evnt_ring_cycle ^= 1;
			xhcidev->evnt_ring_index = 0;
		}
	}

	/* Update the Dequeue Pointer of interrupt 0 to recently
	* processed event_ring_segment entry (known as TRB Entry) */
	xhcidev->rt_regs->intr_reg[0].evtRngDeqPtrLo = (erdp + sizeof(xhci_trb_t) * xhcidev->evnt_ring_index) << 4 | (1 << 3);
	xhcidev->rt_regs->intr_reg[0].evtRngDeqPtrHi = (erdp + sizeof(xhci_trb_t) * xhcidev->evnt_ring_index) >> 32;

	/* Update the interrupt pending bit with value 1, so
	* that new interrupt gets asserted with new events */
	xhcidev->rt_regs->intr_reg[0].intr_man |= (1 << 1) | 1;

	/* Clear the Event Handler Busy bit again */
	xhcidev->rt_regs->intr_reg[0].evtRngDeqPtrLo |= 1 << 3;

}

/*
 * XHCIInterrupt -- interrupt handler
 * @param v -- vector number
 * @param p -- Pointer to interrupt frame
 */
void XHCIInterrupt(size_t v, void* p) {
	AuDisableInterrupt();
	uint32_t status = 0;
	/* clear the USB status bit */
	status = xhcidev->op_regs->op_usbsts;
	/* acknowledge the controller */
	xhcidev->op_regs->op_usbsts = status;


	if (status & XHCI_USB_STS_EINT) 
		XHCIHandleEventInterrupt();

	/*End Of Interrupt to Interrupt Controller */
	AuInterruptEnd(xhcidev->irq);
	AuEnableInterrupt();
}

void XHCIBiosHandoff(xhci_ext_cap_t* cap) {
	while (1) {
		switch (cap->id) {
		case 1: {
			AuTextOut("USB Legacy Support \n");
			xhci_legacy_cap_t* legacy_cp = (xhci_legacy_cap_t*)cap;
			if ((legacy_cp->legctlsts & (1<<16)))
				AuTextOut("XHCI was owned by BIOS/UEFI \n");
			legacy_cp->legctlsts |= (1 << 24);
			int timeout = 1000;
			while ((legacy_cp->legctlsts & (1 << 16)) && timeout--) {
				APICTimerSleep(200);
			}
			if (timeout <= 0) {
				AuTextOut("XHCI BIOS Hand off failed \n");
			}
			AuTextOut("BIOS Handoff was successfully \n");
			break;
		}
		case 2: {
			xhci_ex_cap_protocol_t* prot = (xhci_ex_cap_protocol_t*)cap;
			char prot_name[4];
			memset(prot_name, 0, 4);
			memcpy(prot_name, prot->name, 4);
			uint8_t start_port = prot->port_cap_field & 0xff;
			uint8_t max_port = (prot->port_cap_field >> 8) & 0xff;
			uint32_t proto_speed_id_count = (prot->port_cap_field >> 28) & 0xffff;
			uint8_t slot_type = prot->port_cap_field2 & 0x1f;
			AuTextOut("[USB]:Protocol speed count -> %d, slot_type -> %d \n", proto_speed_id_count, slot_type);
			break;
		}
		case 3:
			AuTextOut("USB Extended Power Management \n");
			break;
		case 4:
			AuTextOut("USB I/O Virtualization \n");
			break;
		case 5:
			AuTextOut("USB Message Interrupt \n");
			break;
		case 6:
			AuTextOut("USB Local Memory \n");
			break;
		case 10:
			AuTextOut("USB Debug Capabiltiy \n");
			break;
		}
		if (cap->next == 0)
			break;
		cap = ((xhci_ext_cap_t*)cap + (static_cast<uint64_t>(cap->next) << 2));
	}
}

/*
* AuDriverMain -- Main entry for driver
*/
AU_EXTERN AU_EXPORT int AuDriverMain() {
	AuTextOut("XHCI ***** \n");
	int bus, dev, func;

	uint64_t device = AuPCIEScanClassIF(0x0C, 0x03, 0x30, &bus, &dev, &func);
	if (device == 0xFFFFFFFF) {
		AuTextOut("[usb]: xhci not found \n");
		for (;;);
		return 1;
	}


	if (AuCheckDevice(0x0C, 0x03, 0x30)) {
		AuTextOut("Already USB Driver installed \n");
		return 1;
	}

	uint64_t vendorID = AuPCIERead(device, PCI_VENDOR_ID, bus, dev, func);
	uint64_t deviceID = AuPCIERead(device, PCI_DEVICE_ID, bus, dev, func);


	xhcidev = (XHCIDevice*)kmalloc(sizeof(XHCIDevice));
	xhcidev->initialised = false;
	xhcidev->usb_lock = AuCreateSpinlock(false);
	usbThreadCmd = 0;
	portNum = 0;
	deviceHotPlugList = initialize_list();
	xhcidev->vendor = vendorID;

	uint64_t command = AuPCIERead(device, PCI_COMMAND, bus, dev, func);
	command |= (1 << 10);
	command |= 0x6;


	AuPCIEWrite(device, PCI_COMMAND, command, bus, dev, func);

	switch (xhcidev->vendor) {
	case XHCI_VENDOR_INTEL:
		AuTextOut("xHCI vendor : Intel Corporation \n");
		break;
	case XHCI_VENDOR_AMD:
		AuTextOut("xHCI vendor: AMD \n");
		break;
	case XHCI_VENDOR_ASMEDIA:
		AuTextOut("xHCI vendor: ASMedia Technology Inc\n");
		break;
	case XHCI_VENDOR_LINUX_FOUNDATION:
		AuTextOut("xHCI vendor: Linux Foundation \n");
		break;
	case XHCI_VENDOR_NEC:
		AuTextOut("xHCI vendor: NEC Corporation \n");
		break;
	case XHCI_VENDOR_NVIDIA:
		AuTextOut("xHCI vendor: NVIDIA \n");
		break;
	case XHCI_VENDOR_FRESCO:
		AuTextOut("xHCI vendor: Fresco Logic \n");
		break;
	case XHCI_VENDOR_VIA:
		AuTextOut("xHCI vendor: VIA Technologies, Inc. \n");
		break;
	default:
		AuTextOut("xHCI vendor: unknown \n");
		break;
	}

	uint32_t usb_addr_low = AuPCIERead(device, PCI_BAR0, bus, dev, func) & 0xFFFFFFF0;
	uint32_t usb_addr_high = AuPCIERead(device, PCI_BAR1, bus, dev, func) & 0xFFFFFFFF;
	uint64_t mmio_addr = (static_cast<uint64_t>(usb_addr_high) << 32) | usb_addr_low;

	uint64_t mmio_base = (uint64_t)AuMapMMIO(mmio_addr, 16);
	xhci_cap_regs_t* cap = (xhci_cap_regs_t*)mmio_base;

	uint64_t op_base = (uint64_t)(mmio_base + (cap->cap_caplen_version & 0xFF));
	xhci_op_regs_t* op = (xhci_op_regs_t*)op_base;

	uint64_t rt_base = (uint64_t)(mmio_base + (cap->cap_hccparams2 & ~(0x1FUL)));
	xhci_runtime_regs_t* rt = (xhci_runtime_regs_t*)rt_base;

	uint64_t db_base = (uint64_t)(mmio_base + (cap->cap_dboff));
	xhci_doorbell_regs_t* db = (xhci_doorbell_regs_t*)db_base;

	uint64_t ext_base = (uint64_t)(mmio_base + ((static_cast<uint64_t>(cap->cap_hccparams1) >> 16) << 2));
	xhci_ext_cap_t* ext_cap = (xhci_ext_cap_t*)ext_base;

	uint64_t ports_base = (uint64_t)(mmio_base + ((cap->cap_caplen_version & 0xFF)) + 0x400);
	xhci_port_regs_t* ports = (xhci_port_regs_t*)ports_base;

	xhcidev->cap_regs = cap;
	xhcidev->op_regs = op;
	xhcidev->db_regs = db;
	xhcidev->rt_regs = rt;
	xhcidev->ext_cap = ext_cap;
	xhcidev->ports = ports;
	xhcidev->is_csz_64 = (cap->cap_hccparams1 >> 2) & 0xf;

	xhcidev->num_slots = (cap->cap_hcsparams1 & 0xFF);
	xhcidev->num_ports = (cap->cap_hcsparams1 >> 24);
	xhcidev->max_intrs = (cap->cap_hcsparams1 >> 8) & 0xffff;

	xhcidev->trb_event_index = -1;
	xhcidev->event_available = false;
	xhcidev->poll_event_for_trb = -1;
	xhcidev->poll_return_trb_type = -1;
	/* initialize the slot list */
	xhcidev->slot_list = initialize_list();
	
	//XHCIBiosHandoff(ext_cap);

	XHCIReset(xhcidev);

	uint32_t cfg = op->op_config;
	cfg &= ~0xFF;
	cfg |= xhcidev->num_slots;
	op->op_config = cfg;

	/* Initialize Device context base array pointer (DCBAAP) */
	XHCIDeviceContextInit(xhcidev);

	XHCICommandRingInit(xhcidev);

	setvect(76, XHCIInterrupt);
	AuPCIEAllocMSI(device, 76, bus, dev, func);

	XHCIEventRingInit(xhcidev);


	/* temporarily enable interrupts, because
	 * we need to send commands and receive
	 * events on interrupts, then disable it
	 */
	AuEnableInterrupt();

	x86_64_udelay(1000);
	//APICTimerSleep(1000 * 800);
	
	XHCIStartDefaultPorts(xhcidev);

	AuThread* t = AuCreateKthread(AnuvabUSB3Thread, P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL) + 4096), (uint64_t)AuGetRootPageTable(), "AnubhavUsb");
	xhcidev->usbThread = t;

	/* Disable all interrupts again because
	* scheduler will enable them all */

	AuDevice* audev = (AuDevice*)kmalloc(sizeof(AuDevice));
	audev->classCode = 0x0C;
	audev->subClassCode = 0x03;
	audev->progIf = 0x30;
	audev->initialized = true;
	audev->aurora_dev_class = DEVICE_CLASS_USB3;
	audev->aurora_driver_class = DRIVER_CLASS_USB;
	AuRegisterDevice(audev);
	xhcidev->initialised = true;

	AuDisableInterrupt();
	return 0;
}

/*
 * XHCIGetHost -- returns the host device
 * data structure
 */
XHCIDevice* XHCIGetHost() {
	return xhcidev;
}