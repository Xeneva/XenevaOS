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

#include "parse.h"
#include <string.h>

/* Data-element header: type(5) in the high bits, size index in low 3.
 * Returns the element's value length, or -1. *hdr gets the header size. */
static int sdp_de_len(const uint8_t* p, int len, int* hdr) {
	unsigned long v;
	if (len < 1)
		return -1;
	switch (p[0] & 7) {
	case 0:
		*hdr = 1;
		return 1;
	case 1:
		*hdr = 1;
		return 2;
	case 2:
		*hdr = 1;
		return 4;
	case 3:
		*hdr = 1;
		return 8;
	case 4:
		*hdr = 1;
		return 16;
	case 5:
		if (len < 2)
			return -1;
		*hdr = 2;
		return p[1];
	case 6:
		if (len < 3)
			return -1;
		*hdr = 3;
		v = ((unsigned long)p[1] << 8) | p[2];
		return v > 0x7fffffff ? -1 : (int)v;
	default:
		return -1;
	}
}

int sdp_build_search(uint8_t* o, uint16_t tid, uint16_t uuid16) {
	o[0] = SDP_SS_REQ;
	o[1] = (uint8_t)(tid >> 8);
	o[2] = (uint8_t)tid;
	o[3] = 0;
	o[4] = 8;
	o[5] = 0x35;
	o[6] = 0x03;
	o[7] = 0x19;
	o[8] = (uint8_t)(uuid16 >> 8);
	o[9] = (uint8_t)uuid16;
	/* MaxServiceRecordCount: BlueZ/lib sdp sends 0xFFFF so headsets with
	 * several AudioSink-class records (Sony ULT WEAR exposes A2DP +
	 * AVRCP + HFP records) still answer. The old 0x000A is legal but
	 * some firmware treats small counts as "no room" and stays silent. */
	o[10] = 0xff;
	o[11] = 0xff;
	o[12] = 0;
	return 13;
}

int sdp_build_attr(uint8_t* o, uint16_t tid, uint32_t handle, uint16_t attr) {
	o[0] = SDP_SA_REQ;
	o[1] = (uint8_t)(tid >> 8);
	o[2] = (uint8_t)tid;
	o[3] = 0;
	/* plen 12 + MaxAttributeByteCount 0xFFFF (BlueZ) + cont 0. */
	o[4] = 12;
	o[5] = (uint8_t)(handle >> 24);
	o[6] = (uint8_t)(handle >> 16);
	o[7] = (uint8_t)(handle >> 8);
	o[8] = (uint8_t)handle;
	o[9] = 0xff;
	o[10] = 0xff;
	o[11] = 0x35;
	o[12] = 0x03;
	o[13] = 0x09;
	o[14] = (uint8_t)(attr >> 8);
	o[15] = (uint8_t)attr;
	o[16] = 0;
	return 17;
}

/* Combined search+attr in one PDU (SDP spec 4.7, BlueZ
 * sdp_service_search_attr_req): pattern, MaxAttributeByteCount 0xFFFF,
 * single-UINT16 attribute id list, continuation 0. */
int sdp_build_search_attr(uint8_t* o, uint16_t tid, uint16_t uuid16,
						  uint16_t attr) {
	o[0] = SDP_SSA_REQ;
	o[1] = (uint8_t)(tid >> 8);
	o[2] = (uint8_t)tid;
	o[3] = 0;
	/* 5 (uuid seq) + 2 (max) + 5 (one UINT16 attr) + 1 (empty cont) = 13.
	 * A 14th 0x00 makes the PDU longer than its elements; ULT WEAR drops
	 * it and then ignores the rest of the SDP session. */
	o[4] = 13;
	o[5] = 0x35;
	o[6] = 0x03;
	o[7] = 0x19;
	o[8] = (uint8_t)(uuid16 >> 8);
	o[9] = (uint8_t)uuid16;
	o[10] = 0xff;
	o[11] = 0xff;
	o[12] = 0x35;
	o[13] = 0x03;
	o[14] = 0x09;
	o[15] = (uint8_t)(attr >> 8);
	o[16] = (uint8_t)attr;
	o[17] = 0;
	return 18;
}

int sdp_cont_get(const uint8_t* p, int len, const uint8_t** state) {
	unsigned total, cur, pay;
	if (len < 9 + 1 || !p)
		return -1;
	/* SS_RSP counts handles (4 octets each); SA/SSA_RSP count payload
	 * bytes (SDP spec 4.4.1 vs 4.5.1/4.7.1). Either way the trailing
	 * continuation-length byte follows the payload. */
	total = ((unsigned)p[5] << 8) | p[6];
	cur = ((unsigned)p[7] << 8) | p[8];
	(void)total;
	pay = (p[0] == SDP_SS_RSP) ? cur * 4u : cur;
	if (len < 9 + (int)pay + 1)
		return -1;
	{
		int cl = p[9 + pay];
		if (len < 9 + (int)pay + 1 + cl)
			return -1;
		if (state)
			*state = p + 9 + pay + 1;
		return cl;
	}
}

int sdp_cont_append(uint8_t* o, int clen, const uint8_t* state, int slen) {
	int i;
	if (clen < 1 || !o || slen < 0 || slen > 16)
		return -1;
	/* Last byte of a fresh request is the empty continuation (0x00);
	 * replace it with ContLen + state bytes. ParameterLength counts
	 * those new state bytes (the length byte itself replaces the 0x00). */
	o[clen - 1] = (uint8_t)slen;
	for (i = 0; i < slen; i++)
		o[clen + i] = state[i];
	{
		int plen = ((int)o[3] << 8) | o[4];
		plen += slen;
		o[3] = (uint8_t)(plen >> 8);
		o[4] = (uint8_t)plen;
	}
	return clen + slen;
}

int sdp_parse_search_rsp(const uint8_t* p, int len, uint16_t tid, uint32_t* handle) {
	unsigned total, cur;
	if (len < 9 || p[0] != SDP_SS_RSP || p[1] != (tid >> 8) || p[2] != (tid & 0xff))
		return -1;
	/* TotalServiceRecordCount / CurrentServiceRecordCount are handle
	 * counts (SDP spec 4.4.1), each handle 4 octets, then a trailing
	 * continuation-length byte. */
	total = ((unsigned)p[5] << 8) | p[6];
	cur = ((unsigned)p[7] << 8) | p[8];
	if (total < 1 || cur < 1 || len < 9 + (int)cur * 4 + 1)
		return -1;
	*handle = ((uint32_t)p[9] << 24) | ((uint32_t)p[10] << 16) |
			  ((uint32_t)p[11] << 8) | p[12];
	return 0;
}

/* Scan a ProtocolDescriptorList value for UUID16 + UINT16 pairs:
 * L2CAP (0x0100) gives the PSM, AVDTP (0x0019) the version. p/len cover
 * the list content (inner sequences included); the scan simply finds the
 * 0x19,hi,lo,0x09,hi,lo patterns. */
static int sdp_proto_elem(const uint8_t* p, int len, uint16_t* psm, uint16_t* ver) {
	int i;
	for (i = 0; i + 6 <= len; i++) {
		uint16_t uuid, val;
		if (p[i] != 0x19 || p[i + 3] != 0x09)
			continue;
		uuid = (uint16_t)(p[i + 1] << 8) | p[i + 2];
		val = (uint16_t)(p[i + 4] << 8) | p[i + 5];
		if (uuid == SDP_UUID_L2CAP)
			*psm = val;
		else if (uuid == SDP_UUID_AVDTP)
			*ver = val;
	}
	return (*psm != 0) ? 0 : -1;
}

int sdp_parse_attr_psm(const uint8_t* p, int len, uint16_t tid, uint16_t* psm,
					   uint16_t* ver) {
	int count, off, h, dl;
	*psm = 0;
	*ver = 0;
	if (len < 7 || p[0] != SDP_SA_RSP || p[1] != (tid >> 8) || p[2] != (tid & 0xff))
		return -1;
	count = (p[5] << 8) | p[6];
	if (count <= 0 || 7 + count > len)
		return -1;
	off = 7;
	/* Attribute list is a sequence; walk its elements. */
	if (off >= len || (p[off] >> 3) != 6)
		return -1;
	dl = sdp_de_len(p + off, len - off, &h);
	if (dl < 0)
		return -1;
	off += h;
	while (off + 5 <= 7 + count && off + 5 <= len) {
		/* Attribute ID: UINT16 0x09 len. */
		uint16_t id;
		int vh, vl;
		if (p[off] != 0x09)
			break;
		id = (uint16_t)(p[off + 1] << 8) | p[off + 2];
		off += 3;
		if (off >= len)
			break;
		if ((p[off] >> 3) != 6) {
			/* Scalar value: skip it. */
			vl = sdp_de_len(p + off, len - off, &vh);
			if (vl < 0)
				break;
			off += vh + vl;
			continue;
		}
		vl = sdp_de_len(p + off, len - off, &vh);
		if (vl < 0)
			break;
		if (id == SDP_ATTR_PROTO_LIST &&
			!sdp_proto_elem(p + off + vh, vl, psm, ver))
			return 0;
		off += vh + vl;
	}
	return (*psm != 0) ? 0 : -1;
}

int sdp_parse_ssa_psm(const uint8_t* p, int len, uint16_t tid, uint16_t* psm,
					  uint16_t* ver) {
	int cur;
	*psm = 0;
	*ver = 0;
	if (len < 9 + 1 || p[0] != SDP_SSA_RSP || p[1] != (tid >> 8) ||
		p[2] != (tid & 0xff))
		return -1;
	/* TotalAttributeByteCount / CurrentAttributeByteCount are byte counts
	 * here (spec 4.7.1); payload follows at offset 9. The combined list
	 * already contains the ProtocolDescriptorList, so the same pattern
	 * scan as the split Attr path applies — this is what BlueZ does when
	 * it reads the SSA response for the A2DP PSM. */
	cur = (p[7] << 8) | p[8];
	if (cur <= 0 || 9 + cur + 1 > len)
		return -1;
	return sdp_proto_elem(p + 9, cur, psm, ver);
}

int bt_ad_name(const uint8_t* ad, int len, char* out, int cap) {
	int i = 0;
	int best = 0;
	if (cap <= 0)
		return 0;
	out[0] = 0;
	while (i < len) {
		int fl = ad[i];
		uint8_t type;
		int n, copy, k;
		if (fl == 0 || i + fl >= len)
			break;
		type = ad[i + 1];
		n = fl - 1;
		if ((type == 0x09 || (type == 0x08 && best == 0)) && n > 0) {
			copy = n;
			if (copy > cap - 1)
				copy = cap - 1;
			for (k = 0; k < copy; k++) {
				uint8_t c = ad[i + 2 + k];
				out[k] = (c >= 32 && c < 127) ? (char)c : '.';
			}
			out[copy] = 0;
			best = type == 0x09 ? 2 : 1;
			if (best == 2)
				return copy;
		}
		i += fl + 1;
	}
	return (int)strlen(out);
}

int bt_hci_cmd(uint8_t* out, uint16_t opcode, const uint8_t* param, uint8_t plen) {
	out[0] = (uint8_t)opcode;
	out[1] = (uint8_t)(opcode >> 8);
	out[2] = plen;
	if (plen && param)
		memcpy(out + 3, param, plen);
	return 3 + plen;
}

int bt_att_read_req(uint8_t* out, uint16_t handle) {
	out[0] = 0x0a;
	out[1] = (uint8_t)handle;
	out[2] = (uint8_t)(handle >> 8);
	return 3;
}

int bt_att_read_rsp(const uint8_t* pdu, int len, uint8_t* val, int cap) {
	int n, i;
	if (len < 1 || pdu[0] != 0x0b)
		return -1;
	n = len - 1;
	if (n > cap)
		n = cap;
	for (i = 0; i < n; i++)
		val[i] = pdu[1 + i];
	return n;
}
