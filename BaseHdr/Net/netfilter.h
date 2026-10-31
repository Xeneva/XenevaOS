/**
 * Netfilter-style hooks for Xeneva (linear rule list, XR-scale).
 */

#ifndef __AU_NETFILTER_H__
#define __AU_NETFILTER_H__

#include <stdint.h>
#include <Net/packet.h>

enum {
	NF_PRE_ROUTING = 0,
	NF_LOCAL_IN = 1,
	NF_FORWARD = 2,
	NF_LOCAL_OUT = 3,
	NF_POST_ROUTING = 4,
	NF_NUM_HOOKS = 5
};

#define AU_NF_MAX_RULES 64
#define AU_NF_IFNAMSIZ  16

typedef struct _au_nf_rule_ {
	int hook;                 /* NF_* */
	char in_dev[AU_NF_IFNAMSIZ];
	char out_dev[AU_NF_IFNAMSIZ];
	uint8_t proto;            /* 0 = any; IPPROTOCOL_* / IPV4_PROTOCOL_* */
	uint32_t src;
	uint32_t src_mask;
	uint32_t dst;
	uint32_t dst_mask;
	uint16_t sport;           /* host order; 0 = any */
	uint16_t dport;           /* host order; 0 = any */
	int target;               /* NF_ACCEPT / DROP / REJECT / NAT_STUB */
} AuNfRule;

typedef struct _au_nf_rule_info_ {
	int index;
	AuNfRule* rule;
} AuNfRuleInfo;

extern void AuNetfilterInit(void);
extern void AuNetfilterInstallXrPolicy(void);
extern int AuNetfilterHook(int hook, AuPacket* pkt);

extern int AuNetfilterAppend(const AuNfRule* rule);
extern int AuNetfilterDelete(int index);
extern int AuNetfilterFlush(void);
extern int AuNetfilterGetNum(void);
extern int AuNetfilterGetEntry(int index, AuNfRule* out);

#endif
