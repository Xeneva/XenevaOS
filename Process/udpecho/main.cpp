/**
 * Weekly localhost UDP self-test: bind 127.0.0.1, sendto self, recv, compare.
 * Requires kernel loopback / local delivery (notes/loopback-local-delivery.md).
 */

#include <stdint.h>
#include <_xeneva.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/_keproc.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define DEFAULT_PORT 7777
#define PAYLOAD "xeneva-lo-udp"
#define TIMEOUT_ITERS 2000

static int is_prog(const char* s) {
	if (!s || !s[0])
		return 1;
	if (s[0] == '/')
		return 1;
	if (strstr(s, ".exe"))
		return 1;
	return 0;
}

static void usage(void) {
	printf("usage: udpecho [addr] [port]\n");
	printf("  udpecho\n");
	printf("  udpecho 127.0.0.1 7777\n");
	printf("Bind UDP to addr:port, sendto self, expect echo of payload.\n");
}

int main(int argc, char* argv[]) {
	const char* addr_str = "127.0.0.1";
	uint16_t port = DEFAULT_PORT;
	int sock;
	sockaddr_in bind_addr;
	sockaddr_in dest;
	sockaddr_in from;
	socklen_t fromlen;
	char buf[256];
	ssize_t n;
	int i;
	int argi = 0;

	for (i = 0; i < argc; i++) {
		if (is_prog(argv[i]))
			continue;
		if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			usage();
			return 0;
		}
		if (argi == 0) {
			addr_str = argv[i];
			argi++;
		} else if (argi == 1) {
			port = (uint16_t)atoi(argv[i]);
			argi++;
		}
	}

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		printf("udpecho: socket failed\n");
		return 1;
	}

	memset(&bind_addr, 0, sizeof(bind_addr));
	bind_addr.sin_family = AF_INET;
	bind_addr.sin_port = htons(port);
	/* Match ping: htonl(inet_addr) so kernel AuNetworkRoute sees host-order 127/8. */
	bind_addr.sin_addr.s_addr = htonl(inet_addr(addr_str));

	if (bind(sock, (sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
		printf("udpecho: bind %s:%u failed\n", addr_str, (unsigned)port);
		return 1;
	}

	memset(&dest, 0, sizeof(dest));
	dest.sin_family = AF_INET;
	dest.sin_port = htons(port);
	dest.sin_addr.s_addr = bind_addr.sin_addr.s_addr;

	if (sendto(sock, (void*)PAYLOAD, strlen(PAYLOAD), 0, (sockaddr*)&dest, sizeof(dest)) < 0) {
		printf("udpecho: sendto failed\n");
		return 1;
	}

	memset(buf, 0, sizeof(buf));
	fromlen = sizeof(from);
	for (i = 0; i < TIMEOUT_ITERS; i++) {
		n = recvfrom(sock, buf, sizeof(buf) - 1, 0, (sockaddr*)&from, &fromlen);
		if (n > 0)
			break;
		_KeProcessSleep(1);
	}

	if (n <= 0) {
		printf("udpecho: timeout waiting for localhost delivery\n");
		return 1;
	}

	buf[n] = 0;
	if ((size_t)n != strlen(PAYLOAD) || memcmp(buf, PAYLOAD, (size_t)n) != 0) {
		printf("udpecho: mismatch got '%s' (%d bytes)\n", buf, (int)n);
		return 1;
	}

	printf("udpecho: ok %s:%u (%d bytes)\n", addr_str, (unsigned)port, (int)n);
	fflush(stdout);
	return 0;
}
