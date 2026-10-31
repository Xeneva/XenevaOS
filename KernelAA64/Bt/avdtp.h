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

#ifndef __BT_AVDTP_H__
#define __BT_AVDTP_H__

#include <stdint.h>

/* Minimal AVDTP (Audio/Video Distribution Transport Protocol) for the A2DP
 * source role: talk to a headset sink (Sony ULT WEAR and friends) as
 * BlueZ does — L2CAP to PSM 25 for signaling, DISCOVER, GET_CAPABILITIES,
 * SET_CONFIGURATION (SBC), OPEN, a second L2CAP channel to PSM 25 for the
 * media transport, START, then RTP/SBC media packets. LE Audio (CIS/LC3)
 * is untouched and stays available; production gets both transports.
 * Everything here is plain <stdint.h> so the host unit test covers it. */

/* L2CAP fixed signaling channel + commands we use as initiator. */
#define AVDTP_PSM        25
#define L2CAP_CID_SIGNAL 0x0001
#define L2CAP_CONN_REQ   0x02
#define L2CAP_CONN_RSP   0x03
#define L2CAP_CONF_REQ   0x04
#define L2CAP_CONF_RSP   0x05
#define L2CAP_CONF_SUCCESS 0x0000
#define L2CAP_CONN_OK      0x0000
#define L2CAP_CONN_PENDING 0x0001
#define L2CAP_OPT_MTU      0x01

/* AVDTP header (single packet): byte0 = trans(4)<<4 | ptype(2)<<2 | msg(2). */
#define AVDTP_PKT_SINGLE 0x00
#define AVDTP_MSG_CMD    0x00
#define AVDTP_MSG_ACCEPT 0x02
#define AVDTP_MSG_REJECT 0x03

#define AVDTP_DISCOVER          0x01
#define AVDTP_GET_CAPABILITIES  0x02
#define AVDTP_SET_CONFIGURATION 0x03
#define AVDTP_GET_CONFIGURATION 0x04
#define AVDTP_RECONFIGURE       0x05
#define AVDTP_OPEN              0x06
#define AVDTP_START             0x07
#define AVDTP_CLOSE             0x08
#define AVDTP_SUSPEND           0x09
#define AVDTP_ABORT             0x0A
#define AVDTP_GET_ALLCAPABILITIES 0x0C
#define AVDTP_DELAYREPORT       0x0D

#define AVDTP_SEP_SINK   1
#define AVDTP_MEDIA_AUDIO 0

#define AVDTP_CAT_TRANSPORT 0x01
#define AVDTP_CAT_REPORTING 0x02
#define AVDTP_CAT_RECOVERY  0x03
#define AVDTP_CAT_PROTECTION 0x04
#define AVDTP_CAT_HDRCMPR   0x05
#define AVDTP_CAT_MUX       0x06
#define AVDTP_CAT_CODEC     0x07
/* BlueZ avdtp.h AVDTP_DELAY_REPORTING. Category 0x08, not 0x09. */
#define AVDTP_CAT_DELAYREP  0x08

#define A2DP_CODEC_SBC 0x00
/* SBC codec info, A2DP endian: {freq|mode, blocks|subbands|alloc, min, max}.
 * 0x21 0x15 = 44.1 kHz + joint stereo, 16 blocks, 8 subbands, loudness. */
#define A2DP_SBC_FREQ_44100 0x20
#define A2DP_SBC_FREQ_48000 0x10
#define A2DP_SBC_MODE_JOINT 0x01
#define A2DP_SBC_BLK16      0x10
#define A2DP_SBC_SB8        0x04
#define A2DP_SBC_LOUDNESS   0x01

#define AVDTP_RTP_PT_SBC 96

/* Our fixed offer: 44.1 kHz joint stereo, 16 blocks, 8 subbands, loudness,
 * bitpool 2..53 (A2DP high quality, ~328 kbit/s). */
#define AVDTP_SBC_OFFER_0 (A2DP_SBC_FREQ_44100 | A2DP_SBC_MODE_JOINT)
#define AVDTP_SBC_OFFER_1 (A2DP_SBC_BLK16 | A2DP_SBC_SB8 | A2DP_SBC_LOUDNESS)
#define AVDTP_SBC_BITPOOL 53

/* ---- L2CAP signaling builders (payload after the 4-byte L2CAP header).
 * All return the payload length. */

static inline int l2cap_build_conn_req(uint8_t* o, uint8_t id, uint16_t psm,
									   uint16_t scid) {
	o[0] = L2CAP_CONN_REQ;
	o[1] = id;
	o[2] = 4;
	o[3] = 0;
	o[4] = (uint8_t)psm;
	o[5] = (uint8_t)(psm >> 8);
	o[6] = (uint8_t)scid;
	o[7] = (uint8_t)(scid >> 8);
	return 8;
}

/* Parse a connection response (payload after the L2CAP header:
 * code id len(2) dcid(2) scid(2) result(2) status(2)).
 * Returns 0 on success and fills dcid. */
static inline int l2cap_parse_conn_rsp(const uint8_t* p, int len, uint8_t id,
									   uint16_t* dcid) {
	uint16_t result;
	if (len < 12 || p[0] != L2CAP_CONN_RSP || p[1] != id)
		return -1;
	*dcid = (uint16_t)(p[4] | (p[5] << 8));
	result = (uint16_t)(p[8] | (p[9] << 8));
	return result == L2CAP_CONN_OK ? 0 : -2;
}

/* Raw result word of a connection response (after L2CAP header):
 * L2CAP_CONN_OK (0) or L2CAP_CONN_PENDING (1). Returns -1 when the frame
 * is not a matching ConnRsp. Lets the caller keep waiting on PENDING
 * like BlueZ (l2cap_conn_rsp) instead of failing Sony-style deferred
 * accepts. */
static inline int l2cap_conn_rsp_result(const uint8_t* p, int len, uint8_t id,
										uint16_t* dcid) {
	if (len < 12 || p[0] != L2CAP_CONN_RSP || p[1] != id)
		return -1;
	*dcid = (uint16_t)(p[4] | (p[5] << 8));
	return (int)(uint16_t)(p[8] | (p[9] << 8));
}

static inline int l2cap_build_conf_req(uint8_t* o, uint8_t id, uint16_t dcid,
									   uint16_t mtu) {
	o[0] = L2CAP_CONF_REQ;
	o[1] = id;
	o[2] = 8;
	o[3] = 0;
	o[4] = (uint8_t)dcid;
	o[5] = (uint8_t)(dcid >> 8);
	o[6] = 0;
	o[7] = 0;
	o[8] = L2CAP_OPT_MTU;
	o[9] = 2;
	o[10] = (uint8_t)mtu;
	o[11] = (uint8_t)(mtu >> 8);
	return 12;
}

/* Parse a config response (code id len(2) scid(2) flags(2) result(2)...).
 * Returns 0 when our SCID was accepted. */
static inline int l2cap_parse_conf_rsp(const uint8_t* p, int len, uint8_t id,
									   uint16_t scid) {
	uint16_t got, res;
	if (len < 10 || p[0] != L2CAP_CONF_RSP || p[1] != id)
		return -1;
	got = (uint16_t)(p[4] | (p[5] << 8));
	res = (uint16_t)(p[8] | (p[9] << 8));
	return (got == scid && res == L2CAP_CONF_SUCCESS) ? 0 : -2;
}

/* Config response. scid is the requester's channel id (the peer's),
 * not the Destination CID from their request (that one is our SCID). */

/* Config response carrying our MTU. Returns 14. */
static inline int l2cap_build_conf_rsp_mtu(uint8_t* o, uint8_t id, uint16_t scid,
										   uint16_t mtu) {
	o[0] = L2CAP_CONF_RSP;
	o[1] = id;
	o[2] = 10;
	o[3] = 0;
	o[4] = (uint8_t)scid;
	o[5] = (uint8_t)(scid >> 8);
	o[6] = 0;
	o[7] = 0;
	o[8] = 0;
	o[9] = 0;
	o[10] = L2CAP_OPT_MTU;
	o[11] = 2;
	o[12] = (uint8_t)mtu;
	o[13] = (uint8_t)(mtu >> 8);
	return 14;
}

static inline int l2cap_build_conf_rsp_ok(uint8_t* o, uint8_t id, uint16_t scid) {
	return l2cap_build_conf_rsp_mtu(o, id, scid, 672);
}

/* Parse the peer's MTU out of a config request payload (after the L2CAP
 * header). Returns 0 and fills *mtu when the MTU option is present. */
static inline int l2cap_parse_conf_req_mtu(const uint8_t* p, int len,
										   uint16_t* mtu) {
	int off;
	if (len < 8 || p[0] != L2CAP_CONF_REQ)
		return -1;
	off = 8;
	while (off + 2 <= len) {
		uint8_t type = p[off], l = p[off + 1];
		if (type == L2CAP_OPT_MTU && l == 2 && off + 4 <= len) {
			*mtu = (uint16_t)(p[off + 2] | (p[off + 3] << 8));
			return 0;
		}
		off += 2 + l;
	}
	return -1;
}

/* ---- AVDTP builders. trans is our 4-bit transaction label. ---- */

static inline int avdtp_build_discover(uint8_t* o, uint8_t trans) {
	o[0] = (uint8_t)((trans << 4) | AVDTP_MSG_CMD);
	o[1] = AVDTP_DISCOVER;
	return 2;
}

static inline int avdtp_build_getcaps(uint8_t* o, uint8_t trans, uint8_t seid) {
	o[0] = (uint8_t)((trans << 4) | AVDTP_MSG_CMD);
	o[1] = AVDTP_GET_CAPABILITIES;
	o[2] = (uint8_t)(seid << 2);
	return 3;
}

/* GET_ALLCAPABILITIES (spec 8.9): fallback when a sink rejects
 * GET_CAPABILITIES. Same wire shape, different signal id. */
static inline int avdtp_build_getallcaps(uint8_t* o, uint8_t trans, uint8_t seid) {
	o[0] = (uint8_t)((trans << 4) | AVDTP_MSG_CMD);
	o[1] = AVDTP_GET_ALLCAPABILITIES;
	o[2] = (uint8_t)(seid << 2);
	return 3;
}

/* 1 when a GET_*CAPABILITIES accept lists Delay Reporting (category
 * 0x08, length 0). BlueZ avdtp_set_configuration appends that category
 * when both sides support it. */
static inline int avdtp_caps_have_delay(const uint8_t* p, int len) {
	int off = 2;
	if (len < 4 || (p[0] & 3) != AVDTP_MSG_ACCEPT)
		return 0;
	while (off + 2 <= len) {
		uint8_t cat = p[off], l = p[off + 1];
		if (cat == AVDTP_CAT_DELAYREP && l == 0)
			return 1;
		off += 2 + l;
	}
	return 0;
}

/* caps = transport + SBC codec, plus Delay Reporting when delay is set.
 * Returns length. */
static inline int avdtp_build_setconfig(uint8_t* o, uint8_t trans, uint8_t acp,
										uint8_t intu, uint8_t sbc0, uint8_t sbc1,
										uint8_t minbp, uint8_t maxbp, int delay) {
	o[0] = (uint8_t)((trans << 4) | AVDTP_MSG_CMD);
	o[1] = AVDTP_SET_CONFIGURATION;
	o[2] = (uint8_t)(acp << 2);
	o[3] = (uint8_t)(intu << 2);
	o[4] = AVDTP_CAT_TRANSPORT;
	o[5] = 0;
	o[6] = AVDTP_CAT_CODEC;
	o[7] = 6;
	o[8] = (uint8_t)(AVDTP_MEDIA_AUDIO << 4);
	o[9] = A2DP_CODEC_SBC;
	o[10] = sbc0;
	o[11] = sbc1;
	o[12] = minbp;
	o[13] = maxbp;
	if (!delay)
		return 14;
	o[14] = AVDTP_CAT_DELAYREP;
	o[15] = 0;
	return 16;
}

static inline int avdtp_build_open(uint8_t* o, uint8_t trans, uint8_t seid) {
	o[0] = (uint8_t)((trans << 4) | AVDTP_MSG_CMD);
	o[1] = AVDTP_OPEN;
	o[2] = (uint8_t)(seid << 2);
	return 3;
}

static inline int avdtp_build_start(uint8_t* o, uint8_t trans, uint8_t seid) {
	o[0] = (uint8_t)((trans << 4) | AVDTP_MSG_CMD);
	o[1] = AVDTP_START;
	o[2] = (uint8_t)(seid << 2);
	return 3;
}

static inline int avdtp_build_suspend(uint8_t* o, uint8_t trans, uint8_t seid) {
	o[0] = (uint8_t)((trans << 4) | AVDTP_MSG_CMD);
	o[1] = AVDTP_SUSPEND;
	o[2] = (uint8_t)(seid << 2);
	return 3;
}

static inline int avdtp_build_close(uint8_t* o, uint8_t trans, uint8_t seid) {
	o[0] = (uint8_t)((trans << 4) | AVDTP_MSG_CMD);
	o[1] = AVDTP_CLOSE;
	o[2] = (uint8_t)(seid << 2);
	return 3;
}

/* 0 = accept, nonzero = reject/error for our transaction label. */
static inline int avdtp_is_accept(const uint8_t* p, int len, uint8_t trans,
								  uint8_t signal) {
	if (len < 2)
		return -1;
	if ((p[0] >> 4) != trans || p[1] != signal)
		return -1;
	return ((p[0] & 3) == AVDTP_MSG_ACCEPT) ? 0 : 1;
}

/* DISCOVER accept payload is a packed list of 2-byte SEID records
 * (BlueZ struct seid_info). There is no count byte. p includes the
 * 2-byte AVDTP header. Writes unused audio-sink SEIDs in wire order.
 * Returns the count, or -1 if the PDU is not an accept. */
static inline int avdtp_list_sink_seids(const uint8_t* p, int len,
										uint8_t* out, int max) {
	int nrec, i, n = 0;
	if (len < 4 || (p[0] & 3) != AVDTP_MSG_ACCEPT || max < 1)
		return -1;
	nrec = (len - 2) / 2;
	for (i = 0; i < nrec && n < max; i++) {
		const uint8_t* e = p + 2 + i * 2;
		int seid = e[0] >> 2;
		int inuse = (e[0] >> 1) & 1;
		int type = (e[1] >> 3) & 1;
		int media = e[1] >> 4;
		if (!inuse && type == AVDTP_SEP_SINK && media == AVDTP_MEDIA_AUDIO &&
			seid >= 1 && seid <= 62)
			out[n++] = (uint8_t)seid;
	}
	return n;
}

/* First unused audio sink in a DISCOVER accept, or -1. */
static inline int avdtp_pick_sink_seid(const uint8_t* p, int len) {
	uint8_t seid;
	int n = avdtp_list_sink_seids(p, len, &seid, 1);
	if (n <= 0)
		return -1;
	return seid;
}

/* Media codec type from a GET_CAPABILITIES / GET_ALL_CAPABILITIES accept.
 * A2DP SBC is 0x00, MPEG-2/4 AAC is 0x02. -1 when the category is absent. */
static inline int avdtp_media_codec(const uint8_t* p, int len) {
	int off = 2;
	if (len < 2 || (p[0] & 3) != AVDTP_MSG_ACCEPT)
		return -1;
	while (off + 2 <= len) {
		uint8_t cat = p[off], l = p[off + 1];
		if (cat == AVDTP_CAT_CODEC && l >= 2 && off + 4 <= len)
			return p[off + 3];
		off += 2 + l;
	}
	return -1;
}

/* Parse a GET_CAPABILITIES accept into SBC codec bytes. p includes header.
 * Fills sbc[4] with the sink's SBC capability bytes. 0 on success. */
static inline int avdtp_parse_sbc_caps(const uint8_t* p, int len, uint8_t* sbc) {
	int off = 2;
	if (len < 2 || (p[0] & 3) != AVDTP_MSG_ACCEPT)
		return -1;
	while (off + 2 <= len) {
		uint8_t cat = p[off], l = p[off + 1];
		if (cat == AVDTP_CAT_CODEC && l >= 6 && off + 2 + 6 <= len &&
			(p[off + 2] >> 4) == AVDTP_MEDIA_AUDIO && p[off + 3] == A2DP_CODEC_SBC) {
			sbc[0] = p[off + 4];
			sbc[1] = p[off + 5];
			sbc[2] = p[off + 6];
			sbc[3] = p[off + 7];
			return 0;
		}
		off += 2 + l;
	}
	return -1;
}

/* Intersect our fixed offer with sink caps. Fills use[4]. Prefer 48 kHz
 * when the sink offers it: Deodhai mixes at 48000, and a 44.1 kHz SBC
 * stream of that PCM is the wrong clock (headset stays quiet or plays
 * at the wrong speed). 44.1 kHz is the fallback. Bitpool min and max are
 * the single selected value, which is what BlueZ puts in SetConfiguration. */
static inline int avdtp_pick_sbc_config(const uint8_t* caps, uint8_t* use,
										int* freq44100) {
	uint8_t f = caps[0];
	uint8_t b = caps[1];
	use[0] = 0;
	use[1] = 0;
	if (f & A2DP_SBC_FREQ_48000) {
		use[0] |= A2DP_SBC_FREQ_48000;
		*freq44100 = 0;
	} else if (f & A2DP_SBC_FREQ_44100) {
		use[0] |= A2DP_SBC_FREQ_44100;
		*freq44100 = 1;
	} else {
		return -1;
	}
	/* Joint stereo required by our encoder. */
	if (!(f & A2DP_SBC_MODE_JOINT))
		return -1;
	use[0] |= A2DP_SBC_MODE_JOINT;
	/* 16 blocks, 8 subbands, loudness (encoder-fixed). */
	if (!(b & A2DP_SBC_BLK16) || !(b & A2DP_SBC_SB8) || !(b & A2DP_SBC_LOUDNESS))
		return -1;
	use[1] = (uint8_t)(A2DP_SBC_BLK16 | A2DP_SBC_SB8 | A2DP_SBC_LOUDNESS);
	/* Bitpool inside the sink range, capped at our HQ 53. */
	{
		int lo = caps[2], hi = caps[3], bp = AVDTP_SBC_BITPOOL;
		if (hi < lo)
			return -1;
		if (bp > hi)
			bp = hi;
		if (bp < lo)
			bp = lo;
		if (bp < 2)
			return -1;
		use[2] = (uint8_t)bp;
		use[3] = (uint8_t)bp;
	}
	return 0;
}

/* RTP header (12 bytes) + 1-byte SBC frame count. Returns 13. */
static inline int avdtp_build_media_hdr(uint8_t* o, uint16_t seq, uint32_t ts,
										uint32_t ssrc, uint8_t nframes) {
	o[0] = 0x80;
	o[1] = AVDTP_RTP_PT_SBC;
	o[2] = (uint8_t)(seq >> 8);
	o[3] = (uint8_t)seq;
	o[4] = (uint8_t)(ts >> 24);
	o[5] = (uint8_t)(ts >> 16);
	o[6] = (uint8_t)(ts >> 8);
	o[7] = (uint8_t)ts;
	o[8] = (uint8_t)(ssrc >> 24);
	o[9] = (uint8_t)(ssrc >> 16);
	o[10] = (uint8_t)(ssrc >> 8);
	o[11] = (uint8_t)ssrc;
	o[12] = nframes;
	return 13;
}

#endif
