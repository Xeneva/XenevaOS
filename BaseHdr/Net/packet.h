/**
 * AuPacket for netfilter hooks and loopback origin tracking.
 */

#ifndef __AU_PACKET_H__
#define __AU_PACKET_H__

#include <stdint.h>
#include <Fs/vfs.h>

enum {
	AU_PKT_ORIGIN_WIRE = 0,
	AU_PKT_ORIGIN_LOCAL = 1,
	AU_PKT_ORIGIN_STACK = 2
};

enum {
	NF_ACCEPT = 0,
	NF_DROP = 1,
	NF_REJECT = 2,
	NF_NAT_STUB = 3
};

typedef struct _au_packet_ {
	void* data;           /* L3 start (IPv4Header* / IPv6Header*) */
	uint16_t len;
	uint16_t l3_off;
	uint16_t l4_off;
	uint8_t proto;
	uint8_t origin;       /* AU_PKT_ORIGIN_* */
	AuVFSNode* in_dev;
	AuVFSNode* out_dev;
	void* rt;             /* optional AuRouteResult* */
	uint32_t mark;
	int verdict;
} AuPacket;

/*
 * Process-local origin + reinject depth (AA64 loopback).
 * Depth >= 2 stops further local reinject to bound ICMP echo bounce.
 */
extern void AuPacketSetOrigin(uint8_t origin);
extern uint8_t AuPacketGetOrigin(void);
extern int AuPacketLocalEnter(void);  /* returns 0 if depth would exceed cap */
extern void AuPacketLocalLeave(void);
extern int AuPacketLocalDepth(void);

/* Fill a stack AuPacket from an IPv4 header (l4_off from IHL). */
extern void AuPacketInitIpv4(AuPacket* pkt, void* ipv4, uint16_t len,
	AuVFSNode* in_dev, AuVFSNode* out_dev);

#endif
