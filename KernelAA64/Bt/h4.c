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

#include "h4.h"
#include <string.h>

void bt_h4_reset(BtH4* h) {
	memset(h, 0, sizeof *h);
}

static int payload_len(uint8_t kind, const uint8_t* hdr, uint16_t got) {
	if (kind == H4_CMD && got >= 3)
		return 3 + hdr[2];
	if (kind == H4_SCO && got >= 3)
		return 3 + hdr[2];
	if (kind == H4_EVT && got >= 2)
		return 2 + hdr[1];
	if ((kind == H4_ACL || kind == H4_ISO) && got >= 4)
		return 4 + (hdr[2] | (hdr[3] << 8));
	return 0;
}

int bt_h4_feed(BtH4* h, const uint8_t* bytes, size_t n, uint8_t* kind, uint8_t* out, uint16_t* outlen,
			   uint16_t cap) {
	size_t i;
	for (i = 0; i < n; i++) {
		uint8_t b = bytes[i];
		if (h->kind == 0) {
			if (b != H4_CMD && b != H4_ACL && b != H4_SCO && b != H4_EVT && b != H4_ISO)
				continue;
			h->kind = b;
			h->got = 0;
			h->need = 0;
			continue;
		}
		if (h->got < sizeof h->buf)
			h->buf[h->got++] = b;
		if (h->need == 0) {
			int pl = payload_len(h->kind, h->buf, h->got);
			if (pl <= 0)
				continue;
			if (pl > (int)sizeof h->buf) {
				bt_h4_reset(h);
				continue;
			}
			h->need = (uint16_t)pl;
		}
	if (h->need && h->got >= h->need) {
		if (h->need <= cap) {
			*kind = h->kind;
			*outlen = h->got;
			memcpy(out, h->buf, h->got);
			bt_h4_reset(h);
			return 1;
		}
		/* larger than the caller's buffer: drop the frame whole but keep
		 * the byte stream in sync -- copying it overran callers' out[512]
		 * and smashed the bthost kernel stack (ret-to-garbage) */
		bt_h4_reset(h);
	}
	}
	return 0;
}

int bt_h4_wrap(uint8_t kind, const uint8_t* payload, uint16_t len, uint8_t* out, uint16_t cap) {
	if ((uint32_t)len + 1 > cap)
		return -1;
	out[0] = kind;
	memcpy(out + 1, payload, len);
	return (int)len + 1;
}
