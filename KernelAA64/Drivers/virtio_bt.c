/**
* @file virtio_bt.c
*
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

/*
 * virtio-bt: mock Bluetooth HCI controller for testing without real hardware.
 *
 * Implements AuBtUsbOps so the in-kernel BT stack (Bt/host.c) can run
 * scan / connect / pair / audio against simulated LE advertisers and a
 * simulated peer device. Used only as a fallback: Bt/host.c first probes
 * UART1 for a Zephyr-style serial bridge (QEMU -serial unix:/tmp/bt-server-bredr
 * <-> btproxy <-> BlueZ) and uses the real host adapter when present.
 *
 * Event formats match what on_event() in host.c parses:
 *   0x0E command-complete: ev[3-4]=opcode  ev[5]=status  ev[6...]=return-params
 *   0x0F command-status:   ev[2]=status    ev[4-5]=opcode
 *   0x3E/0x02 LE adv report: sub=0x02  r[0]=num  r[1]=evt_type  r[2]=addr_type
 *                            r[3..8]=addr  r[9]=dlen  r[10..]=ad-data  rssi
 *   0x3E/0x01 LE conn complete: ev[3]=status  ev[4-5]=handle
 *                               ev[6]=addr_type  ev[7..12]=addr
 */

#include <stdint.h>
#include <string.h>
#include <Bt/bt.h>
#include <Drivers/uart.h>
#include <_null.h>

/* ---- HCI opcodes used by host.c ---- */
#define HCI_RESET        0x0C03
#define HCI_SET_EVT_MASK 0x0C01
#define HCI_WRITE_LE_HST 0x0C6D
#define HCI_READ_VER     0x1001
#define HCI_READ_BD      0x1009
#define HCI_READ_BUF     0x1005
#define HCI_DISCONNECT   0x0406
#define LE_SET_EVT_MASK  0x2001
#define LE_READ_BUF      0x2002
#define LE_READ_FEAT     0x2003
#define LE_SET_SCAN_PAR  0x200B
#define LE_SET_SCAN_EN   0x200C
#define LE_CREATE_CONN   0x200D
#define LE_START_ENC     0x2019
#define LE_LTK_REPLY     0x201A
#define LE_LTK_NEG       0x201B
#define LE_READ_BUF_V2   0x2060
#define LE_SET_CIG       0x2062
#define LE_CREATE_CIS    0x2064
#define LE_SETUP_ISO     0x206E
#define LE_SET_HOST_FEAT 0x2074

/* ---- mock controller state ---- */
static int mockReady;
static uint8_t mockBd[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 };
static int mockScanning;
static int mockConnected;
static uint16_t mockHandle = 0x0001;
static uint8_t mockPeer[6] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01 };
static int mockAdvSent;
static uint16_t mockCisHandle = 0x0008;

/* ---- event ring (mock -> host.c via vbtIntr) ---- */
#define EVTQ_SZ 32
static uint8_t evtq[EVTQ_SZ][256];
static uint16_t evtqLen[EVTQ_SZ];
static int evtqHead, evtqTail;

static void evtPush(const uint8_t* d, uint16_t n) {
	int next = (evtqTail + 1) % EVTQ_SZ;
	if (next == evtqHead)
		return;
	memcpy(evtq[evtqTail], d, n);
	evtqLen[evtqTail] = n;
	evtqTail = next;
}

/* ---- ACL ring (mock -> host.c via vbtBulkIn) ---- */
#define ACLQ_SZ 8
static uint8_t aclq[ACLQ_SZ][256];
static uint16_t aclqLen[ACLQ_SZ];
static int aclqHead, aclqTail;

static void aclPush(const uint8_t* d, uint16_t n) {
	int next = (aclqTail + 1) % ACLQ_SZ;
	if (next == aclqHead)
		return;
	memcpy(aclq[aclqTail], d, n);
	aclqLen[aclqTail] = n;
	aclqTail = next;
}

/* ---- HCI event generators ---- */

static void evtCmdComplete(uint16_t op, const uint8_t* ret, uint8_t retlen) {
	uint8_t ev[128];
	ev[0] = 0x0E;
	ev[1] = (uint8_t)(3 + retlen);
	ev[2] = 1;
	ev[3] = (uint8_t)op;
	ev[4] = (uint8_t)(op >> 8);
	memcpy(ev + 5, ret, retlen);
	evtPush(ev, (uint16_t)(5 + retlen));
}

static void evtCmdStatus(uint16_t op, uint8_t status) {
	uint8_t ev[6];
	ev[0] = 0x0F;
	ev[1] = 4;
	ev[2] = status;
	ev[3] = 1;
	ev[4] = (uint8_t)op;
	ev[5] = (uint8_t)(op >> 8);
	evtPush(ev, 6);
}

static void evtAdvReport(const uint8_t* addr, const char* name) {
	uint8_t ev[80];
	uint16_t off = 0;
	int nl = (int)strlen(name);
	uint8_t dlen = (uint8_t)(3 + 2 + nl); /* flags AD + name AD */
	ev[off++] = 0x3E;
	ev[off++] = (uint8_t)(12 + dlen);
	ev[off++] = 0x02; /* LE Advertising Report */
	ev[off++] = 0x01; /* num reports */
	ev[off++] = 0x00; /* ADV_IND */
	ev[off++] = 0x00; /* public addr */
	memcpy(ev + off, addr, 6);
	off += 6;
	ev[off++] = dlen;
	ev[off++] = 0x02; ev[off++] = 0x01; ev[off++] = 0x06; /* Flags */
	ev[off++] = (uint8_t)(1 + nl); ev[off++] = 0x09; /* Complete Local Name */
	memcpy(ev + off, name, (size_t)nl);
	off += (uint16_t)nl;
	ev[off++] = (uint8_t)(-55); /* RSSI */
	evtPush(ev, off);
}

static void evtConnComplete(void) {
	uint8_t ev[14];
	memset(ev, 0, sizeof ev);
	ev[0] = 0x3E;
	ev[1] = 12;
	ev[2] = 0x01; /* LE Connection Complete */
	ev[3] = 0x00; /* status */
	ev[4] = (uint8_t)mockHandle;
	ev[5] = (uint8_t)(mockHandle >> 8);
	ev[6] = 0x00; /* peer addr type */
	memcpy(ev + 7, mockPeer, 6);
	evtPush(ev, 14);
}

static void evtDisconnComplete(void) {
	uint8_t ev[7];
	ev[0] = 0x05;
	ev[1] = 4;
	ev[2] = 0x00; /* status */
	ev[3] = (uint8_t)mockHandle;
	ev[4] = (uint8_t)(mockHandle >> 8);
	ev[5] = 0x16; /* reason: connection terminated by local host */
	evtPush(ev, 6);
}

static void evtEncChange(void) {
	uint8_t ev[7];
	ev[0] = 0x08;
	ev[1] = 4;
	ev[2] = 0x00;
	ev[3] = (uint8_t)mockHandle;
	ev[4] = (uint8_t)(mockHandle >> 8);
	ev[5] = 0x01; /* encrypted */
	evtPush(ev, 6);
}

static void evtCisEstablished(void) {
	uint8_t ev[20];
	memset(ev, 0, sizeof ev);
	ev[0] = 0x3E;
	ev[1] = 18;
	ev[2] = 0x19; /* LE CIS Established */
	ev[3] = 0x00; /* status */
	ev[4] = (uint8_t)mockCisHandle;
	ev[5] = (uint8_t)(mockCisHandle >> 8);
	evtPush(ev, 20);
}

/* ---- fake advertisers ---- */
static const uint8_t advAddr0[6] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x01 };
static const uint8_t advAddr1[6] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x02 };

static void pushFakeAdv(void) {
	if (mockAdvSent == 0) {
		evtAdvReport(advAddr0, "XenevaSink");
		mockAdvSent++;
	} else if (mockAdvSent == 1) {
		evtAdvReport(advAddr1, "XenevaHead");
		mockAdvSent++;
	}
}

/* ---- ACL: minimal ATT responder for Device Name (0x2A00) ---- */
static void mockAttRespond(const uint8_t* l2cap, uint16_t l2len) {
	uint8_t acl[64];
	uint8_t att[20];
	uint16_t attLen = 0;
	uint16_t l2len2;
	(void)l2len;
	/* l2cap: [len(2)] [cid(2)] [att...]  — host.c l2cap_send wraps this */
	/* We only handle ATT Read By Type Request (0x08) for UUID 0x2A00 */
	if (l2cap[4] != 0x08)
		return;
	/* Build ATT Read By Type Response: opcode 0x09, len, handle(2), name */
	att[attLen++] = 0x09;
	att[attLen++] = 0x0C; /* handle(2) + name(10) */
	att[attLen++] = 0x01; /* handle low */
	att[attLen++] = 0x00; /* handle high */
	memcpy(att + attLen, "XenevaSink", 10);
	attLen += 10;
	/* Wrap in L2CAP: len(2) cid(2) */
	l2len2 = attLen;
	acl[0] = (uint8_t)mockHandle;
	acl[1] = (uint8_t)((mockHandle >> 8) | 0x20); /* PB=0x2 (first flush) */
	acl[2] = (uint8_t)(l2len2 + 4); /* acl data length */
	acl[3] = (uint8_t)((l2len2 + 4) >> 8);
	acl[4] = (uint8_t)l2len2;
	acl[5] = (uint8_t)(l2len2 >> 8);
	acl[6] = 0x04; /* ATT CID low */
	acl[7] = 0x00; /* ATT CID high */
	memcpy(acl + 8, att, attLen);
	aclPush(acl, (uint16_t)(8 + attLen));
}

/* ---- HCI command dispatcher ---- */
static void mockProcessCmd(const uint8_t* d, uint16_t len) {
	uint16_t op;
	uint8_t p[32];
	if (len < 3)
		return;
	op = (uint16_t)(d[0] | (d[1] << 8));

	switch (op) {
	case HCI_RESET:
	case HCI_SET_EVT_MASK:
	case HCI_WRITE_LE_HST:
	case LE_SET_EVT_MASK:
	case LE_SET_HOST_FEAT:
	case LE_SET_SCAN_PAR:
	case LE_LTK_REPLY:
	case LE_LTK_NEG:
	case LE_SETUP_ISO:
		p[0] = 0;
		evtCmdComplete(op, p, 1);
		break;

	case LE_SET_SCAN_EN:
		p[0] = 0;
		evtCmdComplete(op, p, 1);
		if (len >= 4 && d[3] == 1) {
			mockScanning = 1;
			mockAdvSent = 0;
		} else {
			mockScanning = 0;
		}
		break;

	case HCI_READ_VER:
		memset(p, 0, 9);
		p[0] = 0;    /* status */
		p[1] = 0x09; /* HCI version: BT 5.0 */
		p[4] = 0x09; /* LMP version */
		p[5] = 0x1D; /* manufacturer: Intel */
		evtCmdComplete(op, p, 9);
		break;

	case HCI_READ_BD:
		p[0] = 0;
		memcpy(p + 1, mockBd, 6);
		evtCmdComplete(op, p, 7);
		break;

	case HCI_READ_BUF:
		memset(p, 0, 8);
		p[0] = 0;
		p[1] = 27;  /* ACL MTU low */
		p[3] = 1;   /* ACL max packets */
		evtCmdComplete(op, p, 8);
		break;

	case LE_READ_BUF:
		p[0] = 0;
		p[1] = 27;
		p[2] = 0;
		p[3] = 1;
		evtCmdComplete(op, p, 4);
		break;

	case LE_READ_FEAT:
		p[0] = 0;
		memset(p + 1, 0, 8);
		p[1] = 0xFF; /* LE features bitmap */
		evtCmdComplete(op, p, 9);
		break;

	case LE_READ_BUF_V2:
		memset(p, 0, 7);
		p[0] = 0;
		p[1] = 27;  /* LE ACL MTU */
		p[3] = 1;   /* LE ACL max */
		p[4] = 40;  /* ISO MTU (16kHz/10ms/40-octet) */
		p[6] = 1;   /* ISO max */
		evtCmdComplete(op, p, 7);
		break;

	case LE_SET_CIG:
		memset(p, 0, 8);
		p[0] = 0; /* status */
		p[1] = 0; /* cig_id */
		p[2] = 1; /* CIS_Count */
		p[3] = (uint8_t)mockCisHandle;
		p[4] = (uint8_t)(mockCisHandle >> 8);
		evtCmdComplete(op, p, 5);
		break;

	case LE_CREATE_CONN:
		evtCmdStatus(op, 0);
		evtConnComplete();
		break;

	case LE_CREATE_CIS:
		p[0] = 0;
		evtCmdComplete(op, p, 1);
		evtCisEstablished();
		break;

	case LE_START_ENC:
		evtCmdStatus(op, 0);
		evtEncChange();
		break;

	case HCI_DISCONNECT:
		p[0] = 0;
		evtCmdComplete(op, p, 1);
		evtDisconnComplete();
		break;

	default:
		p[0] = 0;
		evtCmdComplete(op, p, 1);
		break;
	}
}

/* ---- AuBtUsbOps implementation ---- */

static int mockControl(void* ctx, uint8_t bm, uint8_t req, uint16_t val,
					   uint16_t idx, uint8_t* data, uint16_t len, int data_in) {
	(void)ctx; (void)bm; (void)req; (void)val; (void)idx; (void)data_in;
	if (!mockReady || !data || len < 3)
		return -1;
	mockProcessCmd(data, len);
	return 0;
}

static int mockIntr(void* ctx, uint8_t* data, uint16_t cap) {
	uint16_t n;
	(void)ctx;
	if (!mockReady)
		return 0;
	/* Generate fake advertisers while scanning */
	if (mockScanning && evtqHead == evtqTail)
		pushFakeAdv();
	if (evtqHead == evtqTail)
		return 0;
	n = evtqLen[evtqHead];
	if (n > cap)
		n = cap;
	memcpy(data, evtq[evtqHead], n);
	evtqHead = (evtqHead + 1) % EVTQ_SZ;
	return n;
}

static int mockBulkOut(void* ctx, const uint8_t* data, uint16_t len) {
	(void)ctx;
	if (!mockReady || !data || len < 8)
		return -1;
	mockAttRespond(data + 4, (uint16_t)(len - 4));
	return 0;
}

static int mockBulkIn(void* ctx, uint8_t* data, uint16_t cap) {
	uint16_t n;
	(void)ctx;
	if (!mockReady)
		return 0;
	if (aclqHead == aclqTail)
		return 0;
	n = aclqLen[aclqHead];
	if (n > cap)
		n = cap;
	memcpy(data, aclq[aclqHead], n);
	aclqHead = (aclqHead + 1) % ACLQ_SZ;
	return n;
}

static int mockIsoOut(void* ctx, const uint8_t* data, uint16_t len) {
	(void)ctx; (void)data; (void)len;
	return 0; /* consume silently */
}

/* ---- initialisation ---- */
void AuVirtioBtInitialize(uint64_t device, int bus, int dev, int func) {
	AuBtUsbOps ops;
	(void)device; (void)bus; (void)dev; (void)func;
	memset(&ops, 0, sizeof ops);
	ops.control = mockControl;
	ops.intr = mockIntr;
	ops.bulk_out = mockBulkOut;
	ops.bulk_in = mockBulkIn;
	ops.iso_out = mockIsoOut;
	ops.has_iso = 1;
	mockReady = 1;
	UARTDebugOut("[virtio-bt]: mock controller ready (2 fake advertisers)\r\n");
	AuBtUsbReady(&ops);
}
