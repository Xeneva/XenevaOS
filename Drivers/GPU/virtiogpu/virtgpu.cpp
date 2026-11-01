/**
* @file virtgpu.cpp
* 
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
#include <audrv.h>
#include <aucon.h>
#include <Drivers/uart.h>
#include <pcie.h>
#include <Drivers/virtio.h>
#include <Mm/vmmngr.h>
#include <Mm/pmmngr.h>
#include <Mm/kmalloc.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <string.h>
#include "virtiogpu.h"
#include "virtscreen.h"
#include <Hal/AA64/gic.h>
#include <Fs/vfs.h>
#include <Fs/Dev/devfs.h>
#include "cmd2d.h"
#include <aurora.h>

#define VIRTIO_PCI_CAP_COMMON_CFG 1
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2
#define VIRTIO_PCI_CAP_DEVICE_CFG 4

/** feature bits */
#define VIRTIO_GPU_F_VIRGL		   (1ULL << 0) //virgl mode is supported
#define VIRTIO_GPU_F_EDID		   (1ULL << 1) //EDID is supported
#define VIRTIO_GPU_F_RESOURCE_UUID (1ULL << 2) //assigning resources UUIDs
#define VIRTIO_GPU_F_RESOURCE_BLOB (1ULL << 3) //creating and using size-based blob
#define VIRTIO_GPU_F_CONTEXT_INIT  (1ULL << 4) //multiple context types and sync timelines
#define VIRTQ_DESC_F_NEXT		   1
#define VIRTQ_DESC_F_WRITE		   2

/** static internal usable variables */
static volatile uint8_t* notifyBase;
static uint32_t notifyOffMultiplier;
static bool _is_edid_supported;
static bool _is_virgl_supported;
static struct VirtioQueue* controlq;
static struct VirtioQueue* cursorq;
static uint64_t controlq_phys;
static uint64_t cursorq_phys;
static void* command_phys;
static void* resp_phys;
static uint64_t command_page_phys;
static int gpu_spi = -1;
static int controlq_sz;
static int cursorq_sz;
static int gpu_resource_id;
static int default_scr_rsrc_id;
/* read inside a spin-wait loop and written only from the IRQ handler --
 * without volatile the compiler has no reason to re-read it each iteration
 * at -O2 and can hoist the load out of the loop entirely, turning the wait
 * into a real infinite spin no matter what the interrupt does. Found this
 * by watching the boot hang silently right after cursorq init with no
 * "command timed out" print ever appearing. --axiss */
static volatile bool _resp_ok;
static uint16_t* notifyAddress;
static VirtioCommonCfg* _cfg;
AuVFSNode* fsnode;

void gpu_reset_device(VirtioCommonCfg* cfg);

/**                                                                                                                                                                                                                                                                          
    * gpu_find_cap -- look up a virtio-PCI capability of the requested type in                                                                                                                                                                                                  
    * the device's PCI Config space and report where its config struct lives.                                                                                                                                                                                                   
    *                                                                                                                                                                                                                                                                           
    * Background: type-0 PCI Config space is a 256-byte region. 0x00-0x3F is the                                                                                                                                                                                                
    * fixed header, and 0x40-0xFF holds a *linked list* of capability nodes:                                                                                                                                                                                                    
    *                                                                                                                                                                                                                                                                           
    *   byte 0   capability ID                                                                                                                                                                                                                                                  
    *   byte 1   offset of the NEXT node (0 terminates the list)                                                                                                                                                                                                                
    *   byte 2   length of this node                                                                                                                                                                                                                                            
    *   byte 3+  node-specific payload                                                                                                                                                                                                                                          
    *                                                                                                                                                                                                                                                                           
    * Because the position of a node is not fixed and varies from device to                                                                                                                                                                                                     
    * device, it is not possible to read the virtio config from a hardcoded                                                                                                                                                                                                     
    * offset. The read of 0x34 is the head pointer of the list, and the rest of                                                                                                                                                                                                 
    * the code is just walking node-by-node via the `next` field until one                                                                                                                                                                                                      
    * matches cfg_type.                                                                                                                                                                                                                                                         
    *                                                                                                                                                                                                                                                                           
    * The virtio (ID 0x09) payload we are looking for is:                                                                                                                                                                                                                       
    *                                                                                                                                                                                                                                                                           
    *   byte 3   cfg_type        -- which of the virtio capabilities this is                                                                                                                                                                                                    
    *   byte 4   bar             -- which BAR (0-5) the config struct lives in                                                                                                                                                                                                  
    *   byte 8   offset (u32)    -- byte offset within that BAR                                                                                                                                                                                                                 
    *   byte 12 length (u32)     -- size of the config struct                                                                                                                                                                                                                   
    *   byte 16 notify_off_multiplier (u32)                                                                                                                                                                                                                                     
    *           -- present only in the NOTIFICATION capability (whose cap_len is                                                                                                                                                                                                
    *              20, not 16), spacing in bytes between per-queue notify slots                                                                                                                                                                                                 
    *                                                                                                                                                                                                                                                                           
    * Returns true on success and fills the out_* fields (except the multiplier,                                                                                                                                                                                                
    * but the caller must have passed NULL). False means "this device does                                                                                                                                                                                                      
    * not satisfy us" -- not an unrecoverable state.                                                                                                                                                                                                                            
    */
static bool
gpu_find_cap(uint64_t device,
			 int bus,
			 int dev,
			 int func,
			 uint8_t cfg_type,
			 uint8_t* out_bar,
			 uint32_t* out_offset, // TODO: either supress the warning or pass a struct here
			 uint32_t* out_length,
			 uint32_t* out_notify_multiplier) {
	// Head pointer of the capability list. Per the PCI spec this is optional,
	// so 0x00 is a legal value meaning "this device exposes no capabilities."
	// That's not a situation to panic over -- returning false lets the
	// driver main refuse to attach to this device, and the system keeps
	// booting without a GPU.
	uint8_t ptr = (uint8_t)AuPCIERead64(device, PCI_CAPABILITIES_PTR, 1, bus, dev, func);
	if (ptr == NULL)
		return false;

	// Safety limit: 0x40-0xFF holds at most a small number of nodes, which
	// makes this bound a sane upper limit, and protects against a device
	// that hands us a circular list.
	constexpr int NUM_CAP = 48;

	// By spec, capability nodes are only allowed to live in the 0x40-0xFF
	// portion of the 256-byte region (the fixed header owns 0x00-0x3F).
	// (The old code also rejected odd offsets. The PCI spec does not require
	// node alignment, so we won't reject a legal device that just happens to
	// be weird.)
	constexpr int PCI_HEADER_START = 0x40;
	constexpr int PCI_HEADER_LIMIT = 0xFC;

	for (unsigned count = 0; ptr && count < NUM_CAP; count++) {
		if (ptr < PCI_HEADER_START || ptr > PCI_HEADER_LIMIT)
			return false;

		uint8_t id = (uint8_t)AuPCIERead64(device, ptr, 1, bus, dev, func);
		uint8_t next = (uint8_t)AuPCIERead64(device, ptr + 1, 1, bus, dev, func);

		// 0x09 = virtio-PCI capability. Careful: in the base PCI spec
		// 0x09 means "vendor specific", so other (non-virtio) capabilities
		// can legitimately carry ID 0x09.
		constexpr int VIRTIO_PCI_CAP_ID = 0x09;
		if (id != VIRTIO_PCI_CAP_ID) {
			ptr = next;
			continue;
		}

		uint8_t length = (uint8_t)AuPCIERead64(device, ptr + 2, 1, bus, dev, func);

		// Two sanity checks *before* reading the node's payload:
		//  - length < 16: even the smallest virtio capability has to
		//    carry the u32 offset (byte 8) and u32 length (byte 12)
		//    fields. A shorter node is malformed, and reading it would
		//    bleed into whatever sits next to it in Config space.
		//  - ptr + length > 0x100: everything must fit inside the 256-byte
		//    Config space. A bogus length must not push our payload reads
		//    past 0xFF.
		constexpr int VIRTIO_CAPABILITY_STRUCT_LEN = 16;
		constexpr int VIRTIO_CAPABILITY_MAX_LEN = 0x100;

		if (length < VIRTIO_CAPABILITY_STRUCT_LEN || ptr + length > VIRTIO_CAPABILITY_MAX_LEN) {
			ptr = next;
			continue;
		}

		uint8_t type = (uint8_t)AuPCIERead64(device, ptr + 3, 1, bus, dev, func);
		if (type != cfg_type) {
			ptr = next;
			continue;
		}

		// The requested capability was found. The remaining byte-4 defense
		// against a fake 0x09 (vendor-specific) node is that a real virtio
		// capability points at a real BAR, i.e. 0-5. Whatever gpu_map_cap
		// that can't be verified against a valid BAR range gets rejected
		// before the MMIO access.
		uint8_t bar = (uint8_t)AuPCIERead64(device, ptr + 4, 1, bus, dev, func);
		if (bar > 5) {
			ptr = next;
			continue;
		}

		uint32_t offset = (uint32_t)AuPCIERead64(device, ptr + 8, 4, bus, dev, func);
		uint32_t cfg_length = (uint32_t)AuPCIERead64(device, ptr + 12, 4, bus, dev, func);

		// The NOTIFICATION capability additionally puts
		// notify_off_multiplier at byte 16, which is why its cap_len is 20.
		// If the caller requested it (i.e. we were looking for a notification
		// capability), verify that it's actually present. If the node is
		// malformed, we can't give up on the whole device, so we just look
		// for the next node in the list.
		if (out_notify_multiplier) {
			if (length < 20) {
				ptr = next;
				continue;
			}
			*out_notify_multiplier = (uint32_t)AuPCIERead64(device, ptr + 16, 4, bus, dev, func);
		}

		*out_bar = bar;
		*out_offset = offset;
		*out_length = cfg_length;
		return true;
	}
	return false;
}

static void* gpu_map_cap(
	uint64_t device, int bus, int dev, int func, uint8_t bar, uint32_t offset, uint32_t length) {
	size_t bar_size = 0;
	uint64_t bar_base =
		AuPCIEReadBAR(device, (uint16_t)bus, (uint16_t)dev, (uint16_t)func, bar, &bar_size);
	if (!bar_base || !bar_size || !length || offset > bar_size || length > bar_size - offset)
		return NULL;
	uint64_t map_base = bar_base & ~(uint64_t)(PAGE_SIZE - 1);
	uint64_t delta = bar_base - map_base;
	uint64_t pages = (delta + offset + length + PAGE_SIZE - 1) / PAGE_SIZE;
	uint8_t* mapped = (uint8_t*)AuMapMMIO(map_base, (size_t)pages);
	return mapped ? mapped + delta + offset : NULL;
}
/* Opt in to runtime handoff to Process/gpusrv. The unload path below removes
 * every callback and VFS node that points into this image before the loader
 * tears the image down. */
AU_DRIVER_FLAGS(AU_DRV_FLAG_UNLOADABLE);

/*
 * AuDriverUnload -- detach the fallback driver from the Aurora system.
 */
AU_EXTERN AU_EXPORT int AuDriverUnload() {
	/* AuDriverLoad/AuDriverUnload uses the component refcount for ordinary
	 * opens. Keep this local guard as a second line of defense for shutdown
	 * paths that call the driver's hook directly. */
	if (fsnode && fsnode->fileCopyCount) {
		UARTDebugOut("[virtio-gpu]: unload blocked by %d open descriptor(s)\r\n",
					 (int)fsnode->fileCopyCount);
		return -1;
	}
	/* Stop callbacks before resetting the device. In particular, the console
	 * mirror hook is called from arbitrary kernel writers and must not survive
	 * the image unload. Restore the stable GOP framebuffer for /dev/graph; the
	 * user-space server will copy from it into its own DMA backing. */
	AuConsoleSetPresentHook(NULL);
	if (fsnode) {
		AuVFSNode* devfs = AuVFSFind("/dev");
		if (devfs)
			AuDevFSRemoveFile(devfs, "/virtiogpu");
		fsnode = NULL;
	}
	if (_cfg)
		gpu_reset_device(_cfg);
	virt_gpu_release_fb();
	if (controlq_phys) {
		AuFreePages((uint64_t)controlq, false, PAGE_SIZE);
		AuPmmngrReleasePage(controlq_phys);
	}
	if (cursorq_phys) {
		AuFreePages((uint64_t)cursorq, false, PAGE_SIZE);
		AuPmmngrReleasePage(cursorq_phys);
	}
	if (command_page_phys)
		AuPmmngrReleasePage(command_page_phys);
	if (gpu_spi >= 0) {
		GICClearSPIHandler(gpu_spi);
		AuGICDeallocateSPI(gpu_spi);
	}
	_cfg = NULL;
	controlq = NULL;
	cursorq = NULL;
	controlq_phys = 0;
	cursorq_phys = 0;
	command_phys = NULL;
	resp_phys = NULL;
	command_page_phys = 0;
	gpu_spi = -1;
	UARTDebugOut("[virtio-gpu]: reset completed \r\n");
	return 0;
}

/**
 * @brief gpu_reset_device -- reset the virtio gpu
 * device
 * @param cfg -- pointer to virtio common config
 */
void gpu_reset_device(VirtioCommonCfg* cfg) {
	/* Reset the device */
	cfg->DeviceStatus = 0;

	isb_flush();
	dsb_ish();

	cfg->DeviceStatus = VIRTIO_STATUS_ACKNOWLEDGE;
	isb_flush();
	dsb_ish();

	cfg->DeviceStatus |= VIRTIO_STATUS_DRIVER;
	isb_flush();
	dsb_ish();

	UARTDebugOut("[virtio-gpu]: reset completed successfully \r\n");
}

/**
 * @brief gpu_feature_negotiate -- check available
 * features of host device and enable only those
 * supported by the driver
 * @param cfg -- pointer to virtio common config
 */
static bool gpu_feature_negotiate(VirtioCommonCfg* cfg) {
	cfg->DevFeatureSelect = 0;
	isb_flush();
	dsb_ish();
	uint32_t features_lo = cfg->DevFeature;
	cfg->DevFeatureSelect = 1;
	isb_flush();
	dsb_ish();
	uint32_t feature_hi = cfg->DevFeature;
	uint64_t features = ((uint64_t)feature_hi << 32) | features_lo;

	/** examine the supported feature step by step*/
	if (features & VIRTIO_GPU_F_VIRGL) {
		UARTDebugOut("[virtio-gpu]: virgl is supported \r\n");
		_is_virgl_supported = true;
	}

	if (features & VIRTIO_GPU_F_EDID) {
		UARTDebugOut("[virtio-gpu]: edid blob is supported \r\n");
		_is_edid_supported = true;
	}

	/* This fallback implements the modern split-ring interface. Do not
	 * advertise optional GPU features we do not consume; VERSION_1 is the
	 * required bit when legacy transport is disabled. */
	uint64_t guestfeatures = features & (1ULL << VIRTIO_F_VERSION_1_BIT);

	cfg->GuestFeatureSelect = 0;
	cfg->GuestFeature = guestfeatures & UINT32_MAX;
	isb_flush();
	dsb_ish();

	cfg->GuestFeatureSelect = 1;
	cfg->GuestFeature = (guestfeatures >> 32) & UINT32_MAX;
	isb_flush();
	dsb_ish();

	cfg->DeviceStatus |= VIRTIO_STATUS_FEATURES_OK;
	isb_flush();
	dsb_ish();
	if (!(cfg->DeviceStatus & VIRTIO_STATUS_FEATURES_OK)) {
		UARTDebugOut("[virtio-gpu]: device rejected driver features\r\n");
		cfg->DeviceStatus |= VIRTIO_STATUS_FAILED;
		return false;
	}

	UARTDebugOut("[virtio-gpu]: features negotiation done \r\n");
	return true;
}

/**
 * @brief gpu_is_virgl_supported -- returns if
 * virgl is supported by host
 * @return true if supported, false if not supported
 */
bool gpu_is_virgl_supported() {
	return _is_virgl_supported;
}

/**
 * @brief gpu_is_edid_supported -- returns if
 * edid blob is supported by host
 * @return true if supported, false if not supported
 */
bool gpu_is_edid_supported() {
	return _is_edid_supported;
}

/**
 * gpu_initialize_controlq -- initialize the control 
 * queue
 * @param cfg - Pointer to virtio common config
 */
static bool gpu_initialize_controlq(VirtioCommonCfg* cfg) {
	cfg->QueueSelect = 0;
	isb_flush();
	dsb_ish();

	uint16_t queueSz = cfg->QueueSize;
	if (!queueSz || queueSz > 64) {
		UARTDebugOut("[virtio-gpu]: invalid control queue size : %d\r\n", queueSz);
		return false;
	}
	controlq_sz = queueSz;
	uint64_t queuePhys = (uint64_t)AuPmmngrAllocPage(
		AURORA_PAGE_NORMAL); //AuPmmngrAllocBlocks(((sizeof(struct VirtioQueue) * queueSz)) / 0x1000);
	if (!queuePhys || queuePhys == UINT64_MAX)
		return false;
	controlq = (struct VirtioQueue*)AuMapMMIO(queuePhys, 1);
	if (!controlq) {
		AuPmmngrReleasePage(queuePhys);
		return false;
	}
	controlq_phys = queuePhys;
	memset((void*)controlq, 0, 0x1000);
	UARTDebugOut("[virtio-gpu]: controlq size : %d \r\n", queueSz);
	cfg->QueueDesc = queuePhys;
	cfg->QueueAvail = (queuePhys) + OFFSETOF(struct VirtioQueue, available);
	cfg->QueueUsed = (queuePhys) + OFFSETOF(struct VirtioQueue, used);
	cfg->MSix = 0;
	cfg->QueueMSixVector = 0;
	cfg->QueueEnable = 1;
	isb_flush();
	dsb_ish();
	UARTDebugOut("[virtio-gpu]: controlq initialized \r\n");
	return true;
}

/**
 * @brief gpu_initialize_cursorq -- initialize the cursor queue
 * @param cfg -- Pointer to virtio common config
 */
static bool gpu_initialize_cursorq(VirtioCommonCfg* cfg) {
	cfg->QueueSelect = 1;
	isb_flush();
	dsb_ish();

	uint16_t queueSz = cfg->QueueSize;
	if (!queueSz || queueSz > 64) {
		UARTDebugOut("[virtio-gpu]: invalid cursor queue size : %d\r\n", queueSz);
		return false;
	}
	UARTDebugOut("[virtio-gpu]: cursorq size : %d \r\n", queueSz);
	cursorq_sz = queueSz;
	uint64_t queuePhys = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	if (!queuePhys || queuePhys == UINT64_MAX)
		return false;
	cursorq = (struct VirtioQueue*)AuMapMMIO(queuePhys, 1);
	if (!cursorq) {
		AuPmmngrReleasePage(queuePhys);
		return false;
	}
	cursorq_phys = queuePhys;
	memset((void*)cursorq, 0, 0x1000);
	UARTDebugOut("[virtio-gpu]: controlq size : %d \r\n", queueSz);
	cfg->QueueDesc = queuePhys;
	cfg->QueueAvail = (queuePhys) + OFFSETOF(struct VirtioQueue, available);
	cfg->QueueUsed = (queuePhys) + OFFSETOF(struct VirtioQueue, used);
	cfg->MSix = 0;
	cfg->QueueMSixVector = 0;
	cfg->QueueEnable = 1;
	isb_flush();
	dsb_ish();

	UARTDebugOut("[virtio-gpu]: cursorq initialized \r\n");
	return true;
}

/**
 * @brief gpu_notify_queue -- notify host that new command
 * is present
 * @param queueIdx -- queue number, zero for controlq and
 * one for cursorq
 */
void gpu_notify_queue(VirtioCommonCfg* cfg, uint16_t queueIdx) {
	cfg->QueueSelect = queueIdx;
	isb_flush();
	dsb_ish();
	uint16_t notify_off = cfg->QueueNotifyOff;
	volatile uint16_t* notifyAddr =
		(volatile uint16_t*)((uint64_t)notifyBase + notify_off * notifyOffMultiplier);
	*notifyAddr = queueIdx;
	isb_flush();
	dsb_ish();
}

/**
 * @brief gpu_execute_command -- submit a command to gpu
 * @param cfg -- pointer to virtio common config 
 * @param cmd -- Pointer to command struct
 * @param len -- total length of the command
 */
void gpu_execute_command(VirtioCommonCfg* cfg, void* cmd, size_t len) {
	int index = controlq->available.index % controlq_sz;
	memcpy(command_phys, cmd, len);

	controlq->buffers[index].Addr = (uint64_t)V2P((uint64_t)command_phys);
	controlq->buffers[index].Length = len;
	controlq->buffers[index].Flags = VIRTQ_DESC_F_NEXT;
	controlq->buffers[index].Next = (index + 1) % controlq_sz;

	virtio_gpu_ctrl_hdr* resp = (virtio_gpu_ctrl_hdr*)resp_phys;
	memset(resp, 0, sizeof(virtio_gpu_ctrl_hdr));

	controlq->buffers[(index + 1) % controlq_sz].Addr = (uint64_t)V2P((uint64_t)resp_phys);
	controlq->buffers[(index + 1) % controlq_sz].Length = sizeof(virtio_gpu_ctrl_hdr);
	controlq->buffers[(index + 1) % controlq_sz].Flags = VIRTQ_DESC_F_WRITE;

	uint16_t ringSlot = controlq->available.index % controlq_sz;
	controlq->available.ring[ringSlot] = index;
	isb_flush();
	dsb_ish();

	controlq->available.index++;
	isb_flush();
	dsb_ish();

	gpu_notify_queue(cfg, 0);

	/* was waiting on the _resp_ok flag the IRQ handler sets, but the
	 * virtio-gpu SPI apparently never actually reaches the CPU on this
	 * board/config (same commented-out GICSetTargetCPU() as every other
	 * driver here, so it's not obviously that) -- every single command was
	 * burning the full spin budget before giving up, at ~70ms/frame. The
	 * device itself completes commands promptly (confirmed: resp->type
	 * reliably reads back a real VIRTIO_GPU_RESP_OK_NODATA), so poll the
	 * response buffer directly instead of depending on the interrupt --
	 * invalidate + check each spin, same cache-coherency requirement as
	 * the IRQ path had. --axiss */
	uint32_t spin = 2000000;
	dc_ivac((uint64_t)resp_phys);
	while (resp->type == 0 && --spin) {
		dc_ivac((uint64_t)resp_phys);
	}
	if (resp->type == 0)
		UARTDebugOut("[virtio-gpu]: command timed out waiting for response, type=%x\r\n",
					 resp->type);

	memset(command_phys, 0, PAGE_SIZE);
	_resp_ok = false;
}

/**
 * @brief gpu_attach_back_cmd -- attach backing stroe command needs two descriptor
 * previous gpu_execute_command won't satisfy this
 * @param cfg -- pointer to virtio common config
 * @param req -- pointer to request command one
 * @param len1 -- request command one length
 * @param req2 -- pointer to request command two
 * @param len2 -- request command two length
 */
void gpu_attach_back_cmd(
	VirtioCommonCfg* cfg, void* req, uint32_t len1, void* req2, uint32_t len2) {
	uint16_t idx = controlq->available.index % controlq_sz;
	uint16_t idx1 = (uint16_t)((idx + 1) % controlq_sz);
	uint16_t idx2 = (uint16_t)((idx + 2) % controlq_sz);

	memcpy(command_phys, req, len1);
	memcpy((void*)((uint64_t)command_phys + len1), req2, len2);

	controlq->buffers[idx].Addr = (uint64_t)V2P((uint64_t)command_phys);
	controlq->buffers[idx].Length = len1;
	controlq->buffers[idx].Flags = VIRTQ_DESC_F_NEXT;
	controlq->buffers[idx].Next = idx1;

	controlq->buffers[idx1].Addr = (uint64_t)V2P((uint64_t)command_phys) + len1;
	controlq->buffers[idx1].Length = len2;
	controlq->buffers[idx1].Flags = VIRTQ_DESC_F_NEXT;
	controlq->buffers[idx1].Next = idx2;

	virtio_gpu_ctrl_hdr* resp = (virtio_gpu_ctrl_hdr*)resp_phys;
	memset(resp, 0, sizeof(virtio_gpu_ctrl_hdr));

	controlq->buffers[idx2].Addr = (uint64_t)V2P((uint64_t)resp_phys);
	controlq->buffers[idx2].Length = sizeof(virtio_gpu_ctrl_hdr);
	controlq->buffers[idx2].Flags = VIRTQ_DESC_F_WRITE;

	controlq->available.ring[idx % controlq_sz] = idx;
	isb_flush();
	dsb_ish();

	controlq->available.index++;
	isb_flush();
	dsb_ish();

	gpu_notify_queue(cfg, 0);

	/* was waiting on the IRQ-set _resp_ok flag, same as gpu_execute_command --
	 * poll the response buffer directly instead, see the comment there. --axiss */
	uint32_t spin = 2000000;
	dc_ivac((uint64_t)resp_phys);
	while (resp->type == 0 && --spin) {
		dc_ivac((uint64_t)resp_phys);
	}
	if (resp->type == 0)
		UARTDebugOut("[virtio-gpu]: attach backing command timed out waiting for response\r\n");

	memset(command_phys, 0, PAGE_SIZE);
	_resp_ok = false;
}

/**
 * @brief gpu_allocate_resource_id -- allocates
 * resource for commands
 * @return newly allocated resource id
 */
int gpu_allocate_resource_id() {
	int id = gpu_resource_id;
	gpu_resource_id += 1;
	return id;
}

/**
 * @brief Virtio-keyboard interrupt handler
 */
void gpu_virt_interrupt(int spinum) {
	/** shoud read the status register **/
	/** but skipping it for now **/
	/* the device DMA-writes this response; without invalidating our cache
	 * line first we keep reading back the zeroed buffer gpu_execute_command
	 * memset before submitting, so every command "times out" even though
	 * the IRQ fires -- same class of bug already fixed for virtio-tablet's
	 * ring reads. Must use the plain dc_ivac() here, not
	 * aa64_dc_ivac_range() -- the range helper calls AA64SleepUS(100),
	 * which busy-waits via a _wfi() loop for the counter to advance. This
	 * function runs in IRQ context (called from GICCallSPIHandler), and
	 * calling a WFI-based delay from inside an interrupt handler deadlocks
	 * the core waiting for a wake event that can't arrive at this priority
	 * -- found by GDB-sampling the stuck PC and landing exactly on that
	 * _wfi(). dc_ivac() itself only needed AU_EXPORT added in
	 * aa64lowlevel.h to be callable from a driver DLL. --axiss */
	dc_ivac((uint64_t)resp_phys);
	dsb_sy_barrier();
	virtio_gpu_ctrl_hdr* resp = (virtio_gpu_ctrl_hdr*)resp_phys;
	switch (resp->type) {
	case VIRTIO_GPU_RESP_OK_NODATA:
		_resp_ok = true;
		break;
	case VIRTIO_GPU_RESP_OK_DISPLAY_INFO:
		UARTDebugOut("[virtio-gpu]: interrupt edid display info changed \r\n");
		break;
	default:
		//UARTDebugOut("[virtio-gpu]: unknown response type : %x \r\n", resp->type);
		break;
	}
}

/**
 * @brief virtio_gpu_iocontrol -- io control for virtio gpu
 * @param file -- Pointer to gpu file struct
 * @param code -- code number
 * @param arg -- data struct passed to this driver
 */
int virtio_gpu_iocontrol(AuVFSNode* file, int code, void* arg) {
	AuFileIOControl* ioctl = (AuFileIOControl*)arg;
	switch (code) {
	case VIRTIO_GPU_CREATE_RESOURCE_2D: {
		int resourceID = ioctl->uint_1;
		uint32_t width = ioctl->ulong_1 & UINT32_MAX;
		uint32_t height = ioctl->ulong_2 & UINT32_MAX;
		virtio_cmd2d_create(resourceID, width, height);
		UARTDebugOut("[virtio-gpu-ioctl]: 2d resource created \r\n");
		break;
	}
	case VIRTIO_GPU_ATTACH_BACKING:
		break;
	case VIRTIO_GPU_TRANSFER_TO_HOST2D: {
		int resourceID = ioctl->uint_1;
		int x = ioctl->ushort_1 & UINT32_MAX;
		int y = ioctl->ushort_2 & UINT32_MAX;
		int w = ioctl->ulong_1 & UINT32_MAX;
		int h = ioctl->ulong_2 & UINT32_MAX;
		virt_gpu_transfer_to_host2d(_cfg, resourceID, x, y, w, h);
		virt_gpu_flush_rect(_cfg, resourceID, x, y, w, h);
		break;
	}
	case VIRTIO_GPU_FLUSH: {
		int resourceID = ioctl->uint_1;
		int x = ioctl->ushort_1 & UINT32_MAX;
		int y = ioctl->ushort_2 & UINT32_MAX;
		int w = ioctl->ulong_1 & UINT32_MAX;
		int h = ioctl->ulong_2 & UINT32_MAX;
		virt_gpu_flush_rect(_cfg, resourceID, x, y, w, h);
		break;
	}
	case VIRTIO_GPU_GET_SCREEN_RSRC_ID:
		return default_scr_rsrc_id;
	}
	return 0;
}

/*
* AuDriverMain -- Main entry for virtio gpu driver
*/
AU_EXTERN AU_EXPORT int AuDriverMain(AuDriver* drv) {
	UARTDebugOut("[virtio-gpu]: hello from inside virtio gpu driver \n");
	/* the driver manager already matched us onto this device by
	 * class/subclass (audrv.cnf) and did the PCI scan -- reuse its
	 * result instead of scanning again --axiss */
	int bus = drv->bus;
	int dev = drv->dev;
	int func = drv->func;
	uint64_t device = drv->device;

	/* gpusrv deliberately disables decoding and bus mastering before handing
	 * the function back. Restore them before touching modern virtio PCI BARs. */
	uint16_t command = (uint16_t)AuPCIERead(device, PCI_COMMAND, bus, dev, func);
	command |= 0x7; /* I/O space, memory space, bus master */
	AuPCIEWrite(device, PCI_COMMAND, command, bus, dev, func);
	isb_flush();
	dsb_ish();

	_is_edid_supported = false;
	_is_virgl_supported = false;
	_resp_ok = false;
	gpu_resource_id = 1;
	uint64_t command_page = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	if (!command_page || command_page == UINT64_MAX) {
		UARTDebugOut("[virtio-gpu]: command buffer allocation failed\r\n");
		return -1;
	}
	command_phys = (void*)P2V(command_page);
	command_page_phys = command_page;
	resp_phys = (void*)((uint64_t)command_phys + 2048);
	memset(command_phys, 0, PAGE_SIZE);

	uint8_t common_bar = 0, notify_bar = 0;
	uint32_t common_offset = 0, common_length = 0;
	uint32_t notify_offset = 0, notify_length = 0;
	if (!gpu_find_cap(device,
					  bus,
					  dev,
					  func,
					  VIRTIO_PCI_CAP_COMMON_CFG,
					  &common_bar,
					  &common_offset,
					  &common_length,
					  NULL) ||
		!gpu_find_cap(device,
					  bus,
					  dev,
					  func,
					  VIRTIO_PCI_CAP_NOTIFY_CFG,
					  &notify_bar,
					  &notify_offset,
					  &notify_length,
					  &notifyOffMultiplier) ||
		!notifyOffMultiplier) {
		UARTDebugOut("[virtio-gpu]: required PCI capabilities missing\r\n");
		return -1;
	}
	VirtioCommonCfg* cfg = (VirtioCommonCfg*)gpu_map_cap(
		device, bus, dev, func, common_bar, common_offset, common_length);
	notifyBase = (volatile uint8_t*)gpu_map_cap(
		device, bus, dev, func, notify_bar, notify_offset, notify_length);
	if (!cfg || !notifyBase) {
		UARTDebugOut("[virtio-gpu]: failed to map required PCI capabilities\r\n");
		return -1;
	}
	UARTDebugOut("[virtio-gpu]: NumQueue : %d \r\n", cfg->Queues);

	_cfg = cfg;
	/** reset the device first **/
	gpu_reset_device(cfg);

	/** negotiate features **/
	if (!gpu_feature_negotiate(cfg))
		return -1;

	int spiID = AuGICAllocateSPI();
	UARTDebugOut("[virtio-gpu]: spi id: %d \n", spiID);
	gpu_spi = spiID;
	if (AuPCIEAllocMSI(device, spiID, bus, dev, func)) {
		UARTDebugOut("[virtio-gpu]: msi/msi-x allocated \r\n");
	}
	GICEnableSPIIRQ(spiID);
	//GICSetTargetCPU(spiID);
	isb_flush();
	dsb_ish();

	GICRegisterSPIHandler(&gpu_virt_interrupt, spiID);

	/** initialize both queues **/
	if (!gpu_initialize_controlq(cfg) || !gpu_initialize_cursorq(cfg)) {
		UARTDebugOut("[virtio-gpu]: queue initialization failed\r\n");
		cfg->DeviceStatus |= VIRTIO_STATUS_FAILED;
		return -1;
	}
	cfg->DeviceStatus |= VIRTIO_STATUS_DRIVER_OK;
	isb_flush();
	dsb_ish();

	enable_irqs();

	/** initialize the screen data and start scanout 0.
	 * Size the GPU resource to the firmware GOP mode. A hardcoded
	 * 1024x768 resource with a 1920-wide compositor stride is what
	 * produced the repeating vertical wallpaper strips on every
	 * mode except 1024x768. --axiss */
	KERNEL_BOOT_INFO* binfo = AuGetBootInfoStruc();
	if (binfo && (uint64_t)binfo < KERNEL_BASE_ADDRESS) {
		UARTDebugOut("[virtio-gpu]: ignoring invalid low boot-info pointer %x\r\n",
					 (uint64_t)binfo);
		binfo = NULL;
	}
	uint32_t gpu_w = 1024;
	uint32_t gpu_h = 768;
	if (binfo && binfo->X_Resolution && binfo->Y_Resolution) {
		gpu_w = binfo->X_Resolution;
		gpu_h = binfo->Y_Resolution;
	}
	/* A manual loader menu entry (resolutions the firmware GOP never
	 * offers, e.g. 1920x1080) travels separately from the real GOP
	 * geometry so the early console stays valid. Prefer it, validated,
	 * so the desktop scanout follows the chosen size. --axiss */
	if (binfo && binfo->DesktopOverrideWidth >= 640 && binfo->DesktopOverrideWidth <= 4096 &&
		binfo->DesktopOverrideHeight >= 480 && binfo->DesktopOverrideHeight <= 4096) {
		gpu_w = binfo->DesktopOverrideWidth;
		gpu_h = binfo->DesktopOverrideHeight;
		UARTDebugOut("[virtio-gpu]: using desktop override %d x %d\r\n", gpu_w, gpu_h);
	}
	UARTDebugOut("[virtio-gpu]: creating scanout %d x %d\r\n", gpu_w, gpu_h);
	int resource_id = virt_gpu_screen_init(cfg, gpu_w, gpu_h);
	default_scr_rsrc_id = resource_id;
	virt_gpu_alloc_fb(cfg, resource_id);
	virt_gpu_set_scanout(cfg, resource_id, 0);
	virt_gpu_fill_screen(gpu_w, gpu_h, 0xFF000000);
	virt_gpu_transfer_to_host2d(cfg, resource_id, 0, 0, gpu_w, gpu_h);
	virt_gpu_flush(cfg, resource_id);
	/* NOTE: the console-mirror hookup lives kernel-side (audrv resolves
	 * VirtGpuConsolePresent from our export table). Calling the setter
	 * from here faults: driver->kernel imports resolve only through the
	 * k_exports allowlist, which has no console entry. --axiss */
	mask_irqs();

	AuVFSNode* devfs = AuVFSFind("/dev");
	if (!devfs)
		return 0;

	/* avoiding using pipe for latency issue */
	AuVFSNode* node = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(node, 0, sizeof(AuVFSNode));
	strcpy(node->filename, "virtiogpu");
	node->flags |= FS_FLAG_DEVICE;
	node->device = cfg;
	node->read = 0;	 // AuDevInputMiceRead;
	node->write = 0; // AuDevInputMiceWrite;
	node->open = 0;
	node->close = 0;
	node->iocontrol = virtio_gpu_iocontrol; // AuDevMouseIoControl;
	fsnode = node;
	AuDevFSAddFile(devfs, "/", node);
	UARTDebugOut("[virtio_gpu]: registered to device file system \r\n");
	return 0;
}

/**
 * @brief gpu_get_config_pointer -- return the pointer
 * virtio common config descriptor from pcie
 * @return system config descriptor from pcie config space
 */
VirtioCommonCfg* gpu_get_config_pointer() {
	return _cfg;
}

int virt_gpu_default_resource_id() {
	return default_scr_rsrc_id;
}
