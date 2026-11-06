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

/* Encode-only SBC for A2DP classic audio (Sony ULT WEAR and every A2DP
 * sink). Filterbank, Loudness offsets, bit allocation, CRC and packing are
 * the A2DP Appendix B procedures as implemented by google/libsbc
 * (Apache-2.0, Copyright 2022 Google LLC / Tempow); ported to a fixed
 * joint-stereo configuration with no libc beyond <stdint.h>/<string.h>.
 * Bit allocation is recomputed identically by every headset decoder, so
 * the Loudness tables and distribution loop below must stay bit-exact. */

#include "sbc.h"
#include <string.h>

#define SBC_SAT16(v) \
	(int16_t)(((v) > 32767 ? 32767 : ((v) < -32768 ? -32768 : (v))))

/* Loudness allocation offsets, per sampling frequency. */
static const int sbc_loud8[2][8] = {
	/* 44.1 kHz */ { -4, 0, 0, 0, 0, 0, 1, 2 },
	/* 48 kHz   */ { -4, 0, 0, 0, 0, 0, 1, 2 },
};

/* CRC-8 table, G(X) = X^8 + X^4 + X^3 + X^2 + 1, generated once. */
static uint8_t sbc_crc_tab[256];
static int sbc_crc_ready;

static void sbc_crc_init(void) {
	int i, k;
	if (sbc_crc_ready)
		return;
	for (i = 0; i < 256; i++) {
		unsigned c = (unsigned)i;
		for (k = 0; k < 8; k++)
			c = (c & 0x80) ? ((c << 1) ^ 0x1d) : (c << 1);
		sbc_crc_tab[i] = (uint8_t)(c & 0xff);
	}
	sbc_crc_ready = 1;
}

/* MSB-first bit writer. */
typedef struct {
	uint8_t* buf;
	unsigned bit;
	unsigned cap;
} SbcBits;

static void sbc_bw(SbcBits* b, uint8_t* out, unsigned cap) {
	b->buf = out;
	b->bit = 0;
	b->cap = cap * 8;
	memset(out, 0, cap);
}

static void sbc_put(SbcBits* b, unsigned v, int n) {
	while (n > 0) {
		unsigned byte = b->bit >> 3;
		int room = 8 - (b->bit & 7);
		int take = n < room ? n : room;
		unsigned shift = (unsigned)(n - take);
		if (byte < b->cap / 8)
			b->buf[byte] |= (uint8_t)(((v >> shift) & ((1u << take) - 1))
									 << (room - take));
		b->bit += (unsigned)take;
		n -= take;
	}
}

/* 8-subband analysis, fixed 2.13 window + DCT. */
static void sbc_analyze8(BtSbcCh* st, const int16_t* in, int pitch, int16_t* out) {
	static const int16_t window[2][8][10] = {
		{
			{ 0, 185, 2228, -2228, -185, 0, 185, 2228, -2228, -185 },
			{ 27, 480, 4039, -480, 30, 27, 480, 4039, -480, 30 },
			{ 5, 263, 2719, -1743, -115, 5, 263, 2719, -1743, -115 },
			{ 58, 502, 4764, 290, 69, 58, 502, 4764, 290, 69 },
			{ 11, 343, 3197, -1280, -54, 11, 343, 3197, -1280, -54 },
			{ 48, 532, 4612, 96, 65, 48, 532, 4612, 96, 65 },
			{ 18, 418, 3644, -856, -6, 18, 418, 3644, -856, -6 },
			{ 37, 521, 4367, -161, 53, 37, 521, 4367, -161, 53 },
		},
		{
			{ 66, 424, 4815, 424, 66, 66, 424, 4815, 424, 66 },
			{ 30, -480, 4039, 480, 27, 30, -480, 4039, 480, 27 },
			{ 69, 290, 4764, 502, 58, 69, 290, 4764, 502, 58 },
			{ -115, -1743, 2719, 263, 5, -115, -1743, 2719, 263, 5 },
			{ 65, 96, 4612, 532, 48, 65, 96, 4612, 532, 48 },
			{ -54, -1280, 3197, 343, 11, -54, -1280, 3197, 343, 11 },
			{ 53, -161, 4367, 521, 37, 53, -161, 4367, 521, 37 },
			{ -6, -856, 3644, 418, 18, -6, -856, 3644, 418, 18 },
		},
	};
	static const int16_t cosmat[8][8] = {
		{ 5793, 6811, 7568, 8035, 4551, 3135, 1598, 8192 },
		{ -5793, -1598, 3135, 6811, -8035, -7568, -4551, 8192 },
		{ -5793, -8035, -3135, 4551, 1598, 7568, 6811, 8192 },
		{ 5793, -4551, -7568, 1598, 6811, -3135, -8035, 8192 },
		{ 5793, 4551, -7568, -1598, -6811, -3135, 8035, 8192 },
		{ -5793, 8035, -3135, -4551, -1598, 7568, -6811, 8192 },
		{ -5793, 1598, 3135, -6811, 8035, -7568, 4551, 8192 },
		{ 5793, -6811, 7568, -8035, -4551, 3135, -1598, 8192 },
	};
	int idx = st->idx >> 1, odd = st->idx & 1;
	int16_t(*x)[5] = st->x[odd];
	int in_idx = idx ? 5 - idx : 0;
	int y0, y1, y2, y3, y4, y5, y6, y7;
	int16_t y[8];
	int i;
	const int16_t(*w0)[10] = (const int16_t(*)[10])(window[0][0] + idx);
	const int16_t(*w1)[10] = (const int16_t(*)[10])(window[1][0] + idx);

	x[0][in_idx] = in[(7 - 0) * pitch];
	x[1][in_idx] = in[(7 - 4) * pitch];
	x[2][in_idx] = in[(7 - 1) * pitch];
	x[3][in_idx] = in[(7 - 7) * pitch];
	x[4][in_idx] = in[(7 - 2) * pitch];
	x[5][in_idx] = in[(7 - 6) * pitch];
	x[6][in_idx] = in[(7 - 3) * pitch];
	x[7][in_idx] = in[(7 - 5) * pitch];

	y0 = x[0][0] * w0[0][0] + x[0][1] * w0[0][1] + x[0][2] * w0[0][2] +
		 x[0][3] * w0[0][3] + x[0][4] * w0[0][4] + st->y[0];
	st->y[0] = x[0][0] * w1[0][0] + x[0][1] * w1[0][1] + x[0][2] * w1[0][2] +
			   x[0][3] * w1[0][3] + x[0][4] * w1[0][4];
	y1 = x[2][0] * w0[2][0] + x[2][1] * w0[2][1] + x[2][2] * w0[2][2] +
		 x[2][3] * w0[2][3] + x[2][4] * w0[2][4] + x[3][0] * w0[3][0] +
		 x[3][1] * w0[3][1] + x[3][2] * w0[3][2] + x[3][3] * w0[3][3] +
		 x[3][4] * w0[3][4];
	y4 = st->y[1];
	st->y[1] = x[2][0] * w1[2][0] + x[2][1] * w1[2][1] + x[2][2] * w1[2][2] +
			   x[2][3] * w1[2][3] + x[2][4] * w1[2][4] - x[3][0] * w1[3][0] -
			   x[3][1] * w1[3][1] - x[3][2] * w1[3][2] - x[3][3] * w1[3][3] -
			   x[3][4] * w1[3][4];
	y2 = x[4][0] * w0[4][0] + x[4][1] * w0[4][1] + x[4][2] * w0[4][2] +
		 x[4][3] * w0[4][3] + x[4][4] * w0[4][4] + x[5][0] * w0[5][0] +
		 x[5][1] * w0[5][1] + x[5][2] * w0[5][2] + x[5][3] * w0[5][3] +
		 x[5][4] * w0[5][4];
	y5 = st->y[2];
	st->y[2] = x[4][0] * w1[4][0] + x[4][1] * w1[4][1] + x[4][2] * w1[4][2] +
			   x[4][3] * w1[4][3] + x[4][4] * w1[4][4] - x[5][0] * w1[5][0] -
			   x[5][1] * w1[5][1] - x[5][2] * w1[5][2] - x[5][3] * w1[5][3] -
			   x[5][4] * w1[5][4];
	y3 = x[6][0] * w0[6][0] + x[6][1] * w0[6][1] + x[6][2] * w0[6][2] +
		 x[6][3] * w0[6][3] + x[6][4] * w0[6][4] + x[7][0] * w0[7][0] +
		 x[7][1] * w0[7][1] + x[7][2] * w0[7][2] + x[7][3] * w0[7][3] +
		 x[7][4] * w0[7][4];
	y6 = st->y[3];
	st->y[3] = x[6][0] * w1[6][0] + x[6][1] * w1[6][1] + x[6][2] * w1[6][2] +
			   x[6][3] * w1[6][3] + x[6][4] * w1[6][4] - x[7][0] * w1[7][0] -
			   x[7][1] * w1[7][1] - x[7][2] * w1[7][2] - x[7][3] * w1[7][3] -
			   x[7][4] * w1[7][4];
	y7 = x[1][0] * w0[1][0] + x[1][1] * w0[1][1] + x[1][2] * w0[1][2] +
		 x[1][3] * w0[1][3] + x[1][4] * w0[1][4];

	y[0] = SBC_SAT16((y0 + (1 << 14)) >> 15);
	y[1] = SBC_SAT16((y1 + (1 << 14)) >> 15);
	y[2] = SBC_SAT16((y2 + (1 << 14)) >> 15);
	y[3] = SBC_SAT16((y3 + (1 << 14)) >> 15);
	y[4] = SBC_SAT16((y4 + (1 << 14)) >> 15);
	y[5] = SBC_SAT16((y5 + (1 << 14)) >> 15);
	y[6] = SBC_SAT16((y6 + (1 << 14)) >> 15);
	y[7] = SBC_SAT16((y7 + (1 << 14)) >> 15);

	st->idx = st->idx < 9 ? st->idx + 1 : 0;

	for (i = 0; i < 8; i++) {
		int s = y[0] * cosmat[i][0] + y[1] * cosmat[i][1] +
				y[2] * cosmat[i][2] + y[3] * cosmat[i][3] +
				y[4] * cosmat[i][4] + y[5] * cosmat[i][5] +
				y[6] * cosmat[i][6] + y[7] * cosmat[i][7];
		*(out++) = SBC_SAT16((s + (1 << 12)) >> 13);
	}
}

void bt_sbc_init(BtSbcEnc* e) {
	memset(e, 0, sizeof *e);
}

unsigned bt_sbc_frame_len(int bitpool) {
	unsigned nbits;
	if (bitpool < 2 || bitpool > 250)
		return 0;
	/* check_frame for joint stereo, 8 subbands, 16 blocks:
	 * max_bits = ((16*8*16) << 1) - 32 - 64 - 8. */
	{
		int max_bits = (16 * 8 * 16) * 2 - 32 - 64 - 8;
		int max_bp = max_bits / 16;
		if (max_bp > 256)
			max_bp = 256;
		if (bitpool > max_bp)
			return 0;
	}
	nbits = 64u + (unsigned)bitpool * 16u + 8u;
	return 4u + ((nbits + 7u) >> 3);
}

int bt_sbc_encode(BtSbcEnc* e, const int16_t* interleaved, int bitpool, int freq,
				  uint8_t* out) {
	int16_t sb[2][SBC_NSAMPLES];
	int scf[2][SBC_NSUBBANDS];
	int nbits[2][SBC_NSUBBANDS];
	const int* loud;
	unsigned mjoint = 0;
	unsigned flen;
	SbcBits bw;
	int ich, isb, iblk;
	int fenc;

	if (!e || !interleaved || !out)
		return -1;
	if (freq != SBC_FREQ_44100 && freq != SBC_FREQ_48000)
		return -1;
	flen = bt_sbc_frame_len(bitpool);
	if (!flen || flen > SBC_MAX_FRAME)
		return -1;
	sbc_crc_init();
	loud = sbc_loud8[freq];

	/* Analysis: 16 blocks per channel straight off the interleaved PCM. */
	for (iblk = 0; iblk < SBC_NBLOCKS; iblk++) {
		sbc_analyze8(&e->ch[0], interleaved + iblk * 16, 2, sb[0] + iblk * 8);
		sbc_analyze8(&e->ch[1], interleaved + iblk * 16 + 1, 2,
					 sb[1] + iblk * 8);
	}

	/* Scale factors + joint decision (L+R / L-R wins). */
	for (isb = 0; isb < SBC_NSUBBANDS; isb++) {
		unsigned m[2] = { 0, 0 }, mj[2] = { 0, 0 };
		int s0, s1, scf0, scf1, js0, js1;
		for (iblk = 0; iblk < SBC_NBLOCKS; iblk++) {
			s0 = sb[0][iblk * 8 + isb];
			s1 = sb[1][iblk * 8 + isb];
			m[0] |= (unsigned)(s0 < 0 ? ~s0 : s0);
			m[1] |= (unsigned)(s1 < 0 ? ~s1 : s1);
			mj[0] |= (unsigned)(s0 + s1 < 0 ? ~(s0 + s1) : s0 + s1);
			mj[1] |= (unsigned)(s0 - s1 < 0 ? ~(s0 - s1) : s0 - s1);
		}
		scf0 = m[0] ? 31 - __builtin_clz(m[0]) : 0;
		scf1 = m[1] ? 31 - __builtin_clz(m[1]) : 0;
		js0 = mj[0] ? 31 - __builtin_clz(mj[0]) : 0;
		js1 = mj[1] ? 31 - __builtin_clz(mj[1]) : 0;
		if (isb < SBC_NSUBBANDS - 1 && js0 + js1 < scf0 + scf1) {
			mjoint |= 1u << (unsigned)isb;
			scf0 = js0;
			scf1 = js1;
		}
		scf[0][isb] = scf0;
		scf[1][isb] = scf1;
	}

	/* Loudness bit allocation (decoder recomputes this identically). */
	{
		int need[2][SBC_NSUBBANDS];
		int maxneed = 0;
		int bc = 0, bitslice;
		for (ich = 0; ich < 2; ich++)
			for (isb = 0; isb < SBC_NSUBBANDS; isb++) {
				int bn, s = scf[ich][isb];
				if (s)
					bn = s - loud[isb];
				else
					bn = -5;
				if (bn > 0)
					bn >>= 1;
				if (bn > maxneed)
					maxneed = bn;
				need[ich][isb] = bn;
			}
		bitslice = maxneed + 1;
		/* NOTE: bitcount tracks the pre-slice total; the two fill loops
		 * below then distribute any remainder. Matches the reference
		 * distribution bit-exactly (decoders recompute it). */
		{
			int bitcount = 0;
			while (bc < bitpool) {
				int bs = bitslice--;
				bitcount = bc;
				if (bitcount == bitpool)
					break;
				for (ich = 0; ich < 2; ich++)
					for (isb = 0; isb < SBC_NSUBBANDS; isb++) {
						int bn = need[ich][isb];
						bc += (bn >= bs && bn < bs + 15) + (bn == bs);
					}
			}
			for (ich = 0; ich < 2; ich++)
				for (isb = 0; isb < SBC_NSUBBANDS; isb++) {
					int nb = need[ich][isb] - bitslice;
					nbits[ich][isb] = nb < 2 ? 0 : nb > 16 ? 16 : nb;
				}
			for (isb = 0; isb < SBC_NSUBBANDS && bitcount < bitpool; isb++)
				for (ich = 0; ich < 2 && bitcount < bitpool; ich++) {
					int n = nbits[ich][isb] && nbits[ich][isb] < 16 ? 1
							: need[ich][isb] == bitslice + 1 &&
									  bitpool > bitcount + 1
								? 2
								: 0;
					nbits[ich][isb] += n;
					bitcount += n;
				}
			for (isb = 0; isb < SBC_NSUBBANDS && bitcount < bitpool; isb++)
				for (ich = 0; ich < 2 && bitcount < bitpool; ich++) {
					int n = (nbits[ich][isb] < 16);
					nbits[ich][isb] += n;
					bitcount += n;
				}
		}
	}

	sbc_bw(&bw, out, flen);
	/* Header. */
	sbc_put(&bw, 0x9c, 8);
	fenc = (freq == SBC_FREQ_48000) ? 3 : 2;
	sbc_put(&bw, (unsigned)fenc, 2);
	sbc_put(&bw, 3, 2); /* 16 blocks */
	sbc_put(&bw, 3, 2); /* joint stereo */
	sbc_put(&bw, 0, 1); /* loudness */
	sbc_put(&bw, 1, 1); /* 8 subbands */
	sbc_put(&bw, (unsigned)bitpool, 8);
	sbc_put(&bw, 0, 8); /* crc placeholder */
	/* Joint mask, subband 7 first. */
	sbc_put(&bw,
			((mjoint & 0x01) << 7) | ((mjoint & 0x02) << 5) |
				((mjoint & 0x04) << 3) | ((mjoint & 0x08) << 1) |
				((mjoint & 0x10) >> 1) | ((mjoint & 0x20) >> 3) |
				((mjoint & 0x40) >> 5),
			8);
	/* Scale factors. */
	for (ich = 0; ich < 2; ich++)
		for (isb = 0; isb < SBC_NSUBBANDS; isb++)
			sbc_put(&bw, (unsigned)scf[ich][isb], 4);

	/* Couple joint subbands. */
	for (isb = 0; isb < SBC_NSUBBANDS; isb++) {
		if (((mjoint >> (unsigned)isb) & 1) == 0)
			continue;
		for (iblk = 0; iblk < SBC_NBLOCKS; iblk++) {
			int16_t s0 = sb[0][iblk * 8 + isb];
			int16_t s1 = sb[1][iblk * 8 + isb];
			sb[0][iblk * 8 + isb] = (int16_t)((s0 + s1) >> 1);
			sb[1][iblk * 8 + isb] = (int16_t)((s0 - s1) >> 1);
		}
	}

	/* Quantize. */
	for (iblk = 0; iblk < SBC_NBLOCKS; iblk++)
		for (ich = 0; ich < 2; ich++)
			for (isb = 0; isb < SBC_NSUBBANDS; isb++) {
				int nb = nbits[ich][isb];
				int sfc = scf[ich][isb];
				int range;
				int s;
				if (!nb)
					continue;
				s = sb[ich][iblk * 8 + isb];
				range = (int)(~(0xffffffffu << (unsigned)nb));
				sbc_put(&bw, (unsigned)(((s * range) >> (sfc + 1)) + range) >> 1,
						nb);
			}
	/* Pad to byte. */
	{
		unsigned used = bw.bit % 8;
		if (used)
			sbc_put(&bw, 0, 8 - (int)used);
	}

	/* CRC over header bytes 1..2 and the scalefactor/joint field. */
	{
		unsigned nbit = 2u * 8u * 4u + 8u;
		unsigned i;
		uint8_t crc = 0x0f;
		crc = sbc_crc_tab[crc ^ out[1]];
		crc = sbc_crc_tab[crc ^ out[2]];
		for (i = 4; i < 4 + nbit / 8; i++)
			crc = sbc_crc_tab[crc ^ out[i]];
		if (nbit % 8)
			crc = (uint8_t)((crc << 4) ^ sbc_crc_tab[(crc >> 4) ^ (out[i] >> 4)]);
		out[3] = crc;
	}
	return (int)flen;
}
