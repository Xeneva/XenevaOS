/**
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

#include "xhci.h"
#include <stdint.h>
#include <_null.h>
#include <Mm/kmalloc.h>
#include <Hal/x86_64_cpu.h>


/*
* XHCISlotGetEP_DCI -- returns an endpoint by its endpoint number
*/
XHCIEndpoint* XHCISlotGetEP_DCI(XHCISlot* slot, uint8_t endp_num) {
	for (int i = 0; i < slot->endpoints->pointer; i++) {
		XHCIEndpoint* ep = (XHCIEndpoint*)list_get_at(slot->endpoints, i);
		if (ep->dci == endp_num)
			return ep;
	}
	return NULL;
}

/*
* XHCIGetSlotByID -- returns a slot from slot list
* @param dev -- Pointer to USB device
* @param slot_id -- slot id
*/
XHCISlot* XHCIGetSlotByID(XHCIDevice* dev, uint8_t slot_id) {
	for (int i = 0; i < dev->slot_list->pointer; i++) {
		XHCISlot* slot = (XHCISlot*)list_get_at(dev->slot_list, i);
		if (slot->slot_id == slot_id)
			return slot;
	}

	return NULL;
}

/*
 * XHCISlotGetEP -- returns an endpoint by its endpoint number
 */
XHCIEndpoint* XHCISlotGetEP(XHCISlot* slot, uint8_t endp_num) {
	for (int i = 0; i < slot->endpoints->pointer; i++) {
		XHCIEndpoint* ep = (XHCIEndpoint*)list_get_at(slot->endpoints, i);
		if (ep->endpoint_num == endp_num)
			return ep;
	}
	return NULL;
}


/*
 * XHCIAddSlot -- adds a slot structure to slot list
 * @param dev -- Pointer to usb device
 * @param slot -- pointer to slot
 */
void XHCIAddSlot(XHCIDevice* dev, XHCISlot* slot) {
	list_add(dev->slot_list, slot);
}

/*
* XHCISlotRemove -- remove a slot structure from slot list
* @param dev -- Pointer to usb device structure
* @param slot_id -- slot id
*/
void XHCISlotRemove(XHCIDevice* dev, uint8_t slot_id) {
	XHCISlot* slot_ = NULL;
	for (int i = 0; i < dev->slot_list->pointer; i++) {
		XHCISlot* slot = (XHCISlot*)list_get_at(dev->slot_list, i);
		if (slot->slot_id == slot_id) {
			slot_ = (XHCISlot*)list_remove(dev->slot_list, i);
		}
	}
	if (slot_)
		kfree(slot_);
	return;
}



/*
 * XHCIGetMaxPacketSize -- get the packe size by
 * looking USB speed
 * @param speed -- speed of USB port
 */
size_t XHCIGetMaxPacketSize(uint8_t speed) {
	switch (speed) {
	case USB_LOW_SPEED:
	case USB_FULL_SPEED:
		return 8;
	case USB_HIGH_SPEED:
		return 64;
	case USB_SUPER_SPEED:
	case USB_SUPER_SPEED_PLUS:
		return 512;
	default:
		return 512;
	}
}

/*
* XHCISendCmdDefaultEP -- sends command to slot trb, i.e
* default control endpoint
* @param slot -- pointer to slot data structure
* @param param1 -- first parameter of trb structure
* @param param2 -- 2nd parameter of trb structure
* @param status -- status field of trb structure
* @param ctrl -- control field of trb structure
*/
void XHCISendCmdDefaultEP(XHCISlot* slot, uint32_t param1, uint32_t param2, uint32_t status, uint32_t ctrl) {

	ctrl &= ~1;
	ctrl |= slot->cmd_ring_cycle;
	slot->cmd_ring[slot->cmd_ring_index].trb_param_1 = param1;
	slot->cmd_ring[slot->cmd_ring_index].trb_param_2 = param2;
	slot->cmd_ring[slot->cmd_ring_index].trb_status = status;
	slot->cmd_ring[slot->cmd_ring_index].trb_control = ctrl;

	slot->cmd_ring_index++;

	if (slot->cmd_ring_index >= slot->cmd_ring_max) {
		slot->cmd_ring[slot->cmd_ring_index].trb_control |= slot->cmd_ring_cycle;
		if (slot->cmd_ring[slot->cmd_ring_index].trb_control & (1 << 1)) {
			slot->cmd_ring_cycle ^= 1;
		}
		slot->cmd_ring_index = 0;
	}
}


/*
* XHCISendCmdOtherEP -- sends command to endpoint trb
* @param slot -- pointer to slot data structure
* @param endp_num -- endpoint number
* @param param1 -- first parameter of trb structure
* @param param2 -- 2nd parameter of trb structure
* @param status -- status field of trb structure
* @param ctrl -- control field of trb structure
*/
void XHCISendCmdOtherEP(XHCISlot* slot, uint8_t endp_num, uint32_t param1, uint32_t param2, uint32_t status, uint32_t ctrl) {
	XHCIEndpoint* ep = XHCISlotGetEP(slot, endp_num);
	if (!ep)
		return;

	ctrl &= ~1;
	ctrl |= ep->cmd_ring_cycle & 0x1;
	ep->cmd_ring[ep->cmd_ring_index].trb_param_1 = param1;
	ep->cmd_ring[ep->cmd_ring_index].trb_param_2 = param2;
	ep->cmd_ring[ep->cmd_ring_index].trb_status = status;
	ep->cmd_ring[ep->cmd_ring_index].trb_control = ctrl;

	ep->cmd_ring_index++;

	if (ep->cmd_ring_index >= ep->cmd_ring_max) {
		ep->cmd_ring[ep->cmd_ring_index].trb_control |= ep->cmd_ring_cycle;
		if (ep->cmd_ring[ep->cmd_ring_index].trb_control & (1 << 1)) {
			if (ep->cmd_ring_cycle)
				ep->cmd_ring_cycle ^= 1;
		}

		ep->cmd_ring_index = 0;
	}
}

/*
* XHCISendCmdToHost -- Sends command to XHCI
* host
* @param dev -- Pointer to usb device structure
* @param param1 -- parameter 1 of TRB
* @param param2 -- parameter 2 of TRB
* @param status -- status of TRB
* @param ctrl -- control for TRB
*/
void XHCISendCmdToHost(XHCIDevice* dev, uint32_t param1, uint32_t param2, uint32_t status, uint32_t ctrl) {

	ctrl &= ~1;
	ctrl |= dev->cmd_ring_cycle;
	dev->cmd_ring[dev->cmd_ring_index].trb_param_1 = param1;
	dev->cmd_ring[dev->cmd_ring_index].trb_param_2 = param2;
	dev->cmd_ring[dev->cmd_ring_index].trb_status = status;
	dev->cmd_ring[dev->cmd_ring_index].trb_control = ctrl;

	dev->cmd_ring_index++;

	if (dev->cmd_ring_index >= dev->cmd_ring_max) {
		dev->cmd_ring[dev->cmd_ring_index].trb_control |= dev->cmd_ring_cycle;
		if (dev->cmd_ring[dev->cmd_ring_index].trb_control & (1 << 1)) {
			dev->cmd_ring_cycle ^= 1;
		}
		dev->cmd_ring_index = 0;
	}
}

/*
* XHCIRingDoorbellHost -- rings the host doorbell
* @param dev -- Pointer to usb structure
*/
void XHCIRingDoorbellHost(XHCIDevice* dev) {
	dev->db_regs->doorbell[0] = (0 << 16) | 0;
}

/*
* XHCIRingDoorbellSlot -- rings the doorbell by slot
* @param dev -- Pointer to usb structure
* @param slot -- slot id
* @param endpoint -- endpoint number, it should be 0 if
* the slot is 0, else values 1-255 should be placed
*
*/
void XHCIRingDoorbellSlot(XHCIDevice* dev, uint8_t slot, uint32_t endpoint) {
	dev->db_regs->doorbell[slot] = endpoint;
}



/*
* XHCIPollEvent -- waits for an event to occur on interrupts
* @param usb_device -- pointer to usb device structure
* @param trb_type -- type of trb to look
* @return trb_event_index -- index of the trb on event_ring_segment
*/
int XHCIPollEvent(XHCIDevice* usb_device, int trb_type) {
	int timeout = 1000;
	for (;;) {
		if (usb_device->event_available && usb_device->poll_return_trb_type == trb_type) {
			usb_device->event_available = false;

			///* here we need a delay, while using VirtualBox */
			x86_64_udelay(100);

			int idx = usb_device->trb_event_index;
			usb_device->trb_event_index = -1;
			usb_device->poll_return_trb_type = -1;
			return idx;
		}
		if (timeout <= 0)
			break;
		x86_64_udelay(100);
		timeout--;
	}

	return -1; //trb_event_index;
}