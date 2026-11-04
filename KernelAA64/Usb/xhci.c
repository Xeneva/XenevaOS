/**
 * BSD 2-Clause License
 *
 * Copyright (c) 2022-2026, Manas Kamal Choudhury
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
#include <stdbool.h>
extern bool AuIsPCIeInitialized(void);
#include <pcie.h>
#include <aucon.h>
#include <string.h>
#include <_null.h>
#include <Mm/pmmngr.h>
#include <Mm/vmmngr.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Hal/AA64/aa64cpu.h>
#include <Hal/AA64/gic.h>
#include <Hal/AA64/sched.h>
#include <Drivers/uart.h>
#include <Fs/Dev/devinput.h>
#include <Fs/Dev/devfs.h>
#include <Log/_print.h>
#include <Cred/group.h>
#include <Fs/vdisk.h>
#include <Mm/kmalloc.h>

#define RING_N 256
#define TRB_LINK 6
#define TRB_ENABLE_SLOT 9
#define TRB_DISABLE_SLOT 10
#define TRB_ADDR_DEV 11
#define TRB_CFG_EP 12
#define TRB_SETUP 2
#define TRB_DATA 3
#define TRB_STATUS 4
#define TRB_NORMAL 1
#define TRB_ISOCH 5
#define TRB_EVAL_CTX 13
#define TRB_RESET_EP 14
#define TRB_STOP_EP 15
#define TRB_SET_DEQ 16
#define TRB_EVENT_XFER 32
#define TRB_EVENT_CMD 33

/* CRCR bits: run/stop (RCS), command stop (CS) and command abort (CA). */
#define CRCR_RCS (1u << 0)
#define CRCR_CA (1u << 2)

/* Completion codes reported for a stopped command ring. */
#define TRB_CC_CMD_RING_STOPPED 24
#define TRB_CC_CMD_ABORTED 25

/* One entry per slot the controller can report (HCS1 caps the count at 32
 * in bringup()), so a full rack of root-port devices cannot exhaust the
 * table before the controller runs out of slots. */
#define XHCI_MAX_DEV 32
/* HCSPARAMS1 reports the root port count in eight bits.  The slot table
 * still limits how many of them can be enumerated at once. */
#define XHCI_MAX_PORTS 255

/* PORTSC bits used by the port-change scan. The change bits are RW1C, so
 * acknowledging a port means writing back just the bits read as set. */
#define PORTSC_CCS (1u << 0)
#define PORTSC_PED (1u << 1)
#define PORTSC_PR (1u << 4)
#define PORTSC_PP (1u << 9)
#define PORTSC_CSC (1u << 17)
#define PORTSC_PEC (1u << 18)
#define PORTSC_WRC (1u << 19)
#define PORTSC_OCC (1u << 20)
#define PORTSC_PRC (1u << 21)
#define PORTSC_PLC (1u << 22)
#define PORTSC_CEC (1u << 23)
#define PORTSC_CHANGE                                                                              \
	(PORTSC_CSC | PORTSC_PEC | PORTSC_WRC | PORTSC_OCC | PORTSC_PRC | PORTSC_PLC | PORTSC_CEC)

/* USB 2.0 hub class requests and feature selectors.  A hub that hangs off
 * a root port is a normal device with class 9; its downstream ports are
 * driven with these class-specific control transfers. */
#define HUB_CLASS 9
#define HUB_DESC_TYPE 0x29
#define HUB_MAX_PORTS 255
#define HUB_MAX_TIER 5
#define HUB_PORT_CONNECTION 0
#define HUB_PORT_ENABLE 1
#define HUB_PORT_RESET 4
#define HUB_PORT_POWER 8
/* wPortChange uses bits 0..4 of the second status word.  The values for
 * CLEAR_PORT_FEATURE are separate selectors (16, 17, 19, 20). */
#define HUB_CHANGE_CONNECTION 0
#define HUB_CHANGE_ENABLE 1
#define HUB_CHANGE_OVERCURRENT 3
#define HUB_CHANGE_RESET 4
#define HUB_CLEAR_ENABLE 17
#define HUB_CLEAR_OVERCURRENT 19
#define HUB_CLEAR_RESET 20
#define HUB_CLEAR_CONNECTION 16

/* A device slot is only handed out from XDEV_FREE. Enumeration owns the
 * slot until it is READY; a port thread disconnect moves READY -> CLOSING
 * and waits for the HID reader before releasing the DMA pages. */
#define XDEV_FREE 0
#define XDEV_ENUM 1
#define XDEV_READY 2
#define XDEV_CLOSING 3

/* A parsed HID input field.  HID descriptors describe each report as a
 * sequence of fields: a usage page, a usage (or range/list), a bit width
 * and a count.  Keeping the fields is what lets the driver decode report
 * IDs and non-boot layouts instead of assuming the boot keyboard/mouse
 * layout. */
#define HID_MAX_FIELDS 24
#define HID_MAX_USAGES 8
#define HID_GLOBAL_STACK 4
/* A composite HID device can expose several interfaces, each with its own
 * interrupt endpoint and report descriptor.  Four covers the keyboard,
 * pointer, consumer-control and vendor layouts seen on real hardware
 * without making the per-slot device table large. */
#define XHCI_MAX_HID 4

#define HID_FIELD_CONSTANT 1
#define HID_FIELD_VARIABLE 2
#define HID_FIELD_RELATIVE 4
#define HID_FIELD_ARRAY 8
/* Largest input report the poll thread keeps room for.  Reports can exceed
 * one endpoint packet, so the transfer length comes from the descriptor
 * rather than from wMaxPacketSize. */
#define HID_REPORT_MAX 256
/* HCI event packets are a two-byte header plus at most a 255-byte
 * payload; the endpoint packet size is usually 16. */
#define BT_HCI_EVENT_MAX 257

typedef struct {
	uint32_t report_id;
	uint32_t usage_page;
	uint32_t usage_min;
	uint32_t usage_max;
	uint32_t usages[HID_MAX_USAGES];
	uint8_t nusages;
	uint8_t has_range;
	uint8_t flags;
	uint32_t bit_offset;
	uint32_t report_size;
	uint32_t report_count;
	int32_t logical_min;
	int32_t logical_max;
} HidField;

typedef struct {
	int valid;
	int kind; /* 1 keyboard, 2 pointer */
	int has_ids; /* input reports carry a leading report ID byte */
	int nfields;
	HidField fields[HID_MAX_FIELDS];
	uint32_t in_len; /* largest input report, including any ID byte */

	/* LED output report, when the descriptor describes one.  led_offset is
	 * a bit offset in the report body, after any report ID byte. */
	int led_valid;
	uint32_t led_report_id;
	uint32_t led_offset;
	uint32_t led_count;
} HidReport;

typedef struct {
	uint32_t w0, w1, w2, w3;
} Trb;

/* USB mass storage state.  The vdisk keeps this structure as its private
 * data, so it has to outlive a single plug: the table is indexed by device
 * slot and a re-plugged disk reuses its entry and its registered vdisk. */
struct MscDisk;

typedef struct {
	Trb* v;
	uint64_t phys;
	uint32_t enq;
	uint8_t cycle;
	uint8_t dci;
	uint8_t stream; /* UAS stream ID, zero for a normal ring */
} Ring;

/* One HID interface: its own interrupt ring, report layout and keyboard
 * state.  Pointer coordinates stay on UsbDev because a device has one
 * pointer regardless of how many HID interfaces it exposes. */
typedef struct {
	int kind; /* 1 keyboard, 2 pointer */
	uint8_t iface;
	uint8_t ep_addr;
	Ring intr;
	uint8_t* buf;
	uint64_t buf_phys;
	uint16_t mps;
	uint16_t xfer_len; /* bytes queued per interrupt transfer */
	int armed;
	uint64_t trb;
	HidReport rep;
	uint8_t last_mods;
	uint8_t last_keys[6];
	uint8_t led_state;
} HidPipe;

typedef struct {
	int state;
	int refs;
	uint8_t slot;
	uint8_t port; /* root port this device descends from */
	uint8_t speed;
	uint16_t vid;
	uint16_t pid;

	/* Topology.  parent is an index+1 into devices (0 = root port) and
	 * route is the xHCI route string, four bits per hub tier. */
	uint8_t parent;
	uint8_t parent_port;
	uint8_t tier;
	uint8_t is_hub;
	uint8_t hub_ports;
	uint32_t route;

	Ring ep0;
	uint8_t* in_ctx;
	uint64_t in_phys;
	uint8_t* out_ctx;
	uint64_t out_phys;
	uint8_t* xfer;
	uint64_t xfer_phys;

	/* String descriptors, decoded to ASCII for logs and future naming. */
	char manufacturer[32];
	char product[32];
	char serial[32];

	/* Interrupt IN pipe used by BT HCI events and CDC notifications.
	 * HID interfaces own separate pipes in hids[] below. */
	Ring intr;
	uint8_t* intr_buf;
	uint64_t intr_phys;
	uint16_t intr_mps;
	uint16_t intr_len; /* bytes queued per interrupt transfer */
	uint8_t intr_interval;
	int intr_armed;
	uint64_t intr_trb;

	/* HID interfaces, if any. */
	HidPipe hids[XHCI_MAX_HID];
	int nhids;
	int32_t pointer_x;
	int32_t pointer_y;
	uint8_t pointer_valid;

	/* Bluetooth adapter, if any. */
	int bt;
	Ring bout;
	Ring bin;
	Ring iout;
	uint8_t* bin_buf;
	uint64_t bin_phys;
	uint16_t bin_mps;
	uint16_t bin_len;
	int bin_armed;
	uint64_t bin_trb;
	int have_iso;

	/* Mass storage, if any. */
	struct MscDisk* msc;

	/* Serial: 1 CDC ACM, 2 FTDI vendor class. */
	int serial_kind;
	uint8_t serial_iface;
} UsbDev;

/* UAS exposes four bulk pipes: command, status, data-in and data-out.  On
 * SuperSpeed the latter three use streams, one stream per command tag, so
 * the status and data transfers can be matched to their command without a
 * separate ready phase. */
#define UAS_TAGS 4
#define UAS_MAXPSTREAMS 2 /* 2^(n+1) = 8 stream context entries */

#define UAS_UI_COMMAND 0x01
#define UAS_UI_SENSE 0x03
#define UAS_UI_READ_READY 0x06
#define UAS_UI_WRITE_READY 0x07

#define UAS_EP_STATUS 0
#define UAS_EP_IN 1
#define UAS_EP_OUT 2
#define UAS_NEP 3

#define XHCI_MAX_EXTRA_LUN 16

typedef struct MscDisk {
	UsbDev* dev;
	AuVDisk* vd;
	uint8_t iface;
	uint8_t in_addr;
	uint8_t out_addr;
	uint8_t lun;
	uint8_t owner; /* device index + 1, zero while the slot is unused */
	uint32_t block_size;
	uint64_t max_lba;
	uint32_t tag;
	int uas;
	int streams;
	struct MscDisk* xport; /* shared command/status/data rings, NULL on the primary */
	Ring cmd;
	Ring status;
	Ring din;
	Ring dout;
	uint8_t* iu_buf; /* command IU page, separate from the data-out page */
	uint64_t iu_phys;
	uint64_t sca_phys[UAS_NEP];
	Ring sring[UAS_NEP][UAS_TAGS];
} MscDisk;

static MscDisk msc_disks[XHCI_MAX_DEV];
static MscDisk msc_extra[XHCI_MAX_EXTRA_LUN];
static AuVFSNode* serial_nodes[XHCI_MAX_DEV];

static volatile uint32_t* mmio;
static uint32_t caplen;
static uint32_t dboff;
static uint32_t rtsoff;
static uint32_t ctx_sz;
static uint32_t nports;
static uint32_t max_psa; /* HCCPARAMS1 MaxPSASize: 2^(n+1) primary stream entries */
static uint64_t dma_ceiling; /* zero when the controller addresses all of RAM */
static uint32_t page_bytes = PAGE_SIZE; /* scratchpad buffer page size */
static uint32_t mmio_bytes;
static int bus, devn, func;
static uint64_t pci;
static Ring cmdring;
static Trb* evt;
static uint64_t evt_phys;
static uint32_t evt_deq;
static uint8_t evt_cycle;
static uint64_t* dcbaa;
static uint64_t dcbaa_phys;
static Trb pend[64];
static int pend_n;
static UsbDev devices[XHCI_MAX_DEV];
static int hid_count;
static int bt_count;
static int xhci_initialized;
static int polling_started;
static AuBtUsbOps bt_ops;
static int bt_ops_valid;

/* Format a device's path as root-port[.hub-port...].  The controller port
 * number is the first component; each non-zero route-string nibble adds the
 * downstream hub port for that tier.  This is log evidence only: the route
 * string itself stays the source of truth for addressing. */
static void usb_port_path(const UsbDev* d, char* out, size_t size) {
	int n;
	int tier;

	if (!out || !size)
		return;
	n = _snprintf(out, size, "%d", (int)d->port);
	if (n < 0 || (size_t)n >= size) {
		out[0] = 0;
		return;
	}
	for (tier = 0; tier < HUB_MAX_TIER; tier++) {
		uint32_t port = (d->route >> (4 * tier)) & 0xfu;
		if (!port)
			break;
		n += _snprintf(out + n, size - (size_t)n, ".%d", (int)port);
		if (n < 0 || (size_t)n >= size)
			break;
	}
}

static void usb_child_path(const UsbDev* hub, int port, char* out, size_t size) {
	char base[24];
	usb_port_path(hub, base, sizeof base);
	_snprintf(out, size, "%s.%d", base, port);
}

/* The BT thread and the HID poll thread both reach the command/event rings.
 * Every multi-TRB sequence takes this lock for its whole lifetime so a
 * completion can never be consumed by the other caller.
 *
 * The sections only poll the event ring and never need the controller IRQ,
 * so interrupts stay masked for the whole hold. Without that, the timer can
 * preempt a kernel-mode holder and this scheduler only switches on return
 * to EL0: a user thread that then spun on usb_busy at EL1 would keep the
 * holder from ever running. Masking makes the lock invisible to any other
 * thread on this single-core target. */
static volatile int usb_busy;
static uint64_t usb_daif;

static void usb_lock(void) {
	uint64_t daif = read_daif();
	mask_irqs();
	while (__atomic_exchange_n(&usb_busy, 1, __ATOMIC_ACQUIRE))
		;
	usb_daif = daif;
}

static void usb_unlock(void) {
	__atomic_store_n(&usb_busy, 0, __ATOMIC_RELEASE);
	restore_daif(usb_daif);
}

static void spin_ms(uint32_t ms) {
	uint64_t freq = get_cntfrq_el0();
	uint64_t ticks = (freq / 1000ull) * ms;
	uint64_t start = get_cntpct_el0();
	if (ticks == 0)
		ticks = 1;
	while ((get_cntpct_el0() - start) < ticks)
		;
}

static inline uint32_t rd(uint32_t off) {
	return mmio[off / 4];
}
static inline void wr(uint32_t off, uint32_t v) {
	mmio[off / 4] = v;
	dsb_ish();
}

static void inv(void* p, size_t n) {
	aa64_dc_ivac_range(p, n);
}

static void clean(void* p, size_t n) {
	aa64_data_cache_clean_range(p, n);
}

static void* dma_pages(uint32_t count, uint64_t* phys) {
	uint64_t p = AuPmmngrAllocPages(count, 1, dma_ceiling, AURORA_PAGE_DMA);
	void* v;
	if (!p)
		return NULL;
	v = (void*)P2V(p);
	memset(v, 0, (size_t)count * PAGE_SIZE);
	clean(v, (size_t)count * PAGE_SIZE);
	*phys = p;
	return v;
}

static void* dma_page(uint64_t* phys) {
	return dma_pages(1, phys);
}

/* HCCPARAMS2 splits the scratchpad count across two five-bit fields: the
 * high five bits at 25:21 and the low five at 31:27. */
static uint32_t scratchpad_count(uint32_t hcc2) {
	return (((hcc2 >> 21) & 0x1fu) << 5) | ((hcc2 >> 27) & 0x1fu);
}

/* PAGESIZE holds the controller's page size in 4 KiB units; one bit must
 * be set.  A malformed value falls back to 4 KiB, and 128 KiB is the
 * architecture's maximum. */
static uint32_t controller_page_bytes(uint32_t ps) {
	uint32_t v = ps & 0xffffu;
	if (!v || (v & (v - 1)) || v > 32)
		return PAGE_SIZE;
	return v << 12;
}

/* Some controllers require scratchpad buffers before they accept commands.
 * DCBAAP[0] points at an array of page physical addresses, so allocate the
 * array first, then one controller-sized buffer per entry.  QEMU asks for
 * none, in which case none of this runs. */
static int scratchpad_setup(uint32_t count) {
	uint64_t bytes = (uint64_t)count * sizeof(uint64_t);
	uint32_t pages = (uint32_t)((bytes + PAGE_SIZE - 1) >> PAGE_SHIFT);
	uint32_t per = page_bytes / PAGE_SIZE;
	uint64_t arr_phys;
	uint64_t* arr;
	uint32_t i;

	UARTDebugOut("[xhci]: %d scratchpad buffers\r\n", (int)count);
	if (!count)
		return 0;
	arr = (uint64_t*)dma_pages(pages, &arr_phys);
	if (!arr)
		return -1;
	for (i = 0; i < count; i++) {
		uint64_t phys;
		if (!dma_pages(per, &phys))
			return -1;
		arr[i] = phys;
	}
	clean(arr, (size_t)pages * PAGE_SIZE);
	dcbaa[0] = arr_phys;
	clean(dcbaa, PAGE_SIZE);
	return 0;
}

static uint64_t ring_push(Ring* r, uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3) {
	if (r->enq == RING_N - 1) {
		Trb* t = &r->v[r->enq];
		t->w0 = (uint32_t)r->phys;
		t->w1 = (uint32_t)(r->phys >> 32);
		t->w2 = 0;
		t->w3 = (TRB_LINK << 10) | (1 << 1) | r->cycle;
		clean(t, sizeof *t);
		r->enq = 0;
		r->cycle ^= 1;
	}
	{
		Trb* t = &r->v[r->enq];
		uint64_t phys = r->phys + (uint64_t)r->enq * 16;
		t->w0 = w0;
		t->w1 = w1;
		t->w2 = w2;
		dsb_ish();
		t->w3 = (w3 & ~1u) | r->cycle;
		clean(t, sizeof *t);
		r->enq++;
		return phys;
	}
}

static void door(uint32_t slot, uint32_t target) {
	volatile uint32_t* db = (volatile uint32_t*)((uint8_t*)mmio + dboff);
	db[slot] = target;
	dsb_ish();
}

/* A stream doorbell carries the stream ID in bits 31:16 and the target
 * endpoint in bits 7:0. */
static void door_stream(uint32_t slot, uint32_t dci, uint32_t stream_id) {
	volatile uint32_t* db = (volatile uint32_t*)((uint8_t*)mmio + dboff);
	db[slot] = (dci & 0xffu) | (stream_id << 16);
	dsb_ish();
}

static void ring_door(uint32_t slot, const Ring* r) {
	if (r->stream)
		door_stream(slot, r->dci, r->stream);
	else
		door(slot, r->dci);
}

static void erdp_advance(void) {
	uint64_t addr = evt_phys + (uint64_t)evt_deq * 16;
	uint32_t iman_base = rtsoff;
	wr(iman_base + 0x38, (uint32_t)addr | (1u << 3));
	wr(iman_base + 0x3c, (uint32_t)(addr >> 32));
}

static int event_pop(Trb* out) {
	Trb* t = &evt[evt_deq];
	inv(t, sizeof *t);
	if ((t->w3 & 1u) != evt_cycle)
		return 0;
	*out = *t;
	evt_deq++;
	if (evt_deq == RING_N) {
		evt_deq = 0;
		evt_cycle ^= 1;
	}
	erdp_advance();
	return 1;
}

static void stash(Trb ev) {
	if (pend_n < (int)(sizeof pend / sizeof pend[0]))
		pend[pend_n++] = ev;
}

/* Command completions are matched by TRB address.  Before the command ring
 * is rebuilt, drop the ones still queued so a late event for an abandoned
 * TRB cannot answer a new command at the same address. */
static void pend_drop_commands(void) {
	int i, j = 0;
	for (i = 0; i < pend_n; i++) {
		if (((pend[i].w3 >> 10) & 0x3f) != TRB_EVENT_CMD)
			pend[j++] = pend[i];
	}
	pend_n = j;
}

/* Port status changes are read from PORTSC directly; the change bit stays
 * set until software clears it, so the event ring needs no second consumer. */
static int take_match(int want_cmd, uint64_t trb_phys, uint8_t slot, uint8_t dci, Trb* out) {
	int i;
	for (i = 0; i < pend_n; i++) {
		uint32_t type = (pend[i].w3 >> 10) & 0x3f;
		uint64_t ptr = ((uint64_t)pend[i].w0 | ((uint64_t)pend[i].w1 << 32)) & ~0xfull;
		uint8_t ev_slot = (uint8_t)((pend[i].w3 >> 24) & 0xff);
		uint8_t ep = (uint8_t)((pend[i].w3 >> 16) & 0x1f);
		int hit = 0;
		if (want_cmd) {
			if (type == TRB_EVENT_CMD)
				hit = trb_phys ? (ptr == (trb_phys & ~0xfull)) : 1;
		} else if (type != TRB_EVENT_XFER) {
			hit = 0;
		} else if (slot && ev_slot != slot) {
			hit = 0;
		} else if (trb_phys) {
			hit = ptr == (trb_phys & ~0xfull);
		} else {
			hit = ep == dci;
		}
		if (!hit)
			continue;
		*out = pend[i];
		{
			int j;
			for (j = i; j < pend_n - 1; j++)
				pend[j] = pend[j + 1];
		}
		pend_n--;
		return 1;
	}
	return 0;
}

static void drain(void) {
	Trb ev;
	while (event_pop(&ev)) {
		uint32_t type = (ev.w3 >> 10) & 0x3f;
		if (type == TRB_EVENT_XFER || type == TRB_EVENT_CMD)
			stash(ev);
	}
}

/* After a command times out the controller may still own the command ring.
 * Ask it to abort, drop completions for TRBs that are about to become valid
 * again, and restart from the ring base.  Every command is synchronous, so
 * this is the only way the enqueue point can disagree with the controller;
 * growing the ring would not help.  Caller holds usb_lock. */
static void cmd_ring_abort(void) {
	Trb ev;
	int spins, stopped = 0;

	wr(caplen + 0x18, (uint32_t)(cmdring.phys | CRCR_CA | CRCR_RCS));
	wr(caplen + 0x1c, (uint32_t)(cmdring.phys >> 32));
	for (spins = 0; !stopped && spins < 200; spins++) {
		drain();
		while (take_match(1, 0, 0, 0, &ev)) {
			int code = (int)((ev.w2 >> 24) & 0xff);
			if (code == TRB_CC_CMD_RING_STOPPED ||
				code == TRB_CC_CMD_ABORTED) {
				stopped = 1;
				break;
			}
		}
		if (!stopped)
			spin_ms(1);
	}
	pend_drop_commands();
	memset(cmdring.v, 0, PAGE_SIZE);
	clean(cmdring.v, PAGE_SIZE);
	cmdring.enq = 0;
	cmdring.cycle = 1;
	wr(caplen + 0x18, (uint32_t)(cmdring.phys | CRCR_RCS));
	wr(caplen + 0x1c, (uint32_t)(cmdring.phys >> 32));
	if (stopped)
		UARTDebugOut("[xhci]: command ring restarted\r\n");
	else
		UARTDebugOut("[xhci]: command ring reset without stop event\r\n");
}

static int wait_cmd(uint64_t trb_phys, uint8_t* slot_out, int* code) {
	int spins = 0;
	while (spins++ < 500) {
		Trb ev;
		drain();
		if (take_match(1, trb_phys, 0, 0, &ev)) {
			if (slot_out)
				*slot_out = (uint8_t)((ev.w3 >> 24) & 0xff);
			if (code)
				*code = (int)((ev.w2 >> 24) & 0xff);
			return 0;
		}
		spin_ms(1);
	}
	UARTDebugOut("[xhci]: command timed out\r\n");
	cmd_ring_abort();
	return -1;
}

static int wait_xfer_ev(uint64_t trb_phys, uint8_t slot, Trb* out) {
	int spins = 0;
	while (spins++ < 500) {
		Trb ev;
		drain();
		if (take_match(0, trb_phys, slot, 0, &ev)) {
			int c = (int)((ev.w2 >> 24) & 0xff);
			if (c == 1 || c == 13) {
				if (out)
					*out = ev;
				return 0;
			}
			UARTDebugOut("[xhci]: transfer completion %d\r\n", c);
			return -1;
		}
		spin_ms(1);
	}
	UARTDebugOut("[xhci]: transfer timed out\r\n");
	return -1;
}

static int wait_xfer(uint64_t trb_phys, uint8_t slot) {
	return wait_xfer_ev(trb_phys, slot, NULL);
}

/* Endpoint command completion code, or -1 if the command never completed.
 * Caller holds usb_lock. */
static int ep_cmd_locked(const UsbDev* d, uint8_t dci, uint32_t type) {
	uint64_t trb;
	int code = 0;
	trb = ring_push(&cmdring, 0, 0, 0,
					(type << 10) | ((uint32_t)dci << 16) | ((uint32_t)d->slot << 24));
	door(0, 0);
	if (wait_cmd(trb, NULL, &code))
		return -1;
	return code;
}

/* Rebuild an endpoint's ring after a halt, a failed transfer or a timeout.
 * Stop first so a transfer still in flight cannot write the ring while it is
 * being cleared; Reset clears the halt a failed transfer leaves behind (it
 * reports Context State Error when Stop already left the endpoint Stopped,
 * which is fine).  Set TR Dequeue then points the endpoint at the fresh ring
 * and the next doorbell restarts it.  Caller holds usb_lock. */
static int ep_recover_locked(UsbDev* d, Ring* ring) {
	uint64_t trb;
	int code;
	if (!d->slot || !ring->v || !ring->dci)
		return -1;
	UARTDebugOut("[xhci]: recover slot %d dci %d\r\n", d->slot, ring->dci);
	code = ep_cmd_locked(d, ring->dci, TRB_STOP_EP);
	if (code != 1)
		UARTDebugOut("[xhci]: stop endpoint completion %d\r\n", code);
	code = ep_cmd_locked(d, ring->dci, TRB_RESET_EP);
	if (code != 1)
		UARTDebugOut("[xhci]: reset endpoint completion %d\r\n", code);

	/* Completions for the abandoned TRBs would otherwise match the rebuilt
	 * ring, whose TRBs sit at the same physical addresses. */
	pend_n = 0;
	memset(ring->v, 0, PAGE_SIZE);
	clean(ring->v, PAGE_SIZE);
	ring->enq = 0;
	ring->cycle = 1;

	code = -1;
	{
		/* For a stream, Set TR Dequeue takes the stream ID in w2 and the
		 * stream context type in the dequeue pointer, which is the only
		 * place a stream ring is addressed. */
		uint32_t lo = (uint32_t)(ring->phys | 1);
		uint32_t w2 = 0;
		if (ring->stream) {
			lo |= 1u << 1; /* SCT_PRI_TR */
			w2 = (uint32_t)ring->stream << 16;
		}
		trb = ring_push(&cmdring, lo, (uint32_t)(ring->phys >> 32), w2,
						(TRB_SET_DEQ << 10) | ((uint32_t)ring->dci << 16) |
							((uint32_t)d->slot << 24));
	}
	door(0, 0);
	if (wait_cmd(trb, NULL, &code) || code != 1) {
		UARTDebugOut("[xhci]: set dequeue completion %d\r\n", code);
		return -1;
	}
	if (ring == &d->intr)
		d->intr_armed = 0;
	for (int i = 0; i < XHCI_MAX_HID; i++) {
		if (ring == &d->hids[i].intr)
			d->hids[i].armed = 0;
	}
	if (ring == &d->bin) {
		d->bin_armed = 0;
		d->bin_len = 0;
	}
	return 0;
}

/* Endpoint State from the output device context: 0 disabled, 1 running,
 * 2 halted, 3 stopped, 4 error.  The controller owns this memory, so the
 * cache line is invalidated before the read. */
static uint32_t ep_state_locked(UsbDev* d, uint8_t dci) {
	uint8_t* ctx;
	if (!d->out_ctx || !dci)
		return 0;
	ctx = d->out_ctx + (uint32_t)ctx_sz * dci;
	inv(ctx, ctx_sz);
	return *(uint32_t*)ctx & 7u;
}

/* Run a control transfer.  Caller holds usb_lock.  A descriptor set can be
 * larger than one page, and the data stage TRB addresses physical memory
 * directly, so a large transfer needs its own contiguous DMA block. */
static int ctrl_locked(UsbDev* d, uint8_t bm, uint8_t req, uint16_t val, uint16_t idx,
					   uint8_t* data, uint16_t len, int data_in) {
	uint32_t trt = len ? (data_in ? 3 : 2) : 0;
	uint64_t status_phys;
	uint64_t xfer_phys;
	uint32_t setup0 = (uint32_t)bm | ((uint32_t)req << 8) | ((uint32_t)val << 16);
	uint32_t setup1 = (uint32_t)idx | ((uint32_t)len << 16);
	uint8_t* xfer;
	uint8_t* large = NULL;
	int rc;
	if (!d)
		return -1;
	if (d->state != XDEV_ENUM && d->state != XDEV_READY)
		return -1;
	xfer = d->xfer;
	xfer_phys = d->xfer_phys;
	if (len > PAGE_SIZE) {
		uint32_t pages = ((uint32_t)len + PAGE_SIZE - 1) >> PAGE_SHIFT;
		large = (uint8_t*)dma_pages(pages, &xfer_phys);
		if (!large)
			return -1;
		xfer = large;
	}
	if (!data_in && len && data) {
		memcpy(xfer, data, len);
		clean(xfer, len);
	}
	ring_push(&d->ep0, setup0, setup1, 8, (trt << 16) | (1 << 6) | (TRB_SETUP << 10));
	if (len) {
		ring_push(&d->ep0, (uint32_t)xfer_phys, (uint32_t)(xfer_phys >> 32), len,
				  (data_in ? (1u << 16) : 0) | (TRB_DATA << 10));
	}
	status_phys = ring_push(&d->ep0, 0, 0, 0,
							((len && data_in) ? 0 : (1u << 16)) | (1u << 5) | (TRB_STATUS << 10));
	door(d->slot, 1);
	rc = wait_xfer(status_phys, d->slot);
	if (!rc && data_in && len && data) {
		inv(xfer, len);
		memcpy(data, xfer, len);
	}
	if (large) {
		if (!AuPmmngrReleasePages(xfer_phys))
			AuPmmngrReleasePage(xfer_phys);
	}
	if (rc && d->slot)
		ep_recover_locked(d, &d->ep0);
	return rc ? -1 : 0;
}

static int ctrl(UsbDev* d, uint8_t bm, uint8_t req, uint16_t val, uint16_t idx, uint8_t* data,
				uint16_t len, int data_in) {
	int rc;
	usb_lock();
	rc = ctrl_locked(d, bm, req, val, idx, data, len, data_in);
	usb_unlock();
	return rc;
}

/* Read one UTF-16LE string descriptor and keep its printable ASCII subset.
 * Devices may answer with a short first packet, so the length is fetched
 * before the body.  A failed string is not fatal: the descriptor index is
 * optional and enumeration continues with an empty field. */
static int usb_string_desc(UsbDev* d, uint8_t index, uint16_t lang, char* out, int cap) {
	uint8_t buf[64];
	int len, i, n;

	if (!out || cap < 1)
		return -1;
	out[0] = 0;
	if (!index)
		return -1;
	memset(buf, 0, sizeof buf);
	if (ctrl(d, 0x80, 6, (uint16_t)(0x0300u | index), lang, buf, 2, 1))
		return -1;
	if (buf[0] < 2 || buf[1] != 3)
		return -1;
	len = buf[0] > sizeof buf ? (int)sizeof buf : (int)buf[0];
	if (ctrl(d, 0x80, 6, (uint16_t)(0x0300u | index), lang, buf, (uint16_t)len, 1))
		return -1;
	n = (len - 2) / 2;
	if (n >= cap)
		n = cap - 1;
	for (i = 0; i < n; i++) {
		uint16_t ch = (uint16_t)(buf[2 + i * 2] | (buf[3 + i * 2] << 8));
		out[i] = (ch >= 0x20 && ch < 0x7f) ? (char)ch : '?';
	}
	while (n > 0 && out[n - 1] == ' ')
		n--;
	out[n] = 0;
	return 0;
}

/* String zero carries the language IDs.  English (US) is the fallback for
 * devices that answer the language request with a stall. */
static uint16_t usb_lang_id(UsbDev* d) {
	uint8_t buf[4];
	memset(buf, 0, sizeof buf);
	if (ctrl(d, 0x80, 6, 0x0300, 0, buf, sizeof buf, 1))
		return 0x0409;
	if (buf[0] < 4 || buf[1] != 3)
		return 0x0409;
	return (uint16_t)(buf[2] | (buf[3] << 8));
}

static void usb_read_strings(UsbDev* d, const uint8_t* devdesc) {
	uint16_t lang = 0x0409;

	if (devdesc[14] || devdesc[15] || devdesc[16])
		lang = usb_lang_id(d);
	usb_string_desc(d, devdesc[14], lang, d->manufacturer, sizeof d->manufacturer);
	usb_string_desc(d, devdesc[15], lang, d->product, sizeof d->product);
	usb_string_desc(d, devdesc[16], lang, d->serial, sizeof d->serial);
	if (d->manufacturer[0])
		UARTDebugOut("[xhci]: manufacturer %s\r\n", d->manufacturer);
	if (d->product[0])
		UARTDebugOut("[xhci]: product %s\r\n", d->product);
}

typedef struct {
	uint8_t burst;
	uint8_t mult;
	uint16_t bytes;
} EpComp;

static void ep_init(uint8_t* ctx, Ring* ring, uint32_t type, uint16_t mps, uint32_t interval,
					uint32_t avg, const EpComp* comp) {
	uint32_t* e = (uint32_t*)ctx;
	uint32_t burst = comp ? comp->burst : 0;
	uint32_t esit = comp ? comp->bytes : 0;
	uint32_t is_iso = (type == 1 || type == 5);
	/* Isochronous endpoints must not retry (CErr = 0). */
	uint32_t cerr = is_iso ? 0u : 3u;

	memset(ctx, 0, ctx_sz);
	e[0] = (interval & 0xff) << 16;
	if (is_iso) {
		/* Mult and the high byte of Max ESIT Payload share the dword. */
		if (comp)
			e[0] |= (uint32_t)(comp->mult & 3) << 8;
		e[0] |= ((esit >> 16) & 0xffu) << 24;
	}
	e[1] = (cerr << 1) | (type << 3) | ((burst & 0xffu) << 8) | ((uint32_t)mps << 16);
	e[2] = (uint32_t)(ring->phys | ring->cycle);
	e[3] = (uint32_t)(ring->phys >> 32);
	e[4] = avg;
	if (is_iso)
		e[4] |= (esit & 0xffffu) << 16;
}

/* A bulk endpoint that uses streams points its endpoint context at the
 * stream context array instead of a single ring, with MaxPStreams and LSA
 * set.  The array entries, not this context, carry the per-stream rings. */
static void ep_init_streams(uint8_t* ctx, uint32_t type, uint16_t mps, uint64_t sca,
							const EpComp* comp) {
	uint32_t* e = (uint32_t*)ctx;
	uint32_t burst = comp ? comp->burst : 0;

	memset(ctx, 0, ctx_sz);
	e[0] = (UAS_MAXPSTREAMS << 10) | (1u << 15);
	e[1] = (3u << 1) | (type << 3) | ((burst & 0xffu) << 8) | ((uint32_t)mps << 16);
	e[2] = (uint32_t)sca; /* DCS clear: this is an array, not a ring */
	e[3] = (uint32_t)(sca >> 32);
	e[4] = mps;
}

static uint16_t ep0_mps_for_speed(int speed) {
	/* PORTSC speed: 1 full, 2 low, 3 high, 4 SuperSpeed. The initial
	 * default control pipe uses 8 bytes on USB2 low/full speed. */
	if (speed == 4)
		return 512;
	if (speed == 3)
		return 64;
	return 8;
}

static int address_at(UsbDev* d) {
	uint16_t mps = ep0_mps_for_speed(d->speed);
	uint32_t* icc;
	uint8_t* slotc;
	uint32_t* s;
	int code = 0;
	uint64_t cmd;

	memset(d->in_ctx, 0, PAGE_SIZE);
	memset(d->out_ctx, 0, PAGE_SIZE);
	d->ep0.v = (Trb*)dma_page(&d->ep0.phys);
	if (!d->ep0.v)
		return -1;
	d->ep0.enq = 0;
	d->ep0.cycle = 1;

	icc = (uint32_t*)d->in_ctx;
	slotc = d->in_ctx + 32; /* Input Control Context is always 32B */
	s = (uint32_t*)slotc;
	icc[1] = (1u << 0) | (1u << 1);
	/* Route string nibble 0 is the first hub port below the root port.
	 * Context Entries stays 1 until Configure Endpoint adds the class
	 * endpoints.  Bit 26 (Hub) is not known yet: the device descriptor
	 * is read after Address Device, and QEMU does not consult it. */
	s[0] = (d->route & 0xfffffu) | ((uint32_t)d->speed << 20) | (1u << 27);
	s[1] = ((uint32_t)d->port << 16);
	/* Full/low-speed devices behind a high-speed hub use that hub's
	 * transaction translator.  TT Port Number is the hub port the device
	 * is attached to, TT Hub Slot ID is the hub's slot. */
	if ((d->speed == 1 || d->speed == 2) && d->parent) {
		UsbDev* p = &devices[d->parent - 1];
		if (p->speed == 3)
			s[2] = (uint32_t)p->slot | ((uint32_t)d->parent_port << 8);
	}
	/* EP0 context follows the slot context. */
	ep_init(d->in_ctx + 32 + ctx_sz, &d->ep0, 4, mps, 0, 8, NULL);
	clean(d->in_ctx, PAGE_SIZE);
	clean(d->out_ctx, PAGE_SIZE);
	dcbaa[d->slot] = d->out_phys;
	clean(dcbaa, PAGE_SIZE);

	UARTDebugOut("[xhci]: address slot=%d port=%d route=%x speed=%d mps=%d\r\n",
				 d->slot, d->port, (unsigned)d->route, d->speed, mps);
	usb_lock();
	cmd = ring_push(&cmdring, (uint32_t)d->in_phys, (uint32_t)(d->in_phys >> 32), 0,
			  (TRB_ADDR_DEV << 10) | ((uint32_t)d->slot << 24));
	door(0, 0);
	if (wait_cmd(cmd, NULL, &code) || code != 1) {
		usb_unlock();
		UARTDebugOut("[xhci]: address device completion %d\r\n", code);
		return -1;
	}
	usb_unlock();
	return 0;
}

/* A full-speed device may report a default control pipe larger than the 8
 * bytes used for the first descriptor read.  Copy its live EP0 context into
 * the input context, change the packet size, and evaluate it so the longer
 * descriptor reads are scheduled with the real size. */
static int ep0_set_mps(UsbDev* d, uint16_t mps) {
	uint32_t* icc = (uint32_t*)d->in_ctx;
	uint32_t* in_ep = (uint32_t*)(d->in_ctx + 32 + ctx_sz);
	uint32_t* out_ep = (uint32_t*)(d->out_ctx + 32 + ctx_sz);
	uint64_t cmd;
	int code = 0;

	if (mps == ep0_mps_for_speed(d->speed))
		return 0;
	inv(out_ep, ctx_sz);
	memcpy(in_ep, out_ep, ctx_sz);
	in_ep[0] &= ~7u; /* EP state must be cleared for Evaluate Context */
	in_ep[1] = (in_ep[1] & ~(0xffffu << 16)) | ((uint32_t)mps << 16);
	icc[0] = 0;
	icc[1] = 1u << 1; /* add only the EP0 context */
	clean(d->in_ctx, PAGE_SIZE);
	usb_lock();
	cmd = ring_push(&cmdring, (uint32_t)d->in_phys, (uint32_t)(d->in_phys >> 32), 0,
			  (TRB_EVAL_CTX << 10) | ((uint32_t)d->slot << 24));
	door(0, 0);
	if (wait_cmd(cmd, NULL, &code) || code != 1) {
		usb_unlock();
		UARTDebugOut("[xhci]: evaluate ep0 %d completion %d\r\n", mps, code);
		return -1;
	}
	usb_unlock();
	UARTDebugOut("[xhci]: ep0 max packet %d\r\n", mps);
	return 0;
}

static int configure_eps(UsbDev* d, uint32_t add, uint32_t entries) {
	uint32_t* icc = (uint32_t*)d->in_ctx;
	uint32_t* s = (uint32_t*)(d->in_ctx + 32);
	int code = 0;
	uint64_t cmd;
	icc[0] = 0;
	icc[1] = add | 1u;
	s[0] = (s[0] & ~(0x1fu << 27)) | (entries << 27);
	clean(d->in_ctx, PAGE_SIZE);
	usb_lock();
	cmd = ring_push(&cmdring, (uint32_t)d->in_phys, (uint32_t)(d->in_phys >> 32), 0,
			  (TRB_CFG_EP << 10) | ((uint32_t)d->slot << 24));
	door(0, 0);
	if (wait_cmd(cmd, NULL, &code) || code != 1) {
		usb_unlock();
		UARTDebugOut("[xhci]: configure endpoint completion %d\r\n", code);
		return -1;
	}
	usb_unlock();
	return 0;
}

static int disable_slot_locked(UsbDev* d) {
	uint64_t cmd;
	int code = 0;
	if (!d->slot)
		return 0;
	cmd = ring_push(&cmdring, 0, 0, 0,
					(TRB_DISABLE_SLOT << 10) | ((uint32_t)d->slot << 24));
	door(0, 0);
	(void)wait_cmd(cmd, NULL, &code);
	dcbaa[d->slot] = 0;
	clean(dcbaa, PAGE_SIZE);
	d->slot = 0;
	return 0;
}

static void dma_release(uint64_t* phys) {
	if (*phys) {
		AuPmmngrReleasePage(*phys);
		*phys = 0;
	}
}

/* UAS stream context arrays and per-tag rings live in the MscDisk, which
 * outlives a single plug so its vdisk can be reused.  A disconnect has to
 * hand those pages back before the next claim reuses the entry. */
static void msc_release_uas(MscDisk* m) {
	int e, t;
	if (!m->uas)
		return;
	for (e = 0; e < UAS_NEP; e++) {
		for (t = 0; t < UAS_TAGS; t++)
			dma_release(&m->sring[e][t].phys);
		dma_release(&m->sca_phys[e]);
	}
	dma_release(&m->cmd.phys);
	dma_release(&m->status.phys);
	dma_release(&m->din.phys);
	dma_release(&m->dout.phys);
	dma_release(&m->iu_phys);
	m->iu_buf = NULL;
	m->uas = 0;
	m->streams = 0;
}

/* Release every page a device owns. Called with usb_lock held so no
 * transfer can be halfway through a ring while the backing page goes back
 * to the allocator. */
static void dev_release_locked(UsbDev* d) {
	int nhids = d->nhids;
	int was_bt = d->bt;
	int idx = (int)(d - devices);
	int i;
	if (d->serial_kind && serial_nodes[idx])
		serial_nodes[idx]->device = NULL;
	disable_slot_locked(d);
	dma_release(&d->ep0.phys);
	dma_release(&d->in_phys);
	dma_release(&d->out_phys);
	dma_release(&d->xfer_phys);
	dma_release(&d->intr.phys);
	dma_release(&d->intr_phys);
	for (i = 0; i < XHCI_MAX_HID; i++) {
		dma_release(&d->hids[i].intr.phys);
		dma_release(&d->hids[i].buf_phys);
	}
	dma_release(&d->bout.phys);
	dma_release(&d->bin.phys);
	dma_release(&d->bin_phys);
	dma_release(&d->iout.phys);
	if (nhids) {
		hid_count -= nhids;
		if (hid_count < 0)
			hid_count = 0;
	}
	if (was_bt && bt_count > 0)
		bt_count--;
	if (d->msc) {
		msc_release_uas(d->msc);
		d->msc->dev = NULL;
	}
	for (i = 0; i < XHCI_MAX_EXTRA_LUN; i++)
		if (msc_extra[i].dev == d)
			msc_extra[i].dev = NULL;
	memset(d, 0, sizeof *d);
	d->state = XDEV_FREE;
}

/* Enumeration never publishes a slot until it is READY, so a failure can
 * release the partial device without waiting for readers. */
static void enum_abort(UsbDev* d) {
	usb_lock();
	dev_release_locked(d);
	usb_unlock();
}

static int port_reset(int port) {
	uint32_t base = caplen + 0x400 + (uint32_t)(port - 1) * 16;
	uint32_t sc, w;
	int i;
	sc = rd(base);
	/* An unattached port still burns the whole 200 ms reset poll below, and
	 * the scan calls us for every port on every pass. CCS stays clear
	 * until something connects, so bail before touching the port at all. */
	if ((sc & 1u) == 0)
		return 0;
	w = sc & ~((1u << 1) | (1u << 4));
	w &= ~((1u << 17) | (1u << 18) | (1u << 19) | (1u << 20) | (1u << 21) | (1u << 22) | (1u << 23));
	w |= (1u << 9) | (1u << 4);
	wr(base, w);
	for (i = 0; i < 200; i++) {
		sc = rd(base);
		if (sc & (1u << 21))
			break;
		spin_ms(1);
	}
	wr(base, (sc & ~((1u << 1) | (1u << 4))) | (1u << 21) | (1u << 9));
	spin_ms(2);
	sc = rd(base);
	if ((sc & 1u) == 0)
		return 0;
	return (sc & (1u << 1)) ? 1 : 0;
}

static int speed_of(int port) {
	uint32_t base = caplen + 0x400 + (uint32_t)(port - 1) * 16;
	return (int)((rd(base) >> 10) & 0xf);
}

/* Arm one interrupt IN pipe for len bytes.  Ring, buffer and state are
 * passed in so HID interfaces, BT HCI events, hub status changes and CDC
 * notifications share the same transfer machinery.  wMaxPacketSize only
 * bounds each packet; a report or event may span several. */
static int arm_pipe_locked(UsbDev* d, Ring* ring, uint8_t* buf, uint64_t buf_phys,
						   uint16_t len, int* armed, uint64_t* trb) {
	uint64_t phys;
	uint16_t n;
	if (!ring->v || !buf)
		return -1;
	n = len ? len : 64;
	if (n > PAGE_SIZE)
		n = PAGE_SIZE;
	memset(buf, 0, n);
	clean(buf, n);
	phys = ring_push(ring, (uint32_t)buf_phys, (uint32_t)(buf_phys >> 32), n,
					 (1u << 5) | (TRB_NORMAL << 10));
	*trb = phys;
	ring_door(d->slot, ring);
	*armed = 1;
	return 0;
}

static int arm_intr_locked(UsbDev* d) {
	static int armed_logged;
	uint16_t len = d->intr_len ? d->intr_len : d->intr_mps;
	if (d->intr.v && !armed_logged) {
		armed_logged = 1;
		UARTDebugOut("[xhci]: intr armed slot %d dci %d len %d\r\n", d->slot,
					 d->intr.dci, len ? len : 64);
	}
	return arm_pipe_locked(d, &d->intr, d->intr_buf, d->intr_phys, len,
						   &d->intr_armed, &d->intr_trb);
}

static int arm_intr(UsbDev* d) {
	int rc;
	usb_lock();
	rc = arm_intr_locked(d);
	usb_unlock();
	return rc;
}

static int arm_hid(UsbDev* d, HidPipe* p) {
	int rc;
	uint16_t len = p->xfer_len ? p->xfer_len : p->mps;
	usb_lock();
	rc = arm_pipe_locked(d, &p->intr, p->buf, p->buf_phys, len, &p->armed,
						 &p->trb);
	usb_unlock();
	return rc;
}

static int pipe_xfer(UsbDev* d, Ring* ring, uint8_t* buf, uint64_t buf_phys,
					 uint16_t len, int* armed, uint64_t* trb, uint8_t* data,
					 uint16_t cap) {
	Trb ev;
	uint16_t n;
	uint32_t cc, residual;
	if (!d || !ring->v)
		return 0;
	usb_lock();
	if (d->state != XDEV_READY) {
		usb_unlock();
		return 0;
	}
	if (!*armed && arm_pipe_locked(d, ring, buf, buf_phys, len, armed, trb)) {
		usb_unlock();
		return 0;
	}
	drain();
	if (!take_match(0, *trb, d->slot, ring->dci, &ev)) {
		uint32_t state = ep_state_locked(d, ring->dci);
		if (state == 2 || state == 4) {
			UARTDebugOut("[xhci]: intr endpoint state %d slot %d dci %d\r\n",
						 state, d->slot, ring->dci);
			*armed = 0;
			if (ep_recover_locked(d, ring) == 0)
				arm_pipe_locked(d, ring, buf, buf_phys, len, armed, trb);
		}
		usb_unlock();
		return 0;
	}
	cc = (ev.w2 >> 24) & 0xff;
	residual = ev.w2 & 0xffffff;
	*armed = 0;
	if (cc != 1 && cc != 13) {
		UARTDebugOut("[xhci]: intr completion %d slot %d dci %d\r\n",
					 cc, d->slot, ring->dci);
		if (ep_recover_locked(d, ring) == 0)
			arm_pipe_locked(d, ring, buf, buf_phys, len, armed, trb);
		usb_unlock();
		return 0;
	}
	n = len ? len : 64;
	if (n > PAGE_SIZE)
		n = PAGE_SIZE;
	if (residual <= n)
		n = (uint16_t)(n - residual);
	else
		n = 0;
	if (n && data) {
		if (n > cap)
			n = cap;
		inv(buf, n);
		memcpy(data, buf, n);
	} else {
		n = 0;
	}
	arm_pipe_locked(d, ring, buf, buf_phys, len, armed, trb);
	usb_unlock();
	return (int)n;
}

static int intr_xfer(UsbDev* d, uint8_t* data, uint16_t cap) {
	uint16_t len = d->intr_len ? d->intr_len : d->intr_mps;
	return pipe_xfer(d, &d->intr, d->intr_buf, d->intr_phys, len,
					 &d->intr_armed, &d->intr_trb, data, cap);
}

static int hid_pipe_xfer(UsbDev* d, HidPipe* p, uint8_t* data, uint16_t cap) {
	uint16_t len = p->xfer_len ? p->xfer_len : p->mps;
	return pipe_xfer(d, &p->intr, p->buf, p->buf_phys, len, &p->armed,
					 &p->trb, data, cap);
}

/* Queue one bulk IN transfer for exactly n bytes.  A short packet ends the
 * transfer early, so the caller learns the real length from the residual in
 * the completion event.  Nothing is re-armed here: BT polls and wants one
 * HCI packet per call, while mass storage arms a data phase and its 13-byte
 * status packet with different lengths. */
static int arm_bin_locked(UsbDev* d, uint16_t n) {
	uint64_t phys;
	if (!d->bin.v || !d->bin_buf || !n || n > PAGE_SIZE || d->bin_armed)
		return -1;
	memset(d->bin_buf, 0, n);
	clean(d->bin_buf, n);
	phys = ring_push(&d->bin, (uint32_t)d->bin_phys, (uint32_t)(d->bin_phys >> 32), n,
					 (1u << 5) | (TRB_NORMAL << 10));
	d->bin_trb = phys;
	d->bin_len = n;
	ring_door(d->slot, &d->bin);
	d->bin_armed = 1;
	return 0;
}

/* Copy an OUT payload into the device's shared transfer page and run the
 * transfer while still holding usb_lock.  The copy cannot be split from the
 * ring push: another caller would otherwise overwrite the page between the
 * two, and the first transfer would go out with the second caller's data.
 * Isochronous rings take an Isoch TRB with SIA set: the transfer starts in
 * the next available frame instead of a caller-supplied frame ID, which is
 * the only scheduling this synchronous interface can promise. */
static int out_xfer_locked(UsbDev* d, Ring* ring, const uint8_t* data, uint16_t len, int iso) {
	uint64_t trb;
	uint32_t ctrl = (1u << 5) | ((iso ? TRB_ISOCH : TRB_NORMAL) << 10);
	int rc;

	if (!ring->v || !len)
		return -1;
	if (d->state != XDEV_ENUM && d->state != XDEV_READY)
		return -1;
	if (iso)
		ctrl |= 1u << 31; /* SIA */
	memcpy(d->xfer, data, len);
	clean(d->xfer, len);
	trb = ring_push(ring, (uint32_t)d->xfer_phys, (uint32_t)(d->xfer_phys >> 32), len, ctrl);
	ring_door(d->slot, ring);
	rc = wait_xfer(trb, d->slot);
	if (rc)
		ep_recover_locked(d, ring);
	return rc;
}

static int xhci_control(void* ctx, uint8_t bm, uint8_t req, uint16_t val, uint16_t idx,
						uint8_t* data, uint16_t len, int data_in) {
	UsbDev* d = (UsbDev*)ctx;
	return ctrl(d, bm, req, val, idx, data, len, data_in);
}

static int xhci_intr(void* ctx, uint8_t* data, uint16_t cap) {
	return intr_xfer((UsbDev*)ctx, data, cap);
}

static int xhci_bulk_out(void* ctx, const uint8_t* data, uint16_t len) {
	UsbDev* d = (UsbDev*)ctx;
	int rc;
	if (!d || len > PAGE_SIZE)
		return -1;
	usb_lock();
	rc = out_xfer_locked(d, &d->bout, data, len, 0);
	usb_unlock();
	return rc;
}

/* Queue a bulk IN transfer and ring the doorbell, without waiting for its
 * completion.  UAS needs this: the data URB has to reach the device before
 * the command completes, or the device may finish the command and free the
 * request while its data transfer is still unqueued.  Callers collect the
 * transfer with ring_in_wait_locked(). */
static uint64_t ring_in_arm_locked(UsbDev* d, Ring* ring, uint16_t len) {
	uint64_t trb;

	if (!ring->v || !d->bin_buf || !len || len > PAGE_SIZE)
		return 0;
	memset(d->bin_buf, 0, len);
	clean(d->bin_buf, len);
	trb = ring_push(ring, (uint32_t)d->bin_phys, (uint32_t)(d->bin_phys >> 32), len,
					(1u << 5) | (TRB_NORMAL << 10));
	ring_door(d->slot, ring);
	return trb;
}

/* Collect one transfer queued by ring_in_arm_locked(). */
static int ring_in_wait_locked(UsbDev* d, Ring* ring, uint64_t trb, uint16_t len,
							   uint8_t* data) {
	Trb ev;
	uint32_t residual, got;

	if (wait_xfer_ev(trb, d->slot, &ev)) {
		ep_recover_locked(d, ring);
		return -1;
	}
	residual = ev.w2 & 0xffffff;
	got = residual < len ? (uint32_t)len - residual : 0;
	if (got && data) {
		inv(d->bin_buf, got);
		memcpy(data, d->bin_buf, got);
	}
	return (int)got;
}

/* Run one bulk IN transfer of exactly len bytes on a ring.  When armed is
 * non-NULL the caller keeps at most one transfer queued (BT polls); mass
 * storage passes NULL because each phase waits for its own completion. */
static int ring_in_locked(UsbDev* d, Ring* ring, uint8_t* data, uint16_t len, int* armed) {
	uint64_t trb;
	int rc;

	if (armed && *armed)
		return -1;
	trb = ring_in_arm_locked(d, ring, len);
	if (!trb)
		return -1;
	if (armed)
		*armed = 1;
	rc = ring_in_wait_locked(d, ring, trb, len, data);
	if (armed)
		*armed = 0;
	return rc;
}

/* Run one bulk IN transfer to completion.  Caller holds usb_lock. */
static int bulk_in_locked(UsbDev* d, uint8_t* data, uint16_t len) {
	int rc = ring_in_locked(d, &d->bin, data, len, &d->bin_armed);
	d->bin_len = 0;
	return rc;
}

/* Polling bulk IN for the Bluetooth bridge.  Arm the caller's buffer size
 * once and leave the transfer queued until it completes, so the next poll
 * can collect a full packet instead of just one max-size packet. */
static int bulk_in_xfer(UsbDev* d, uint8_t* data, uint16_t cap) {
	Trb ev;
	uint16_t n, armed;
	uint32_t cc, residual;
	if (!d || !d->bin.v)
		return 0;
	usb_lock();
	if (d->state != XDEV_READY) {
		usb_unlock();
		return 0;
	}
	if (!cap || cap > PAGE_SIZE) {
		usb_unlock();
		return 0;
	}
	if (!d->bin_armed && arm_bin_locked(d, cap)) {
		usb_unlock();
		return 0;
	}
	armed = d->bin_len;
	drain();
	if (!take_match(0, d->bin_trb, d->slot, d->bin.dci, &ev)) {
		uint32_t state = ep_state_locked(d, d->bin.dci);
		if (state == 2 || state == 4) {
			UARTDebugOut("[xhci]: bulk-in endpoint state %d slot %d dci %d\r\n",
						 state, d->slot, d->bin.dci);
			d->bin_armed = 0;
			d->bin_len = 0;
			ep_recover_locked(d, &d->bin);
		}
		usb_unlock();
		return 0;
	}
	cc = (ev.w2 >> 24) & 0xff;
	residual = ev.w2 & 0xffffff;
	d->bin_armed = 0;
	d->bin_len = 0;
	if (cc != 1 && cc != 13) {
		UARTDebugOut("[xhci]: bulk-in completion %d slot %d dci %d\r\n",
					 cc, d->slot, d->bin.dci);
		ep_recover_locked(d, &d->bin);
		usb_unlock();
		return 0;
	}
	if (residual <= armed)
		n = (uint16_t)(armed - residual);
	else
		n = 0;
	if (n && data) {
		if (n > cap)
			n = cap;
		inv(d->bin_buf, n);
		memcpy(data, d->bin_buf, n);
	} else {
		n = 0;
	}
	usb_unlock();
	return (int)n;
}

static int xhci_bulk_in(void* ctx, uint8_t* data, uint16_t cap) {
	return bulk_in_xfer((UsbDev*)ctx, data, cap);
}

static int xhci_iso_out(void* ctx, const uint8_t* data, uint16_t len) {
	UsbDev* d = (UsbDev*)ctx;
	Ring* r;
	int rc;
	if (!d || len > PAGE_SIZE)
		return -1;
	r = d->have_iso ? &d->iout : &d->bout;
	usb_lock();
	rc = out_xfer_locked(d, r, data, len, r == &d->iout);
	usb_unlock();
	return rc;
}

static int dci_for(uint8_t addr) {
	int num = addr & 0x0f;
	int in = (addr & 0x80) != 0;
	return num == 0 ? 1 : (num * 2 + (in ? 1 : 0));
}

/* A SuperSpeed endpoint descriptor is followed by a companion (0x30) that
 * carries the burst size and, for isochronous pipes, the payload per
 * service interval.  USB 2.0 descriptors have neither, so a zeroed result
 * keeps the endpoint context unchanged for those devices. */
static void usb_ep_comp(const uint8_t* desc, int len, uint8_t ep_addr, EpComp* out) {
	int i = 0;

	memset(out, 0, sizeof *out);
	while (i + 2 <= len) {
		uint8_t l = desc[i];
		uint8_t t = desc[i + 1];

		if (l < 2 || i + l > len)
			break;
		if (t == 5 && l >= 7 && desc[i + 2] == ep_addr) {
			int j = i + l;
			while (j + 2 <= len) {
				uint8_t cl = desc[j];
				uint8_t ct = desc[j + 1];

				/* Stop at the next endpoint or interface: a companion
				 * only belongs to the descriptor immediately before it. */
				if (cl < 2 || j + cl > len || ct == 5 || ct == 4)
					break;
				if (ct == 0x30 && cl >= 6) {
					out->burst = desc[j + 2] & 0x1f;
					out->mult = desc[j + 3] & 0x03;
					out->bytes = (uint16_t)(desc[j + 4] | (desc[j + 5] << 8));
					UARTDebugOut("[xhci]: ss companion ep %x burst %d mult %d bytes %d\r\n",
								 ep_addr, out->burst, out->mult, out->bytes);
					return;
				}
				j += cl;
			}
			return;
		}
		i += l;
	}
}

/* Log the companion of every SuperSpeed endpoint in a configuration.  Class
 * drivers look up the companions they need; this walks the descriptor once
 * so an unclaimed device still proves the SS descriptor layout was read. */
static void usb_log_companions(const uint8_t* desc, int len) {
	int i = 0;

	while (i + 2 <= len) {
		uint8_t l = desc[i];
		uint8_t t = desc[i + 1];
		int j;

		if (l < 2 || i + l > len)
			break;
		if (t != 5 || l < 7) {
			i += l;
			continue;
		}
		j = i + l;
		while (j + 2 <= len) {
			uint8_t cl = desc[j];
			uint8_t ct = desc[j + 1];

			if (cl < 2 || j + cl > len || ct == 5 || ct == 4)
				break;
			if (ct == 0x30 && cl >= 6) {
				UARTDebugOut("[xhci]: ss companion ep %x burst %d mult %d bytes %d\r\n",
							 desc[i + 2], desc[j + 2] & 0x1f, desc[j + 3] & 3,
							 desc[j + 4] | (desc[j + 5] << 8));
				break;
			}
			j += cl;
		}
		i += l;
	}
}

typedef struct {
	int kind; /* 1 keyboard, 2 pointer, 3 HID with no boot protocol */
	uint8_t proto;
	uint8_t iface;
	uint8_t ep_addr;
	uint16_t ep_mps;
	uint8_t ep_interval;
	uint16_t report_len;
} HidIface;

/* Collect every HID interface in a configuration.  Binding is by interface
 * class/subclass/protocol: QEMU's usb-kbd has device class 0 and only its
 * interface says HID/1/1.  An interface without an interrupt IN endpoint
 * cannot carry reports, so it is dropped from the result. */
static int hid_collect(const uint8_t* desc, int len, HidIface* out, int max) {
	int i = 0;
	int n = 0;
	int cur = -1;
	while (i + 2 <= len) {
		uint8_t l = desc[i];
		uint8_t t = desc[i + 1];
		if (l < 2 || i + l > len)
			break;
		if (t == 4 && l >= 9) {
			cur = -1;
			UARTDebugOut("[xhci]: iface %d class %x/%x/%x\r\n", desc[i + 2],
						 desc[i + 5], desc[i + 6], desc[i + 7]);
			if (desc[i + 5] == 3 && n < max) {
				cur = n++;
				memset(&out[cur], 0, sizeof out[cur]);
				out[cur].iface = desc[i + 2];
				out[cur].proto = desc[i + 7];
				out[cur].kind = desc[i + 7] == 1 ? 1 : 3;
			}
		} else if (t == 0x21 && l >= 9 && cur >= 0) {
			/* HID descriptor: bNumDescriptors, first descriptor type
			 * (0x22 report) and its little-endian length. */
			if (desc[i + 6] == 0x22 && !out[cur].report_len)
				out[cur].report_len = (uint16_t)(desc[i + 7] | (desc[i + 8] << 8));
		} else if (t == 5 && l >= 7 && cur >= 0) {
			uint8_t addr = desc[i + 2];
			uint8_t attr = desc[i + 3] & 3;
			UARTDebugOut("[xhci]: ep %x attr %x mps %x\r\n", addr, attr,
						 desc[i + 4] | (desc[i + 5] << 8));
			if (attr == 3 && (addr & 0x80) && !out[cur].ep_addr) {
				out[cur].ep_addr = addr;
				out[cur].ep_mps = (uint16_t)(desc[i + 4] | (desc[i + 5] << 8));
				out[cur].ep_mps &= 0x7ff;
				out[cur].ep_interval = desc[i + 6];
			}
		}
		i += l;
	}
	for (i = 0, cur = 0; i < n; i++) {
		if (out[i].ep_addr)
			out[cur++] = out[i];
	}
	return cur;
}

static uint32_t hid_item_u32(const uint8_t* p, int n) {
	uint32_t v = 0;
	int i;
	for (i = 0; i < n && i < 4; i++)
		v |= (uint32_t)p[i] << (8 * i);
	return v;
}

static int32_t hid_item_s32(uint32_t v, int n) {
	if (n > 0 && n < 4 && (v & (1u << (n * 8 - 1))))
		v |= ~((1u << (n * 8)) - 1u);
	return (int32_t)v;
}

static int hid_usage_for(const HidField* f, uint32_t idx, uint32_t* page, uint32_t* usage) {
	*page = f->usage_page;
	if (f->nusages) {
		if (idx >= f->nusages)
			return 0;
		*usage = f->usages[idx];
		return 1;
	}
	if (f->has_range) {
		*usage = f->usage_min + idx;
		return *usage <= f->usage_max;
	}
	return 0;
}

static int32_t hid_field_value(const uint8_t* data, int len, uint32_t off, uint32_t width,
								  int32_t logical_min) {
	uint32_t v = 0;
	uint32_t i;
	if (width == 0 || width > 32)
		return 0;
	for (i = 0; i < width; i++) {
		uint32_t bit = off + i;
		if ((int)(bit >> 3) >= len)
			return 0;
		if (data[bit >> 3] & (1u << (bit & 7)))
			v |= 1u << i;
	}
	if (logical_min < 0 && width < 32 && (v & (1u << (width - 1))))
		v |= ~((1u << width) - 1u);
	return (int32_t)v;
}

/* Parse a HID report descriptor into input fields and the LED output
 * report.  Short items only: long items are vanishingly rare in keyboards
 * and pointing devices, and an unknown item can be skipped by its declared
 * length. */
static int hid_parse_report(const uint8_t* p, int len, HidReport* out) {
	uint32_t page = 0, rsize = 0, rcount = 0, rid = 0;
	int32_t lmin = 0, lmax = 0;
	uint32_t bits[256];
	uint32_t obits[256];
	uint32_t usages[HID_MAX_USAGES];
	uint32_t umin = 0, umax = 0;
	uint32_t last_page = 0, last_usage = 0;
	int nusages = 0;
	int have_min = 0, have_max = 0, have_last = 0;
	int i = 0;

	struct {
		uint32_t page, rsize, rcount, rid;
		int32_t lmin, lmax;
	} stack[HID_GLOBAL_STACK];
	int sp = 0;

	memset(out, 0, sizeof *out);
	memset(bits, 0, sizeof bits);
	memset(obits, 0, sizeof obits);
	while (i < len) {
		uint8_t pre = p[i++];
		int size = pre & 3;
		int type = (pre >> 2) & 3;
		int tag = pre >> 4;
		uint32_t val;

		if (size == 3)
			size = 4;
		if (i + size > len)
			break;
		val = hid_item_u32(p + i, size);
		i += size;

		if (type == 0) { /* main item */
			if (tag == 0xA) { /* collection */
				if (have_last && last_page == 0x01 &&
					(last_usage == 1 || last_usage == 2 || last_usage == 6)) {
					if (!out->kind)
						out->kind = (last_usage == 6) ? 1 : 2;
				}
			} else if (tag == 0x8) { /* input */
				/* Report IDs apply to input reports too.  A
				 * descriptor may use them only for output
				 * reports, so decide from the input fields. */
				if (rid)
					out->has_ids = 1;
				if (!(val & HID_FIELD_CONSTANT) && out->nfields < HID_MAX_FIELDS) {
					HidField* f = &out->fields[out->nfields++];
					f->report_id = rid;
					f->usage_page = page;
					f->logical_min = lmin;
					f->logical_max = lmax;
					f->report_size = rsize;
					f->report_count = rcount;
					f->bit_offset = bits[rid & 0xff];
					f->flags = HID_FIELD_VARIABLE;
					if (val & 4)
						f->flags |= HID_FIELD_RELATIVE;
					if (!(val & 2))
						f->flags |= HID_FIELD_ARRAY;
					if (nusages) {
						f->nusages = (uint8_t)nusages;
						memcpy(f->usages, usages, sizeof(uint32_t) * (size_t)nusages);
					}
					if (have_min) {
						f->has_range = 1;
						f->usage_min = umin;
						f->usage_max = have_max ? umax : umin;
					}
				}
				bits[rid & 0xff] += rsize * rcount;
			} else if (tag == 0x9) { /* output */
				/* One-bit LED usages are what keyboards use.  Keep the
				 * first LED field so SET_REPORT can drive the mask. */
				if (!(val & HID_FIELD_CONSTANT) && page == 0x08 &&
					rsize == 1 && rcount) {
					if (!out->led_valid) {
						out->led_valid = 1;
						out->led_report_id = rid;
						out->led_offset = obits[rid & 0xff];
						out->led_count = rcount > 8 ? 8 : rcount;
					}
				}
				obits[rid & 0xff] += rsize * rcount;
			}
			/* Local items belong to exactly one main item. */
			nusages = 0;
			have_min = have_max = 0;
			have_last = 0;
			continue;
		}
		if (type == 1) { /* global item */
			switch (tag) {
			case 0x0:
				page = val;
				break;
			case 0x1:
				lmin = hid_item_s32(val, size);
				break;
			case 0x2:
				lmax = hid_item_s32(val, size);
				break;
			case 0x7:
				rsize = val;
				break;
			case 0x8:
				rid = val;
				break;
			case 0x9:
				rcount = val;
				break;
			case 0xA: /* push */
				if (sp < HID_GLOBAL_STACK) {
					stack[sp].page = page;
					stack[sp].rsize = rsize;
					stack[sp].rcount = rcount;
					stack[sp].rid = rid;
					stack[sp].lmin = lmin;
					stack[sp].lmax = lmax;
					sp++;
				}
				break;
			case 0xB: /* pop */
				if (sp > 0) {
					sp--;
					page = stack[sp].page;
					rsize = stack[sp].rsize;
					rcount = stack[sp].rcount;
					rid = stack[sp].rid;
					lmin = stack[sp].lmin;
					lmax = stack[sp].lmax;
				}
				break;
			}
			continue;
		}
		if (type == 2) { /* local item */
			switch (tag) {
			case 0x0: /* usage */
				if (size == 4) {
					last_page = val >> 16;
					last_usage = val & 0xffff;
				} else {
					last_page = page;
					last_usage = val & 0xffff;
				}
				if (nusages < HID_MAX_USAGES)
					usages[nusages++] = last_usage;
				have_last = 1;
				break;
			case 0x1:
				umin = val & 0xffff;
				have_min = 1;
				break;
			case 0x2:
				umax = val & 0xffff;
				have_max = 1;
				break;
			}
		}
	}

	/* An input report can span several endpoint packets, so its transfer
	 * length comes from the descriptor rather than wMaxPacketSize.  The
	 * report ID byte precedes every report when IDs are in use. */
	for (i = 0; i < 256; i++) {
		uint32_t bytes = (bits[i] + 7) / 8;
		if (bytes > out->in_len)
			out->in_len = bytes;
	}
	if (out->in_len && out->has_ids)
		out->in_len++;
	if (out->in_len > HID_REPORT_MAX)
		out->in_len = HID_REPORT_MAX;

	/* Some devices omit the application collection usage; infer from the
	 * fields instead of rejecting a perfectly usable report. */
	if (!out->kind) {
		for (i = 0; i < out->nfields; i++) {
			HidField* f = &out->fields[i];
			uint32_t usage;
			for (usage = 0;; usage++) {
				uint32_t pg, u;
				if (!hid_usage_for(f, usage, &pg, &u))
					break;
				if (pg == 0x07 && ((f->flags & HID_FIELD_ARRAY) ||
								   (u >= 0xe0 && u <= 0xe7)))
					out->kind = 1;
				else if (pg == 0x09 || (pg == 0x01 && (u == 0x30 || u == 0x31)))
					out->kind = out->kind ? out->kind : 2;
			}
			if (out->kind == 1)
				break;
		}
	}
	out->valid = out->kind != 0 && out->nfields != 0;
	return out->valid;
}

/* Send the output report that drives the keyboard LEDs.  The mask follows
 * the HID LED page order: bit 0 Num Lock, 1 Caps Lock, 2 Scroll Lock. */
static int hid_set_leds(UsbDev* d, HidPipe* p, uint8_t mask) {
	uint8_t rep[8];
	uint32_t bit, len, i;
	int rid;

	if (!p->rep.led_valid)
		return 0;
	rid = (int)p->rep.led_report_id;
	len = (p->rep.led_offset + p->rep.led_count + 7) / 8;
	if (len == 0)
		len = 1;
	if (len + (rid ? 1u : 0u) > sizeof rep)
		return -1;
	memset(rep, 0, sizeof rep);
	bit = p->rep.led_offset + (rid ? 8u : 0u);
	for (i = 0; i < p->rep.led_count && i < 8; i++) {
		if (mask & (1u << i))
			rep[(bit + i) >> 3] |= (uint8_t)(1u << ((bit + i) & 7));
	}
	if (rid)
		rep[0] = (uint8_t)rid;
	if (ctrl(d, 0x21, 0x09, (uint16_t)(0x0200 | (rid & 0xff)), p->iface, rep,
			 (uint16_t)(len + (rid ? 1u : 0u)), 0)) {
		UARTDebugOut("[xhci-hid]: led report failed\r\n");
		return -1;
	}
	p->led_state = mask;
	return 1;
}

static int claim_hid(UsbDev* d, const uint8_t* desc, int len, uint8_t cfgval) {
	HidIface ifaces[XHCI_MAX_HID];
	EpComp comp;
	uint32_t add = 0;
	uint32_t entries = 1;
	int niface;
	int count;
	int i;
	uint32_t w, h;

	niface = hid_collect(desc, len, ifaces, XHCI_MAX_HID);
	if (!niface)
		return 0; /* not a HID interface */

	/* One SET_CONFIGURATION covers every interface on the device, so it
	 * happens once even when several HID interfaces are present. */
	if (ctrl(d, 0x00, 9, cfgval, 0, NULL, 0, 0)) {
		UARTDebugOut("[xhci]: HID set configuration failed\r\n");
		return -1;
	}

	w = AuConsoleGetScreenWidth();
	h = AuConsoleGetScreenHeight();
	if (!w || !h) {
		w = 1024;
		h = 768;
	}
	d->pointer_x = (int32_t)(w / 2);
	d->pointer_y = (int32_t)(h / 2);
	d->pointer_valid = 0;

	/* Set up every interface's endpoint context first, then add them
	 * all with a single Configure Endpoint command. */
	for (count = 0; count < niface; count++) {
		HidIface* hi = &ifaces[count];
		HidPipe* p = &d->hids[count];
		int kind = hi->kind;
		int dci;
		uint32_t iv;
		uint16_t rlen;

		memset(p, 0, sizeof *p);
		p->iface = hi->iface;
		p->ep_addr = hi->ep_addr;

		/* Parse the report descriptor when the HID descriptor gives us
		 * its length.  This is the only way to know a non-boot
		 * device's layout, and it also tells us whether report IDs
		 * are in use. */
		rlen = hi->report_len;
		if (rlen > PAGE_SIZE)
			rlen = PAGE_SIZE;
		if (rlen && ctrl(d, 0x81, 6, 0x2200, hi->iface, d->xfer, rlen, 1) == 0) {
			hid_parse_report(d->xfer, rlen, &p->rep);
			if (p->rep.valid) {
				kind = p->rep.kind;
				UARTDebugOut("[xhci-hid]: report iface %d len %d fields %d kind %s ids %d\r\n",
							 hi->iface, rlen, p->rep.nfields,
							 kind == 1 ? "keyboard" : "pointer", p->rep.has_ids);
			}
		}

		/* Protocol 0 does not say keyboard or mouse, and some
		 * descriptors are too short to parse.  Fall back to the first
		 * application usage bytes, which is enough to separate
		 * QEMU's tablet/mouse/keyboard. */
		if (kind == 3) {
			uint8_t rd[16];
			memset(rd, 0, sizeof rd);
			kind = 2;
			if (ctrl(d, 0x81, 6, 0x2200, hi->iface, rd, 8, 1) == 0) {
				if (rd[0] == 0x05 && rd[1] == 0x07)
					kind = 1;
				else if (rd[0] == 0x05 && rd[1] == 0x01 && rd[2] == 0x09 &&
						 rd[3] == 0x06)
					kind = 1;
			}
		}
		p->kind = kind;

		dci = dci_for(hi->ep_addr);
		p->intr.v = (Trb*)dma_page(&p->intr.phys);
		p->buf = (uint8_t*)dma_page(&p->buf_phys);
		if (!p->intr.v || !p->buf)
			return -1;
		p->intr.enq = 0;
		p->intr.cycle = 1;
		p->intr.dci = (uint8_t)dci;
		p->mps = hi->ep_mps ? hi->ep_mps : 8;
		/* Queue the whole input report.  A boot-layout device without a
		 * usable descriptor falls back to one packet. */
		p->xfer_len = p->rep.in_len ? (uint16_t)p->rep.in_len : p->mps;

		/* xHCI encodes high-speed bInterval as bInterval-1; full/low
		 * speed devices use the frame count directly. */
		if (d->speed >= 3)
			iv = hi->ep_interval ? (uint32_t)(hi->ep_interval - 1) : 0;
		else
			iv = hi->ep_interval;
		if (iv > 15)
			iv = 15;
		usb_ep_comp(desc, len, hi->ep_addr, &comp);
		ep_init(d->in_ctx + 32 + ctx_sz * (uint32_t)dci, &p->intr, 7,
				p->mps, iv, p->mps, &comp);
		add |= 1u << dci;
		if ((uint32_t)dci > entries)
			entries = (uint32_t)dci;

		/* SET_IDLE(0): report only on change.  Report protocol is the
		 * default and is what the descriptor-driven decoder expects;
		 * only a keyboard with no usable report descriptor needs the
		 * fixed boot layout forced on it. */
		if (ctrl(d, 0x21, 0x0a, 0, hi->iface, NULL, 0, 0))
			UARTDebugOut("[xhci]: HID set idle failed\r\n");
		if (kind == 1 && !p->rep.valid) {
			if (ctrl(d, 0x21, 0x0b, 0, hi->iface, NULL, 0, 0))
				UARTDebugOut("[xhci]: HID boot protocol failed\r\n");
		}
	}
	if (configure_eps(d, add, entries)) {
		UARTDebugOut("[xhci]: HID configure endpoint failed\r\n");
		return -1;
	}

	d->nhids = count;
	for (i = 0; i < count; i++) {
		HidPipe* p = &d->hids[i];
		arm_hid(d, p);
		if (p->kind == 1 && hid_set_leds(d, p, 0) == 1)
			UARTDebugOut("[xhci-hid]: leds %x\r\n", 0);
		hid_count++;
		{
			char path[24];
			usb_port_path(d, path, sizeof path);
			UARTDebugOut("[xhci]: HID %s ready on port %s slot %d ep=%x mps=%d\r\n",
						 p->kind == 1 ? "keyboard" : "pointer", path, d->slot,
						 p->ep_addr, p->mps);
		}
	}
	if (niface > XHCI_MAX_HID)
		UARTDebugOut("[xhci]: hid %d interfaces, kept %d\r\n", niface,
					 XHCI_MAX_HID);
	return 1;
}

static int claim_bt(UsbDev* d, const uint8_t* desc, int len, uint8_t cfgval) {
	int i = 0;
	int in_if = 0;
	int bt_iface = 0;
	uint32_t add = 0;
	uint32_t entries = 1;
	AuBtUsbOps ops;

	while (i + 2 <= len) {
		uint8_t l = desc[i];
		uint8_t t = desc[i + 1];
		if (l < 2 || i + l > len)
			break;
		if (t == 4 && l >= 9) {
			in_if = (desc[i + 5] == 0xe0 && desc[i + 6] == 0x01 && desc[i + 7] == 0x01);
			if (in_if)
				bt_iface = 1;
		}
		if (in_if && t == 5 && l >= 7) {
			uint8_t addr = desc[i + 2];
			uint8_t attr = desc[i + 3] & 3;
			uint16_t mps = (uint16_t)(desc[i + 4] | (desc[i + 5] << 8));
			uint8_t interval = desc[i + 6];
			int dir_in = (addr & 0x80) != 0;
			int dci = dci_for(addr);
			Ring* ring = NULL;
			uint32_t type = 4;
			if (attr == 3 && dir_in && !d->intr.v) {
				d->intr.v = (Trb*)dma_page(&d->intr.phys);
				d->intr.enq = 0;
				d->intr.cycle = 1;
				d->intr.dci = (uint8_t)dci;
				d->intr_mps = mps ? mps : 16;
				d->intr_interval = interval;
				ring = &d->intr;
				type = 7;
			} else if (attr == 2 && !dir_in && !d->bout.v) {
				d->bout.v = (Trb*)dma_page(&d->bout.phys);
				d->bout.enq = 0;
				d->bout.cycle = 1;
				d->bout.dci = (uint8_t)dci;
				ring = &d->bout;
				type = 2;
			} else if (attr == 2 && dir_in && !d->bin.v) {
				d->bin.v = (Trb*)dma_page(&d->bin.phys);
				d->bin_buf = (uint8_t*)dma_page(&d->bin_phys);
				d->bin.enq = 0;
				d->bin.cycle = 1;
				d->bin.dci = (uint8_t)dci;
				d->bin_mps = mps ? mps : 64;
				ring = &d->bin;
				type = 6;
			} else if (!dir_in && !d->iout.v && (attr == 1 || (attr == 2 && d->bout.v))) {
				d->iout.v = (Trb*)dma_page(&d->iout.phys);
				d->iout.enq = 0;
				d->iout.cycle = 1;
				d->iout.dci = (uint8_t)dci;
				ring = &d->iout;
				type = (attr == 1) ? 1 : 2;
				d->have_iso = 1;
			}
			if (ring) {
				EpComp comp;
				uint32_t iv = interval ? (uint32_t)(interval - 1) : 0;
				if (iv > 15)
					iv = 6;
				usb_ep_comp(desc, len, addr, &comp);
				ep_init(d->in_ctx + 32 + ctx_sz * (uint32_t)dci, ring, type,
						mps ? mps : 64, iv, mps ? mps : 64, &comp);
				add |= 1u << dci;
				if ((uint32_t)dci > entries)
					entries = (uint32_t)dci;
			}
		}
		i += l;
	}
	if (!bt_iface)
		return 0; /* not a Bluetooth HCI interface */
	if (!d->intr.v || !d->bout.v || !d->bin.v)
		return -1;
	d->intr_len = BT_HCI_EVENT_MAX;
	if (!d->intr_buf) {
		d->intr_buf = (uint8_t*)dma_page(&d->intr_phys);
		if (!d->intr_buf)
			return -1;
	}
	if (!d->bin_buf) {
		d->bin_buf = (uint8_t*)dma_page(&d->bin_phys);
		if (!d->bin_buf)
			return -1;
	}
	if (ctrl(d, 0x00, 9, cfgval, 0, NULL, 0, 0))
		return -1;
	if (configure_eps(d, add, entries))
		return -1;
	arm_intr(d);
	memset(&ops, 0, sizeof ops);
	ops.ctx = d;
	ops.control = xhci_control;
	ops.intr = xhci_intr;
	ops.bulk_out = xhci_bulk_out;
	ops.bulk_in = xhci_bulk_in;
	ops.iso_out = xhci_iso_out;
	ops.has_iso = d->have_iso;
	d->bt = 1;
	bt_count++;
	/* AuBtUsbReady() creates a kernel thread; record the bridge now and
	 * let AuXhciStartPolling() start it once the scheduler owns its thread
	 * list. */
	bt_ops = ops;
	bt_ops_valid = 1;
	/* Boot enumeration runs before the scheduler owns the thread list, so
	 * AuXhciStartPolling() defers the bridge until then.  A hot-plugged
	 * adapter arrives after that point and must wake the parked BT thread
	 * now instead of waiting for a start that already happened. */
	if (polling_started) {
		bt_ops_valid = 0;
		AuBtUsbReady(&bt_ops);
	}
	return 1;
}

/* ------------------------- USB mass storage ------------------------- */

#define MSC_CLASS 8
#define MSC_SUBCLASS_SCSI 6
#define MSC_PROTO_BOT 0x50
#define MSC_PROTO_UAS 0x62
#define MSC_CBW_SIG 0x43425355u
#define MSC_CSW_SIG 0x53425355u

static uint32_t msc_le32(const uint8_t* p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
		   ((uint32_t)p[3] << 24);
}

static void msc_put_le32(uint8_t* p, uint32_t v) {
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static uint32_t msc_be32(const uint8_t* p) {
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
		   (uint32_t)p[3];
}

static uint16_t msc_be16(const uint8_t* p) {
	return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void msc_put_be32(uint8_t* p, uint32_t v) {
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

static void msc_put_be64(uint8_t* p, uint64_t v) {
	msc_put_be32(p, (uint32_t)(v >> 32));
	msc_put_be32(p + 4, (uint32_t)v);
}

static void msc_put_be16(uint8_t* p, uint16_t v) {
	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)v;
}

/* Secondary LUNs share the command, status and data rings of the first
 * LUN; only the LUN field in each command differs. */
static MscDisk* msc_transport(MscDisk* m) {
	return m->xport ? m->xport : m;
}

/* One Bulk-Only Transport transaction: CBW, optional data phase, then CSW.
 * Caller holds usb_lock so the three phases cannot interleave with another
 * mass-storage command.  Returns 0 when a CSW with the matching tag arrived
 * and stores its status byte.  A transport error leaves recovery to the
 * caller, because resetting the pipe also has to clear the device halt. */
static int msc_transport_locked(UsbDev* d, MscDisk* m, const uint8_t* cdb, uint8_t cdblen,
								uint8_t* data, uint32_t len, int in, uint8_t* status,
								int allow_short) {
	MscDisk* t = msc_transport(m);
	uint8_t cbw[31];
	uint8_t csw[13];
	uint32_t tag = ++t->tag;
	int got = 0;
	int short_data = 0;

	if (cdblen > 16 || len > PAGE_SIZE)
		return -1;
	memset(cbw, 0, sizeof cbw);
	msc_put_le32(cbw, MSC_CBW_SIG);
	msc_put_le32(cbw + 4, tag);
	msc_put_le32(cbw + 8, len);
	cbw[12] = in ? 0x80 : 0x00;
	cbw[13] = m->lun & 0x0f;
	cbw[14] = cdblen;
	memcpy(cbw + 15, cdb, cdblen);
	if (out_xfer_locked(d, &d->bout, cbw, sizeof cbw, 0))
		return -1;

	if (len) {
		if (in) {
			got = bulk_in_locked(d, data, (uint16_t)len);
			if (got < 0)
				return -1;
			short_data = got != (int)len;
		} else if (out_xfer_locked(d, &d->bout, data, (uint16_t)len, 0)) {
			return -1;
		}
	}

	memset(csw, 0, sizeof csw);
	if (bulk_in_locked(d, csw, sizeof csw) != (int)sizeof csw) {
		UARTDebugOut("[xhci-msc]: short csw\r\n");
		return -1;
	}
	if (msc_le32(csw) != MSC_CSW_SIG || msc_le32(csw + 4) != tag) {
		UARTDebugOut("[xhci-msc]: bad csw sig/tag\r\n");
		return -1;
	}
	*status = csw[12];
	if (*status) {
		UARTDebugOut("[xhci-msc]: csw status %d residue %d\r\n",
					 *status, (int)msc_le32(csw + 8));
		return 0;
	}
	if (short_data) {
		if (allow_short)
			return 0;
		UARTDebugOut("[xhci-msc]: short data %d/%d\r\n", got, (int)len);
		return -1;
	}
	return 0;
}

/* Bulk-Only Mass Storage Reset followed by Clear Feature Halt on both bulk
 * endpoints, then rebuild the xHCI rings.  The class request clears the
 * device's Bulk-Only state machine; Clear Feature clears the device-side
 * halt while Reset Endpoint clears the controller state and data toggle. */
static int msc_reset_locked(UsbDev* d, MscDisk* m) {
	int rc = 0;
	if (d->state != XDEV_ENUM && d->state != XDEV_READY)
		return -1;
	UARTDebugOut("[xhci-msc]: reset recovery slot %d\r\n", d->slot);
	if (ctrl_locked(d, 0x21, 0xff, 0, m->iface, NULL, 0, 0)) {
		UARTDebugOut("[xhci-msc]: bulk reset failed\r\n");
		rc = -1;
	}
	if (ctrl_locked(d, 0x02, 0x01, 0, m->out_addr, NULL, 0, 0)) {
		UARTDebugOut("[xhci-msc]: clear halt out failed\r\n");
		rc = -1;
	}
	if (ctrl_locked(d, 0x02, 0x01, 0, m->in_addr, NULL, 0, 0)) {
		UARTDebugOut("[xhci-msc]: clear halt in failed\r\n");
		rc = -1;
	}
	if (ep_recover_locked(d, &d->bout))
		rc = -1;
	if (ep_recover_locked(d, &d->bin))
		rc = -1;
	return rc;
}

/* Fetch the sense data a failed command left pending.  The response is
 * logged; unit_attention tells the caller to retry the command instead of
 * treating the rejected command as final. */
static int msc_sense_locked(UsbDev* d, MscDisk* m, uint8_t status, int* unit_attention) {
	uint8_t cdb[6] = { 0x03, 0, 0, 0, 18, 0 }; /* REQUEST SENSE */
	uint8_t sense[18];
	uint8_t csw_status = 0;
	int key, asc, ascq;

	*unit_attention = 0;
	memset(sense, 0, sizeof sense);
	if (msc_transport_locked(d, m, cdb, sizeof cdb, sense, sizeof sense, 1,
							 &csw_status, 0) ||
		csw_status) {
		UARTDebugOut("[xhci-msc]: request sense failed\r\n");
		return -1;
	}
	key = sense[2] & 0x0f;
	asc = sense[12];
	ascq = sense[13];
	*unit_attention = key == 6;
	UARTDebugOut("[xhci-msc]: sense status %d key %x asc %x ascq %x\r\n", status, key, asc,
				 ascq);
	return 0;
}

/* A transport or phase error is retried once because reset recovery leaves
 * the pipe usable again.  A command the device rejected is not retried: the
 * REQUEST SENSE above has consumed its sense data and the failure is real. */
#define MSC_RETRY (-2)

/* UAS gives each command a tag; on SuperSpeed that tag is also the stream
 * ID of the status and data transfers, so the three phases of a command can
 * be matched without the Bulk-Only ready/CSW framing. */
static Ring* uas_ring(MscDisk* m, int ep, uint32_t tag) {
	MscDisk* t = msc_transport(m);
	if (t->streams)
		return &t->sring[ep][tag - 1];
	switch (ep) {
	case UAS_EP_STATUS:
		return &t->status;
	case UAS_EP_IN:
		return &t->din;
	default:
		return &t->dout;
	}
}

/* Read one status IU.  A ready notification is 4 bytes, a good completion
 * 16 and a failing one up to 34, so the caller inspects the short result
 * instead of requiring one fixed length. */
static int uas_status_locked(UsbDev* d, MscDisk* m, uint32_t tag, uint8_t* iu) {
	return ring_in_locked(d, uas_ring(m, UAS_EP_STATUS, tag), iu, 34, NULL);
}

/* Send one command IU.  It uses its own page because a data-out transfer is
 * already queued on the shared transfer page when the command goes out. */
static int uas_cmd_out_locked(UsbDev* d, MscDisk* m, const uint8_t* iu) {
	MscDisk* t = msc_transport(m);
	uint64_t trb;

	memcpy(t->iu_buf, iu, 32);
	clean(t->iu_buf, 32);
	trb = ring_push(&t->cmd, (uint32_t)t->iu_phys, (uint32_t)(t->iu_phys >> 32), 32,
					(1u << 5) | (TRB_NORMAL << 10));
	ring_door(d->slot, &t->cmd);
	if (wait_xfer(trb, d->slot)) {
		ep_recover_locked(d, &t->cmd);
		return -1;
	}
	return 0;
}

/* Queue a data-out transfer without waiting.  UAS data URBs must be in the
 * device before the command completes, so the command cannot be sent first
 * and the data phase cannot wait to be armed. */
static uint64_t uas_data_out_arm_locked(UsbDev* d, Ring* ring, const uint8_t* data,
										uint16_t len) {
	uint64_t trb;

	memcpy(d->xfer, data, len);
	clean(d->xfer, len);
	trb = ring_push(ring, (uint32_t)d->xfer_phys, (uint32_t)(d->xfer_phys >> 32), len,
					(1u << 5) | (TRB_NORMAL << 10));
	ring_door(d->slot, ring);
	return trb;
}

static int uas_data_out_wait_locked(UsbDev* d, Ring* ring, uint64_t trb) {
	if (wait_xfer(trb, d->slot)) {
		ep_recover_locked(d, ring);
		return -1;
	}
	return 0;
}

/* Run one UAS command and return its SCSI status.  Without streams the
 * device first reports READ_READY or WRITE_READY and only then accepts the
 * data transfer; with streams the tag selects both data and status rings,
 * and the data URB is queued before the command so a command that finishes
 * early cannot free the request before its data phase exists. */
static int uas_transport_locked(UsbDev* d, MscDisk* m, const uint8_t* cdb, uint8_t cdblen,
								uint8_t* data, uint32_t len, int in, uint8_t* sense,
								int allow_short) {
	MscDisk* t = msc_transport(m);
	uint8_t iu[32];
	uint8_t st[34];
	uint32_t tag = (t->tag++ % UAS_TAGS) + 1;
	Ring* data_ring = uas_ring(m, in ? UAS_EP_IN : UAS_EP_OUT, tag);
	uint64_t data_trb = 0;
	int data_short = 0;
	int have_status = 0;
	int got;

	if (cdblen > 16 || len > PAGE_SIZE)
		return -1;
	memset(iu, 0, sizeof iu);
	iu[0] = UAS_UI_COMMAND;
	msc_put_be16(iu + 2, (uint16_t)tag);
	/* Simple task attribute, no additional CDB.  The 8-byte LUN field
	 * starts at byte 8; simple LUN values below 256 live in its second
	 * byte, which is what QEMU's UAS target decodes. */
	iu[9] = m->lun;
	memcpy(iu + 16, cdb, cdblen);
	if (t->streams && len) {
		if (in)
			data_trb = ring_in_arm_locked(d, data_ring, (uint16_t)len);
		else
			data_trb = uas_data_out_arm_locked(d, data_ring, data, (uint16_t)len);
		if (!data_trb) {
			UARTDebugOut("[xhci-uas]: tag %d data arm failed\r\n", (int)tag);
			return -1;
		}
	}
	if (uas_cmd_out_locked(d, m, iu)) {
		if (data_trb)
			ep_recover_locked(d, data_ring);
		UARTDebugOut("[xhci-uas]: command %x tag %d failed\r\n", cdb[0], (int)tag);
		return -1;
	}
	if (!t->streams && len) {
		uint8_t ready = in ? UAS_UI_READ_READY : UAS_UI_WRITE_READY;
		got = uas_status_locked(d, m, tag, st);
		if (got < 4 || msc_be16(st + 2) != (uint16_t)tag) {
			UARTDebugOut("[xhci-uas]: tag %d ready phase %d\r\n", (int)tag,
						 got > 0 ? st[0] : got);
			return -1;
		}
		/* A command that fails before its data phase never gets a ready
		 * notification; the sense status itself is the final result. */
		if (st[0] == UAS_UI_SENSE) {
			have_status = 1;
			goto final_status;
		}
		if (st[0] != ready) {
			UARTDebugOut("[xhci-uas]: tag %d ready phase %d\r\n", (int)tag, st[0]);
			return -1;
		}
	}
	if (len) {
		if (t->streams) {
			if (in) {
				got = ring_in_wait_locked(d, data_ring, data_trb, (uint16_t)len, data);
				if (got < 0) {
					UARTDebugOut("[xhci-uas]: tag %d data %d/%d\r\n", (int)tag, got,
								 (int)len);
					return -1;
				}
				data_short = got != (int)len;
			} else if (uas_data_out_wait_locked(d, data_ring, data_trb)) {
				UARTDebugOut("[xhci-uas]: tag %d data out failed\r\n", (int)tag);
				return -1;
			}
		} else if (in) {
			got = ring_in_locked(d, data_ring, data, (uint16_t)len, NULL);
			if (got < 0) {
				UARTDebugOut("[xhci-uas]: tag %d data %d/%d\r\n", (int)tag, got, (int)len);
				return -1;
			}
			data_short = got != (int)len;
		} else if (out_xfer_locked(d, data_ring, data, (uint16_t)len, 0)) {
			UARTDebugOut("[xhci-uas]: tag %d data out failed\r\n", (int)tag);
			return -1;
		}
	}
final_status:
	if (!have_status)
		got = uas_status_locked(d, m, tag, st);
	if (got < 16 || st[0] != UAS_UI_SENSE || msc_be16(st + 2) != (uint16_t)tag) {
		UARTDebugOut("[xhci-uas]: tag %d final status %d id %d\r\n", (int)tag, got,
					 got > 0 ? st[0] : 0);
		return -1;
	}
	if (sense) {
		uint32_t sl = msc_be16(st + 14);
		if (sl > 18)
			sl = 18;
		memset(sense, 0, 18);
		memcpy(sense, st + 16, sl);
	}
	/* A failed command completes its data phase short or empty, so short
	 * data is only an error when the command itself reported success. */
	if (data_short && st[6] == 0 && !allow_short) {
		UARTDebugOut("[xhci-uas]: tag %d short data\r\n", (int)tag);
		return -1;
	}
	return st[6];
}

/* UAS reports the command result and its sense data in the same status IU,
 * so unlike BOT there is no separate REQUEST SENSE after a failure. */
static int msc_uas_cmd_locked(UsbDev* d, MscDisk* m, const uint8_t* cdb, uint8_t cdblen,
							  uint8_t* data, uint32_t len, int in, int allow_short) {
	uint8_t sense[18];
	int status = uas_transport_locked(d, m, cdb, cdblen, data, len, in, sense, allow_short);
	int key;

	if (status < 0)
		return MSC_RETRY;
	if (status == 0)
		return 0;
	if (status != 2) {
		UARTDebugOut("[xhci-uas]: command %x status %d\r\n", cdb[0], status);
		return -1;
	}
	key = sense[2] & 0x0f;
	UARTDebugOut("[xhci-uas]: sense status %d key %x asc %x ascq %x\r\n", status, key,
				 sense[12], sense[13]);
	return key == 6 ? MSC_RETRY : -1;
}

static int msc_cmd_locked(UsbDev* d, MscDisk* m, const uint8_t* cdb, uint8_t cdblen,
						  uint8_t* data, uint32_t len, int in, int allow_short) {
	uint8_t status = 0;
	int unit_attention = 0;

	if (cdblen > 16 || len > PAGE_SIZE)
		return -1;
	if (m->uas)
		return msc_uas_cmd_locked(d, m, cdb, cdblen, data, len, in, allow_short);
	if (msc_transport_locked(d, m, cdb, cdblen, data, len, in, &status, allow_short)) {
		msc_reset_locked(d, m);
		return MSC_RETRY;
	}
	if (!status)
		return 0;
	if (msc_sense_locked(d, m, status, &unit_attention) || status == 2) {
		msc_reset_locked(d, m);
		return MSC_RETRY;
	}
	/* A device reports UNIT ATTENTION once after power-up or reset; the
	 * command itself is still pending, so run it again. */
	if (unit_attention)
		return MSC_RETRY;
	return -1;
}

static int msc_cmd_flags(UsbDev* d, MscDisk* m, const uint8_t* cdb, uint8_t cdblen,
						 uint8_t* data, uint32_t len, int in, int allow_short) {
	int rc;
	usb_lock();
	rc = msc_cmd_locked(d, m, cdb, cdblen, data, len, in, allow_short);
	usb_unlock();
	if (rc != MSC_RETRY)
		return rc;
	UARTDebugOut(m->uas ? "[xhci-uas]: retry command %x\r\n"
						: "[xhci-msc]: retry command %x\r\n",
				 cdb[0]);
	usb_lock();
	rc = msc_cmd_locked(d, m, cdb, cdblen, data, len, in, allow_short);
	usb_unlock();
	return rc == 0 ? 0 : -1;
}

static int msc_cmd(UsbDev* d, MscDisk* m, const uint8_t* cdb, uint8_t cdblen, uint8_t* data,
				   uint32_t len, int in) {
	return msc_cmd_flags(d, m, cdb, cdblen, data, len, in, 0);
}

/* A command whose reply length is set by the payload, such as REPORT LUNS,
 * completes with a short data phase by design rather than truncation. */
static int msc_cmd_short(UsbDev* d, MscDisk* m, const uint8_t* cdb, uint8_t cdblen,
						 uint8_t* data, uint32_t len, int in) {
	return msc_cmd_flags(d, m, cdb, cdblen, data, len, in, 1);
}

static int msc_inquiry(UsbDev* d, MscDisk* m, char* product) {
	uint8_t cdb[6] = { 0x12, 0, 0, 0, 36, 0 };
	uint8_t inq[36];
	int i;
	memset(inq, 0, sizeof inq);
	if (msc_cmd(d, m, cdb, sizeof cdb, inq, sizeof inq, 1))
		return -1;
	for (i = 0; i < 16; i++)
		product[i] = (char)inq[16 + i];
	product[16] = 0;
	for (i = 15; i >= 0 && product[i] == ' '; i--)
		product[i] = 0;
	return 0;
}

static int msc_capacity(UsbDev* d, MscDisk* m) {
	uint8_t cdb[16];
	uint8_t cap[32];
	uint32_t block_size;

	memset(cdb, 0, sizeof cdb);
	cdb[0] = 0x25; /* READ CAPACITY (10) */
	memset(cap, 0, sizeof cap);
	if (msc_cmd(d, m, cdb, 10, cap, 8, 1))
		return -1;
	m->max_lba = msc_be32(cap);
	block_size = msc_be32(cap + 4);

	/* READ CAPACITY (10) clips at 2 TiB, so a device that reports the
	 * maximum LBA may be larger.  SERVICE ACTION IN (16) returns the full
	 * 64-bit address; if the device rejects it, keep the clipped value. */
	if (m->max_lba == 0xffffffffu) {
		memset(cdb, 0, sizeof cdb);
		cdb[0] = 0x9e;
		cdb[1] = 0x10; /* READ CAPACITY (16) */
		msc_put_be32(cdb + 10, sizeof cap);
		memset(cap, 0, sizeof cap);
		if (!msc_cmd(d, m, cdb, sizeof cdb, cap, sizeof cap, 1)) {
			m->max_lba = ((uint64_t)msc_be32(cap) << 32) | msc_be32(cap + 4);
			block_size = msc_be32(cap + 8);
		}
	}
	m->block_size = block_size;
	return 0;
}

/* Read or write through READ/WRITE (10) while the transfer stays below
 * 2 TiB, and through the 16-byte CDB variants once an LBA needs 64 bits. */
static int msc_rw_blocks(UsbDev* d, MscDisk* m, uint64_t lba, uint32_t count, uint8_t* buf,
						 int in) {
	uint32_t per = PAGE_SIZE / m->block_size;
	if (!per)
		return -1;
	while (count) {
		uint32_t n = count > per ? per : count;
		int wide = lba + n - 1 > 0xffffffffull;
		uint8_t cdb[16];
		memset(cdb, 0, sizeof cdb);
		if (wide) {
			cdb[0] = in ? 0x88 : 0x8a; /* READ (16), WRITE (16) */
			msc_put_be64(cdb + 2, lba);
			msc_put_be32(cdb + 10, n);
		} else {
			cdb[0] = in ? 0x28 : 0x2a; /* READ (10), WRITE (10) */
			msc_put_be32(cdb + 2, (uint32_t)lba);
			msc_put_be16(cdb + 7, (uint16_t)n);
		}
		if (msc_cmd(d, m, cdb, wide ? 16 : 10, buf, n * m->block_size, in))
			return -1;
		lba += n;
		count -= n;
		buf += n * m->block_size;
	}
	return 0;
}

static int msc_sync(UsbDev* d, MscDisk* m) {
	uint8_t cdb[10];
	memset(cdb, 0, sizeof cdb);
	cdb[0] = 0x35; /* SYNCHRONIZE CACHE (10) */
	return msc_cmd(d, m, cdb, sizeof cdb, NULL, 0, 0);
}

/* vdisk callbacks run after enumeration too.  Taking a reference around the
 * transfer lets disconnect wait for an in-flight command before it hands the
 * DMA pages back; clearing MscDisk.dev on release keeps a later call from
 * finding a reused slot. */
static UsbDev* msc_begin(MscDisk* m) {
	UsbDev* d;
	usb_lock();
	d = m->dev;
	if (d && (d->state == XDEV_ENUM || d->state == XDEV_READY))
		d->refs++;
	else
		d = NULL;
	usb_unlock();
	return d;
}

static void msc_end(UsbDev* d) {
	usb_lock();
	if (d->refs > 0)
		d->refs--;
	usb_unlock();
}

static int msc_vdisk_read(AuVDisk* vd, uint64_t lba, uint32_t count, uint64_t* buffer) {
	MscDisk* m = (MscDisk*)vd->data;
	UsbDev* d;
	int rc;

	if (!m || !buffer || !count || !m->block_size)
		return 0;
	if (lba > m->max_lba)
		return 0;
	if ((uint64_t)count - 1 > m->max_lba - lba)
		return 0;
	d = msc_begin(m);
	if (!d)
		return 0;
	rc = msc_rw_blocks(d, m, lba, count, (uint8_t*)buffer, 1);
	msc_end(d);
	return rc ? 0 : (int)((size_t)count * m->block_size);
}

static int msc_vdisk_write(AuVDisk* vd, uint64_t lba, uint32_t count, uint64_t* buffer) {
	MscDisk* m = (MscDisk*)vd->data;
	UsbDev* d;
	int rc;

	if (!m || !buffer || !count || !m->block_size)
		return 0;
	if (lba > m->max_lba)
		return 0;
	if ((uint64_t)count - 1 > m->max_lba - lba)
		return 0;
	d = msc_begin(m);
	if (!d)
		return 0;
	rc = msc_rw_blocks(d, m, lba, count, (uint8_t*)buffer, 0);
	if (!rc)
		rc = msc_sync(d, m);
	msc_end(d);
	return rc ? 0 : (int)((size_t)count * m->block_size);
}

static int msc_vdisk_flush(AuVDisk* vd) {
	MscDisk* m = (MscDisk*)vd->data;
	UsbDev* d;
	int rc;
	if (!m)
		return 0;
	d = msc_begin(m);
	if (!d)
		return -1;
	rc = msc_sync(d, m);
	msc_end(d);
	return rc;
}

/* Publish capacity in MiB so a disk above 2 TiB still prints a useful
 * number through the debug console's 32-bit %d. */
static uint32_t msc_capacity_mb(const MscDisk* m) {
	uint64_t per_mb = (1024u * 1024u) / m->block_size;
	return per_mb ? (uint32_t)((m->max_lba + 1) / per_mb) : 0;
}

/* Initialize the per-plug fields of a LUN without touching its vdisk, so a
 * re-plugged device can reuse the registered disk entry. */
static void msc_lun_init(MscDisk* m, UsbDev* d, MscDisk* primary, uint8_t lun) {
	m->dev = d;
	m->lun = lun;
	m->xport = primary;
	m->uas = primary->uas;
	m->streams = primary->streams;
	m->iface = primary->iface;
	m->in_addr = primary->in_addr;
	m->out_addr = primary->out_addr;
	m->block_size = 0;
	m->max_lba = 0;
}

/* Find the secondary-LUN entry this device used last time, or an unused
 * one.  Entries are never recycled: keeping the owner across a disconnect
 * is what lets a re-plugged device reuse the vdisk it registered. */
static MscDisk* msc_alloc_lun(UsbDev* d, MscDisk* primary, uint8_t lun,
							  const char* tag) {
	int owner = (int)(d - devices) + 1;
	MscDisk* unused = NULL;
	int i;

	for (i = 0; i < XHCI_MAX_EXTRA_LUN; i++) {
		MscDisk* m = &msc_extra[i];
		if (m->owner == (uint8_t)owner && m->lun == lun) {
			msc_lun_init(m, d, primary, lun);
			return m;
		}
		if (!m->owner && !unused)
			unused = m;
	}
	if (!unused) {
		UARTDebugOut("[xhci-%s]: no slot for lun %d\r\n", tag, (int)lun);
		return NULL;
	}
	unused->owner = (uint8_t)owner;
	unused->vd = NULL;
	msc_lun_init(unused, d, primary, lun);
	return unused;
}

/* Report LUNS returns an 8-byte list header followed by eight bytes per
 * LUN, so the reply is normally shorter than the request and the short data
 * phase is expected rather than a truncation error.  Only simple LUNs below
 * 256 are published, and LUN 0 is handled separately, so it is skipped
 * here.  A target that rejects the command simply exposes no secondary
 * LUNs. */
static int msc_report_luns(UsbDev* d, MscDisk* m, uint8_t* luns, int max) {
	uint8_t cdb[12];
	uint8_t buf[256];
	uint32_t list_len;
	uint32_t off;
	int n = 0;

	memset(cdb, 0, sizeof cdb);
	cdb[0] = 0xa0; /* REPORT LUNS */
	msc_put_be32(cdb + 6, (uint32_t)sizeof buf);
	memset(buf, 0, sizeof buf);
	if (msc_cmd_short(d, m, cdb, sizeof cdb, buf, sizeof buf, 1))
		return 0;
	list_len = msc_be32(buf);
	if (list_len > sizeof buf - 8)
		list_len = sizeof buf - 8;
	for (off = 8; off + 8 <= list_len + 8 && n < max; off += 8) {
		uint8_t lun = buf[off + 1];
		if (!(buf[off] & 0xc0) && lun)
			luns[n++] = lun;
	}
	return n;
}

/* Build a disk name, adding the LUN for a secondary disk so two LUNs of the
 * same target stay distinguishable in the vdisk list.  product is the
 * 16-byte INQUIRY field, so the formatted name always fits. */
static void msc_set_diskname(AuVDisk* vd, const char* product, uint8_t lun) {
	char name[sizeof vd->diskname];

	if (lun)
		_snprintf(name, sizeof name, "USB %s L%d", product, (int)lun);
	else
		_snprintf(name, sizeof name, "USB %s", product);
	memcpy(vd->diskname, name, sizeof vd->diskname);
}

/* Run the SCSI probe and publish a vdisk once the transport is live.  BOT
 * and UAS share the command set, so everything above the transport is
 * identical; tag names the transport in the boot log. */
static int msc_publish(UsbDev* d, MscDisk* m, const char* tag) {
	char product[17];
	uint8_t* block;
	AuVDisk* vd;
	int fresh = 0;

	UARTDebugOut("[xhci-%s]: lun %d\r\n", tag, (int)m->lun);
	memset(product, 0, sizeof product);
	if (msc_inquiry(d, m, product)) {
		UARTDebugOut("[xhci-%s]: inquiry failed\r\n", tag);
		return -1;
	}
	UARTDebugOut("[xhci-%s]: inquiry %s\r\n", tag, product);
	if (msc_capacity(d, m) || !m->block_size || m->block_size > PAGE_SIZE) {
		UARTDebugOut("[xhci-%s]: read capacity failed\r\n", tag);
		return -1;
	}
	UARTDebugOut("[xhci-%s]: capacity %d MB x %d bytes\r\n", tag,
				 (int)msc_capacity_mb(m), (int)m->block_size);

	/* Reading and rewriting LBA 0 proves both data directions.  A read-only
	 * disk fails the write with CHECK CONDITION, which also exercises the
	 * sense path. */
	block = (uint8_t*)kmalloc(m->block_size);
	if (block) {
		if (msc_rw_blocks(d, m, 0, 1, block, 1) == 0) {
			UARTDebugOut("[xhci-%s]: read lba0 %x %x %x %x\r\n", tag, block[0], block[1],
						 block[2], block[3]);
			if (msc_rw_blocks(d, m, 0, 1, block, 0) == 0) {
				msc_sync(d, m);
				UARTDebugOut("[xhci-%s]: write lba0 ok\r\n", tag);
			} else {
				UARTDebugOut("[xhci-%s]: write lba0 failed\r\n", tag);
			}
		} else {
			UARTDebugOut("[xhci-%s]: read lba0 failed\r\n", tag);
		}
		/* A disk past 2 TiB needs READ (16)/WRITE (16); prove both
		 * 64-bit address paths once at probe time instead of on the
		 * first deep access.  The block just read is written back
		 * unchanged, so the probe does not alter the disk. */
		if (m->max_lba > 0xffffffffull) {
			if (msc_rw_blocks(d, m, m->max_lba, 1, block, 1) == 0) {
				UARTDebugOut("[xhci-%s]: read last lba ok\r\n", tag);
				if (msc_rw_blocks(d, m, m->max_lba, 1, block, 0) == 0) {
					msc_sync(d, m);
					UARTDebugOut("[xhci-%s]: write last lba ok\r\n", tag);
				} else {
					UARTDebugOut("[xhci-%s]: write last lba failed\r\n", tag);
				}
			} else {
				UARTDebugOut("[xhci-%s]: read last lba failed\r\n", tag);
			}
		}
		kfree(block);
	}

	if (!m->vd) {
		m->vd = AuCreateVDisk();
		if (!m->vd)
			return -1;
		fresh = 1;
	}
	vd = m->vd;
	msc_set_diskname(vd, product, m->lun);
	memset(vd->serialNumber, 0, sizeof vd->serialNumber);
	strncpy(vd->serialNumber, d->serial[0] ? d->serial : "USB-MSC",
			sizeof vd->serialNumber - 1);
	vd->blockSize = m->block_size;
	vd->max_blocks = (uint64_t)m->max_lba + 1;
	vd->data = m;
	vd->Read = msc_vdisk_read;
	vd->Write = msc_vdisk_write;
	vd->Flush = msc_vdisk_flush;
	if (fresh)
		AuVDiskRegister(vd);
	else {
		/* The disk entry and its file system outlive an unplug.  Mark
		 * the transport present again and reattach the same volume
		 * under the letter it reserved at first registration. */
		AuVDiskSetOnline(vd, 1);
		UARTDebugOut("[xhci-%s]: vdisk reuse slot %d\r\n", tag, d->slot);
	}

	UARTDebugOut("[xhci-%s]: vdisk %s ready on port %d slot %d\r\n", tag, vd->diskname,
				 d->port, d->slot);
	return 1;
}

/* LUN 0 is live, so the target can answer REPORT LUNS.  A target that
 * rejects it is a single-LUN device and keeps the old behavior.  BOT and
 * UAS share the command set and the per-LUN transport, so discovery only
 * needs the tag that names the transport in the boot log. */
static void msc_publish_luns(UsbDev* d, MscDisk* m, const char* tag) {
	uint8_t luns[XHCI_MAX_EXTRA_LUN];
	int nlun;
	int i;

	nlun = msc_report_luns(d, m, luns, XHCI_MAX_EXTRA_LUN);
	if (nlun)
		UARTDebugOut("[xhci-%s]: report luns %d\r\n", tag, nlun);
	for (i = 0; i < nlun; i++) {
		MscDisk* x = msc_alloc_lun(d, m, luns[i], tag);
		if (!x)
			continue;
		if (msc_publish(d, x, tag) < 0)
			UARTDebugOut("[xhci-%s]: lun %d probe failed\r\n", tag, (int)luns[i]);
	}
}

/* Bind a Bulk-Only Transport mass-storage interface.  INQUIRY and READ
 * CAPACITY run before the vdisk is registered, which also proves the exact
 * length bulk IN path: each data phase and the 13-byte CSW are separate
 * transfers of different sizes. */
static int claim_msc(UsbDev* d, const uint8_t* desc, int len, uint8_t cfgval) {
	int i = 0;
	int cur_msc = 0;
	int iface = -1;
	uint8_t in_addr = 0, out_addr = 0;
	uint16_t in_mps = 0, out_mps = 0;
	EpComp in_comp, out_comp;
	uint32_t add = 0;
	uint32_t entries = 1;
	MscDisk* m;

	while (i + 2 <= len) {
		uint8_t l = desc[i];
		uint8_t t = desc[i + 1];
		if (l < 2 || i + l > len)
			break;
		if (t == 4 && l >= 9) {
			cur_msc = desc[i + 5] == MSC_CLASS && desc[i + 6] == MSC_SUBCLASS_SCSI &&
					  desc[i + 7] == MSC_PROTO_BOT;
			if (cur_msc && iface < 0)
				iface = desc[i + 2];
		} else if (cur_msc && t == 5 && l >= 7) {
			uint8_t addr = desc[i + 2];
			uint16_t mps = (uint16_t)(desc[i + 4] | (desc[i + 5] << 8));
			if ((desc[i + 3] & 3) != 2)
				continue;
			if ((addr & 0x80) && !in_addr) {
				in_addr = addr;
				in_mps = mps & 0x7ff;
			} else if (!(addr & 0x80) && !out_addr) {
				out_addr = addr;
				out_mps = mps & 0x7ff;
			}
		}
		i += l;
	}
	if (iface < 0)
		return 0; /* not a BOT mass-storage interface */
	if (!in_addr || !out_addr)
		return -1;

	d->bout.v = (Trb*)dma_page(&d->bout.phys);
	d->bin.v = (Trb*)dma_page(&d->bin.phys);
	d->bin_buf = (uint8_t*)dma_page(&d->bin_phys);
	if (!d->bout.v || !d->bin.v || !d->bin_buf)
		return -1;
	d->bout.enq = 0;
	d->bout.cycle = 1;
	d->bout.dci = (uint8_t)dci_for(out_addr);
	d->bin.enq = 0;
	d->bin.cycle = 1;
	d->bin.dci = (uint8_t)dci_for(in_addr);
	d->bin_mps = in_mps ? in_mps : 512;
	usb_ep_comp(desc, len, out_addr, &out_comp);
	usb_ep_comp(desc, len, in_addr, &in_comp);
	ep_init(d->in_ctx + 32 + ctx_sz * (uint32_t)d->bout.dci, &d->bout, 2,
			out_mps ? out_mps : 512, 0, out_mps ? out_mps : 512, &out_comp);
	ep_init(d->in_ctx + 32 + ctx_sz * (uint32_t)d->bin.dci, &d->bin, 6, d->bin_mps, 0,
			d->bin_mps, &in_comp);
	add = (1u << d->bout.dci) | (1u << d->bin.dci);
	entries = d->bout.dci > d->bin.dci ? d->bout.dci : d->bin.dci;

	if (ctrl(d, 0x00, 9, cfgval, 0, NULL, 0, 0)) {
		UARTDebugOut("[xhci-msc]: set configuration failed\r\n");
		return -1;
	}
	if (configure_eps(d, add, entries)) {
		UARTDebugOut("[xhci-msc]: configure endpoint failed\r\n");
		return -1;
	}

	m = &msc_disks[d - devices];
	m->dev = d;
	m->uas = 0;
	m->lun = 0;
	m->owner = 0;
	m->xport = NULL;
	m->streams = 0;
	m->iface = (uint8_t)iface;
	m->in_addr = in_addr;
	m->out_addr = out_addr;
	m->block_size = 0;
	m->max_lba = 0;
	d->msc = m;

	if (msc_publish(d, m, "msc") < 0)
		return -1;
	msc_publish_luns(d, m, "msc");
	return 1;
}

/* Bind a UAS interface.  Four bulk pipes replace BOT's single pair: command
 * OUT, status IN, data-in and data-out.  The pipe usage descriptor (0x24)
 * names each endpoint; when it is missing, the endpoints are taken in the
 * order controllers and bridges normally emit them. */
static int claim_uas(UsbDev* d, const uint8_t* desc, int len, uint8_t cfgval) {
	static const uint8_t ep_type[UAS_NEP] = { 6, 6, 2 };
	int i = 0;
	int cur_uas = 0;
	int iface = -1;
	uint8_t usage_addr[5] = { 0 };
	uint16_t usage_mps[5] = { 0 };
	uint8_t eps[4] = { 0 };
	uint16_t eps_mps[4] = { 0 };
	int neps = 0;
	uint8_t ep_addr = 0;
	uint16_t ep_mps = 0;
	int have_ep = 0;
	MscDisk* m;
	uint32_t add = 0;
	uint32_t entries = 1;
	int e, f;

	while (i + 2 <= len) {
		uint8_t l = desc[i];
		uint8_t t = desc[i + 1];

		if (l < 2 || i + l > len)
			break;
		if (t == 4 && l >= 9) {
			cur_uas = desc[i + 5] == MSC_CLASS && desc[i + 6] == MSC_SUBCLASS_SCSI &&
					  desc[i + 7] == MSC_PROTO_UAS;
			if (cur_uas && iface < 0)
				iface = desc[i + 2];
			have_ep = 0;
		} else if (cur_uas && t == 5 && l >= 7) {
			ep_addr = desc[i + 2];
			ep_mps = (uint16_t)((desc[i + 4] | (desc[i + 5] << 8)) & 0x7ff);
			have_ep = (desc[i + 3] & 3) == 2;
			if (have_ep && neps < 4) {
				eps[neps] = ep_addr;
				eps_mps[neps] = ep_mps;
				neps++;
			}
		} else if (cur_uas && t == 0x24 && l >= 4 && have_ep) {
			uint8_t id = desc[i + 2];
			if (id >= 1 && id <= 4) {
				usage_addr[id] = ep_addr;
				usage_mps[id] = ep_mps;
			}
			have_ep = 0;
		}
		i += l;
	}
	if (iface < 0)
		return 0; /* not a UAS interface */
	if (!usage_addr[1] && neps == 4) {
		for (e = 0; e < 4; e++) {
			usage_addr[e + 1] = eps[e];
			usage_mps[e + 1] = eps_mps[e];
		}
	}
	if (!usage_addr[1] || !usage_addr[2] || !usage_addr[3] || !usage_addr[4])
		return -1;
	if ((usage_addr[1] & 0x80) || !(usage_addr[2] & 0x80) ||
		!(usage_addr[3] & 0x80) || (usage_addr[4] & 0x80))
		return -1;

	if (d->speed == 4) {
		/* A SuperSpeed UAS device always uses streams; there is no
		 * non-stream fallback in the protocol. */
		if (max_psa < UAS_MAXPSTREAMS) {
			UARTDebugOut("[xhci-uas]: controller has no stream arrays\r\n");
			return -1;
		}
	}

	m = &msc_disks[d - devices];
	m->dev = d;
	m->uas = 1;
	m->lun = 0;
	m->owner = 0;
	m->xport = NULL;
	m->streams = 0;
	m->iface = (uint8_t)iface;
	m->in_addr = usage_addr[2];
	m->out_addr = usage_addr[1];
	m->block_size = 0;
	m->max_lba = 0;
	d->msc = m;

	d->bin_buf = (uint8_t*)dma_page(&d->bin_phys);
	m->cmd.v = (Trb*)dma_page(&m->cmd.phys);
	m->iu_buf = (uint8_t*)dma_page(&m->iu_phys);
	if (!d->bin_buf || !m->cmd.v || !m->iu_buf)
		return -1;
	m->cmd.enq = 0;
	m->cmd.cycle = 1;
	m->cmd.dci = (uint8_t)dci_for(usage_addr[1]);
	{
		EpComp comp;
		uint16_t mps = usage_mps[1] ? usage_mps[1] : 512;

		usb_ep_comp(desc, len, usage_addr[1], &comp);
		ep_init(d->in_ctx + 32 + ctx_sz * (uint32_t)m->cmd.dci, &m->cmd, 2, mps, 0, mps,
				&comp);
	}
	add = 1u << m->cmd.dci;
	entries = m->cmd.dci;

	if (d->speed == 4) {
		m->streams = 1;
		for (e = 0; e < UAS_NEP; e++) {
			uint8_t addr = usage_addr[2 + e];
			uint16_t mps = usage_mps[2 + e] ? usage_mps[2 + e] : 512;
			uint32_t dci = (uint32_t)dci_for(addr);
			uint8_t* sca = (uint8_t*)dma_page(&m->sca_phys[e]);
			uint32_t* se = (uint32_t*)sca;
			EpComp comp;

			if (!sca)
				return -1;
			usb_ep_comp(desc, len, addr, &comp);
			for (f = 1; f <= UAS_TAGS; f++) {
				Ring* r = &m->sring[e][f - 1];
				r->v = (Trb*)dma_page(&r->phys);
				if (!r->v)
					return -1;
				r->enq = 0;
				r->cycle = 1;
				r->dci = (uint8_t)dci;
				r->stream = (uint8_t)f;
				/* Stream context array entries are 16 bytes: the
				 * ring address with SCT_PRI_TR and initial cycle
				 * state, then two reserved dwords. */
				se[4 * f] = (uint32_t)(r->phys | (1u << 1) | 1u);
				se[4 * f + 1] = (uint32_t)(r->phys >> 32);
			}
			clean(sca, PAGE_SIZE);
			ep_init_streams(d->in_ctx + 32 + ctx_sz * dci, ep_type[e], mps,
							m->sca_phys[e], &comp);
			add |= 1u << dci;
			if (dci > entries)
				entries = dci;
		}
		UARTDebugOut("[xhci-uas]: streams enabled, max %d tags %d\r\n", UAS_MAXPSTREAMS,
					 UAS_TAGS);
	} else {
		Ring* rings[UAS_NEP] = { &m->status, &m->din, &m->dout };
		UARTDebugOut("[xhci-uas]: streams off, shared rings\r\n");
		for (e = 0; e < UAS_NEP; e++) {
			uint8_t addr = usage_addr[2 + e];
			uint16_t mps = usage_mps[2 + e] ? usage_mps[2 + e] : 512;
			uint32_t dci = (uint32_t)dci_for(addr);
			EpComp comp;

			rings[e]->v = (Trb*)dma_page(&rings[e]->phys);
			if (!rings[e]->v)
				return -1;
			rings[e]->enq = 0;
			rings[e]->cycle = 1;
			rings[e]->dci = (uint8_t)dci;
			usb_ep_comp(desc, len, addr, &comp);
			ep_init(d->in_ctx + 32 + ctx_sz * dci, rings[e], ep_type[e], mps, 0, mps,
					&comp);
			add |= 1u << dci;
			if (dci > entries)
				entries = dci;
		}
	}

	if (ctrl(d, 0x00, 9, cfgval, 0, NULL, 0, 0)) {
		UARTDebugOut("[xhci-uas]: set configuration failed\r\n");
		return -1;
	}
	if (configure_eps(d, add, entries)) {
		UARTDebugOut("[xhci-uas]: configure endpoint failed\r\n");
		return -1;
	}

	if (msc_publish(d, m, "uas") < 0)
		return -1;
	msc_publish_luns(d, m, "uas");
	return 1;
}

/* ------------------------- USB serial ------------------------- */

#define CDC_CLASS_COMM 2
#define CDC_SUBCLASS_ACM 2
#define CDC_PROTO_AT 1
#define CDC_CLASS_DATA 0x0a
#define FTDI_VID 0x0403
#define SERIAL_KIND_CDC 1
#define SERIAL_KIND_FTDI 2

typedef struct {
	int kind;
	uint8_t comm_iface;
	uint8_t notify_addr;
	uint8_t in_addr;
	uint8_t out_addr;
	uint16_t notify_mps;
	uint16_t in_mps;
	uint16_t out_mps;
	uint8_t notify_interval;
} SerialIface;

/* Match either a CDC ACM pair of interfaces or QEMU's FTDI clone.  Both
 * expose a bulk pair; CDC adds an interrupt notification pipe on the
 * communication interface. */
static int serial_parse(UsbDev* d, const uint8_t* desc, int len, SerialIface* out) {
	int i = 0;
	int comm = 0, data = 0, ftdi = 0;
	int cur_comm = 0, cur_data = 0, cur_ftdi = 0;

	memset(out, 0, sizeof *out);
	while (i + 2 <= len) {
		uint8_t l = desc[i];
		uint8_t t = desc[i + 1];

		if (l < 2 || i + l > len)
			break;
		if (t == 4 && l >= 9) {
			cur_comm = desc[i + 5] == CDC_CLASS_COMM && desc[i + 6] == CDC_SUBCLASS_ACM &&
					   desc[i + 7] == CDC_PROTO_AT;
			cur_data = desc[i + 5] == CDC_CLASS_DATA;
			cur_ftdi = d->vid == FTDI_VID && desc[i + 5] == 0xff;
			if (cur_comm) {
				comm = 1;
				out->comm_iface = desc[i + 2];
			}
			if (cur_data)
				data = 1;
			if (cur_ftdi)
				ftdi = 1;
		} else if (t == 5 && l >= 7) {
			uint8_t addr = desc[i + 2];
			uint8_t attr = desc[i + 3] & 3;
			uint16_t mps = (uint16_t)((desc[i + 4] | (desc[i + 5] << 8)) & 0x7ff);
			uint8_t interval = desc[i + 6];

			if (cur_comm && attr == 3 && (addr & 0x80) && !out->notify_addr) {
				out->notify_addr = addr;
				out->notify_mps = mps;
				out->notify_interval = interval;
			} else if ((cur_data || cur_ftdi) && attr == 2) {
				if ((addr & 0x80) && !out->in_addr) {
					out->in_addr = addr;
					out->in_mps = mps;
				} else if (!(addr & 0x80) && !out->out_addr) {
					out->out_addr = addr;
					out->out_mps = mps;
				}
			}
		}
		i += l;
	}
	if (comm && data && out->in_addr && out->out_addr)
		out->kind = SERIAL_KIND_CDC;
	else if (ftdi && out->in_addr && out->out_addr)
		out->kind = SERIAL_KIND_FTDI;
	return out->kind;
}

static size_t serial_read(AuVFSNode* node, AuVFSNode* file, uint64_t* buffer,
						  uint32_t length) {
	UsbDev* d = node ? (UsbDev*)node->device : NULL;
	int n;

	(void)file;
	if (!d || !buffer || !length)
		return 0;
	if (length > PAGE_SIZE)
		length = PAGE_SIZE;
	n = xhci_bulk_in(d, (uint8_t*)buffer, (uint16_t)length);
	return n > 0 ? (size_t)n : 0;
}

static size_t serial_write(AuVFSNode* node, AuVFSNode* file, uint64_t* buffer,
						   uint32_t length) {
	UsbDev* d = node ? (UsbDev*)node->device : NULL;
	uint32_t done = 0;

	(void)file;
	if (!d || !buffer)
		return 0;
	while (done < length) {
		uint32_t chunk = length - done;
		if (chunk > PAGE_SIZE)
			chunk = PAGE_SIZE;
		if (xhci_bulk_out(d, (const uint8_t*)buffer + done, (uint16_t)chunk))
			break;
		done += chunk;
	}
	return done;
}

static void serial_register(UsbDev* d) {
	int idx = (int)(d - devices);
	AuVFSNode* node = serial_nodes[idx];
	AuVFSNode* devfs;
	int rc;

	if (!node) {
		devfs = AuVFSFind("/dev");
		if (!devfs) {
			UARTDebugOut("[xhci-serial]: no dev file system\r\n");
			return;
		}
		node = (AuVFSNode*)kmalloc(sizeof *node);
		if (!node)
			return;
		memset(node, 0, sizeof *node);
		strcpy(node->filename, "ttyUSB");
		if (idx < 10) {
			node->filename[6] = (char)('0' + idx);
			node->filename[7] = 0;
		} else {
			node->filename[6] = (char)('0' + idx / 10);
			node->filename[7] = (char)('0' + idx % 10);
			node->filename[8] = 0;
		}
		node->flags |= FS_FLAG_DEVICE;
		node->gid = AuCredGetGroupID(AURORA_GID_MISC_WORLD);
		node->read = serial_read;
		node->write = serial_write;
		rc = AuDevFSAddFile(devfs, "/", node);
		if (rc < 1) {
			kfree(node);
			UARTDebugOut("[xhci-serial]: node registration failed\r\n");
			return;
		}
		serial_nodes[idx] = node;
	}
	node->device = d;
	UARTDebugOut("[xhci-serial]: /dev/%s ready\r\n", node->filename);
}

static int cdc_init(UsbDev* d, const SerialIface* si) {
	uint8_t line[7];

	memset(line, 0, sizeof line);
	line[0] = 0x00; /* 115200 baud, little endian */
	line[1] = 0xc2;
	line[2] = 0x01;
	line[3] = 0x00;
	line[4] = 0; /* one stop bit */
	line[5] = 0; /* no parity */
	line[6] = 8;
	if (ctrl(d, 0x21, 0x20, 0, si->comm_iface, line, sizeof line, 0)) {
		UARTDebugOut("[xhci-serial]: set line coding failed\r\n");
		return -1;
	}
	if (ctrl(d, 0x21, 0x22, 3, si->comm_iface, NULL, 0, 0)) {
		UARTDebugOut("[xhci-serial]: set control lines failed\r\n");
		return -1;
	}
	return 0;
}

/* FTDI's vendor requests are small enough to spell out: reset, baud
 * divisor, 8N1, no flow control, then assert DTR and RTS. */
static int ftdi_init(UsbDev* d) {
	if (ctrl(d, 0x40, 0, 0, 0, NULL, 0, 0) || /* reset */
		ctrl(d, 0x40, 3, 26, 0, NULL, 0, 0) || /* ~115200 baud */
		ctrl(d, 0x40, 4, 8, 0, NULL, 0, 0) ||  /* 8 data bits, no parity */
		ctrl(d, 0x40, 2, 0, 0, NULL, 0, 0) ||  /* no flow control */
		ctrl(d, 0x40, 1, 0x300, 0, NULL, 0, 0)) { /* DTR | RTS */
		UARTDebugOut("[xhci-serial]: ftdi init failed\r\n");
		return -1;
	}
	return 0;
}

static int claim_serial(UsbDev* d, const uint8_t* desc, int len, uint8_t cfgval) {
	SerialIface si;
	EpComp in_comp, out_comp;
	uint32_t add, entries;

	if (!serial_parse(d, desc, len, &si))
		return 0;

	d->bout.v = (Trb*)dma_page(&d->bout.phys);
	d->bin.v = (Trb*)dma_page(&d->bin.phys);
	d->bin_buf = (uint8_t*)dma_page(&d->bin_phys);
	if (!d->bout.v || !d->bin.v || !d->bin_buf)
		return -1;
	d->bout.enq = 0;
	d->bout.cycle = 1;
	d->bout.dci = (uint8_t)dci_for(si.out_addr);
	d->bin.enq = 0;
	d->bin.cycle = 1;
	d->bin.dci = (uint8_t)dci_for(si.in_addr);
	d->bin_mps = si.in_mps ? si.in_mps : 64;
	usb_ep_comp(desc, len, si.out_addr, &out_comp);
	usb_ep_comp(desc, len, si.in_addr, &in_comp);
	ep_init(d->in_ctx + 32 + ctx_sz * (uint32_t)d->bout.dci, &d->bout, 2,
			si.out_mps ? si.out_mps : 64, 0, si.out_mps ? si.out_mps : 64, &out_comp);
	ep_init(d->in_ctx + 32 + ctx_sz * (uint32_t)d->bin.dci, &d->bin, 6, d->bin_mps, 0,
			d->bin_mps, &in_comp);
	add = (1u << d->bout.dci) | (1u << d->bin.dci);
	entries = d->bout.dci > d->bin.dci ? d->bout.dci : d->bin.dci;

	if (si.kind == SERIAL_KIND_CDC) {
		EpComp notify_comp;
		int ndci = dci_for(si.notify_addr);
		uint32_t iv;

		d->intr.v = (Trb*)dma_page(&d->intr.phys);
		d->intr_buf = (uint8_t*)dma_page(&d->intr_phys);
		if (!d->intr.v || !d->intr_buf)
			return -1;
		d->intr.enq = 0;
		d->intr.cycle = 1;
		d->intr.dci = (uint8_t)ndci;
		d->intr_mps = si.notify_mps ? si.notify_mps : 8;
		d->intr_interval = si.notify_interval;
		iv = d->speed >= 3 ? (uint32_t)(si.notify_interval ? si.notify_interval - 1 : 0)
						   : si.notify_interval;
		if (iv > 15)
			iv = 15;
		usb_ep_comp(desc, len, si.notify_addr, &notify_comp);
		ep_init(d->in_ctx + 32 + ctx_sz * (uint32_t)ndci, &d->intr, 7, d->intr_mps, iv,
				d->intr_mps, &notify_comp);
		add |= 1u << ndci;
		if ((uint32_t)ndci > entries)
			entries = (uint32_t)ndci;
	}

	if (ctrl(d, 0x00, 9, cfgval, 0, NULL, 0, 0)) {
		UARTDebugOut("[xhci-serial]: set configuration failed\r\n");
		return -1;
	}
	if (configure_eps(d, add, entries)) {
		UARTDebugOut("[xhci-serial]: configure endpoint failed\r\n");
		return -1;
	}
	if (si.kind == SERIAL_KIND_CDC ? cdc_init(d, &si) : ftdi_init(d))
		return -1;

	d->serial_kind = si.kind;
	d->serial_iface = si.comm_iface;
	serial_register(d);
	UARTDebugOut("[xhci-serial]: %s ready on port %d slot %d ep %x/%x\r\n",
				 si.kind == SERIAL_KIND_CDC ? "CDC ACM" : "FTDI", d->port, d->slot,
				 si.in_addr, si.out_addr);
	return 1;
}

typedef struct {
	int found;
	uint8_t iface;
	uint8_t ep_addr;
	uint16_t ep_mps;
	uint8_t ep_interval;
} HubIface;

static int hub_parse(const uint8_t* desc, int len, HubIface* out) {
	int i = 0;
	int cur_hub = 0;
	memset(out, 0, sizeof *out);
	while (i + 2 <= len) {
		uint8_t l = desc[i];
		uint8_t t = desc[i + 1];
		if (l < 2 || i + l > len)
			break;
		if (t == 4 && l >= 9) {
			cur_hub = desc[i + 5] == HUB_CLASS;
			if (cur_hub) {
				out->found = 1;
				out->iface = desc[i + 2];
			}
		} else if (t == 5 && l >= 7 && cur_hub && !out->ep_addr) {
			uint8_t addr = desc[i + 2];
			uint8_t attr = desc[i + 3] & 3;
			if (attr == 3 && (addr & 0x80)) {
				out->ep_addr = addr;
				out->ep_mps = (uint16_t)(desc[i + 4] | (desc[i + 5] << 8));
				out->ep_mps &= 0x7ff;
				out->ep_interval = desc[i + 6];
			}
		}
		i += l;
	}
	return out->found && out->ep_addr;
}

/* Tell the controller that this slot is a hub.  The hub descriptor is
 * only available after the device is addressed, so the slot context is
 * updated with Configure Endpoint: Hub bit, maximum downstream ports and
 * the TT think time.  USB 2.0 multi-TT hubs also set the MTT bit. */
static int hub_update_context(UsbDev* d, const uint8_t* hd, int dci) {
	uint32_t* s = (uint32_t*)(d->in_ctx + 32);
	uint32_t think = ((uint32_t)hd[3] >> 5) & 3u;

	s[0] |= 1u << 26;
	if (hd[3] & 0x02)
		s[0] |= 1u << 25;
	else if (d->speed == 1 || d->speed == 2)
		s[0] &= ~(1u << 25);
	s[1] = (s[1] & ~(0xffu << 24)) | ((uint32_t)d->hub_ports << 24);
	if (think)
		s[2] = (s[2] & ~(3u << 16)) | (think << 16);
	if (configure_eps(d, 0, (uint32_t)dci)) {
		char path[24];
		usb_port_path(d, path, sizeof path);
		UARTDebugOut("[xhci]: hub context update failed on port %s\r\n", path);
		return -1;
	}
	return 0;
}

/* Bind a USB 2.0 or USB 3.0 hub.  The hub descriptor and the per-port
 * power requests are class-specific, so this cannot share the HID parser.
 * Downstream devices are enumerated later from the status-change bitmap. */
static int claim_hub(UsbDev* d, const uint8_t* desc, int len, uint8_t cfgval) {
	HubIface hi;
	EpComp comp;
	uint8_t hd[9];
	int dci;
	uint32_t iv;
	uint8_t desc_type;
	int p;

	if (!hub_parse(desc, len, &hi))
		return 0; /* not a hub interface */

	dci = dci_for(hi.ep_addr);
	d->intr.v = (Trb*)dma_page(&d->intr.phys);
	d->intr_buf = (uint8_t*)dma_page(&d->intr_phys);
	if (!d->intr.v || !d->intr_buf)
		return -1;
	d->intr.enq = 0;
	d->intr.cycle = 1;
	d->intr.dci = (uint8_t)dci;
	d->intr_mps = hi.ep_mps ? hi.ep_mps : 2;
	d->intr_interval = hi.ep_interval;

	if (d->speed >= 3)
		iv = hi.ep_interval ? (uint32_t)(hi.ep_interval - 1) : 0;
	else
		iv = hi.ep_interval;
	if (iv > 15)
		iv = 15;

	if (ctrl(d, 0x00, 9, cfgval, 0, NULL, 0, 0)) {
		UARTDebugOut("[xhci]: hub set configuration failed\r\n");
		return -1;
	}

	/* USB 3 hubs use descriptor type 0x2A; the port count and
	 * characteristics stay at the same offsets as the USB 2.0 layout. */
	desc_type = d->speed >= 4 ? 0x2a : HUB_DESC_TYPE;
	memset(hd, 0, sizeof hd);
	if (ctrl(d, 0xa0, 6, (uint16_t)(desc_type << 8), 0, hd, sizeof hd, 1)) {
		UARTDebugOut("[xhci]: hub descriptor read failed\r\n");
		return -1;
	}
	if (hd[1] != desc_type || (hd[2] & 0x0f) == 0) {
		UARTDebugOut("[xhci]: invalid hub descriptor %x/%x\r\n", hd[1], hd[2]);
		return -1;
	}
	d->hub_ports = hd[2];
	if (d->hub_ports > HUB_MAX_PORTS)
		d->hub_ports = HUB_MAX_PORTS;
	/* The status-change bitmap is one bit per port plus the hub bit, not
	 * one endpoint packet; hubs with many ports span several packets. */
	d->intr_len = (uint16_t)((d->hub_ports + 1 + 7) / 8);

	usb_ep_comp(desc, len, hi.ep_addr, &comp);
	ep_init(d->in_ctx + 32 + ctx_sz * (uint32_t)dci, &d->intr, 7,
			d->intr_mps, iv, d->intr_mps, &comp);
	if (configure_eps(d, 1u << dci, (uint32_t)dci)) {
		UARTDebugOut("[xhci]: hub configure endpoint failed\r\n");
		return -1;
	}
	if (d->speed >= 4 && ctrl(d, 0x20, 0x0c, (uint16_t)d->tier, 0, NULL, 0, 0))
		UARTDebugOut("[xhci]: set hub depth failed on port %d\r\n", d->port);
	if (hub_update_context(d, hd, dci))
		return -1;
	UARTDebugOut("[xhci]: hub context slot %d ports %d mtt %d tt %d\r\n",
				 d->slot, d->hub_ports, (hd[3] & 0x02) != 0,
				 (int)((hd[3] >> 5) & 3));

	/* Power every downstream port.  QEMU already powers them on reset,
	 * but a real hub needs the explicit request before it reports a
	 * connect and before a reset is accepted.  bPwrOn2PwrGood is in 2 ms
	 * units; wait at least 20 ms for a hub that reports no delay. */
	for (p = 1; p <= d->hub_ports; p++)
		if (ctrl(d, 0x23, 3, HUB_PORT_POWER, (uint16_t)p, NULL, 0, 0))
			UARTDebugOut("[xhci]: hub power port %d failed\r\n", p);
	{
		uint32_t wait = (uint32_t)hd[5] * 2;
		spin_ms(wait > 20 ? wait : 20);
	}

	d->is_hub = 1;
	arm_intr(d);
	{
		char path[24];
		usb_port_path(d, path, sizeof path);
		UARTDebugOut("[xhci]: hub ready on port %s slot %d ports %d\r\n",
					 path, d->slot, d->hub_ports);
	}
	return 1;
}

/* enum_port() verdicts. The scan must tell "nothing attached" (keep polling
 * cheaply) apart from "a device answered" -- re-enumerating a port that already
 * answered is what used to stall boot. */
#define XCT_NONE 0  /* nothing connected on this port */
#define XCT_DEV 1   /* supported class claimed */
#define XCT_NOTSUP 2 /* a device answered, but no driver wanted it */
#define XCT_FAIL -1 /* enumeration error, worth one retry */

/* Built-in USB class drivers, tried in order. A probe returns 1 when it
 * claimed the device, 0 when the interface is not its class, and -1 when
 * the device matched but could not be configured. */
typedef struct {
	const char* name;
	int (*probe)(UsbDev* d, const uint8_t* desc, int len, uint8_t cfgval);
} UsbClassDriver;

static const UsbClassDriver usb_classes[] = {
	{ "hub", claim_hub },
	{ "hid", claim_hid },
	{ "bt", claim_bt },
	{ "serial", claim_serial },
	{ "uas", claim_uas },
	{ "msc", claim_msc },
};

static UsbDev* dev_alloc(void) {
	int i;
	for (i = 0; i < XHCI_MAX_DEV; i++) {
		if (devices[i].state == XDEV_FREE) {
			devices[i].state = XDEV_ENUM;
			return &devices[i];
		}
	}
	return NULL;
}

/* Caller holds usb_lock.  route 0 is a device on the root port itself. */
static UsbDev* dev_for_route_locked(int port, uint32_t route) {
	int i;
	for (i = 0; i < XHCI_MAX_DEV; i++) {
		UsbDev* d = &devices[i];
		if (d->state == XDEV_READY && d->port == port && d->route == route)
			return d;
	}
	return NULL;
}

/* Enumerate one device at root_port/route.  Hub children enter through the
 * same path as root-port devices; only their reset and address differ. */
static int enum_device(int root_port, uint32_t route, UsbDev* parent, int parent_port,
					   int speed) {
	uint8_t devdesc[18];
	uint8_t cfg_buf[512];
	uint8_t* cfg = cfg_buf;
	uint8_t* heap_cfg = NULL;
	uint16_t total;
	uint16_t cfg_len;
	int verdict = XCT_NOTSUP;
	int code = 0;
	int i;
	int r;
	UsbDev* d;
	uint8_t cfgval;
	uint64_t cmd;

	d = dev_alloc();
	if (!d) {
		UARTDebugOut("[xhci]: no free device slot for port %d\r\n", root_port);
		return XCT_NOTSUP;
	}
	memset(d, 0, sizeof *d);
	d->state = XDEV_ENUM;
	d->port = (uint8_t)root_port;
	d->route = route & 0xfffffu;
	d->parent = parent ? (uint8_t)(parent - devices + 1) : 0;
	d->parent_port = (uint8_t)parent_port;
	d->tier = parent ? (uint8_t)(parent->tier + 1) : 0;
	d->speed = (uint8_t)speed;
	d->in_ctx = (uint8_t*)dma_page(&d->in_phys);
	d->out_ctx = (uint8_t*)dma_page(&d->out_phys);
	d->xfer = (uint8_t*)dma_page(&d->xfer_phys);
	if (!d->in_ctx || !d->out_ctx || !d->xfer) {
		enum_abort(d);
		return XCT_FAIL;
	}

	usb_lock();
	cmd = ring_push(&cmdring, 0, 0, 0, TRB_ENABLE_SLOT << 10);
	door(0, 0);
	if (wait_cmd(cmd, &d->slot, &code) || code != 1 || d->slot == 0) {
		usb_unlock();
		UARTDebugOut("[xhci]: enable slot completion %d\r\n", code);
		enum_abort(d);
		return XCT_FAIL;
	}
	usb_unlock();

	if (address_at(d)) {
		enum_abort(d);
		return XCT_FAIL;
	}
	memset(devdesc, 0, sizeof devdesc);
	if (ctrl(d, 0x80, 6, 0x0100, 0, devdesc, 8, 1)) {
		enum_abort(d);
		return XCT_FAIL;
	}
	if (d->speed == 1) {
		uint16_t mps = (uint16_t)devdesc[7];
		if ((mps == 16 || mps == 32 || mps == 64) && ep0_set_mps(d, mps)) {
			enum_abort(d);
			return XCT_FAIL;
		}
	}
	if (ctrl(d, 0x80, 6, 0x0100, 0, devdesc, 18, 1)) {
		enum_abort(d);
		return XCT_FAIL;
	}
	usb_read_strings(d, devdesc);
	if (ctrl(d, 0x80, 6, 0x0200, 0, cfg, 9, 1)) {
		enum_abort(d);
		return XCT_FAIL;
	}
	total = (uint16_t)(cfg[2] | (cfg[3] << 8));
	if (total < 9) {
		enum_abort(d);
		return XCT_NOTSUP;
	}
	/* The class interfaces can sit past any prefix, so keep the whole
	 * configuration descriptor.  Large sets get their own DMA block in
	 * ctrl(); between the stack buffer and that path, use the heap. */
	cfg_len = total;
	if (cfg_len > sizeof cfg_buf) {
		heap_cfg = (uint8_t*)kmalloc(cfg_len);
		if (!heap_cfg) {
			enum_abort(d);
			return XCT_FAIL;
		}
		cfg = heap_cfg;
	}
	if (ctrl(d, 0x80, 6, 0x0200, 0, cfg, cfg_len, 1)) {
		verdict = XCT_FAIL;
		goto out;
	}
	cfgval = cfg[5];
	d->vid = (uint16_t)(devdesc[8] | (devdesc[9] << 8));
	d->pid = (uint16_t)(devdesc[10] | (devdesc[11] << 8));
	UARTDebugOut("[xhci]: port %d route %x dev %x:%x\r\n", root_port,
				 (unsigned)d->route, d->vid, d->pid);
	UARTDebugOut("[xhci]: class %x/%x/%x mps0 %x cfg %d total %d\r\n",
				 devdesc[4], devdesc[5], devdesc[6], devdesc[7], cfgval, total);
	if (d->speed >= 4) {
		UARTDebugOut("[xhci]: superspeed device on port %d slot %d\r\n",
					 root_port, d->slot);
		usb_log_companions(cfg, cfg_len);
	}

	for (i = 0; i < (int)(sizeof usb_classes / sizeof usb_classes[0]); i++) {
		r = usb_classes[i].probe(d, cfg, cfg_len, cfgval);
		if (r == 1) {
			d->state = XDEV_READY;
			verdict = XCT_DEV;
			goto out;
		}
		if (r < 0) {
			UARTDebugOut("[xhci]: %s probe failed on port %d\r\n",
						 usb_classes[i].name, root_port);
			verdict = XCT_FAIL;
			goto out;
		}
	}

	UARTDebugOut("[xhci]: port %d route %x has no supported interface\r\n",
				 root_port, (unsigned)d->route);
out:
	if (heap_cfg)
		kfree(heap_cfg);
	if (verdict != XCT_DEV)
		enum_abort(d);
	return verdict;
}

/* A device on a root port: reset the port, then run the common enumeration
 * with route string zero and no parent hub. */
static int enum_port(int port) {
	int speed;

	if (!port_reset(port))
		return XCT_NONE;
	speed = speed_of(port);
	if (speed == 0)
		speed = 1;
	return enum_device(port, 0, NULL, 0, speed);
}

/* Stop a published device, wait for a reader that already took a reference,
 * then release its slot and DMA pages. The static device array stays valid,
 * so a stale BT bridge pointer cannot dereference freed memory. */
static void dev_disconnect(UsbDev* d) {
	char path[24];
	uint32_t route;
	int was_bt;
	int idx;
	int i;

	usb_lock();
	if (d->state != XDEV_READY) {
		usb_unlock();
		return;
	}
	d->state = XDEV_CLOSING;
	usb_port_path(d, path, sizeof path);
	route = d->route;
	was_bt = d->bt;
	idx = (int)(d - devices) + 1;
	usb_unlock();

	/* A hub owns every device below it.  Tear the tree down from the
	 * leaves so a child never outlives the slot its route points at. */
	for (i = 0; i < XHCI_MAX_DEV; i++) {
		UsbDev* child = &devices[i];
		if (child->parent == idx && child->state == XDEV_READY)
			dev_disconnect(child);
	}

	if (was_bt)
		AuBtUsbDetach();

	while (__atomic_load_n(&d->refs, __ATOMIC_ACQUIRE) != 0)
		spin_ms(1);

	/* Drop the volumes before the transport goes away.  The fs node and
	 * its FatFS are deliberately kept: a re-plug reuses the same vdisk,
	 * and an open file descriptor may still point at the node. */
	if (d->msc && d->msc->vd)
		AuVDiskSetOnline(d->msc->vd, 0);
	for (i = 0; i < XHCI_MAX_EXTRA_LUN; i++)
		if (msc_extra[i].dev == d && msc_extra[i].vd)
			AuVDiskSetOnline(msc_extra[i].vd, 0);

	usb_lock();
	dev_release_locked(d);
	usb_unlock();
	UARTDebugOut("[xhci]: device on port %s route %x released\r\n",
				 path, (unsigned)route);
}

static uint16_t hub_le16(const uint8_t* p) {
	return (uint16_t)(p[0] | (p[1] << 8));
}

static int hub_port_status(UsbDev* hub, int port, uint8_t st[4]) {
	memset(st, 0, 4);
	return ctrl(hub, 0xa3, 0, 0, (uint16_t)port, st, 4, 1);
}

static void hub_clear_feature(UsbDev* hub, int port, uint16_t feature) {
	char path[32];
	int rc = ctrl(hub, 0x23, 1, feature, (uint16_t)port, NULL, 0, 0);
	if (rc) {
		usb_child_path(hub, port, path, sizeof path);
		UARTDebugOut("[xhci-hub]: clear %x port %s rc %d\r\n",
					 feature, path, rc);
	}
}

static void hub_set_feature(UsbDev* hub, int port, uint16_t feature) {
	ctrl(hub, 0x23, 3, feature, (uint16_t)port, NULL, 0, 0);
}

/* Reset a downstream port and return the speed of what is now enabled on
 * it.  A hub reports the low/high-speed bit in wPortStatus; a USB 2.0 hub
 * port is otherwise full speed. */
static int hub_port_reset(UsbDev* hub, int port, int* speed) {
	uint8_t st[4];
	uint16_t ws;
	/* A SuperSpeed link trains and retrains, so give it more time than a
	 * USB 2.0 port needs (100 ms). */
	int tries = hub->speed >= 4 ? 100 : 20;
	int i;

	hub_set_feature(hub, port, HUB_PORT_RESET);
	for (i = 0; i < tries; i++) {
		if (hub_port_status(hub, port, st) == 0 &&
			(hub_le16(st + 2) & (1u << HUB_CHANGE_RESET)))
			break;
		spin_ms(5);
	}
	/* TRSTRCY is 10 ms for USB 2.0. */
	spin_ms(10);
	hub_clear_feature(hub, port, HUB_CLEAR_RESET);
	hub_clear_feature(hub, port, HUB_CLEAR_ENABLE);
	hub_clear_feature(hub, port, HUB_CLEAR_CONNECTION);

	if (hub_port_status(hub, port, st))
		return 0;
	ws = hub_le16(st);
	if (!(ws & (1u << HUB_PORT_CONNECTION)) ||
		!(ws & (1u << HUB_PORT_ENABLE)))
		return 0;
	if (hub->speed >= 4)
		*speed = 4; /* a SuperSpeed hub port has no speed bits */
	else if (ws & (1u << 9))
		*speed = 2;
	else if (ws & (1u << 10))
		*speed = 3;
	else
		*speed = 1;
	return 1;
}

/* Reset one downstream port and enumerate whatever answered.  The route
 * string gains this hub's port number at the hub's tier. */
static int hub_enum_child(UsbDev* hub, int port) {
	uint32_t route = hub->route | ((uint32_t)port << (4 * hub->tier));
	int speed;
	int attempt;
	int r = XCT_NONE;

	if (!hub_port_reset(hub, port, &speed))
		return XCT_NONE;
	for (attempt = 0; attempt < 3; attempt++) {
		r = enum_device(hub->port, route, hub, port, speed);
		if (r != XCT_FAIL)
			break;
		spin_ms(20);
	}
	if (r == XCT_DEV) {
		char path[32];
		usb_child_path(hub, port, path, sizeof path);
		UARTDebugOut("[xhci-hotplug]: enumerated port %s\r\n", path);
		UARTDebugOut("[xhci-hub]: child enumerated on port %s\r\n", path);
	} else if (r == XCT_FAIL) {
		char path[32];
		usb_child_path(hub, port, path, sizeof path);
		UARTDebugOut("[xhci-hub]: enumeration failed on port %s\r\n", path);
	}
	return r;
}

/* Handle one status-change bit.  The bitmap reports which ports changed,
 * not what changed, so read wPortStatus/wPortChange and acknowledge every
 * change bit that was consumed. */
static void hub_port_changed(UsbDev* hub, int port) {
	uint8_t st[4];
	uint16_t ws, wc;
	uint32_t route = hub->route | ((uint32_t)port << (4 * hub->tier));
	UsbDev* child;
	char path[32];

	usb_child_path(hub, port, path, sizeof path);
	if (hub_port_status(hub, port, st))
		return;
	ws = hub_le16(st);
	wc = hub_le16(st + 2);
	UARTDebugOut("[xhci-hub]: port %s status %x change %x\r\n",
				 path, ws, wc);

	if (wc & (1u << HUB_CHANGE_CONNECTION)) {
		hub_clear_feature(hub, port, HUB_CLEAR_CONNECTION);
		if (!(ws & (1u << HUB_PORT_CONNECTION))) {
			usb_lock();
			child = dev_for_route_locked(hub->port, route);
			usb_unlock();
			if (child) {
				UARTDebugOut("[xhci-hotplug]: disconnect on port %s\r\n",
							 path);
				dev_disconnect(child);
			}
			if (wc & (1u << HUB_CHANGE_ENABLE))
				hub_clear_feature(hub, port, HUB_CLEAR_ENABLE);
			return;
		}
	}
	if (wc & (1u << HUB_CHANGE_ENABLE))
		hub_clear_feature(hub, port, HUB_CLEAR_ENABLE);
	if (wc & (1u << HUB_CHANGE_RESET))
		hub_clear_feature(hub, port, HUB_CLEAR_RESET);
	if (wc & (1u << HUB_CHANGE_OVERCURRENT)) {
		hub_clear_feature(hub, port, HUB_CLEAR_OVERCURRENT);
		UARTDebugOut("[xhci-hub]: port %s overcurrent\r\n", path);
		return;
	}

	if (!(ws & (1u << HUB_PORT_CONNECTION)))
		return;

	usb_lock();
	child = dev_for_route_locked(hub->port, route);
	usb_unlock();
	if (child)
		return;

	hub_enum_child(hub, port);
}

/* Boot path: QEMU's hub port-power timer can fire before the guest reaches
 * the poll thread, so read each downstream port directly instead of waiting
 * for a status-change report. */
static int hub_probe_ports(UsbDev* hub) {
	uint8_t st[4];
	uint16_t ws;
	uint32_t route;
	UsbDev* child;
	int port;
	int added = 0;

	for (port = 1; port <= hub->hub_ports; port++) {
		if (hub_port_status(hub, port, st))
			continue;
		ws = hub_le16(st);
		if (!(ws & (1u << HUB_PORT_CONNECTION)))
			continue;
		route = hub->route | ((uint32_t)port << (4 * hub->tier));
		usb_lock();
		child = dev_for_route_locked(hub->port, route);
		usb_unlock();
		if (child)
			continue;
		if (hub_enum_child(hub, port) == XCT_DEV)
			added++;
	}
	return added;
}

static void hub_poll(UsbDev* d) {
	uint8_t rep[32]; /* 255 ports plus the hub bit, one bit each */
	int n;
	int p;

	if (!d->is_hub || d->state != XDEV_READY)
		return;
	n = intr_xfer(d, rep, sizeof rep);
	if (n <= 0)
		return;
	/* Bit 0 reports a hub-wide change, which has no driver action here.
	 * Test each port's bit directly so a bitmap wider than 32 bits does
	 * not wrap. */
	for (p = 1; p <= (int)d->hub_ports; p++) {
		int byte = p / 8;
		if (byte < n && (rep[byte] & (uint8_t)(1u << (p & 7))))
			hub_port_changed(d, p);
	}
}

/* Used during boot: the status endpoint may not have a completion queued
 * yet, so probe every downstream port directly.  A newly found hub is
 * picked up by the next pass, up to the five-tier topology limit. */
static void xhci_hub_scan(void) {
	int pass;
	int i;
	int hubs;
	int added;

	for (pass = 0; pass < HUB_MAX_TIER; pass++) {
		hubs = 0;
		added = 0;
		for (i = 0; i < XHCI_MAX_DEV; i++) {
			UsbDev* d = &devices[i];
			if (d->state == XDEV_READY && d->is_hub) {
				added += hub_probe_ports(d);
				hub_poll(d);
				hubs++;
			}
		}
		if (!hubs || !added)
			break;
		spin_ms(20);
	}
}

static void xhci_port_scan(void) {
	int p;
	for (p = 1; p <= (int)nports; p++) {
		uint32_t base = caplen + 0x400 + (uint32_t)(p - 1) * 16;
		uint32_t sc = rd(base);
		int known;

		if (!(sc & PORTSC_CSC))
			continue;

		usb_lock();
		known = dev_for_route_locked(p, 0) != NULL;
		usb_unlock();

		/* Acknowledge before touching the port, then again after
		 * the reset, which raises PRC/CSC itself. */
		wr(base, sc & PORTSC_CHANGE);

		if (sc & PORTSC_CCS) {
			int attempt;
			if (known)
				continue;
			UARTDebugOut("[xhci-hotplug]: connect on port %d\r\n", p);
			for (attempt = 0; attempt < 3; attempt++) {
				if (enum_port(p) != XCT_FAIL)
					break;
				spin_ms(20);
			}
			sc = rd(base);
			wr(base, sc & PORTSC_CHANGE);
			usb_lock();
			known = dev_for_route_locked(p, 0) != NULL;
			usb_unlock();
			if (known)
				UARTDebugOut("[xhci-hotplug]: enumerated port %d\r\n", p);
		} else if (known) {
			UsbDev* d;
			UARTDebugOut("[xhci-hotplug]: disconnect on port %d\r\n", p);
			usb_lock();
			d = dev_for_route_locked(p, 0);
			usb_unlock();
			if (d)
				dev_disconnect(d);
		}
	}
}

/* Interrupter 0 starts 0x20 bytes into the runtime register space.  IMAN.IP
 * is RW1C, so writing it back clears the level that drove the MSI.  The event
 * ring itself is drained by the polling paths; this handler only acknowledges
 * the interrupt so the controller can lower it. */
static void xhci_irq(int spi) {
	static volatile int seen;
	(void)spi;
	if (!rtsoff)
		return;
	wr(rtsoff + 0x20, rd(rtsoff + 0x20) | 1u);
	if (!seen) {
		seen = 1;
		UARTDebugOut("[xhci]: irq delivered\r\n");
	}
}

/* xHCI extended capability IDs.  The list is located through
 * HCCPARAMS1[31:16]; each entry's next pointer is a dword offset from its
 * own header, so a malformed list needs the iteration guard as well as the
 * BAR-size check. */
static const char* xhci_ext_name(uint8_t id) {
	switch (id) {
	case 1:
		return "legacy";
	case 2:
		return "protocol";
	case 3:
		return "power";
	case 4:
		return "virtualization";
	case 5:
		return "message";
	case 6:
		return "local-memory";
	case 10:
		return "debug";
	default:
		return "unknown";
	}
}

/* Take the controller away from firmware.  A BIOS may still own the legacy
 * semaphore after boot; set OS owned, wait for BIOS to drop its bit, then
 * turn off the SMI enables and clear their status latches. */
static void xhci_legacy_handoff(uint32_t off) {
	uint32_t sem;
	int i;

	if (off + 12 > mmio_bytes)
		return;
	sem = rd(off + 4);
	if (sem & (1u << 16)) {
		wr(off + 4, sem | (1u << 24));
		for (i = 0; i < 100; i++) {
			if (!(rd(off + 4) & (1u << 16)))
				break;
			spin_ms(1);
		}
		UARTDebugOut("[xhci]: legacy handoff %s\r\n",
					 (rd(off + 4) & (1u << 16)) ? "timeout" : "ok");
	} else {
		UARTDebugOut("[xhci]: legacy support free\r\n");
	}
	/* USBLEGCTLSTS: write ones to the SMI status bits to clear them and
	 * zero the enable bits so firmware cannot take the controller back. */
	wr(off + 8, 0x70u);
}

static void xhci_supported_protocol(uint32_t off) {
	uint32_t name = rd(off + 4);
	uint32_t ports = rd(off + 8);
	uint32_t slot = rd(off + 12);
	char proto[5];
	uint8_t start, count, psic;
	int i;

	memcpy(proto, &name, 4);
	proto[4] = 0;
	for (i = 3; i >= 0 && proto[i] == ' '; i--)
		proto[i] = 0;
	start = (uint8_t)(ports & 0xff);
	count = (uint8_t)((ports >> 8) & 0xff);
	psic = (uint8_t)((ports >> 28) & 0xf);
	UARTDebugOut("[xhci]: protocol %s ports %d+%d psi %d slots %d\r\n", proto, start, count,
				 (int)psic, (int)(slot & 0x1f));
	for (i = 0; i < psic && off + 16 + (uint32_t)i * 4 < mmio_bytes; i++) {
		uint32_t psi = rd(off + 16 + (uint32_t)i * 4);
		UARTDebugOut("[xhci]: protocol %s speed id %d link %x\r\n", proto, (int)(psi & 0xf),
					 (int)((psi >> 16) & 0xffff));
	}
}

static void xhci_ext_caps(uint32_t off) {
	int guard;

	if (!off || off >= mmio_bytes)
		return;
	for (guard = 0; guard < 64 && off; guard++) {
		uint32_t v;
		uint8_t id, next;

		if (off + 16 > mmio_bytes)
			break;
		v = rd(off);
		id = (uint8_t)(v & 0xff);
		next = (uint8_t)((v >> 8) & 0xff);
		UARTDebugOut("[xhci]: ext cap %d %s\r\n", id, xhci_ext_name(id));
		if (id == 1)
			xhci_legacy_handoff(off);
		else if (id == 2)
			xhci_supported_protocol(off);
		if (!next)
			break;
		off += (uint32_t)next * 4;
	}
}

static int bringup(void) {
	uint32_t hcs1, hcc1, hcc2, cmd, sts;
	uint32_t slots;
	uint64_t erst_phys = 0;
	uint32_t* erst;
	int i;
	uint64_t bar;
	size_t barsz = 0;

	bar = AuPCIEReadBAR(pci, (uint16_t)bus, (uint16_t)devn, (uint16_t)func, 0, &barsz);
	if (bar == 0)
		return -1;
	bar &= ~0xfull;
	if (barsz < PAGE_SIZE)
		barsz = 16 * PAGE_SIZE;
	mmio_bytes = barsz > 0xffffffffull ? 0xffffffffu : (uint32_t)barsz;
	mmio = (volatile uint32_t*)AuMapMMIO(bar, (barsz + PAGE_SIZE - 1) / PAGE_SIZE);
	if (!mmio)
		return -1;
	caplen = rd(0) & 0xff;
	hcs1 = rd(0x04);
	hcc1 = rd(0x10);
	hcc2 = rd(0x1c);
	/* AC64 clear means the controller can only reach the low 4 GiB. */
	if (!(hcc1 & 1u)) {
		dma_ceiling = 0xffffffffull;
		UARTDebugOut("[xhci]: 32-bit dma only\r\n");
	}
	page_bytes = controller_page_bytes(rd(caplen + 8));
	if (page_bytes != PAGE_SIZE)
		UARTDebugOut("[xhci]: page size %d\r\n", (int)page_bytes);
	dboff = rd(0x14) & ~3u;
	rtsoff = rd(0x18) & ~0x1fu;
	nports = (hcs1 >> 24) & 0xff;
	slots = hcs1 & 0xff;
	if (slots == 0)
		slots = 8;
	else if (slots > XHCI_MAX_DEV)
		slots = XHCI_MAX_DEV;
	ctx_sz = (hcc1 & 4u) ? 64 : 32;
	max_psa = (hcc1 >> 12) & 0xfu;
	if (nports == 0 || nports > XHCI_MAX_PORTS)
		return -1;
	/* HCCPARAMS1[31:16] points at the extended capability list in dwords;
	 * CAPLENGTH only locates the operational registers. */
	xhci_ext_caps((uint32_t)((hcc1 >> 16) & 0xffff) << 2);
	UARTDebugOut("[xhci]: primary stream arrays %d\r\n", (int)(2u << max_psa));

	cmd = rd(caplen);
	sts = rd(caplen + 4);
	if ((sts & 1u) == 0) {
		wr(caplen, cmd & ~1u);
		for (i = 0; i < 200; i++) {
			if (rd(caplen + 4) & 1u)
				break;
			spin_ms(1);
		}
	}
	wr(caplen, rd(caplen) | 2u);
	for (i = 0; i < 500; i++) {
		sts = rd(caplen + 4);
		if ((rd(caplen) & 2u) == 0 && (sts & (1u << 11)) == 0)
			break;
		spin_ms(1);
	}

	dcbaa = (uint64_t*)dma_page(&dcbaa_phys);
	cmdring.v = (Trb*)dma_page(&cmdring.phys);
	evt = (Trb*)dma_page(&evt_phys);
	erst = (uint32_t*)dma_page(&erst_phys);
	if (!dcbaa || !cmdring.v || !evt || !erst)
		return -1;
	if (scratchpad_setup(scratchpad_count(hcc2)))
		return -1;
	cmdring.enq = 0;
	cmdring.cycle = 1;
	evt_deq = 0;
	evt_cycle = 1;
	erst[0] = (uint32_t)evt_phys;
	erst[1] = (uint32_t)(evt_phys >> 32);
	erst[2] = RING_N;
	erst[3] = 0;
	clean(erst, 16);

	wr(caplen + 0x38, slots);
	wr(caplen + 0x30, (uint32_t)dcbaa_phys);
	wr(caplen + 0x34, (uint32_t)(dcbaa_phys >> 32));
	wr(caplen + 0x18, (uint32_t)(cmdring.phys | 1u));
	wr(caplen + 0x1c, (uint32_t)(cmdring.phys >> 32));
	wr(rtsoff + 0x28, 1);
	wr(rtsoff + 0x30, (uint32_t)erst_phys);
	wr(rtsoff + 0x34, (uint32_t)(erst_phys >> 32));
	wr(rtsoff + 0x38, (uint32_t)evt_phys);
	wr(rtsoff + 0x3c, (uint32_t)(evt_phys >> 32));
	wr(rtsoff + 0x20, 2u);

	/* Run with global interrupt enable clear.  AuXhciInitialize() arms it
	 * after MSI-X and the SPI handler are in place; until then all
	 * completion is polled from the event ring. */
	wr(caplen, 1u);
	for (i = 0; i < 100; i++) {
		if ((rd(caplen + 4) & 1u) == 0)
			break;
		spin_ms(1);
	}
	UARTDebugOut("[aurora]: xhci up\r\n");
	return 0;
}

/* HID usage id (keyboard page) -> PC set-1 make code. Extended keys carry
 * their E0 prefix in the high byte, matching what the native console and
 * virtio-keyboard drivers publish. */
static const uint16_t hid_scancode[256] = {
	[0x04] = 0x1e, [0x05] = 0x30, [0x06] = 0x2e, [0x07] = 0x20,
	[0x08] = 0x12, [0x09] = 0x21, [0x0a] = 0x22, [0x0b] = 0x23,
	[0x0c] = 0x17, [0x0d] = 0x24, [0x0e] = 0x25, [0x0f] = 0x26,
	[0x10] = 0x32, [0x11] = 0x31, [0x12] = 0x18, [0x13] = 0x19,
	[0x14] = 0x10, [0x15] = 0x13, [0x16] = 0x1f, [0x17] = 0x14,
	[0x18] = 0x16, [0x19] = 0x2f, [0x1a] = 0x11, [0x1b] = 0x2d,
	[0x1c] = 0x15, [0x1d] = 0x2c,
	[0x1e] = 0x02, [0x1f] = 0x03, [0x20] = 0x04, [0x21] = 0x05,
	[0x22] = 0x06, [0x23] = 0x07, [0x24] = 0x08, [0x25] = 0x09,
	[0x26] = 0x0a, [0x27] = 0x0b,
	[0x28] = 0x1c, [0x29] = 0x01, [0x2a] = 0x0e, [0x2b] = 0x0f,
	[0x2c] = 0x39, [0x2d] = 0x0c, [0x2e] = 0x0d, [0x2f] = 0x1a,
	[0x30] = 0x1b, [0x31] = 0x2b, [0x33] = 0x27, [0x34] = 0x28,
	[0x35] = 0x29, [0x36] = 0x33, [0x37] = 0x34, [0x38] = 0x35,
	[0x39] = 0x3a,
	[0x46] = 0xe037, [0x47] = 0x46, [0x48] = 0xe11d,
	[0x49] = 0xe052, [0x4a] = 0xe047, [0x4b] = 0xe049,
	[0x4c] = 0xe053, [0x4d] = 0xe04f, [0x4e] = 0xe051,
	[0x4f] = 0xe04d, [0x50] = 0xe04b, [0x51] = 0xe050,
	[0x52] = 0xe048,
	[0x53] = 0x45, [0x54] = 0xe035, [0x55] = 0x37, [0x56] = 0x4a,
	[0x57] = 0x4e, [0x58] = 0xe01c,
	[0x59] = 0x4f, [0x5a] = 0x50, [0x5b] = 0x51, [0x5c] = 0x4b,
	[0x5d] = 0x4c, [0x5e] = 0x4d, [0x5f] = 0x47, [0x60] = 0x48,
	[0x61] = 0x49, [0x62] = 0x52, [0x63] = 0x53, [0x64] = 0x56,
	[0x65] = 0xe05d, [0x66] = 0xe05e,
	[0xe0] = 0x1d, [0xe1] = 0x2a, [0xe2] = 0x38, [0xe3] = 0xe05b,
	[0xe4] = 0xe01d, [0xe5] = 0x36, [0xe6] = 0xe038, [0xe7] = 0xe05c,
};

static void emit_key(UsbDev* d, uint8_t usage, int down) {
	uint32_t code = hid_scancode[usage];
	AuInputMessage msg;
	(void)d;
	if (!code)
		return;
	if (down)
		code = (code & 0xe000u) | (code & 0xffu);
	else
		code = (code & 0xe000u) | ((code & 0xffu) | 0x80u);
	memset(&msg, 0, sizeof msg);
	msg.type = AU_INPUT_KEYBOARD;
	msg.code = code;
	AuDevWriteKybrd(&msg);
}

static int key_in(const uint8_t* keys, uint8_t usage) {
	int i;
	for (i = 0; i < 6; i++) {
		if (keys[i] == usage)
			return 1;
	}
	return 0;
}

/* Apply a decoded keyboard state.  Both the boot layout and the report
 * descriptor path reduce to modifiers plus a list of key usages, so the
 * diff and native delivery live in one place. */
static void hid_keyboard_update(UsbDev* d, HidPipe* p, uint8_t mods, const uint8_t* keys) {
	uint8_t leds = p->led_state;
	int i;
	static int logged;
	for (i = 0; i < 8; i++) {
		uint8_t bit = (uint8_t)(1u << i);
		if ((mods & bit) != (p->last_mods & bit)) {
			uint8_t usage = (uint8_t)(0xe0 + i);
			if (logged < 16) {
				logged++;
				UARTDebugOut("[xhci-hid]: key %x %s\r\n", hid_scancode[usage],
							 (mods & bit) ? "down" : "up");
			}
			emit_key(d, usage, (mods & bit) != 0);
		}
	}
	for (i = 0; i < 6; i++) {
		if (p->last_keys[i] && !key_in(keys, p->last_keys[i])) {
			if (logged < 16) {
				logged++;
				UARTDebugOut("[xhci-hid]: key %x up\r\n",
							 hid_scancode[p->last_keys[i]]);
			}
			emit_key(d, p->last_keys[i], 0);
		}
	}
	for (i = 0; i < 6; i++) {
		if (keys[i] && !key_in(p->last_keys, keys[i])) {
			if (logged < 16) {
				logged++;
				UARTDebugOut("[xhci-hid]: key %x down\r\n",
							 hid_scancode[keys[i]]);
			}
			emit_key(d, keys[i], 1);
			/* Lock keys toggle their LED on the make code. */
			if (keys[i] == 0x39)
				leds ^= 0x02;
			else if (keys[i] == 0x53)
				leds ^= 0x01;
			else if (keys[i] == 0x47)
				leds ^= 0x04;
		}
	}
	if (leds != p->led_state && hid_set_leds(d, p, leds) == 1)
		UARTDebugOut("[xhci-hid]: leds %x\r\n", leds);
	p->last_mods = mods;
	memcpy(p->last_keys, keys, 6);
}

static void hid_keyboard_report(UsbDev* d, HidPipe* p, const uint8_t* rep, int len) {
	uint8_t keys[6];
	uint8_t mods;
	if (len < 8)
		return;
	mods = rep[0];
	memcpy(keys, rep + 2, 6);
	/* ErrorRollOver: every key is reported as 0x01, no useful diff. */
	if (keys[0] == 0x01)
		return;
	hid_keyboard_update(d, p, mods, keys);
}

static void hid_pointer_report(UsbDev* d, const uint8_t* rep, int len) {
	AuInputMessage msg;
	uint32_t w, h;
	int32_t x, y;
	uint8_t buttons = 0;
	static int logged;
	if (len < 5)
		return;
	if (rep[0] & 0x01)
		buttons |= LEFT_CLICK;
	if (rep[0] & 0x02)
		buttons |= RIGHT_CLICK;
	if (rep[0] & 0x04)
		buttons |= MIDDLE_CLICK;
	/* QEMU usb-tablet: 16-bit absolute X/Y in 0..32767. */
	w = AuConsoleGetScreenWidth();
	h = AuConsoleGetScreenHeight();
	if (!w || !h) {
		w = 1024;
		h = 768;
	}
	x = (int32_t)(((uint32_t)(rep[1] | (rep[2] << 8)) * w) / 32767u);
	y = (int32_t)(((uint32_t)(rep[3] | (rep[4] << 8)) * h) / 32767u);
	if (x >= (int32_t)w)
		x = (int32_t)w - 1;
	if (y >= (int32_t)h)
		y = (int32_t)h - 1;
	if (len >= 6 && rep[5]) {
		if (rep[5] & 0x80)
			buttons |= MOUSE_SCROLL_DOWN;
		else
			buttons |= MOUSE_SCROLL_UP;
	}
	memset(&msg, 0, sizeof msg);
	msg.type = AU_INPUT_MOUSE;
	msg.xpos = x;
	msg.ypos = y;
	msg.button_state = buttons;
	AuDevWriteMice(&msg);
	if (logged < 16) {
		logged++;
		UARTDebugOut("[xhci-hid]: pointer %d,%d buttons %x\r\n", x, y, buttons);
	}
	(void)d;
}

static int hid_field_matches(const HidPipe* p, const HidField* f, uint8_t rid) {
	return !p->rep.has_ids || f->report_id == rid;
}

/* Decode a report described by its HID report descriptor.  Report IDs, if
 * present, are the first byte and the field offsets are relative to the
 * bytes that follow. */
static void hid_keyboard_generic(UsbDev* d, HidPipe* p, const uint8_t* data, int len,
								 uint8_t rid) {
	uint8_t mods = 0;
	uint8_t keys[6];
	int nkeys = 0;
	int matched = 0;
	int i;
	uint32_t j;

	memset(keys, 0, sizeof keys);
	for (i = 0; i < p->rep.nfields; i++) {
		HidField* f = &p->rep.fields[i];
		if (!hid_field_matches(p, f, rid))
			continue;
		/* Only reports that carry keyboard-page fields belong to this
		 * decoder; a pointer report on a composite interface must not
		 * clear the keys that are currently held. */
		if (f->usage_page != 0x07)
			continue;
		matched = 1;
		if (f->flags & HID_FIELD_ARRAY) {
			for (j = 0; j < f->report_count && nkeys < 6; j++) {
				int32_t v = hid_field_value(data, len,
											 f->bit_offset + j * f->report_size,
											 f->report_size, f->logical_min);
				if (v > 0 && v < 0x100)
					keys[nkeys++] = (uint8_t)v;
			}
			continue;
		}
		if (f->report_size != 1)
			continue;
		for (j = 0; j < f->report_count; j++) {
			uint32_t page, usage;
			if (!hid_usage_for(f, j, &page, &usage))
				continue;
			if (page != 0x07 || usage < 0xe0 || usage > 0xe7)
				continue;
			if (hid_field_value(data, len, f->bit_offset + j, 1, 0))
				mods |= (uint8_t)(1u << (usage - 0xe0));
		}
	}
	if (!matched || keys[0] == 0x01)
		return;
	hid_keyboard_update(d, p, mods, keys);
}

static void hid_pointer_generic(UsbDev* d, HidPipe* p, const uint8_t* data, int len,
								uint8_t rid) {
	AuInputMessage msg;
	uint32_t w, h;
	int32_t x = 0, y = 0, wheel = 0;
	int32_t xmin = 0, xmax = 0, ymin = 0, ymax = 0;
	int have_x = 0, have_y = 0, have_wheel = 0;
	int xrel = 0, yrel = 0;
	uint8_t buttons = 0;
	int matched = 0;
	int i;
	uint32_t j;
	static int logged;

	for (i = 0; i < p->rep.nfields; i++) {
		HidField* f = &p->rep.fields[i];
		if (!hid_field_matches(p, f, rid))
			continue;
		for (j = 0; j < f->report_count; j++) {
			uint32_t page, usage;
			int32_t v;
			if (!hid_usage_for(f, j, &page, &usage))
				continue;
			if (page == 0x09 && f->report_size == 1) {
				matched = 1;
				if (!hid_field_value(data, len, f->bit_offset + j, 1, 0))
					continue;
				if (usage == 1)
					buttons |= LEFT_CLICK;
				else if (usage == 2)
					buttons |= RIGHT_CLICK;
				else if (usage == 3)
					buttons |= MIDDLE_CLICK;
				continue;
			}
			if (page != 0x01 ||
				(usage != 0x30 && usage != 0x31 && usage != 0x38))
				continue;
			matched = 1;
			v = hid_field_value(data, len,
								f->bit_offset + j * f->report_size,
								f->report_size, f->logical_min);
			if (usage == 0x30) {
				x = v;
				have_x = 1;
				xrel = (f->flags & HID_FIELD_RELATIVE) != 0;
				xmin = f->logical_min;
				xmax = f->logical_max;
			} else if (usage == 0x31) {
				y = v;
				have_y = 1;
				yrel = (f->flags & HID_FIELD_RELATIVE) != 0;
				ymin = f->logical_min;
				ymax = f->logical_max;
			} else {
				wheel = v;
				have_wheel = 1;
			}
		}
	}
	if (!matched)
		return;

	w = AuConsoleGetScreenWidth();
	h = AuConsoleGetScreenHeight();
	if (!w || !h) {
		w = 1024;
		h = 768;
	}

	if (have_x) {
		if (xrel) {
			if (!d->pointer_valid) {
				d->pointer_x = (int32_t)(w / 2);
				d->pointer_y = (int32_t)(h / 2);
				d->pointer_valid = 1;
			}
			d->pointer_x += x;
		} else if (xmax > xmin) {
			d->pointer_x = (int32_t)(((int64_t)(x - xmin) * (int64_t)(w - 1)) /
									 (int64_t)(xmax - xmin));
			d->pointer_valid = 1;
		} else {
			d->pointer_x = x;
			d->pointer_valid = 1;
		}
	}
	if (have_y) {
		if (yrel) {
			if (!d->pointer_valid) {
				d->pointer_x = (int32_t)(w / 2);
				d->pointer_y = (int32_t)(h / 2);
				d->pointer_valid = 1;
			}
			d->pointer_y += y;
		} else if (ymax > ymin) {
			d->pointer_y = (int32_t)(((int64_t)(y - ymin) * (int64_t)(h - 1)) /
									 (int64_t)(ymax - ymin));
			d->pointer_valid = 1;
		} else {
			d->pointer_y = y;
			d->pointer_valid = 1;
		}
	}
	if (!d->pointer_valid) {
		d->pointer_x = (int32_t)(w / 2);
		d->pointer_y = (int32_t)(h / 2);
		d->pointer_valid = 1;
	}
	if (d->pointer_x < 0)
		d->pointer_x = 0;
	if (d->pointer_y < 0)
		d->pointer_y = 0;
	if (d->pointer_x >= (int32_t)w)
		d->pointer_x = (int32_t)w - 1;
	if (d->pointer_y >= (int32_t)h)
		d->pointer_y = (int32_t)h - 1;
	if (have_wheel && wheel > 0)
		buttons |= MOUSE_SCROLL_UP;
	else if (have_wheel && wheel < 0)
		buttons |= MOUSE_SCROLL_DOWN;

	memset(&msg, 0, sizeof msg);
	msg.type = AU_INPUT_MOUSE;
	msg.xpos = d->pointer_x;
	msg.ypos = d->pointer_y;
	msg.button_state = buttons;
	AuDevWriteMice(&msg);
	if (logged < 16) {
		logged++;
		UARTDebugOut("[xhci-hid]: pointer %d,%d buttons %x\r\n",
					 d->pointer_x, d->pointer_y, buttons);
	}
}

static void hid_decode_report(UsbDev* d, HidPipe* p, const uint8_t* rep, int len) {
	uint8_t rid = 0;
	int off = 0;

	if (p->rep.valid) {
		if (len <= 0)
			return;
		if (p->rep.has_ids) {
			rid = rep[0];
			off = 1;
		}
		if (len <= off)
			return;
		/* A descriptor can put keyboard and pointer collections in
		 * the same report stream.  Each decoder ignores fields from
		 * the other collection, so run both and let the report's
		 * usages decide which one produces input. */
		hid_keyboard_generic(d, p, rep + off, len - off, rid);
		hid_pointer_generic(d, p, rep + off, len - off, rid);
		return;
	}
	if (p->kind == 1)
		hid_keyboard_report(d, p, rep, len);
	else
		hid_pointer_report(d, rep, len);
}

static void xhci_hid_thread(uint64_t arg) {
	uint8_t buf[HID_REPORT_MAX];
	int i, j;
	static int alive;
	(void)arg;
	if (!alive) {
		alive = 1;
		UARTDebugOut("[xhci-hid]: poll thread running\r\n");
		UARTDebugOut("[xhci-hotplug]: port thread running\r\n");
	}
	for (;;) {
		for (i = 0; i < XHCI_MAX_DEV; i++) {
			UsbDev* d = &devices[i];
			int nhids;
			usb_lock();
			if (d->state != XDEV_READY || !d->nhids) {
				usb_unlock();
				continue;
			}
			d->refs++;
			nhids = d->nhids;
			usb_unlock();
			for (j = 0; j < nhids; j++) {
				int n = hid_pipe_xfer(d, &d->hids[j], buf, sizeof buf);
				if (n > 0)
					hid_decode_report(d, &d->hids[j], buf, n);
			}
			usb_lock();
			d->refs--;
			usb_unlock();
		}
		/* Hub status changes and root-port changes share this thread
		 * with HID polling. A second kernel thread would contend for
		 * usb_busy with the interrupt reader, and on this scheduler a
		 * preempted lock holder could leave the other thread spinning
		 * before either made progress. */
		for (i = 0; i < XHCI_MAX_DEV; i++) {
			UsbDev* d = &devices[i];
			usb_lock();
			if (d->state != XDEV_READY || !d->is_hub) {
				usb_unlock();
				continue;
			}
			d->refs++;
			usb_unlock();
			hub_poll(d);
			usb_lock();
			d->refs--;
			usb_unlock();
		}
		xhci_port_scan();
		/* Mark this thread asleep and let the timer tick switch away.
		 * Calling AuScheduleNext() here would hand off through
		 * aa64_schedule_init(), which saves the live ELR_EL1/SPSR_EL1
		 * (stale for an EL1 kernel thread) and resumes via
		 * aa64_restore_sp() on a later tick. The first eret after that
		 * returns to the stale ELR. The normal tick path captures a
		 * complete exception frame instead. */
		AuSleepThread(AuGetCurrentThread(), 8);
	}
}

void AuXhciInitialize(void) {
	int p;
	uint16_t command;
	int attempt;
	uint8_t tried[XHCI_MAX_PORTS + 1];
	int bt_seen = 0;

	if (xhci_initialized)
		return;
	if (!AuIsPCIeInitialized())
		return;
	pci = AuPCIEScanClassIF(0x0c, 0x03, 0x30, &bus, &devn, &func);
	if (pci == 0 || pci == 0xffffffffull) {
		UARTDebugOut("[aurora]: xhci not present\r\n");
		return;
	}
	command = (uint16_t)AuPCIERead(pci, PCI_COMMAND, bus, devn, func);
	command |= 0x6;
	AuPCIEWrite(pci, PCI_COMMAND, command, bus, devn, func);
	if (bringup()) {
		UARTDebugOut("[xhci]: controller reset failed\r\n");
		return;
	}

	/* qemu-xhci exposes MSI-X; other controllers may expose MSI or no
	 * message interrupt at all.  The event ring is polled either way, so a
	 * failed allocation only costs the interrupt, not the controller. */
	{
		int spi = AuGICAllocateSPI();
		if (spi >= 0 && AuPCIEAllocMSI(pci, (size_t)spi, bus, devn, func)) {
			GICRegisterSPIHandler((void*)xhci_irq, spi);
			GICEnableSPIIRQ((uint32_t)spi);
			wr(caplen, rd(caplen) | 4u);
			UARTDebugOut("[xhci]: irq spi %d\r\n", spi);
		} else {
			UARTDebugOut("[xhci]: no message irq, event polling only\r\n");
		}
	}
	xhci_initialized = 1;

	/* Power on all ports first -- after HCRST PP bits are clear so CCS
	 * reads 0 even with a device attached. */
	for (p = 1; p <= (int)nports; p++) {
		uint32_t base = caplen + 0x400 + (uint32_t)(p - 1) * 16;
		wr(base, rd(base) | (1u << 9));
	}
	spin_ms(50);
	for (p = 1; p <= (int)nports; p++) {
		uint32_t base = caplen + 0x400 + (uint32_t)(p - 1) * 16;
		UARTDebugOut("[xhci]: port %d sc=%x\r\n", p, rd(base));
	}

	/* QEMU usb-host connects asynchronously -- poll briefly for ports that
	 * have not answered yet. A port that answered once is never reset again:
	 * re-running Enable Slot + Address Device on it would leak slots and
	 * stall boot. */
	memset(tried, 0, sizeof tried);
	for (attempt = 0; attempt < 50; attempt++) {
		int fresh = 0;
		for (p = 1; p <= (int)nports; p++) {
			int r;
			if (tried[p])
				continue;
			r = enum_port(p);
			if (r == XCT_NONE)
				continue;
			if (r == XCT_FAIL) {
				/* One retryable failure per pass; the next pass
				 * gets another go until the 5s window closes. */
				fresh = 1;
				continue;
			}
			tried[p] = 1;
			fresh = 1;
			if (r == XCT_DEV && bt_count && !bt_seen)
				bt_seen = 1;
		}
		if (!fresh)
			break;
		if (attempt % 10 == 0) {
			uint32_t base = caplen + 0x400 + (uint32_t)((nports >= 5 ? 5 : nports) - 1) * 16;
			UARTDebugOut("[xhci]: scan %d port %d sc=%x\r\n", attempt,
						 nports >= 5 ? 5 : nports, rd(base));
		}
		spin_ms(100);
	}

	/* Acknowledge the connect/reset changes raised while the initial scan
	 * reset ports, so the hotplug thread starts from a quiet controller. */
	for (p = 1; p <= (int)nports; p++) {
		uint32_t base = caplen + 0x400 + (uint32_t)(p - 1) * 16;
		uint32_t sc = rd(base);
		wr(base, sc & PORTSC_CHANGE);
	}

	/* Hubs can be several tiers deep, so enumerate their children before
	 * the HID poll thread starts.  Otherwise the first injected report can
	 * race the first child enumeration. */
	xhci_hub_scan();

	if (hid_count)
		UARTDebugOut("[aurora]: xhci hid %d interface(s)\r\n", hid_count);
	else
		UARTDebugOut("[aurora]: xhci no hid device\r\n");
}

/**
 * @brief AuXhciStartPolling -- start the HID and port-change consumers
 *
 * Controller enumeration happens early, while the USB device objects and
 * rings can still be built.  Kernel threads, however, are inserted into the
 * scheduler's thread list, and AuSchedulerInitialize() resets that list.
 * Creating them afterwards is what keeps the poll threads alive.
 */
void AuXhciStartPolling(void) {
	AA64Thread* hid;
	if (polling_started || !xhci_initialized)
		return;
	polling_started = 1;
	/* One poller owns both HID reads and port changes. It runs for the
	 * controller's lifetime: a keyboard or tablet may be plugged in long
	 * after boot, and a HID device may be unplugged at any point. */
	hid = AuCreateKthread(xhci_hid_thread, AuGetRootPageTable(), "xhci-poll");
	if (hid)
		UARTDebugOut("[aurora]: xhci poll started\r\n");
	if (bt_ops_valid) {
		bt_ops_valid = 0;
		AuBtUsbReady(&bt_ops);
	}
}
