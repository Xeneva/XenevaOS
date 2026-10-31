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
#include <Drivers/uart.h>

#define RING_N 256
#define TRB_LINK 6
#define TRB_ENABLE_SLOT 9
#define TRB_ADDR_DEV 11
#define TRB_CFG_EP 12
#define TRB_SETUP 2
#define TRB_DATA 3
#define TRB_STATUS 4
#define TRB_NORMAL 1
#define TRB_EVENT_XFER 32
#define TRB_EVENT_CMD 33

typedef struct {
	uint32_t w0, w1, w2, w3;
} Trb;

typedef struct {
	Trb* v;
	uint64_t phys;
	uint32_t enq;
	uint8_t cycle;
	uint8_t dci;
} Ring;

static volatile uint32_t* mmio;
static uint32_t caplen;
static uint32_t dboff;
static uint32_t rtsoff;
static uint32_t ctx_sz;
static uint32_t nports;
static int bus, devn, func;
static uint64_t pci;
static Ring cmdring;
static Trb* evt;
static uint64_t evt_phys;
static uint32_t evt_deq;
static uint8_t evt_cycle;
static uint64_t* dcbaa;
static uint64_t dcbaa_phys;
static uint8_t slot_id;
static uint8_t* out_ctx;
static uint64_t out_phys;
static uint8_t* in_ctx;
static uint64_t in_phys;
static Ring ep0, ep_intr, ep_bout, ep_bin, ep_iout;
static uint8_t* xfer_buf;
static uint64_t xfer_phys;
static uint8_t* intr_buf;
static uint64_t intr_phys;
static uint8_t* bin_buf;
static uint64_t bin_phys;
static uint16_t intr_mps;
static uint16_t bin_mps;
static int intr_armed;
static int bin_armed;
static int have_iso;
static int irq_seen;
static int ready;
static Trb pend[24];
static int pend_n;

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

static void* dma_page(uint64_t* phys) {
	uint64_t p = AuPmmngrAllocPages(1, 1, 0, AURORA_PAGE_DMA);
	void* v;
	if (!p)
		return NULL;
	v = (void*)P2V(p);
	memset(v, 0, PAGE_SIZE);
	clean(v, PAGE_SIZE);
	*phys = p;
	return v;
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

static int take_match(int want_cmd, uint64_t trb_phys, uint8_t dci, Trb* out) {
	int i;
	for (i = 0; i < pend_n; i++) {
		uint32_t type = (pend[i].w3 >> 10) & 0x3f;
		uint64_t ptr = ((uint64_t)pend[i].w0 | ((uint64_t)pend[i].w1 << 32)) & ~0xfull;
		uint8_t ep = (uint8_t)((pend[i].w3 >> 16) & 0x1f);
		int hit = 0;
		if (want_cmd && type == TRB_EVENT_CMD)
			hit = 1;
		else if (!want_cmd && trb_phys && type == TRB_EVENT_XFER && ptr == (trb_phys & ~0xfull))
			hit = 1;
		else if (!want_cmd && !trb_phys && type == TRB_EVENT_XFER && ep == dci)
			hit = 1;
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
	while (event_pop(&ev))
		stash(ev);
}

static int wait_cmd(uint8_t* slot_out, int* code) {
	int spins = 0;
	while (spins++ < 500) {
		Trb ev;
		drain();
		if (take_match(1, 0, 0, &ev)) {
			if (slot_out)
				*slot_out = (uint8_t)((ev.w3 >> 24) & 0xff);
			if (code)
				*code = (int)((ev.w2 >> 24) & 0xff);
			return 0;
		}
		spin_ms(1);
	}
	UARTDebugOut("[xhci]: command timed out\r\n");
	return -1;
}

static int wait_xfer(uint64_t trb_phys) {
	int spins = 0;
	while (spins++ < 500) {
		Trb ev;
		drain();
		if (take_match(0, trb_phys, 0, &ev)) {
			int c = (int)((ev.w2 >> 24) & 0xff);
			if (c == 1 || c == 13)
				return 0;
			UARTDebugOut("[xhci]: transfer completion %d\r\n", c);
			return -1;
		}
		spin_ms(1);
	}
	UARTDebugOut("[xhci]: transfer timed out\r\n");
	return -1;
}

static int ctrl(uint8_t bm, uint8_t req, uint16_t val, uint16_t idx, uint8_t* data, uint16_t len,
				int data_in) {
	uint32_t trt = len ? (data_in ? 3 : 2) : 0;
	uint64_t status_phys;
	uint32_t setup0 = (uint32_t)bm | ((uint32_t)req << 8) | ((uint32_t)val << 16);
	uint32_t setup1 = (uint32_t)idx | ((uint32_t)len << 16);
	if (!data_in && len && data) {
		memcpy(xfer_buf, data, len);
		clean(xfer_buf, len);
	}
	ring_push(&ep0, setup0, setup1, 8, (trt << 16) | (1 << 6) | (TRB_SETUP << 10));
	if (len) {
		ring_push(&ep0, (uint32_t)xfer_phys, (uint32_t)(xfer_phys >> 32), len,
				  (data_in ? (1u << 16) : 0) | (TRB_DATA << 10));
	}
	status_phys = ring_push(&ep0, 0, 0, 0, ((len && data_in) ? 0 : (1u << 16)) | (1u << 5) | (TRB_STATUS << 10));
	door(slot_id, 1);
	if (wait_xfer(status_phys))
		return -1;
	if (data_in && len && data) {
		inv(xfer_buf, len);
		memcpy(data, xfer_buf, len);
	}
	return 0;
}

static void ep_init(uint8_t* ctx, Ring* ring, uint32_t type, uint16_t mps, uint32_t interval,
					uint32_t avg) {
	uint32_t* e = (uint32_t*)ctx;
	memset(ctx, 0, ctx_sz);
	e[0] = (interval & 0xff) << 16;
	e[1] = (type << 3) | (3u << 8) | ((uint32_t)mps << 16);
	e[2] = (uint32_t)(ring->phys | ring->cycle);
	e[3] = (uint32_t)(ring->phys >> 32);
	e[4] = avg;
}

static int address_at(int port, int speed) {
	uint16_t mps = (speed <= 2) ? 64 : (speed == 3 ? 64 : 512);
	if (speed == 2)
		mps = 8;
	uint32_t* icc = (uint32_t*)in_ctx;
	uint8_t* slotc = in_ctx + 32;   /* Input Control Context is always 32B */
	uint32_t* s = (uint32_t*)slotc;
	int code = 0;
	memset(in_ctx, 0, PAGE_SIZE);
	memset(out_ctx, 0, PAGE_SIZE);
	ep0.v = (Trb*)dma_page(&ep0.phys);
	if (!ep0.v)
		return -1;
	ep0.enq = 0;
	ep0.cycle = 1;
	icc[1] = (1u << 0) | (1u << 1);
	s[0] = ((uint32_t)speed << 20) | (1u << 27);
	s[1] = ((uint32_t)port << 16);
	ep_init(in_ctx + 32 + ctx_sz, &ep0, 4, mps, 0, 8);
	clean(in_ctx, PAGE_SIZE);
	clean(out_ctx, PAGE_SIZE);
	dcbaa[0] = 0;
	dcbaa[slot_id] = out_phys;
	clean(dcbaa, PAGE_SIZE);
	UARTDebugOut("[xhci]: addr ctx_sz=%d in_phys=%x slot=%d speed=%d mps=%d\r\n",
				 ctx_sz, (uint32_t)in_phys, slot_id, speed, mps);
	UARTDebugOut("[xhci]: icc %x %x s0=%x s1=%x ep0 %x %x %x %x\r\n",
				 icc[0], icc[1], s[0], s[1],
				 ((uint32_t*)(in_ctx + 32 + ctx_sz))[0],
				 ((uint32_t*)(in_ctx + 32 + ctx_sz))[1],
				 ((uint32_t*)(in_ctx + 32 + ctx_sz))[2],
				 ((uint32_t*)(in_ctx + 32 + ctx_sz))[3]);
	ring_push(&cmdring, (uint32_t)in_phys, (uint32_t)(in_phys >> 32), 0,
			  (TRB_ADDR_DEV << 10) | (1u << 9) | ((uint32_t)slot_id << 24));
	door(0, 0);
	if (wait_cmd(NULL, &code) || code != 1) {
		UARTDebugOut("[xhci]: address device completion %d\r\n", code);
		return -1;
	}
	return 0;
}

static int configure_eps(uint32_t add, uint32_t entries) {
	uint32_t* icc = (uint32_t*)in_ctx;
	uint32_t* s = (uint32_t*)(in_ctx + 32);
	int code = 0;
	icc[0] = 0;
	icc[1] = add | 1u;
	s[0] = (s[0] & ~(0x1fu << 27)) | (entries << 27);
	clean(in_ctx, PAGE_SIZE);
	ring_push(&cmdring, (uint32_t)in_phys, (uint32_t)(in_phys >> 32), 0,
			  (TRB_CFG_EP << 10) | ((uint32_t)slot_id << 24));
	door(0, 0);
	if (wait_cmd(NULL, &code) || code != 1) {
		UARTDebugOut("[xhci]: configure endpoint completion %d\r\n", code);
		return -1;
	}
	return 0;
}

static int port_reset(int port) {
	uint32_t base = caplen + 0x400 + (uint32_t)(port - 1) * 16;
	uint32_t sc, w;
	int i;
	sc = rd(base);
	/* An unattached port still burns the whole 200 ms reset poll below, and
	 * the BT scan calls us for every port on every pass. CCS stays clear
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

static int arm_intr(void) {
	uint64_t phys;
	if (!ep_intr.v)
		return -1;
	memset(intr_buf, 0, intr_mps ? intr_mps : 64);
	clean(intr_buf, 64);
	phys = ring_push(&ep_intr, (uint32_t)intr_phys, (uint32_t)(intr_phys >> 32),
					 intr_mps ? intr_mps : 64, (1u << 5) | (TRB_NORMAL << 10));
	(void)phys;
	door(slot_id, ep_intr.dci);
	intr_armed = 1;
	return 0;
}

static int xhci_control(void* ctx, uint8_t bm, uint8_t req, uint16_t val, uint16_t idx, uint8_t* data,
						uint16_t len, int data_in) {
	(void)ctx;
	if (len > PAGE_SIZE)
		return -1;
	return ctrl(bm, req, val, idx, data, len, data_in);
}

static int xhci_intr(void* ctx, uint8_t* data, uint16_t cap) {
	Trb ev;
	uint16_t n;
	(void)ctx;
	if (!intr_armed)
		arm_intr();
	drain();
	if (!take_match(0, 0, ep_intr.dci, &ev))
		return 0;
	n = intr_mps;
	if (n > cap)
		n = cap;
	inv(intr_buf, n);
	memcpy(data, intr_buf, n);
	intr_armed = 0;
	arm_intr();
	return (int)n;
}

static int arm_bin(void) {
	uint16_t n = bin_mps ? bin_mps : 64;
	if (!ep_bin.v || !bin_buf)
		return -1;
	memset(bin_buf, 0, n);
	clean(bin_buf, n);
	ring_push(&ep_bin, (uint32_t)bin_phys, (uint32_t)(bin_phys >> 32), n, (1u << 5) | (TRB_NORMAL << 10));
	door(slot_id, ep_bin.dci);
	bin_armed = 1;
	return 0;
}

static int bulk_xfer(Ring* ring, uint64_t phys, uint16_t len) {
	uint64_t trb;
	if (!ring->v || !len)
		return -1;
	trb = ring_push(ring, (uint32_t)phys, (uint32_t)(phys >> 32), len, (1u << 5) | (TRB_NORMAL << 10));
	door(slot_id, ring->dci);
	return wait_xfer(trb);
}

static int xhci_bulk_out(void* ctx, const uint8_t* data, uint16_t len) {
	(void)ctx;
	if (len > PAGE_SIZE)
		return -1;
	memcpy(xfer_buf, data, len);
	clean(xfer_buf, len);
	return bulk_xfer(&ep_bout, xfer_phys, len);
}

static int xhci_bulk_in(void* ctx, uint8_t* data, uint16_t cap) {
	Trb ev;
	uint16_t n;
	(void)ctx;
	if (!bin_armed)
		arm_bin();
	drain();
	if (!take_match(0, 0, ep_bin.dci, &ev))
		return 0;
	n = bin_mps ? bin_mps : 64;
	if (n > cap)
		n = cap;
	inv(bin_buf, n);
	memcpy(data, bin_buf, n);
	bin_armed = 0;
	arm_bin();
	return (int)n;
}

static int xhci_iso_out(void* ctx, const uint8_t* data, uint16_t len) {
	Ring* r = have_iso ? &ep_iout : &ep_bout;
	(void)ctx;
	if (len > PAGE_SIZE)
		return -1;
	memcpy(xfer_buf, data, len);
	clean(xfer_buf, len);
	return bulk_xfer(r, xfer_phys, len);
}

static int claim_bt(const uint8_t* desc, int len, int speed) {
	int i = 0;
	int in_if = 0;
	uint32_t add = 0;
	uint32_t entries = 1;
	uint8_t cfg = 1;
	AuBtUsbOps ops;
	(void)speed;
	ep_intr.v = ep_bout.v = ep_bin.v = ep_iout.v = NULL;
	while (i + 2 <= len) {
		uint8_t l = desc[i];
		uint8_t t = desc[i + 1];
		if (l < 2 || i + l > len)
			break;
		if (t == 2 && l >= 9)
			cfg = desc[i + 5];
		if (t == 4 && l >= 9) {
			in_if = (desc[i + 5] == 0xe0 && desc[i + 6] == 0x01 && desc[i + 7] == 0x01);
		}
		if (in_if && t == 5 && l >= 7) {
			uint8_t addr = desc[i + 2];
			uint8_t attr = desc[i + 3] & 3;
			uint16_t mps = (uint16_t)(desc[i + 4] | (desc[i + 5] << 8));
			uint8_t interval = desc[i + 6];
			int dir_in = (addr & 0x80) != 0;
			int num = addr & 0x0f;
			int dci = (num == 0) ? 1 : (num * 2 + (dir_in ? 1 : 0));
			Ring* ring = NULL;
			uint32_t type = 4;
			if (attr == 3 && dir_in && !ep_intr.v) {
				ep_intr.v = (Trb*)dma_page(&ep_intr.phys);
				ep_intr.enq = 0;
				ep_intr.cycle = 1;
				ep_intr.dci = (uint8_t)dci;
				ring = &ep_intr;
				type = 7;
				intr_mps = mps ? mps : 16;
			} else if (attr == 2 && !dir_in && !ep_bout.v) {
				ep_bout.v = (Trb*)dma_page(&ep_bout.phys);
				ep_bout.enq = 0;
				ep_bout.cycle = 1;
				ep_bout.dci = (uint8_t)dci;
				ring = &ep_bout;
				type = 2;
			} else if (attr == 2 && dir_in && !ep_bin.v) {
				ep_bin.v = (Trb*)dma_page(&ep_bin.phys);
				ep_bin.enq = 0;
				ep_bin.cycle = 1;
				ep_bin.dci = (uint8_t)dci;
				ring = &ep_bin;
				type = 6;
				bin_mps = mps ? mps : 64;
			} else if (!dir_in && !ep_iout.v && (attr == 1 || (attr == 2 && ep_bout.v))) {
				ep_iout.v = (Trb*)dma_page(&ep_iout.phys);
				ep_iout.enq = 0;
				ep_iout.cycle = 1;
				ep_iout.dci = (uint8_t)dci;
				ring = &ep_iout;
				type = (attr == 1) ? 1 : 2;
				have_iso = 1;
			}
			if (ring) {
				uint32_t iv = interval ? (uint32_t)(interval - 1) : 0;
				if (iv > 15)
					iv = 6;
				ep_init(in_ctx + ctx_sz * (uint32_t)(dci + 1), ring, type, mps ? mps : 64, iv, mps ? mps : 64);
				add |= 1u << dci;
				if ((uint32_t)dci > entries)
					entries = (uint32_t)dci;
			}
		}
		i += l;
	}
	if (!ep_intr.v || !ep_bout.v || !ep_bin.v)
		return -1;
	if (ctrl(0x00, 9, cfg, 0, NULL, 0, 0))
		return -1;
	if (configure_eps(add, entries))
		return -1;
	arm_intr();
	arm_bin();
	memset(&ops, 0, sizeof ops);
	ops.ctx = NULL;
	ops.control = xhci_control;
	ops.intr = xhci_intr;
	ops.bulk_out = xhci_bulk_out;
	ops.bulk_in = xhci_bulk_in;
	ops.iso_out = xhci_iso_out;
	ops.has_iso = have_iso;
	ready = 1;
	AuBtUsbReady(&ops);
	return 0;
}

/* enum_port() verdicts. The scan must tell "nothing attached" (keep polling
 * cheaply) apart from "a device answered" — re-enumerating a port that already
 * answered is what used to stall boot. */
#define XCT_NONE 0  /* nothing connected on this port */
#define XCT_BT 1    /* bluetooth adapter claimed */
#define XCT_NOTBT 2 /* a device answered, but it is not a BT adapter */
#define XCT_FAIL -1 /* enumeration error, worth one retry */

static int enum_port(int port) {
	uint8_t devdesc[18];
	uint8_t cfg[512];
	uint16_t total;
	int speed;
	int code = 0;
	if (!port_reset(port))
		return XCT_NONE;
	speed = speed_of(port);
	if (speed == 0)
		speed = 1;
	ring_push(&cmdring, 0, 0, 0, TRB_ENABLE_SLOT << 10);
	door(0, 0);
	if (wait_cmd(&slot_id, &code) || code != 1 || slot_id == 0) {
		UARTDebugOut("[xhci]: enable slot completion %d\r\n", code);
		return XCT_FAIL;
	}
	if (address_at(port, speed))
		return XCT_FAIL;
	memset(devdesc, 0, sizeof devdesc);
	if (ctrl(0x80, 6, 0x0100, 0, devdesc, 8, 1))
		return XCT_FAIL;
	if (ctrl(0x80, 6, 0x0100, 0, devdesc, 18, 1))
		return XCT_FAIL;
	if (ctrl(0x80, 6, 0x0200, 0, cfg, 9, 1))
		return XCT_FAIL;
	total = (uint16_t)(cfg[2] | (cfg[3] << 8));
	if (total > sizeof cfg)
		total = sizeof cfg;
	if (total < 9)
		return XCT_NOTBT;
	if (ctrl(0x80, 6, 0x0200, 0, cfg, total, 1))
		return XCT_FAIL;
	UARTDebugOut("[xhci]: device class %x subclass %x\r\n", devdesc[4], devdesc[5]);
	UARTDebugOut("[xhci]: devdesc %x %x %x %x %x %x %x %x\r\n", devdesc[0], devdesc[1],
				 devdesc[2], devdesc[3], devdesc[4], devdesc[5], devdesc[6], devdesc[7]);
	UARTDebugOut("[xhci]: cfg %x %x %x %x %x\r\n", cfg[0], cfg[1], cfg[2], cfg[3], cfg[4]);
	if (claim_bt(cfg, total, speed)) {
		UARTDebugOut("[xhci]: port %d is not a bluetooth adapter\r\n", port);
		return XCT_NOTBT;
	}
	return XCT_BT;
}

static void xhci_irq(int spi) {
	(void)spi;
	irq_seen = 1;
	if (rtsoff)
		wr(rtsoff, rd(rtsoff) | 1u);
}

static int bringup(void) {
	uint32_t hcs1, hcc1, cmd, sts;
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
	mmio = (volatile uint32_t*)AuMapMMIO(bar, (barsz + PAGE_SIZE - 1) / PAGE_SIZE);
	if (!mmio)
		return -1;
	caplen = rd(0) & 0xff;
	hcs1 = rd(0x04);
	hcc1 = rd(0x10);
	dboff = rd(0x14) & ~3u;
	rtsoff = rd(0x18) & ~0x1fu;
	nports = (hcs1 >> 24) & 0xff;
	slots = hcs1 & 0xff;
	if (slots == 0 || slots > 32)
		slots = 8;
	ctx_sz = (hcc1 & 4u) ? 64 : 32;
	if (nports == 0 || nports > 32)
		return -1;

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
	in_ctx = (uint8_t*)dma_page(&in_phys);
	out_ctx = (uint8_t*)dma_page(&out_phys);
	xfer_buf = (uint8_t*)dma_page(&xfer_phys);
	intr_buf = (uint8_t*)dma_page(&intr_phys);
	bin_buf = (uint8_t*)dma_page(&bin_phys);
	erst = (uint32_t*)dma_page(&erst_phys);
	if (!dcbaa || !cmdring.v || !evt || !in_ctx || !out_ctx || !xfer_buf || !intr_buf || !bin_buf || !erst)
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

	/* qemu-xhci exposes MSI-X. AuPCIEAllocMSI maps that table through a
	 * one-page window and faults when the table sits past the first page.
	 * Completion is polled from the event ring, so the controller still runs. */
	(void)xhci_irq;
	wr(caplen, 1u | 4u);
	for (i = 0; i < 100; i++) {
		if ((rd(caplen + 4) & 1u) == 0)
			break;
		spin_ms(1);
	}
	UARTDebugOut("[aurora]: xhci up\r\n");
	return 0;
}

void AuXhciInitialize(void) {
	int p;
	int found = 0;
	uint16_t command;
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
	/* Power on all ports first — after HCRST PP bits are clear so CCS
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
	/* QEMU usb-host connects asynchronously — poll for up to 5 s when nothing
	 * has shown up yet. A port that has already answered is never enumerated
	 * again: re-running port_reset + Enable Slot + Address Device on it
	 * leaked a slot and cost 0.5 s command timeouts per pass, which stalled
	 * boot for ~90 s and left the display dark. Once a device has been seen
	 * and a full pass adds nothing new, the topology has settled. */
	{
		int attempt;
		int attached = 0;
		uint8_t mark[33];
		uint8_t tried[33];
		memset(mark, 0, sizeof mark);
		memset(tried, 0, sizeof tried);
		for (attempt = 0; attempt < 50 && !found; attempt++) {
			int fresh = 0;
			for (p = 1; p <= (int)nports && !found; p++) {
				int r;
				if (tried[p])
					continue;
				r = enum_port(p);
				if (r == XCT_BT) {
					found = 1;
					break;
				}
				if (r == XCT_NONE)
					continue;
				if (!mark[p]) {
					mark[p] = 1;
					attached = 1;
					fresh = 1;
				}
				if (r == XCT_NOTBT)
					tried[p] = 1;
			}
			if (found)
				break;
			if (attached && !fresh)
				break;
			if (attempt % 10 == 0) {
				uint32_t base = caplen + 0x400 + (uint32_t)(5 - 1) * 16;
				UARTDebugOut("[xhci]: retry %d port 5 sc=%x\r\n", attempt, rd(base));
			}
			spin_ms(100);
		}
	}
	if (!found)
		UARTDebugOut("[aurora]: xhci no bluetooth adapter\r\n");
}
