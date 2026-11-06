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

#ifndef __WIFI_H__
#define __WIFI_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* virtio-net registers its Ethernet transmit. wifi.dll sends through it. */
void AuWifiPortalSetTx(void (*fn)(void* frame, uint16_t len));
void AuWifiPortalTx(void* frame, uint16_t len);

/* wifi.dll registers this. virtio-net calls it before AuEthernetHandle.
 * Non-zero means the frame was consumed. */
void AuWifiPortalSetRx(int (*fn)(void* frame, int len));
int AuWifiPortalRx(void* frame, int len);

#define WIFI_SSID_MAX 32
#define WIFI_MAX_BSS  4

typedef struct _wifi_bss_info_ {
	uint8_t ssid[WIFI_SSID_MAX];
	uint8_t ssid_len;
	uint8_t bssid[6];
	uint8_t channel;
	int8_t rssi;
	uint8_t rsn;
} WifiBssInfo;

typedef struct _wifi_scan_result_ {
	uint32_t count;
	WifiBssInfo bss[WIFI_MAX_BSS];
} WifiScanResult;

typedef struct _wifi_connect_req_ {
	char ssid[33];
	char pass[64];
} WifiConnectReq;

typedef struct _wifi_status_ {
	char state[16];
	char ssid[33];
	uint8_t bssid[6];
	int32_t link;
	int32_t rsn;
} WifiStatus;

#ifdef __cplusplus
}
#endif

#endif
