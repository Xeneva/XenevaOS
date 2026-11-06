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

#ifndef __BT_H__
#define __BT_H__

#include <stdint.h>

#define BT_GET_INFO  0x4201
#define BT_SCAN      0x4202
#define BT_CONNECT   0x4203
#define BT_DISCONNECT 0x4204
#define BT_READ_NAME 0x4205
#define BT_PAIR      0x4206
#define BT_PASSKEY   0x4207
#define BT_CONFIRM   0x4208
#define BT_AUDIO     0x4209
#define BT_A2DP      0x420A
#define BT_FORGET    0x420B

#define BT_OK            0
#define BT_ERR           1
#define BT_NEED_CONFIRM  2
#define BT_NO_AUDIO      3

#define BT_MAX_SCAN  64
/* Classic inquiry result. LE address types remain 0 (public) and 1 (random). */
#define BT_ADDR_BREDR 2
#define BT_NAME_LEN  32

typedef struct _bt_scan_ent_ {
	uint8_t addr[6];
	uint8_t addr_type;
	int8_t rssi;
	char name[BT_NAME_LEN];
} BtScanEnt;

typedef struct _bt_info_ {
	uint8_t bd_addr[6];
	uint8_t hci_ver;
	uint16_t acl_mtu;
	uint16_t iso_mtu;
	uint8_t iso;
	uint8_t connected;
	uint8_t encrypted;
	uint8_t peer[6];
	uint8_t peer_type;
	char peer_name[64];
	char msg[80];
	uint32_t passkey;
	int status;
	int nscan;
	int card_id;
	BtScanEnt scan[BT_MAX_SCAN];
} BtInfo;

typedef struct _au_bt_usb_ops_ {
	void* ctx;
	int (*control)(void* ctx, uint8_t bm, uint8_t req, uint16_t val, uint16_t idx, uint8_t* data,
				   uint16_t len, int data_in);
	int (*intr)(void* ctx, uint8_t* data, uint16_t cap);
	int (*bulk_out)(void* ctx, const uint8_t* data, uint16_t len);
	int (*bulk_in)(void* ctx, uint8_t* data, uint16_t cap);
	int (*iso_out)(void* ctx, const uint8_t* data, uint16_t len);
	int has_iso;
} AuBtUsbOps;

extern void AuBtUsbReady(const AuBtUsbOps* ops);
extern void AuBtUsbDetach(void);
extern void AuVirtioBtInitialize(uint64_t device, int bus, int dev, int func);

#endif
