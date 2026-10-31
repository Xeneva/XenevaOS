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

static const uint8_t sbox[256] = {
	0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
	0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
	0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
	0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
	0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
	0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
	0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
	0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
	0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
	0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
	0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
	0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
	0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
	0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
	0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
	0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16};

static const uint8_t rcon[11] = {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36};

void bt_rev(const uint8_t* in, uint8_t* out, size_t n) {
	size_t i;
	for (i = 0; i < n; i++)
		out[n - 1 - i] = in[i];
}

static uint8_t xtime(uint8_t x) {
	return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1b : 0));
}

void bt_aes_encrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
	uint8_t rk[176];
	uint8_t s[16];
	int i, round;

	memcpy(rk, key, 16);
	for (i = 4; i < 44; i++) {
		uint8_t t[4];
		memcpy(t, rk + (i - 1) * 4, 4);
		if ((i % 4) == 0) {
			uint8_t tmp = t[0];
			t[0] = sbox[t[1]] ^ rcon[i / 4];
			t[1] = sbox[t[2]];
			t[2] = sbox[t[3]];
			t[3] = sbox[tmp];
		}
		rk[i * 4 + 0] = rk[(i - 4) * 4 + 0] ^ t[0];
		rk[i * 4 + 1] = rk[(i - 4) * 4 + 1] ^ t[1];
		rk[i * 4 + 2] = rk[(i - 4) * 4 + 2] ^ t[2];
		rk[i * 4 + 3] = rk[(i - 4) * 4 + 3] ^ t[3];
	}

	memcpy(s, in, 16);
	for (i = 0; i < 16; i++)
		s[i] ^= rk[i];

	for (round = 1; round <= 10; round++) {
		uint8_t n[16];
		for (i = 0; i < 16; i++)
			s[i] = sbox[s[i]];
		n[0] = s[0];
		n[1] = s[5];
		n[2] = s[10];
		n[3] = s[15];
		n[4] = s[4];
		n[5] = s[9];
		n[6] = s[14];
		n[7] = s[3];
		n[8] = s[8];
		n[9] = s[13];
		n[10] = s[2];
		n[11] = s[7];
		n[12] = s[12];
		n[13] = s[1];
		n[14] = s[6];
		n[15] = s[11];
		if (round < 10) {
			for (i = 0; i < 16; i += 4) {
				uint8_t a = n[i], b = n[i + 1], c = n[i + 2], d = n[i + 3];
				uint8_t a2 = xtime(a), b2 = xtime(b), c2 = xtime(c), d2 = xtime(d);
				n[i] = (uint8_t)(a2 ^ b2 ^ b ^ c ^ d);
				n[i + 1] = (uint8_t)(a ^ b2 ^ c2 ^ c ^ d);
				n[i + 2] = (uint8_t)(a ^ b ^ c2 ^ d2 ^ d);
				n[i + 3] = (uint8_t)(a2 ^ a ^ b ^ c ^ d2);
			}
		}
		for (i = 0; i < 16; i++)
			n[i] ^= rk[round * 16 + i];
		memcpy(s, n, 16);
	}
	memcpy(out, s, 16);
}

static void cmac_subkey(uint8_t* k) {
	int i;
	uint8_t msb = k[0] & 0x80;
	for (i = 0; i < 15; i++)
		k[i] = (uint8_t)((k[i] << 1) | (k[i + 1] >> 7));
	k[15] = (uint8_t)(k[15] << 1);
	if (msb)
		k[15] ^= 0x87;
}

void bt_cmac(const uint8_t key[16], const uint8_t* msg, size_t len, uint8_t mac[16]) {
	uint8_t zero[16];
	uint8_t k1[16], k2[16];
	uint8_t x[16];
	size_t n, i, off;
	int complete;

	memset(zero, 0, 16);
	bt_aes_encrypt(key, zero, k1);
	cmac_subkey(k1);
	memcpy(k2, k1, 16);
	cmac_subkey(k2);

	memset(x, 0, 16);
	if (len == 0) {
		n = 1;
		complete = 0;
	} else {
		n = (len + 15) / 16;
		complete = (len % 16) == 0;
	}

	off = 0;
	for (i = 0; i < n; i++) {
		uint8_t block[16];
		size_t take = 16;
		int last = (i + 1 == n);
		memset(block, 0, 16);
		if (!last || complete) {
			if (off + 16 > len)
				take = len - off;
			if (take)
				memcpy(block, msg + off, take);
		} else {
			take = len - off;
			if (take)
				memcpy(block, msg + off, take);
			block[take] = 0x80;
		}
		if (last) {
			const uint8_t* ks = complete ? k1 : k2;
			int j;
			for (j = 0; j < 16; j++)
				block[j] ^= ks[j];
		}
		for (int j = 0; j < 16; j++)
			x[j] ^= block[j];
		bt_aes_encrypt(key, x, x);
		off += 16;
	}
	memcpy(mac, x, 16);
}

void bt_e(const uint8_t k[16], uint8_t r[16]) {
	uint8_t key[16], data[16], out[16];
	bt_rev(k, key, 16);
	bt_rev(r, data, 16);
	bt_aes_encrypt(key, data, out);
	bt_rev(out, r, 16);
}

static void xor16(uint8_t* d, const uint8_t* a, const uint8_t* b) {
	int i;
	for (i = 0; i < 16; i++)
		d[i] = (uint8_t)(a[i] ^ b[i]);
}

void bt_c1(const uint8_t k[16], const uint8_t r[16], const uint8_t preq[7], const uint8_t pres[7],
		   uint8_t iat, const uint8_t ia[6], uint8_t rat, const uint8_t ra[6], uint8_t res[16]) {
	uint8_t p1[16], p2[16];
	memset(p1, 0, 16);
	p1[0] = iat;
	p1[1] = rat;
	memcpy(p1 + 2, preq, 7);
	memcpy(p1 + 9, pres, 7);
	xor16(res, r, p1);
	bt_e(k, res);
	memcpy(p2, ra, 6);
	memcpy(p2 + 6, ia, 6);
	memset(p2 + 12, 0, 4);
	xor16(res, res, p2);
	bt_e(k, res);
}

void bt_s1(const uint8_t k[16], const uint8_t r1[16], const uint8_t r2[16], uint8_t out[16]) {
	memcpy(out, r2, 8);
	memcpy(out + 8, r1, 8);
	bt_e(k, out);
}

void bt_ah(const uint8_t irk[16], const uint8_t r[3], uint8_t out[3]) {
	uint8_t block[16];
	memcpy(block, r, 3);
	memset(block + 3, 0, 13);
	bt_e(irk, block);
	memcpy(out, block, 3);
}

static void cmac_le(const uint8_t key_le[16], const uint8_t* msg_le, size_t len, uint8_t mac_le[16]) {
	uint8_t key[16], msg[80], mac[16];
	if (len > 80)
		len = 80;
	bt_rev(key_le, key, 16);
	bt_rev(msg_le, msg, len);
	bt_cmac(key, msg, len, mac);
	bt_rev(mac, mac_le, 16);
}

void bt_f4(const uint8_t u[32], const uint8_t v[32], const uint8_t x[16], uint8_t z, uint8_t res[16]) {
	uint8_t m[65];
	m[0] = z;
	memcpy(m + 1, v, 32);
	memcpy(m + 33, u, 32);
	cmac_le(x, m, 65, res);
}

void bt_f5(const uint8_t w[32], const uint8_t n1[16], const uint8_t n2[16], const uint8_t a1[7],
		   const uint8_t a2[7], uint8_t mackey[16], uint8_t ltk[16]) {
	const uint8_t btle[4] = {0x65, 0x6c, 0x74, 0x62};
	const uint8_t salt[16] = {0xbe, 0x83, 0x60, 0x5a, 0xdb, 0x0b, 0x37, 0x60,
							   0x38, 0xa5, 0xf5, 0xaa, 0x91, 0x83, 0x88, 0x6c};
	const uint8_t length[2] = {0x00, 0x01};
	uint8_t m[53], t[16];
	cmac_le(salt, w, 32, t);
	memcpy(m, length, 2);
	memcpy(m + 2, a2, 7);
	memcpy(m + 9, a1, 7);
	memcpy(m + 16, n2, 16);
	memcpy(m + 32, n1, 16);
	memcpy(m + 48, btle, 4);
	m[52] = 0;
	cmac_le(t, m, 53, mackey);
	m[52] = 1;
	cmac_le(t, m, 53, ltk);
}

void bt_f6(const uint8_t w[16], const uint8_t n1[16], const uint8_t n2[16], const uint8_t r[16],
		   const uint8_t io_cap[3], const uint8_t a1[7], const uint8_t a2[7], uint8_t res[16]) {
	uint8_t m[65];
	memcpy(m, a2, 7);
	memcpy(m + 7, a1, 7);
	memcpy(m + 14, io_cap, 3);
	memcpy(m + 17, r, 16);
	memcpy(m + 33, n2, 16);
	memcpy(m + 49, n1, 16);
	cmac_le(w, m, 65, res);
}

uint32_t bt_g2(const uint8_t u[32], const uint8_t v[32], const uint8_t x[16], const uint8_t y[16]) {
	uint8_t m[80], tmp[16];
	uint32_t val;
	memcpy(m, y, 16);
	memcpy(m + 16, v, 32);
	memcpy(m + 48, u, 32);
	cmac_le(x, m, 80, tmp);
	val = (uint32_t)tmp[0] | ((uint32_t)tmp[1] << 8) | ((uint32_t)tmp[2] << 16) | ((uint32_t)tmp[3] << 24);
	return val % 1000000u;
}

/* NIST P-256, 32-bit little-endian limbs. */
typedef struct {
	uint32_t d[8];
} fe;

static const fe FE_P = {{0xffffffff, 0xffffffff, 0xffffffff, 0x00000000, 0x00000000, 0x00000000,
						  0x00000001, 0xffffffff}};
static const fe FE_GX = {{0xd898c296, 0xf4a13945, 0x2deb33a0, 0x77037d81, 0x63a440f2, 0xf8bce6e5,
						   0xe12c4247, 0x6b17d1f2}};
static const fe FE_GY = {{0x37bf51f5, 0xcbb64068, 0x6b315ece, 0x2bce3357, 0x7c0f9e16, 0x8ee7eb4a,
						   0xfe1a7f9b, 0x4fe342e2}};

static void fe_set(fe* r, const fe* a) {
	memcpy(r->d, a->d, sizeof r->d);
}

static int fe_is0(const fe* a) {
	int i;
	for (i = 0; i < 8; i++)
		if (a->d[i])
			return 0;
	return 1;
}

static int fe_cmp(const fe* a, const fe* b) {
	int i;
	for (i = 7; i >= 0; i--) {
		if (a->d[i] < b->d[i])
			return -1;
		if (a->d[i] > b->d[i])
			return 1;
	}
	return 0;
}

static void fe_add(fe* r, const fe* a, const fe* b) {
	uint64_t c = 0;
	int i;
	for (i = 0; i < 8; i++) {
		c += (uint64_t)a->d[i] + b->d[i];
		r->d[i] = (uint32_t)c;
		c >>= 32;
	}
}

static void fe_sub(fe* r, const fe* a, const fe* b) {
	int64_t c = 0;
	int i;
	for (i = 0; i < 8; i++) {
		c += (int64_t)a->d[i] - b->d[i];
		r->d[i] = (uint32_t)c;
		c >>= 32;
	}
}

static void fe_mod(fe* r) {
	while (fe_cmp(r, &FE_P) >= 0) {
		fe tmp;
		fe_sub(&tmp, r, &FE_P);
		fe_set(r, &tmp);
	}
}

static void wide_sub(uint32_t* r, const uint32_t* a, int n) {
	int64_t c = 0;
	int i;
	for (i = 0; i < n; i++) {
		c += (int64_t)r[i] - a[i];
		r[i] = (uint32_t)c;
		c >>= 32;
	}
}

static int wide_cmp(const uint32_t* a, const uint32_t* b, int n) {
	int i;
	for (i = n - 1; i >= 0; i--) {
		if (a[i] < b[i])
			return -1;
		if (a[i] > b[i])
			return 1;
	}
	return 0;
}

static void p_shift(uint32_t* dst, int sh) {
	int i;
	memset(dst, 0, 16 * sizeof(uint32_t));
	for (i = 0; i < 256; i++) {
		uint32_t bit = (FE_P.d[i / 32] >> (i % 32)) & 1u;
		int d = i + sh;
		if (bit && d < 512)
			dst[d / 32] |= 1u << (d % 32);
	}
}

/* Product is < 2^512 and P is 256 bits, so one subtraction per bit is enough. */
static void fe_reduce_wide(fe* r, uint32_t w[16]) {
	int sh;
	for (sh = 256; sh >= 0; sh--) {
		uint32_t ps[16];
		p_shift(ps, sh);
		if (wide_cmp(w, ps, 16) >= 0)
			wide_sub(w, ps, 16);
	}
	memcpy(r->d, w, 32);
}

static void fe_mul(fe* r, const fe* a, const fe* b) {
	uint32_t w[16];
	int i, j;
	memset(w, 0, sizeof w);
	for (i = 0; i < 8; i++) {
		uint64_t c = 0;
		for (j = 0; j < 8; j++) {
			c += (uint64_t)w[i + j] + (uint64_t)a->d[i] * b->d[j];
			w[i + j] = (uint32_t)c;
			c >>= 32;
		}
		w[i + 8] = (uint32_t)c;
	}
	fe_reduce_wide(r, w);
}

static void fe_addm(fe* r, const fe* a, const fe* b) {
	uint32_t w[16];
	uint64_t c = 0;
	int i;
	memset(w, 0, sizeof w);
	for (i = 0; i < 8; i++) {
		c += (uint64_t)a->d[i] + b->d[i];
		w[i] = (uint32_t)c;
		c >>= 32;
	}
	w[8] = (uint32_t)c;
	fe_reduce_wide(r, w);
}

static void fe_subm(fe* r, const fe* a, const fe* b) {
	fe t;
	if (fe_cmp(a, b) >= 0) {
		fe_sub(&t, a, b);
	} else {
		fe u;
		fe_add(&u, a, &FE_P);
		fe_sub(&t, &u, b);
	}
	fe_mod(&t);
	fe_set(r, &t);
}

static void fe_inv(fe* r, const fe* a) {
	/* a^(p-2) */
	fe base, result;
	int bit;
	uint8_t e[32];
	/* p-2 big endian */
	fe pm2;
	fe_set(&pm2, &FE_P);
	if (pm2.d[0] >= 2)
		pm2.d[0] -= 2;
	else {
		pm2.d[0] = pm2.d[0] + 0xfffffffe;
		int k = 1;
		while (k < 8 && pm2.d[k] == 0) {
			pm2.d[k] = 0xffffffff;
			k++;
		}
		if (k < 8)
			pm2.d[k]--;
	}
	for (bit = 0; bit < 8; bit++) {
		e[31 - bit * 4] = 0;
	}
	for (int i = 0; i < 8; i++) {
		uint32_t w = pm2.d[i];
		e[31 - (i * 4 + 0)] = 0;
		/* store as 32 big-endian bytes */
		(void)w;
	}
	for (int i = 0; i < 32; i++)
		e[i] = (uint8_t)((pm2.d[7 - i / 4] >> ((3 - (i % 4)) * 8)) & 0xff);

	fe_set(&base, a);
	memset(&result, 0, sizeof result);
	result.d[0] = 1;
	for (bit = 0; bit < 256; bit++) {
		int on = (e[bit / 8] >> (7 - (bit % 8))) & 1;
		fe sq;
		fe_mul(&sq, &result, &result);
		fe_set(&result, &sq);
		if (on) {
			fe m;
			fe_mul(&m, &result, &base);
			fe_set(&result, &m);
		}
	}
	fe_set(r, &result);
}

static void fe_from_be(fe* r, const uint8_t b[32]) {
	int i;
	for (i = 0; i < 8; i++) {
		int o = (7 - i) * 4;
		r->d[i] = ((uint32_t)b[o] << 24) | ((uint32_t)b[o + 1] << 16) | ((uint32_t)b[o + 2] << 8) |
				  b[o + 3];
	}
}

static void fe_to_be(const fe* a, uint8_t b[32]) {
	int i;
	for (i = 0; i < 8; i++) {
		int o = (7 - i) * 4;
		b[o] = (uint8_t)(a->d[i] >> 24);
		b[o + 1] = (uint8_t)(a->d[i] >> 16);
		b[o + 2] = (uint8_t)(a->d[i] >> 8);
		b[o + 3] = (uint8_t)a->d[i];
	}
}

typedef struct {
	fe x, y;
	int inf;
} pt;

static void pt_dbl(pt* r, const pt* p) {
	fe lambda, t, x3, y3, num, den, inv;
	if (p->inf || fe_is0(&p->y)) {
		r->inf = 1;
		return;
	}
	/* lambda = (3*x^2 - 3) / (2*y)  since a = -3 */
	fe xx, three, nume;
	fe_mul(&xx, &p->x, &p->x);
	memset(&three, 0, sizeof three);
	three.d[0] = 3;
	fe_mul(&nume, &xx, &three);
	fe_subm(&nume, &nume, &three); /* 3x^2 - 3 */
	memset(&den, 0, sizeof den);
	den.d[0] = 2;
	fe_mul(&den, &den, &p->y);
	fe_inv(&inv, &den);
	fe_mul(&lambda, &nume, &inv);
	fe_mul(&t, &lambda, &lambda);
	fe_subm(&x3, &t, &p->x);
	fe_subm(&x3, &x3, &p->x);
	fe_subm(&num, &p->x, &x3);
	fe_mul(&y3, &lambda, &num);
	fe_subm(&y3, &y3, &p->y);
	fe_set(&r->x, &x3);
	fe_set(&r->y, &y3);
	r->inf = 0;
	(void)num;
}

static void pt_add(pt* r, const pt* a, const pt* b) {
	fe lambda, inv, den, num, x3, y3, t;
	if (a->inf) {
		*r = *b;
		return;
	}
	if (b->inf) {
		*r = *a;
		return;
	}
	if (fe_cmp(&a->x, &b->x) == 0) {
		fe yy;
		fe_addm(&yy, &a->y, &b->y);
		if (fe_is0(&yy) || fe_cmp(&a->y, &b->y) != 0) {
			r->inf = 1;
			return;
		}
		pt_dbl(r, a);
		return;
	}
	fe_subm(&num, &b->y, &a->y);
	fe_subm(&den, &b->x, &a->x);
	fe_inv(&inv, &den);
	fe_mul(&lambda, &num, &inv);
	fe_mul(&t, &lambda, &lambda);
	fe_subm(&x3, &t, &a->x);
	fe_subm(&x3, &x3, &b->x);
	fe_subm(&num, &a->x, &x3);
	fe_mul(&y3, &lambda, &num);
	fe_subm(&y3, &y3, &a->y);
	fe_set(&r->x, &x3);
	fe_set(&r->y, &y3);
	r->inf = 0;
}

static void pt_mul(pt* r, const uint8_t scalar[32], const pt* p) {
	pt base = *p;
	pt acc;
	int i, bit;
	acc.inf = 1;
	for (i = 0; i < 32; i++) {
		for (bit = 7; bit >= 0; bit--) {
			pt d;
			pt_dbl(&d, &acc);
			acc = d;
			if ((scalar[i] >> bit) & 1) {
				pt s;
				pt_add(&s, &acc, &base);
				acc = s;
			}
		}
	}
	*r = acc;
}

int bt_p256_mul_g(const uint8_t scalar[32], uint8_t x[32], uint8_t y[32]) {
	pt g, r;
	g.inf = 0;
	fe_set(&g.x, &FE_GX);
	fe_set(&g.y, &FE_GY);
	pt_mul(&r, scalar, &g);
	if (r.inf)
		return -1;
	fe_to_be(&r.x, x);
	fe_to_be(&r.y, y);
	return 0;
}

int bt_p256_dh(const uint8_t scalar[32], const uint8_t px[32], const uint8_t py[32], uint8_t outx[32]) {
	pt q, r;
	uint8_t y[32];
	q.inf = 0;
	fe_from_be(&q.x, px);
	fe_from_be(&q.y, py);
	pt_mul(&r, scalar, &q);
	if (r.inf)
		return -1;
	fe_to_be(&r.x, outx);
	fe_to_be(&r.y, y);
	return 0;
}
