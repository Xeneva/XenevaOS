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

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <_null.h>
#include <aucon.h>
#include <Bt/bt.h>
#include <Fs/vfs.h>
#include <Fs/Dev/devfs.h>
#include <Mm/kmalloc.h>
#include <Mm/vmmngr.h>
#include <Hal/AA64/sched.h>
#include <Hal/AA64/aa64cpu.h>
#include <Sound/sound.h>
#include <Drivers/uart.h>
#include <pcie.h>
#include <Bt/bt.h>
#include "crypto.h"
#include "h4.h"
#include "parse.h"
#include "lc3.h"
#include "sbc.h"
#include "avdtp.h"

extern uint64_t AA64GetPhysicalTimerCount(void);
extern uint64_t cpuFrequency;
/* Set true only in AuSchedulerStart. AuBtInitialize runs before that. */
extern bool _scheduler_initialized;

#define HCI_RESET 0x0C03
#define HCI_SET_EVENT_MASK 0x0C01
#define HCI_WRITE_LE_HOST 0x0C6D
#define HCI_READ_VER 0x1001
#define HCI_READ_BD 0x1009
#define HCI_READ_BUF 0x1005
#define HCI_DISCONNECT 0x0406
#define LE_SET_EVENT_MASK 0x2001
#define LE_READ_BUF 0x2002
#define LE_READ_FEAT 0x2003
#define LE_SET_SCAN_PAR 0x200B
#define LE_SET_SCAN_EN 0x200C
#define LE_CREATE_CONN 0x200D
#define HCI_INQUIRY 0x0401
#define HCI_CREATE_CONN 0x0405
#define HCI_AUTH_REQ 0x0411
#define HCI_SET_ENCRYPT 0x0413
#define HCI_LINK_KEY_REPLY 0x040B
#define HCI_LINK_KEY_NEG 0x040C
#define HCI_PIN_REPLY 0x040D
#define HCI_IO_CAP_REPLY 0x042B
#define HCI_USER_CONFIRM 0x042C
#define HCI_WRITE_INQ_MODE 0x0C45
#define HCI_WRITE_SIMPLE_PAIR_MODE 0x0C56
#define HCI_WRITE_SEC_CONN_HOST 0x0C7A
#define LE_START_ENC 0x2019
#define LE_LTK_REPLY 0x201A
#define LE_LTK_NEG 0x201B
#define LE_READ_BUF_V2 0x2060
#define LE_SET_CIG 0x2062
#define LE_CREATE_CIS 0x2064
#define LE_SETUP_ISO 0x206E
#define LE_SET_HOST_FEAT 0x2074

#define MAX_BONDS 8

typedef struct {
	uint8_t addr[6];
	uint8_t type;
	uint8_t lesc;
	uint8_t key_size;
	uint8_t irk_ok;
	uint16_t ediv;
	uint8_t randn[8];
	uint8_t ltk[16];
	uint8_t irk[16];
} Bond;

static AuBtUsbOps usb;
static int have_usb;
static BtH4 h4;
/* HCI ACL MTU on this adapter is ~1021 and L2CAP config offers 672, so a
 * single received frame is larger than 512. A smaller cap makes bt_h4_feed
 * drop the frame before snoop/on_acl, which looks exactly like "SDP timeout". */
static uint8_t h4_rx[1024];
static int h4_on;

/*
 * H4 UART transport (Zephyr-style serial bridge).
 *
 * Host runs BlueZ btproxy as a UNIX-socket server:
 *   sudo hciconfig hci0 down
 *   sudo btproxy -u -i 0        # listens on /tmp/bt-server-bredr
 * QEMU maps that socket to its second PL011:
 *   -serial stdio -serial unix:/tmp/bt-server-bredr
 * Guest UART1 (0x09040000, IRQ 8) then speaks raw H4:
 *   0x01 CMD host->ctrl, 0x02 ACL bidir, 0x03 SCO bidir,
 *   0x04 EVT ctrl->host, 0x05 ISO bidir.
 * This replaces the old /dev/vhci + virtio-serial bridge, which fought
 * BlueZ for the adapter and had no QEMU wiring. Probe runs first; if
 * UART1 answers HCI_RESET we use the real controller, else mock.
 */
#ifdef __TARGET_BOARD_QEMU_VIRT__
#define BT_UART1_PHYS 0x09040000u
#else
#define BT_UART1_PHYS 0u
#endif
static volatile uint32_t* bt_uart;
static int have_h4uart;
/* RX parked while an H4 frame is being written. Dispatching it inline
 * would transmit a second frame through the same PL011 in the middle of
 * the first. The RX FIFO is 16 bytes, so it still has to be emptied
 * during TX or Number-of-Completed-Packets is lost and ACL credits
 * stick at 0. */
static uint8_t uart_rx_hold[2048];
static int uart_rx_hold_n;
static int uart_hold_rd;
static int uart_tx_depth;
static int uart_feed_depth;

static uint8_t bd[6];
static uint8_t hci_ver;
static uint16_t acl_mtu = 27;
static uint16_t acl_mtu_le = 27;
static uint16_t acl_credits = 1;
static uint16_t acl_max = 1;
static uint16_t iso_mtu;
static uint8_t le_feat[8];
static uint16_t handle;
static int connected;
static int encrypted;
static uint8_t peer[6];
static uint8_t peer_type;
static char peer_name[64];
static BtScanEnt scans[BT_MAX_SCAN];
static int nscan;
static uint8_t scan_psrm[BT_MAX_SCAN];
static uint16_t scan_clk[BT_MAX_SCAN];
static int inq_done;
static int pair_bredr;

static Bond bonds[MAX_BONDS];
static int nbonds;
/* Bonding keys are staged in RAM by remember_bond() and written to
 * /bt/bonds.bin by bond_flush() in perform() thread context. Like BlueZ
 * (mgmt pumps link keys after pairing completes), Zephyr (settings save
 * via workqueue) and NimBLE (store_bond on post-pairing callback), we
 * never touch the filesystem from inside the HCI event handler: that path
 * runs nested deep in poll_usb() wait loops with little stack left, and
 * faulted (bthost data-abort) when bond_save() ran from the 0x18 link-key
 * notify event. */
static int bond_dirty;

static uint8_t acl_asm[1024];
static uint16_t acl_got;
static uint16_t acl_need;

static uint8_t cmd_rsp[80];
static uint8_t cmd_rsplen;
static uint16_t wait_op;
static int cmd_done;
static int cmd_status;

static int conn_done;
static int conn_status;
static int enc_done;

static uint8_t att_rsp[180];
static uint8_t att_len;
static int att_done;

static int smp_on;
static int smp_sc;
static uint8_t preq[7];
static uint8_t pres[7];
static uint8_t prnd[16];
static uint8_t rrnd[16];
static uint8_t pcnf[16];
static uint8_t local_pk[64];
static uint8_t local_sk[32];
static uint8_t remote_pk[64];
static uint8_t dhkey[32];
static uint8_t mackey[16];
static uint8_t ltk_new[16];
static uint32_t passkey;
static int need_confirm;
static int user_ok;
static int pair_done;
static int pair_fail;
static int keys_sent;
static uint16_t cis_handle;
static int audio_on;
static int card_id = -1;
static int16_t pcm_acc[LC3_SAMPLES];
static int pcm_n;
static uint16_t iso_seq;
static AuSound ble_card;

/* Classic A2DP source (SBC) for BREDR headsets: Sony ULT WEAR and every
 * A2DP sink. LE Audio (ble0/LC3) is untouched; production gets both.
 * Signaling runs on L2CAP PSM 25, media on a second PSM-25 channel. */
static uint16_t a2dp_sig_loc;
static uint16_t a2dp_sig_rem;
static uint16_t a2dp_media_loc;
static uint16_t a2dp_media_rem;
static uint8_t a2dp_seid;
static int a2dp_state; /* 0 idle, 1 streaming */
static int a2dp_bitpool = AVDTP_SBC_BITPOOL;
static int a2dp_freq = SBC_FREQ_44100;
static uint16_t a2dp_seq;
static uint32_t a2dp_ts;
static uint32_t a2dp_ssrc;
static uint32_t a2dp_frames;
/* Sample-clock origin, microseconds. Sending faster than this makes
 * the controller flush packets and the headset skips. */
static uint64_t a2dp_t0;
static BtSbcEnc a2dp_enc;
static uint8_t a2dp_acc[512];
static int a2dp_acc_n;
/* Peer media-channel MTU. l2_peer_mtu is overwritten when AVRCP opens. */
static uint16_t a2dp_media_mtu;
/* Several SBC frames per RTP packet. One frame per ACL buffer was ~256
 * frames/s; 48 kHz needs 375, so the headset underran. */
static uint8_t a2dp_pkt[13 + 15 * SBC_MAX_FRAME];
static int a2dp_pkt_n;
static int a2dp_pkt_frames;
static AuSound a2dp_card;
static int a2dp_on;
static int card2_id = -1;
/* Pending L2CAP signaling + AVDTP transactions (initiator side). */
static uint8_t l2_next_id = 1;
static int l2_sig_id;
static int l2_conf_id; /* config phase uses its own id (BlueZ l2cap) */
static uint16_t l2_sig_scid; /* SCID the pending transaction belongs to */
static int l2_sig_done;
static int l2_sig_ok;
static int l2_sig_pending; /* peer sent PENDING, final rsp still coming */
static uint16_t l2_sig_dcid;
static int l2_conf_done;
static int l2_conf_ok;
static int l2_peer_conf;
static uint16_t l2_peer_mtu; /* last MTU offered by the peer (0 = none) */
static uint8_t av_trans;
static int av_done;
static int av_ok;
static uint8_t av_rsp[512];
static int av_rlen;
/* AVDTP adaptation-layer reassembly (spec 8.4.1.1, BlueZ avdtp_recv):
 * start packet buffers here until the single/end fragment completes.
 * Sony GET_CAPABILITIES (SBC+AAC+LDAC) does not fit in 128 bytes. */
static uint8_t av_asm[512];
static int av_asm_n;
static int av_asm_on;
/* Minimal SDP client state (AudioSink search before AVDTP). */
static uint16_t sdp_loc;
static uint16_t sdp_rem;
static int sdp_done;
static int sdp_ok;
static int sdp_err; /* SDP ErrorRsp code, 0 = none */
static uint8_t sdp_rsp[512];
static int sdp_rlen;
static uint16_t sdp_tid;
/* Best-effort AVRCP (AVCTP PSM 23) channel for media keys / absolute
 * volume. Non-fatal: A2DP audio never depends on it. */
static uint16_t avrcp_loc;
static uint16_t avrcp_rem;

static int job;
static BtInfo* job_info;
static BtInfo job_buf;
static volatile int job_done;
static int job_status;
static int bt_started;
static uint8_t snoop[8192];
static uint32_t snoop_len;

static void snoop_open(void);
static void bond_load(void);
static void bond_save(void);
static int hci(uint16_t op, const uint8_t* param, uint8_t plen, uint8_t* out, uint8_t* outlen);
static void hci_send_now(uint16_t op, const uint8_t* param, uint8_t plen);
static void on_event(const uint8_t* ev, uint16_t len);
static void on_acl(const uint8_t* acl, uint16_t len);
static int l2cap_send(uint16_t cid, const uint8_t* data, uint16_t len);
static void poll_usb(void);
static int perform(int code, BtInfo* info);

/* The scheduler is cooperative. A busy wait on bthost freezes the
 * compositor for the whole scan, page, or A2DP handshake. After
 * AuSchedulerStart, sleep and yield; the caller polls on either side.
 * Before that, AuBtInitialize is still the boot thread and the idle
 * thread is current. Sleeping it only links idle onto the sleep list,
 * and AuScheduleNext returns without switching, so an HCI wait ends
 * before the controller has answered. */
static void spin_ms(uint32_t ms) {
	uint64_t start;
	uint64_t need;
	if (ms == 0)
		return;
	if (!_scheduler_initialized) {
		start = AA64GetPhysicalTimerCount();
		/* AA64GetPhysicalTimerCount()/cpuFrequency is microseconds. */
		need = cpuFrequency ? (uint64_t)ms * 1000ull * cpuFrequency
							: (uint64_t)ms * 50000ull;
		while (AA64GetPhysicalTimerCount() - start < need)
			;
		return;
	}
	AuSleepThread(AuGetCurrentThread(), ms);
	AuScheduleNext();
}

static void put16(uint8_t* p, uint16_t v) {
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static uint16_t get16(const uint8_t* p) {
	return (uint16_t)(p[0] | (p[1] << 8));
}

static void addr_str(const uint8_t* a, char* o, int n) {
	static const char* hexd = "0123456789ABCDEF";
	int i, p = 0;
	for (i = 5; i >= 0 && p + 3 < n; i--) {
		o[p++] = hexd[a[i] >> 4];
		o[p++] = hexd[a[i] & 0xf];
		if (i)
			o[p++] = ':';
	}
	o[p] = 0;
}

/* One-line hexdump for small signaling payloads (capped). */
static void bt_hex(const char* tag, const uint8_t* p, int len) {
	char line[3 * 48 + 1];
	int i, n = 0;
	static const char* hexd = "0123456789ABCDEF";
	if (len > 48)
		len = 48;
	for (i = 0; i < len; i++) {
		line[n++] = hexd[p[i] >> 4];
		line[n++] = hexd[p[i] & 0xf];
		line[n++] = ' ';
	}
	line[n] = 0;
	UARTDebugOut("[bt]: %s %s\r\n", tag, line);
}

static void snoop_add(int from_ctrl, const uint8_t* p, uint16_t len) {
	uint8_t rec[16];
	uint32_t i;
	if (snoop_len == 0) {
		memcpy(snoop, "btsnoop", 7);
		snoop[7] = 0;
		snoop[8] = 0;
		snoop[9] = 0;
		snoop[10] = 0;
		snoop[11] = 1;
		snoop[12] = 0;
		snoop[13] = 0;
		snoop[14] = 0x03;
		snoop[15] = 0xea;
		snoop_len = 16;
	}
	if ((uint32_t)len + 16 + snoop_len > sizeof snoop)
		return;
	rec[0] = 0;
	rec[1] = 0;
	rec[2] = (uint8_t)(len >> 8);
	rec[3] = (uint8_t)len;
	rec[4] = rec[0];
	rec[5] = rec[1];
	rec[6] = rec[2];
	rec[7] = rec[3];
	rec[8] = 0;
	rec[9] = 0;
	rec[10] = 0;
	rec[11] = from_ctrl ? 1 : 0;
	memset(rec + 12, 0, 4);
	memcpy(snoop + snoop_len, rec, 16);
	snoop_len += 16;
	for (i = 0; i < len; i++)
		snoop[snoop_len++] = p[i];
}

static void snoop_flush(void) {
	AuVFSNode* root;
	AuVFSNode* file;
	if (snoop_len < 16)
		return;
	root = AuVFSFind("/");
	if (!root)
		return;
	AuVFSCreateDir(root, "/bt");
	file = AuVFSOpen("/bt/btsnoop.log");
	if (!file)
		file = AuVFSCreateFile(root, "/bt/btsnoop.log");
	if (!file)
		return;
	file->current = file->first_block;
	file->eof = 0;
	file->pos = 0;
	AuVFSNodeWrite(root, file, (uint64_t*)snoop, snoop_len);
}

static void bond_load(void) {
	AuVFSNode* root = AuVFSFind("/");
	AuVFSNode* file;
	/* Heap page, not a small stack array: same block-size over-read
	 * hazard as bond_save had. This runs at every boot once bonds.bin
	 * exists, on a small stack. */
	uint8_t* buf = (uint8_t*)kmalloc(4096);
	size_t n;
	int i;
	nbonds = 0;
	if (!buf)
		return;
	if (!root) {
		kfree(buf);
		return;
	}
	file = AuVFSOpen("/bt/bonds.bin");
	if (!file) {
		kfree(buf);
		return;
	}
	memset(buf, 0, 4096);
	n = AuVFSNodeRead(root, file, (uint64_t*)buf, sizeof(BtBondRec) * MAX_BONDS);
	if (n > sizeof(BtBondRec) * MAX_BONDS)
		n = sizeof(BtBondRec) * MAX_BONDS;
	for (i = 0; i + (int)sizeof(BtBondRec) <= (int)n && nbonds < MAX_BONDS; i += (int)sizeof(BtBondRec)) {
		BtBondRec* r = (BtBondRec*)(buf + i);
		if (r->magic != BT_BOND_MAGIC)
			break;
		memcpy(bonds[nbonds].addr, r->addr, 6);
		bonds[nbonds].type = r->addr_type;
		bonds[nbonds].lesc = r->lesc;
		bonds[nbonds].key_size = r->key_size;
		bonds[nbonds].irk_ok = r->irk_ok;
		bonds[nbonds].ediv = r->ediv;
		memcpy(bonds[nbonds].randn, r->rand, 8);
		memcpy(bonds[nbonds].ltk, r->ltk, 16);
		memcpy(bonds[nbonds].irk, r->irk, 16);
		nbonds++;
	}
	kfree(buf);
}

static void bond_save(void) {
	AuVFSNode* root = AuVFSFind("/");
	AuVFSNode* file;
	/* Heap page, not a small stack array: the FAT write path copies in
	 * block-sized chunks and over-read a ~448-byte stack buffer faults
	 * the bthost stack guard (FAR FFFFB000 page, 0x1000 copy). */
	uint8_t* buf = (uint8_t*)kmalloc(4096);
	int i;
	if (!buf)
		return;
	if (!root) {
		kfree(buf);
		return;
	}
	AuVFSCreateDir(root, "/bt");
	file = AuVFSOpen("/bt/bonds.bin");
	if (!file)
		file = AuVFSCreateFile(root, "/bt/bonds.bin");
	if (!file) {
		kfree(buf);
		return;
	}
	memset(buf, 0, 4096);
	for (i = 0; i < nbonds; i++) {
		BtBondRec* r = (BtBondRec*)(buf + i * sizeof(BtBondRec));
		r->magic = BT_BOND_MAGIC;
		memcpy(r->addr, bonds[i].addr, 6);
		r->addr_type = bonds[i].type;
		r->lesc = bonds[i].lesc;
		r->key_size = bonds[i].key_size ? bonds[i].key_size : 16;
		r->irk_ok = bonds[i].irk_ok;
		r->ediv = bonds[i].ediv;
		memcpy(r->rand, bonds[i].randn, 8);
		memcpy(r->ltk, bonds[i].ltk, 16);
		memcpy(r->irk, bonds[i].irk, 16);
	}
	file->current = file->first_block;
	file->eof = 0;
	file->pos = 0;
	AuVFSNodeWrite(root, file, (uint64_t*)buf, (uint32_t)(nbonds * sizeof(BtBondRec)));
	kfree(buf);
}

static int bond_find(const uint8_t* addr, uint8_t type) {
	int i;
	for (i = 0; i < nbonds; i++) {
		if (bonds[i].type == type && memcmp(bonds[i].addr, addr, 6) == 0)
			return i;
	}
	return -1;
}

static int bond_resolve(const uint8_t* addr) {
	int i;
	uint8_t hash[3];
	if ((addr[5] & 0xc0) != 0x40)
		return -1;
	for (i = 0; i < nbonds; i++) {
		if (!bonds[i].irk_ok)
			continue;
		bt_ah(bonds[i].irk, addr + 3, hash);
		if (memcmp(hash, addr, 3) == 0)
			return i;
	}
	return -1;
}

static void fill_rand(uint8_t* p, int n) {
	int i;
	for (i = 0; i < n; i++)
		p[i] = (uint8_t)(rand() ^ (AA64GetPhysicalTimerCount() >> (i & 7)));
}

/* ---- PL011 UART1 helpers (polled, console UART0 untouched) ---- */
static void bt_uart_init_hw(void) {
#ifdef __TARGET_BOARD_QEMU_VIRT__
	if (BT_UART1_PHYS == 0 || bt_uart)
		return;
	bt_uart = (volatile uint32_t*)AuMapMMIO(BT_UART1_PHYS, 1);
	if (!bt_uart)
		return;
	bt_uart[UART_CR / 4] = 0;
	bt_uart[UART_IBRD / 4] = 13;
	bt_uart[UART_FBRD / 4] = 1;
	bt_uart[UART_LCR_H / 4] = (uint32_t)(UART_LCR_H_WLEN_8BIT | UART_LCR_H_FEN);
	bt_uart[UART_CR / 4] = (uint32_t)(UART_CR_UARTEN | UART_CR_TXE | UART_CR_RXE);
	bt_uart[UART_ICR / 4] = 0xffff;
#endif
}

static void bt_uart_hold_rx(void) {
	if (!bt_uart)
		return;
	while (!(bt_uart[UART_FR / 4] & UART_FR_RXFE) &&
		   uart_rx_hold_n < (int)sizeof uart_rx_hold) {
		uart_rx_hold[uart_rx_hold_n++] =
			(uint8_t)(bt_uart[UART_DR / 4] & 0xff);
	}
}

/* One cursor shared with a nested poll. acl_send waits for credits from
 * inside on_acl, and that wait has to keep reading or the completion
 * event never lands. A second cursor would reorder the bytes still
 * sitting in uart_rx_hold. */
static void bt_uart_feed(void) {
	int outer;
	if (!bt_uart || uart_tx_depth)
		return;
	outer = (uart_feed_depth == 0);
	uart_feed_depth++;
	for (;;) {
		uint8_t b;
		uint8_t kind = 0;
		uint16_t olen = 0;
		if (uart_hold_rd < uart_rx_hold_n)
			b = uart_rx_hold[uart_hold_rd++];
		else if (!(bt_uart[UART_FR / 4] & UART_FR_RXFE))
			b = (uint8_t)(bt_uart[UART_DR / 4] & 0xff);
		else
			break;
		if (!bt_h4_feed(&h4, &b, 1, &kind, h4_rx, &olen, sizeof h4_rx))
			continue;
		if (kind == H4_EVT) {
			snoop_add(1, h4_rx, olen);
			on_event(h4_rx, olen);
		} else if (kind == H4_ACL) {
			snoop_add(1, h4_rx, olen);
			on_acl(h4_rx, olen);
		}
	}
	uart_feed_depth--;
	if (outer) {
		uart_rx_hold_n = 0;
		uart_hold_rd = 0;
	}
}

static void bt_uart_tx_bytes(const uint8_t* d, uint16_t n) {
	uint16_t i;
	if (!bt_uart)
		return;
	uart_tx_depth++;
	{
		/* One yield for the whole frame. Sleeping inside every byte
		 * turns a 700-byte media packet into most of a second. */
		int spins = 0;
		for (i = 0; i < n; i++) {
			while (bt_uart[UART_FR / 4] & UART_FR_TXFF) {
				bt_uart_hold_rx();
				if (_scheduler_initialized && ++spins >= 256) {
					spins = 0;
					AuSleepThread(AuGetCurrentThread(), 1);
					AuScheduleNext();
				}
			}
			bt_uart[UART_DR / 4] = d[i];
		}
	}
	uart_tx_depth--;
	if (uart_tx_depth == 0 && uart_feed_depth == 0)
		bt_uart_feed();
}

static void bt_uart_tx_h4(uint8_t kind, const uint8_t* payload, uint16_t len) {
	uint8_t wrapped[1104];
	int n = bt_h4_wrap(kind, payload, len, wrapped, sizeof wrapped);
	if (n > 0)
		bt_uart_tx_bytes(wrapped, (uint16_t)n);
}

static void poll_h4uart(void) {
	if (!have_h4uart || !bt_uart)
		return;
	/* A frame is on the wire. Park RX so this poll cannot answer it
	 * with another TX until bt_uart_tx_bytes finishes the frame. */
	if (uart_tx_depth) {
		bt_uart_hold_rx();
		return;
	}
	bt_uart_feed();
}

static int transport_cmd(const uint8_t* bytes, uint16_t len) {
	snoop_add(0, bytes, len);
	if (have_usb)
		return usb.control(usb.ctx, 0x20, 0, 0, 0, (uint8_t*)bytes, len, 0);
	if (have_h4uart) {
		bt_uart_tx_h4(H4_CMD, bytes, len);
		return 0;
	}
	return -1;
}

static int transport_acl(const uint8_t* bytes, uint16_t len) {
	snoop_add(0, bytes, len);
	if (have_usb)
		return usb.bulk_out(usb.ctx, bytes, len);
	if (have_h4uart) {
		bt_uart_tx_h4(H4_ACL, bytes, len);
		return 0;
	}
	return -1;
}

static int transport_iso(const uint8_t* bytes, uint16_t len) {
	snoop_add(0, bytes, len);
	if (have_usb) {
		if (usb.iso_out)
			return usb.iso_out(usb.ctx, bytes, len);
		return -1;
	}
	if (have_h4uart) {
		bt_uart_tx_h4(H4_ISO, bytes, len);
		return 0;
	}
	return -1;
}

static void poll_usb(void) {
	uint8_t buf[512];
	int n;
	if (have_h4uart)
		poll_h4uart();
	if (!have_usb)
		return;
	n = usb.intr(usb.ctx, buf, sizeof buf);
	if (n > 0) {
		snoop_add(1, buf, (uint16_t)n);
		on_event(buf, (uint16_t)n);
	}
	n = usb.bulk_in(usb.ctx, buf, sizeof buf);
	if (n > 0) {
		snoop_add(1, buf, (uint16_t)n);
		on_acl(buf, (uint16_t)n);
	}
}

static int wait_cmd_done(void) {
	int spins = 0;
	while (!cmd_done && spins++ < 300) {
		poll_usb();
		spin_ms(2);
	}
	return cmd_done ? 0 : -1;
}

/* Reply from inside an event handler. hci() would replace the command
 * the caller is still waiting on. */
static void hci_send_now(uint16_t op, const uint8_t* param, uint8_t plen) {
	uint8_t cmd[80];
	int n = bt_hci_cmd(cmd, op, param, plen);
	if (n > 0)
		transport_cmd(cmd, (uint16_t)n);
}

static int scan_find(const uint8_t* addr) {
	int i;
	for (i = 0; i < nscan; i++) {
		if (!memcmp(scans[i].addr, addr, 6))
			return i;
	}
	return -1;
}

static void scan_add(const uint8_t* addr, uint8_t type, int8_t rssi, const char* name,
					 uint8_t psrm, uint16_t clk) {
	int idx = scan_find(addr);
	BtScanEnt* s;
	if (idx < 0) {
		/* Nameless adverts are common (flags-only). Don't let them
		 * consume the table and evict named devices. A later report
		 * with a name will create the entry. */
		if (!name || !name[0])
			return;
		if (nscan >= BT_MAX_SCAN)
			return;
		idx = nscan++;
		memset(&scans[idx], 0, sizeof scans[idx]);
		memcpy(scans[idx].addr, addr, 6);
	}
	s = &scans[idx];
	/* A classic result for an address we already saw as LE replaces it.
	 * The -LE advert is the assistant side, not the audio device. */
	if (type == BT_ADDR_BREDR || s->addr_type != BT_ADDR_BREDR)
		s->addr_type = type;
	s->rssi = rssi;
	if (name && name[0]) {
		int n = 0;
		while (name[n] && n < BT_NAME_LEN - 1) {
			s->name[n] = name[n];
			n++;
		}
		s->name[n] = 0;
	}
	scan_psrm[idx] = psrm;
	scan_clk[idx] = clk;
}

static int hci(uint16_t op, const uint8_t* param, uint8_t plen, uint8_t* out, uint8_t* outlen) {
	uint8_t cmd[260];
	int n = bt_hci_cmd(cmd, op, param, plen);
	cmd_done = 0;
	cmd_status = -1;
	cmd_rsplen = 0;
	wait_op = op;
	if (transport_cmd(cmd, (uint16_t)n))
		return -1;
	if (wait_cmd_done())
		return -1;
	if (out && outlen) {
		uint8_t c = cmd_rsplen;
		if (c > *outlen)
			c = *outlen;
		memcpy(out, cmd_rsp, c);
		*outlen = c;
	}
	return cmd_status;
}

static int acl_send(uint16_t h, uint8_t pb, const uint8_t* data, uint16_t len) {
	/* One controller ACL buffer. Classic MTU is about 1021; 600 dropped
	 * every multi-frame A2DP packet before it reached the UART. */
	uint8_t pkt[1100];
	uint16_t hf;
	int spins = 0;
	/* A few controller slots, not the old mis-read 1024. Completions
	 * arrive as HCI event 0x13; give them about a second. */
	while (acl_credits == 0 && spins++ < 500) {
		poll_usb();
		spin_ms(2);
	}
	if (acl_credits == 0)
		return -1;
	if ((uint32_t)len + 4 > sizeof pkt)
		return -1;
	hf = (uint16_t)(h | ((uint16_t)pb << 12));
	put16(pkt, hf);
	put16(pkt + 2, len);
	memcpy(pkt + 4, data, len);
	if (transport_acl(pkt, (uint16_t)(len + 4)))
		return -1;
	acl_credits--;
	return 0;
}

static int l2cap_send(uint16_t cid, const uint8_t* data, uint16_t len) {
	/* Full L2CAP frame, then sliced to the ACL MTU. 520 was smaller
	 * than one 7-frame SBC packet (about 850 bytes), so l2cap_send
	 * returned failure with credits still available and the headset
	 * heard nothing. */
	uint8_t pkt[1100];
	/* Classic ACL MTU for BREDR channels (BlueZ uses the controller's
	 * HCI_READ_BUFFER_SIZE ACL length, ~1011). The LE buffer size is
	 * smaller and only applies to LE CoCs — using it here needlessly
	 * fragments every SBC media packet. */
	uint16_t room = acl_mtu ? acl_mtu : 1011;
	uint16_t first;
	uint16_t off;
	/* Host->controller Packet Boundary: 0x02 is the first automatically
	 * flushable fragment (BlueZ ACL_START). 0x00 is non-flushable. Intel
	 * controllers deliver the occasional signaling PDU either way, then
	 * hold a stream of non-flushable media, so the headset beeps at
	 * START and never plays PCM. Continuation stays 0x01. LE keeps 0x00. */
	uint8_t start_pb = (peer_type == BT_ADDR_BREDR) ? 0x02 : 0x00;
	if ((uint32_t)len + 4 > sizeof pkt)
		return -1;
	put16(pkt, len);
	put16(pkt + 2, cid);
	memcpy(pkt + 4, data, len);
	first = (uint16_t)(len + 4);
	if (first > room)
		first = room;
	if (acl_send(handle, start_pb, pkt, first))
		return -1;
	off = first;
	while (off < len + 4) {
		uint16_t chunk = (uint16_t)(len + 4 - off);
		if (chunk > room)
			chunk = room;
		if (acl_send(handle, 0x01, pkt + off, chunk))
			return -1;
		off = (uint16_t)(off + chunk);
	}
	return 0;
}

static void remember_bond(int lesc) {
	int idx = bond_find(peer, peer_type);
	Bond* b;
	if (idx < 0) {
		if (nbonds >= MAX_BONDS)
			idx = 0;
		else
			idx = nbonds++;
	}
	b = &bonds[idx];
	memset(b, 0, sizeof *b);
	memcpy(b->addr, peer, 6);
	b->type = peer_type;
	b->lesc = (uint8_t)lesc;
	b->key_size = 16;
	memcpy(b->ltk, ltk_new, 16);
	if (!lesc) {
		b->ediv = (uint16_t)(rand() & 0xffff);
		fill_rand(b->randn, 8);
	}
	fill_rand(b->irk, 16);
	b->irk_ok = 1;
	/* Deferred: bond_flush() in perform() does the filesystem write. */
	bond_dirty = 1;
}

static void bond_flush(void) {
	if (!bond_dirty)
		return;
	bond_dirty = 0;
	bond_save();
}

static void smp_send(const uint8_t* p, uint16_t len) {
	l2cap_send(0x0006, p, len);
}

static void smp_send_keys(void) {
	uint8_t p[17];
	if (keys_sent)
		return;
	keys_sent = 1;
	p[0] = 0x06;
	memcpy(p + 1, ltk_new, 16);
	smp_send(p, 17);
	if (!smp_sc) {
		uint8_t id[11];
		int idx = bond_find(peer, peer_type);
		id[0] = 0x07;
		if (idx >= 0) {
			put16(id + 1, bonds[idx].ediv);
			memcpy(id + 3, bonds[idx].randn, 8);
		} else {
			memset(id + 1, 0, 10);
		}
		smp_send(id, 11);
	}
	p[0] = 0x08;
	{
		int idx = bond_find(peer, peer_type);
		if (idx >= 0)
			memcpy(p + 1, bonds[idx].irk, 16);
		else
			memset(p + 1, 0, 16);
	}
	smp_send(p, 17);
	p[0] = 0x09;
	p[1] = 0;
	memcpy(p + 2, bd, 6);
	smp_send(p, 8);
	pair_done = 1;
}

static void start_enc(const uint8_t* randn, uint16_t ediv, const uint8_t* ltk) {
	uint8_t p[28];
	put16(p, handle);
	memcpy(p + 2, randn, 8);
	put16(p + 10, ediv);
	memcpy(p + 12, ltk, 16);
	enc_done = 0;
	hci(LE_START_ENC, p, 28, NULL, NULL);
}

static void lesc_after_random(void) {
	uint8_t cfm[16];
	uint8_t a1[7], a2[7];
	uint8_t sk[32], rx[32], ry[32], dh[32];
	bt_f4(remote_pk, local_pk, rrnd, 0, cfm);
	if (memcmp(cfm, pcnf, 16) != 0) {
		pair_fail = 1;
		return;
	}
	bt_rev(local_sk, sk, 32);
	bt_rev(remote_pk, rx, 32);
	bt_rev(remote_pk + 32, ry, 32);
	if (bt_p256_dh(sk, rx, ry, dh)) {
		pair_fail = 1;
		return;
	}
	bt_rev(dh, dhkey, 32);
	memcpy(a1, peer, 6);
	a1[6] = peer_type;
	memcpy(a2, bd, 6);
	a2[6] = 0;
	/* Central is A. N1 is our random, N2 is the peripheral random. */
	bt_f5(dhkey, prnd, rrnd, a2, a1, mackey, ltk_new);
	passkey = bt_g2(local_pk, remote_pk, prnd, rrnd);
	need_confirm = 1;
	UARTDebugOut("[aurora]: bt passkey %d\r\n", passkey);
}

static void lesc_dhcheck(void) {
	uint8_t e[17];
	uint8_t r[16];
	uint8_t iocap[3];
	uint8_t a1[7], a2[7];
	memset(r, 0, 16);
	memcpy(iocap, preq + 1, 3);
	memcpy(a2, bd, 6);
	a2[6] = 0;
	memcpy(a1, peer, 6);
	a1[6] = peer_type;
	e[0] = 0x0d;
	bt_f6(mackey, prnd, rrnd, r, iocap, a2, a1, e + 1);
	smp_send(e, 17);
}

static void on_smp(const uint8_t* p, uint16_t len) {
	if (len < 1)
		return;
	switch (p[0]) {
	case 0x02:
		if (len < 7)
			return;
		memcpy(pres, p, 7);
		smp_sc = (p[3] & 0x08) != 0;
		if (smp_sc) {
			uint8_t pkt[65];
			uint8_t sk[32], x[32], y[32];
			fill_rand(local_sk, 32);
			bt_rev(local_sk, sk, 32);
			if (bt_p256_mul_g(sk, x, y))
				return;
			bt_rev(x, local_pk, 32);
			bt_rev(y, local_pk + 32, 32);
			pkt[0] = 0x0c;
			memcpy(pkt + 1, local_pk, 64);
			smp_send(pkt, 65);
		} else {
			uint8_t cfm[17];
			uint8_t zero[16];
			fill_rand(prnd, 16);
			memset(zero, 0, 16);
			cfm[0] = 0x03;
			bt_c1(zero, prnd, preq, pres, 0, bd, peer_type, peer, cfm + 1);
			smp_send(cfm, 17);
		}
		break;
	case 0x03:
		if (len < 17)
			return;
		memcpy(pcnf, p + 1, 16);
		if (!smp_sc) {
			uint8_t rnd[17];
			rnd[0] = 0x04;
			memcpy(rnd + 1, prnd, 16);
			smp_send(rnd, 17);
		}
		break;
	case 0x04:
		if (len < 17)
			return;
		memcpy(rrnd, p + 1, 16);
		if (smp_sc) {
			lesc_after_random();
		} else {
			uint8_t cfm[16];
			uint8_t stk[16];
			uint8_t zero[16];
			memset(zero, 0, 16);
			bt_c1(zero, rrnd, preq, pres, 0, bd, peer_type, peer, cfm);
			if (memcmp(cfm, pcnf, 16) != 0) {
				pair_fail = 1;
				return;
			}
			bt_s1(zero, rrnd, prnd, stk);
			memcpy(ltk_new, stk, 16);
			start_enc(zero, 0, stk);
		}
		break;
	case 0x0c:
		if (len < 65)
			return;
		memcpy(remote_pk, p + 1, 64);
		{
			uint8_t cfm[17];
			fill_rand(prnd, 16);
			cfm[0] = 0x03;
			bt_f4(local_pk, remote_pk, prnd, 0, cfm + 1);
			smp_send(cfm, 17);
		}
		break;
	case 0x0d:
		if (user_ok || !need_confirm) {
			uint8_t zero[8];
			memset(zero, 0, 8);
			remember_bond(1);
			start_enc(zero, 0, ltk_new);
		}
		break;
	case 0x06:
		if (len >= 17)
			memcpy(ltk_new, p + 1, 16);
		break;
	case 0x07:
		if (len >= 11) {
			int idx;
			remember_bond(0);
			idx = bond_find(peer, peer_type);
			if (idx >= 0) {
				bonds[idx].ediv = get16(p + 1);
				memcpy(bonds[idx].randn, p + 3, 8);
				memcpy(bonds[idx].ltk, ltk_new, 16);
				/* Deferred write, see bond_dirty. */
				bond_dirty = 1;
			}
		}
		break;
	case 0x08:
		if (len >= 17) {
			int idx = bond_find(peer, peer_type);
			if (idx >= 0) {
				memcpy(bonds[idx].irk, p + 1, 16);
				bonds[idx].irk_ok = 1;
				/* Deferred write, see bond_dirty. */
				bond_dirty = 1;
			}
		}
		break;
	case 0x05:
		pair_fail = 1;
		break;
	default:
		break;
	}
}

/* ---- Classic A2DP: L2CAP signaling + AVDTP (initiator side) ----
 * Mirrors BlueZ profiles/audio: L2CAP CoC to PSM 25, config handshake,
 * AVDTP DISCOVER/GET_CAPS/SET_CONFIG/OPEN, second CoC for media, START. */

static void on_l2cap_sig(const uint8_t* p, uint16_t len) {
	uint8_t rsp[16];
	if (len < 4)
		return;
	UARTDebugOut("[bt]: l2cap sig code=%d id=%d len=%d\r\n", p[0], p[1], len);
	bt_hex("l2cap-rx", p, len);
	if (p[0] == 0x0a && len >= 6) {
		/* Information Request: answer Extended Features with none
		 * supported (legal all-zero mask); reject anything else. */
		uint16_t type = (uint16_t)(p[4] | (p[5] << 8));
		if (type == 0x0002) {
			rsp[0] = 0x0b;
			rsp[1] = p[1];
			rsp[2] = 8;
			rsp[3] = 0;
			rsp[4] = 2;
			rsp[5] = 0;
			rsp[6] = 0;
			rsp[7] = 0;
			rsp[8] = 0;
			rsp[9] = 0;
			rsp[10] = 0;
			rsp[11] = 0;
			l2cap_send(L2CAP_CID_SIGNAL, rsp, 12);
			UARTDebugOut("[bt]: l2cap info rsp ext-features=0\r\n");
		} else {
			rsp[0] = 0x0b;
			rsp[1] = p[1];
			rsp[2] = 4;
			rsp[3] = 0;
			rsp[4] = (uint8_t)type;
			rsp[5] = (uint8_t)(type >> 8);
			rsp[6] = 1;
			rsp[7] = 0;
			l2cap_send(L2CAP_CID_SIGNAL, rsp, 8);
		}
		return;
	}
	if (p[0] == L2CAP_CONN_RSP && p[1] == (uint8_t)l2_sig_id && !l2_sig_done) {
		int res = l2cap_conn_rsp_result(p, len, (uint8_t)l2_sig_id, &l2_sig_dcid);
		if (res == L2CAP_CONN_PENDING) {
			/* Deferred accept (Sony sends PENDING, then success on the
			 * same id): keep waiting like BlueZ l2cap_conn_rsp. */
			UARTDebugOut("[bt]: a2dp l2cap connect pending...\r\n");
			l2_sig_pending = 1;
		} else if (res == L2CAP_CONN_OK) {
			UARTDebugOut("[bt]: a2dp l2cap connected dcid=%d\r\n", l2_sig_dcid);
			l2_sig_ok = 1;
			l2_sig_done = 1;
		} else {
			UARTDebugOut("[bt]: a2dp l2cap connect refused\r\n");
			l2_sig_ok = 0;
			l2_sig_done = 1;
		}
	} else if (p[0] == L2CAP_CONF_RSP && p[1] == (uint8_t)l2_conf_id &&
			   !l2_conf_done) {
		if (!l2cap_parse_conf_rsp(p, len, (uint8_t)l2_conf_id, l2_sig_scid)) {
			UARTDebugOut("[bt]: a2dp l2cap config ok\r\n");
			l2_conf_ok = 1;
		} else {
			UARTDebugOut("[bt]: a2dp l2cap config refused\r\n");
			l2_conf_ok = 0;
		}
		l2_conf_done = 1;
	} else if (p[0] == L2CAP_CONF_REQ && len >= 8) {
		uint16_t dst = (uint16_t)(p[4] | (p[5] << 8));
		uint16_t omtu = 0;
		int n;
		/* Every channel we open (SDP included) must answer the peer's
		 * config request, or the peer leaves the channel unconfigured
		 * and silently drops everything we send — the ULT WEAR SDP
		 * timeout. The old code only accepted the two AVDTP SCIDs, so
		 * the SDP channel never completed configuration. */
		if (dst != a2dp_sig_loc && dst != a2dp_media_loc && dst != sdp_loc &&
			dst != avrcp_loc)
			return;
		if (!l2cap_parse_conf_req_mtu(p, len, &omtu)) {
			l2_peer_mtu = omtu;
			if (dst == a2dp_media_loc && omtu)
				a2dp_media_mtu = omtu;
		}
		/* dst is our SCID (the request is addressed to us). The Source
		 * CID in the response is the requester's CID, which is the peer
		 * endpoint from the connection response. ULT WEAR checks that
		 * field against its own SCID and, on a mismatch, finishes the
		 * handshake (so we log "config ok") but never answers SDP or
		 * AVDTP. The capture showed us answering with 0x40 while their
		 * endpoint was 0x41. Echo their MTU: Success plus a different
		 * value (672 against their 895) is not an accept. */
		if (!l2_sig_dcid)
			return;
		if (omtu)
			n = l2cap_build_conf_rsp_mtu(rsp, p[1], l2_sig_dcid, omtu);
		else {
			rsp[0] = L2CAP_CONF_RSP;
			rsp[1] = p[1];
			rsp[2] = 6;
			rsp[3] = 0;
			rsp[4] = (uint8_t)l2_sig_dcid;
			rsp[5] = (uint8_t)(l2_sig_dcid >> 8);
			rsp[6] = rsp[7] = rsp[8] = rsp[9] = 0;
			n = 10;
		}
		l2cap_send(L2CAP_CID_SIGNAL, rsp, (uint16_t)n);
		l2_peer_conf = 1;
		UARTDebugOut("[bt]: a2dp l2cap peer config, accepted (mtu=%d rsp_cid=%d)\r\n",
					 omtu, l2_sig_dcid);
	} else if (p[0] == L2CAP_CONN_REQ) {
		/* We only initiate; refuse inbound channels cleanly. */
		rsp[0] = L2CAP_CONN_RSP;
		rsp[1] = p[1];
		rsp[2] = 8;
		rsp[3] = 0;
		rsp[4] = 0;
		rsp[5] = 0;
		rsp[6] = 0;
		rsp[7] = 0;
		rsp[8] = 0;
		rsp[9] = 0;
		rsp[10] = 2;
		rsp[11] = 0;
		rsp[12] = 0;
		rsp[13] = 0;
		l2cap_send(L2CAP_CID_SIGNAL, rsp, 14);
	} else if (p[0] == 0x06 && len >= 8) {
		/* Disconnect request: ack it and drop our side of that channel so
		 * no wait loop hangs past the timeout. */
		uint16_t dst = (uint16_t)(p[4] | (p[5] << 8));
		uint16_t src = (uint16_t)(p[6] | (p[7] << 8));
		rsp[0] = 0x07;
		rsp[1] = p[1];
		rsp[2] = 4;
		rsp[3] = 0;
		rsp[4] = (uint8_t)dst;
		rsp[5] = (uint8_t)(dst >> 8);
		rsp[6] = (uint8_t)src;
		rsp[7] = (uint8_t)(src >> 8);
		l2cap_send(L2CAP_CID_SIGNAL, rsp, 8);
		UARTDebugOut("[bt]: l2cap peer disconnected channel dst=%d src=%d\r\n",
					 dst, src);
		/* Only the channel named in the request. Clearing every CID here
		 * dropped the media channel when a late SDP disconnect arrived,
		 * and bredr_a2dp_write then threw the PCM away. */
		if (dst == a2dp_sig_loc || src == a2dp_sig_loc) {
			a2dp_sig_rem = 0;
			av_done = 1;
			l2_sig_done = 1;
			l2_conf_done = 1;
		}
		if (dst == a2dp_media_loc || src == a2dp_media_loc) {
			a2dp_media_rem = 0;
			a2dp_media_mtu = 0;
			a2dp_pkt_n = 0;
			a2dp_pkt_frames = 0;
			a2dp_state = 0;
			a2dp_on = 0;
		}
		if (dst == sdp_loc || src == sdp_loc) {
			sdp_rem = 0;
			sdp_done = 1;
		}
		if (dst == avrcp_loc || src == avrcp_loc)
			avrcp_rem = 0;
	} else if (p[0] == 0x07 && len >= 8) {
		/* Disconnect response to our close: just log it. The channel is
		 * already torn down locally; the old code logged "unhandled"
		 * here, which hid clean shutdowns in the snoop. */
		UARTDebugOut("[bt]: l2cap disconnect rsp id=%d\r\n", p[1]);
	} else {
		UARTDebugOut("[bt]: l2cap sig unhandled code=%d\r\n", p[0]);
	}
}

static void on_avdtp_sig(const uint8_t* p, uint16_t len) {
	int n;
	uint8_t ptype;
	/* Sink delay reports are commands on the peer's transaction label.
	 * BlueZ accepts them (endpoint_delayreport_ind). Dropping the
	 * command leaves the headset waiting and it never plays the tone. */
	if (len >= 2 && (p[0] & 3) == AVDTP_MSG_CMD && p[1] == AVDTP_DELAYREPORT) {
		uint8_t rsp[2];
		unsigned delay = 0;
		if (len >= 5)
			delay = (unsigned)((p[3] << 8) | p[4]);
		rsp[0] = (uint8_t)((p[0] & 0xf0) | AVDTP_MSG_ACCEPT);
		rsp[1] = AVDTP_DELAYREPORT;
		if (a2dp_sig_rem)
			l2cap_send(a2dp_sig_rem, rsp, 2);
		UARTDebugOut("[bt]: a2dp delay report %d (0.1 ms), accepted\r\n", delay);
		return;
	}
	if (len < 2 || av_done)
		return;
	if ((p[0] >> 4) != (av_trans & 0x0f))
		return;
	ptype = (uint8_t)((p[0] >> 2) & 3);
	if (ptype == 0x01) {
		/* Start packet (spec 8.4.1.1): buffer, more fragments follow.
		 * Sony GET_CAPABILITIES with delay-reporting + content
		 * protection exceeds one L2CAP MTU. */
		if (len > sizeof av_asm)
			return;
		memcpy(av_asm, p, len);
		av_asm_n = len;
		av_asm_on = 1;
		return;
	}
	if (ptype == 0x02 || ptype == 0x03) {
		/* Continuation / end: append payload past the 1-byte header. */
		if (!av_asm_on || av_asm_n + (int)len - 1 > (int)sizeof av_asm) {
			av_asm_on = 0;
			av_asm_n = 0;
			return;
		}
		memcpy(av_asm + av_asm_n, p + 1, len - 1);
		av_asm_n += (int)len - 1;
		if (ptype != 0x03)
			return;
		p = av_asm;
		len = (uint16_t)av_asm_n;
		av_asm_on = 0;
		av_asm_n = 0;
	}
	n = len > (int)sizeof av_rsp ? (int)sizeof av_rsp : len;
	memcpy(av_rsp, p, (size_t)n);
	av_rlen = n;
	av_ok = ((p[0] & 3) == AVDTP_MSG_ACCEPT);
	av_done = 1;
	bt_hex("avdtp-rx", p, len);
	UARTDebugOut("[bt]: a2dp avdtp rsp %s\r\n", av_ok ? "accept" : "reject");
}

static void on_sdp_rsp(const uint8_t* p, uint16_t len) {
	int n;
	if (len < 5 || sdp_done)
		return;
	if (p[1] != (sdp_tid >> 8) || p[2] != (sdp_tid & 0xff))
		return;
	if (p[0] == SDP_ERR_RSP) {
		/* BlueZ surfaces the two-octet error code (spec 4.1); the old
		 * code treated this as a generic error and retried blindly. */
		sdp_err = len >= 7 ? (int)((p[5] << 8) | p[6]) : -1;
		sdp_ok = 0;
		sdp_done = 1;
		bt_hex("sdp-rx", p, len);
		UARTDebugOut("[bt]: sdp error rsp code=%d\r\n", sdp_err);
		return;
	}
	n = len > (int)sizeof sdp_rsp ? (int)sizeof sdp_rsp : len;
	memcpy(sdp_rsp, p, (size_t)n);
	sdp_rlen = n;
	sdp_ok = (p[0] == SDP_SS_RSP || p[0] == SDP_SA_RSP || p[0] == SDP_SSA_RSP);
	sdp_done = 1;
	bt_hex("sdp-rx", p, len);
	UARTDebugOut("[bt]: sdp rsp pdu=%d len=%d\r\n", p[0], len);
}

static uint16_t sdp_next_tid(void) {
	sdp_tid++;
	if (sdp_tid == 0)
		sdp_tid = 1;
	return sdp_tid;
}

/* One SDP request/response round over the open SDP channel, with the
 * BlueZ continuation loop (sdp_send_req): while the server leaves a
 * non-empty continuationState, re-send the same TID with the state
 * appended until the full record arrives (max 4 rounds). Accumulated
 * payload lands in sdp_rsp/sdp_rlen. */
static int sdp_round(const uint8_t* cmd, int clen) {
	uint8_t req[64];
	uint8_t acc[512];
	int acc_total = -1; /* total count from first response, -1 = single */
	int acc_n = 0;
	int acc_is_search = 0;
	int rounds = 0;
	int spins = 0;
	if (clen < 1 || clen + 16 > (int)sizeof req)
		return -1;
	memcpy(req, cmd, (size_t)clen);
	sdp_done = 0;
	sdp_ok = 0;
	sdp_err = 0;
	sdp_rlen = 0;
	bt_hex("sdp-tx", cmd, clen);
	l2cap_send(sdp_rem, cmd, (uint16_t)clen);
	for (rounds = 0; rounds < 5; rounds++) {
		const uint8_t* cs = NULL;
		int csl, total, cur, pay;
		spins = 0;
		while (!sdp_done && spins++ < 800) {
			poll_usb();
			spin_ms(5);
		}
		if (!sdp_done) {
			UARTDebugOut("[bt]: sdp timeout\r\n");
			return -1;
		}
		if (!sdp_ok) {
			UARTDebugOut("[bt]: sdp error response\r\n");
			return -1;
		}
		if (rounds == 0) {
			acc_is_search = (sdp_rsp[0] == SDP_SS_RSP);
			/* Single-round fast path: no continuation. */
			csl = sdp_cont_get(sdp_rsp, sdp_rlen, NULL);
			if (csl <= 0)
				return 0;
			/* First chunk of a multi-round answer: seed accumulator.
			 * An empty first chunk with continuation is legal; later
			 * rounds carry the payload. */
			total = (sdp_rsp[5] << 8) | sdp_rsp[6];
			cur = (sdp_rsp[7] << 8) | sdp_rsp[8];
			pay = acc_is_search ? cur * 4 : cur;
			if (pay < 0 || 9 + pay > (int)sizeof acc)
				return -1;
			memcpy(acc, sdp_rsp + 9, (size_t)pay);
			acc_n = pay;
			acc_total = acc_is_search ? total * 4 : total;
		} else {
			total = (sdp_rsp[5] << 8) | sdp_rsp[6];
			cur = (sdp_rsp[7] << 8) | sdp_rsp[8];
			pay = acc_is_search ? cur * 4 : cur;
			if (pay < 0 || acc_n + pay > (int)sizeof acc)
				return -1;
			if (pay > 0) {
				memcpy(acc + acc_n, sdp_rsp + 9, (size_t)pay);
				acc_n += pay;
			}
			(void)total;
		}
		csl = sdp_cont_get(sdp_rsp, sdp_rlen, &cs);
		if (csl < 0)
			return -1;
		if (csl == 0)
			break;
		{
			/* Continuation request: same TID, state appended. The
			 * caller-built cmd ends in the empty 0x00 cont byte. */
			int ncl = sdp_cont_append(req, clen, cs, csl);
			if (ncl < 0)
				break;
			sdp_done = 0;
			sdp_ok = 0;
			bt_hex("sdp-tx-cont", req, ncl);
			l2cap_send(sdp_rem, req, (uint16_t)ncl);
		}
	}
	if (acc_total >= 0) {
		/* Rebuild one logical response: header + merged payload. */
		uint8_t* h = sdp_rsp;
		if (acc_n + 10 > (int)sizeof sdp_rsp)
			return -1;
		memcpy(sdp_rsp + 9, acc, (size_t)acc_n);
		if (acc_is_search) {
			h[5] = (uint8_t)(acc_total / 4 >> 8);
			h[6] = (uint8_t)(acc_total / 4);
			h[7] = (uint8_t)(acc_n / 4 >> 8);
			h[8] = (uint8_t)(acc_n / 4);
		} else {
			h[5] = (uint8_t)(acc_n >> 8);
			h[6] = (uint8_t)acc_n;
			h[7] = (uint8_t)(acc_n >> 8);
			h[8] = (uint8_t)acc_n;
		}
		sdp_rsp[9 + acc_n] = 0;
		sdp_rlen = 9 + acc_n + 1;
	}
	return 0;
}

/* Best-effort L2CAP disconnect of one of our channels. */
static void l2cap_bredr_close(uint16_t rem, uint16_t loc) {
	uint8_t d[8];
	if (!rem || !loc)
		return;
	l2_next_id++;
	if (l2_next_id == 0)
		l2_next_id = 1;
	d[0] = 0x06;
	d[1] = l2_next_id;
	d[2] = 4;
	d[3] = 0;
	d[4] = (uint8_t)rem;
	d[5] = (uint8_t)(rem >> 8);
	d[6] = (uint8_t)loc;
	d[7] = (uint8_t)(loc >> 8);
	l2cap_send(L2CAP_CID_SIGNAL, d, 8);
}

/* Open one L2CAP CoC to PSM and run the config handshake. loc is our SCID.
 * Fills *rem with the peer DCID. 0 on success. Mirrors the BlueZ
 * l2cap_sock_connect + config exchange: distinct signalling ids per phase,
 * PENDING tolerance, MTU offer 672. */
static int l2cap_bredr_open(uint16_t psm, uint16_t loc, uint16_t* rem) {
	uint8_t req[16];
	int spins = 0;
	l2_next_id++;
	if (l2_next_id == 0)
		l2_next_id = 1;
	l2_sig_id = l2_next_id;
	l2_sig_scid = loc;
	l2_sig_done = 0;
	l2_sig_ok = 0;
	l2_sig_pending = 0;
	l2_conf_done = 0;
	l2_conf_ok = 0;
	l2_peer_conf = 0;
	l2_peer_mtu = 0;
	{
		int rl = l2cap_build_conn_req(req, (uint8_t)l2_sig_id, psm, loc);
		bt_hex("l2cap-tx-conn", req, rl);
		l2cap_send(L2CAP_CID_SIGNAL, req, (uint16_t)rl);
	}
	while (!l2_sig_done && spins++ < 800) {
		poll_usb();
		spin_ms(5);
	}
	if (!l2_sig_done || !l2_sig_ok) {
		UARTDebugOut("[bt]: a2dp l2cap open psm=%d failed: no connect rsp\r\n", psm);
		return -1;
	}
	*rem = l2_sig_dcid;
	/* Config phase gets its own signalling id (BlueZ never reuses the
	 * connect id): the old reuse confused ConnRsp/ConfRsp matching when
	 * the peer pipelines its own ConfReq on the same id. */
	l2_next_id++;
	if (l2_next_id == 0)
		l2_next_id = 1;
	l2_conf_id = l2_next_id;
	{
		int rl = l2cap_build_conf_req(req, (uint8_t)l2_conf_id, *rem, 672);
		bt_hex("l2cap-tx-conf", req, rl);
		l2cap_send(L2CAP_CID_SIGNAL, req, (uint16_t)rl);
	}
	spins = 0;
	while ((!l2_conf_done || !l2_peer_conf) && spins++ < 800) {
		poll_usb();
		spin_ms(5);
	}
	if (!l2_conf_done || !l2_conf_ok) {
		UARTDebugOut("[bt]: a2dp l2cap open psm=%d failed: no config rsp\r\n", psm);
		return -1;
	}
	if (!l2_peer_conf)
		UARTDebugOut("[bt]: a2dp l2cap open psm=%d warning: peer never sent config req\r\n",
					 psm);
	return 0;
}

/* Next AVDTP transaction label. Callers build the command with it, then
 * avdtp_round() waits for the matching response. */
static uint8_t avdtp_next(void) {
	av_trans = (uint8_t)((av_trans + 1) & 0x0f);
	return av_trans;
}

/* One AVDTP command/response round. Response bytes land in av_rsp/av_rlen. */
static int avdtp_round(const uint8_t* cmd, int clen, uint8_t signal) {
	int spins = 0;
	av_done = 0;
	av_ok = 0;
	av_rlen = 0;
	av_asm_on = 0;
	av_asm_n = 0;
	bt_hex("avdtp-tx", cmd, clen);
	l2cap_send(a2dp_sig_rem, cmd, (uint16_t)clen);
	while (!av_done && spins++ < 800) {
		poll_usb();
		spin_ms(5);
	}
	if (!av_done) {
		UARTDebugOut("[bt]: a2dp avdtp signal=%d timeout\r\n", signal);
		return -1;
	}
	if (!av_ok) {
		UARTDebugOut("[bt]: a2dp avdtp signal=%d rejected\r\n", signal);
		return -1;
	}
	return 0;
}

static void on_acl(const uint8_t* acl, uint16_t len) {
	uint16_t hf, dlen, pb, h;
	const uint8_t* data;
	if (len < 4)
		return;
	hf = get16(acl);
	dlen = get16(acl + 2);
	h = hf & 0x0fff;
	pb = (hf >> 12) & 3;
	if (h != handle)
		return;
	if (dlen > len - 4)
		dlen = (uint16_t)(len - 4);
	data = acl + 4;
	if (pb != 1) {
		acl_got = 0;
		acl_need = 0;
	}
	if (acl_got + dlen > sizeof acl_asm)
		return;
	memcpy(acl_asm + acl_got, data, dlen);
	acl_got = (uint16_t)(acl_got + dlen);
	if (acl_need == 0 && acl_got >= 4)
		acl_need = (uint16_t)(get16(acl_asm) + 4);
	if (acl_need && acl_got >= acl_need) {
		uint16_t cid = get16(acl_asm + 2);
		uint16_t plen = get16(acl_asm);
		const uint8_t* pdu = acl_asm + 4;
		if (cid == 0x0004) {
			att_len = plen > sizeof att_rsp ? sizeof att_rsp : (uint8_t)plen;
			memcpy(att_rsp, pdu, att_len);
			att_done = 1;
			if (plen >= 1 && pdu[0] == 0x0b && plen > 1) {
				int c = plen - 1;
				if (c > 63)
					c = 63;
				memcpy(peer_name, pdu + 1, (size_t)c);
				peer_name[c] = 0;
			}
		} else if (cid == 0x0006) {
			on_smp(pdu, plen);
		} else if (cid == L2CAP_CID_SIGNAL) {
			on_l2cap_sig(pdu, plen);
		} else if (a2dp_sig_loc && cid == a2dp_sig_loc) {
			/* Incoming L2CAP is addressed to our SCID. */
			on_avdtp_sig(pdu, plen);
		} else if (a2dp_media_loc && cid == a2dp_media_loc) {
			/* Sink-to-source media never carries audio; drop it. */
		} else if (avrcp_loc && cid == avrcp_loc) {
			/* AVCTP control channel: button/notify frames from the
			 * headset. Full AVRCP parsing is future work; consume so
			 * the channel stays healthy. */
			bt_hex("avctp-rx", pdu, plen > 48 ? 48 : (int)plen);
		} else if (sdp_loc && cid == sdp_loc) {
			on_sdp_rsp(pdu, plen);
		} else {
			UARTDebugOut("[bt]: acl inbound cid=%d len=%d (unclaimed)\r\n", cid,
						 plen);
		}
		acl_got = 0;
		acl_need = 0;
	}
}

static void on_event(const uint8_t* ev, uint16_t len) {
	uint16_t elen;
	if (len < 2)
		return;
	/* USB delivers a full max-packet buffer. The HCI event is only
	 * param-length + 2 bytes; the rest is padding. */
	elen = (uint16_t)(ev[1] + 2);
	if (elen < len)
		len = elen;
	if (ev[0] == 0x0e && len >= 6) {
		uint16_t op = get16(ev + 3);
		if (op == wait_op) {
			cmd_status = ev[5];
			cmd_rsplen = (uint8_t)(ev[1] >= 3 ? ev[1] - 3 : 0);
			if (cmd_rsplen > sizeof cmd_rsp)
				cmd_rsplen = sizeof cmd_rsp;
			if (len >= (uint16_t)(6 + cmd_rsplen))
				memcpy(cmd_rsp, ev + 5, cmd_rsplen);
			else
				memcpy(cmd_rsp, ev + 5, len - 5);
			cmd_done = 1;
		}
	} else if (ev[0] == 0x0f && len >= 6) {
		uint16_t op = get16(ev + 4);
		if (op == wait_op) {
			cmd_status = ev[2];
			cmd_rsplen = 1;
			cmd_rsp[0] = ev[2];
			cmd_done = 1;
		}
	} else if (ev[0] == 0x13 && len >= 5) {
		int n = ev[2];
		int i;
		for (i = 0; i < n && 3 + i * 4 + 3 < len; i++) {
			uint16_t c = get16(ev + 5 + i * 4);
			acl_credits = (uint16_t)(acl_credits + c);
			if (acl_credits > acl_max)
				acl_credits = acl_max;
		}
	} else if (ev[0] == 0x01 && len >= 3) {
		inq_done = 1;
	} else if (ev[0] == 0x03 && len >= 13) {
		conn_status = ev[2];
		conn_done = 1;
		if (conn_status == 0) {
			handle = get16(ev + 3);
			memcpy(peer, ev + 5, 6);
			peer_type = BT_ADDR_BREDR;
			connected = 1;
		} else {
			char ab[24];
			addr_str(ev + 5, ab, sizeof ab);
			UARTDebugOut("[bt]: bredr conn complete failed status=%d peer=%s (4=page timeout: headset not pageable? put it back in pairing mode)\r\n",
						 conn_status, ab);
			connected = 0;
		}
	} else if (ev[0] == 0x05) {
		connected = 0;
		encrypted = 0;
		handle = 0;
		a2dp_state = 0;
		a2dp_on = 0;
		a2dp_sig_rem = 0;
		a2dp_media_rem = 0;
		a2dp_media_mtu = 0;
		a2dp_pkt_n = 0;
		a2dp_pkt_frames = 0;
		sdp_rem = 0;
		avrcp_rem = 0;
		av_asm_on = 0;
		av_asm_n = 0;
		UARTDebugOut("[bt]: disconnected\r\n");
	} else if (ev[0] == 0x06 && len >= 5) {
		/* Authentication Complete (BREDR legacy auth after HCI_AUTH_REQ).
		 * Linux enables encryption here when a link key already exists;
		 * without this the stack waits for an encryption event that never
		 * comes and pairing times out. */
		if (ev[2] != 0) {
			UARTDebugOut("[bt]: bredr auth complete failed status=%d\r\n", ev[2]);
			pair_fail = 1;
		} else {
			UARTDebugOut("[bt]: bredr auth complete ok, enabling encryption\r\n");
			if (pair_bredr && !encrypted) {
				uint8_t enc[3];
				put16(enc, handle);
				enc[2] = 1;
				hci_send_now(HCI_SET_ENCRYPT, enc, 3);
			}
		}
	} else if (ev[0] == 0x08 && len >= 4) {
		encrypted = (len >= 6) ? (ev[5] != 0) : (ev[3] != 0);
		enc_done = 1;
		UARTDebugOut("[bt]: encryption %s\r\n", encrypted ? "on" : "off");
		if (encrypted && smp_on)
			smp_send_keys();
	} else if (ev[0] == 0x16 && len >= 8) {
		/* PIN Code Request: legacy fallback for headsets without SSP.
		 * Linux agents answer 0000 for headsets; do the same. */
		uint8_t rep[23];
		char ab[24];
		addr_str(ev + 2, ab, sizeof ab);
		UARTDebugOut("[bt]: PIN request from %s, replying 0000\r\n", ab);
		memcpy(rep, ev + 2, 6);
		rep[6] = 4;
		memcpy(rep + 7, "0000", 4);
		memset(rep + 11, 0, 12);
		hci_send_now(HCI_PIN_REPLY, rep, 23);
	} else if (ev[0] == 0x17 && len >= 8) {
		uint8_t rep[22];
		int idx;
		memcpy(rep, ev + 2, 6);
		idx = -1;
		{
			int i;
			for (i = 0; i < nbonds; i++) {
				if (bonds[i].type == BT_ADDR_BREDR && !memcmp(bonds[i].addr, ev + 2, 6)) {
					idx = i;
					break;
				}
			}
		}
		if (idx >= 0) {
			memcpy(rep + 6, bonds[idx].ltk, 16);
			UARTDebugOut("[bt]: link-key request: bond found, replying key\r\n");
			hci_send_now(HCI_LINK_KEY_REPLY, rep, 22);
		} else {
			UARTDebugOut("[bt]: link-key request: no bond, starting SSP\r\n");
			hci_send_now(HCI_LINK_KEY_NEG, rep, 6);
		}
	} else if (ev[0] == 0x18 && len >= 24) {
		UARTDebugOut("[bt]: link-key notify: bonding, key saved\r\n");
		memcpy(peer, ev + 2, 6);
		peer_type = BT_ADDR_BREDR;
		memcpy(ltk_new, ev + 8, 16);
		remember_bond(0);
	} else if (ev[0] == 0x2f && len >= 17) {
		char name[BT_NAME_LEN];
		const uint8_t* eir = ev + 17;
		int elen = (int)len - 17;
		name[0] = 0;
		if (elen > 240)
			elen = 240;
		bt_ad_name(eir, elen, name, BT_NAME_LEN);
		scan_add(ev + 3, BT_ADDR_BREDR, (int8_t)ev[16], name, ev[9], get16(ev + 14));
	} else if (ev[0] == 0x31 && len >= 8) {
		uint8_t rep[9];
		memcpy(rep, ev + 2, 6);
		rep[6] = 0x03; /* NoInputNoOutput */
		rep[7] = 0x00; /* no OOB */
		/* 0x04 = General Bonding, No MITM (hcidump: "General Bonding (No
		 * MITM Protection)"). With MITM-not-required on both sides the
		 * GAP runs numeric comparison with automatic accept = Just
		 * Works, and the key is kept. Our old 0x01 meant "No Bonding,
		 * MITM", which forced the headset into legacy PIN and a
		 * throwaway key every time. */
		rep[8] = 0x04;
		UARTDebugOut("[bt]: IO-cap request: replying NoInputNoOutput + bonding (Just Works)\r\n");
		hci_send_now(HCI_IO_CAP_REPLY, rep, 9);
	} else if (ev[0] == 0x33 && len >= 12) {
		uint8_t rep[6];
		char ab[24];
		passkey = (uint32_t)ev[8] | ((uint32_t)ev[9] << 8) | ((uint32_t)ev[10] << 16) |
				  ((uint32_t)ev[11] << 24);
		addr_str(ev + 2, ab, sizeof ab);
		UARTDebugOut("[bt]: user-confirm %s passkey %d: auto-accepting (Just Works)\r\n", ab,
					 passkey);
		memcpy(rep, ev + 2, 6);
		hci_send_now(HCI_USER_CONFIRM, rep, 6);
		need_confirm = 0;
		user_ok = 1;
	} else if (ev[0] == 0x36 && len >= 9) {
		pair_fail = ev[2] != 0;
		pair_done = ev[2] == 0;
		UARTDebugOut("[bt]: simple-pairing complete status=%d\r\n", ev[2]);
		if (pair_done && pair_bredr) {
			uint8_t enc[3];
			put16(enc, handle);
			enc[2] = 1;
			hci_send_now(HCI_SET_ENCRYPT, enc, 3);
		}
	} else if (ev[0] == 0x3e && len >= 3) {
		uint8_t sub = ev[2];
		if (sub == 0x02 && len >= 12) {
			const uint8_t* r = ev + 3;
			uint8_t num = r[0];
			int off = 1;
			int i;
			(void)num;
			for (i = 0; i < num && off + 8 < (int)len - 3; i++) {
				uint8_t elen;
				{
					char nm[BT_NAME_LEN];
					int8_t rssi = 0;
					nm[0] = 0;
					elen = r[off + 8];
					if (off + 9 + elen < (int)len - 3) {
						bt_ad_name(r + off + 9, elen, nm, BT_NAME_LEN);
						rssi = (int8_t)r[off + 9 + elen];
					}
					scan_add(r + off + 2, r[off + 1], rssi, nm, 0, 0);
				}
				off += 10 + elen;
			}
		} else if ((sub == 0x01 || sub == 0x0a) && len >= 14) {
			conn_status = ev[3];
			conn_done = 1;
			if (conn_status == 0) {
				handle = get16(ev + 4);
				peer_type = ev[6];
				memcpy(peer, ev + 7, 6);
				connected = 1;
			} else {
				UARTDebugOut("[bt]: LE conn complete failed status=%d\r\n", conn_status);
				connected = 0;
			}
		} else if (sub == 0x05 && len >= 13) {
			uint8_t rep[18];
			int idx = bond_find(peer, peer_type);
			put16(rep, handle);
			if (idx >= 0) {
				memcpy(rep + 2, bonds[idx].ltk, 16);
				hci(LE_LTK_REPLY, rep, 18, NULL, NULL);
			} else {
				hci(LE_LTK_NEG, rep, 2, NULL, NULL);
			}
		} else if (sub == 0x19) {
			if (len >= 6 && ev[3] == 0)
				cis_handle = get16(ev + 4);
		}
	}
}

static int att_req(const uint8_t* req, uint16_t len) {
	int spins = 0;
	att_done = 0;
	if (l2cap_send(0x0004, req, len))
		return -1;
	while (!att_done && spins++ < 200) {
		poll_usb();
		spin_ms(2);
	}
	return att_done ? 0 : -1;
}

static int read_peer_name(void) {
	uint8_t req[7];
	req[0] = 0x08;
	put16(req + 1, 0x0001);
	put16(req + 3, 0xffff);
	put16(req + 5, 0x2a00);
	if (att_req(req, 7))
		return -1;
	if (att_len >= 5 && att_rsp[0] == 0x09) {
		uint8_t rlen = att_rsp[1];
		uint16_t vh;
		uint8_t rd[3];
		if (rlen >= 2 && att_len >= 4) {
			vh = get16(att_rsp + 2);
			if (rlen > 2 && att_len > 4) {
				int c = rlen - 2;
				if (c > 63)
					c = 63;
				memcpy(peer_name, att_rsp + 4, (size_t)c);
				peer_name[c] = 0;
				return 0;
			}
			rd[0] = 0x0a;
			put16(rd + 1, vh);
			if (!att_req(rd, 3) && att_len > 1 && att_rsp[0] == 0x0b) {
				int c = att_len - 1;
				if (c > 63)
					c = 63;
				memcpy(peer_name, att_rsp + 1, (size_t)c);
				peer_name[c] = 0;
				return 0;
			}
		}
	}
	return -1;
}

static void copy_info(BtInfo* info) {
	if (!info)
		return;
	memset(info->msg, 0, sizeof info->msg);
	memcpy(info->bd_addr, bd, 6);
	info->hci_ver = hci_ver;
	info->acl_mtu = acl_mtu;
	info->iso_mtu = iso_mtu;
	info->iso = iso_mtu ? 1 : 0;
	info->connected = (uint8_t)connected;
	info->encrypted = (uint8_t)encrypted;
	memcpy(info->peer, peer, 6);
	info->peer_type = peer_type;
	memcpy(info->peer_name, peer_name, sizeof info->peer_name);
	if (nscan < 0)
		nscan = 0;
	if (nscan > BT_MAX_SCAN)
		nscan = BT_MAX_SCAN;
	info->nscan = nscan;
	if (nscan > 0)
		memcpy(info->scan, scans, (size_t)nscan * sizeof(BtScanEnt));
	info->passkey = passkey;
	info->card_id = card_id;
	if (!have_usb && !have_h4uart)
		memcpy(info->msg, "no adapter", 11);
}

static int do_scan(BtInfo* info) {
	uint8_t par[7];
	uint8_t en[2];
	int spins = 0;
	nscan = 0;
	par[0] = 0x01;
	put16(par + 1, 0x0060);
	put16(par + 3, 0x0030);
	par[5] = 0;
	par[6] = 0;
	if (hci(LE_SET_SCAN_PAR, par, 7, NULL, NULL)) {
		copy_info(info);
		if (info)
			memcpy(info->msg, "scan failed", 12);
		return BT_ERR;
	}
	en[0] = 1;
	en[1] = 0;
	if (hci(LE_SET_SCAN_EN, en, 2, NULL, NULL)) {
		copy_info(info);
		if (info)
			memcpy(info->msg, "scan failed", 12);
		return BT_ERR;
	}
	while (spins++ < 150) {
		poll_usb();
		AuSleepThread(AuGetCurrentThread(), 10);
		AuScheduleNext();
	}
	en[0] = 0;
	hci(LE_SET_SCAN_EN, en, 2, NULL, NULL);
	/* Classic inquiry as well. The JBL audio device is "JBL TUNE FLEX".
	 * The advert named "...-LE" is only the assistant side. */
	{
		uint8_t mode = 2;
		uint8_t inq[5];
		int ispins = 0;
		hci(HCI_WRITE_INQ_MODE, &mode, 1, NULL, NULL);
		inq[0] = 0x33;
		inq[1] = 0x8b;
		inq[2] = 0x9e;
		inq[3] = 0x04;
		inq[4] = 0;
		inq_done = 0;
		if (!hci(HCI_INQUIRY, inq, 5, NULL, NULL)) {
			while (!inq_done && ispins++ < 600) {
				poll_usb();
				spin_ms(10);
			}
		}
	}
	copy_info(info);
	return BT_OK;
}

static int do_connect_bredr(BtInfo* info) {
	uint8_t p[13];
	int idx = scan_find(info->peer);
	int spins = 0;
	uint8_t psrm = 1;
	uint16_t clk = 0;
	char ab[24];
	if (idx >= 0) {
		psrm = scan_psrm[idx];
		clk = scan_clk[idx];
	}
	addr_str(info->peer, ab, sizeof ab);
	if (connected) {
		char cur[24];
		addr_str(peer, cur, sizeof cur);
		UARTDebugOut("[bt]: connect bredr %s refused: already connected to %s; run btctl disconnect first\r\n",
					 ab, cur);
		copy_info(info);
		if (info)
			memcpy(info->msg, "already connected", 18);
		return BT_ERR;
	}
	UARTDebugOut("[bt]: connect bredr %s start\r\n", ab);
	memcpy(p, info->peer, 6);
	put16(p + 6, 0xcc18);
	p[8] = psrm ? psrm : 1;
	p[9] = 0;
	put16(p + 10, clk);
	p[12] = 1;
	conn_done = 0;
	connected = 0;
	conn_status = 0;
	if (hci(HCI_CREATE_CONN, p, 13, NULL, NULL)) {
		UARTDebugOut("[bt]: connect bredr %s failed: HCI_CREATE_CONN submit failed\r\n", ab);
		return BT_ERR;
	}
	while (!conn_done && spins++ < 1200) {
		poll_usb();
		spin_ms(5);
	}
	if (!connected) {
		UARTDebugOut("[bt]: connect bredr %s failed: no connection complete (conn_done=%d status=%d)\r\n",
					 ab, conn_done, conn_status);
		return BT_ERR;
	}
	peer_type = BT_ADDR_BREDR;
	UARTDebugOut("[bt]: connect bredr %s ok handle=%d\r\n", ab, handle);
	copy_info(info);
	return BT_OK;
}

static int do_connect(BtInfo* info) {
	uint8_t p[25];
	int idx;
	int spins = 0;
	char ab[24];
	if (!info) {
		UARTDebugOut("[bt]: connect failed: no info\r\n");
		return BT_ERR;
	}
	if (info->peer_type == BT_ADDR_BREDR)
		return do_connect_bredr(info);
	addr_str(info->peer, ab, sizeof ab);
	if (connected) {
		char cur[24];
		addr_str(peer, cur, sizeof cur);
		UARTDebugOut("[bt]: connect LE %s refused: already connected to %s; run btctl disconnect first\r\n",
					 ab, cur);
		copy_info(info);
		if (info)
			memcpy(info->msg, "already connected", 18);
		return BT_ERR;
	}
	UARTDebugOut("[bt]: connect LE %s type=%d start\r\n", ab, info->peer_type);
	memset(p, 0, sizeof p);
	put16(p, 0x0060);
	put16(p + 2, 0x0030);
	p[4] = 0;
	p[5] = info->peer_type;
	memcpy(p + 6, info->peer, 6);
	p[12] = 0;
	put16(p + 13, 0x0018);
	put16(p + 15, 0x0028);
	put16(p + 17, 0);
	put16(p + 19, 0x01f4);
	put16(p + 21, 0);
	put16(p + 23, 0);
	conn_done = 0;
	conn_status = 0;
	if (hci(LE_CREATE_CONN, p, 25, NULL, NULL)) {
		UARTDebugOut("[bt]: connect LE %s failed: LE_CREATE_CONN submit failed\r\n", ab);
		return BT_ERR;
	}
	while (!conn_done && spins++ < 400) {
		poll_usb();
		spin_ms(5);
	}
	if (!connected) {
		UARTDebugOut("[bt]: connect LE %s failed: no conn complete (conn_done=%d status=%d); wrong type? use addr+r suffix for random\r\n",
					 ab, conn_done, conn_status);
		return BT_ERR;
	}
	idx = bond_find(peer, peer_type);
	if (idx < 0)
		idx = bond_resolve(peer);
	if (idx >= 0) {
		uint8_t z[8];
		memset(z, 0, 8);
		UARTDebugOut("[bt]: connect LE %s: known bond, re-encrypting\r\n", ab);
		if (bonds[idx].lesc)
			start_enc(z, 0, bonds[idx].ltk);
		else
			start_enc(bonds[idx].randn, bonds[idx].ediv, bonds[idx].ltk);
		spins = 0;
		while (!enc_done && spins++ < 200) {
			poll_usb();
			spin_ms(5);
		}
		UARTDebugOut("[bt]: connect LE %s: re-encrypt done enc=%d\r\n", ab, encrypted);
	}
	UARTDebugOut("[bt]: connect LE %s ok handle=%d enc=%d\r\n", ab, handle, encrypted);
	copy_info(info);
	return connected ? BT_OK : BT_ERR;
}

static int do_pair_bredr(BtInfo* info) {
	uint8_t p[2];
	int spins = 0;
	if (!connected || !handle) {
		UARTDebugOut("[bt]: pair bredr failed: not connected (conn=%d handle=%d); run btctl connect first\r\n",
					 connected, handle);
		return BT_ERR;
	}
	/* Fresh pairing every time: drop any stale bond first. A familiar
	 * BD_ADDR with a foreign key wedges some headsets (legacy fallback,
	 * silent services); the new bond is stored on success. */
	{
		int idx = bond_find(peer, peer_type);
		if (idx >= 0) {
			int i;
			char ab[24];
			addr_str(peer, ab, sizeof ab);
			for (i = idx; i + 1 < nbonds; i++)
				bonds[i] = bonds[i + 1];
			nbonds--;
			memset(&bonds[nbonds], 0, sizeof bonds[nbonds]);
			bond_dirty = 1;
			UARTDebugOut("[bt]: pair: forgot stale bond %s, pairing fresh\r\n", ab);
		}
	}
	UARTDebugOut("[bt]: pair bredr start handle=%d\r\n", handle);
	pair_bredr = 1;
	smp_on = 0;
	pair_done = 0;
	pair_fail = 0;
	encrypted = 0;
	enc_done = 0;
	put16(p, handle);
	if (hci(HCI_AUTH_REQ, p, 2, NULL, NULL)) {
		UARTDebugOut("[bt]: pair bredr failed: HCI_AUTH_REQ submit failed\r\n");
		pair_bredr = 0;
		return BT_ERR;
	}
	/* SSP + user accept on a headset can take several seconds; Linux
	 * waits ~30s. Poll ~8s before calling it a timeout. */
	while (!pair_fail && !encrypted && spins++ < 1600) {
		poll_usb();
		spin_ms(5);
	}
	pair_bredr = 0;
	copy_info(info);
	if (pair_fail) {
		UARTDebugOut("[bt]: pair bredr failed: controller rejected pairing\r\n");
		return BT_ERR;
	}
	if (!encrypted) {
		UARTDebugOut("[bt]: pair bredr failed: timeout, no encryption (did peer accept Just Works?)\r\n");
		return BT_ERR;
	}
	UARTDebugOut("[bt]: pair bredr ok enc=%d\r\n", encrypted);
	return BT_OK;
}

static int do_pair(BtInfo* info) {
	uint8_t req[7];
	int spins = 0;
	if (!connected) {
		UARTDebugOut("[bt]: pair LE failed: not connected; run btctl connect first\r\n");
		return BT_ERR;
	}
	if (peer_type == BT_ADDR_BREDR || (info && info->peer_type == BT_ADDR_BREDR))
		return do_pair_bredr(info);
	{
		int idx = bond_find(peer, peer_type);
		if (idx >= 0) {
			int i;
			char ab[24];
			addr_str(peer, ab, sizeof ab);
			for (i = idx; i + 1 < nbonds; i++)
				bonds[i] = bonds[i + 1];
			nbonds--;
			memset(&bonds[nbonds], 0, sizeof bonds[nbonds]);
			bond_dirty = 1;
			UARTDebugOut("[bt]: pair: forgot stale bond %s, pairing fresh\r\n", ab);
		}
	}
	UARTDebugOut("[bt]: pair LE start handle=%d, sending SMP request\r\n", handle);
	smp_on = 1;
	smp_sc = 0;
	pair_done = 0;
	pair_fail = 0;
	need_confirm = 0;
	user_ok = 0;
	keys_sent = 0;
	memset(ltk_new, 0, 16);
	req[0] = 0x01;
	req[1] = 0x01;
	req[2] = 0x00;
	req[3] = 0x0d;
	req[4] = 16;
	req[5] = 0x02;
	req[6] = 0x02;
	memcpy(preq, req, 7);
	smp_send(req, 7);
	while (!need_confirm && !pair_done && !pair_fail && !encrypted && spins++ < 400) {
		poll_usb();
		spin_ms(5);
	}
	copy_info(info);
	if (need_confirm && !user_ok) {
		UARTDebugOut("[bt]: pair LE needs confirm: passkey %d, run btctl confirm\r\n", passkey);
		return BT_NEED_CONFIRM;
	}
	if (pair_fail) {
		UARTDebugOut("[bt]: pair LE failed: peer sent SMP pairing-failed\r\n");
		return BT_ERR;
	}
	if (!encrypted && !pair_done) {
		UARTDebugOut("[bt]: pair LE failed: timeout, no SMP reply (mock controller has no SMP; skip pair on mock)\r\n");
		return BT_ERR;
	}
	UARTDebugOut("[bt]: pair LE ok enc=%d done=%d\r\n", encrypted, pair_done);
	return (encrypted || pair_done) ? BT_OK : BT_ERR;
}

static int do_confirm(BtInfo* info) {
	int spins = 0;
	uint8_t zero[8];
	user_ok = 1;
	need_confirm = 0;
	if (info && info->passkey)
		passkey = info->passkey;
	UARTDebugOut("[bt]: confirm start, sending DHCheck\r\n");
	memset(zero, 0, 8);
	lesc_dhcheck();
	remember_bond(1);
	start_enc(zero, 0, ltk_new);
	while (!encrypted && !pair_fail && spins++ < 300) {
		poll_usb();
		spin_ms(5);
	}
	copy_info(info);
	if (!encrypted) {
		UARTDebugOut("[bt]: confirm failed: no encryption (fail=%d); retry pair\r\n", pair_fail);
		return BT_ERR;
	}
	UARTDebugOut("[bt]: confirm ok enc=%d\r\n", encrypted);
	return BT_OK;
}

static int ble_write(uint8_t* buffer, size_t length) {
	size_t i = 0;
	if (!audio_on || !cis_handle)
		return 0;
	while (i + 1 < length) {
		int16_t s = (int16_t)(buffer[i] | (buffer[i + 1] << 8));
		pcm_acc[pcm_n++] = s;
		i += 2;
		if (pcm_n >= LC3_SAMPLES) {
			uint8_t frame[LC3_BYTES];
			uint8_t pkt[8 + LC3_BYTES];
			uint16_t hf = (uint16_t)(cis_handle | (2u << 12));
			uint16_t dlen = 4 + LC3_BYTES;
			uint16_t sl = LC3_BYTES;
			bt_lc3_encode(pcm_acc, frame);
			put16(pkt, hf);
			put16(pkt + 2, dlen);
			put16(pkt + 4, iso_seq++);
			put16(pkt + 6, sl);
			memcpy(pkt + 8, frame, LC3_BYTES);
			transport_iso(pkt, (uint16_t)(8 + LC3_BYTES));
			pcm_n = 0;
		}
	}
	return (int)length;
}

static int ble_start(void) {
	return 0;
}
static int ble_stop(void) {
	return 0;
}
static int ble_vol(uint8_t v) {
	(void)v;
	return 0;
}
static int ble_ctl(void* d, int c) {
	(void)d;
	(void)c;
	return 0;
}

static int bredr_a2dp_start(void) {
	return 0;
}
static int bredr_a2dp_stop(void) {
	return 0;
}
static int bredr_a2dp_vol(uint8_t v) {
	(void)v;
	return 0;
}
static int bredr_a2dp_ctl(void* d, int c) {
	(void)d;
	(void)c;
	return 0;
}

/* Set while a write is inside l2cap_send, which yields. Deodhai and
 * btctl both land here; a second entry would share a2dp_acc. */
static int a2dp_writing;

/* How many L2CAP payload bytes fit in one controller ACL buffer.
 * Splitting a media packet spends a credit per piece. */
static int a2dp_tx_limit(void) {
	int mtu = a2dp_media_mtu ? (int)a2dp_media_mtu : 672;
	if (acl_mtu > 20) {
		int acl = (int)acl_mtu - 4;
		if (acl < mtu)
			mtu = acl;
	}
	if (mtu > (int)sizeof a2dp_pkt)
		mtu = (int)sizeof a2dp_pkt;
	/* l2cap_send holds the 4-byte L2CAP header plus this payload. */
	if (mtu > 1100 - 4)
		mtu = 1100 - 4;
	if (mtu < 13 + 16)
		mtu = 13 + 16;
	return mtu;
}

static int a2dp_send_pkt(void) {
	int plen;
	uint32_t before;
	int nfr;
	static int fails;
	if (!a2dp_pkt_frames)
		return 0;
	nfr = a2dp_pkt_frames;
	/* 128 samples per SBC frame. Sleep until the sample clock catches
	 * the packets already queued, so a fast write cannot run ahead of
	 * the 200 ms ACL flush timeout. */
	if (_scheduler_initialized && cpuFrequency) {
		uint64_t now = AA64GetPhysicalTimerCount() / cpuFrequency;
		uint32_t hz = (a2dp_freq >= 40000) ? (uint32_t)a2dp_freq : 48000;
		if (!a2dp_frames || !a2dp_t0)
			a2dp_t0 = now;
		else if (now >= a2dp_t0) {
			uint64_t audio_us = (uint64_t)a2dp_frames * 128ull * 1000000ull / hz;
			uint64_t wall = now - a2dp_t0;
			/* btctl already stopped. Sending the idle gap as fast as
			 * the controller will take it overruns the 200 ms flush
			 * and the next track starts late. Rebase to this packet. */
			if (audio_us + 100000ull < wall) {
				a2dp_t0 = now - audio_us;
				wall = audio_us;
			}
			if (audio_us > wall + 2000ull) {
				uint32_t ms = (uint32_t)((audio_us - wall) / 1000ull);
				if (ms > 40)
					ms = 40;
				if (ms)
					spin_ms(ms);
			}
		}
	}
	avdtp_build_media_hdr(a2dp_pkt, a2dp_seq, a2dp_ts, a2dp_ssrc, (uint8_t)nfr);
	if (a2dp_seq == 0)
		a2dp_pkt[1] = (uint8_t)(a2dp_pkt[1] | 0x80);
	plen = 13 + a2dp_pkt_n;
	if (l2cap_send(a2dp_media_rem, a2dp_pkt, (uint16_t)plen)) {
		if ((fails++ & 31) == 0)
			UARTDebugOut("[bt]: a2dp media send failed cid=%d credits=%d len=%d\r\n",
						 a2dp_media_rem, acl_credits, plen);
		return -1;
	}
	fails = 0;
	a2dp_seq++;
	a2dp_ts += 128u * (uint32_t)nfr;
	before = a2dp_frames;
	a2dp_frames += (uint32_t)nfr;
	if (before == 0 || (before / 256u) != (a2dp_frames / 256u))
		UARTDebugOut("[bt]: a2dp streaming frames=%d cid=%d credits=%d pkt=%d\r\n",
					 a2dp_frames, a2dp_media_rem, acl_credits, nfr);
	a2dp_pkt_n = 0;
	a2dp_pkt_frames = 0;
	return 0;
}

/* PCM sink for classic audio: 44.1/48 kHz stereo int16 in, as many SBC
 * frames as the peer MTU holds in one RTP packet. */
static int bredr_a2dp_write(uint8_t* buffer, size_t length) {
	size_t i = 0;
	if (!a2dp_on || a2dp_state != 1 || !a2dp_media_rem)
		return 0;
	if (a2dp_writing)
		return (int)length;
	a2dp_writing = 1;
	while (i < length) {
		size_t room = sizeof a2dp_acc - (size_t)a2dp_acc_n;
		size_t take = length - i < room ? length - i : room;
		int expect;
		memcpy(a2dp_acc + a2dp_acc_n, buffer + i, take);
		a2dp_acc_n += (int)take;
		i += take;
		expect = (int)bt_sbc_frame_len(a2dp_bitpool);
		while (a2dp_acc_n >= 512) {
			int16_t pcm[256];
			int k, n;
			int limit;
			if (expect <= 0) {
				a2dp_writing = 0;
				return (int)length;
			}
			limit = a2dp_tx_limit();
			if (a2dp_pkt_frames >= 15 || 13 + a2dp_pkt_n + expect > limit) {
				if (a2dp_send_pkt()) {
					a2dp_writing = 0;
					return (int)length;
				}
			}
			for (k = 0; k < 256; k++)
				pcm[k] = (int16_t)(a2dp_acc[2 * k] |
								   (a2dp_acc[2 * k + 1] << 8));
			n = bt_sbc_encode(&a2dp_enc, pcm, a2dp_bitpool,
							  a2dp_freq == 48000 ? SBC_FREQ_48000
												 : SBC_FREQ_44100,
							  a2dp_pkt + 13 + a2dp_pkt_n);
			if (n <= 0) {
				a2dp_writing = 0;
				return (int)length;
			}
			a2dp_pkt_n += n;
			a2dp_pkt_frames++;
			a2dp_acc_n -= 512;
			/* Forward shift (dest < src). No memmove in this libc. */
			for (k = 0; k < a2dp_acc_n; k++)
				a2dp_acc[k] = a2dp_acc[k + 512];
		}
	}
	a2dp_send_pkt();
	a2dp_writing = 0;
	return (int)length;
}

/* One SEP's capabilities. AVDTP >= 1.3 (BlueZ avdtp_discover_resp) asks
 * GET_ALL_CAPABILITIES first; older peers, and a reject of that command,
 * fall back to GET_CAPABILITIES. 0 when av_rsp holds an accept. */
static int a2dp_fetch_caps(uint8_t seid, uint16_t ver) {
	uint8_t cmd[8];
	int all_first = ver >= 0x0103;
	int i;
	for (i = 0; i < 2; i++) {
		int use_all = all_first ? (i == 0) : (i == 1);
		uint8_t t = avdtp_next();
		int n;
		uint8_t sig;
		if (use_all) {
			n = avdtp_build_getallcaps(cmd, t, seid);
			sig = AVDTP_GET_ALLCAPABILITIES;
			UARTDebugOut("[bt]: a2dp GET_ALLCAPABILITIES seid=%d\r\n", seid);
		} else {
			n = avdtp_build_getcaps(cmd, t, seid);
			sig = AVDTP_GET_CAPABILITIES;
			UARTDebugOut("[bt]: a2dp GET_CAPABILITIES seid=%d\r\n", seid);
		}
		if (!avdtp_round(cmd, n, sig))
			return 0;
	}
	return -1;
}

/* A2DP source setup on an encrypted BREDR link: SDP AudioSink search
 * (like every desktop initiator), signaling CoC, DISCOVER,
 * capabilities of every sink SEP, SET_CONFIG (SBC), OPEN, media CoC,
 * START, register bredr0. Discover records are 2 bytes each with no
 * count; the first ULT WEAR sink is SBC and the second is AAC. */
static int do_a2dp(BtInfo* info) {
	uint8_t cmd[32];
	uint8_t sbc_caps[4], use[4];
	uint8_t sinks[8];
	uint16_t avpsm = AVDTP_PSM;
	uint16_t av_ver = 0;
	uint32_t sunk = 0;
	int nseids, si, f44100 = 1;
	uint8_t t;
	int att;
	uint16_t tid;
	if (!connected || !handle || peer_type != BT_ADDR_BREDR) {
		UARTDebugOut("[bt]: a2dp failed: need connected BREDR (conn=%d type=%d); pair a classic headset first\r\n",
					 connected, peer_type);
		if (info) {
			memcpy(info->msg, "no A2DP", 8);
			info->status = BT_NO_AUDIO;
		}
		return BT_NO_AUDIO;
	}
	a2dp_sig_loc = 0x40;
	a2dp_sig_rem = 0;
	a2dp_media_loc = 0x41;
	a2dp_media_rem = 0;
	a2dp_seid = 0;
	a2dp_state = 0;
	sdp_loc = 0x42;
	sdp_rem = 0;
	avrcp_loc = 0x43;
	avrcp_rem = 0;
	/* Fresh capture: the fail path dumps the snoop tail, so start it here
	 * and the dump holds this handshake instead of old scan traffic. */
	snoop_open();
	UARTDebugOut("[bt]: a2dp start handle=%d, SDP AudioSink search\r\n", handle);
	if (l2cap_bredr_open(SDP_PSM, sdp_loc, &sdp_rem)) {
		/* A2DP's AVDTP PSM is the assigned number 25. SDP only confirms
		 * it. A channel that never opens must not block audio. */
		UARTDebugOut("[bt]: a2dp SDP channel failed, using AVDTP PSM %d\r\n",
					 AVDTP_PSM);
		avpsm = AVDTP_PSM;
		goto sdp_done_ok;
	}
	/* Combined query first (one round, BlueZ default). */
	tid = sdp_next_tid();
	{
		int sl = sdp_build_search_attr(cmd, tid, SDP_UUID_AUDIOSINK,
									   SDP_ATTR_PROTO_LIST);
		if (!sdp_round(cmd, sl)) {
			uint16_t psm = 0, ver = 0;
			if (!sdp_parse_ssa_psm(sdp_rsp, sdp_rlen, tid, &psm, &ver) && psm) {
				avpsm = psm;
				av_ver = ver;
				UARTDebugOut("[bt]: a2dp peer AVDTP PSM=%d ver=%d (SSA)\r\n", psm,
							 ver);
				goto sdp_done_ok;
			}
			UARTDebugOut("[bt]: a2dp SSA gave no PSM, trying split search\r\n");
		}
	}
	tid = sdp_next_tid();
	{
		int sl = sdp_build_search(cmd, tid, SDP_UUID_AUDIOSINK);
		if (sdp_round(cmd, sl)) {
			UARTDebugOut("[bt]: a2dp SDP silent, using AVDTP PSM %d\r\n",
						 AVDTP_PSM);
			avpsm = AVDTP_PSM;
			goto sdp_done_ok;
		}
	}
	if (sdp_parse_search_rsp(sdp_rsp, sdp_rlen, tid, &sunk)) {
		UARTDebugOut("[bt]: a2dp no AudioSink record, using AVDTP PSM %d\r\n",
					 AVDTP_PSM);
		avpsm = AVDTP_PSM;
		goto sdp_done_ok;
	}
	UARTDebugOut("[bt]: a2dp AudioSink handle found\r\n");
	tid = sdp_next_tid();
	{
		int sl = sdp_build_attr(cmd, tid, sunk, SDP_ATTR_PROTO_LIST);
		if (sdp_round(cmd, sl)) {
			UARTDebugOut("[bt]: a2dp SDP attr failed, using AVDTP PSM %d\r\n",
						 AVDTP_PSM);
			avpsm = AVDTP_PSM;
			goto sdp_done_ok;
		}
	}
	{
		uint16_t psm = 0, ver = 0;
		if (!sdp_parse_attr_psm(sdp_rsp, sdp_rlen, tid, &psm, &ver) && psm) {
			avpsm = psm;
			av_ver = ver;
			UARTDebugOut("[bt]: a2dp peer AVDTP PSM=%d ver=%d\r\n", psm, ver);
		} else {
			UARTDebugOut("[bt]: a2dp peer PSM unreadable, assuming %d\r\n",
						 AVDTP_PSM);
		}
	}
sdp_done_ok:
	l2cap_bredr_close(sdp_rem, sdp_loc);
	sdp_rem = 0;
	UARTDebugOut("[bt]: a2dp opening signaling channel psm=%d\r\n", avpsm);
	if (l2cap_bredr_open(avpsm, a2dp_sig_loc, &a2dp_sig_rem))
		goto fail;
	t = avdtp_next();
	avdtp_build_discover(cmd, t);
	UARTDebugOut("[bt]: a2dp DISCOVER\r\n");
	for (att = 0; att < 3; att++) {
		if (!avdtp_round(cmd, 2, AVDTP_DISCOVER))
			break;
		UARTDebugOut("[bt]: a2dp DISCOVER retry %d\r\n", att + 1);
	}
	if (att >= 3)
		goto fail;
	nseids = avdtp_list_sink_seids(av_rsp, av_rlen, sinks, 8);
	if (nseids <= 0) {
		UARTDebugOut("[bt]: a2dp failed: no audio sink SEP in DISCOVER rsp\r\n");
		goto fail;
	}
	for (si = 0; si < nseids; si++) {
		int codec;
		a2dp_seid = sinks[si];
		UARTDebugOut("[bt]: a2dp sink seid=%d\r\n", a2dp_seid);
		if (a2dp_fetch_caps(a2dp_seid, av_ver)) {
			UARTDebugOut("[bt]: a2dp seid=%d capabilities failed\r\n", a2dp_seid);
			continue;
		}
		codec = avdtp_media_codec(av_rsp, av_rlen);
		UARTDebugOut("[bt]: a2dp seid=%d codec=%d\r\n", a2dp_seid, codec);
		if (avdtp_parse_sbc_caps(av_rsp, av_rlen, sbc_caps))
			continue;
		if (avdtp_pick_sbc_config(sbc_caps, use, &f44100)) {
			UARTDebugOut("[bt]: a2dp seid=%d no SBC overlap (%02x %02x %02x %02x)\r\n",
						 a2dp_seid, sbc_caps[0], sbc_caps[1], sbc_caps[2],
						 sbc_caps[3]);
			continue;
		}
		break;
	}
	if (si >= nseids) {
		UARTDebugOut("[bt]: a2dp failed: sink offers no SBC codec\r\n");
		goto fail;
	}
	a2dp_freq = f44100 ? 44100 : 48000;
	a2dp_bitpool = use[3];
	UARTDebugOut("[bt]: a2dp SBC %dHz bitpool=%d delay=%d\r\n", a2dp_freq,
				 a2dp_bitpool, avdtp_caps_have_delay(av_rsp, av_rlen));
	t = avdtp_next();
	{
		int clen = avdtp_build_setconfig(cmd, t, a2dp_seid, 1, use[0], use[1],
										 use[2], use[3],
										 avdtp_caps_have_delay(av_rsp, av_rlen));
		UARTDebugOut("[bt]: a2dp SET_CONFIGURATION\r\n");
		if (avdtp_round(cmd, clen, AVDTP_SET_CONFIGURATION))
			goto fail;
	}
	t = avdtp_next();
	avdtp_build_open(cmd, t, a2dp_seid);
	UARTDebugOut("[bt]: a2dp OPEN\r\n");
	if (avdtp_round(cmd, 3, AVDTP_OPEN))
		goto fail;
	UARTDebugOut("[bt]: a2dp opening media channel\r\n");
	if (l2cap_bredr_open(AVDTP_PSM, a2dp_media_loc, &a2dp_media_rem))
		goto fail;
	/* HCI Write Automatic Flush Timeout (0x0C28). 0x0140 * 0.625 ms = 200 ms.
	 * BlueZ marks the A2DP transport flushable; without this the controller
	 * keeps the new media PDUs even after the PB flag says flushable. */
	{
		uint8_t fl[4];
		put16(fl, handle);
		put16(fl + 2, 0x0140);
		if (!hci(0x0C28, fl, 4, NULL, NULL))
			UARTDebugOut("[bt]: a2dp acl flush timeout 200ms\r\n");
		else
			UARTDebugOut("[bt]: a2dp acl flush timeout rejected\r\n");
	}
	t = avdtp_next();
	avdtp_build_start(cmd, t, a2dp_seid);
	UARTDebugOut("[bt]: a2dp START\r\n");
	if (avdtp_round(cmd, 3, AVDTP_START))
		goto fail;
	memset(&a2dp_card, 0, sizeof a2dp_card);
	memcpy(a2dp_card.name, "bredr0", 7);
	a2dp_card.write = bredr_a2dp_write;
	a2dp_card.start_output = bredr_a2dp_start;
	a2dp_card.stop_output = bredr_a2dp_stop;
	a2dp_card.set_vol = bredr_a2dp_vol;
	a2dp_card.control = bredr_a2dp_ctl;
	a2dp_card._force_write = 1;
	if (AuSoundRegisterCard(&a2dp_card) == 0)
		card2_id = au_sound_last_id;
	bt_sbc_init(&a2dp_enc);
	a2dp_seq = 0;
	a2dp_ts = 0;
	fill_rand((uint8_t*)&a2dp_ssrc, 4);
	a2dp_acc_n = 0;
	a2dp_pkt_n = 0;
	a2dp_pkt_frames = 0;
	a2dp_frames = 0;
	a2dp_t0 = 0;
	a2dp_state = 1;
	a2dp_on = 1;
	/* Best-effort AVRCP (AVCTP PSM 23) for media keys / absolute volume,
	 * like BlueZ connecting the control channel after AVDTP START. Never
	 * fatal: audio already streams without it. */
	if (!l2cap_bredr_open(23, avrcp_loc, &avrcp_rem))
		UARTDebugOut("[bt]: avrcp control channel open (mtu=%d)\r\n", l2_peer_mtu);
	else {
		avrcp_rem = 0;
		UARTDebugOut("[bt]: avrcp control channel unavailable (audio unaffected)\r\n");
	}
	copy_info(info);
	if (info) {
		memcpy(info->msg, "bredr0", 7);
		info->card_id = card2_id;
	}
	UARTDebugOut("[bt]: a2dp streaming on bredr0 card=%d\r\n", card2_id);
	return BT_OK;
fail:
	/* Release any half-open channels: abandoned CoCs linger on the
	 * headset and can wedge its acceptor across runs. */
	l2cap_bredr_close(avrcp_rem, avrcp_loc);
	l2cap_bredr_close(a2dp_media_rem, a2dp_media_loc);
	l2cap_bredr_close(a2dp_sig_rem, a2dp_sig_loc);
	l2cap_bredr_close(sdp_rem, sdp_loc);
	a2dp_sig_rem = 0;
	a2dp_media_rem = 0;
	a2dp_media_mtu = 0;
	a2dp_pkt_n = 0;
	a2dp_pkt_frames = 0;
	sdp_rem = 0;
	avrcp_rem = 0;
	if (info) {
		memcpy(info->msg, "no A2DP", 8);
		info->status = BT_NO_AUDIO;
		info->card_id = -1;
	}
	/* Wire truth: hexdump the tail of the BTSnoop buffer so the handshake
	 * can be decoded by hand (or Wireshark via /bt/btsnoop.log). */
	{
		uint32_t tail = snoop_len > 384 ? snoop_len - 384 : 0;
		uint32_t off = tail;
		UARTDebugOut("[bt]: snoop tail %d bytes from %d\r\n", snoop_len - tail, tail);
		while (off < snoop_len) {
			int chunk = (int)(snoop_len - off);
			if (chunk > 48)
				chunk = 48;
			bt_hex("snoop", snoop + off, chunk);
			off += (uint32_t)chunk;
		}
	}
	return BT_NO_AUDIO;
}

static int do_audio(BtInfo* info) {
	uint8_t feat[2];
	uint8_t cig[31];
	uint8_t rsp[16];
	uint8_t rlen;
	uint8_t cis[5];
	uint8_t path[13];
	int spins = 0;
	int hci_rc;
	int card_rc;
	feat[0] = 32;
	feat[1] = 1;
	hci(LE_SET_HOST_FEAT, feat, 2, NULL, NULL);
	UARTDebugOut("[bt]: audio start conn=%d type=%d handle=%d iso_mtu=%d\r\n", connected,
				 peer_type, handle, iso_mtu);
	if (!connected) {
		UARTDebugOut("[bt]: audio failed: not connected; run btctl connect first\r\n");
		if (info) {
			memcpy(info->msg, "no LE Audio", 12);
			info->status = BT_NO_AUDIO;
		}
		return BT_NO_AUDIO;
	}
	if (peer_type == BT_ADDR_BREDR)
		UARTDebugOut("[bt]: audio warning: peer is BREDR, CIS needs LE transport and will likely fail\r\n");
	if (!iso_mtu) {
		uint8_t brsp[16];
		uint8_t blen = sizeof brsp;
		if (!hci(LE_READ_BUF_V2, NULL, 0, brsp, &blen) && blen >= 6 && brsp[0] == 0)
			iso_mtu = get16(brsp + 4);
		UARTDebugOut("[bt]: audio re-read ISO buf iso_mtu=%d\r\n", iso_mtu);
	}
	if (!iso_mtu) {
		UARTDebugOut("[bt]: audio failed: controller reports no ISO (iso_mtu=0); adapter cannot do LE Audio\r\n");
		if (info) {
			memcpy(info->msg, "no LE Audio", 12);
			info->status = BT_NO_AUDIO;
		}
		return BT_NO_AUDIO;
	}
	memset(cig, 0, sizeof cig);
	cig[0] = 0;
	cig[1] = 0x10;
	cig[2] = 0x27;
	cig[3] = 0x00;
	cig[4] = 0x10;
	cig[5] = 0x27;
	cig[6] = 0x00;
	cig[7] = 0;
	cig[8] = 0;
	cig[9] = 0;
	put16(cig + 10, 10);
	put16(cig + 12, 10);
	cig[14] = 1;
	cig[15] = 0;
	put16(cig + 16, 40);
	put16(cig + 18, 0);
	/* LE 1M PHY. 2M is optional and a controller that only has 1M
	 * rejects the CIG, which btctl reports as "no LE Audio". */
	cig[20] = 0x01;
	cig[21] = 0x01;
	cig[22] = 2;
	cig[23] = 2;
	rlen = sizeof rsp;
	memset(rsp, 0, sizeof rsp);
	hci_rc = hci(LE_SET_CIG, cig, 24, rsp, &rlen);
	if (hci_rc || rsp[0] != 0) {
		UARTDebugOut("[bt]: audio failed: SET_CIG rejected (hci_rc=%d status=%d); controller or peer refused 1M/16k/10ms/40-octet\r\n",
					 hci_rc, rsp[0]);
		if (info)
			memcpy(info->msg, "no LE Audio", 12);
		return BT_NO_AUDIO;
	}
	if (rlen >= 5)
		cis_handle = get16(rsp + 3);
	UARTDebugOut("[bt]: audio CIG ok cis=%d, creating CIS on acl=%d\r\n", cis_handle, handle);
	cis[0] = 1;
	put16(cis + 1, cis_handle);
	put16(cis + 3, handle);
	hci(LE_CREATE_CIS, cis, 5, NULL, NULL);
	while (spins++ < 200) {
		poll_usb();
		spin_ms(5);
	}
	UARTDebugOut("[bt]: audio CIS wait done cis=%d, setting ISO path\r\n", cis_handle);
	memset(path, 0, sizeof path);
	put16(path, cis_handle);
	path[2] = 0;
	path[3] = 0;
	path[4] = 0x06;
	hci_rc = hci(LE_SETUP_ISO, path, 13, NULL, NULL);
	if (hci_rc)
		UARTDebugOut("[bt]: audio warning: SETUP_ISO submit failed (rc=%d)\r\n", hci_rc);
	memset(&ble_card, 0, sizeof ble_card);
	memcpy(ble_card.name, "ble0", 5);
	ble_card.write = ble_write;
	ble_card.start_output = ble_start;
	ble_card.stop_output = ble_stop;
	ble_card.set_vol = ble_vol;
	ble_card.control = ble_ctl;
	ble_card._force_write = 1;
	card_rc = AuSoundRegisterCard(&ble_card);
	if (card_rc == 0)
		card_id = au_sound_last_id;
	UARTDebugOut("[bt]: audio ok card=%d (register rc=%d)\r\n", card_id, card_rc);
	audio_on = 1;
	copy_info(info);
	if (info) {
		memcpy(info->msg, "ble0", 5);
		info->card_id = card_id;
	}
	return BT_OK;
}

static int perform(int code, BtInfo* info) {
	int st = BT_ERR;
	switch (code) {
	case BT_GET_INFO:
		copy_info(info);
		st = (have_usb || have_h4uart) ? BT_OK : BT_ERR;
		break;
	case BT_SCAN:
		st = do_scan(info);
		break;
	case BT_CONNECT:
		st = do_connect(info);
		break;
	case BT_DISCONNECT: {
		uint8_t p[3];
		if (a2dp_state == 1 && a2dp_sig_rem) {
			uint8_t sus[4];
			avdtp_build_suspend(sus, avdtp_next(), a2dp_seid);
			l2cap_send(a2dp_sig_rem, sus, 3);
			UARTDebugOut("[bt]: a2dp suspended for disconnect\r\n");
		}
		a2dp_state = 0;
		a2dp_on = 0;
		l2cap_bredr_close(avrcp_rem, avrcp_loc);
		l2cap_bredr_close(a2dp_media_rem, a2dp_media_loc);
		l2cap_bredr_close(a2dp_sig_rem, a2dp_sig_loc);
		l2cap_bredr_close(sdp_rem, sdp_loc);
		a2dp_sig_rem = 0;
		a2dp_media_rem = 0;
		a2dp_media_mtu = 0;
		a2dp_pkt_n = 0;
		a2dp_pkt_frames = 0;
		sdp_rem = 0;
		avrcp_rem = 0;
		put16(p, handle);
		p[2] = 0x13;
		st = hci(HCI_DISCONNECT, p, 3, NULL, NULL) ? BT_ERR : BT_OK;
		copy_info(info);
		break;
	}
	case BT_READ_NAME:
		st = read_peer_name() ? BT_ERR : BT_OK;
		copy_info(info);
		break;
	case BT_PAIR:
		st = do_pair(info);
		break;
	case BT_CONFIRM:
	case BT_PASSKEY:
		st = do_confirm(info);
		break;
	case BT_AUDIO:
		st = do_audio(info);
		break;
	case BT_A2DP:
		st = do_a2dp(info);
		break;
	case BT_FORGET: {
		int idx = bond_find(info->peer, info->peer_type);
		if (idx < 0) {
			memcpy(info->msg, "no bond", 8);
			st = BT_ERR;
		} else {
			int i;
			char ab[24];
			addr_str(info->peer, ab, sizeof ab);
			for (i = idx; i + 1 < nbonds; i++)
				bonds[i] = bonds[i + 1];
			nbonds--;
			memset(&bonds[nbonds], 0, sizeof bonds[nbonds]);
			bond_dirty = 1;
			bond_flush();
			copy_info(info);
			memcpy(info->msg, "forgot", 7);
			UARTDebugOut("[bt]: forgot bond %s\r\n", ab);
			st = BT_OK;
		}
		break;
	}
	default:
		st = BT_ERR;
		break;
	}
	/* Serial mirror: btctl stdout goes to the framebuffer console in TERM
	 * runs, so the QEMU stdio term only sees UART output. Per-stage
	 * detail is logged inside do_connect/pair/audio; this is the summary. */
	UARTDebugOut("[bt-test]: cmd=%d st=%d nscan=%d conn=%d enc=%d\r\n", code, st,
				 info ? info->nscan : -1, connected, encrypted);
	{
		const char* op = "unknown";
		const char* res = "err";
		if (code == BT_GET_INFO)
			op = "info";
		else if (code == BT_SCAN)
			op = "scan";
		else if (code == BT_CONNECT)
			op = "connect";
		else if (code == BT_DISCONNECT)
			op = "disconnect";
		else if (code == BT_READ_NAME)
			op = "name";
		else if (code == BT_PAIR)
			op = "pair";
		else if (code == BT_CONFIRM)
			op = "confirm";
		else if (code == BT_PASSKEY)
			op = "passkey";
		else if (code == BT_AUDIO)
			op = "audio";
		else if (code == BT_A2DP)
			op = "a2dp";
		else if (code == BT_FORGET)
			op = "forget";
		if (st == BT_OK)
			res = "ok";
		else if (st == BT_NEED_CONFIRM)
			res = "need-confirm";
		else if (st == BT_NO_AUDIO)
			res = "no-audio";
		UARTDebugOut("[bt]: %s => %s conn=%d enc=%d handle=%d iso=%d card=%d\r\n", op, res,
					 connected, encrypted, handle, iso_mtu ? 1 : 0, card_id);
		if (info && info->msg[0])
			UARTDebugOut("[bt]: %s msg=%s\r\n", op, info->msg);
		if (code == BT_PAIR && st == BT_NEED_CONFIRM)
			UARTDebugOut("[bt]: pair next: run btctl confirm (passkey %d)\r\n", passkey);
	}
	if (info && code == BT_SCAN) {
		int si;
		for (si = 0; si < info->nscan && si < BT_MAX_SCAN; si++) {
			char ab[24];
			addr_str(info->scan[si].addr, ab, sizeof ab);
			UARTDebugOut("[bt-test]: scan %s type=%d rssi=%d %s\r\n", ab,
						 info->scan[si].addr_type, info->scan[si].rssi,
						 info->scan[si].name);
		}
	}
	if (info && code == BT_READ_NAME)
		UARTDebugOut("[bt-test]: name=%s\r\n", info->peer_name);
	if (info)
		info->status = st;
	/* Thread context here (same place snoop_flush already runs safely),
	 * never inside the HCI event handler. */
	bond_flush();
	snoop_flush();
	return st;
}

static void bt_thread(uint64_t arg) {
	(void)arg;
	for (;;) {
		if (job && !job_done) {
			/* TEMP-BT-TEST (revert before commit) */
			UARTDebugOut("[bt-test]: pickup job=%d\r\n", job);
			job_status = perform(job, job_info);
			job = 0;
			/* perform() writes job_buf. The waiter runs on another thread
			 * and must not copy that buffer until those stores are done.
			 * Do not point job_info at the caller's BtInfo or switch TTBR0
			 * to the caller's table: do_scan yields, and the scheduler
			 * reloads this thread's root table before copy_info runs. */
			__asm__ volatile("dmb ish" ::: "memory");
			job_done = 1;
		} else {
			poll_usb();
		}
		AuSleepThread(AuGetCurrentThread(), 10);
		AuScheduleNext();
	}
}

static int submit(int code, BtInfo* info) {
	int spins = 0;
	int st;
	if (!info)
		return BT_ERR;
	/* TEMP-BT-TEST (revert before commit) */
	UARTDebugOut("[bt-test]: submit code=%d started=%d\r\n", code, bt_started);
	/* bthost runs on the root page table. Its low half is empty, so a
	 * user BtInfo pointer faults there. Copy through kernel memory and
	 * write the result back on this thread, which still has the caller. */
	if (!bt_started)
		return perform(code, info);
	memcpy(&job_buf, info, sizeof job_buf);
	job_info = &job_buf;
	job_done = 0;
	job_status = BT_ERR;
	__asm__ volatile("dmb ish" ::: "memory");
	job = code;
	/* A2DP can spend several seconds in SDP plus the AVDTP handshake.
	 * 800 * 20 ms (16 s) expired before that finished and btctl exited
	 * while the headset was still coming up. */
	while (!job_done && spins++ < 3000) {
		AuSleepThread(AuGetCurrentThread(), 20);
		AuScheduleNext();
	}
	st = job_done ? job_status : BT_ERR;
	__asm__ volatile("dmb ish" ::: "memory");
	memcpy(info, &job_buf, sizeof job_buf);
	if (!job_done && info->msg[0] == 0)
		memcpy(info->msg, "timed out", 10);
	return st;
}

static size_t bt_read(AuVFSNode* fs, AuVFSNode* file, uint64_t* buffer, uint32_t length) {
	char text[128];
	char addr[24];
	uint32_t n;
	(void)fs;
	(void)file;
	addr_str(bd, addr, sizeof addr);
	text[0] = 0;
	{
		int i = 0;
		const char* s = addr;
		while (*s && i < 40)
			text[i++] = *s++;
		text[i++] = ' ';
		text[i++] = 'i';
		text[i++] = 's';
		text[i++] = 'o';
		text[i++] = '=';
		text[i++] = iso_mtu ? '1' : '0';
		text[i++] = '\n';
		text[i] = 0;
		n = (uint32_t)i;
	}
	if (n > length)
		n = length;
	memcpy(buffer, text, n);
	return n;
}

static int bt_ioctl(AuVFSNode* node, int code, void* arg) {
	(void)node;
	return submit(code, (BtInfo*)arg);
}

static void publish_dev(void) {
	AuVFSNode* dev = AuVFSFind("/dev");
	AuVFSNode* node;
	if (!dev)
		return;
	node = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	if (!node)
		return;
	memset(node, 0, sizeof *node);
	memcpy(node->filename, "bt0", 4);
	node->flags = FS_FLAG_DEVICE;
	node->read = bt_read;
	node->iocontrol = bt_ioctl;
	AuDevFSAddFile(dev, "/bt0", node);
}

static void identity(void) {
	uint8_t mask[8];
	uint8_t lehost[2];
	uint8_t feat[2];
	uint8_t rsp[32];
	uint8_t rlen;
	char shown[24];
	memset(mask, 0xff, 8);
	hci(HCI_SET_EVENT_MASK, mask, 8, NULL, NULL);
	hci(HCI_RESET, NULL, 0, NULL, NULL);
	hci(HCI_SET_EVENT_MASK, mask, 8, NULL, NULL);
	/* Secure Simple Pairing + Secure Connections host support, like BlueZ
	 * adapter init. Without Simple Pairing Mode the controller answers
	 * every pairing with legacy PIN (Sony kept asking for 0000) and the
	 * link never becomes an SSP-bonded one. */
	{
		uint8_t one = 1;
		if (!hci(HCI_WRITE_SIMPLE_PAIR_MODE, &one, 1, NULL, NULL))
			UARTDebugOut("[bt]: simple pairing mode on\r\n");
		else
			UARTDebugOut("[bt]: WARN simple pairing mode rejected\r\n");
		if (!hci(HCI_WRITE_SEC_CONN_HOST, &one, 1, NULL, NULL))
			UARTDebugOut("[bt]: secure connections host support on\r\n");
		else
			UARTDebugOut("[bt]: WARN secure connections rejected\r\n");
		/* Class of Device: Rendering + Audio/Video major, like a BlueZ
		 * desktop source (0x200414: major Audio/Video, minor Loudspeaker,
		 * service Rendering). Headsets use CoD/EIR to pick codecs and to
		 * decide whether SDP is worth answering. Best-effort. */
		{
			uint8_t cod[3] = {0x14, 0x04, 0x20};
			if (!hci(0x0C24, cod, 3, NULL, NULL))
				UARTDebugOut("[bt]: class of device 200414 (rendering/audio)\r\n");
		}
	}
	lehost[0] = 1;
	lehost[1] = 0;
	hci(HCI_WRITE_LE_HOST, lehost, 2, NULL, NULL);
	hci(LE_SET_EVENT_MASK, mask, 8, NULL, NULL);
	rlen = sizeof rsp;
	if (!hci(HCI_READ_VER, NULL, 0, rsp, &rlen) && rlen >= 2)
		hci_ver = rsp[1];
	rlen = sizeof rsp;
	if (!hci(HCI_READ_BD, NULL, 0, rsp, &rlen) && rlen >= 7)
		memcpy(bd, rsp + 1, 6);
	rlen = sizeof rsp;
	if (!hci(HCI_READ_BUF, NULL, 0, rsp, &rlen) && rlen >= 5) {
		uint16_t m = get16(rsp + 1);
		if (m) {
			/* HCI_Read_Buffer_Size return: status, ACL length (2),
			 * SCO length (1), ACL packet count (2), SCO count (2).
			 * The count used to be read at rsp+3, which is the SCO
			 * length byte plus the low half of the count, so a count
			 * of 4 became 1024 credits. The host then pushed 1024
			 * media packets (the log stops at frames=1024, credits=0)
			 * into a controller that only has a handful of slots. */
			acl_mtu = m;
			acl_max = (rlen >= 7) ? get16(rsp + 4) : rsp[4];
			if (acl_max == 0)
				acl_max = 1;
			acl_credits = acl_max;
		}
	}
	/* LE buffer size is kept separate: it only bounds LE CoCs. The old
	 * code overwrote the classic ACL MTU with the (much smaller) LE
	 * value, fragmenting every BREDR L2CAP frame into 27-octet HCI
	 * chunks like no BlueZ host does. */
	rlen = sizeof rsp;
	if (!hci(LE_READ_BUF, NULL, 0, rsp, &rlen) && rlen >= 4 && get16(rsp + 1)) {
		acl_mtu_le = get16(rsp + 1);
	}
	UARTDebugOut("[bt]: acl_mtu_classic=%d acl_mtu_le=%d credits=%d\r\n", acl_mtu,
				 acl_mtu_le, acl_max);
	rlen = sizeof rsp;
	if (!hci(LE_READ_FEAT, NULL, 0, rsp, &rlen) && rlen >= 9)
		memcpy(le_feat, rsp + 1, 8);
	/* Bit 32 is Connected Isochronous Stream (Host Support). Controllers
	 * report a zero ISO packet length until the host sets it, which made
	 * btctl audio print "no LE Audio" on adapters that can do LE Audio.
	 * The bit has to be set before any connection exists. */
	feat[0] = 32;
	feat[1] = 1;
	hci(LE_SET_HOST_FEAT, feat, 2, NULL, NULL);
	rlen = sizeof rsp;
	iso_mtu = 0;
	if (!hci(LE_READ_BUF_V2, NULL, 0, rsp, &rlen) && rlen >= 6 && rsp[0] == 0)
		iso_mtu = get16(rsp + 4);
	if (!iso_mtu) {
		rlen = sizeof rsp;
		if (!hci(LE_READ_BUF_V2, NULL, 0, rsp, &rlen) && rlen >= 6 && rsp[0] == 0)
			iso_mtu = get16(rsp + 4);
	}
	addr_str(bd, shown, sizeof shown);
	UARTDebugOut("[aurora]: bt %s iso=%d\r\n", shown, iso_mtu ? 1 : 0);
	/* Boot-time LE scan — results go to the QEMU serial console. */
	{
		uint8_t par[7];
		uint8_t en[2];
		int spins = 0;
		par[0] = 0x01;
		put16(par + 1, 0x0060);
		put16(par + 3, 0x0030);
		par[5] = 0;
		par[6] = 0;
		nscan = 0;
		if (!hci(LE_SET_SCAN_PAR, par, 7, NULL, NULL)) {
			en[0] = 1;
			en[1] = 0;
			if (!hci(LE_SET_SCAN_EN, en, 2, NULL, NULL)) {
				while (spins++ < 300) {
					poll_usb();
					spin_ms(10);
				}
				en[0] = 0;
				hci(LE_SET_SCAN_EN, en, 2, NULL, NULL);
			}
		}
		if (nscan > 0) {
			int i;
			for (i = 0; i < nscan && i < BT_MAX_SCAN; i++) {
				char ab[24];
				addr_str(scans[i].addr, ab, sizeof ab);
				UARTDebugOut("[btscan]: %s type=%d rssi=%d %s\r\n", ab, scans[i].addr_type,
							 scans[i].rssi, scans[i].name);
			}
		} else {
			UARTDebugOut("[btscan]: no advertisers found\r\n");
		}
	}
	publish_dev();
	bond_load();
	snoop_open();
	snoop_flush();
}

static void snoop_open(void) {
	snoop_len = 0;
}

void AuBtUsbReady(const AuBtUsbOps* ops) {
	AA64Thread* thr;
	if (!ops)
		return;
	usb = *ops;
	have_usb = 1;
	identity();
	/* A replacement adapter after AuBtUsbDetach() reuses the parked
	 * bthost thread: it polls have_usb, so updating the ops table and
	 * setting the flag is enough.  Creating a second thread would give
	 * two consumers the same HCI event stream. */
	if (bt_started)
		return;
	thr = AuCreateKthread(bt_thread, AuGetRootPageTable(), "bthost");
	(void)thr;
	bt_started = 1;
}

/* The host controller no longer has a live Bluetooth device: stop routing
 * HCI through the old ops table. The thread stays parked on have_usb. */
void AuBtUsbDetach(void) {
	have_usb = 0;
}

void AuBtInitialize(void) {
	bt_h4_reset(&h4);
#ifdef __TARGET_BOARD_QEMU_VIRT__
	/* Zephyr-style serial bridge first: UART1 <-> -serial unix:/tmp/bt-server-bredr
	 * <-> btproxy <-> BlueZ HCI_USER. If UART1 answers, the real host
	 * adapter owns the stack and the mock is skipped. */
	{
		bt_uart_init_hw();
		if (bt_uart) {
			bt_h4_reset(&h4);
			have_h4uart = 1;
			if (!hci(HCI_RESET, NULL, 0, NULL, NULL)) {
				AA64Thread* thr;
				UARTDebugOut("[bt]: H4 UART1 bridge detected, using host adapter\r\n");
				identity();
				thr = AuCreateKthread(bt_thread, AuGetRootPageTable(), "bthost");
				(void)thr;
				bt_started = 1;
				return;
			}
			have_h4uart = 0;
			bt_h4_reset(&h4);
			UARTDebugOut("[bt]: no H4 UART1 controller, falling back\r\n");
		}
	}
#endif
	{
		int b, d, f;
		for (b = 0; b < 256 && !have_usb; b++)
			for (d = 0; d < 32 && !have_usb; d++)
				for (f = 0; f < 8 && !have_usb; f++) {
					uint64_t addr = AuPCIEGetDevice(0, b, d, f);
					if (!addr)
						continue;
					uint16_t vend = AuPCIERead(addr, PCI_VENDOR_ID, b, d, f);
					uint16_t did = AuPCIERead(addr, PCI_DEVICE_ID, b, d, f);
					if (vend == 0x1AF4 && did == 0x1043)
						AuVirtioBtInitialize(addr, b, d, f);
				}
	}
	if (!have_usb) {
		/* No virtio-serial PCI device: use the mock controller. */
		AuVirtioBtInitialize(0, 0, 0, 0);
	}
	if (!have_usb && !have_h4uart)
		UARTDebugOut("[btscan]: no adapter\r\n");
}

/* H4 byte pipe. A second UART calls this; the console UART is not used. */
void AuBtH4Push(uint8_t byte) {
	uint8_t kind = 0;
	uint16_t olen = 0;
	uint8_t b = byte;
	h4_on = 1;
	if (!bt_h4_feed(&h4, &b, 1, &kind, h4_rx, &olen, sizeof h4_rx))
		return;
	if (kind == H4_EVT)
		on_event(h4_rx, olen);
	else if (kind == H4_ACL)
		on_acl(h4_rx, olen);
}
