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

#include "crypto.h"
#include <string.h>

static uint32_t rotl32(uint32_t x, int n) {
	return (x << n) | (x >> (32 - n));
}

static void sha1_block(uint32_t st[5], const uint8_t blk[64]) {
	uint32_t w[80];
	uint32_t a, b, c, d, e;
	int i;

	for (i = 0; i < 16; i++) {
		w[i] = ((uint32_t)blk[i * 4] << 24) | ((uint32_t)blk[i * 4 + 1] << 16) |
			   ((uint32_t)blk[i * 4 + 2] << 8) | blk[i * 4 + 3];
	}
	for (i = 16; i < 80; i++)
		w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
	a = st[0];
	b = st[1];
	c = st[2];
	d = st[3];
	e = st[4];
	for (i = 0; i < 80; i++) {
		uint32_t f, k, t;
		if (i < 20) {
			f = (b & c) | ((~b) & d);
			k = 0x5A827999u;
		} else if (i < 40) {
			f = b ^ c ^ d;
			k = 0x6ED9EBA1u;
		} else if (i < 60) {
			f = (b & c) | (b & d) | (c & d);
			k = 0x8F1BBCDCu;
		} else {
			f = b ^ c ^ d;
			k = 0xCA62C1D6u;
		}
		t = rotl32(a, 5) + f + e + k + w[i];
		e = d;
		d = c;
		c = rotl32(b, 30);
		b = a;
		a = t;
	}
	st[0] += a;
	st[1] += b;
	st[2] += c;
	st[3] += d;
	st[4] += e;
}

void wifi_sha1(const uint8_t* msg, int len, uint8_t out[20]) {
	uint32_t st[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
	uint8_t blk[64];
	uint64_t bits;
	int off = 0;
	int i;

	if (len < 0)
		len = 0;
	while (len - off >= 64) {
		sha1_block(st, msg + off);
		off += 64;
	}
	memset(blk, 0, 64);
	if (len > off)
		memcpy(blk, msg + off, (size_t)(len - off));
	blk[len - off] = 0x80;
	if (len - off >= 56) {
		sha1_block(st, blk);
		memset(blk, 0, 64);
	}
	bits = (uint64_t)len * 8u;
	for (i = 0; i < 8; i++)
		blk[63 - i] = (uint8_t)(bits >> (8 * i));
	sha1_block(st, blk);
	for (i = 0; i < 5; i++) {
		out[i * 4] = (uint8_t)(st[i] >> 24);
		out[i * 4 + 1] = (uint8_t)(st[i] >> 16);
		out[i * 4 + 2] = (uint8_t)(st[i] >> 8);
		out[i * 4 + 3] = (uint8_t)st[i];
	}
}

void wifi_hmac_sha1(const uint8_t* key, int key_len, const uint8_t* msg, int msg_len, uint8_t out[20]) {
	uint8_t k[64];
	uint8_t ipad[64];
	uint8_t opad[64];
	uint8_t inner[20];
	uint8_t buf[256];
	uint8_t* cat;
	int i;
	int use_heap = 0;

	memset(k, 0, 64);
	if (key_len > 64) {
		wifi_sha1(key, key_len, k);
	} else if (key_len > 0) {
		memcpy(k, key, (size_t)key_len);
	}
	for (i = 0; i < 64; i++) {
		ipad[i] = (uint8_t)(k[i] ^ 0x36);
		opad[i] = (uint8_t)(k[i] ^ 0x5c);
	}
	if (msg_len < 0)
		msg_len = 0;
	if (64 + msg_len <= (int)sizeof(buf)) {
		cat = buf;
	} else {
		/* Caller keeps EAPOL and PRF inputs under a few hundred bytes. */
		cat = buf;
		if (msg_len > (int)sizeof(buf) - 64)
			msg_len = (int)sizeof(buf) - 64;
		use_heap = 0;
		(void)use_heap;
	}
	memcpy(cat, ipad, 64);
	if (msg_len && msg)
		memcpy(cat + 64, msg, (size_t)msg_len);
	wifi_sha1(cat, 64 + msg_len, inner);
	memcpy(cat, opad, 64);
	memcpy(cat + 64, inner, 20);
	wifi_sha1(cat, 84, out);
}

void wifi_pbkdf2_sha1(const uint8_t* pass, int pass_len, const uint8_t* salt, int salt_len,
					  int rounds, uint8_t* out, int out_len) {
	uint8_t u[20];
	uint8_t t[20];
	uint8_t block[128];
	int produced = 0;
	uint32_t counter = 1;

	if (!out || out_len <= 0 || rounds <= 0)
		return;
	if (salt_len < 0)
		salt_len = 0;
	if (salt_len > (int)sizeof(block) - 4)
		salt_len = (int)sizeof(block) - 4;
	while (produced < out_len) {
		int i, r, n;
		if (salt_len && salt)
			memcpy(block, salt, (size_t)salt_len);
		block[salt_len] = (uint8_t)(counter >> 24);
		block[salt_len + 1] = (uint8_t)(counter >> 16);
		block[salt_len + 2] = (uint8_t)(counter >> 8);
		block[salt_len + 3] = (uint8_t)counter;
		wifi_hmac_sha1(pass, pass_len, block, salt_len + 4, u);
		memcpy(t, u, 20);
		for (r = 1; r < rounds; r++) {
			wifi_hmac_sha1(pass, pass_len, u, 20, u);
			for (i = 0; i < 20; i++)
				t[i] ^= u[i];
		}
		n = out_len - produced;
		if (n > 20)
			n = 20;
		memcpy(out + produced, t, (size_t)n);
		produced += n;
		counter++;
	}
}

void wifi_sha1_prf(const uint8_t* key, int key_len, const char* label,
				   const uint8_t* data, int data_len, uint8_t* out, int out_len) {
	uint8_t msg[160];
	int label_len;
	int produced = 0;
	uint8_t counter = 0;

	if (!label)
		label = "";
	label_len = 0;
	while (label[label_len])
		label_len++;
	label_len++; /* IEEE 802.11i PRF includes the trailing NUL. */
	if (data_len < 0)
		data_len = 0;
	while (produced < out_len) {
		uint8_t mac[20];
		int n;
		int mlen = 0;
		if (label_len + data_len + 1 > (int)sizeof(msg))
			return;
		memcpy(msg, label, (size_t)label_len);
		mlen = label_len;
		if (data_len && data) {
			memcpy(msg + mlen, data, (size_t)data_len);
			mlen += data_len;
		}
		msg[mlen++] = counter;
		wifi_hmac_sha1(key, key_len, msg, mlen, mac);
		n = out_len - produced;
		if (n > 20)
			n = 20;
		memcpy(out + produced, mac, (size_t)n);
		produced += n;
		counter++;
	}
}

static uint8_t xtime(uint8_t x) {
	return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1b : 0));
}

static uint8_t gf_mul(uint8_t a, uint8_t b) {
	uint8_t p = 0;
	int i;
	for (i = 0; i < 8; i++) {
		if (b & 1)
			p ^= a;
		a = xtime(a);
		b = (uint8_t)(b >> 1);
	}
	return p;
}

static uint8_t gf_inv(uint8_t a) {
	uint8_t p;
	int i;
	if (!a)
		return 0;
	p = a;
	/* a^254 in GF(2^8). */
	for (i = 0; i < 253; i++)
		p = gf_mul(p, a);
	return p;
}

static uint8_t sbox_of(uint8_t a) {
	uint8_t x = gf_inv(a);
	uint8_t y = x;
	y = (uint8_t)(y ^ ((x << 1) | (x >> 7)));
	y = (uint8_t)(y ^ ((x << 2) | (x >> 6)));
	y = (uint8_t)(y ^ ((x << 3) | (x >> 5)));
	y = (uint8_t)(y ^ ((x << 4) | (x >> 4)));
	return (uint8_t)(y ^ 0x63);
}

static uint8_t sbox[256];
static uint8_t rsbox[256];
static int sbox_ready;

static void sbox_init(void) {
	int i;
	if (sbox_ready)
		return;
	for (i = 0; i < 256; i++) {
		sbox[i] = sbox_of((uint8_t)i);
		rsbox[sbox[i]] = (uint8_t)i;
	}
	sbox_ready = 1;
}

static void add_round_key(uint8_t s[16], const uint8_t* rk) {
	int i;
	for (i = 0; i < 16; i++)
		s[i] ^= rk[i];
}

static void sub_bytes(uint8_t s[16]) {
	int i;
	for (i = 0; i < 16; i++)
		s[i] = sbox[s[i]];
}

static void inv_sub_bytes(uint8_t s[16]) {
	int i;
	for (i = 0; i < 16; i++)
		s[i] = rsbox[s[i]];
}

static void shift_rows(uint8_t s[16]) {
	uint8_t t;
	t = s[1];
	s[1] = s[5];
	s[5] = s[9];
	s[9] = s[13];
	s[13] = t;
	t = s[2];
	s[2] = s[10];
	s[10] = t;
	t = s[6];
	s[6] = s[14];
	s[14] = t;
	t = s[15];
	s[15] = s[11];
	s[11] = s[7];
	s[7] = s[3];
	s[3] = t;
}

static void inv_shift_rows(uint8_t s[16]) {
	uint8_t t;
	t = s[13];
	s[13] = s[9];
	s[9] = s[5];
	s[5] = s[1];
	s[1] = t;
	t = s[2];
	s[2] = s[10];
	s[10] = t;
	t = s[6];
	s[6] = s[14];
	s[14] = t;
	t = s[3];
	s[3] = s[7];
	s[7] = s[11];
	s[11] = s[15];
	s[15] = t;
}

static void mix_columns(uint8_t s[16]) {
	int c;
	for (c = 0; c < 4; c++) {
		uint8_t* p = s + c * 4;
		uint8_t a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3];
		p[0] = (uint8_t)(gf_mul(a0, 2) ^ gf_mul(a1, 3) ^ a2 ^ a3);
		p[1] = (uint8_t)(a0 ^ gf_mul(a1, 2) ^ gf_mul(a2, 3) ^ a3);
		p[2] = (uint8_t)(a0 ^ a1 ^ gf_mul(a2, 2) ^ gf_mul(a3, 3));
		p[3] = (uint8_t)(gf_mul(a0, 3) ^ a1 ^ a2 ^ gf_mul(a3, 2));
	}
}

static void inv_mix_columns(uint8_t s[16]) {
	int c;
	for (c = 0; c < 4; c++) {
		uint8_t* p = s + c * 4;
		uint8_t a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3];
		p[0] = (uint8_t)(gf_mul(a0, 14) ^ gf_mul(a1, 11) ^ gf_mul(a2, 13) ^ gf_mul(a3, 9));
		p[1] = (uint8_t)(gf_mul(a0, 9) ^ gf_mul(a1, 14) ^ gf_mul(a2, 11) ^ gf_mul(a3, 13));
		p[2] = (uint8_t)(gf_mul(a0, 13) ^ gf_mul(a1, 9) ^ gf_mul(a2, 14) ^ gf_mul(a3, 11));
		p[3] = (uint8_t)(gf_mul(a0, 11) ^ gf_mul(a1, 13) ^ gf_mul(a2, 9) ^ gf_mul(a3, 14));
	}
}

static void expand_key(const uint8_t key[16], uint8_t rk[176]) {
	static const uint8_t rcon[10] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36};
	int i;
	memcpy(rk, key, 16);
	for (i = 4; i < 44; i++) {
		uint8_t t[4];
		memcpy(t, rk + (i - 1) * 4, 4);
		if ((i % 4) == 0) {
			uint8_t tmp = t[0];
			t[0] = (uint8_t)(sbox[t[1]] ^ rcon[(i / 4) - 1]);
			t[1] = sbox[t[2]];
			t[2] = sbox[t[3]];
			t[3] = sbox[tmp];
		}
		rk[i * 4] = (uint8_t)(rk[(i - 4) * 4] ^ t[0]);
		rk[i * 4 + 1] = (uint8_t)(rk[(i - 4) * 4 + 1] ^ t[1]);
		rk[i * 4 + 2] = (uint8_t)(rk[(i - 4) * 4 + 2] ^ t[2]);
		rk[i * 4 + 3] = (uint8_t)(rk[(i - 4) * 4 + 3] ^ t[3]);
	}
}

static void aes_crypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16], int decrypt) {
	uint8_t rk[176];
	uint8_t s[16];
	int round;

	sbox_init();
	expand_key(key, rk);
	memcpy(s, in, 16);
	if (!decrypt) {
		add_round_key(s, rk);
		for (round = 1; round <= 10; round++) {
			sub_bytes(s);
			shift_rows(s);
			if (round != 10)
				mix_columns(s);
			add_round_key(s, rk + round * 16);
		}
	} else {
		add_round_key(s, rk + 160);
		for (round = 9; round >= 0; round--) {
			inv_shift_rows(s);
			inv_sub_bytes(s);
			add_round_key(s, rk + round * 16);
			if (round)
				inv_mix_columns(s);
		}
	}
	memcpy(out, s, 16);
}

void wifi_aes_encrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
	aes_crypt(key, in, out, 0);
}

static void aes_decrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
	aes_crypt(key, in, out, 1);
}

static void xor_t(uint8_t a[8], uint64_t t) {
	int i;
	for (i = 7; i >= 0; i--) {
		a[i] = (uint8_t)(a[i] ^ (uint8_t)t);
		t >>= 8;
	}
}

int wifi_aes_wrap(const uint8_t kek[16], const uint8_t* plain, int plain_len, uint8_t* out) {
	uint8_t a[8];
	uint8_t r[8][8];
	int n, j, i;

	if (!kek || !plain || !out || plain_len < 16 || (plain_len % 8) != 0 || plain_len > 64)
		return -1;
	n = plain_len / 8;
	memset(a, 0xa6, 8);
	for (i = 0; i < n; i++)
		memcpy(r[i], plain + i * 8, 8);
	for (j = 0; j <= 5; j++) {
		for (i = 1; i <= n; i++) {
			uint8_t b[16];
			uint8_t c[16];
			memcpy(b, a, 8);
			memcpy(b + 8, r[i - 1], 8);
			wifi_aes_encrypt(kek, b, c);
			memcpy(a, c, 8);
			xor_t(a, (uint64_t)(n * j + i));
			memcpy(r[i - 1], c + 8, 8);
		}
	}
	memcpy(out, a, 8);
	for (i = 0; i < n; i++)
		memcpy(out + 8 + i * 8, r[i], 8);
	return plain_len + 8;
}

int wifi_aes_unwrap(const uint8_t kek[16], const uint8_t* cipher, int cipher_len, uint8_t* out) {
	uint8_t a[8];
	uint8_t r[8][8];
	int n, j, i;

	if (!kek || !cipher || !out || cipher_len < 24 || (cipher_len % 8) != 0 || cipher_len > 72)
		return -1;
	n = (cipher_len / 8) - 1;
	memcpy(a, cipher, 8);
	for (i = 0; i < n; i++)
		memcpy(r[i], cipher + 8 + i * 8, 8);
	for (j = 5; j >= 0; j--) {
		for (i = n; i >= 1; i--) {
			uint8_t b[16];
			uint8_t c[16];
			xor_t(a, (uint64_t)(n * j + i));
			memcpy(b, a, 8);
			memcpy(b + 8, r[i - 1], 8);
			aes_decrypt(kek, b, c);
			memcpy(a, c, 8);
			memcpy(r[i - 1], c + 8, 8);
		}
	}
	for (i = 0; i < 8; i++) {
		if (a[i] != 0xa6)
			return -1;
	}
	for (i = 0; i < n; i++)
		memcpy(out + i * 8, r[i], 8);
	return cipher_len - 8;
}

static void ccm_ctr(uint8_t block[16], const uint8_t nonce[13], uint16_t ctr) {
	block[0] = 1; /* L-1, L = 2 */
	memcpy(block + 1, nonce, 13);
	block[14] = (uint8_t)(ctr >> 8);
	block[15] = (uint8_t)ctr;
}

static void ccm_mac_blocks(const uint8_t key[16], uint8_t x[16], const uint8_t* p, int len) {
	int off = 0;
	while (off < len) {
		uint8_t blk[16];
		int n = len - off;
		int i;
		if (n > 16)
			n = 16;
		memset(blk, 0, 16);
		memcpy(blk, p + off, (size_t)n);
		for (i = 0; i < 16; i++)
			x[i] ^= blk[i];
		wifi_aes_encrypt(key, x, x);
		off += n;
	}
}

int wifi_ccm_encrypt(const uint8_t key[16], const uint8_t nonce[13],
					 const uint8_t* aad, int aad_len,
					 const uint8_t* pt, int pt_len, uint8_t* ct_and_mic) {
	uint8_t b0[16];
	uint8_t x[16];
	uint8_t s[16];
	uint8_t aad_buf[48];
	int aad_bytes;
	int i;

	if (!key || !nonce || !ct_and_mic || pt_len < 0 || pt_len > 1500)
		return -1;
	if (aad_len < 0)
		aad_len = 0;
	if (aad_len > 32)
		return -1;
	/* M = 8, L = 2, Adata set when an AAD is present. Flags = 0x59 with AAD. */
	b0[0] = (uint8_t)((aad_len ? 0x40 : 0) | (((8 - 2) / 2) << 3) | 1);
	memcpy(b0 + 1, nonce, 13);
	b0[14] = (uint8_t)(pt_len >> 8);
	b0[15] = (uint8_t)pt_len;
	wifi_aes_encrypt(key, b0, x);
	if (aad_len) {
		memset(aad_buf, 0, sizeof(aad_buf));
		aad_buf[0] = (uint8_t)(aad_len >> 8);
		aad_buf[1] = (uint8_t)aad_len;
		memcpy(aad_buf + 2, aad, (size_t)aad_len);
		aad_bytes = 2 + aad_len;
		if (aad_bytes % 16)
			aad_bytes += 16 - (aad_bytes % 16);
		ccm_mac_blocks(key, x, aad_buf, aad_bytes);
	}
	if (pt_len && pt)
		ccm_mac_blocks(key, x, pt, pt_len);
	ccm_ctr(s, nonce, 0);
	wifi_aes_encrypt(key, s, s);
	for (i = 0; i < 8; i++)
		ct_and_mic[pt_len + i] = (uint8_t)(x[i] ^ s[i]);
	for (i = 0; i < pt_len; i += 16) {
		uint8_t ks[16];
		int n = pt_len - i;
		int k;
		if (n > 16)
			n = 16;
		ccm_ctr(ks, nonce, (uint16_t)((i / 16) + 1));
		wifi_aes_encrypt(key, ks, ks);
		for (k = 0; k < n; k++)
			ct_and_mic[i + k] = (uint8_t)(pt[i + k] ^ ks[k]);
	}
	return pt_len + 8;
}

int wifi_ccm_decrypt(const uint8_t key[16], const uint8_t nonce[13],
					 const uint8_t* aad, int aad_len,
					 const uint8_t* ct_and_mic, int ct_len, uint8_t* pt) {
	uint8_t expect[1600];
	int pt_len;
	int i;
	uint8_t diff = 0;

	if (!ct_and_mic || ct_len < 8 || ct_len - 8 > 1500)
		return -1;
	pt_len = ct_len - 8;
	for (i = 0; i < pt_len; i += 16) {
		uint8_t ks[16];
		int n = pt_len - i;
		int k;
		if (n > 16)
			n = 16;
		ccm_ctr(ks, nonce, (uint16_t)((i / 16) + 1));
		wifi_aes_encrypt(key, ks, ks);
		for (k = 0; k < n; k++)
			pt[i + k] = (uint8_t)(ct_and_mic[i + k] ^ ks[k]);
	}
	if (wifi_ccm_encrypt(key, nonce, aad, aad_len, pt, pt_len, expect) < 0)
		return -1;
	for (i = 0; i < pt_len + 8; i++)
		diff |= (uint8_t)(expect[i] ^ ct_and_mic[i]);
	if (diff) {
		memset(pt, 0, (size_t)pt_len);
		return -1;
	}
	return pt_len;
}
