/**
 * Telnet client for XenevaOS (terminal app).
 *
 * Usage: telnet <host> [port]   (default port 23, host:port accepted)
 *
 * Minimal NVT: replies WONT to every DO and DONT to every WILL, except
 * SGA (we WILL it, we already send char-at-a-time) and TTYPE (we WILL it
 * and answer "VT100", matching the Xeneva Terminal's ANSI/VT100 support).
 * Ctrl+] quits. Close detection relies on AuTCPReceive returning 0 on
 * FIN/close and -1 when the RX queue is empty.
 */

#include <stdint.h>
#include <_xeneva.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/_keproc.h>
#include <sys/_kefile.h>
#include <sys/_kesignal.h>
#include <sys/socket.h>
#include <sys/netdb.h>
#include <arpa/inet.h>

#define TEL_IAC	  255
#define TEL_DONT  254
#define TEL_DO	  253
#define TEL_WONT  252
#define TEL_WILL  251
#define TEL_SB	  250
#define TEL_SE	  240
#define TEL_TTYPE 24
#define TEL_SGA	  3
#define TEL_SEND  1
#define TEL_IS	  0

static int s_sock = -1;
static volatile int s_open = 0;
static sockaddr_in s_dest;

/* The terminal eats Ctrl+C itself (SIGINT to the foreground job, which
 * xesh forwards here), so it can never travel in-band to the server.
 * Treat it as a graceful local quit, same as Ctrl+]. */
static void TelSigInt(int signo) {
	(void)signo;
	const char* msg = "\r\n[telnet: interrupted, closing]\r\n";
	s_open = 0;
	if (s_sock >= 0)
		_KeCloseFile(s_sock);
	_KeWriteFile(XENEVA_STDOUT, (void*)msg, strlen(msg));
	_KeProcessExit();
}

static void NsSendRaw(const uint8_t* buf, size_t len) {
	if (!s_open || s_sock < 0 || len == 0)
		return;
	sendto(s_sock, buf, len, 0, (sockaddr*)&s_dest, sizeof(s_dest));
}

static void NsSendCmd(uint8_t cmd, uint8_t opt) {
	uint8_t b[3] = {TEL_IAC, cmd, opt};
	NsSendRaw(b, 3);
}

/* Keyboard -> network. Runs on a helper thread; Ctrl+] quits. */
static void TelInputThread(void) {
	uint8_t c;
	while (s_open) {
		memset(&c, 0, 1);
		int n = _KeReadFile(XENEVA_STDIN, &c, 1);
		if (n <= 0) {
			_KeProcessSleep(10);
			continue;
		}
		if (c == 0x1D) { /* Ctrl+] */
			const char* msg = "\r\n[telnet: quit]\r\n";
			_KeWriteFile(XENEVA_STDOUT, (void*)msg, strlen(msg));
			s_open = 0;
			_KeCloseFile(s_sock);
			return;
		}
		if (c == TEL_IAC) {
			uint8_t esc[2] = {TEL_IAC, TEL_IAC};
			NsSendRaw(esc, 2);
		} else {
			NsSendRaw(&c, 1);
		}
	}
}

static void NsHandleServer(const uint8_t* buf, int len) {
	/* Small output buffer: IAC sequences collapse, the rest passes through. */
	static uint8_t out[2048];
	size_t oi = 0;
	int i = 0;
	while (i < len) {
		uint8_t c = buf[i];
		if (c != TEL_IAC) {
			if (oi < sizeof(out))
				out[oi++] = c;
			i++;
			continue;
		}
		/* IAC sequence */
		if (i + 1 >= len)
			break;
		uint8_t cmd = buf[i + 1];
		if (cmd == TEL_IAC) {
			if (oi < sizeof(out))
				out[oi++] = TEL_IAC;
			i += 2;
		} else if (cmd == TEL_DO || cmd == TEL_DONT || cmd == TEL_WILL || cmd == TEL_WONT) {
			if (i + 2 >= len)
				break;
			uint8_t opt = buf[i + 2];
			if (cmd == TEL_DO) {
				if (opt == TEL_SGA || opt == TEL_TTYPE)
					NsSendCmd(TEL_WILL, opt);
				else
					NsSendCmd(TEL_WONT, opt);
			} else if (cmd == TEL_WILL) {
				NsSendCmd(TEL_DONT, opt);
			}
			/* DONT/WONT need no reply. */
			i += 3;
		} else if (cmd == TEL_SB) {
			/* Subnegotiation: look for IAC SE, answer TTYPE SEND. */
			int j = i + 2;
			while (j + 1 < len && !(buf[j] == TEL_IAC && buf[j + 1] == TEL_SE))
				j++;
			if (j + 1 >= len)
				break; /* split across reads; drop (rare, harmless) */
			if (i + 2 < len && buf[i + 2] == TEL_TTYPE && i + 3 < len && buf[i + 3] == TEL_SEND) {
				static const uint8_t reply[] = {
					TEL_IAC, TEL_SB, TEL_TTYPE, TEL_IS, 'V', 'T', '1', '0', '0', TEL_IAC, TEL_SE};
				NsSendRaw(reply, sizeof(reply));
			}
			i = j + 2;
		} else {
			/* EOR, BRK, NOP, etc: ignore. */
			i += 2;
		}
	}
	if (oi > 0)
		_KeWriteFile(XENEVA_STDOUT, out, oi);
}

static void usage(void) {
	printf("Usage: telnet <host> [port]\n");
	printf("  telnet towel.blinkenlights.nl\n");
	printf("  telnet example.com 23\n");
	printf("\nCtrl+C or Ctrl+] quits (the terminal reserves Ctrl+C,\n"
		   "so it cannot be sent to the server). No TLS/SSH.\n");
}

int main(int argc, char* argv[]) {
	printf("telnet started \r\n");
	const char* host = NULL;
	uint16_t port = 23;
	char hostbuf[128];
	memset(hostbuf, 0, sizeof(hostbuf));

	for (int i = 0; i < argc; i++) {
		const char* a = argv[i];
		if (!a || !a[0])
			continue;
		if (a[0] == '/')
			continue;
		if (strstr(a, ".exe"))
			continue;
		if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
			usage();
			return 0;
		}
		if (a[0] == '-') {
			printf("telnet: unknown option %s\n", a);
			return 2;
		}
		if (!host) {
			const char* colon = strchr(a, ':');
			if (colon && colon != a) {
				size_t hlen = (size_t)(colon - a);
				if (hlen >= sizeof(hostbuf)) {
					printf("telnet: hostname too long\n");
					return 2;
				}
				memcpy(hostbuf, (void*)a, hlen);
				hostbuf[hlen] = 0;
				host = hostbuf;
				int prt = 0;
				for (const char* d = colon + 1; *d >= '0' && *d <= '9'; d++)
					prt = prt * 10 + (*d - '0');
				if (prt > 0 && prt < 65536)
					port = (uint16_t)prt;
			} else {
				host = a;
			}
			continue;
		}
		/* second positional arg: port */
		{
			int prt = 0;
			const char* d = a;
			while (*d >= '0' && *d <= '9') {
				prt = prt * 10 + (*d - '0');
				d++;
			}
			if (*d == 0 && prt > 0 && prt < 65536)
				port = (uint16_t)prt;
		}
	}

	if (!host) {
		usage();
		return 2;
	}

	hostent* ent = gethostbyname(host);
	if (!ent) {
		printf("telnet: could not resolve %s\n", host);
		return 6;
	}
	if (ent->h_addrtype != AF_INET || ent->h_length != 4 || !ent->h_addr_list[0]) {
		printf("telnet: no IPv4 address for %s\n", host);
		return 6;
	}
	uint32_t ipaddr = *(uint32_t*)ent->h_addr_list[0];

	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		printf("telnet: socket failed\n");
		return 7;
	}

	memset(&s_dest, 0, sizeof(s_dest));
	s_dest.sin_family = AF_INET;
	s_dest.sin_port = htons(port);
	memcpy(&s_dest.sin_addr, &ipaddr, sizeof(uint32_t));

	if (connect(sock, (sockaddr_*)&s_dest, sizeof(s_dest)) < 0) {
		printf("telnet: connect to %s:%u failed\n", host, port);
		_KeCloseFile(sock);
		return 7;
	}

	s_sock = sock;
	s_open = 1;
	_KeSetSignal(SIGINT, TelSigInt);
	printf("Connected to %s:%u. Ctrl+C or Ctrl+] quits.\n", host, port);

	_KeCreateThread(TelInputThread, (char*)"telin");

	uint8_t buf[2048];
	while (s_open) {
		memset(buf, 0, sizeof(buf));
		int n = recvfrom(sock, buf, sizeof(buf) - 1, 0, NULL, NULL);
		if (n > 0) {
			NsHandleServer(buf, n);
			continue;
		}
		if (n == 0) {
			printf("\r\n[connection closed by remote]\r\n");
			break;
		}
		_KeProcessSleep(10);
	}

	s_open = 0;
	_KeCloseFile(sock);
	_KeProcessSleep(100);
	return 0;
}
