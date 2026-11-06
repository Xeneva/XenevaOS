/*
 * Process/nettest/main.cpp -- network liveness probe.
 *
 * Sends one ICMP echo to the QEMU slirp gateway every two seconds and logs
 * "[nettest] seq N reply" or "[nettest] seq N timeout" to the serial log
 * (via _KePrint), so a boot test can watch the network come up, survive a
 * driver-server crash, and come back. An ordinary application: it only needs
 * the network group, no driver privileges.
 */
#include <stdint.h>
#include <_xeneva.h>
#include <stdio.h>
#include <sys/_keproc.h>
#include <sys/_kefile.h>
#include <sys/_ketime.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/iocodes.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define ECHO_BYTES 64

struct IcmpHdr {
	uint8_t type, code;
	uint16_t checksum;
	uint16_t identifier;
	uint16_t sequenceNum;
	uint8_t payload[];
};

static uint16_t checksum(char* payload, size_t len) {
	uint32_t sum = 0;
	uint16_t* s = (uint16_t*)payload;
	for (size_t i = 0; i < len / 2; ++i)
		sum += ntohs(s[i]);
	if (sum > UINT16_MAX)
		sum = (sum >> 16) + (sum & UINT16_MAX);
	return ~(sum & UINT16_MAX) & UINT16_MAX;
}

int main(int argc, char* argv[]) {
	(void)argc; (void)argv;
	/* sleep accuracy: _KeProcessSleep(ms) must last about ms of wall-clock time */
	{
		static const unsigned ms[3] = {100, 500, 2000};
		for (int i = 0; i < 3; i++) {
			uint64_t t0 = _KeGetCurrentUS();
			_KeProcessSleep(ms[i]);
			uint64_t dt = (_KeGetCurrentUS() - t0) / 1000;
			_KePrint("[nettest] sleep(%d ms) took %d ms\r\n", (int)ms[i], (int)dt);
		}
	}
	sockaddr_in dest;
	memset(&dest, 0, sizeof(dest));
	dest.sin_family = AF_INET;
	dest.sin_addr.s_addr = inet_addr("10.0.2.2");
	IcmpHdr* ping = (IcmpHdr*)malloc(ECHO_BYTES);
	char* data = (char*)malloc(4096);
	_KePrint("[nettest] started, probing 10.0.2.2\r\n");
	for (unsigned seq = 1;; seq++) {
		/* the kernel has exactly one ICMP socket for the whole system, so I only hold it
		 * for the length of one probe; a long-lived socket here makes the shell's own
		 * ping fail with "failed to create socket" --axiss */
		int sock = socket(AF_INET, SOCK_DGRAM, IPPROTOCOL_ICMP);
		if (sock < 0) {
			_KePrint("[nettest] seq %d busy (ICMP socket in use)\r\n", (int)seq);
			sleep(2);
			continue;
		}
		memset(ping, 0, ECHO_BYTES);
		ping->type = 8;
		ping->identifier = htons(0x4E54);
		ping->sequenceNum = htons((uint16_t)seq);
		for (int i = 0; i < ECHO_BYTES - 8; ++i)
			ping->payload[i] = (uint8_t)i;
		ping->checksum = htons(checksum((char*)ping, ECHO_BYTES));
		int sent = sendto(sock, (void*)ping, ECHO_BYTES, 0, (sockaddr*)&dest, sizeof(sockaddr_in));
		int got = 0;
		if (sent >= 0) {
			for (int t = 0; t < 1000 && !got; t++) {
				sockaddr_in src;
				socklen_t sz = sizeof(sockaddr_in);
				ssize_t len = recvfrom(sock, data, 4096, 0, (sockaddr*)&src, &sz);
				if (len > 0) {
					IcmpHdr* r = (IcmpHdr*)data;
					if (r->type == 0 && ntohs(r->sequenceNum) == seq)
						got = 1;
				}
				if (!got)
					_KeProcessSleep(1);
			}
		}
		_KeCloseFile(sock);
		_KePrint("[nettest] seq %d %s\r\n", (int)seq, got ? "reply" : (sent < 0 ? "send-failed" : "timeout"));
		sleep(2);
	}
	return 0;
}
