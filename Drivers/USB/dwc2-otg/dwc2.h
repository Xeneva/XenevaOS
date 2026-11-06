/**
* BSD 2-Clause License
*
* @file dwc2.h
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

#ifndef __DWC2_H__
#define __DWC2_H__

#include <stdint.h>
#include <Mm/dma.h>

#define USB_PID_SETUP 3
#define USB_PID_DATA 2
#define USB_PID_DATA0 0
#define USB_PID_DATA2 1


#define DWC2_GET_PORT_SPEED(hport)  ((hport >> 17) & 0x3)  
#define EP_OUT 0
#define EP_IN 1
#define EP_BIDIR 2

#define EP_CONTROL 0
#define EP_ISOCHRONOUS 1
#define EP_BULK 2
#define EP_INTERRUPT 3

typedef struct {
	uint8_t dev_address; // 0 is default, 1-127 are assigned by device
	uint8_t ep_num; // 0 control endpoint, 0-15 max
	uint8_t dir; //0 = OUT, 1 = IN , 2 = BIDIR
	uint8_t type; //0 = Control, 1 = Isoch, 2, = Bulk, 3 = Interrupt
	uint8_t speed;  //0 = HIGH, 1, FULL, 2 = LOW
	/** max_packet_sz:  Control (HIGH-SPEED) : 64
	* Control FS = 8/16/32/64
	* Control LS = 8
	* Bulk HS = 512
	* Bulk FS = 64
	* Interrupt = 1-64 (from descriptor)
	*/
	uint16_t max_packet_sz;
	uint8_t interval;
	uint8_t split_enable;
	uint8_t hub_address;
	uint8_t hub_port;
	uint8_t next_pid;
	uint8_t ch;
}dwc2_usb_endpoint_t;


typedef struct {
	uint8_t bmRequestType;
	uint8_t bmRequest;
	uint16_t wValue;
	uint16_t wIndex;
	uint16_t wLength;
}usb_setup_packet_t;
/**
 * @brief dwc2_read -- reads a value from dwc2 register
 * @param base -- memory base
 */
extern uint32_t dwc2_read(uint64_t base);

/**
 * @brief dwc2_write -- write a value to dwc2 register
 * @param base -- memory base
 * @param value -- value to write
 */
extern void dwc2_write(uint64_t base, uint32_t value);

extern bool dwc2_control_transfer(struct dwc2_core_regs* regs, dwc2_usb_endpoint_t* ep, uint8_t bmRequestType, uint8_t b_request,
	uint16_t wValue, uint16_t wIndex, void* data, uint16_t wLength);

extern void dwc2_interrupt_transfer(dwc2_core_regs* regs, dwc2_usb_endpoint_t* ep, void* intbuf, uint8_t ch, uint32_t odd, uint8_t pid);
extern void dwc2_interrupt_transfer_csplit(dwc2_core_regs* regs, dwc2_usb_endpoint_t* ep, void* intbuf, uint8_t ch, uint32_t odd, uint8_t pid);

/**
 * dwc2_enumerate_root_device -- enumerate the root device
 * @param regs -- pointer to system regs
 */
extern void dwc2_enumerate_root_device(struct dwc2_core_regs* regs);

/**
 * @brief dwc2_handle_channel_interrupt -- handle channel
 * interrupts here
 * @param regs -- Pointer to dwc2 core registers
 * @param ch -- channel numbers
 */
extern void dwc2_handle_channel_interrupt(dwc2_core_regs* regs, int ch);

extern int dwc2_wait_channel(struct dwc2_core_regs* regs, int ch);

extern int dwc2_wait_channel_look_bit(struct dwc2_core_regs* regs, int ch, int bit);

/**
 * @brief dwc2_get_dma_address -- return pre allocated dma address
 */
extern void* dwc2_get_dma_address();

extern void* dwc2_get_dma_address_phys();

extern void dwc2_flush_tx_fifo(struct dwc2_core_regs* regs, uint32_t nfifo);

extern void dwc2_flush_rx_fifo(struct dwc2_core_regs* regs);

/**
 * @brief dwc2_add_to_used_dma_list --
 * there is a bug i guess within the dwc2 ip
 * where same buffer can't be used for setup
 * packet
 */
extern void dwc2_add_to_used_dma_list(void* phys);

/**
 * @brief dwc2_free_used_dma_list -- free up all used
 * physical memories, this function must be called
 * at the end of the class driver initialization
 */
extern void dwc2_free_used_dma_list();

extern void root_hub_transfer_int(dwc2_core_regs* regs, uint8_t pid);

extern void* root_hub_get_interrupt_buffer();

extern void root_hub_handle_port_change(dwc2_core_regs* regs, uint8_t port);

/**
 * @brief dwc2_assign_address -- assigns address
 * to a usb device
 */
extern uint8_t dwc2_assign_address();

/**
 * @brief dwc2_get_dma_class -- get global dma class
 */
extern AuDMAGlobalClass* dwc2_get_dma_class();

extern dwc2_core_regs* dwc2_get_core_regs();
#endif