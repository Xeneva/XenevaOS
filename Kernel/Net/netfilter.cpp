/**
 * x86 stub: netfilter hooks ACCEPT everything.
 */

#include <Net/netfilter.h>
#include <string.h>

void AuNetfilterInit(void) {}
void AuNetfilterInstallXrPolicy(void) {}

int AuNetfilterHook(int hook, AuPacket* pkt) {
	(void)hook;
	if (pkt)
		pkt->verdict = NF_ACCEPT;
	return NF_ACCEPT;
}

int AuNetfilterAppend(const AuNfRule* rule) {
	(void)rule;
	return -1;
}

int AuNetfilterDelete(int index) {
	(void)index;
	return -1;
}

int AuNetfilterFlush(void) {
	return 0;
}

int AuNetfilterGetNum(void) {
	return 0;
}

int AuNetfilterGetEntry(int index, AuNfRule* out) {
	(void)index;
	(void)out;
	return -1;
}
