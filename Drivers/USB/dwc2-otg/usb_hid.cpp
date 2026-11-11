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
#include <aurora.h>
#include <Drivers/uart.h>
#include <Fs/Dev/devinput.h>
#include <Hal/AA64/sched.h>
#include <Sync/bkl.h>
#include <Mm/vmmngr.h>
#include <aucon.h>
#include <timer.h>
#include <string.h>
#include "dwc2_hw.h"
#include "usb.h"
#include "usb_hid.h"

/*
 * USB HID mouse (HID 1.11).
 *
 * The report layout comes from the device's report descriptor (section
 * 6.2.2 and the HID Usage Tables): inside the Generic
 * Desktop / Mouse application collection, the variable Input fields with
 * usages Button 1..3, X, Y and Wheel give each value's bit offset, size and
 * signedness, plus the report ID that carries them. Many mice ignore
 * SET_PROTOCOL(boot) and keep sending that layout (often with a report ID
 * byte in front or 12/16-bit X/Y), so it is used whenever it parses.
 * Only if it does not is the boot report (buttons, int8 X, int8 Y, wheel)
 * assumed, after SET_PROTOCOL(boot).
 *
 * The interrupt endpoints are polled from a kernel thread at the
 * endpoint's bInterval. Driver entry runs before the scheduler, and
 * kernel threads only survive if created after AuSchedulerInitialize, so
 * a one-shot kernel timer creates the thread from the first ticks.
 */

#define HID_MAX_MICE	   4
#define HID_REPORT_MAX	   64
#define HID_REPORT_DESC_MAX 1024
#define HID_POLL_MIN_MS	   4
#define HID_POLL_MAX_MS	   32
#define HID_MAX_ERRORS	   50
#define HID_START_DELAY_MS 100
#define HID_LOG_RAW		   6

/* usages, page << 16 | id */
#define HID_USAGE_MOUSE	  0x00010002u
#define HID_USAGE_X		  0x00010030u
#define HID_USAGE_Y		  0x00010031u
#define HID_USAGE_WHEEL	  0x00010038u
#define HID_USAGE_BUTTON1 0x00090001u

typedef struct {
	uint16_t bit; /* offset in the report data, after the report ID */
	uint8_t size;
	uint8_t is_signed;
	uint8_t valid;
} hid_field_t;

typedef struct {
	uint8_t report_id; /* 0: the device uses no report IDs */
	hid_field_t button[3];
	hid_field_t x, y, wheel;
} hid_layout_t;

typedef struct {
	usb_device_t* dev;
	uint8_t ep;
	uint16_t mps;
	uint32_t interval_ms;
	uint64_t next_poll_ms;
	uint32_t errors;
	uint8_t last_buttons;
	uint8_t raw_logged;
	hid_layout_t layout;
	uint8_t report[HID_REPORT_MAX];
} hid_mouse_t;

static hid_mouse_t hid_mice[HID_MAX_MICE];
static int hid_num_mice;
static AA64Thread* volatile hid_thread;
static int32_t hid_x, hid_y;
static uint8_t hid_desc_buf[HID_REPORT_DESC_MAX];
static uint16_t hid_id_offset[256];

/*
 * hid_interval_ms -- bInterval in ms: frames for low/full speed, 2^(n-1)
 * microframes for high speed (USB 2.0 table 9-13)
 */
static uint32_t hid_interval_ms(uint8_t speed, uint8_t binterval) {
	uint32_t ms;
	if (speed == DWC2_SPEED_HIGH) {
		uint32_t exp = (binterval >= 1 && binterval <= 16) ? binterval - 1u : 3u;
		ms = (1u << exp) / 8;
	} else {
		ms = binterval;
	}
	if (ms < HID_POLL_MIN_MS)
		ms = HID_POLL_MIN_MS;
	if (ms > HID_POLL_MAX_MS)
		ms = HID_POLL_MAX_MS;
	return ms;
}

/* ------------------------------------------------------------------
 * report descriptor parser (HID 1.11 6.2.2)
 * ------------------------------------------------------------------ */

typedef struct {
	uint32_t usage_page;
	int32_t logical_min;
	uint32_t report_size;
	uint32_t report_count;
	uint8_t report_id;
} hid_globals_t;

#define HID_MAX_USAGES 32

/**
 * hid_parse_mouse -- find the mouse fields in a report descriptor
 * @return true if X and Y were found
 */
static bool hid_parse_mouse(const uint8_t* d, uint32_t len, hid_layout_t* out) {
	hid_globals_t g, stack[4];
	memset(&g, 0, sizeof(g));
	int sp = 0;
	uint32_t usages[HID_MAX_USAGES];
	int nusages = 0;
	uint32_t umin = 0, umax = 0;
	bool have_range = false;
	int depth = 0, mouse_depth = -1;
	bool id_locked = false;
	hid_layout_t cur;
	memset(&cur, 0, sizeof(cur));
	memset(hid_id_offset, 0, sizeof(hid_id_offset));

	uint32_t i = 0;
	while (i < len) {
		uint8_t prefix = d[i++];
		if (prefix == 0xFE) { /* long item: bDataSize, bLongItemTag, data */
			if (i + 1 >= len)
				break;
			i += 2u + d[i];
			continue;
		}
		uint32_t size = prefix & 3;
		if (size == 3)
			size = 4;
		if (i + size > len)
			break;
		uint32_t uval = 0;
		for (uint32_t k = 0; k < size; k++)
			uval |= (uint32_t)d[i + k] << (8 * k);
		int32_t sval = (int32_t)uval;
		if (size == 1)
			sval = (int8_t)uval;
		else if (size == 2)
			sval = (int16_t)uval;
		i += size;

		uint8_t type = (prefix >> 2) & 3;
		uint8_t tag = prefix >> 4;

		if (type == 1) { /* global */
			switch (tag) {
			case 0x0: g.usage_page = uval; break;
			case 0x1: g.logical_min = sval; break;
			case 0x7: g.report_size = uval; break;
			case 0x8: g.report_id = (uint8_t)uval; break;
			case 0x9: g.report_count = uval; break;
			case 0xA: if (sp < 4) stack[sp++] = g; break;
			case 0xB: if (sp > 0) g = stack[--sp]; break;
			}
		} else if (type == 2) { /* local */
			uint32_t usage = (size == 4) ? uval : ((g.usage_page << 16) | (uval & 0xFFFF));
			switch (tag) {
			case 0x0:
				if (nusages < HID_MAX_USAGES)
					usages[nusages++] = usage;
				break;
			case 0x1: umin = usage; break;
			case 0x2: umax = usage; have_range = true; break;
			}
		} else if (type == 0) { /* main */
			if (tag == 0xA) { /* collection */
				depth++;
				/* application collection (0x01) of usage Mouse */
				if (uval == 0x01 && mouse_depth < 0 && nusages > 0 && usages[0] == HID_USAGE_MOUSE)
					mouse_depth = depth;
			} else if (tag == 0xC) { /* end collection */
				if (depth == mouse_depth) {
					mouse_depth = -1;
					if (cur.x.valid && cur.y.valid)
						break;
				}
				if (depth > 0)
					depth--;
			} else if (tag == 0x8) { /* input */
				uint16_t* off = &hid_id_offset[g.report_id];
				bool constant = uval & 0x1;
				bool variable = uval & 0x2;
				if (!constant && variable && mouse_depth >= 0 &&
					(!id_locked || g.report_id == cur.report_id) && g.report_size &&
					g.report_size <= 32) {
					bool used = false;
					for (uint32_t n = 0; n < g.report_count; n++) {
						uint32_t usage;
						if (have_range) {
							usage = umin + n;
							if (usage > umax)
								break;
						} else if (nusages) {
							usage = usages[n < (uint32_t)nusages ? n : (uint32_t)nusages - 1];
						} else {
							break;
						}
						hid_field_t f;
						f.bit = (uint16_t)(*off + n * g.report_size);
						f.size = (uint8_t)g.report_size;
						f.is_signed = g.logical_min < 0;
						f.valid = 1;
						if (usage >= HID_USAGE_BUTTON1 && usage < HID_USAGE_BUTTON1 + 3) {
							cur.button[usage - HID_USAGE_BUTTON1] = f;
							used = true;
						} else if (usage == HID_USAGE_X) {
							cur.x = f;
							used = true;
						} else if (usage == HID_USAGE_Y) {
							cur.y = f;
							used = true;
						} else if (usage == HID_USAGE_WHEEL) {
							cur.wheel = f;
							used = true;
						}
					}
					if (used) {
						cur.report_id = g.report_id;
						id_locked = true;
					}
				}
				*off = (uint16_t)(*off + g.report_size * g.report_count);
			}
			/* local items only live until the next main item */
			nusages = 0;
			have_range = false;
			umin = umax = 0;
		}
	}

	if (!cur.x.valid || !cur.y.valid)
		return false;
	*out = cur;
	return true;
}

/* the HID 1.11 appendix B.2 boot mouse report */
static void hid_boot_layout(hid_layout_t* l) {
	memset(l, 0, sizeof(*l));
	for (int b = 0; b < 3; b++) {
		l->button[b].bit = (uint16_t)b;
		l->button[b].size = 1;
		l->button[b].valid = 1;
	}
	l->x.bit = 8;
	l->y.bit = 16;
	l->wheel.bit = 24;
	l->x.size = l->y.size = l->wheel.size = 8;
	l->x.is_signed = l->y.is_signed = l->wheel.is_signed = 1;
	l->x.valid = l->y.valid = l->wheel.valid = 1;
}

/* extract a little-endian bit field, sign extended when signed */
static int32_t hid_get(const uint8_t* data, int len, const hid_field_t* f) {
	if (!f->valid)
		return 0;
	uint32_t v = 0;
	for (uint32_t b = 0; b < f->size; b++) {
		uint32_t bit = f->bit + b;
		if ((int)(bit >> 3) >= len)
			return 0; /* not in this (short) report */
		if (data[bit >> 3] & (1u << (bit & 7)))
			v |= 1u << b;
	}
	if (f->is_signed && f->size < 32 && (v & (1u << (f->size - 1))))
		v |= ~0u << f->size;
	return (int32_t)v;
}

/* ------------------------------------------------------------------ */

/**
 * @brief usb_hid_attach -- set a mouse up
 */
void usb_hid_attach(usb_device_t* dev, usb_interface_desc_t* intf, usb_endpoint_desc_t* ep,
					uint16_t report_len) {
	uint8_t ifnum = intf->bInterfaceNumber;
	bool boot = intf->bInterfaceSubClass == HID_SUBCLASS_BOOT;
	if (boot && intf->bInterfaceProtocol == HID_PROTOCOL_KEYBOARD) {
		UARTDebugOut("[usb-hid]: device %d: boot keyboard found (not handled yet) \r\n",
					 dev->address);
		return;
	}
	if (hid_num_mice >= HID_MAX_MICE)
		return;

	hid_layout_t layout;
	bool parsed = false;
	if (report_len && report_len <= HID_REPORT_DESC_MAX) {
		int got = usb_control_msg(dev, USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE,
								  USB_REQ_GET_DESCRIPTOR, USB_DT_REPORT << 8, ifnum, hid_desc_buf,
								  report_len);
		if (got > 0)
			parsed = hid_parse_mouse(hid_desc_buf, (uint32_t)got, &layout);
		else
			UARTDebugOut("[usb-hid]: device %d: no report descriptor (%d) \r\n", dev->address,
						 got);
	}

	if (parsed) {
		/* keep (or put) the device in report protocol: the layout above
		 * describes that protocol */
		if (boot)
			usb_control_msg(dev, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
							HID_REQ_SET_PROTOCOL, 1, ifnum, NULL, 0);
	} else if (boot && intf->bInterfaceProtocol == HID_PROTOCOL_MOUSE) {
		int ret = usb_control_msg(dev, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
								  HID_REQ_SET_PROTOCOL, 0, ifnum, NULL, 0);
		if (ret < 0)
			UARTDebugOut("[usb-hid]: device %d: SET_PROTOCOL(boot) failed (%d) \r\n",
						 dev->address, ret);
		hid_boot_layout(&layout);
	} else {
		return; /* not a mouse */
	}
	/* report only on change; some mice STALL SET_IDLE, which is harmless */
	usb_control_msg(dev, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE, HID_REQ_SET_IDLE, 0,
					ifnum, NULL, 0);

	hid_mouse_t* m = &hid_mice[hid_num_mice++];
	memset(m, 0, sizeof(hid_mouse_t));
	m->dev = dev;
	m->ep = ep->bEndpointAddress & 0xF;
	m->mps = ep->wMaxPacketSize & 0x7FF;
	if (m->mps == 0 || m->mps > HID_REPORT_MAX)
		m->mps = 8;
	m->interval_ms = hid_interval_ms(dev->speed, ep->bInterval);
	m->layout = layout;
	dev->toggle_in[m->ep] = DWC2_PID_DATA0;
	UARTDebugOut("[usb-hid]: device %d: mouse, %s layout, ep %d, %d bytes every %d ms \r\n",
				 dev->address, parsed ? "report descriptor" : "boot", m->ep, m->mps,
				 m->interval_ms);
	/* UARTDebugOut takes at most 7 arguments after the format */
	UARTDebugOut("[usb-hid]:   report id %d, buttons @%d, x @%d/%d, y @%d/%d \r\n",
				 (int)layout.report_id, (int)layout.button[0].bit, (int)layout.x.bit,
				 (int)layout.x.size, (int)layout.y.bit, (int)layout.y.size);
	UARTDebugOut("[usb-hid]:   wheel @%d/%d \r\n", layout.wheel.valid ? (int)layout.wheel.bit : 0,
				 layout.wheel.valid ? (int)layout.wheel.size : 0);
}

static void hid_mouse_report(hid_mouse_t* m, int len) {
	const uint8_t* data = m->report;
	if (m->raw_logged < HID_LOG_RAW) {
		m->raw_logged++;
		UARTDebugOut("[usb-hid]: raw report (%d):", len);
		for (int i = 0; i < len && i < 12; i++)
			UARTDebugOut(" %x", data[i]);
		UARTDebugOut(" \r\n");
	}
	if (m->layout.report_id) {
		/* other report IDs (consumer keys, ...) are not for us */
		if (len < 1 || data[0] != m->layout.report_id)
			return;
		data++;
		len--;
	}

	uint8_t raw = 0;
	for (int b = 0; b < 3; b++)
		if (hid_get(data, len, &m->layout.button[b]))
			raw |= (uint8_t)(1u << b);
	int32_t dx = hid_get(data, len, &m->layout.x);
	int32_t dy = hid_get(data, len, &m->layout.y);
	int32_t wheel = hid_get(data, len, &m->layout.wheel);
	if (!dx && !dy && !wheel && raw == m->last_buttons)
		return;
	m->last_buttons = raw;

	int32_t w = (int32_t)AuConsoleGetScreenWidth();
	int32_t h = (int32_t)AuConsoleGetScreenHeight();
	if (w <= 0 || h <= 0) {
		w = 1024;
		h = 768;
	}
	hid_x += dx;
	hid_y += dy;
	if (hid_x < 0)
		hid_x = 0;
	if (hid_y < 0)
		hid_y = 0;
	if (hid_x >= w)
		hid_x = w - 1;
	if (hid_y >= h)
		hid_y = h - 1;

	uint8_t buttons = 0;
	if (raw & 0x01)
		buttons |= LEFT_CLICK;
	if (raw & 0x02)
		buttons |= RIGHT_CLICK;
	if (raw & 0x04)
		buttons |= MIDDLE_CLICK;
	if (wheel > 0)
		buttons |= MOUSE_SCROLL_UP;
	else if (wheel < 0)
		buttons |= MOUSE_SCROLL_DOWN;

	AuInputMessage msg;
	memset(&msg, 0, sizeof(msg));
	msg.type = AU_INPUT_MOUSE;
	msg.xpos = hid_x;
	msg.ypos = hid_y;
	msg.button_state = buttons;
	AuDevWriteMice(&msg);

	static uint32_t logged;
	if (logged < 16) {
		logged++;
		UARTDebugOut("[usb-hid]: pointer %d,%d buttons %x \r\n", hid_x, hid_y, buttons);
	}
}

/* mark the thread asleep; the next tick switches away from it */
static void hid_sleep_ms(uint64_t ms) {
	AA64Thread* self = hid_thread;
	/* never wait holding the BKL: every syscall in the system needs it */
	AuBKLReleaseAll();
	AuSleepThread(self, ms);
	while (*(volatile uint8_t*)&self->state == THREAD_STATE_SLEEP)
		__asm__ volatile("wfi");
	/* the scheduler resumes kernel threads with one BKL level */
	AuBKLReleaseAll();
}

static void hid_poll_thread(uint64_t arg) {
	(void)arg;
	while (!hid_thread)
		__asm__ volatile("yield");
	/* the poller touches only the controller and the lock-free input
	 * ring: it runs without the BKL, so a USB transaction (a split
	 * transaction to a low speed mouse takes a millisecond or more) never
	 * blocks other processes' syscalls */
	AuBKLReleaseAll();
	UARTDebugOut("[usb-hid]: polling %d mouse(s) \r\n", hid_num_mice);
	/* poll cost: every poll busy-waits on the controller (core 0) */
	uint64_t stat_since = dwc2_now_us(), stat_max = 0;
	uint32_t stat_slow = 0, stat_polls = 0;

	for (;;) {
		uint64_t now = dwc2_now_us() / 1000;
		uint64_t next = now + HID_POLL_MAX_MS;
		for (int i = 0; i < hid_num_mice; i++) {
			hid_mouse_t* m = &hid_mice[i];
			if (m->errors >= HID_MAX_ERRORS)
				continue;
			if (now >= m->next_poll_ms) {
				m->next_poll_ms = now + m->interval_ms;
				/* a preemption since the last sleep hands back a level */
				AuBKLReleaseAll();
				uint64_t t0 = dwc2_now_us();
				int ret = usb_interrupt_in(m->dev, m->ep, m->mps, m->report, m->mps);
				uint64_t took = dwc2_now_us() - t0;
				stat_polls++;
				if (took > stat_max)
					stat_max = took;
				if (took > 2000)
					stat_slow++;
				if (ret > 0) {
					m->errors = 0;
					hid_mouse_report(m, ret);
				} else if (ret < 0) {
					if (++m->errors == HID_MAX_ERRORS)
						UARTDebugOut("[usb-hid]: device %d: giving up after errors (%d) \r\n",
									 m->dev->address, ret);
				}
			}
			if (m->next_poll_ms < next)
				next = m->next_poll_ms;
		}
		if (dwc2_now_us() - stat_since > 5000000) {
			if (stat_slow)
				UARTDebugOut("[usb-hid]: %d of %d polls over 2 ms (max %d us) \r\n", stat_slow,
							 stat_polls, stat_max);
			stat_since = dwc2_now_us();
			stat_max = 0;
			stat_slow = stat_polls = 0;
		}
		now = dwc2_now_us() / 1000;
		hid_sleep_ms(next > now ? next - now : 1);
	}
}

/* kernel timer callback, first ticks after the scheduler started */
static void hid_start_thread(void* param) {
	(void)param;
	if (hid_thread)
		return;
	hid_thread = AuCreateKthread(hid_poll_thread, AuGetRootPageTable(), (char*)"dwc2-hid");
	if (!hid_thread)
		UARTDebugOut("[usb-hid]: could not create the polling thread \r\n");
}

/**
 * @brief usb_hid_start -- start polling if a mouse was found
 */
void usb_hid_start() {
	if (!hid_num_mice)
		return;
	hid_x = (int32_t)AuConsoleGetScreenWidth() / 2;
	hid_y = (int32_t)AuConsoleGetScreenHeight() / 2;
	if (AuroraTimerStart(HID_START_DELAY_MS, hid_start_thread, NULL, -1) < 0)
		UARTDebugOut("[usb-hid]: no kernel timer free \r\n");
}
