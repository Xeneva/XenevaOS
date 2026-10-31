/**
 * @file netfilter.c
 * Linear netfilter hooks (iptables-shaped, XR-scale).
 */

#include <Net/netfilter.h>
#include <Net/aunet.h>
#include <Net/ipv4.h>
#include <Net/ipv6.h>
#include <Net/icmp.h>
#include <Net/udp.h>
#include <Mm/kmalloc.h>
#include <string.h>
#include <_null.h>
#include <Drivers/uart.h>

static AuNfRule* _nf_rules[AU_NF_MAX_RULES];
static int _nf_count;

static int AuNfDevMatch(const char* want, AuVFSNode* dev) {
	if (!want || want[0] == '\0')
		return 1;
	if (!dev)
		return 0;
	if (strcmp(want, dev->filename) == 0)
		return 1;
	/* QEMU virtio-net is registered as e1000 plus a virtio-net alias. */
	if (strcmp(want, "virtio-net") == 0 && strcmp(dev->filename, "e1000") == 0)
		return 1;
	if (strcmp(want, "e1000") == 0 && strcmp(dev->filename, "virtio-net") == 0)
		return 1;
	return 0;
}

static uint16_t AuNfGetPort(AuPacket* pkt, int dest) {
	IPv4Header* ip;
	UDPHeader* udp;

	if (!pkt || !pkt->data)
		return 0;
	if (pkt->proto != IPV4_PROTOCOL_UDP && pkt->proto != IPV4_PROTOCOL_TCP)
		return 0;
	ip = (IPv4Header*)pkt->data;
	udp = (UDPHeader*)((uint8_t*)ip + pkt->l4_off);
	return dest ? ntohs(udp->destPort) : ntohs(udp->srcPort);
}

static int AuNfRuleMatches(const AuNfRule* r, AuPacket* pkt) {
	IPv4Header* ip;
	uint16_t sport;
	uint16_t dport;

	if (!r || !pkt || !pkt->data)
		return 0;
	if (r->hook < 0 || r->hook >= NF_NUM_HOOKS)
		return 0;
	if (!AuNfDevMatch(r->in_dev, pkt->in_dev))
		return 0;
	if (!AuNfDevMatch(r->out_dev, pkt->out_dev))
		return 0;
	if (r->proto && r->proto != pkt->proto)
		return 0;

	ip = (IPv4Header*)pkt->data;
	if (r->src_mask && (ip->srcAddress & r->src_mask) != (r->src & r->src_mask))
		return 0;
	if (r->dst_mask && (ip->destAddress & r->dst_mask) != (r->dst & r->dst_mask))
		return 0;

	sport = AuNfGetPort(pkt, 0);
	dport = AuNfGetPort(pkt, 1);
	if (r->sport && r->sport != sport)
		return 0;
	if (r->dport && r->dport != dport)
		return 0;
	return 1;
}

static void AuNfSendReject(AuPacket* pkt) {
	/* RFC 792 Destination Unreachable — best-effort; avoid ICMP about ICMP. */
	if (!pkt || !pkt->data)
		return;
	if (pkt->proto == 1)
		return;
	AuICMPSendDestUnreachable((IPv4Header*)pkt->data, pkt->in_dev ? pkt->in_dev : pkt->out_dev, 3);
}

void AuNetfilterInit(void) {
	int i;

	for (i = 0; i < AU_NF_MAX_RULES; i++)
		_nf_rules[i] = NULL;
	_nf_count = 0;
}

int AuNetfilterAppend(const AuNfRule* rule) {
	AuNfRule* copy;

	if (!rule || _nf_count >= AU_NF_MAX_RULES)
		return -1;
	if (rule->hook < 0 || rule->hook >= NF_NUM_HOOKS)
		return -1;
	copy = (AuNfRule*)kmalloc(sizeof(AuNfRule));
	if (!copy)
		return -1;
	memcpy(copy, rule, sizeof(AuNfRule));
	_nf_rules[_nf_count++] = copy;
	return 0;
}

int AuNetfilterDelete(int index) {
	int i;

	if (index < 0 || index >= _nf_count)
		return -1;
	kfree(_nf_rules[index]);
	for (i = index; i < _nf_count - 1; i++)
		_nf_rules[i] = _nf_rules[i + 1];
	_nf_rules[_nf_count - 1] = NULL;
	_nf_count--;
	return 0;
}

int AuNetfilterFlush(void) {
	int i;

	for (i = 0; i < _nf_count; i++) {
		kfree(_nf_rules[i]);
		_nf_rules[i] = NULL;
	}
	_nf_count = 0;
	return 0;
}

int AuNetfilterGetNum(void) {
	return _nf_count;
}

int AuNetfilterGetEntry(int index, AuNfRule* out) {
	if (!out || index < 0 || index >= _nf_count || !_nf_rules[index])
		return -1;
	memcpy(out, _nf_rules[index], sizeof(AuNfRule));
	return 0;
}

int AuNetfilterHook(int hook, AuPacket* pkt) {
	int i;

	if (!pkt)
		return NF_DROP;
	if (hook < 0 || hook >= NF_NUM_HOOKS)
		return NF_ACCEPT;

	pkt->verdict = NF_ACCEPT;
	for (i = 0; i < _nf_count; i++) {
		AuNfRule* r = _nf_rules[i];
		if (!r || r->hook != hook)
			continue;
		if (!AuNfRuleMatches(r, pkt))
			continue;
		pkt->verdict = r->target;
		if (r->target == NF_DROP)
			return NF_DROP;
		if (r->target == NF_REJECT) {
			AuNfSendReject(pkt);
			return NF_DROP;
		}
		if (r->target == NF_NAT_STUB)
			continue; /* reserved */
		if (r->target == NF_ACCEPT)
			return NF_ACCEPT;
	}
	return pkt->verdict;
}

static void AuNfAddSimple(int hook, const char* in_dev, const char* out_dev,
	uint8_t proto, uint16_t sport, uint16_t dport, int target) {
	AuNfRule r;

	memset(&r, 0, sizeof(r));
	r.hook = hook;
	r.proto = proto;
	r.sport = sport;
	r.dport = dport;
	r.target = target;
	if (in_dev)
		strncpy(r.in_dev, in_dev, AU_NF_IFNAMSIZ - 1);
	if (out_dev)
		strncpy(r.out_dev, out_dev, AU_NF_IFNAMSIZ - 1);
	AuNetfilterAppend(&r);
}

void AuNetfilterInstallXrPolicy(void) {
	/*
	 * Default hook verdict is ACCEPT (covers lo). Do not install a
	 * catch-all ACCEPT on lo — that would shadow later user DROP rules.
	 * Radio INPUT: DHCP + DNS replies, then DROP. FORWARD DROP.
	 */
	AuNfAddSimple(NF_LOCAL_IN, "virtio-net", NULL, IPV4_PROTOCOL_UDP, 0, 67, NF_ACCEPT);
	AuNfAddSimple(NF_LOCAL_IN, "virtio-net", NULL, IPV4_PROTOCOL_UDP, 0, 68, NF_ACCEPT);
	AuNfAddSimple(NF_LOCAL_IN, "virtio-net", NULL, IPV4_PROTOCOL_UDP, 53, 0, NF_ACCEPT);
	/* Echo replies / ICMP errors (no conntrack this month). */
	AuNfAddSimple(NF_LOCAL_IN, "virtio-net", NULL, 1, 0, 0, NF_ACCEPT);
	/* ICMPv6 (echo + NDP). pkt->proto is IPv6 next-header 58, not IPv4 proto 1. */
	AuNfAddSimple(NF_LOCAL_IN, "virtio-net", NULL, IPV6_NEXT_ICMPV6, 0, 0, NF_ACCEPT);
	/* Outbound TCP (SYN-ACK / data) — no conntrack, so accept TCP on INPUT. */
	AuNfAddSimple(NF_LOCAL_IN, "virtio-net", NULL, IPV4_PROTOCOL_TCP, 0, 0, NF_ACCEPT);
	AuNfAddSimple(NF_LOCAL_IN, "virtio-net", NULL, 0, 0, 0, NF_DROP);
	AuNfAddSimple(NF_FORWARD, NULL, NULL, 0, 0, 0, NF_DROP);

	AU_NET_LOG("[aurora]: XR netfilter policy installed (%d rules)\r\n", _nf_count);
}
