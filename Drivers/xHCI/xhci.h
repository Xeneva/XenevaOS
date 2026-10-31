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

#ifndef __XHCI_H__
#define __XHCI_H__

#include <stdint.h>
#include <list.h>
#include <Sync/spinlock.h>
#include <Hal/x86_64_sched.h>

#define XHCI_VENDOR_INTEL           0x8086
#define XHCI_VENDOR_AMD             0x1022
#define XHCI_VENDOR_ASMEDIA         0x1B21
#define XHCI_VENDOR_LINUX_FOUNDATION 0x1D6A
#define XHCI_VENDOR_NEC             0x1033
#define XHCI_VENDOR_NVIDIA          0x10DE
#define XHCI_VENDOR_FRESCO          0x1E31
#define XHCI_VENDOR_VIA             0x1106

#define XHCI_USB_CMD_INTE  (1<<2) //Interrupter enable
#define XHCI_USB_CMD_HSEE  (1<<3) // Host System Error enable

#define XHCI_USB_STS_HCH  (1<<0)  //HC Halted
#define XHCI_USB_STS_HSE  (1<<2)  //Host System Error -- 1 on error
#define XHCI_USB_STS_EINT (1<<3)  //Event Interrupt
#define XHCI_USB_STS_PCD  (1<<4)  //Port Change Detect
#define XHCI_USB_STS_SSS  (1<<8)  //Save state status -- 1 when cmd_css is 1
#define XHCI_USB_STS_RSS  (1<<9)  //Restore state status
#define XHCI_USB_STS_SRE  (1<<10) //Save Restore error
#define XHCI_USB_STS_CNR  (1<<11) //Controller Not Ready
#define XHCI_USB_STS_HCE  (1<<12) //Host Controller Error

#define XHCI_USB_CFG_MXSLOT_ENABLE  0xFF
#define XHCI_USB_CFG_U3_EN  (1<<8)  //U3 Entry Enable
#define XHCI_USB_CFG_CINFO_EN    (1 << 9) //Config Information Enable

#define XHCI_USB_CCR_RCS  (1<<0)  //Ring Cycle State
#define XHCI_USB_CCR_CS   (1<<1)  //Command Stop
#define XHCI_USB_CCR_CA   (1<<2)  //Command Abort
#define XHCI_USB_CCR_CRR  (1<<3)  //Command Ring Running

#define XHCI_USB_CCR_PTR_LO  0xFFFFFFC0
#define XCHI_USB_CCR_PTR     0xFFFFFFFFFFFFFFC0

#define XHCI_PORTSC_CCS  (1<<0) //Current Connect Status
#define XHCI_PORTSC_PED  (1<<1)  //Port Enabled/Disabled
#define XHCI_PORTSC_OCA  (1<<3)  //Overcurrent Active
#define XHCI_PORTSC_PR   (1<<4)  //Port Reset
#define XHCI_PORTSC_PP   (1<<9)  //Port Power 
#define XHCI_PORTSC_CSC  (1<<17) //Connect Status Change
#define XHCI_PORTSC_PEC  (1<<18)  //Port Enabled/Disabled Change
#define XHCI_PORTSC_PRC  (1<<21)  //Port Reset Change
#define XHCI_PORTSC_WPR  (1<<31) //On USB3 port warm reset

#define XHCI_INT_ERDP_BUSY  (1<<3)


#define XHCI_TRB_SIZE  16 
#define XHCI_EVENT_RING_SEG_TBL_ENTRY_SIZE 16

#define XHCI_TRB_ENT  0x200000000
#define XHCI_TRB_ISP  0x400000000
#define XHCI_TRB_IOC  0x2000000000
#define XHCI_TRB_IDT  0x4000000000
#define XHCI_TRB_TRT(x)   ((uint64_t)x << 48)
#define XHCI_TRB_DIR_IN   ((uint64_t)1 << 48)

#define XHCI_DOORBELL_ENDPOINT_0    1
#define XHCI_DOORBELL_ENDPOINT_1    2

/* USB speed numbers */
#define USB_SPEED_RESERVED   0
#define USB_FULL_SPEED       1
#define USB_LOW_SPEED        2
#define USB_HIGH_SPEED       3
#define USB_SUPER_SPEED      4
#define USB_SUPER_SPEED_PLUS 5

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

typedef void(*endpoint_callback)(void* dev, void* slot, void* Endp);

#pragma pack(push,1)
typedef struct _xhci_cap_regs_ {
	uint8_t cap_caplen_version;
	uint8_t reserved;
	uint16_t hci_version;
	uint32_t cap_hcsparams1;
	uint32_t cap_hcsparams2;
	uint32_t cap_hcsparams3;
	uint32_t cap_hccparams1;
	uint32_t cap_dboff;
	uint32_t cap_hccparams2;
}xhci_cap_regs_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _xhci_op_regs_ {
	uint32_t op_usbcmd;
	uint32_t op_usbsts;
	uint32_t op_pagesize;
	uint32_t op_pad1[2];
	uint32_t op_dnctrl;
	uint64_t op_crcr;
	uint32_t op_pad2[4];
	uint32_t op_dcbaap_lo;
	uint32_t op_dcbaap_hi;
	uint32_t op_config;
}xhci_op_regs_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _xhci_intr_reg_ {
	unsigned intr_man;
	unsigned intr_mod;
	unsigned evtRngSegTabSz;
	unsigned res;
	unsigned evtRngSegBaseLo;
	unsigned evtRngSegBaseHi;
	unsigned evtRngDeqPtrLo;
	unsigned evtRngDeqPtrHi;
}xhci_interrupter_reg_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _xhci_runtime_regs_ {
	unsigned micro_frame_index;
	char res[28];
	xhci_interrupter_reg_t intr_reg[];
}xhci_runtime_regs_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _xhci_doorbell_ {
	unsigned doorbell[256];
}xhci_doorbell_regs_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _xhci_trb_ {
	uint32_t trb_param_1;
	uint32_t trb_param_2;
	uint32_t trb_status;
	uint32_t trb_control;
}xhci_trb_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _xhci_ex_cap_ {
	uint32_t id : 8;
	uint32_t next : 8;
	uint32_t controller_bios_semaphore : 1;
	uint32_t rsvd : 7;
	uint32_t controller_os_sem : 1;
	uint32_t rsvd2 : 7;
}xhci_ext_cap_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _xhci_port_reg_ {
	uint32_t port_sc;
	uint32_t port_pmsc;
	uint32_t port_link_info;
	uint32_t port_hardware_LMP_ctl;
}xhci_port_regs_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _endp_ {
	xhci_trb_t* cmd_ring;
	uint8_t endpointAddress;
	uint8_t endpointAttr;
	uint8_t endpoint_num;
	uint8_t endpoint_type;
	uint8_t interval;
	uint16_t max_packet_sz;
	unsigned cmd_ring_index;
	unsigned cmd_ring_max;
	unsigned cmd_ring_cycle;
	uint8_t dci;
	uint32_t offset;
	uint32_t dc_offset;
	uint8_t ep_type;
	uint8_t dir;
	endpoint_callback callback;
}XHCIEndpoint;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _xhci_slot_ {
	uint8_t slot_id;
	uint8_t root_hub_port_num;
	uint8_t port_speed;
	uint64_t cmd_ring_base;
	uint64_t input_context_phys;
	uint64_t output_context_phys;
	uint8_t classC;
	uint8_t subClassC;
	uint8_t prot;
	xhci_trb_t* cmd_ring;
	unsigned cmd_ring_index;
	unsigned cmd_ring_max;
	unsigned cmd_ring_cycle;
	uint64_t descriptor_buff;
	uint16_t interface_val;
	list_t* endpoints;
}XHCISlot;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _xhci_event_trb_ {
	uint64_t ptr;
	uint32_t rsvd : 24;
	uint32_t completionCode : 8;
	uint32_t cycleBit : 1;
	uint32_t rsvd2 : 9;
	uint32_t trbType : 6;
	uint32_t rsvd3 : 16;
} xhci_event_trb_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _xhci_link_trb_ {
	uint64_t segment_ptr;
	uint32_t rsvd : 24;
	uint32_t interrupter_target : 8;
	uint32_t cycle_bit : 1;
	uint32_t toggle_cycle : 1;
	uint32_t rsvd2 : 2;
	uint32_t chain_bit : 1;
	uint32_t ioc : 1;
	uint32_t rsvd3 : 4;
	uint32_t trb_type : 6;
	uint32_t rsvd4 : 16;
} xhci_link_trb_t;
#pragma pack (pop)

#pragma pack(push,1)
typedef struct _xhci_setup_trb_t_ {
	uint32_t bmRequestType : 8;
	uint32_t bRequest : 8;
	uint32_t wValue : 16;
	uint32_t wIndex : 16;
	uint32_t wLength : 16;
	uint32_t trbTransferLength : 17;
	uint32_t rsvdZ : 5;
	uint32_t interrupterTarget : 10;
	uint32_t cycleBit : 1;
	uint32_t rsvdZ2 : 4;
	uint32_t ioc : 1;
	uint32_t immediateData : 1;
	uint32_t rsvdZ3 : 3;
	uint32_t trbType : 6;
	uint32_t transferType : 2;
	uint32_t rsvdZ4 : 14;
} xhci_setup_trb_t;
#pragma pack(pop)


#pragma pack(push,1)
typedef struct _xhci_data_trb_ {
	uint64_t dataBuffer;
	uint32_t transferLength : 17;
	uint32_t size : 5;
	uint32_t interrupterTarget : 10;
	uint32_t cycleBit : 1;
	uint32_t evaluateNextTRB : 1;
	uint32_t interrupt_on_short_packet : 1;
	uint32_t no_snoop : 1;
	uint32_t chain_bit : 1;
	uint32_t ioc : 1;
	uint32_t immediate_data : 1;
	uint32_t rsvdZ : 3;
	uint32_t trb_type : 6;
	uint32_t direction : 1;
	uint32_t rsvdZ2 : 15;
}xhci_data_trb_t;
#pragma pack(pop)

typedef struct _xhci_status_trb_ {
	uint32_t rsvdZ0[2];
	uint32_t rsvdZ : 22;
	uint32_t interrupter_target : 10;
	uint32_t cycle_bit : 1;
	uint32_t evaluateNextTRB : 1;
	uint32_t rsvdZ2 : 2;
	uint32_t chainBit : 1;
	uint32_t ioc : 1;
	uint32_t rsvdZ3 : 4;
	uint32_t trb_type : 6;
	uint32_t direction : 1;
	uint32_t rsvdZ4 : 15;
}xhci_status_trb_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _xhci_ex_cap_protocol_ {
	xhci_ext_cap_t base;
	unsigned char name[4];
	uint32_t port_cap_field;
	uint32_t port_cap_field2;
}xhci_ex_cap_protocol_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct __xhci_legacy_cap__ {
	volatile uint32_t cap_id_and_next;
	volatile uint32_t legctlsts;
}xhci_legacy_cap_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _xhci_noop_trb_ {
	uint32_t rsvdZ[3];
	uint32_t cycleBit : 1;
	uint32_t rsvdZ2 : 9;
	uint32_t trb_type : 6;
	uint32_t rsvdZ3 : 16;
}xhci_noop_cmd_trb_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _xhci_erst_ {
	uint32_t event_ring_segment_lo;
	uint32_t event_ring_seg_hi;
	uint32_t ring_seg_size;
	uint32_t rerserved;
}xhci_erst_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _xhci_input_ctx_ {
	uint32_t drop_ctx_flags;
	uint32_t add_ctx_flags;
	uint32_t rsvd1;
	uint32_t rsvd2;
	uint32_t rsvd3;
	uint32_t rsvd4;
	uint32_t rsvd5;
	uint32_t paramlast;
}XHCIInputContext;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _xhci_dev_ {
	bool initialised;
	bool is_csz_64;
	list_t* slot_list;
	xhci_cap_regs_t* cap_regs;
	xhci_op_regs_t* op_regs;
	xhci_doorbell_regs_t* db_regs;
	xhci_runtime_regs_t* rt_regs;
	xhci_ext_cap_t* ext_cap;
	xhci_port_regs_t* ports;
	uint32_t num_slots;
	uint32_t num_ports;
	uint16_t max_intrs;
	uint8_t irq;
	uint64_t* dev_ctx_base_array;
	xhci_trb_t* cmd_ring;
	uint64_t cmd_ring_phys;
	unsigned cmd_ring_index;
	unsigned cmd_ring_max;
	unsigned cmd_ring_cycle;
	uint64_t* event_ring_segment;
	uint64_t* event_ring_seg_phys;
	unsigned evnt_ring_index;
	unsigned evnt_ring_cycle;
	unsigned evnt_ring_max;
	int poll_event_for_trb;
	bool event_available;
	int poll_return_trb_type;
	int trb_event_index;
	uint16_t vendor;
	Spinlock* usb_lock;
	AuThread* usbThread;
}XHCIDevice;
#pragma pack(pop)

typedef struct _usb_hotplug_ {
	unsigned int portNum;
	bool initialized;
}USBHotPlug;


#define CTL_TRANSFER_TRT_NO_DATA 0
#define CTL_TRANSFER_TRT_RESV  1
#define CTL_TRANSFER_TRT_OUT_DATA 2
#define CTL_TRANSFER_TRT_IN_DATA 3

#define USB_SLOT_CTX_DWORD0(entries, hub, multi_tt, speed, route_string) \
	(((entries & 0x1F) << 27) | ((hub & 1) << 26) | ((multi_tt & 1) << 25) | ((speed & 0xF) << 20) | (route_string & ((1 << 20) - 1)))

#define USB_SLOT_CTX_DWORD1(num_ports, root_hub_port, max_exit_latency) \
	(((num_ports & 0xFF) << 24) | ((root_hub_port & 0xFF) << 16) | (max_exit_latency & 0xFFFF))

#define USB_ENDPOINT_CTX_DWORD0(max_esit_high, interval, lsa, max_p_streams, mult, ep_state) \
	(((max_esit_high & 0xFF) << 24) | ((interval & 0xFF) << 16) | ((lsa & 1) << 15) | ((max_p_streams & 0x1F) << 10) | ((mult & 0x3) << 8) | (ep_state & 0x7))

#define USB_ENDPOINT_CTX_DWORD1(max_packet_size, max_burst_size, hid, ep_type, cerr) \
	(((max_packet_size & 0xFFFF) << 16) | ((max_burst_size & 0xFF) << 8) | ((hid & 1) << 7) | ((ep_type & 0x7) << 3) | ((cerr & 0x3) << 1))

#define USB_ENDPOINT_CTX_DWORD2(trdp, dcs) \
	((trdp & 0xFFFFFFFF) | (dcs & 1))

#define USB_ENDPOINT_CTX_DWORD3(trdp) \
	((trdp >> 32) & 0xFFFFFFFF)

#define USB_ENDPOINT_CTX_DWORD4(max_esit_lo, average_trb_len) \
	(((max_esit_lo & 0xFFFF) << 16) | (average_trb_len & 0xFFFF))



/*
* XHCISlotGetEP_DCI -- returns an endpoint by its endpoint number
*/
extern XHCIEndpoint* XHCISlotGetEP_DCI(XHCISlot* slot, uint8_t endp_num);

/*
* XHCIGetSlotByID -- returns a slot from slot list
* @param dev -- Pointer to USB device
* @param slot_id -- slot id
*/
extern XHCISlot* XHCIGetSlotByID(XHCIDevice* dev, uint8_t slot_id);

/*
 * XHCIGetHost -- returns the host device
 * data structure
 */
extern XHCIDevice* XHCIGetHost();

/*
 * XHCIAddSlot -- adds a slot structure to slot list
 * @param dev -- Pointer to usb device
 * @param slot -- pointer to slot
 */
extern void XHCIAddSlot(XHCIDevice* dev, XHCISlot* slot);

/*
* XHCISlotRemove -- remove a slot structure from slot list
* @param dev -- Pointer to usb device structure
* @param slot_id -- slot id
*/
extern void XHCISlotRemove(XHCIDevice* dev, uint8_t slot_id);

/*
* XHCIRingDoorbellHost -- rings the host doorbell
* @param dev -- Pointer to usb structure
*/
extern void XHCIRingDoorbellHost(XHCIDevice* dev);

/*
* XHCIPollEvent -- waits for an event to occur on interrupts
* @param usb_device -- pointer to usb device structure
* @param trb_type -- type of trb to look
* @return trb_event_index -- index of the trb on event_ring_segment
*/
extern int XHCIPollEvent(XHCIDevice* usb_device, int trb_type);

/*
* XHCIRingDoorbellSlot -- rings the doorbell by slot
* @param dev -- Pointer to usb structure
* @param slot -- slot id
* @param endpoint -- endpoint number, it should be 0 if
* the slot is 0, else values 1-255 should be placed
*
*/
extern void XHCIRingDoorbellSlot(XHCIDevice* dev, uint8_t slot, uint32_t endpoint);

/*
* XHCISendCmdToHost -- Sends command to XHCI
* host
* @param dev -- Pointer to usb device structure
* @param param1 -- parameter 1 of TRB
* @param param2 -- parameter 2 of TRB
* @param status -- status of TRB
* @param ctrl -- control for TRB
*/
extern void XHCISendCmdToHost(XHCIDevice* dev, uint32_t param1, uint32_t param2, uint32_t status, uint32_t ctrl);

/*
* XHCISendCmdOtherEP -- sends command to endpoint trb
* @param slot -- pointer to slot data structure
* @param endp_num -- endpoint number
* @param param1 -- first parameter of trb structure
* @param param2 -- 2nd parameter of trb structure
* @param status -- status field of trb structure
* @param ctrl -- control field of trb structure
*/
extern void XHCISendCmdOtherEP(XHCISlot* slot, uint8_t endp_num, uint32_t param1, uint32_t param2, uint32_t status, uint32_t ctrl);


/*
* XHCISendCmdDefaultEP -- sends command to slot trb, i.e
* default control endpoint
* @param slot -- pointer to slot data structure
* @param param1 -- first parameter of trb structure
* @param param2 -- 2nd parameter of trb structure
* @param status -- status field of trb structure
* @param ctrl -- control field of trb structure
*/
extern void XHCISendCmdDefaultEP(XHCISlot* slot, uint32_t param1, uint32_t param2, uint32_t status, uint32_t ctrl);


/*
 * XHCIEvaluateContextCmd -- evaluate context command
 * @param dev -- Pointer to usb device
 * @param input_ctx_ptr -- input context physical address
 * @param slot_id -- slot number
 */
extern void XHCIEvaluateContextCmd(XHCIDevice* dev, uint64_t input_ctx_ptr, uint8_t slot_id);

/*
 * XHIConfigureEndpoint -- configure the endpoint
 */
extern void XHCIConfigureEndpoint(XHCIDevice* dev, uint64_t input_ctx_ptr, uint8_t slot_id);

/*
* XHCISendAddressDevice -- issues address device command
* @param dev -- pointer to usb device structure
* @param bsr -- block set address request (BSR)
* @param input_ctx_ptr -- address of input context
* @param slot_id -- slot id number
*/
extern void XHCISendAddressDevice(XHCIDevice* dev, XHCISlot* slot, uint8_t bsr, uint64_t input_ctx_ptr, uint8_t slot_id);

/*
* XHCICreateSetupTRB -- creates a setup stage trb
*/
extern void XHCICreateSetupTRB(XHCISlot* slot, uint8_t rType, uint8_t bRequest, uint16_t value, uint16_t wIndex, uint16_t wLength, uint8_t trt);

/*
* XHCICreateDataTRB -- creates data stage trb
* @param dev -- pointer to usb structure
* @param buffer -- pointer to memory area
* @param size -- size of the buffer
* @param in_direction -- direction
*/
extern void XHCICreateDataTRB(XHCISlot* slot, uint64_t buffer, uint16_t size, bool in_direction);

/*
* XHCICreateStatusTRB -- creates status stage trb
* @param dev -- pointer to usb strucutue
* @param in_direction -- direction
*/
extern void XHCICreateStatusTRB(XHCISlot* slot, bool in_direction);

/*
* XHCIEnableSlot -- sends enable slot command to xHC
* @param dev -- Pointer to usb device structure
* @param slot_type -- type of slot mainly in between 0-31
*/
extern void XHCIEnableSlot(XHCIDevice* dev, uint8_t slot_type);

/*
* XHCIDisableSlot -- sends disable slot command to xHC
* @param dev -- Pointer to usb device structure
* @param slot_num -- slot id to disable
*/
extern void XHCIDisableSlot(XHCIDevice* dev, uint8_t slot_num);

/* XHCISendNoopCmd -- Send No operation command
* @param dev -- Pointer to USB structure
*/
extern void XHCISendNoopCmd(XHCIDevice* dev);


#pragma pack(push,1)
struct USB_REQUEST_PACKET {
	uint8_t request_type;
	uint8_t request;
	uint16_t value;
	uint16_t index;
	uint16_t length;
};
#pragma pack(pop)


/*
* XHCISendControlCmd -- Sends control commands to USB Device's
* default control pipe
* @param dev -- pointer to usb device structure
* @param slot_id -- slot number
* @param request -- USB request packet structure
* @param buffer_addr -- input buffer address
* @param len -- length of the buffer
*/
extern void XHCISendControlCmd(XHCIDevice* dev, XHCISlot* slot, uint8_t slot_id, const USB_REQUEST_PACKET* request, uint64_t buffer_addr, const size_t len,
	uint8_t trt = CTL_TRANSFER_TRT_IN_DATA);


/*
 * XHCISendNormalTRB -- sends a normal trb
 */
extern void XHCISendNormalTRB(XHCIDevice* dev, XHCISlot* slot, uint64_t data_buffer, uint16_t data_len, XHCIEndpoint* ep);

/*
 * XHCIBulkTransfer -- Bulk transfer callback
 * @param dev -- Pointer to host device structure
 * @param slot -- Pointer to device slot
 * @param buffer -- Pointer to memory buffer
 * @param data_len -- total data length
 * @param ep_ -- Pointer to endpoint structure
 */
extern void XHCIBulkTransfer(XHCIDevice* dev, XHCISlot* slot, uint64_t buffer, uint16_t data_len, XHCIEndpoint* ep_);


/*
 * XHCIGetMaxPacketSize -- get the packe size by
 * looking USB speed
 * @param speed -- speed of USB port
 */
extern size_t XHCIGetMaxPacketSize(uint8_t speed);


#endif