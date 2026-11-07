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

#ifndef __BT_PARSE_H__
#define __BT_PARSE_H__

#include <stdint.h>

int bt_ad_name(const uint8_t* ad, int len, char* out, int cap);
int bt_hci_cmd(uint8_t* out, uint16_t opcode, const uint8_t* param, uint8_t plen);
int bt_att_read_req(uint8_t* out, uint16_t handle);
int bt_att_read_rsp(const uint8_t* pdu, int len, uint8_t* val, int cap);

/* Minimal SDP client (Service Discovery Protocol) for the A2DP source
 * role, as every desktop initiator does before AVDTP: find the Audio Sink
 * record and read its Protocol Descriptor List (L2CAP PSM + AVDTP
 * version). Plain <stdint.h>, covered by the host unit test.
 *
 * Mirrors BlueZ behaviour (lib/sdp + profiles/audio/a2dp.c):
 *  - ServiceSearchAttributeReq (combined, one round) first, falling back
 *    to separate ServiceSearch + ServiceAttribute reads;
 *  - MaxServiceRecordCount / MaxAttributeByteCount 0xFFFF;
 *  - continuationState loop when a response does not fit one L2CAP MTU
 *    (Sony ULT WEAR fragments its AudioSink record). */
#define SDP_PSM 1
#define SDP_ERR_RSP 0x01
#define SDP_SS_REQ 0x02
#define SDP_SS_RSP 0x03
#define SDP_SA_REQ 0x04
#define SDP_SA_RSP 0x05
#define SDP_SSA_REQ 0x06
#define SDP_SSA_RSP 0x07
#define SDP_UUID_AUDIOSINK 0x110B
#define SDP_UUID_L2CAP     0x0100
#define SDP_UUID_AVDTP     0x0019
#define SDP_ATTR_PROTO_LIST 0x0004

int sdp_build_search(uint8_t* o, uint16_t tid, uint16_t uuid16);
int sdp_build_attr(uint8_t* o, uint16_t tid, uint32_t handle, uint16_t attr);
/* Combined ServiceSearchAttribute request (BlueZ a2dp record query):
 * pattern = single UUID16, attr = single UINT16 id. Returns length.
 * o must hold 18 bytes (parameter length 13, no trailing pad). */
int sdp_build_search_attr(uint8_t* o, uint16_t tid, uint16_t uuid16,
						  uint16_t attr);
/* Continuation helpers (SDP spec 4.2, BlueZ sdp_send_req loop):
 * sdp_cont_get() returns the continuation-state length byte count
 * (0 = complete) or -1 on truncation. The state bytes (if any) start at
 * *state. sdp_cont_append() appends a previous continuation state to a
 * fresh request buffer; returns the new total length. */
int sdp_cont_get(const uint8_t* p, int len, const uint8_t** state);
int sdp_cont_append(uint8_t* o, int clen, const uint8_t* state, int slen);
int sdp_parse_search_rsp(const uint8_t* p, int len, uint16_t tid, uint32_t* handle);
int sdp_parse_attr_psm(const uint8_t* p, int len, uint16_t tid, uint16_t* psm,
					   uint16_t* ver);
/* Scan a combined SSA response for the L2CAP PSM / AVDTP version pair.
 * 0 when found. */
int sdp_parse_ssa_psm(const uint8_t* p, int len, uint16_t tid, uint16_t* psm,
					  uint16_t* ver);

#define BT_BOND_MAGIC 0x31444e42u /* 'BND1' little-endian */

typedef struct _bt_bond_rec_ {
	uint32_t magic;
	uint8_t addr[6];
	uint8_t addr_type;
	uint8_t lesc;
	uint8_t key_size;
	uint8_t irk_ok;
	uint16_t ediv;
	uint8_t rand[8];
	uint8_t ltk[16];
	uint8_t irk[16];
} BtBondRec;

#endif
