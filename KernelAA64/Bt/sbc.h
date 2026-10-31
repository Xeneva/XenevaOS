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

#ifndef __BT_SBC_H__
#define __BT_SBC_H__

#include <stdint.h>

/* Encode-only SBC for A2DP (Bluetooth Advanced Audio Distribution,
 * Appendix B). Fixed production configuration, matching what every A2DP
 * sink (including Sony ULT WEAR) accepts:
 *   joint stereo, 8 subbands, 16 blocks, Loudness allocation,
 *   44.1 kHz or 48 kHz, configurable bitpool (53 = A2DP high quality).
 *
 * One frame carries 16 blocks x 8 subbands = 128 PCM samples per channel.
 * At bitpool 53 a frame is 119 bytes (~328 kbit/s at 44.1 kHz).
 *
 * Filterbank, Loudness offsets, bit allocation, CRC and packing follow the
 * A2DP spec as implemented by google/libsbc (Apache-2.0); only integer
 * <stdint.h>/<string.h> operations are used so this compiles both in the
 * kernel and in the host unit test. */
#define SBC_NBLOCKS   16
#define SBC_NSUBBANDS 8
#define SBC_NSAMPLES  (SBC_NBLOCKS * SBC_NSUBBANDS)
#define SBC_MAX_FRAME 128
#define SBC_FREQ_44100 0
#define SBC_FREQ_48000 1

typedef struct {
	int16_t x[2][SBC_NSUBBANDS][5];
	int32_t y[4];
	int idx;
} BtSbcCh;

typedef struct {
	BtSbcCh ch[2];
} BtSbcEnc;

void bt_sbc_init(BtSbcEnc* e);
/* Frame length in bytes for a bitpool, or 0 when the bitpool is invalid. */
unsigned bt_sbc_frame_len(int bitpool);
/* Encode 128 interleaved stereo frames (256 int16 samples, LRLR...).
 * freq is SBC_FREQ_44100 or SBC_FREQ_48000. Returns frame bytes in out
 * (SBC_MAX_FRAME available), or -1 on bad parameters. */
int bt_sbc_encode(BtSbcEnc* e, const int16_t* interleaved, int bitpool, int freq,
				  uint8_t* out);

#endif
