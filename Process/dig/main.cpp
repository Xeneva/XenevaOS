/**
* BSD 2-Clause License
*
* Copyright (c) 2023-2024, Manas Kamal Choudhury
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
#include <_xeneva.h>
#include <stdio.h>
#include <sys/_keproc.h>
#include <sys/_kefile.h>
#include <sys/_ketime.h>
#include <sys/socket.h>
#include <sys/netdb.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>

/*
 * AArch64 XEClib vsnprintf() does not fix va_list; snprintf()/printf() do.
 * Always format with snprintf, then emit.
 */
static void dig_emit(const char* line) {
	char serial[280];
	size_t i, j;

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
	serial[j] = '\0';
	_KePrint("%s", serial);
}

#define dig_out(...)                       \
	do {                                   \
		char _dig_line[256];               \
		snprintf(_dig_line, sizeof(_dig_line), __VA_ARGS__); \
		dig_emit(_dig_line);               \
	} while (0)

static int is_qtype_token(const char* s) {
	return s && (strcasecmp(s, "A") == 0 || strcasecmp(s, "AAAA") == 0 ||
				 strcasecmp(s, "ANY") == 0);
}

/*
 * Init/--term: argv[0] is the first user arg (command name stripped).
 * Accept: dig name | dig A name | dig name A | dig AAAA name
 */
int main(int argc, char* argv[]) {
	const char* name = NULL;
	const char* qtype = "ANY";
	int family = AF_UNSPEC;
	addrinfo hints;
	addrinfo* res = NULL;
	addrinfo* rp;
	int err;
	char buf[64];
	char qname[256];
	uint32_t server;
	uint64_t t0, t1;
	int n_ans = 0;
	int want_a = 1;
	int want_aaaa = 1;

	for (int i = 0; i < argc; i++) {
		const char* a = argv[i];
		if (!a || !a[0])
			continue;
		if (a[0] == '/' || strstr(a, ".exe") || strcasecmp(a, "dig") == 0)
			continue;
		if (a[0] == '-') {
			dig_out("dig: unknown option %s\n", a);
			return 1;
		}
		if (is_qtype_token(a)) {
			qtype = a;
			continue;
		}
		if (!name)
			name = a;
	}

	if (!name) {
		dig_out("usage: dig [@server] [type] name\n");
		dig_out("       dig name\n");
		dig_out("       dig A name\n");
		dig_out("       dig AAAA name\n");
		return 1;
	}

	if (strcasecmp(qtype, "A") == 0) {
		family = AF_INET;
		want_aaaa = 0;
	} else if (strcasecmp(qtype, "AAAA") == 0) {
		family = AF_INET6;
		want_a = 0;
	} else {
		qtype = "ANY";
		family = AF_UNSPEC;
	}

	strncpy(qname, name, sizeof(qname) - 2);
	qname[sizeof(qname) - 2] = '\0';
	if (qname[0] && qname[strlen(qname) - 1] != '.')
		strcat(qname, ".");

	if (strcasecmp(qtype, "ANY") == 0)
		dig_out("; <<>> DiG 0.1-xeneva <<>> %s\n", name);
	else
		dig_out("; <<>> DiG 0.1-xeneva <<>> %s %s\n", qtype, name);
	dig_out(";; global options: +cmd\n");

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = family;
	hints.ai_socktype = SOCK_DGRAM;

	t0 = _KeGetCurrentMS();
	err = getaddrinfo(name, NULL, &hints, &res);
	t1 = _KeGetCurrentMS();

	if (err != 0) {
		dig_out(";; Got answer:\n");
		dig_out(";; ->>HEADER<<- opcode: QUERY, status: NXDOMAIN, id: 0\n");
		dig_out(";; flags: qr rd ra; QUERY: 1, ANSWER: 0, AUTHORITY: 0, ADDITIONAL: 0\n");
		dig_out("\n");
		dig_out(";; QUESTION SECTION:\n");
		if (want_a)
			dig_out(";%-31s\tIN\tA\n", qname);
		if (want_aaaa)
			dig_out(";%-31s\tIN\tAAAA\n", qname);
		dig_out("\n");
		dig_out(";; Query time: %u msec\n", (unsigned)(t1 - t0));
		server = xe_dns_last_server();
		if (server) {
			struct in_addr ina;
			char sbuf[32];
			ina.s_addr = server;
			inet_ntop(AF_INET, &ina, sbuf, sizeof(sbuf));
			dig_out(";; SERVER: %s#53(%s)\n", sbuf, sbuf);
		} else {
			dig_out(";; SERVER: local-zone#0(local-zone)\n");
		}
		dig_out(";; MSG SIZE  rcvd: 0\n");
		dig_out("dig: %s: %s\n", name, gai_strerror(err));
		return 1;
	}

	for (rp = res; rp; rp = (addrinfo*)rp->ai_next) {
		if (rp->ai_family == AF_INET && want_a)
			n_ans++;
		else if (rp->ai_family == AF_INET6 && want_aaaa)
			n_ans++;
	}

	dig_out(";; Got answer:\n");
	dig_out(";; ->>HEADER<<- opcode: QUERY, status: NOERROR, id: 0\n");
	dig_out(";; flags: qr aa rd ra; QUERY: 1, ANSWER: %d, AUTHORITY: 0, ADDITIONAL: 0\n",
			n_ans);
	dig_out("\n");
	dig_out(";; QUESTION SECTION:\n");
	if (want_a && want_aaaa) {
		dig_out(";%-31s\tIN\tA\n", qname);
		dig_out(";%-31s\tIN\tAAAA\n", qname);
	} else if (want_a) {
		dig_out(";%-31s\tIN\tA\n", qname);
	} else {
		dig_out(";%-31s\tIN\tAAAA\n", qname);
	}
	dig_out("\n");
	dig_out(";; ANSWER SECTION:\n");

	for (rp = res; rp; rp = (addrinfo*)rp->ai_next) {
		const char* s = NULL;
		const char* tn;
		if (rp->ai_family == AF_INET) {
			if (!want_a)
				continue;
			sockaddr_in* in = (sockaddr_in*)rp->ai_addr;
			s = inet_ntop(AF_INET, &in->sin_addr, buf, sizeof(buf));
			tn = "A";
		} else if (rp->ai_family == AF_INET6) {
			if (!want_aaaa)
				continue;
			sockaddr_in6* in6 = (sockaddr_in6*)rp->ai_addr;
			s = inet_ntop(AF_INET6, &in6->sin6_addr, buf, sizeof(buf));
			tn = "AAAA";
		} else {
			continue;
		}
		if (s)
			dig_out("%-31s\t%d\tIN\t%s\t%s\n", qname, 60, tn, s);
	}

	dig_out("\n");
	dig_out(";; Query time: %u msec\n", (unsigned)(t1 - t0));
	server = xe_dns_last_server();
	if (server) {
		struct in_addr ina;
		char sbuf[32];
		ina.s_addr = server;
		inet_ntop(AF_INET, &ina, sbuf, sizeof(sbuf));
		dig_out(";; SERVER: %s#53(%s)\n", sbuf, sbuf);
	} else {
		dig_out(";; SERVER: local-zone#0(local-zone)\n");
	}
	dig_out(";; MSG SIZE  rcvd: %d\n", n_ans > 0 ? (12 + n_ans * 16) : 0);

	freeaddrinfo(res);
	return n_ans > 0 ? 0 : 1;
}
