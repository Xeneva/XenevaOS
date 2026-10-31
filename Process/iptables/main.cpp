/**
 * Minimal iptables — talk Week-2 SOCK_NF_* ioctls only.
 *
 * Supported:
 *   iptables -A INPUT -i lo -j ACCEPT
 *   iptables -A INPUT -p udp --dport N -j DROP
 *   iptables -A OUTPUT -o virtio-net -j ACCEPT
 *   iptables -L
 *   iptables -F
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

static const char* hook_name(int h) {
	switch (h) {
	case NF_PRE_ROUTING: return "PREROUTING";
	case NF_LOCAL_IN: return "INPUT";
	case NF_FORWARD: return "FORWARD";
	case NF_LOCAL_OUT: return "OUTPUT";
	case NF_POST_ROUTING: return "POSTROUTING";
	default: return "?";
	}
}

static const char* target_name(int t) {
	switch (t) {
	case NF_ACCEPT: return "ACCEPT";
	case NF_DROP: return "DROP";
	case NF_REJECT: return "REJECT";
	default: return "?";
	}
}

static int parse_hook(const char* s) {
	if (strcmp(s, "INPUT") == 0 || strcmp(s, "input") == 0)
		return NF_LOCAL_IN;
	if (strcmp(s, "OUTPUT") == 0 || strcmp(s, "output") == 0)
		return NF_LOCAL_OUT;
	if (strcmp(s, "FORWARD") == 0 || strcmp(s, "forward") == 0)
		return NF_FORWARD;
	if (strcmp(s, "PREROUTING") == 0)
		return NF_PRE_ROUTING;
	if (strcmp(s, "POSTROUTING") == 0)
		return NF_POST_ROUTING;
	return -1;
}

static int parse_target(const char* s) {
	if (strcmp(s, "ACCEPT") == 0)
		return NF_ACCEPT;
	if (strcmp(s, "DROP") == 0)
		return NF_DROP;
	if (strcmp(s, "REJECT") == 0)
		return NF_REJECT;
	return -1;
}

static int cmd_list(int sock) {
	int n = _KeFileIoControl(sock, SOCK_NF_GETNUM, 0);
	int i;
	XENfRule rule;
	XENfRuleInfo info;

	printf("Chain rules (%d):\n", n);
	for (i = 0; i < n; i++) {
		memset(&rule, 0, sizeof(rule));
		info.index = i;
		info.rule = &rule;
		if (_KeFileIoControl(sock, SOCK_NF_LIST, &info))
			continue;
		printf("[%d] %-12s", i, hook_name(rule.hook));
		if (rule.in_dev[0])
			printf(" -i %s", rule.in_dev);
		if (rule.out_dev[0])
			printf(" -o %s", rule.out_dev);
		if (rule.proto == 17)
			printf(" -p udp");
		else if (rule.proto == 6)
			printf(" -p tcp");
		else if (rule.proto == 1)
			printf(" -p icmp");
		else if (rule.proto)
			printf(" -p %u", rule.proto);
		if (rule.sport)
			printf(" --sport %u", rule.sport);
		if (rule.dport)
			printf(" --dport %u", rule.dport);
		printf(" -j %s\n", target_name(rule.target));
	}
	return 0;
}

static int cmd_flush(int sock) {
	_KeFileIoControl(sock, SOCK_NF_FLUSH, 0);
	printf("flushed\n");
	return 0;
}

static int cmd_append(int sock, int argc, char** argv) {
	/* argv[0] == chain, then options (caller already consumed -A) */
	XENfRule r;
	int i;
	int hook;
	int target = NF_ACCEPT;

	if (argc < 1) {
		printf("usage: iptable -A <chain> ... -j TARGET\n");
		return 1;
	}
	memset(&r, 0, sizeof(r));
	hook = parse_hook(argv[0]);
	if (hook < 0) {
		printf("unknown chain %s\n", argv[0]);
		return 1;
	}
	r.hook = hook;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
			strncpy(r.in_dev, argv[++i], 15);
			r.in_dev[15] = 0;
		} else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
			strncpy(r.out_dev, argv[++i], 15);
			r.out_dev[15] = 0;
		} else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
			i++;
			if (strcmp(argv[i], "udp") == 0)
				r.proto = 17;
			else if (strcmp(argv[i], "tcp") == 0)
				r.proto = 6;
			else if (strcmp(argv[i], "icmp") == 0)
				r.proto = 1;
		} else if (strcmp(argv[i], "--dport") == 0 && i + 1 < argc) {
			r.dport = (uint16_t)atoi(argv[++i]);
		} else if (strcmp(argv[i], "--sport") == 0 && i + 1 < argc) {
			r.sport = (uint16_t)atoi(argv[++i]);
		} else if (strcmp(argv[i], "-j") == 0 && i + 1 < argc) {
			target = parse_target(argv[++i]);
			if (target < 0) {
				printf("unknown target\n");
				return 1;
			}
			r.target = target;
		}
	}
	r.target = target;
	if (_KeFileIoControl(sock, SOCK_NF_APPEND, &r)) {
		printf("append failed\n");
		fflush(stdout);
		return 1;
	}
	printf("ok\n");
	fflush(stdout);
	return 0;
}

/*
 * Aurora argv: XEShell/LoadExec pass only trailing args (no prog name),
 * or sometimes "/iptable.exe" as argv[0]. Scan like ping/udpecho.
 */
static int is_prog(const char* s) {
	if (!s || !s[0])
		return 1;
	if (s[0] == '/')
		return 1;
	if (strstr(s, ".exe"))
		return 1;
	if (strcmp(s, "iptable") == 0 || strcmp(s, "iptables") == 0)
		return 1;
	return 0;
}

int main(int argc, char* argv[]) {
	int sock = socket(AF_INET, SOCK_DGRAM, IPPROTOCOL_UDP);
	int rc = 0;
	int i = 0;

	if (sock < 0) {
		printf("iptable: socket failed\n");
		return 1;
	}

	while (i < argc && is_prog(argv[i]))
		i++;

	if (i >= argc) {
		printf("usage: iptable [-L|-F|-A ...]\n");
		return 1;
	}

	if (strcmp(argv[i], "-L") == 0 || strcmp(argv[i], "-l") == 0)
		rc = cmd_list(sock);
	else if (strcmp(argv[i], "-F") == 0)
		rc = cmd_flush(sock);
	else if (strcmp(argv[i], "-A") == 0)
		rc = cmd_append(sock, argc - (i + 1), argv + (i + 1));
	else {
		printf("usage: iptable [-L|-F|-A ...]\n");
		rc = 1;
	}
	fflush(stdout);
	return rc;
}
