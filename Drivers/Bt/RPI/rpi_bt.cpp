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
#include "rpi_bt.h"
#include <Board/RPI3bp/rpi3bp_gpio.h>
#include <Board/RPI3bp/rpi3bp.h>
#include <Hal/AA64/sched.h>
#include <Mm/vmmngr.h>
#include <Mm/kmalloc.h>
#include <Fs/vfs.h>
#include <Sync/bkl.h>
#include <Drivers/uart.h>
#include <Bt/bt.h>
#include <timer.h>
#include <aurora.h>
#include <string.h>
#include <_null.h>

/*
 * The UART work below follows the PL011 TRM and the BCM2835 ARM
 * Peripherals manual; the controller side is the Bluetooth Core HCI UART
 * transport (H4) plus the Broadcom vendor commands every BCM UART
 * controller understands.
 */

/* ---- PL011 (UART0) ---- */
#define PL011_PHYS 0x3F201000ULL

#define PL_DR	0x00
#define PL_FR	0x18
#define PL_IBRD 0x24
#define PL_FBRD 0x28
#define PL_LCRH 0x2C
#define PL_CR	0x30
#define PL_IFLS 0x34
#define PL_IMSC 0x38
#define PL_ICR	0x44

#define PL_FR_BUSY (1u << 3)
#define PL_FR_RXFE (1u << 4)
#define PL_FR_TXFF (1u << 5)
#define PL_FR_TXFE (1u << 7)

#define PL_DR_ERRORS 0xF00u /* framing, parity, break, overrun */

#define PL_LCRH_FEN	  (1u << 4)
#define PL_LCRH_WLEN8 (3u << 5)

#define PL_CR_UARTEN (1u << 0)
#define PL_CR_TXE	 (1u << 8)
#define PL_CR_RXE	 (1u << 9)
#define PL_CR_RTSEN	 (1u << 14)
#define PL_CR_CTSEN	 (1u << 15)

/* RX and TX FIFO level select: 1/2 full (8 of 16). With RTSEN the PL011
 * drops RTS at the RX level, leaving 8 bytes of room for whatever the
 * controller still sends after seeing it */
#define PL_IFLS_HALF ((2u << 3) | 2u)

/* ---- pins ---- */
#define BT_PIN_CTS 30 /* ALT3 CTS0 */
#define BT_PIN_RTS 31 /* ALT3 RTS0 */
#define BT_PIN_TX  32 /* ALT3 TXD0 */
#define BT_PIN_RX  33 /* ALT3 RXD0 */
#define BT_PIN_LPO 43 /* ALT0 GPCLK2: 32 kHz sleep clock of the combo chip */

/* ---- HCI ---- */
#define H4_CMD 0x01
#define H4_EVT 0x04

#define HCI_EV_CMD_COMPLETE 0x0E
#define HCI_EV_CMD_STATUS	0x0F

#define HCI_OP_RESET			  0x0C03
#define HCI_OP_READ_LOCAL_VERSION 0x1001

/* Broadcom vendor commands */
#define BCM_OP_WRITE_BDADDR		   0xFC01
#define BCM_OP_UPDATE_BAUD		   0xFC18
#define BCM_OP_DOWNLOAD_MINIDRIVER 0xFC2E
#define BCM_OP_LAUNCH_RAM		   0xFC4E

#define BT_CMD_TIMEOUT_MS	1000
#define BT_START_DELAY_MS	200

/* the Raspberry Pi Foundation OUI the Pi 3 family uses for its radios */
#define RPI_OUI0 0xB8
#define RPI_OUI1 0x27
#define RPI_OUI2 0xEB

typedef struct _bt_patch_ {
	uint8_t* data;
	uint32_t len;
} BtPatch;

static volatile uint32_t* pl011;
static uint32_t uart_clk;
static AA64Thread* bt_thr;
static uint32_t rx_errors;
static BtPatch patch_4345c0;
static BtPatch patch_43430a1;

/* ------------------------------------------------------------------
 * time and sleeping
 * ------------------------------------------------------------------ */

static uint64_t now_us() {
	uint64_t cnt, frq;
	__asm__ volatile("isb\n mrs %0, cntpct_el0" : "=r"(cnt));
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frq));
	return (cnt / frq) * 1000000ULL + ((cnt % frq) * 1000000ULL) / frq;
}

static void delay_us(uint64_t us) {
	uint64_t start = now_us();
	while (now_us() - start < us)
		;
}

/*
 * thr_sleep -- sleep the driver thread. Never wait holding the BKL; the
 * scheduler resumes kernel threads with one level, which is dropped
 * again here because the bring-up only touches this driver's hardware
 * and the (spinlocked) mailbox
 */
static void thr_sleep(uint64_t ms) {
	AA64Thread* self = bt_thr;
	AuBKLReleaseAll();
	AuSleepThread(self, ms ? ms : 1);
	while (*(volatile uint8_t*)&self->state == THREAD_STATE_SLEEP)
		__asm__ volatile("wfi");
	AuBKLReleaseAll();
}

/*
 * thr_sleep_take_bkl -- sleep, and come back holding the one BKL level
 * the scheduler hands a resumed kernel thread. The host stack expects
 * to run like any other kernel thread (it publishes /dev/bt0 and reads
 * its bond file through the VFS)
 */
static void thr_sleep_take_bkl(uint64_t ms) {
	AA64Thread* self = bt_thr;
	AuBKLReleaseAll();
	AuSleepThread(self, ms ? ms : 1);
	while (*(volatile uint8_t*)&self->state == THREAD_STATE_SLEEP)
		__asm__ volatile("wfi");
}

/* ------------------------------------------------------------------
 * PL011
 * ------------------------------------------------------------------ */

static inline uint32_t pl_read(uint32_t off) {
	return pl011[off / 4];
}

static inline void pl_write(uint32_t off, uint32_t val) {
	pl011[off / 4] = val;
}

/*
 * pl_wait_idle -- wait until the transmitter has shifted everything out
 */
static void pl_wait_idle() {
	uint64_t deadline = now_us() + 100000;
	while ((pl_read(PL_FR) & PL_FR_BUSY) && now_us() < deadline)
		;
}

/*
 * pl_program -- (re)program the PL011 for a baud rate, 8N1, FIFOs on,
 * RTS/CTS hardware flow control, no interrupts. Sequence from the PL011
 * TRM: disable, let the transmitter drain, flush the FIFOs by clearing
 * FEN, write the divisors and latch them with an LCR_H write, enable
 * @return false if the UART clock cannot produce the rate
 */
static bool pl_program(uint32_t baud) {
	/* divisor in 1/64ths: UARTCLK / (16 * baud) */
	uint64_t div = ((uint64_t)uart_clk * 4 + baud / 2) / baud;
	if (div < 64 || (div >> 6) > 0xFFFF)
		return false;

	pl_write(PL_CR, 0);
	pl_wait_idle();
	pl_write(PL_LCRH, 0);
	pl_write(PL_IBRD, (uint32_t)(div >> 6));
	pl_write(PL_FBRD, (uint32_t)(div & 63));
	pl_write(PL_LCRH, PL_LCRH_WLEN8 | PL_LCRH_FEN);
	pl_write(PL_IFLS, PL_IFLS_HALF);
	pl_write(PL_IMSC, 0);
	pl_write(PL_ICR, 0x7FF);
	pl_write(PL_CR, PL_CR_UARTEN | PL_CR_TXE | PL_CR_RXE | PL_CR_RTSEN | PL_CR_CTSEN);
	return true;
}

static void pl_drain_rx() {
	while (!(pl_read(PL_FR) & PL_FR_RXFE))
		(void)pl_read(PL_DR);
}

/*
 * pl_write_bytes -- send bytes, blocking while the controller holds CTS
 * @return false if the FIFO never drained (controller dead or held off)
 */
static bool pl_write_bytes(const uint8_t* p, uint32_t n) {
	uint64_t deadline = now_us() + (uint64_t)BT_CMD_TIMEOUT_MS * 1000;
	for (uint32_t i = 0; i < n; i++) {
		while (pl_read(PL_FR) & PL_FR_TXFF) {
			if (now_us() >= deadline)
				return false;
		}
		pl_write(PL_DR, p[i]);
	}
	return true;
}

/*
 * pl_getc -- next received byte, -1 at the deadline. Polls hard for 2 ms
 * (a command answer normally lands well inside that), then sleeps a
 * tick between polls; RTS keeps the controller from overrunning the
 * FIFO meanwhile
 */
static int pl_getc(uint64_t deadline) {
	uint64_t idle_since = now_us();
	for (;;) {
		if (!(pl_read(PL_FR) & PL_FR_RXFE)) {
			uint32_t d = pl_read(PL_DR);
			if (d & PL_DR_ERRORS)
				rx_errors++;
			return (int)(d & 0xFF);
		}
		uint64_t t = now_us();
		if (t >= deadline)
			return -1;
		if (t - idle_since > 2000)
			thr_sleep(1);
	}
}

/* ------------------------------------------------------------------
 * HCI over H4, just enough for controller setup
 * ------------------------------------------------------------------ */

/*
 * hci_read_event -- next HCI event. Bytes before an event indicator are
 * skipped: nothing but events is expected while the host stack is not
 * running yet
 */
static bool hci_read_event(uint8_t* code, uint8_t* plen, uint8_t* params, uint64_t deadline) {
	int c;
	do {
		c = pl_getc(deadline);
		if (c < 0)
			return false;
	} while (c != H4_EVT);
	if ((c = pl_getc(deadline)) < 0)
		return false;
	*code = (uint8_t)c;
	if ((c = pl_getc(deadline)) < 0)
		return false;
	*plen = (uint8_t)c;
	for (uint32_t i = 0; i < *plen; i++) {
		if ((c = pl_getc(deadline)) < 0)
			return false;
		params[i] = (uint8_t)c;
	}
	return true;
}

/*
 * hci_cmd -- send one HCI command and wait for its Command Complete (or
 * a Command Status for it)
 * @param rsp -- optional, receives the return parameters (status first)
 * @param rsplen -- in: room in rsp, out: bytes stored
 * @return the HCI status (0 = success), -1 on timeout, -2 if the
 * command could not be sent
 */
static int hci_cmd(uint16_t op, const uint8_t* param, uint8_t plen, uint8_t* rsp,
				   uint8_t* rsplen, uint32_t timeout_ms) {
	uint8_t hdr[4];
	hdr[0] = H4_CMD;
	hdr[1] = (uint8_t)op;
	hdr[2] = (uint8_t)(op >> 8);
	hdr[3] = plen;
	if (!pl_write_bytes(hdr, 4) || (plen && !pl_write_bytes(param, plen)))
		return -2;

	uint64_t deadline = now_us() + (uint64_t)timeout_ms * 1000;
	uint8_t code, len;
	uint8_t ev[256];
	for (;;) {
		if (!hci_read_event(&code, &len, ev, deadline))
			return -1;
		/* Command Complete: ncmd, opcode, status, return parameters */
		if (code == HCI_EV_CMD_COMPLETE && len >= 4 &&
			(uint16_t)(ev[1] | (ev[2] << 8)) == op) {
			if (rsp && rsplen) {
				uint8_t n = (uint8_t)(len - 3);
				if (n > *rsplen)
					n = *rsplen;
				memcpy(rsp, ev + 3, n);
				*rsplen = n;
			}
			return ev[3];
		}
		/* Command Status: status, ncmd, opcode */
		if (code == HCI_EV_CMD_STATUS && len >= 4 &&
			(uint16_t)(ev[2] | (ev[3] << 8)) == op)
			return ev[0];
		/* anything else (vendor events after a reset) is not ours */
	}
}

/*
 * bt_reset -- HCI Reset with a few retries; the first one after power up
 * or after the patch launches can get lost while the controller boots
 */
static bool bt_reset() {
	for (int i = 0; i < 3; i++) {
		pl_drain_rx();
		if (hci_cmd(HCI_OP_RESET, NULL, 0, NULL, NULL, BT_CMD_TIMEOUT_MS) == 0)
			return true;
		thr_sleep(50);
	}
	return false;
}

/*
 * bt_read_version -- HCI Read Local Version
 * @return LMP subversion (identifies the chip and patch), 0 on failure
 */
static uint16_t bt_read_version(const char* when) {
	uint8_t rsp[16];
	uint8_t n = sizeof rsp;
	if (hci_cmd(HCI_OP_READ_LOCAL_VERSION, NULL, 0, rsp, &n, BT_CMD_TIMEOUT_MS) != 0 || n < 9)
		return 0;
	/* status, hci version, hci revision(2), lmp version, manufacturer(2),
	 * lmp subversion(2) */
	uint16_t rev = (uint16_t)(rsp[2] | (rsp[3] << 8));
	uint16_t manuf = (uint16_t)(rsp[5] | (rsp[6] << 8));
	uint16_t subver = (uint16_t)(rsp[7] | (rsp[8] << 8));
	UARTDebugOut("[rpi-bt]: %s: hci %x rev %x lmp %x manufacturer %x subversion %x \r\n", when,
				 rsp[1], rev, rsp[4], manuf, subver);
	return subver;
}

/*
 * bt_set_baud -- move controller and PL011 to a new rate together, then
 * prove the link with a Read Local Version
 */
static bool bt_set_baud(uint32_t baud) {
	uint8_t p[6];
	p[0] = 0;
	p[1] = 0;
	p[2] = (uint8_t)baud;
	p[3] = (uint8_t)(baud >> 8);
	p[4] = (uint8_t)(baud >> 16);
	p[5] = (uint8_t)(baud >> 24);
	if (hci_cmd(BCM_OP_UPDATE_BAUD, p, 6, NULL, NULL, BT_CMD_TIMEOUT_MS) != 0)
		return false;
	/* the answer came at the old rate; give the controller a moment to
	 * switch before the PL011 follows */
	delay_us(5000);
	if (!pl_program(baud))
		return false;
	pl_drain_rx();
	uint8_t rsp[16];
	uint8_t n = sizeof rsp;
	return hci_cmd(HCI_OP_READ_LOCAL_VERSION, NULL, 0, rsp, &n, BT_CMD_TIMEOUT_MS) == 0;
}

/*
 * bt_download_patch -- stream an .hcd file. The file is a sequence of
 * complete HCI commands (opcode, length, parameters): write RAM chunks
 * and a final launch RAM. Each one must be acknowledged before the next
 */
static bool bt_download_patch(const BtPatch* fw) {
	if (hci_cmd(BCM_OP_DOWNLOAD_MINIDRIVER, NULL, 0, NULL, NULL, BT_CMD_TIMEOUT_MS) != 0) {
		UARTDebugOut("[rpi-bt]: download minidriver refused \r\n");
		return false;
	}
	/* the minidriver needs a moment before it accepts RAM writes */
	thr_sleep(50);

	uint64_t t0 = now_us();
	uint32_t off = 0, cmds = 0;
	bool launched = false;
	while (off + 3 <= fw->len) {
		uint16_t op = (uint16_t)(fw->data[off] | (fw->data[off + 1] << 8));
		uint8_t plen = fw->data[off + 2];
		if (off + 3 + plen > fw->len) {
			UARTDebugOut("[rpi-bt]: patch truncated at offset %d \r\n", off);
			return false;
		}
		int st = hci_cmd(op, fw->data + off + 3, plen, NULL, NULL, BT_CMD_TIMEOUT_MS);
		if (st != 0) {
			UARTDebugOut("[rpi-bt]: patch command %x at offset %d failed (%d) \r\n", op, off,
						 st);
			return false;
		}
		if (op == BCM_OP_LAUNCH_RAM)
			launched = true;
		off += 3 + plen;
		cmds++;
	}
	UARTDebugOut("[rpi-bt]: patch sent, %d commands, %d bytes in %d ms \r\n", cmds, off,
				 (uint32_t)((now_us() - t0) / 1000));
	if (!launched)
		UARTDebugOut("[rpi-bt]: patch had no launch RAM command \r\n");
	/* the controller restarts into the patched firmware */
	thr_sleep(250);
	return true;
}

/*
 * bt_write_bdaddr -- the combo chip has no address of its own (the ROM
 * reports a shared default), so give it the one the Pi's firmware stack
 * would: Pi OUI + the low 24 bits of the board serial, each byte xor 0xAA
 */
static void bt_write_bdaddr() {
	uint64_t serial = AuRPIGetBoardSerial();
	if (!serial) {
		UARTDebugOut("[rpi-bt]: no board serial, keeping the controller address \r\n");
		return;
	}
	uint8_t a[6]; /* little endian, as HCI carries BD_ADDR */
	a[0] = (uint8_t)(serial ^ 0xAA);
	a[1] = (uint8_t)((serial >> 8) ^ 0xAA);
	a[2] = (uint8_t)((serial >> 16) ^ 0xAA);
	a[3] = RPI_OUI2;
	a[4] = RPI_OUI1;
	a[5] = RPI_OUI0;
	if (hci_cmd(BCM_OP_WRITE_BDADDR, a, 6, NULL, NULL, BT_CMD_TIMEOUT_MS) == 0)
		UARTDebugOut("[rpi-bt]: address b8:27:eb:%x:%x:%x \r\n", a[2], a[1], a[0]);
	else
		UARTDebugOut("[rpi-bt]: write BD_ADDR refused \r\n");
}

/* ------------------------------------------------------------------
 * bring-up
 * ------------------------------------------------------------------ */

/*
 * load_patch -- read a patch file from the initramfs into the heap
 */
static void load_patch(const char* path, BtPatch* out) {
	out->data = NULL;
	out->len = 0;
	AuVFSNode* fsys = AuVFSFind((char*)path);
	AuVFSNode* file = AuVFSOpen((char*)path);
	if (!fsys || !file)
		return;
	uint32_t size = (uint32_t)file->size;
	if (size == 0 || size > 512 * 1024) {
		UARTDebugOut("[rpi-bt]: %s has a bad size (%d) \r\n", path, size);
		AuVFSNodeClose(fsys, file);
		return;
	}
	/* block reads store whole clusters: room for one past the end */
	uint32_t room = ((size + 4095) & ~4095u) + 4096;
	uint8_t* buf = (uint8_t*)kmalloc(room);
	if (!buf) {
		AuVFSNodeClose(fsys, file);
		return;
	}
	uint32_t got = 0;
	while (file->eof != 1 && got + 4096 <= room) {
		size_t n = AuVFSNodeReadBlock(fsys, file, (uint64_t*)(buf + got));
		if (n == 0)
			break;
		got += (uint32_t)n;
	}
	AuVFSNodeClose(fsys, file);
	if (got < size) {
		UARTDebugOut("[rpi-bt]: short read of %s (%d of %d) \r\n", path, got, size);
		kfree(buf);
		return;
	}
	out->data = buf;
	out->len = size;
	UARTDebugOut("[rpi-bt]: %s loaded, %d bytes \r\n", path, size);
}

static void free_patch(BtPatch* p) {
	if (p->data)
		kfree(p->data);
	p->data = NULL;
	p->len = 0;
}

/*
 * bt_power_cycle -- BT_ON low, then high: the controller boots its ROM
 * at 115200 with no patch, whatever ran before
 */
static void bt_power_cycle() {
	if (!AuRPISetExpGPIO(RPI_EXP_GPIO_BT_ON, 0))
		UARTDebugOut("[rpi-bt]: firmware did not confirm BT_ON low \r\n");
	thr_sleep(50);
	if (!AuRPISetExpGPIO(RPI_EXP_GPIO_BT_ON, 1))
		UARTDebugOut("[rpi-bt]: firmware did not confirm BT_ON high \r\n");
	/* controller boot time */
	thr_sleep(200);
}

/*
 * bt_bring_up -- power cycle, patch and speed up the controller
 * @param oper_baud -- final rate; BT_INIT_BAUD skips the rate switches
 */
static bool bt_bring_up(uint32_t oper_baud) {
	if (!pl_program(BT_INIT_BAUD)) {
		UARTDebugOut("[rpi-bt]: UART clock %d cannot do %d baud \r\n", uart_clk, BT_INIT_BAUD);
		return false;
	}
	bt_power_cycle();
	pl_drain_rx();

	if (!bt_reset()) {
		UARTDebugOut("[rpi-bt]: controller does not answer HCI Reset \r\n");
		return false;
	}
	uint16_t subver = bt_read_version("rom");

	const BtPatch* fw = NULL;
	if (subver == RPI_BT_SUBVER_43430A1)
		fw = &patch_43430a1;
	else
		fw = &patch_4345c0; /* the 3B+ part; also tried for unknown parts */
	if (subver != RPI_BT_SUBVER_4345C0 && subver != RPI_BT_SUBVER_43430A1)
		UARTDebugOut("[rpi-bt]: unexpected controller subversion %x \r\n", subver);

	if (oper_baud != BT_INIT_BAUD && !bt_set_baud(oper_baud)) {
		UARTDebugOut("[rpi-bt]: switch to %d baud failed \r\n", oper_baud);
		return false;
	}

	if (fw->data) {
		if (!bt_download_patch(fw))
			return false;
		/* patched firmware starts over at the initial rate */
		if (!pl_program(BT_INIT_BAUD))
			return false;
		pl_drain_rx();
		if (!bt_reset()) {
			UARTDebugOut("[rpi-bt]: no answer after the patch launched \r\n");
			return false;
		}
		if (oper_baud != BT_INIT_BAUD && !bt_set_baud(oper_baud)) {
			UARTDebugOut("[rpi-bt]: switch to %d baud after patching failed \r\n", oper_baud);
			return false;
		}
		bt_read_version("patched");
	} else {
		UARTDebugOut("[rpi-bt]: no patch file, running the ROM firmware \r\n");
	}

	bt_write_bdaddr();
	UARTDebugOut("[rpi-bt]: controller up at %d baud \r\n", oper_baud);
	return true;
}

/*
 * rpibt_thread -- the whole bring-up, then hand over to the host stack
 */
static void rpibt_thread(uint64_t arg) {
	(void)arg;
	while (!bt_thr)
		__asm__ volatile("yield");

	/* VFS work first, while this thread holds the BKL it started with */
	load_patch(RPI_BT_FW_4345C0, &patch_4345c0);
	load_patch(RPI_BT_FW_43430A1, &patch_43430a1);
	if (!patch_4345c0.data)
		UARTDebugOut("[rpi-bt]: %s missing \r\n", RPI_BT_FW_4345C0);
	AuBKLReleaseAll();

	uint64_t t0 = now_us();
	bool up = bt_bring_up(BT_OPER_BAUD);
	if (!up) {
		UARTDebugOut("[rpi-bt]: retrying at %d baud only \r\n", BT_INIT_BAUD);
		up = bt_bring_up(BT_INIT_BAUD);
	}
	free_patch(&patch_4345c0);
	free_patch(&patch_43430a1);
	if (rx_errors)
		UARTDebugOut("[rpi-bt]: %d receive errors during setup \r\n", rx_errors);

	if (up) {
		UARTDebugOut("[rpi-bt]: setup took %d ms \r\n", (uint32_t)((now_us() - t0) / 1000));
		pl_drain_rx();
		thr_sleep_take_bkl(1);
		AuBtSetLocalName(XENEVA_OS_NAME);
		if (AuBtH4UartReady(pl011) != 0)
			UARTDebugOut("[rpi-bt]: host stack refused the UART \r\n");
	} else {
		UARTDebugOut("[rpi-bt]: Bluetooth controller unavailable \r\n");
		AuRPISetExpGPIO(RPI_EXP_GPIO_BT_ON, 0);
	}

	/* nothing left to do on this thread; bthost owns the UART now */
	for (;;)
		thr_sleep(60000);
}

/* kernel timer callback, first ticks after the scheduler started:
 * kernel threads created before AuSchedulerInitialize do not survive */
static void rpibt_start_thread(void* param) {
	(void)param;
	if (bt_thr)
		return;
	bt_thr = AuCreateKthread(rpibt_thread, AuGetRootPageTable(), (char*)"rpi-bt");
	if (!bt_thr)
		UARTDebugOut("[rpi-bt]: could not create the setup thread \r\n");
}

/*
 * rpi_bt_initialize -- claim the PL011 and its pins, set the UART clock;
 * the controller itself is brought up on rpibt_thread
 */
static bool rpi_bt_initialize() {
	pl011 = (volatile uint32_t*)AuMapMMIO(PL011_PHYS, 1);
	if (!pl011) {
		UARTDebugOut("[rpi-bt]: cannot map the PL011 \r\n");
		return false;
	}
	pl_write(PL_CR, 0);
	pl_write(PL_IMSC, 0);
	pl_write(PL_ICR, 0x7FF);

	AuRPI3BPGpioMap();
	AuRPIGPIOSetFunction(BT_PIN_TX, RPI_GPIO_FUNC_ALT3);
	AuRPIGPIOSetFunction(BT_PIN_RX, RPI_GPIO_FUNC_ALT3);
	AuRPIGPIOSetFunction(BT_PIN_CTS, RPI_GPIO_FUNC_ALT3);
	AuRPIGPIOSetFunction(BT_PIN_RTS, RPI_GPIO_FUNC_ALT3);
	AuRPIGPIOSetPull(BT_PIN_TX, RPI_GPIO_PULL_NONE);
	AuRPIGPIOSetPull(BT_PIN_RX, RPI_GPIO_PULL_UP);
	AuRPIGPIOSetPull(BT_PIN_CTS, RPI_GPIO_PULL_UP);
	AuRPIGPIOSetPull(BT_PIN_RTS, RPI_GPIO_PULL_NONE);
	AuRPIGPIOSetFunction(BT_PIN_LPO, RPI_GPIO_FUNC_ALT0);

	uart_clk = AuRPISetClockRate(RPI_FW_CLOCK_UART, BT_UART_CLOCK);
	if (!uart_clk)
		uart_clk = AuRPIGetClockRate(RPI_FW_CLOCK_UART);
	if (!uart_clk) {
		UARTDebugOut("[rpi-bt]: UART clock unknown, assuming %d Hz \r\n", BT_UART_CLOCK);
		uart_clk = BT_UART_CLOCK;
	}
	UARTDebugOut("[rpi-bt]: PL011 on GPIO30-33, UART clock %d Hz \r\n", uart_clk);

	if (AuroraTimerStart(BT_START_DELAY_MS, rpibt_start_thread, NULL, -1) < 0) {
		UARTDebugOut("[rpi-bt]: no kernel timer free \r\n");
		return false;
	}
	return true;
}

/*
 * AuDriverMain -- driver entry, runs from the driver manager before the
 * scheduler starts
 */
AU_EXTERN AU_EXPORT int AuDriverMain(void* drv) {
	(void)drv;
	UARTDebugOut("[rpi-bt]: Raspberry Pi 3B+ Bluetooth (CYW43455 over PL011) \r\n");
	return rpi_bt_initialize() ? 0 : 1;
}

/*
 * AuDriverUnload -- the host stack keeps using the UART, stay resident
 */
AU_EXTERN AU_EXPORT int AuDriverUnload(void* drv) {
	(void)drv;
	return 0;
}
