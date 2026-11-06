/**
 * Minimal route tool — list / add / del IPv4 FIB entries.
 */

#include <stdint.h>
#include <_xeneva.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/_keproc.h>
#include <sys/_kefile.h>
#include <sys/socket.h>
#include <sys/iocodes.h>
#include <arpa/inet.h>

#define htonl_x(l) \
	((((l) & 0xFF) << 24) | (((l) & 0xFF00) << 8) | (((l) & 0xFF0000) >> 8) | \
	 (((l) & 0xFF000000) >> 24))

static void print_flags(uint8_t f) {
	if (f & RTF_UP)
		printf("UP ");
	if (f & RTF_HOST)
		printf("HOST ");
	if (f & RTF_GATEWAY)
		printf("GATEWAY ");
	if (f & RTF_LOCAL)
		printf("LOCAL ");
	if (f & RTF_CONNECTED)
		printf("CONNECTED ");
	if (f == 0)
		printf("-");
}

static void ip_str(uint32_t wire, char* out, size_t n) {
	uint32_t h = htonl_x(wire);
	snprintf(out, n, "%u.%u.%u.%u",
		(h >> 24) & 0xFF, (h >> 16) & 0xFF, (h >> 8) & 0xFF, h & 0xFF);
}

static int cmd_list(int sock) {
	int n = _KeFileIoControl(sock, SOCK_ROUTE_TABLE_GETNUMENTRY, 0);
	int i;
	char ifbuf[64];
	char d[16], m[16], g[16], a[16];
	XERouteEntry entry;
	XERouteEntryInfo info;

	printf("IPv4 route table (%d entries):\n", n);
	printf("%-18s %-18s %-18s %-10s %s\n", "DEST", "MASK", "GATEWAY", "IFACE", "FLAGS");
	for (i = 0; i < n; i++) {
		memset(&entry, 0, sizeof(entry));
		memset(ifbuf, 0, sizeof(ifbuf));
		entry.ifname = ifbuf;
		info.index = i;
		info.route_entry = &entry;
		if (_KeFileIoControl(sock, SOCK_ROUTE_TABLE_GETENTRY, &info))
			continue;
		ip_str(entry.dest, d, sizeof(d));
		ip_str(entry.netmask, m, sizeof(m));
		ip_str(entry.gateway, g, sizeof(g));
		ip_str(entry.ifaddress, a, sizeof(a));
		if (entry.dest == 0 && entry.netmask == 0)
			printf("%-18s %-18s %-18s %-10s ", "default", "*", g, entry.ifname ? entry.ifname : "?");
		else
			printf("%-18s %-18s %-18s %-10s ", d, m, g, entry.ifname ? entry.ifname : "?");
		print_flags(entry.flags);
		printf("\n");
	}
	return 0;
}

static int cmd_add(int sock, int argc, char** argv) {
	/* argv: <dest> <mask> <gw> <ifname> (caller already consumed "add") */
	XERouteEntry e;
	char* ifname;

	if (argc < 4) {
		printf("usage: route add <dest> <mask> <gateway> <ifname>\n");
		return 1;
	}
	memset(&e, 0, sizeof(e));
	ifname = argv[3];
	e.ifname = ifname;
	e.dest = inet_addr(argv[0]);
	e.netmask = inet_addr(argv[1]);
	e.gateway = inet_addr(argv[2]);
	e.ifaddress = 0;
	e.flags = RTF_UP;
	if (e.gateway)
		e.flags |= RTF_GATEWAY;
	else
		e.flags |= RTF_CONNECTED;
	if (strcmp(argv[0], "0.0.0.0") == 0 && strcmp(argv[1], "0.0.0.0") == 0) {
		e.dest = 0;
		e.netmask = 0;
		e.flags = RTF_UP | RTF_GATEWAY;
	}
	if (_KeFileIoControl(sock, SOCK_ROUTE_TABLE_ADD, &e)) {
		printf("route add failed\n");
		return 1;
	}
	printf("ok\n");
	return 0;
}

static int cmd_del(int sock, int argc, char** argv) {
	XERouteEntry e;

	if (argc < 2) {
		printf("usage: route del <dest> <mask>\n");
		return 1;
	}
	memset(&e, 0, sizeof(e));
	e.dest = inet_addr(argv[0]);
	e.netmask = inet_addr(argv[1]);
	if (strcmp(argv[0], "0.0.0.0") == 0)
		e.dest = 0;
	if (strcmp(argv[1], "0.0.0.0") == 0)
		e.netmask = 0;
	if (_KeFileIoControl(sock, SOCK_ROUTE_TABLE_DELETE, &e)) {
		printf("route del failed\n");
		return 1;
	}
	printf("ok\n");
	return 0;
}

/* Same argv layout quirk as ping: trailing args only, or path/.exe as argv[0]. */
static int is_prog(const char* s) {
	if (!s || !s[0])
		return 1;
	if (s[0] == '/')
		return 1;
	if (strstr(s, ".exe"))
		return 1;
	if (strcmp(s, "route") == 0)
		return 1;
	return 0;
}

int main(int argc, char* argv[]) {
	int sock = socket(AF_INET, SOCK_DGRAM, IPPROTOCOL_UDP);
	int rc = 0;
	int i = 0;

	if (sock < 0) {
		printf("route: socket failed\n");
		return 1;
	}

	while (i < argc && is_prog(argv[i]))
		i++;

	if (i >= argc || strcmp(argv[i], "list") == 0 || strcmp(argv[i], "-n") == 0)
		rc = cmd_list(sock);
	else if (strcmp(argv[i], "add") == 0)
		rc = cmd_add(sock, argc - (i + 1), argv + (i + 1));
	else if (strcmp(argv[i], "del") == 0 || strcmp(argv[i], "delete") == 0)
		rc = cmd_del(sock, argc - (i + 1), argv + (i + 1));
	else {
		printf("usage: route [list|add|del] ...\n");
		rc = 1;
	}
	return rc;
}
