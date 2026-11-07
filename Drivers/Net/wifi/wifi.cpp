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
#include <Net/aunet.h>
#include <Net/wifi.h>
#include <Net/ethernet.h>
#include <Net/route.h>
#include <Fs/vfs.h>
#include <Mm/kmalloc.h>
#include <Drivers/uart.h>
#include "crypto.h"

/* IEEE 802.11-2020 data frame, CCMP (12.5.3), and WPA2-PSK (802.11i). */

static const uint8_t k_sta_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x10};
static const uint8_t k_open_bssid[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
static const uint8_t k_wpa_bssid[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};
/* QEMU slirp gateway 10.0.2.2 is 52:55:0a:00:02:02. */
static const uint8_t k_slirp_mac[6] = {0x52, 0x55, 0x0a, 0x00, 0x02, 0x02};
static const uint32_t k_ap_ip = MAKE_IP(192, 168, 50, 1);
static const uint32_t k_ap_mask = MAKE_IP(255, 255, 255, 0);

static AuVFSNode* nic;
static AuNetworkDevice* ndev;
static char g_state[16] = "idle";
static char g_ssid[33];
static uint8_t g_ssid_len;
static uint8_t g_bssid[6];
static int g_rsn;
static int g_keys;
static int g_uplink;
static uint8_t g_tk[16];
static uint8_t g_gtk[16];
static uint8_t g_pmk[32];
static uint16_t g_sta_seq;
static uint16_t g_ap_seq;
static uint64_t g_sta_pn;
static uint64_t g_ap_pn;
static uint64_t g_rx_sta_pn;
static uint64_t g_rx_ap_pn;
static uint32_t g_nonce_ctr;
static uint8_t g_virt_mac[6];
static uint32_t g_virt_ip;
static int g_virt_ready;
static uint8_t g_tx_mpdu[1800];
static uint8_t g_tx_eth[1800];
static uint8_t g_rx_mpdu[1800];
static uint8_t g_rx_eth[1800];
static uint8_t g_reply[1800];

static void set_state(const char* s) {
	int i;
	for (i = 0; i < 15 && s[i]; i++)
		g_state[i] = s[i];
	g_state[i] = 0;
}

static int mac_eq(const uint8_t* a, const uint8_t* b) {
	return memcmp(a, b, 6) == 0;
}

static int ip_eq(const uint8_t* p, uint32_t addr) {
	uint8_t b[4];
	memcpy(b, &addr, 4);
	return memcmp(p, b, 4) == 0;
}

static uint32_t ip_load(const uint8_t* p) {
	uint32_t a;
	memcpy(&a, p, 4);
	return a;
}

static uint32_t sum_bytes(const uint8_t* p, int len) {
	uint32_t s = 0;
	int i;
	for (i = 0; i + 1 < len; i += 2)
		s += ((uint32_t)p[i] << 8) | p[i + 1];
	if (len & 1)
		s += (uint32_t)p[len - 1] << 8;
	return s;
}

static uint16_t csum_fold(uint32_t s) {
	while (s >> 16)
		s = (s & 0xffffu) + (s >> 16);
	return (uint16_t)~s;
}

static void store_be16(uint8_t* p, uint16_t v) {
	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)v;
}

static void fix_l3_l4(uint8_t* ip, int avail) {
	int ihl;
	int total;
	int seglen;
	uint8_t proto;
	uint8_t* l4;
	uint32_t s;

	if (avail < 20)
		return;
	ihl = (ip[0] & 0x0f) * 4;
	if (ihl < 20 || ihl > avail)
		return;
	total = ((int)ip[2] << 8) | ip[3];
	if (total < ihl || total > avail)
		total = avail;
	ip[10] = 0;
	ip[11] = 0;
	store_be16(ip + 10, csum_fold(sum_bytes(ip, ihl)));
	proto = ip[9];
	seglen = total - ihl;
	l4 = ip + ihl;
	if (proto != 6 && proto != 17)
		return;
	if (proto == 17 && seglen >= 8 && l4[6] == 0 && l4[7] == 0)
		return;
	if (proto == 6 && seglen < 18)
		return;
	if (proto == 17 && seglen < 8)
		return;
	if (proto == 6) {
		l4[16] = 0;
		l4[17] = 0;
	} else {
		l4[6] = 0;
		l4[7] = 0;
	}
	s = sum_bytes(ip + 12, 8);
	s += proto;
	s += (uint32_t)seglen;
	s += sum_bytes(l4, seglen);
	if (proto == 6)
		store_be16(l4 + 16, csum_fold(s));
	else {
		uint16_t c = csum_fold(s);
		if (c == 0)
			c = 0xffff;
		store_be16(l4 + 6, c);
	}
}

static int rsn_ie(uint8_t* p) {
	static const uint8_t ie[22] = {
		0x30, 20,
		0x01, 0x00,
		0x00, 0x0f, 0xac, 0x04,
		0x01, 0x00,
		0x00, 0x0f, 0xac, 0x04,
		0x01, 0x00,
		0x00, 0x0f, 0xac, 0x02,
		0x00, 0x00
	};
	memcpy(p, ie, 22);
	return 22;
}

static void put_le16(uint8_t* p, uint16_t v) {
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void ccmp_nonce(uint8_t nonce[13], const uint8_t* ta, uint64_t pn) {
	int i;
	nonce[0] = 0;
	memcpy(nonce + 1, ta, 6);
	for (i = 0; i < 6; i++)
		nonce[7 + i] = (uint8_t)(pn >> (8 * (5 - i)));
}

static void ccmp_aad(uint8_t aad[22], const uint8_t* hdr) {
	memset(aad, 0, 22);
	aad[0] = (uint8_t)(hdr[0] & 0x8f);
	aad[1] = (uint8_t)(hdr[1] & 0xc7);
	memcpy(aad + 2, hdr + 4, 18);
	aad[20] = (uint8_t)(hdr[22] & 0x0f);
	aad[21] = 0;
}

static void write_ccmp_hdr(uint8_t* p, uint64_t pn, int keyid) {
	p[0] = (uint8_t)pn;
	p[1] = (uint8_t)(pn >> 8);
	p[2] = 0;
	p[3] = (uint8_t)((keyid << 6) | 0x20);
	p[4] = (uint8_t)(pn >> 16);
	p[5] = (uint8_t)(pn >> 24);
	p[6] = (uint8_t)(pn >> 32);
	p[7] = (uint8_t)(pn >> 40);
}

static uint64_t read_pn(const uint8_t* p) {
	return (uint64_t)p[0] | ((uint64_t)p[1] << 8) | ((uint64_t)p[4] << 16) |
		   ((uint64_t)p[5] << 24) | ((uint64_t)p[6] << 32) | ((uint64_t)p[7] << 40);
}

static int encap_eth(int from_ap, const uint8_t* eth, int elen, uint8_t* mpdu, int cap) {
	uint8_t body[1600];
	uint8_t nonce[13];
	uint8_t aad[22];
	const uint8_t* a1;
	const uint8_t* a2;
	const uint8_t* a3;
	uint16_t* seq;
	uint64_t pn;
	int blen;
	int protect;

	if (!eth || elen < 14 || elen > 1514 || !mpdu)
		return -1;
	blen = 8 + (elen - 14);
	if (blen > (int)sizeof(body))
		return -1;
	body[0] = 0xaa;
	body[1] = 0xaa;
	body[2] = 0x03;
	body[3] = 0;
	body[4] = 0;
	body[5] = 0;
	body[6] = eth[12];
	body[7] = eth[13];
	memcpy(body + 8, eth + 14, (size_t)(elen - 14));
	if (!from_ap) {
		a1 = g_bssid;
		a2 = k_sta_mac;
		a3 = eth;
		seq = &g_sta_seq;
	} else {
		a1 = eth;
		a2 = g_bssid;
		a3 = eth + 6;
		seq = &g_ap_seq;
	}
	protect = (g_keys && g_rsn) ? 1 : 0;
	if (24 + blen + (protect ? 16 : 0) > cap)
		return -1;
	mpdu[0] = 0x08;
	mpdu[1] = (uint8_t)(from_ap ? 0x02 : 0x01);
	if (protect)
		mpdu[1] = (uint8_t)(mpdu[1] | 0x40);
	mpdu[2] = 0;
	mpdu[3] = 0;
	memcpy(mpdu + 4, a1, 6);
	memcpy(mpdu + 10, a2, 6);
	memcpy(mpdu + 16, a3, 6);
	put_le16(mpdu + 22, (uint16_t)((*seq & 0x0fff) << 4));
	*seq = (uint16_t)((*seq + 1) & 0x0fff);
	if (!protect) {
		memcpy(mpdu + 24, body, (size_t)blen);
		return 24 + blen;
	}
	pn = from_ap ? ++g_ap_pn : ++g_sta_pn;
	write_ccmp_hdr(mpdu + 24, pn, 0);
	ccmp_nonce(nonce, a2, pn);
	ccmp_aad(aad, mpdu);
	if (wifi_ccm_encrypt(g_tk, nonce, aad, 22, body, blen, mpdu + 32) < 0)
		return -1;
	return 32 + blen + 8;
}

static int decap(const uint8_t* mpdu, int mlen, uint8_t* eth, int cap) {
	int is_prot;
	int from_ap;
	int hdr = 24;
	int blen;
	const uint8_t* body;
	uint8_t plain[1600];
	uint8_t nonce[13];
	uint8_t aad[22];
	uint64_t pn;
	uint64_t* last;

	if (!mpdu || mlen < 32 || !eth)
		return -1;
	if ((mpdu[0] & 0x0c) != 0x08)
		return -1;
	is_prot = (mpdu[1] & 0x40) ? 1 : 0;
	from_ap = (mpdu[1] & 0x02) ? 1 : 0;
	if (is_prot) {
		int keyid;
		int ctlen;
		if (mlen < 24 + 16)
			return -1;
		keyid = (mpdu[hdr + 3] >> 6) & 3;
		pn = read_pn(mpdu + hdr);
		ctlen = mlen - hdr - 8;
		if (ctlen < 8 || ctlen > (int)sizeof(plain) + 8)
			return -1;
		if (!g_keys || keyid != 0)
			return -1;
		ccmp_nonce(nonce, mpdu + 10, pn);
		ccmp_aad(aad, mpdu);
		if (wifi_ccm_decrypt(g_tk, nonce, aad, 22, mpdu + 32, ctlen, plain) < 0)
			return -1;
		last = from_ap ? &g_rx_ap_pn : &g_rx_sta_pn;
		if (pn == 0 || pn <= *last)
			return -1;
		*last = pn;
		body = plain;
		blen = ctlen - 8;
	} else {
		body = mpdu + 24;
		blen = mlen - 24;
	}
	if (blen < 8 || 6 + blen > cap)
		return -1;
	if (body[0] != 0xaa || body[1] != 0xaa || body[2] != 0x03)
		return -1;
	if (from_ap) {
		memcpy(eth, mpdu + 4, 6);
		memcpy(eth + 6, mpdu + 16, 6);
	} else {
		memcpy(eth, mpdu + 16, 6);
		memcpy(eth + 6, mpdu + 10, 6);
	}
	eth[12] = body[6];
	eth[13] = body[7];
	memcpy(eth + 14, body + 8, (size_t)(blen - 8));
	return 6 + blen;
}

static void to_station(const uint8_t* eth, int len);

static void ap_on_eth(const uint8_t* eth, int len) {
	uint8_t* rep = g_reply;
	const uint8_t* ip;
	int ihl;
	int total;
	int icmp_len;

	if (len < 14 || len > 1600)
		return;
	if (eth[12] == 0x08 && eth[13] == 0x06 && len >= 42) {
		const uint8_t* arp = eth + 14;
		uint16_t op = (uint16_t)((arp[6] << 8) | arp[7]);
		if (op == 1 && ip_eq(arp + 24, k_ap_ip)) {
			memset(rep, 0, 42);
			memcpy(rep, eth + 6, 6);
			memcpy(rep + 6, g_bssid, 6);
			rep[12] = 0x08;
			rep[13] = 0x06;
			rep[14] = 0;
			rep[15] = 1;
			rep[16] = 0x08;
			rep[17] = 0x00;
			rep[18] = 6;
			rep[19] = 4;
			rep[20] = 0;
			rep[21] = 2;
			memcpy(rep + 22, g_bssid, 6);
			memcpy(rep + 28, &k_ap_ip, 4);
			memcpy(rep + 32, arp + 8, 6);
			memcpy(rep + 38, arp + 14, 4);
			to_station(rep, 42);
		}
		return;
	}
	if (!(eth[12] == 0x08 && eth[13] == 0x00) || len < 34)
		return;
	ip = eth + 14;
	ihl = (ip[0] & 0x0f) * 4;
	total = ((int)ip[2] << 8) | ip[3];
	if (ihl < 20 || total < ihl || 14 + total > len)
		return;
	if (ip_eq(ip + 16, k_ap_ip) && ip[9] == 1 && total >= ihl + 8 && ip[ihl] == 8) {
		icmp_len = total - ihl;
		if (14 + total > (int)sizeof(g_reply))
			return;
		memset(rep, 0, (size_t)(14 + total));
		memcpy(rep, eth + 6, 6);
		memcpy(rep + 6, g_bssid, 6);
		rep[12] = 0x08;
		rep[13] = 0x00;
		memcpy(rep + 14, ip, (size_t)total);
		memcpy(rep + 26, ip + 16, 4);
		memcpy(rep + 30, ip + 12, 4);
		rep[22] = 64;
		rep[14 + ihl] = 0;
		rep[14 + ihl + 2] = 0;
		rep[14 + ihl + 3] = 0;
		store_be16(rep + 14 + ihl + 2, csum_fold(sum_bytes(rep + 14 + ihl, icmp_len)));
		fix_l3_l4(rep + 14, total);
		to_station(rep, 14 + total);
		return;
	}
	if (!g_uplink || !g_virt_ready)
		return;
	if ((ip_load(ip + 16) & k_ap_mask) == (k_ap_ip & k_ap_mask))
		return;
	if (!ip_eq(ip + 12, ndev->ipv4addr))
		return;
	if (len > (int)sizeof(g_reply))
		return;
	memcpy(rep, eth, (size_t)len);
	memcpy(rep, k_slirp_mac, 6);
	memcpy(rep + 6, g_virt_mac, 6);
	memcpy(rep + 26, &g_virt_ip, 4);
	fix_l3_l4(rep + 14, len - 14);
	AuWifiPortalTx(rep, (uint16_t)len);
}

static void to_station(const uint8_t* eth, int len) {
	int m;
	int n;
	uint8_t* copy;

	m = encap_eth(1, eth, len, g_rx_mpdu, (int)sizeof(g_rx_mpdu));
	if (m < 0)
		return;
	n = decap(g_rx_mpdu, m, g_rx_eth, (int)sizeof(g_rx_eth));
	if (n < 0)
		return;
	copy = (uint8_t*)kmalloc((unsigned)n);
	if (!copy)
		return;
	memcpy(copy, g_rx_eth, (size_t)n);
	AuEthernetHandle(copy, n, nic);
	kfree(copy);
}

static size_t wifi_write(AuVFSNode* node, AuVFSNode* file, uint64_t* buffer, uint32_t len) {
	int m;
	int n;
	(void)node;
	(void)file;
	if (!ndev || !ndev->linkStatus || (g_rsn && !g_keys))
		return len;
	if (!buffer || len < 14 || len > sizeof(g_tx_eth))
		return len;
	if (strcmp(g_state, "run") != 0)
		return len;
	m = encap_eth(0, (const uint8_t*)buffer, (int)len, g_tx_mpdu, (int)sizeof(g_tx_mpdu));
	if (m < 0)
		return len;
	n = decap(g_tx_mpdu, m, g_tx_eth, (int)sizeof(g_tx_eth));
	if (n > 0)
		ap_on_eth(g_tx_eth, n);
	return len;
}

static void lab_nonce(uint8_t* out, int len) {
	int i;
	g_nonce_ctr++;
	for (i = 0; i < len; i++) {
		out[i] = (uint8_t)(k_sta_mac[i % 6] ^ g_bssid[i % 6] ^
						   (uint8_t)(g_nonce_ctr >> ((i % 4) * 8)) ^ (uint8_t)(i * 17));
	}
}

static int eapol_build(uint8_t* f, uint16_t info, uint64_t replay, const uint8_t nonce[32],
					   const uint8_t* data, int data_len, const uint8_t kck[16]) {
	int body;
	int total;
	uint8_t mic[20];
	int i;

	if (data_len < 0 || data_len > 80)
		return -1;
	memset(f, 0, (size_t)(99 + data_len));
	f[0] = 1;
	f[1] = 3;
	f[4] = 2;
	store_be16(f + 5, info);
	store_be16(f + 7, 16);
	for (i = 0; i < 8; i++)
		f[9 + i] = (uint8_t)(replay >> (8 * (7 - i)));
	if (nonce)
		memcpy(f + 17, nonce, 32);
	if (data_len && data)
		memcpy(f + 99, data, (size_t)data_len);
	store_be16(f + 97, (uint16_t)data_len);
	body = 95 + data_len;
	store_be16(f + 2, (uint16_t)body);
	total = 4 + body;
	if (kck) {
		wifi_hmac_sha1(kck, 16, f, total, mic);
		memcpy(f + 81, mic, 16);
	}
	return total;
}

static int eapol_mic_ok(const uint8_t* f, int len, const uint8_t kck[16]) {
	uint8_t tmp[200];
	uint8_t mic[20];
	uint8_t got[16];
	if (len < 99 || len > (int)sizeof(tmp))
		return 0;
	memcpy(tmp, f, (size_t)len);
	memcpy(got, tmp + 81, 16);
	memset(tmp + 81, 0, 16);
	wifi_hmac_sha1(kck, 16, tmp, len, mic);
	return memcmp(mic, got, 16) == 0;
}

static void derive_ptk(const uint8_t pmk[32], const uint8_t anonce[32], const uint8_t snonce[32],
					  uint8_t ptk[48]) {
	uint8_t data[76];
	const uint8_t* min_m;
	const uint8_t* max_m;
	const uint8_t* min_n;
	const uint8_t* max_n;

	if (memcmp(g_bssid, k_sta_mac, 6) < 0) {
		min_m = g_bssid;
		max_m = k_sta_mac;
	} else {
		min_m = k_sta_mac;
		max_m = g_bssid;
	}
	if (memcmp(anonce, snonce, 32) < 0) {
		min_n = anonce;
		max_n = snonce;
	} else {
		min_n = snonce;
		max_n = anonce;
	}
	memcpy(data, min_m, 6);
	memcpy(data + 6, max_m, 6);
	memcpy(data + 12, min_n, 32);
	memcpy(data + 44, max_n, 32);
	wifi_sha1_prf(pmk, 32, "Pairwise key expansion", data, 76, ptk, 48);
}

static int pipe_eapol(int from_ap, const uint8_t* eapol, int elen, uint8_t* out, int cap) {
	uint8_t eth[220];
	uint8_t mpdu[400];
	uint8_t back[220];
	int saved_keys;
	int m;
	int n;

	if (elen < 0 || 14 + elen > (int)sizeof(eth))
		return -1;
	memset(eth, 0, sizeof(eth));
	if (from_ap) {
		memcpy(eth, k_sta_mac, 6);
		memcpy(eth + 6, g_bssid, 6);
	} else {
		memcpy(eth, g_bssid, 6);
		memcpy(eth + 6, k_sta_mac, 6);
	}
	eth[12] = 0x88;
	eth[13] = 0x8e;
	memcpy(eth + 14, eapol, (size_t)elen);
	/* EAPOL-Key is unprotected until the link is up. */
	saved_keys = g_keys;
	g_keys = 0;
	m = encap_eth(from_ap, eth, 14 + elen, mpdu, (int)sizeof(mpdu));
	g_keys = saved_keys;
	if (m < 0)
		return -1;
	n = decap(mpdu, m, back, (int)sizeof(back));
	if (n < 14 || n - 14 > cap)
		return -1;
	if (back[12] != 0x88 || back[13] != 0x8e)
		return -1;
	if (memcmp(back + 14, eapol, (size_t)elen) != 0)
		return -1;
	memcpy(out, back + 14, (size_t)(n - 14));
	return n - 14;
}

static int handshake(const char* pass) {
	uint8_t anonce[32];
	uint8_t snonce[32];
	uint8_t ap_pmk[32];
	uint8_t sta_ptk[48];
	uint8_t ap_ptk[48];
	uint8_t msg[180];
	uint8_t got[180];
	uint8_t kde[24];
	uint8_t wrapped[40];
	uint8_t gtk_plain[32];
	int n;
	int wlen;
	int glen;
	uint16_t info;
	static const char k_pass[] = "xenevaos";

	/* Supplicant PSK comes from connect. The AP checks it against its own. */
	wifi_pbkdf2_sha1((const uint8_t*)pass, (int)strlen(pass), (const uint8_t*)g_ssid, g_ssid_len,
					 4096, g_pmk, 32);
	wifi_pbkdf2_sha1((const uint8_t*)k_pass, (int)sizeof(k_pass) - 1, (const uint8_t*)g_ssid,
					 g_ssid_len, 4096, ap_pmk, 32);
	lab_nonce(anonce, 32);
	lab_nonce(snonce, 32);
	lab_nonce(g_gtk, 16);
	derive_ptk(g_pmk, anonce, snonce, sta_ptk);
	derive_ptk(ap_pmk, anonce, snonce, ap_ptk);
	n = eapol_build(msg, 0x008a, 1, anonce, 0, 0, 0);
	if (n < 0 || pipe_eapol(1, msg, n, got, (int)sizeof(got)) != n)
		return -1;
	n = rsn_ie(kde);
	n = eapol_build(msg, 0x010a, 1, snonce, kde, n, sta_ptk);
	if (n < 0 || pipe_eapol(0, msg, n, got, (int)sizeof(got)) != n)
		return -1;
	info = (uint16_t)((got[5] << 8) | got[6]);
	if ((info & 0x0100) == 0 || !eapol_mic_ok(got, n, ap_ptk))
		return -1;
	kde[0] = 0xdd;
	kde[1] = 22;
	kde[2] = 0x00;
	kde[3] = 0x0f;
	kde[4] = 0xac;
	kde[5] = 0x01;
	kde[6] = 0x01;
	kde[7] = 0x00;
	memcpy(kde + 8, g_gtk, 16);
	wlen = wifi_aes_wrap(ap_ptk + 16, kde, 24, wrapped);
	if (wlen < 0)
		return -1;
	n = eapol_build(msg, 0x13ca, 2, anonce, wrapped, wlen, ap_ptk);
	if (n < 0 || pipe_eapol(1, msg, n, got, (int)sizeof(got)) != n)
		return -1;
	if (!eapol_mic_ok(got, n, sta_ptk))
		return -1;
	glen = (int)((got[97] << 8) | got[98]);
	if (99 + glen > n)
		return -1;
	if (wifi_aes_unwrap(sta_ptk + 16, got + 99, glen, gtk_plain) < 0)
		return -1;
	if (gtk_plain[0] != 0xdd || memcmp(gtk_plain + 8, g_gtk, 16) != 0)
		return -1;
	n = eapol_build(msg, 0x030a, 2, 0, 0, 0, sta_ptk);
	if (n < 0 || pipe_eapol(0, msg, n, got, (int)sizeof(got)) != n)
		return -1;
	if (!eapol_mic_ok(got, n, ap_ptk))
		return -1;
	memcpy(g_tk, sta_ptk + 32, 16);
	g_sta_pn = 0;
	g_ap_pn = 0;
	g_rx_sta_pn = 0;
	g_rx_ap_pn = 0;
	return 0;
}

static int mgmt_auth(void) {
	uint8_t req[30];
	uint16_t seq;
	memset(req, 0, sizeof(req));
	req[0] = 0xb0;
	memcpy(req + 4, g_bssid, 6);
	memcpy(req + 10, k_sta_mac, 6);
	memcpy(req + 16, g_bssid, 6);
	put_le16(req + 24, 0);
	put_le16(req + 26, 1);
	put_le16(req + 28, 0);
	seq = (uint16_t)(req[26] | (req[27] << 8));
	if (seq != 1)
		return -1;
	put_le16(req + 26, 2);
	put_le16(req + 28, 0);
	seq = (uint16_t)(req[26] | (req[27] << 8));
	if (seq != 2)
		return -1;
	if ((req[28] | req[29]) != 0)
		return -1;
	return 0;
}

static int mgmt_assoc(void) {
	uint8_t req[96];
	uint8_t* p;
	int has_rsn = 0;
	int ssid_ok = 0;
	uint8_t* end;

	memset(req, 0, sizeof(req));
	req[0] = 0x00;
	memcpy(req + 4, g_bssid, 6);
	memcpy(req + 10, k_sta_mac, 6);
	memcpy(req + 16, g_bssid, 6);
	put_le16(req + 24, (uint16_t)(g_rsn ? 0x0011 : 0x0001));
	put_le16(req + 26, 10);
	p = req + 28;
	*p++ = 0;
	*p++ = g_ssid_len;
	memcpy(p, g_ssid, g_ssid_len);
	p += g_ssid_len;
	*p++ = 1;
	*p++ = 4;
	*p++ = 0x82;
	*p++ = 0x84;
	*p++ = 0x8b;
	*p++ = 0x96;
	if (g_rsn)
		p += rsn_ie(p);
	end = p;
	p = req + 28;
	while (p + 2 <= end) {
		uint8_t id = p[0];
		uint8_t ln = p[1];
		if (p + 2 + ln > end)
			return -1;
		if (id == 0 && ln == g_ssid_len && memcmp(p + 2, g_ssid, ln) == 0)
			ssid_ok = 1;
		if (id == 48)
			has_rsn = 1;
		p += 2 + ln;
	}
	if (!ssid_ok || has_rsn != g_rsn)
		return -1;
	req[0] = 0x10;
	put_le16(req + 24, 0x0011);
	put_le16(req + 26, 0);
	put_le16(req + 28, 0xc001);
	if ((req[26] | req[27]) != 0)
		return -1;
	return 0;
}

static void drop_link(void) {
	g_keys = 0;
	g_rsn = 0;
	g_ssid_len = 0;
	g_ssid[0] = 0;
	memset(g_bssid, 0, 6);
	memset(g_tk, 0, 16);
	memset(g_gtk, 0, 16);
	memset(g_pmk, 0, 32);
	if (ndev)
		ndev->linkStatus = 0;
	set_state("idle");
}

static int load_virt(void) {
	AuVFSNode* v;
	AuNetworkDevice* vd;
	if (g_virt_ready)
		return 0;
	v = AuGetNetworkAdapter("virtio-net");
	if (!v || !v->device)
		return -1;
	vd = (AuNetworkDevice*)v->device;
	memcpy(g_virt_mac, vd->mac, 6);
	g_virt_ip = vd->ipv4addr;
	g_virt_ready = 1;
	return 0;
}

static int uplink_set(int on) {
	if (!on) {
		g_uplink = 0;
		AuRouteSetFlag4("wlan0", 0, 0, 0);
		AuRouteSetFlag4("virtio-net", 0, 0, 1);
		UARTDebugOut("[wifi]: uplink off\r\n");
		return 0;
	}
	if (!ndev || !ndev->linkStatus || !ndev->ipv4addr)
		return 1;
	if (load_virt() != 0)
		return 1;
	ndev->ipv4gateway = k_ap_ip;
	AuNetAddDefaultRoute4(nic, "wlan0");
	if (AuRouteSetFlag4("virtio-net", 0, 0, 0) != 0)
		UARTDebugOut("[wifi]: virtio-net default route not found\r\n");
	g_uplink = 1;
	UARTDebugOut("[wifi]: uplink on\r\n");
	return 0;
}

static const struct {
	const char* ssid;
	const uint8_t* bssid;
	uint8_t channel;
	int8_t rssi;
	uint8_t rsn;
} k_table[2] = {
	{"xeneva-open", k_open_bssid, 6, -42, 0},
	{"xeneva", k_wpa_bssid, 11, -48, 1},
};

static void fill_scan(WifiScanResult* out) {
	int i;
	memset(out, 0, sizeof(*out));
	out->count = 2;
	for (i = 0; i < 2; i++) {
		int n = 0;
		while (k_table[i].ssid[n] && n < WIFI_SSID_MAX) {
			out->bss[i].ssid[n] = (uint8_t)k_table[i].ssid[n];
			n++;
		}
		out->bss[i].ssid_len = (uint8_t)n;
		memcpy(out->bss[i].bssid, k_table[i].bssid, 6);
		out->bss[i].channel = k_table[i].channel;
		out->bss[i].rssi = k_table[i].rssi;
		out->bss[i].rsn = k_table[i].rsn;
	}
}

static int do_connect(const WifiConnectReq* req) {
	int i;
	int n;
	const char* pass;

	if (!req)
		return 1;
	uplink_set(0);
	drop_link();
	n = 0;
	while (req->ssid[n] && n < 32)
		n++;
	if (n == 0)
		return 1;
	for (i = 0; i < 2; i++) {
		int m = 0;
		while (k_table[i].ssid[m])
			m++;
		if (m == n && memcmp(k_table[i].ssid, req->ssid, (size_t)n) == 0)
			break;
	}
	if (i == 2)
		return 1;
	memcpy(g_ssid, req->ssid, (size_t)n);
	g_ssid[n] = 0;
	g_ssid_len = (uint8_t)n;
	memcpy(g_bssid, k_table[i].bssid, 6);
	g_rsn = k_table[i].rsn;
	pass = req->pass;
	if (g_rsn && (pass[0] == 0 || strlen(pass) > 63)) {
		drop_link();
		return 1;
	}
	set_state("auth");
	if (mgmt_auth() != 0) {
		drop_link();
		return 1;
	}
	set_state("assoc");
	if (mgmt_assoc() != 0) {
		drop_link();
		return 1;
	}
	if (g_rsn) {
		set_state("handshake");
		if (handshake(pass) != 0) {
			UARTDebugOut("[wifi]: handshake failed\r\n");
			drop_link();
			return 1;
		}
		g_keys = 1;
	} else {
		g_keys = 1;
	}
	ndev->linkStatus = 1;
	set_state("run");
	UARTDebugOut("[wifi]: associated ");
	UARTDebugOut(g_ssid);
	UARTDebugOut("\r\n");
	return 0;
}

static int wifi_ioctl(AuVFSNode* file, int code, void* arg) {
	int on;
	(void)file;
	if (!ndev)
		return 1;
	switch (code) {
	case AUNET_GET_HARDWARE_ADDRESS:
		if (!arg)
			return 1;
		memcpy(arg, ndev->mac, 6);
		return 0;
	case AUNET_GET_IPV4_ADDRESS:
		if (!arg)
			return 1;
		memcpy(arg, &ndev->ipv4addr, 4);
		return 0;
	case AUNET_SET_IPV4_ADDRESS:
		if (!arg)
			return 1;
		memcpy(&ndev->ipv4addr, arg, 4);
		if (nic)
			AuNetAddConnectedRoute4(nic, "wlan0");
		return 0;
	case AUNET_GET_GATEWAY_ADDRESS:
		if (!arg)
			return 1;
		memcpy(arg, &ndev->ipv4gateway, 4);
		return 0;
	case AUNET_SET_GATEWAY_ADDRESS:
		if (!arg)
			return 1;
		memcpy(&ndev->ipv4gateway, arg, 4);
		if (nic)
			AuNetAddDefaultRoute4(nic, "wlan0");
		return 0;
	case AUNET_GET_SUBNET_MASK:
		if (!arg)
			return 1;
		memcpy(arg, &ndev->ipv4subnet, 4);
		return 0;
	case AUNET_SET_SUBNET_MASK:
		if (!arg)
			return 1;
		memcpy(&ndev->ipv4subnet, arg, 4);
		if (nic)
			AuNetAddConnectedRoute4(nic, "wlan0");
		return 0;
	case AUNET_GET_LINK_STATUS:
		if (!arg)
			return 1;
		memcpy(arg, &ndev->linkStatus, sizeof(ndev->linkStatus));
		return 0;
	case WIFI_SCAN:
		return 0;
	case WIFI_GET_SCAN:
		if (!arg)
			return 1;
		fill_scan((WifiScanResult*)arg);
		return 0;
	case WIFI_CONNECT:
		return do_connect((const WifiConnectReq*)arg);
	case WIFI_DISCONNECT:
		uplink_set(0);
		drop_link();
		UARTDebugOut("[wifi]: disconnected\r\n");
		return 0;
	case WIFI_GET_STATUS: {
		WifiStatus* st;
		if (!arg)
			return 1;
		st = (WifiStatus*)arg;
		memset(st, 0, sizeof(*st));
		memcpy(st->state, g_state, sizeof(st->state));
		memcpy(st->ssid, g_ssid, sizeof(st->ssid));
		memcpy(st->bssid, g_bssid, 6);
		st->link = ndev->linkStatus;
		st->rsn = g_rsn;
		return 0;
	}
	case WIFI_UPLINK:
		if (!arg)
			return 1;
		memcpy(&on, arg, sizeof(on));
		return uplink_set(on ? 1 : 0);
	default:
		return 1;
	}
}

extern "C" int AuWifiDllRx(void* frame, int len) {
	uint8_t* eth;
	int n;
	uint8_t* copy;
	int m;

	if (!g_uplink || !g_virt_ready || !frame || len < 34 || len > (int)sizeof(g_reply))
		return 0;
	eth = (uint8_t*)frame;
	if (!mac_eq(eth, g_virt_mac))
		return 0;
	if (!(eth[12] == 0x08 && eth[13] == 0x00))
		return 0;
	if (!ip_eq(eth + 30, g_virt_ip))
		return 0;
	memcpy(g_reply, eth, (size_t)len);
	memcpy(g_reply, k_sta_mac, 6);
	memcpy(g_reply + 30, &ndev->ipv4addr, 4);
	fix_l3_l4(g_reply + 14, len - 14);
	m = encap_eth(1, g_reply, len, g_rx_mpdu, (int)sizeof(g_rx_mpdu));
	if (m < 0)
		return 0;
	n = decap(g_rx_mpdu, m, g_rx_eth, (int)sizeof(g_rx_eth));
	if (n < 0)
		return 0;
	copy = (uint8_t*)kmalloc((unsigned)n);
	if (!copy)
		return 0;
	memcpy(copy, g_rx_eth, (size_t)n);
	AuEthernetHandle(copy, n, nic);
	kfree(copy);
	return 1;
}

AU_EXTERN AU_EXPORT int AuDriverUnload(void) {
	return 0;
}

AU_EXTERN AU_EXPORT int AuDriverMain(void) {
	nic = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	ndev = (AuNetworkDevice*)kmalloc(sizeof(AuNetworkDevice));
	if (!nic || !ndev)
		return 1;
	memset(nic, 0, sizeof(AuVFSNode));
	memset(ndev, 0, sizeof(AuNetworkDevice));
	memcpy(ndev->mac, k_sta_mac, 6);
	ndev->type = NETDEV_TYPE_802_11;
	ndev->linkStatus = 0;
	strcpy(nic->filename, "wlan0");
	nic->flags = FS_FLAG_DEVICE;
	nic->write = wifi_write;
	nic->iocontrol = wifi_ioctl;
	nic->device = ndev;
	AuAddNetAdapter(nic, "wlan0");
	AuWifiPortalSetRx(AuWifiDllRx);
	set_state("idle");
	UARTDebugOut("[wifi]: wlan0 station 02:00:00:00:00:10\r\n");
	return 0;
}
