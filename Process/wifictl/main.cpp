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

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <_xeneva.h>
#include <sys/_kefile.h>
#include <sys/_keproc.h>
#include <sys/iocodes.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <Net/wifi.h>

#ifndef MAKE_IP
#define MAKE_IP(a, b, c, d) \
	((uint32_t)(d) << 24 | (uint32_t)(c) << 16 | (uint32_t)(b) << 8 | (uint32_t)(a))
#endif

static void emit(const char* line) {
	char serial[240];
	size_t i;
	size_t j;

	printf("%s", line);
	fflush(stdout);
	for (i = 0, j = 0; line[i] && j + 2 < sizeof(serial); i++) {
		if (line[i] == '\n') {
			serial[j++] = '\r';
			serial[j++] = '\n';
		} else {
			serial[j++] = line[i];
		}
	}
	serial[j] = 0;
	_KePrint("%s", serial);
}

#define out(...)                                   \
	do {                                           \
		char _line[220];                           \
		snprintf(_line, sizeof(_line), __VA_ARGS__); \
		emit(_line);                               \
	} while (0)

static int skip_tok(const char* a) {
	if (!a || !a[0])
		return 1;
	if (a[0] == '/')
		return 1;
	if (strstr(a, ".exe"))
		return 1;
	if (strcasecmp(a, "wifictl") == 0)
		return 1;
	return 0;
}

static void usage(void) {
	out("usage: wifictl scan\n");
	out("       wifictl connect <ssid> [passphrase]\n");
	out("       wifictl status\n");
	out("       wifictl disconnect\n");
	out("       wifictl uplink on|off\n");
	out("       wifictl check\n");
	out("       wifictl air\n");
}

/* Host AX200 scan, via Scripts/Linux/wifi_proxy.py on 10.0.2.2:9753.
 * Slirp already routes that address to the laptop. The proxy only reads
 * the cached scan, so Ubuntu keeps VITC-HOS2-4. */
static int cmd_air(void) {
	sockaddr_in dest;
	int sock;
	char req[] = "SCAN\n";
	char buf[512];
	char line[220];
	int idle = 0;
	int got = 0;
	int fill = 0;

	memset(&dest, 0, sizeof(dest));
	dest.sin_family = AF_INET;
	dest.sin_port = htons(9753);
	dest.sin_addr.s_addr = MAKE_IP(10, 0, 2, 2);
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		out("wifictl: air socket failed\n");
		return 1;
	}
	if (connect(sock, (sockaddr_*)&dest, sizeof(dest)) < 0) {
		out("wifictl: start python3 Scripts/Linux/wifi_proxy.py on the host\n");
		_KeCloseFile(sock);
		return 1;
	}
	if (sendto(sock, req, sizeof(req) - 1, 0, (sockaddr*)&dest, sizeof(dest)) < 0) {
		out("wifictl: air send failed\n");
		_KeCloseFile(sock);
		return 1;
	}
	while (idle < 30) {
		int n = recvfrom(sock, buf, sizeof(buf) - 1, 0, NULL, NULL);
		int i;
		if (n > 0) {
			got = 1;
			idle = 0;
			for (i = 0; i < n; i++) {
				if (buf[i] == '\n' || fill + 1 >= (int)sizeof(line)) {
					line[fill] = 0;
					if (fill)
						out("%s\n", line);
					fill = 0;
				} else if (buf[i] != '\r') {
					line[fill++] = buf[i];
				}
			}
			continue;
		}
		if (n == 0 && got)
			break;
		_KeProcessSleep(100);
		idle++;
	}
	if (fill) {
		line[fill] = 0;
		out("%s\n", line);
	}
	_KeCloseFile(sock);
	if (!got) {
		out("wifictl: air proxy gave no scan\n");
		return 1;
	}
	return 0;
}

static int run_ping(const char* host) {
	char name[] = "ping";
	char path[] = "/ping.exe";
	char arg[48];
	char* argv[1];
	int proc;

	snprintf(arg, sizeof(arg), "%s", host);
	argv[0] = arg;
	proc = _KeCreateProcess(0, name);
	if (proc < 0) {
		out("wifictl: cannot start ping\n");
		return 1;
	}
	_KeSetFileToProcess(XENEVA_STDIN, XENEVA_STDIN, proc);
	_KeSetFileToProcess(XENEVA_STDOUT, XENEVA_STDOUT, proc);
	_KeSetFileToProcess(XENEVA_STDERR, XENEVA_STDERR, proc);
	if (_KeProcessLoadExec(proc, path, 1, argv) != 0) {
		out("wifictl: ping failed to load\n");
		return 1;
	}
	_KeProcessWaitForTermination(proc);
	return 0;
}

static int scan_has(const WifiScanResult* scan, const char* name) {
	uint32_t i;
	for (i = 0; i < scan->count && i < WIFI_MAX_BSS; i++) {
		char ssid[33];
		uint8_t n = scan->bss[i].ssid_len;
		if (n > 32)
			n = 32;
		memcpy(ssid, (void*)scan->bss[i].ssid, n);
		ssid[n] = 0;
		if (strcmp(ssid, name) == 0)
			return 1;
	}
	return 0;
}

static int open_wlan(void) {
	int fd = _KeOpenFile("/dev/net/wlan0", FILE_OPEN_READ_ONLY);
	if (fd < 0)
		out("wifictl: wlan0 is not available\n");
	return fd;
}

static int cmd_scan(int fd) {
	WifiScanResult scan;
	uint32_t i;

	memset(&scan, 0, sizeof(scan));
	if (_KeFileIoControl(fd, WIFI_SCAN, 0))
		return 1;
	if (_KeFileIoControl(fd, WIFI_GET_SCAN, &scan))
		return 1;
	for (i = 0; i < scan.count && i < WIFI_MAX_BSS; i++) {
		char ssid[33];
		uint8_t n = scan.bss[i].ssid_len;
		if (n > 32)
			n = 32;
		memcpy(ssid, scan.bss[i].ssid, n);
		ssid[n] = 0;
		out("%s  ch %u  %d dBm  %s  %02x:%02x:%02x:%02x:%02x:%02x\n",
			ssid,
			(unsigned)scan.bss[i].channel,
			(int)scan.bss[i].rssi,
			scan.bss[i].rsn ? "wpa2" : "open",
			scan.bss[i].bssid[0], scan.bss[i].bssid[1], scan.bss[i].bssid[2],
			scan.bss[i].bssid[3], scan.bss[i].bssid[4], scan.bss[i].bssid[5]);
	}
	return 0;
}

static int cmd_connect(int fd, const char* ssid, const char* pass) {
	WifiConnectReq req;
	WifiStatus st;
	uint32_t addr = MAKE_IP(192, 168, 50, 2);
	uint32_t mask = MAKE_IP(255, 255, 255, 0);

	memset(&req, 0, sizeof(req));
	snprintf(req.ssid, sizeof(req.ssid), "%s", ssid);
	if (pass)
		snprintf(req.pass, sizeof(req.pass), "%s", pass);
	if (_KeFileIoControl(fd, WIFI_CONNECT, &req)) {
		out("wifictl: connect failed\n");
		return 1;
	}
	if (_KeFileIoControl(fd, NET_SET_IPV4_ADDRESS, &addr) ||
		_KeFileIoControl(fd, NET_SET_SUBNET_MASK, &mask)) {
		out("wifictl: address setup failed\n");
		return 1;
	}
	memset(&st, 0, sizeof(st));
	if (_KeFileIoControl(fd, WIFI_GET_STATUS, &st))
		return 1;
	out("associated %s  %s\n", st.ssid, st.rsn ? "wpa2" : "open");
	return 0;
}

static int cmd_status(int fd) {
	WifiStatus st;
	memset(&st, 0, sizeof(st));
	if (_KeFileIoControl(fd, WIFI_GET_STATUS, &st))
		return 1;
	out("%s  %s  link %d  %02x:%02x:%02x:%02x:%02x:%02x\n",
		st.state[0] ? st.state : "idle",
		st.ssid,
		(int)st.link,
		st.bssid[0], st.bssid[1], st.bssid[2], st.bssid[3], st.bssid[4], st.bssid[5]);
	return 0;
}

static int virtio_still_slirp(void) {
	int fd;
	uint32_t addr = 0;
	char path[] = "/dev/net/virtio-net";

	fd = _KeOpenFile(path, FILE_OPEN_READ_ONLY);
	if (fd < 0)
		return 0;
	if (_KeFileIoControl(fd, NET_GET_IPV4_ADDRESS, &addr))
		return 0;
	return addr == MAKE_IP(10, 0, 2, 15);
}

static int station_link_down(int fd) {
	int link = 1;
	WifiStatus st;

	if (_KeFileIoControl(fd, NET_GET_LINK_STATUS, &link))
		return 0;
	memset(&st, 0, sizeof(st));
	if (_KeFileIoControl(fd, WIFI_GET_STATUS, &st))
		return 0;
	return link == 0 && st.link == 0;
}

/* One guest pass of the station checks. Ping lines are the pass/fail for each hop. */
static int cmd_check(int fd) {
	WifiScanResult scan;
	int fails = 0;

	_KeProcessSleep(2000);
	out("[wifi-check] 1 ping 1.1.1.1\n");
	run_ping("1.1.1.1");

	out("[wifi-check] 2 scan\n");
	if (cmd_scan(fd) != 0)
		fails++;
	memset(&scan, 0, sizeof(scan));
	if (_KeFileIoControl(fd, WIFI_GET_SCAN, &scan) || !scan_has(&scan, "xeneva-open") ||
		!scan_has(&scan, "xeneva")) {
		out("[wifi-check] scan missing BSS\n");
		fails++;
	}

	out("[wifi-check] 3 connect xeneva-open\n");
	if (cmd_connect(fd, "xeneva-open", 0) != 0)
		fails++;
	out("[wifi-check] 3 ping 192.168.50.1\n");
	run_ping("192.168.50.1");
	out("[wifi-check] 3 ping 1.1.1.1\n");
	run_ping("1.1.1.1");

	out("[wifi-check] 4 connect xeneva wrong\n");
	if (cmd_connect(fd, "xeneva", "wrong") == 0 || !station_link_down(fd)) {
		out("[wifi-check] wrong passphrase was accepted\n");
		fails++;
	}

	out("[wifi-check] 5 connect xeneva\n");
	if (cmd_connect(fd, "xeneva", "xenevaos") != 0)
		fails++;
	out("[wifi-check] 5 ping 192.168.50.1\n");
	run_ping("192.168.50.1");

	out("[wifi-check] 6 uplink on\n");
	{
		int on = 1;
		if (_KeFileIoControl(fd, WIFI_UPLINK, &on)) {
			out("[wifi-check] uplink on failed\n");
			fails++;
		}
	}
	out("[wifi-check] 6 ping 1.1.1.1\n");
	run_ping("1.1.1.1");
	out("[wifi-check] 6 uplink off\n");
	{
		int on = 0;
		if (_KeFileIoControl(fd, WIFI_UPLINK, &on)) {
			out("[wifi-check] uplink off failed\n");
			fails++;
		}
	}
	out("[wifi-check] 6 ping 1.1.1.1\n");
	run_ping("1.1.1.1");

	out("[wifi-check] 7 disconnect\n");
	if (_KeFileIoControl(fd, WIFI_DISCONNECT, 0) || !station_link_down(fd) || !virtio_still_slirp()) {
		out("[wifi-check] disconnect or virtio address failed\n");
		fails++;
	} else {
		out("disconnected\n");
	}
	out("[wifi-check] %s\n", fails ? "FAIL" : "DONE");
	return fails ? 1 : 0;
}

int main(int argc, char* argv[]) {
	const char* tok[8];
	int ntok = 0;
	int i;
	int fd;
	int rc;

	for (i = 0; i < argc && ntok < 8; i++) {
		if (skip_tok(argv[i]))
			continue;
		tok[ntok++] = argv[i];
	}
	if (ntok < 1) {
		usage();
		return 1;
	}
	if (strcasecmp(tok[0], "air") == 0)
		return cmd_air();
	fd = open_wlan();
	if (fd < 0)
		return 1;
	if (strcasecmp(tok[0], "scan") == 0) {
		rc = cmd_scan(fd);
	} else if (strcasecmp(tok[0], "status") == 0) {
		rc = cmd_status(fd);
	} else if (strcasecmp(tok[0], "disconnect") == 0) {
		rc = _KeFileIoControl(fd, WIFI_DISCONNECT, 0) ? 1 : 0;
		if (!rc)
			out("disconnected\n");
	} else if (strcasecmp(tok[0], "uplink") == 0 && ntok >= 2) {
		int on = strcasecmp(tok[1], "on") == 0;
		if (strcasecmp(tok[1], "on") != 0 && strcasecmp(tok[1], "off") != 0) {
			usage();
			return 1;
		}
		rc = _KeFileIoControl(fd, WIFI_UPLINK, &on) ? 1 : 0;
		if (rc)
			out("wifictl: uplink failed\n");
		else
			out("uplink %s\n", on ? "on" : "off");
	} else if (strcasecmp(tok[0], "check") == 0) {
		rc = cmd_check(fd);
	} else if (strcasecmp(tok[0], "connect") == 0 && ntok >= 2) {
		rc = cmd_connect(fd, tok[1], ntok >= 3 ? tok[2] : 0);
	} else {
		usage();
		return 1;
	}
	return rc;
}
