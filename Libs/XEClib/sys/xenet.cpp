/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2024, Manas Kamal Choudhury
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

#include <stdlib.h>
#include <sys/netdb.h>
#include <sys/socket.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/iocodes.h>
#include <stdio.h>
#include <sys/_keproc.h>
#include <sys/_kefile.h>
#include <sys/_ketime.h>
#include <stdint.h>

extern int xe_hosts_lookup(const char* name,
						   int* out_n,
						   int* out_families,
						   uint8_t out_addrs[][16],
						   int maxn);
extern int xe_hosts_reverse(const void* addr,
							int family,
							char* out,
							size_t outsz);

/* XEClib memcpy lacks const on src. */
static inline void xe_memcpy(void* d, const void* s, size_t n) {
	memcpy(d, (void*)s, n);
}

ssize_t recv(int sockfd, void* buf, size_t len, int flags) {
	iovec _iovec;
	_iovec.iov_base = buf;
	_iovec.iov_len = len;

	msghdr _hdr;
	_hdr.msg_name = NULL;
	_hdr.msg_namelen = 0;
	_hdr.msg_iov = &_iovec;
	_hdr.msg_iovlen = 1;
	_hdr.msg_control = NULL;
	_hdr.msg_controllen = 0;
	_hdr.msg_flags = 0;

	return receive(sockfd, &_hdr, flags);
}

ssize_t sendmsg(int sockfd, const msghdr* msg, int flags) {
	return send(sockfd, (msghdr*)msg, flags);
}

ssize_t recvmsg(int sockfd, msghdr* msg, int flags) {
	return receive(sockfd, msg, flags);
}

int setsockopt(int sockfd, int level, int optname, const void* optval, socklen_t optlen) {
	return socket_setopt(sockfd, level, optname, optval, optlen);
}

#define DNS_QTYPE_A    1
#define DNS_QTYPE_PTR  12
#define DNS_QTYPE_AAAA 28
#define DNS_QCLASS_IN  1
#define DNS_TIMEOUT_MS     3000
#define DNS_POLL_MS        100
#define DNS_ROUTE_WAIT_MS  10000 /* wait for DHCP default route */
#define DNS_ROUTE_RETRY_MS 500
#define DNS_MAX_ADDRS      8
#define DNS_CACHE_SLOTS    32
#define DNS_NAME_MAX       256
#define DNS_TTL_CAP_S      60
#define DNS_NEG_TTL_S      5

typedef struct {
	int family;
	uint8_t addr[16];
	uint32_t ttl;
} XeDnsAddr;

typedef struct {
	int n;
	XeDnsAddr addrs[DNS_MAX_ADDRS];
	int negative;
} XeDnsResult;

typedef struct {
	int used;
	int negative;
	char name[DNS_NAME_MAX];
	int family_filter; /* AF_UNSPEC / AF_INET / AF_INET6 */
	uint64_t expires_ms;
	int n;
	XeDnsAddr addrs[DNS_MAX_ADDRS];
} XeDnsCacheEntry;

typedef struct {
	int negative;
	char name[DNS_NAME_MAX];
	uint32_t ttl;
} XePtrResult;

typedef struct {
	const char* name;
	const char* proto;
	uint16_t port;
} XeService;

/* Minimal services database.  This is deliberately static: the image has
 * no /etc/services yet, and getaddrinfo()/getnameinfo() need the common
 * names for HTTP, TLS and DNS without a network round trip. */
static const XeService k_services[] = {
	{ "http", "tcp", 80 },
	{ "https", "tcp", 443 },
	{ "domain", "tcp", 53 },
	{ "domain", "udp", 53 },
	{ "ftp", "tcp", 21 },
	{ "ssh", "tcp", 22 },
	{ "telnet", "tcp", 23 },
	{ "smtp", "tcp", 25 },
	{ "pop3", "tcp", 110 },
	{ "ntp", "udp", 123 },
	{ "imap", "tcp", 143 },
	{ "snmp", "udp", 161 },
	{ "imaps", "tcp", 993 },
	{ "pop3s", "tcp", 995 },
};

static servent g_servent;
static char* g_serv_aliases[1] = { NULL };

static XeDnsCacheEntry g_cache[DNS_CACHE_SLOTS];
static uint32_t g_dns_fingerprint;
static uint32_t g_last_dns_server;
static uint8_t g_hostent_addrs[DNS_MAX_ADDRS][16];
static char* g_host_entry_list[DNS_MAX_ADDRS + 1];
static char g_hostent_name[DNS_NAME_MAX];

int h_errno;

servent* getservbyname(const char* name, const char* proto) {
	if (!name)
		return NULL;
	for (size_t i = 0; i < sizeof(k_services) / sizeof(k_services[0]); i++) {
		if (strcasecmp(name, k_services[i].name) != 0)
			continue;
		if (proto && strcasecmp(proto, k_services[i].proto) != 0)
			continue;
		g_servent.s_name = (char*)k_services[i].name;
		g_servent.s_aliases = g_serv_aliases;
		g_servent.s_port = htons(k_services[i].port);
		g_servent.s_proto = (char*)k_services[i].proto;
		return &g_servent;
	}
	return NULL;
}

servent* getservbyport(int port, const char* proto) {
	uint16_t hostport = ntohs((uint16_t)port);

	for (size_t i = 0; i < sizeof(k_services) / sizeof(k_services[0]); i++) {
		if (k_services[i].port != hostport)
			continue;
		if (proto && strcasecmp(proto, k_services[i].proto) != 0)
			continue;
		g_servent.s_name = (char*)k_services[i].name;
		g_servent.s_aliases = g_serv_aliases;
		g_servent.s_port = port;
		g_servent.s_proto = (char*)k_services[i].proto;
		return &g_servent;
	}
	return NULL;
}

const char* hstrerror(int err) {
	switch (err) {
	case 0:
		return "Resolver error 0 (no error)";
	case HOST_NOT_FOUND:
		return "Unknown host";
	case TRY_AGAIN:
		return "Host name lookup failure";
	case NO_RECOVERY:
		return "Unknown server error";
	case NO_DATA:
		return "No address associated with name";
	default:
		return "Unknown resolver error";
	}
}

void herror(const char* s) {
	const char* msg = hstrerror(h_errno);

	if (s && s[0])
		_KePrint("%s: %s\r\n", s, msg);
	else
		_KePrint("%s\r\n", msg);
}

static uint64_t xe_now_ms(void) {
	return _KeGetCurrentMS();
}

uint32_t xe_dns_last_server(void) {
	return g_last_dns_server;
}

/*
 * Prefer /resolv.cnf (FAT 8.3-safe, matches shell.cnf), then /resolv.conf.
 * RFC 1035 stub resolver config.
 */
static const char* xe_resolv_paths[] = {
	"/resolv.cnf",
	"/resolv.conf",
	"/etc/resolv.conf",
	NULL
};

static int dns_parse_resolv_conf(uint32_t* servers, int maxn, uint32_t* fp_out) {
	int n = 0;
	uint32_t fp = 0;
	FILE* f = NULL;
	char buf[512];
	size_t got;
	char* cur;
	char* end;

	if (!servers || maxn <= 0)
		return 0;

	for (int p = 0; xe_resolv_paths[p]; p++) {
		f = fopen(xe_resolv_paths[p], "r");
		if (f)
			break;
	}
	if (!f)
		return 0;

	memset(buf, 0, sizeof(buf));
	got = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	if (got == 0)
		return 0;
	buf[got] = '\0';

	cur = buf;
	while (n < maxn && *cur) {
		char* s;
		char* tok;
		uint32_t addr = 0;
		uint8_t lit[4];
		char line[128];
		size_t llen = 0;

		end = cur;
		while (*end && *end != '\n' && *end != '\r')
			end++;
		llen = (size_t)(end - cur);
		if (llen >= sizeof(line))
			llen = sizeof(line) - 1;
		xe_memcpy(line, cur, llen);
		line[llen] = '\0';
		while (*end == '\n' || *end == '\r')
			end++;
		cur = end;

		s = line;
		while (*s == ' ' || *s == '\t')
			s++;
		if (*s == '#' || *s == ';' || *s == '\0')
			continue;
		if (strncmp(s, "nameserver", 10) != 0)
			continue;
		s += 10;
		while (*s == ' ' || *s == '\t')
			s++;
		tok = s;
		while (*s && *s != ' ' && *s != '\t' && *s != '#')
			s++;
		*s = '\0';
		if (!tok[0])
			continue;
		if (inet_pton(AF_INET, tok, lit) != 1)
			continue;
		xe_memcpy(&addr, lit, 4);
		if (!addr)
			continue;
		{
			int dup = 0;
			for (int i = 0; i < n; i++) {
				if (servers[i] == addr) {
					dup = 1;
					break;
				}
			}
			if (dup)
				continue;
		}
		servers[n++] = addr;
		fp ^= addr + (uint32_t)(n * 0x9e3779b9u);
	}
	if (fp_out)
		*fp_out = fp;
	return n;
}

static uint32_t xe_dns_server_fingerprint(void) {
	uint32_t servers[4];
	uint32_t fp = 0;
	int n;
	int sock;

	n = dns_parse_resolv_conf(servers, 4, &fp);
	if (n > 0)
		return fp;

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0)
		return 0;
	for (int i = 1; i <= 4; i++) {
		XEDNSEntry dns;
		memset(&dns, 0, sizeof(dns));
		dns.index = i;
		if (_KeFileIoControl(sock, SOCK_GET_DNS_SERVER, &dns) == 0 && dns.address)
			fp ^= dns.address + (uint32_t)(i * 0x9e3779b9u);
	}
	_KeCloseFile(sock);
	return fp;
}

static void xe_dns_cache_flush(void) {
	memset(g_cache, 0, sizeof(g_cache));
}

static void xe_dns_cache_maybe_invalidate(void) {
	uint32_t fp = xe_dns_server_fingerprint();
	if (fp != g_dns_fingerprint) {
		g_dns_fingerprint = fp;
		xe_dns_cache_flush();
	}
}

static int xe_name_eq(const char* a, const char* b) {
	return strcasecmp(a, b) == 0;
}

static XeDnsCacheEntry* xe_dns_cache_find(const char* name, int family) {
	uint64_t now = xe_now_ms();
	for (int i = 0; i < DNS_CACHE_SLOTS; i++) {
		XeDnsCacheEntry* e = &g_cache[i];
		if (!e->used)
			continue;
		if (e->expires_ms && now >= e->expires_ms) {
			e->used = 0;
			continue;
		}
		if (!xe_name_eq(e->name, name))
			continue;
		if (e->family_filter != AF_UNSPEC && family != AF_UNSPEC &&
			e->family_filter != family)
			continue;
		return e;
	}
	return NULL;
}

static void xe_dns_cache_store(const char* name,
							   int family,
							   const XeDnsResult* r,
							   uint32_t ttl_s) {
	XeDnsCacheEntry* slot = NULL;
	uint64_t now = xe_now_ms();
	uint64_t exp;
	int i;

	if (!name || !r)
		return;
	if (ttl_s == 0)
		ttl_s = r->negative ? DNS_NEG_TTL_S : 30;
	if (!r->negative && ttl_s > DNS_TTL_CAP_S)
		ttl_s = DNS_TTL_CAP_S;
	if (r->negative)
		ttl_s = DNS_NEG_TTL_S;
	exp = now + (uint64_t)ttl_s * 1000ULL;

	for (i = 0; i < DNS_CACHE_SLOTS; i++) {
		if (!g_cache[i].used) {
			slot = &g_cache[i];
			break;
		}
	}
	if (!slot)
		slot = &g_cache[0];

	memset(slot, 0, sizeof(*slot));
	slot->used = 1;
	slot->negative = r->negative;
	strncpy(slot->name, name, DNS_NAME_MAX - 1);
	slot->family_filter = family;
	slot->expires_ms = exp;
	slot->n = r->n;
	if (slot->n > DNS_MAX_ADDRS)
		slot->n = DNS_MAX_ADDRS;
	xe_memcpy(slot->addrs, r->addrs, sizeof(XeDnsAddr) * (size_t)slot->n);
}

static int dns_encode_name(uint8_t* dst, size_t dstsz, const char* name) {
	size_t i = 0;
	const char* c = name;

	while (*c) {
		const char* n = strchr(c, '.');
		size_t len;
		if (!n)
			n = c + strlen(c);
		len = (size_t)(n - c);
		if (len == 0 || len > 63 || i + 1 + len >= dstsz)
			return -1;
		dst[i++] = (uint8_t)len;
		xe_memcpy(dst + i, c, len);
		i += len;
		if (!*n)
			break;
		c = n + 1;
	}
	if (i >= dstsz)
		return -1;
	dst[i++] = 0;
	return (int)i;
}

/* Skip / parse a DNS name; returns new offset or -1. */
static int dns_skip_name(const uint8_t* msg, size_t msglen, size_t off) {
	size_t jumps = 0;
	while (off < msglen) {
		uint8_t lab = msg[off];
		if (lab == 0)
			return (int)(off + 1);
		if ((lab & 0xC0) == 0xC0) {
			if (off + 1 >= msglen)
				return -1;
			return (int)(off + 2);
		}
		if ((lab & 0xC0) != 0)
			return -1;
		off += 1u + lab;
		if (++jumps > 128)
			return -1;
	}
	return -1;
}

static int dns_parse_name(const uint8_t* msg,
						  size_t msglen,
						  size_t off,
						  char* out,
						  size_t outsz) {
	size_t oi = 0;
	size_t jumps = 0;
	int first = 1;
	size_t cur = off;
	int end_off = -1;

	if (!out || outsz == 0)
		return -1;
	out[0] = '\0';

	while (cur < msglen) {
		uint8_t lab = msg[cur];
		if (lab == 0) {
			if (end_off < 0)
				end_off = (int)(cur + 1);
			break;
		}
		if ((lab & 0xC0) == 0xC0) {
			if (cur + 1 >= msglen)
				return -1;
			if (end_off < 0)
				end_off = (int)(cur + 2);
			cur = ((size_t)(lab & 0x3F) << 8) | msg[cur + 1];
			if (++jumps > 128)
				return -1;
			continue;
		}
		if ((lab & 0xC0) != 0)
			return -1;
		cur++;
		if (cur + lab > msglen)
			return -1;
		if (!first) {
			if (oi + 1 >= outsz)
				return -1;
			out[oi++] = '.';
		}
		first = 0;
		if (oi + lab >= outsz)
			return -1;
		xe_memcpy(out + oi, msg + cur, lab);
		oi += lab;
		out[oi] = '\0';
		cur += lab;
	}
	return end_off;
}

static int dns_build_query(uint8_t* buf,
						   size_t bufsz,
						   uint16_t qid,
						   const char* name,
						   uint16_t qtype) {
	DNSPacket* pack;
	int nlen;
	size_t need;

	if (bufsz < sizeof(DNSPacket) + 64)
		return -1;
	pack = (DNSPacket*)buf;
	pack->qid = htons(qid);
	pack->flags = htons(0x0100); /* RD */
	pack->questions = htons(1);
	pack->answers = 0;
	pack->authorities = 0;
	pack->additional = 0;
	nlen = dns_encode_name(pack->data, bufsz - sizeof(DNSPacket) - 4, name);
	if (nlen < 0)
		return -1;
	need = sizeof(DNSPacket) + (size_t)nlen + 4;
	if (need > bufsz)
		return -1;
	/* QTYPE + QCLASS in network order */
	{
		uint16_t qt = htons(qtype);
		uint16_t qc = htons(DNS_QCLASS_IN);
		xe_memcpy(pack->data + nlen, &qt, 2);
		xe_memcpy(pack->data + nlen + 2, &qc, 2);
	}
	return (int)need;
}

static int dns_parse_answers(const uint8_t* buf,
							 size_t len,
							 uint16_t want_qid,
							 uint16_t want_type,
							 XeDnsResult* out,
							 uint32_t* min_ttl) {
	DNSPacket* resp;
	uint16_t flags;
	uint16_t qd, an;
	size_t off;
	int i;
	uint32_t ttl_min = DNS_TTL_CAP_S;

	if (!buf || !out || len < sizeof(DNSPacket))
		return -1;
	memset(out, 0, sizeof(*out));
	resp = (DNSPacket*)buf;
	if (ntohs(resp->qid) != want_qid)
		return -1;
	flags = ntohs(resp->flags);
	if ((flags & 0x000F) == 3) { /* NXDOMAIN */
		out->negative = 1;
		if (min_ttl)
			*min_ttl = DNS_NEG_TTL_S;
		return 0;
	}
	if ((flags & 0x000F) != 0)
		return -1;

	qd = ntohs(resp->questions);
	an = ntohs(resp->answers);
	off = sizeof(DNSPacket);

	for (i = 0; i < (int)qd; i++) {
		int noff = dns_skip_name(buf, len, off);
		if (noff < 0 || (size_t)noff + 4 > len)
			return -1;
		off = (size_t)noff + 4;
	}

	for (i = 0; i < (int)an && out->n < DNS_MAX_ADDRS; i++) {
		uint16_t typ, cls, rdlen;
		uint32_t ttl;
		int noff = dns_skip_name(buf, len, off);
		if (noff < 0 || (size_t)noff + 10 > len)
			return -1;
		off = (size_t)noff;
		xe_memcpy(&typ, buf + off, 2);
		xe_memcpy(&cls, buf + off + 2, 2);
		xe_memcpy(&ttl, buf + off + 4, 4);
		xe_memcpy(&rdlen, buf + off + 8, 2);
		typ = ntohs(typ);
		cls = ntohs(cls);
		ttl = ntohl(ttl);
		rdlen = ntohs(rdlen);
		off += 10;
		if (off + rdlen > len)
			return -1;
		(void)cls;
		if (ttl < ttl_min)
			ttl_min = ttl;

		if (typ == want_type || want_type == 0) {
			if (typ == DNS_QTYPE_A && rdlen == 4) {
				XeDnsAddr* a = &out->addrs[out->n++];
				a->family = AF_INET;
				memset(a->addr, 0, 16);
				xe_memcpy(a->addr, buf + off, 4);
				a->ttl = ttl;
			} else if (typ == DNS_QTYPE_AAAA && rdlen == 16) {
				XeDnsAddr* a = &out->addrs[out->n++];
				a->family = AF_INET6;
				xe_memcpy(a->addr, buf + off, 16);
				a->ttl = ttl;
			}
		}
		off += rdlen;
	}

	if (out->n == 0)
		out->negative = 1;
	if (min_ttl)
		*min_ttl = out->negative ? DNS_NEG_TTL_S : (ttl_min ? ttl_min : 30);
	return 0;
}

/*
 * Send one question and wait for a matching response.  The caller owns
 * parsing, which keeps A/AAAA and PTR on the same timeout/route logic.
 */
static int dns_exchange(uint32_t server,
						const char* name,
						uint16_t qtype,
						uint8_t* rbuf,
						size_t rbufsz,
						size_t* rlen,
						uint16_t* qid_out) {
	uint8_t qbuf[512];
	int qlen;
	uint16_t qid;
	int sock;
	sockaddr_in dest;
	int waited = 0;

	if (!server || !name || !rbuf || !rlen)
		return -1;

	qid = (uint16_t)(rand() & 0xFFFF);
	qlen = dns_build_query(qbuf, sizeof(qbuf), qid, name, qtype);
	if (qlen < 0)
		return -1;

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0)
		return -1;

	memset(&dest, 0, sizeof(dest));
	dest.sin_family = AF_INET;
	dest.sin_port = htons(53);
	dest.sin_addr.s_addr = server;

	/* Always record the server we are trying (even if send fails). */
	g_last_dns_server = server;

	/*
	 * DHCP/netmngr may still be installing the default route. Retry
	 * sendto on ENETUNREACH until the FIB has 0/0 or we time out.
	 */
	{
		ssize_t sent = -1;
		int route_waited = 0;
		while (route_waited <= DNS_ROUTE_WAIT_MS) {
			sent = sendto(sock,
						  qbuf,
						  (size_t)qlen,
						  0,
						  (const struct sockaddr*)&dest,
						  sizeof(dest));
			if (sent > 0)
				break;
			_KeProcessSleep(DNS_ROUTE_RETRY_MS);
			route_waited += DNS_ROUTE_RETRY_MS;
		}
		if (sent <= 0) {
			_KeCloseFile(sock);
			return -1;
		}
	}

	while (waited < DNS_TIMEOUT_MS) {
		ssize_t got = recv(sock, rbuf, rbufsz, 0);
		if (got > 0) {
			_KeCloseFile(sock);
			*rlen = (size_t)got;
			if (qid_out)
				*qid_out = qid;
			return 0;
		}
		_KeProcessSleep(DNS_POLL_MS);
		waited += DNS_POLL_MS;
	}
	_KeCloseFile(sock);
	return -1;
}

static int dns_query_one(uint32_t server,
						 const char* name,
						 uint16_t qtype,
						 XeDnsResult* out,
						 uint32_t* ttl) {
	uint8_t rbuf[1550];
	size_t rlen = 0;
	uint16_t qid = 0;

	memset(out, 0, sizeof(*out));
	if (dns_exchange(server, name, qtype, rbuf, sizeof(rbuf), &rlen, &qid) != 0)
		return -1;
	return dns_parse_answers(rbuf, rlen, qid, qtype, out, ttl);
}

static int dns_parse_ptr_answers(const uint8_t* buf,
								 size_t len,
								 uint16_t want_qid,
								 XePtrResult* out) {
	DNSPacket* resp;
	uint16_t flags;
	uint16_t qd, an;
	size_t off;
	int i;

	if (!buf || !out || len < sizeof(DNSPacket))
		return -1;
	memset(out, 0, sizeof(*out));
	resp = (DNSPacket*)buf;
	if (ntohs(resp->qid) != want_qid)
		return -1;
	flags = ntohs(resp->flags);
	if ((flags & 0x000F) == 3) { /* NXDOMAIN */
		out->negative = 1;
		return 0;
	}
	if ((flags & 0x000F) != 0)
		return -1;

	qd = ntohs(resp->questions);
	an = ntohs(resp->answers);
	off = sizeof(DNSPacket);

	for (i = 0; i < (int)qd; i++) {
		int noff = dns_skip_name(buf, len, off);
		if (noff < 0 || (size_t)noff + 4 > len)
			return -1;
		off = (size_t)noff + 4;
	}

	for (i = 0; i < (int)an; i++) {
		uint16_t typ, cls, rdlen;
		uint32_t ttl;
		int noff = dns_skip_name(buf, len, off);
		if (noff < 0 || (size_t)noff + 10 > len)
			return -1;
		off = (size_t)noff;
		xe_memcpy(&typ, buf + off, 2);
		xe_memcpy(&cls, buf + off + 2, 2);
		xe_memcpy(&ttl, buf + off + 4, 4);
		xe_memcpy(&rdlen, buf + off + 8, 2);
		typ = ntohs(typ);
		cls = ntohs(cls);
		ttl = ntohl(ttl);
		rdlen = ntohs(rdlen);
		off += 10;
		if (off + rdlen > len)
			return -1;
		(void)cls;

		if (typ == DNS_QTYPE_PTR && !out->name[0]) {
			if (dns_parse_name(buf, len, off, out->name,
							   sizeof(out->name)) < 0)
				return -1;
			out->ttl = ttl;
		}
		off += rdlen;
	}

	if (!out->name[0])
		out->negative = 1;
	return 0;
}

/* Build the reverse lookup name for in-addr.arpa / ip6.arpa. */
static int dns_make_reverse(const uint8_t* addr,
							int family,
							char* out,
							size_t outsz) {
	static const char hex[] = "0123456789abcdef";
	size_t o = 0;

	if (!addr || !out || outsz == 0)
		return -1;
	if (family == AF_INET) {
		char buf[32];
		int n = snprintf(buf, sizeof(buf), "%u.%u.%u.%u.in-addr.arpa",
						 addr[3], addr[2], addr[1], addr[0]);

		/* snprintf() truncates, so compare the full text with outsz. */
		if (n <= 0 || strlen(buf) >= outsz)
			return -1;
		strcpy(out, buf);
		return 0;
	}
	if (family == AF_INET6) {
		for (int i = 15; i >= 0; i--) {
			if (o + 4 >= outsz)
				return -1;
			out[o++] = hex[addr[i] & 0xF];
			out[o++] = '.';
			out[o++] = hex[(addr[i] >> 4) & 0xF];
			out[o++] = '.';
		}
		if (o + 9 > outsz)
			return -1;
		xe_memcpy(out + o, "ip6.arpa", 8);
		o += 8;
		out[o] = '\0';
		return 0;
	}
	return -1;
}

static int dns_get_servers(uint32_t* servers, int maxn) {
	int sock;
	int n = 0;
	uint8_t lit[4];

	if (!servers || maxn <= 0)
		return 0;

	/* resolv.conf first; kernel global table is fallback (RFC 1035 stub). */
	n = dns_parse_resolv_conf(servers, maxn, NULL);
	if (n > 0)
		return n;

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock >= 0) {
		for (int i = 1; i <= 4 && n < maxn; i++) {
			XEDNSEntry dns;
			memset(&dns, 0, sizeof(dns));
			dns.index = i;
			if (_KeFileIoControl(sock, SOCK_GET_DNS_SERVER, &dns) == 0 && dns.address)
				servers[n++] = dns.address;
		}
		_KeCloseFile(sock);
	}
	if (n > 0)
		return n;

	/*
	 * Last-resort QEMU user-mode networking DNS.  No public resolver is
	 * hardcoded: DHCP (or /resolv.cnf written by NETMngr) is authoritative.
	 */
	if (inet_pton(AF_INET, "10.0.2.3", lit) == 1) {
		xe_memcpy(&servers[0], lit, 4);
		return 1;
	}
	return 0;
}

static int dns_lookup_ptr(const uint8_t* addr,
						  int family,
						  char* out,
						  size_t outsz) {
	uint32_t servers[4];
	int nservers;
	char rev[128];
	uint8_t rbuf[1550];
	size_t rlen = 0;
	uint16_t qid = 0;
	XePtrResult res;

	if (!addr || !out || outsz == 0)
		return -1;
	if (dns_make_reverse(addr, family, rev, sizeof(rev)) != 0)
		return -1;

	nservers = dns_get_servers(servers, 4);
	if (nservers == 0)
		return -1;

	for (int s = 0; s < nservers && s < 2; s++) {
		if (dns_exchange(servers[s], rev, DNS_QTYPE_PTR, rbuf, sizeof(rbuf),
						 &rlen, &qid) != 0)
			continue;
		if (dns_parse_ptr_answers(rbuf, rlen, qid, &res) == 0 &&
			!res.negative && res.name[0]) {
			strncpy(out, res.name, outsz - 1);
			out[outsz - 1] = '\0';
			return 0;
		}
	}
	return -1;
}

static int dns_lookup_udp(const char* name, int family, XeDnsResult* out) {
	uint32_t servers[4];
	int nservers;
	uint16_t types[2];
	int ntypes = 0;
	uint32_t best_ttl = DNS_TTL_CAP_S;
	int any = 0;

	memset(out, 0, sizeof(*out));
	nservers = dns_get_servers(servers, 4);
	if (nservers == 0)
		return -1;

	if (family == AF_INET || family == AF_UNSPEC)
		types[ntypes++] = DNS_QTYPE_A;
	if (family == AF_INET6 || family == AF_UNSPEC)
		types[ntypes++] = DNS_QTYPE_AAAA;

	for (int t = 0; t < ntypes; t++) {
		XeDnsResult part;
		uint32_t ttl = 30;
		int ok = 0;

		memset(&part, 0, sizeof(part));
		/* Try server 1, then retry once on server 2. */
		for (int s = 0; s < nservers && s < 2; s++) {
			if (dns_query_one(servers[s], name, types[t], &part, &ttl) == 0) {
				ok = 1;
				break;
			}
		}
		if (!ok)
			continue;
		if (part.negative && part.n == 0)
			continue;
		any = 1;
		if (ttl < best_ttl)
			best_ttl = ttl;
		for (int i = 0; i < part.n && out->n < DNS_MAX_ADDRS; i++)
			out->addrs[out->n++] = part.addrs[i];
	}

	if (!any) {
		out->negative = 1;
		xe_dns_cache_store(name, family, out, DNS_NEG_TTL_S);
		return -1;
	}
	xe_dns_cache_store(name, family, out, best_ttl);
	return 0;
}

static int xe_resolve(const char* name, int family, XeDnsResult* out) {
	uint8_t lit[16];
	int hosts_n = 0;
	int families[DNS_MAX_ADDRS];
	uint8_t haddrs[DNS_MAX_ADDRS][16];
	int hr;
	XeDnsCacheEntry* ce;

	memset(out, 0, sizeof(*out));
	g_last_dns_server = 0;
	if (!name || !name[0])
		return -1;

	/* Numeric host */
	if (family != AF_INET6 && inet_pton(AF_INET, name, lit) == 1) {
		out->addrs[0].family = AF_INET;
		xe_memcpy(out->addrs[0].addr, lit, 4);
		out->n = 1;
		return 0;
	}
	if (family != AF_INET && inet_pton(AF_INET6, name, lit) == 1) {
		out->addrs[0].family = AF_INET6;
		xe_memcpy(out->addrs[0].addr, lit, 16);
		out->n = 1;
		return 0;
	}

	hr = xe_hosts_lookup(name, &hosts_n, families, haddrs, DNS_MAX_ADDRS);
	if (hr < 0)
		return -1;
	if (hr > 0) {
		for (int i = 0; i < hosts_n && out->n < DNS_MAX_ADDRS; i++) {
			if (family != AF_UNSPEC && families[i] != family)
				continue;
			out->addrs[out->n].family = families[i];
			xe_memcpy(out->addrs[out->n].addr, haddrs[i], 16);
			out->addrs[out->n].ttl = DNS_TTL_CAP_S;
			out->n++;
		}
		return out->n > 0 ? 0 : -1;
	}

	xe_dns_cache_maybe_invalidate();
	ce = xe_dns_cache_find(name, family);
	if (ce) {
		if (ce->negative)
			return -1;
		out->n = 0;
		for (int i = 0; i < ce->n && out->n < DNS_MAX_ADDRS; i++) {
			if (family != AF_UNSPEC && ce->addrs[i].family != family)
				continue;
			out->addrs[out->n++] = ce->addrs[i];
		}
		return out->n > 0 ? 0 : -1;
	}

	return dns_lookup_udp(name, family, out);
}

static int xe_addr_is_loopback(const uint8_t* addr, int family) {
	static const uint8_t lo6[16] = {
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1
	};

	if (!addr)
		return 0;
	if (family == AF_INET)
		return addr[0] == 127;
	if (family == AF_INET6)
		return memcmp(addr, lo6, 16) == 0;
	return 0;
}

struct hostent* gethostbyname(const char* name) {
	static hostent ent;
	XeDnsResult r;
	int family = 0;
	int n = 0;

	memset(&ent, 0, sizeof(ent));
	h_errno = HOST_NOT_FOUND;
	if (!name || !name[0])
		return NULL;

	if (xe_resolve(name, AF_INET, &r) != 0 || r.n == 0) {
		/* Fall back: any family, prefer A then AAAA for h_addr. */
		if (xe_resolve(name, AF_UNSPEC, &r) != 0 || r.n == 0) {
			h_errno = HOST_NOT_FOUND;
			return NULL;
		}
	}

	for (int i = 0; i < r.n; i++) {
		if (r.addrs[i].family == AF_INET) {
			family = AF_INET;
			break;
		}
	}
	if (!family)
		family = AF_INET6;

	for (int i = 0; i < r.n && n < DNS_MAX_ADDRS; i++) {
		if (r.addrs[i].family != family)
			continue;
		memset(g_hostent_addrs[n], 0, 16);
		xe_memcpy(g_hostent_addrs[n], r.addrs[i].addr,
				  family == AF_INET ? 4 : 16);
		g_host_entry_list[n] = (char*)g_hostent_addrs[n];
		n++;
	}
	g_host_entry_list[n] = NULL;

	strncpy(g_hostent_name, name, DNS_NAME_MAX - 1);
	g_hostent_name[DNS_NAME_MAX - 1] = '\0';
	ent.h_name = g_hostent_name;
	ent.h_aliases = NULL;
	ent.h_addrtype = family;
	ent.h_length = family == AF_INET ? 4 : 16;
	ent.h_addr_list = g_host_entry_list;
	h_errno = 0;
	return &ent;
}

struct hostent* gethostbyaddr(const void* addr, socklen_t len, int type) {
	static hostent ent;
	char name[DNS_NAME_MAX];
	const uint8_t* a = (const uint8_t*)addr;

	h_errno = HOST_NOT_FOUND;
	if (!addr || (type != AF_INET && type != AF_INET6)) {
		h_errno = NO_RECOVERY;
		return NULL;
	}
	if ((type == AF_INET && len < 4) || (type == AF_INET6 && len < 16)) {
		h_errno = NO_RECOVERY;
		return NULL;
	}

	if (!xe_hosts_reverse(addr, type, name, sizeof(name))) {
		/* Loopback reverse lookups stay local: 127/8 and ::1 have no
		 * useful PTR record and must never leak a DNS query. */
		if (xe_addr_is_loopback(a, type) ||
			dns_lookup_ptr(a, type, name, sizeof(name)) != 0) {
			h_errno = HOST_NOT_FOUND;
			return NULL;
		}
	}

	memset(&ent, 0, sizeof(ent));
	strncpy(g_hostent_name, name, DNS_NAME_MAX - 1);
	g_hostent_name[DNS_NAME_MAX - 1] = '\0';
	memset(g_hostent_addrs[0], 0, 16);
	xe_memcpy(g_hostent_addrs[0], a, type == AF_INET ? 4 : 16);
	g_host_entry_list[0] = (char*)g_hostent_addrs[0];
	g_host_entry_list[1] = NULL;
	ent.h_name = g_hostent_name;
	ent.h_aliases = NULL;
	ent.h_addrtype = type;
	ent.h_length = type == AF_INET ? 4 : 16;
	ent.h_addr_list = g_host_entry_list;
	h_errno = 0;
	return &ent;
}

static addrinfo* xe_ai_append(addrinfo** head,
							  addrinfo** tail,
							  int family,
							  int socktype,
							  int protocol,
							  const uint8_t* addr,
							  uint16_t port) {
	addrinfo* ai;
	size_t asz = (family == AF_INET) ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
	uint8_t* block;

	block = (uint8_t*)malloc(sizeof(addrinfo) + asz);
	if (!block)
		return NULL;
	memset(block, 0, sizeof(addrinfo) + asz);
	ai = (addrinfo*)block;
	ai->ai_family = family;
	ai->ai_socktype = socktype;
	ai->ai_protocol = protocol;
	ai->ai_addrlen = (socklen_t)asz;
	ai->ai_addr = (sockaddr_*)(block + sizeof(addrinfo));

	if (family == AF_INET) {
		sockaddr_in* in = (sockaddr_in*)ai->ai_addr;
		in->sin_family = AF_INET;
		in->sin_port = port;
		xe_memcpy(&in->sin_addr.s_addr, addr, 4);
	} else {
		sockaddr_in6* in6 = (sockaddr_in6*)ai->ai_addr;
		in6->sin6_family = AF_INET6;
		in6->sin6_port = port;
		xe_memcpy(in6->sin6_addr.s6_addr, addr, 16);
	}

	if (!*head)
		*head = ai;
	else
		(*tail)->ai_next = ai;
	*tail = ai;
	return ai;
}

void freeaddrinfo(addrinfo* res) {
	while (res) {
		addrinfo* n = (addrinfo*)res->ai_next;
		if (res->ai_canonname)
			free(res->ai_canonname);
		free(res);
		res = n;
	}
}

const char* gai_strerror(int errcode) {
	switch (errcode) {
	case 0:
		return "Success";
	case EAI_NONAME:
		return "Name or service not known";
	case EAI_AGAIN:
		return "Temporary failure in name resolution";
	case EAI_FAIL:
		return "Non-recoverable failure in name resolution";
	case EAI_FAMILY:
		return "ai_family not supported";
	case EAI_MEMORY:
		return "Memory allocation failure";
	case EAI_BADFLAGS:
		return "Invalid value for ai_flags";
	case EAI_SERVICE:
		return "Service not supported";
	case EAI_OVERFLOW:
		return "Argument buffer overflow";
	case EAI_SOCKTYPE:
		return "ai_socktype not supported";
	default:
		return "Unknown error";
	}
}

static int xe_service_to_port(const char* service,
							  int socktype,
							  int flags,
							  uint16_t* port) {
	char* end = NULL;
	long p;

	*port = 0;
	if (!service)
		return 0;

	p = strtol(service, &end, 10);
	if (end && end != service && *end == '\0') {
		if (p < 0 || p > 65535)
			return EAI_SERVICE;
		*port = htons((uint16_t)p);
		return 0;
	}
	if (flags & AI_NUMERICSERV)
		return EAI_SERVICE;

	{
		const char* proto = NULL;
		servent* se;

		if (socktype == SOCK_DGRAM)
			proto = "udp";
		else if (socktype == SOCK_STREAM)
			proto = "tcp";
		se = getservbyname(service, proto);
		if (!se)
			return EAI_SERVICE;
		*port = (uint16_t)se->s_port;
		return 0;
	}
}

static int xe_default_protocol(int socktype) {
	if (socktype == SOCK_STREAM)
		return IPPROTOCOL_TCP;
	if (socktype == SOCK_DGRAM)
		return IPPROTOCOL_UDP;
	return 0;
}

static int xe_ai_set_canon(addrinfo* head, const char* node) {
	size_t len;
	char* canon;

	if (!head || !node)
		return 0;
	len = strlen(node) + 1;
	canon = (char*)malloc((unsigned)len);
	if (!canon)
		return -1;
	xe_memcpy(canon, node, len);
	head->ai_canonname = canon;
	return 0;
}

int getaddrinfo(const char* node,
				const char* service,
				const addrinfo* hints,
				addrinfo** res) {
	int family = AF_UNSPEC;
	int socktype = 0;
	int protocol = 0;
	int flags = 0;
	uint16_t port = 0;
	XeDnsResult r;
	addrinfo* head = NULL;
	addrinfo* tail = NULL;
	int rc;
	int i;

	if (!res)
		return EAI_NONAME;
	*res = NULL;
	if (!node && !service)
		return EAI_NONAME;

	if (hints) {
		flags = hints->ai_flags;
		family = hints->ai_family;
		socktype = hints->ai_socktype;
		protocol = hints->ai_protocol;
		if (flags & ~(AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST |
					  AI_NUMERICSERV))
			return EAI_BADFLAGS;
		if (family != AF_UNSPEC && family != AF_INET && family != AF_INET6)
			return EAI_FAMILY;
		if (socktype && socktype != SOCK_STREAM && socktype != SOCK_DGRAM &&
			socktype != SOCK_RAW)
			return EAI_SOCKTYPE;
	}

	rc = xe_service_to_port(service, socktype, flags, &port);
	if (rc != 0)
		return rc;

	if (!protocol)
		protocol = xe_default_protocol(socktype);

	if (!node) {
		uint8_t any4[4] = { 0, 0, 0, 0 };
		uint8_t lo4[4] = { 127, 0, 0, 1 };
		uint8_t any6[16] = { 0 };
		uint8_t lo6[16] = { 0, 0, 0, 0, 0, 0, 0, 0,
							0, 0, 0, 0, 0, 0, 0, 1 };
		const uint8_t* a4 = (flags & AI_PASSIVE) ? any4 : lo4;
		const uint8_t* a6 = (flags & AI_PASSIVE) ? any6 : lo6;

		if (family == AF_INET || family == AF_UNSPEC) {
			if (!xe_ai_append(&head, &tail, AF_INET, socktype, protocol, a4,
							  port)) {
				freeaddrinfo(head);
				return EAI_MEMORY;
			}
		}
		if (family == AF_INET6 || family == AF_UNSPEC) {
			if (!xe_ai_append(&head, &tail, AF_INET6, socktype, protocol, a6,
							  port)) {
				freeaddrinfo(head);
				return EAI_MEMORY;
			}
		}
		if ((flags & AI_CANONNAME) && xe_ai_set_canon(head, node) != 0) {
			freeaddrinfo(head);
			return EAI_MEMORY;
		}
		*res = head;
		return 0;
	}

	if (flags & AI_NUMERICHOST) {
		uint8_t lit[16];

		if (family != AF_INET6 && inet_pton(AF_INET, node, lit) == 1) {
			if (!xe_ai_append(&head, &tail, AF_INET, socktype, protocol, lit,
							  port)) {
				freeaddrinfo(head);
				return EAI_MEMORY;
			}
		} else if (family != AF_INET && inet_pton(AF_INET6, node, lit) == 1) {
			if (!xe_ai_append(&head, &tail, AF_INET6, socktype, protocol, lit,
							  port)) {
				freeaddrinfo(head);
				return EAI_MEMORY;
			}
		} else {
			return EAI_NONAME;
		}
	} else {
		if (xe_resolve(node, family, &r) != 0 || r.n == 0)
			return EAI_NONAME;

		for (i = 0; i < r.n; i++) {
			if (!xe_ai_append(&head,
							  &tail,
							  r.addrs[i].family,
							  socktype,
							  protocol,
							  r.addrs[i].addr,
							  port)) {
				freeaddrinfo(head);
				return EAI_MEMORY;
			}
		}
	}

	if ((flags & AI_CANONNAME) && xe_ai_set_canon(head, node) != 0) {
		freeaddrinfo(head);
		return EAI_MEMORY;
	}
	*res = head;
	return 0;
}

int getnameinfo(const struct sockaddr* sa,
				socklen_t salen,
				char* host,
				socklen_t hostlen,
				char* serv,
				socklen_t servlen,
				int flags) {
	const sockaddr_* s = (const sockaddr_*)sa;
	int family;
	uint8_t addr[16];
	uint16_t port;
	char hostbuf[NI_MAXHOST];
	char servbuf[NI_MAXSERV];
	int named = 0;

	if (!host && !serv)
		return EAI_NONAME;
	if (flags & ~(NI_NOFQDN | NI_NUMERICHOST | NI_NAMEREQD | NI_NUMERICSERV |
				  NI_DGRAM))
		return EAI_BADFLAGS;
	if (!sa)
		return EAI_FAMILY;

	family = s->sa_family;
	if (family == AF_INET) {
		const sockaddr_in* in = (const sockaddr_in*)sa;

		if (salen < sizeof(sockaddr_in))
			return EAI_FAMILY;
		xe_memcpy(addr, &in->sin_addr.s_addr, 4);
		port = ntohs(in->sin_port);
	} else if (family == AF_INET6) {
		const sockaddr_in6* in6 = (const sockaddr_in6*)sa;

		if (salen < sizeof(sockaddr_in6))
			return EAI_FAMILY;
		xe_memcpy(addr, in6->sin6_addr.s6_addr, 16);
		port = ntohs(in6->sin6_port);
	} else {
		return EAI_FAMILY;
	}

	if (host) {
		if (hostlen == 0)
			return EAI_OVERFLOW;
		hostbuf[0] = '\0';
		if (!(flags & NI_NUMERICHOST)) {
			if (xe_hosts_reverse(addr, family, hostbuf, sizeof(hostbuf)))
				named = 1;
			else if (!xe_addr_is_loopback(addr, family) &&
					 dns_lookup_ptr(addr, family, hostbuf,
									sizeof(hostbuf)) == 0)
				named = 1;
		}
		if (!named) {
			if ((flags & NI_NAMEREQD) && !(flags & NI_NUMERICHOST))
				return EAI_NONAME;
			if (!inet_ntop(family, addr, hostbuf, sizeof(hostbuf)))
				return EAI_FAIL;
		}
		if ((flags & NI_NOFQDN) && named) {
			char* dot = strchr(hostbuf, '.');

			if (dot)
				*dot = '\0';
		}
		if (strlen(hostbuf) >= hostlen)
			return EAI_OVERFLOW;
		strcpy(host, hostbuf);
	}

	if (serv) {
		if (servlen == 0)
			return EAI_OVERFLOW;
		if (flags & NI_NUMERICSERV) {
			snprintf(servbuf, sizeof(servbuf), "%u", (unsigned)port);
		} else {
			const char* proto = (flags & NI_DGRAM) ? "udp" : "tcp";
			servent* se = getservbyport(htons(port), proto);

			if (se)
				snprintf(servbuf, sizeof(servbuf), "%s", se->s_name);
			else
				snprintf(servbuf, sizeof(servbuf), "%u", (unsigned)port);
		}
		if (strlen(servbuf) >= servlen)
			return EAI_OVERFLOW;
		strcpy(serv, servbuf);
	}
	return 0;
}
