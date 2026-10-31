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

#ifndef __XHCI_CMD_H__
#define __XHCI_CMD_H__

#include <stdint.h>
#include "usb3.h"

/* Standard XHCI defined Transfer/Command/Event
* TRB type values
*/
#define TRB_TRANSFER_NORMAL                 1
#define TRB_TRANSFER_SETUP_STAGE            2
#define TRB_TRANSFER_DATA_STAGE             3
#define TRB_TRANSFER_STATUS_STAGE           4
#define TRB_TRANSFER_ISOCH                  5
#define TRB_TRANSFER_LINK                   6
#define TRB_TRANSFER_EVENT_DATA             7
#define TRB_TRANSFER_NO_OP                  8
#define TRB_CMD_ENABLE_SLOT                 9
#define TRB_CMD_DISABLE_SLOT                10
#define TRB_CMD_ADDRESS_DEV                 11
#define TRB_CMD_CONFIG_ENDPOINT             12
#define TRB_CMD_EVALUATE_CTX                13
#define TRB_CMD_RESET_ENDPOINT              14
#define TRB_CMD_STOP_ENDPOINT               15
#define TRB_CMD_SET_TR_DEQ_POINTER          16
#define TRB_CMD_RESET_DEV                   17
#define TRB_CMD_FORCE_EVENT                 18
#define TRB_CMD_NEGOTIATE_BANDWIDTH          19
#define TRB_CMD_SET_LATENCY_TOLERANCE_VALUE  20
#define TRB_CMD_GET_PORT_BANDWIDTH           21
#define TRB_CMD_FORCE_HEADER                 22
#define TRB_CMD_NO_OP                        23
#define TRB_CMD_GET_EXT_PROPERTY             24
#define TRB_CMD_SET_EXT_PROPERTY             25
#define TRB_EVENT_TRANSFER                   32
#define TRB_EVENT_CMD_COMPLETION             33
#define TRB_EVENT_PORT_STATUS_CHANGE         34
#define TRB_EVENT_BANDWIDTH_REQUEST          35
#define TRB_EVENT_DOORBELL                   36
#define TRB_EVENT_HOST_CONTROLLER            37
#define TRB_EVENT_DEVICE_NOTIFICATION        38
#define TRB_EVENT_MFINDEX                    39

/*
* XHCIEnableSlot -- sends enable slot command to xHC
* @param dev -- Pointer to usb device structure
* @param slot_type -- type of slot mainly in between 0-31
*/
extern void XHCIEnableSlot(USBDevice *dev, uint8_t slot_type);

/*
* XHCIDisableSlot -- sends disable slot command to xHC
* @param dev -- Pointer to usb device structure
* @param slot_num -- slot id to disable
*/
extern void XHCIDisableSlot(USBDevice *dev, uint8_t slot_num);

/* XHCISendNoopCmd -- Send No operation command
* @param dev -- Pointer to USB structure
*/
extern void XHCISendNoopCmd(USBDevice* dev);

/*
* XHCICreateSetupTRB -- creates a setup stage trb
*/
void XHCICreateSetupTRB(XHCISlot *slot, uint8_t rType, uint8_t bRequest, uint16_t value, uint16_t wIndex, uint16_t wLength, uint8_t trt);

/*
* XHCICreateDataTRB -- creates data stage trb
* @param dev -- pointer to usb structure
* @param buffer -- pointer to memory area
* @param size -- size of the buffer
* @param in_direction -- direction
*/
void XHCICreateDataTRB(XHCISlot *slot, uint64_t buffer, uint16_t size, bool in_direction);

/*
* XHCICreateStatusTRB -- creates status stage trb
* @param dev -- pointer to usb strucutue
* @param in_direction -- direction
*/
void XHCICreateStatusTRB(XHCISlot* slot, bool in_direction);

/*
* XHCISendAddressDevice -- issues address device command
* @param dev -- pointer to usb device structure
* @param bsr -- block set address request (BSR)
* @param input_ctx_ptr -- address of input context
* @param slot_id -- slot id number
*/
void XHCISendAddressDevice(USBDevice* dev, XHCISlot *slot, uint8_t bsr, uint64_t input_ctx_ptr, uint8_t slot_id);

#define CTL_TRANSFER_TRT_NO_DATA 0
#define CTL_TRANSFER_TRT_RESV  1
#define CTL_TRANSFER_TRT_OUT_DATA 2
#define CTL_TRANSFER_TRT_IN_DATA 3
/*
* XHCISendControlCmd -- Sends control commands to xhci
* @param dev -- pointer to usb device structure
* @param slot_id -- slot number
* @param request -- USB request packet structure
* @param buffer_addr -- input buffer address
* @param len -- length of the buffer
*/
void XHCISendControlCmd(USBDevice* dev, XHCISlot* slot, uint8_t slot_id, const USB_REQUEST_PACKET *request, uint64_t buffer_addr, const size_t len,
	uint8_t trt = CTL_TRANSFER_TRT_IN_DATA);
/*
* XHCISendControlCmdEndp -- Sends control commands to xhci
* @param dev -- pointer to usb device structure
* @param slot_id -- slot number
* @param request -- USB request packet structure
* @param buffer_addr -- input buffer address
* @param len -- length of the buffer
* @param endpnum -- Endpoint number
*/
void XHCISendControlCmdEndp(USBDevice* dev, XHCISlot* slot, uint8_t slot_id, const USB_REQUEST_PACKET *request, uint64_t buffer_addr, const size_t len,
	uint32_t endpnum);

/*
* XHCISendNormalTRB -- sends a normal trb
*/
void XHCISendNormalTRB(USBDevice* dev, XHCISlot *slot, uint64_t data_buffer, uint16_t data_len, XHCIEndpoint* ep);
/*
* XHCIEvaluateContextCmd -- evaluate context command
* @param dev -- Pointer to usb device
* @param input_ctx_ptr -- input context physical address
* @param slot_id -- slot number
*/
void XHCIEvaluateContextCmd(USBDevice* dev, uint64_t input_ctx_ptr, uint8_t slot_id);

/*
* XHIConfigureEndpoint -- configure the endpoint
*/
void XHCIConfigureEndpoint(USBDevice* dev, uint64_t input_ctx_ptr, uint8_t slot_id);


#endif