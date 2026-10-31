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

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define XE_HOSTS_MAX 8
#define XE_HOSTS_FILE_MAX 4096

typedef struct {
	const char* name;
	int n;
	int families[XE_HOSTS_MAX];
	const char* literals[XE_HOSTS_MAX];
} XeHostEntry;

/* RFC 6761 §6.3: localhost names MUST resolve to loopback, never to a
 * configured DNS server. XR/AR service names on .localhost keep the
 * headset usable with the radio unplugged. */
static const XeHostEntry k_hosts[] = {
	{ "localhost", 2, { AF_INET, AF_INET6 }, { "127.0.0.1", "::1" } },
	{ "compositor.xr.localhost", 1, { AF_INET }, { "127.0.0.1" } },
	{ "tracking.xr.localhost", 1, { AF_INET }, { "127.0.0.1" } },
	{ "audio.xr.localhost", 1, { AF_INET }, { "127.0.0.1" } },
};

/* /hosts is the FAT 8.3-safe location used by the image; /etc/hosts is
 * accepted for POSIX-style trees.  Missing files are not an error. */
static const char* k_hosts_paths[] = {
	"/hosts",
	"/etc/hosts",
	NULL
};

static int ends_with_localhost(const char* name) {
	size_t n = strlen(name);
	const char* suf = ".localhost";
	size_t sn = 10; /* strlen(".localhost") */
	if (n == 9 && strcasecmp(name, "localhost") == 0)
		return 1;
	if (n < sn)
		return 0;
	return strcasecmp(name + (n - sn), suf) == 0;
}

static void normalize_name(const char* in, char* out, size_t outsz) {
	size_t i = 0;
	if (!in || !out || outsz == 0) {
		if (out && outsz)
			out[0] = '\0';
		return;
	}
	while (in[i] && i + 1 < outsz) {
		out[i] = in[i];
		i++;
	}
	out[i] = '\0';
	/* strip one trailing dot */
	if (i > 1 && out[i - 1] == '.')
		out[i - 1] = '\0';
}

static int hosts_name_eq(const char* a, const char* b) {
	char na[256];
	char nb[256];

	normalize_name(a, na, sizeof(na));
	normalize_name(b, nb, sizeof(nb));
	return strcasecmp(na, nb) == 0;
}

static int hosts_read_file(char* buf, size_t bufsz) {
	for (int p = 0; k_hosts_paths[p]; p++) {
		FILE* f = fopen(k_hosts_paths[p], "r");
		size_t got;

		if (!f)
			continue;
		got = fread(buf, 1, bufsz - 1, f);
		fclose(f);
		buf[got] = '\0';
		return 1;
	}
	return 0;
}

/* Mutates line and returns the next whitespace-delimited token. */
static char* hosts_next_token(char** cursor) {
	char* s = *cursor;
	char* start;

	while (*s == ' ' || *s == '\t')
		s++;
	if (!*s || *s == '\r' || *s == '\n') {
		*cursor = s;
		return NULL;
	}
	start = s;
	while (*s && *s != ' ' && *s != '\t' && *s != '\r' && *s != '\n')
		s++;
	if (*s) {
		*s = '\0';
		s++;
	}
	*cursor = s;
	return start;
}

/* Parse one /hosts line.  Returns 1 when it contains an address, 0 for a
 * blank/comment line, and -1 for a malformed address. */
static int hosts_parse_line(char* line,
							uint8_t addr[16],
							int* family,
							char** names,
							int maxnames,
							int* nnames) {
	char* cursor = line;
	char* tok;
	uint8_t lit[16];

	*names = NULL;
	*nnames = 0;

	while (*cursor == ' ' || *cursor == '\t')
		cursor++;
	if (*cursor == '#' || *cursor == '\0' || *cursor == '\r' || *cursor == '\n')
		return 0;

	/* Drop an inline comment before tokenising. */
	{
		char* hash = strchr(cursor, '#');
		if (hash)
			*hash = '\0';
	}

	tok = hosts_next_token(&cursor);
	if (!tok)
		return 0;

	memset(lit, 0, sizeof(lit));
	if (inet_pton(AF_INET, tok, lit) == 1) {
		*family = AF_INET;
	} else if (inet_pton(AF_INET6, tok, lit) == 1) {
		*family = AF_INET6;
	} else {
		return -1;
	}
	memcpy(addr, lit, 16);

	while (*nnames < maxnames) {
		tok = hosts_next_token(&cursor);
		if (!tok)
			break;
		names[(*nnames)++] = tok;
	}
	return 1;
}

/**
 * xe_hosts_lookup -- resolve from the static XR/localhost table and /hosts.
 * @return 1 hit (fills out_*), 0 not special (continue to DNS),
 *         -1 .localhost with no entry (fail locally, no upstream)
 */
int xe_hosts_lookup(const char* name,
					int* out_n,
					int* out_families,
					uint8_t out_addrs[][16],
					int maxn) {
	char key[256];
	char filebuf[XE_HOSTS_FILE_MAX];
	char* line;
	int have_file;
	size_t i;
	int nfill = 0;

	if (!name || !out_n || !out_families || !out_addrs || maxn <= 0)
		return 0;

	normalize_name(name, key, sizeof(key));
	if (!key[0])
		return 0;

	for (i = 0; i < sizeof(k_hosts) / sizeof(k_hosts[0]); i++) {
		if (strcasecmp(key, k_hosts[i].name) != 0)
			continue;
		for (int j = 0; j < k_hosts[i].n && nfill < maxn; j++) {
			int af = k_hosts[i].families[j];
			uint8_t tmp[16];
			memset(tmp, 0, sizeof(tmp));
			if (inet_pton(af, k_hosts[i].literals[j], tmp) != 1)
				continue;
			out_families[nfill] = af;
			memcpy(out_addrs[nfill], tmp, 16);
			nfill++;
		}
		*out_n = nfill;
		return nfill > 0 ? 1 : -1;
	}

	have_file = hosts_read_file(filebuf, sizeof(filebuf));
	if (have_file) {
		line = filebuf;
		while (*line) {
			char* end = line;
			char* names[XE_HOSTS_MAX];
			int nnames = 0;
			uint8_t addr[16];
			int family = 0;

			while (*end && *end != '\n')
				end++;
			if (*end == '\n')
				*end++ = '\0';

			if (hosts_parse_line(line, addr, &family, names, XE_HOSTS_MAX,
								 &nnames) == 1) {
				for (int j = 0; j < nnames && nfill < maxn; j++) {
					if (!hosts_name_eq(names[j], key))
						continue;
					out_families[nfill] = family;
					memcpy(out_addrs[nfill], addr, 16);
					nfill++;
				}
			}
			line = end;
		}
	}

	if (nfill > 0) {
		*out_n = nfill;
		return 1;
	}
	if (ends_with_localhost(key)) {
		*out_n = 0;
		return -1;
	}
	return 0;
}

/**
 * xe_hosts_reverse -- map an address back to a name using the static
 * table and /hosts.  Returns 1 on a hit, 0 when the address is unknown.
 */
int xe_hosts_reverse(const void* addr, int family, char* out, size_t outsz) {
	const uint8_t* want = (const uint8_t*)addr;
	char filebuf[XE_HOSTS_FILE_MAX];
	char* line;

	if (!addr || !out || outsz == 0)
		return 0;
	if (family != AF_INET && family != AF_INET6)
		return 0;

	for (size_t i = 0; i < sizeof(k_hosts) / sizeof(k_hosts[0]); i++) {
		for (int j = 0; j < k_hosts[i].n; j++) {
			uint8_t lit[16];

			if (k_hosts[i].families[j] != family)
				continue;
			memset(lit, 0, sizeof(lit));
			if (inet_pton(family, k_hosts[i].literals[j], lit) != 1)
				continue;
			if (memcmp(lit, want, family == AF_INET ? 4 : 16) != 0)
				continue;
			strncpy(out, k_hosts[i].name, outsz - 1);
			out[outsz - 1] = '\0';
			return 1;
		}
	}

	if (!hosts_read_file(filebuf, sizeof(filebuf)))
		return 0;

	line = filebuf;
	while (*line) {
		char* end = line;
		char* names[XE_HOSTS_MAX];
		int nnames = 0;
		uint8_t have[16];
		int have_family = 0;

		while (*end && *end != '\n')
			end++;
		if (*end == '\n')
			*end++ = '\0';

		if (hosts_parse_line(line, have, &have_family, names, XE_HOSTS_MAX,
							 &nnames) == 1 &&
			have_family == family && nnames > 0 &&
			memcmp(have, want, family == AF_INET ? 4 : 16) == 0) {
			strncpy(out, names[0], outsz - 1);
			out[outsz - 1] = '\0';
			return 1;
		}
		line = end;
	}
	return 0;
}
