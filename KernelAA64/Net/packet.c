/**
 * @file packet.c
 * AuPacket helpers + loopback origin/depth.
 */

#include <Net/packet.h>
#include <Net/ipv4.h>
#include <Net/aunet.h>
#include <string.h>

static uint8_t au_pkt_origin = AU_PKT_ORIGIN_WIRE;
static int au_pkt_local_depth = 0;

#define AU_PKT_LOCAL_DEPTH_CAP 8

void AuPacketSetOrigin(uint8_t origin) {
	au_pkt_origin = origin;
}

uint8_t AuPacketGetOrigin(void) {
	return au_pkt_origin;
}

int AuPacketLocalEnter(void) {
	if (au_pkt_local_depth >= AU_PKT_LOCAL_DEPTH_CAP)
		return 0;
	au_pkt_local_depth++;
	au_pkt_origin = AU_PKT_ORIGIN_LOCAL;
	return 1;
}

void AuPacketLocalLeave(void) {
	if (au_pkt_local_depth > 0)
		au_pkt_local_depth--;
	if (au_pkt_local_depth == 0)
		au_pkt_origin = AU_PKT_ORIGIN_WIRE;
}

int AuPacketLocalDepth(void) {
	return au_pkt_local_depth;
}

void AuPacketInitIpv4(AuPacket* pkt, void* ipv4, uint16_t len,
	AuVFSNode* in_dev, AuVFSNode* out_dev) {
	IPv4Header* ip;
	uint8_t ihl;

	memset(pkt, 0, sizeof(AuPacket));
	if (!pkt || !ipv4)
		return;
	ip = (IPv4Header*)ipv4;
	ihl = (uint8_t)((ip->versionHeaderLen & 0x0F) * 4);
	if (ihl < 20)
		ihl = 20;
	pkt->data = ipv4;
	pkt->len = len ? len : ntohs(ip->totalLength);
	pkt->l3_off = 0;
	pkt->l4_off = ihl;
	pkt->proto = ip->protocol;
	pkt->origin = AuPacketGetOrigin();
	pkt->in_dev = in_dev;
	pkt->out_dev = out_dev;
	pkt->verdict = NF_ACCEPT;
}
