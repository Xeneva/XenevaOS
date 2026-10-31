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
#include <sys/socket.h>
#include <arpa/inet.h>

#define XE_HOSTS_MAX 4

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

/**
 * xe_hosts_lookup -- resolve from the static XR/localhost table.
 * @return 1 hit (fills out_*), 0 not special (continue to DNS),
 *         -1 .localhost with no entry (fail locally, no upstream)
 */
int xe_hosts_lookup(const char* name,
					int* out_n,
					int* out_families,
					uint8_t out_addrs[][16],
					int maxn) {
	char key[256];
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

	if (ends_with_localhost(key)) {
		*out_n = 0;
		return -1;
	}
	return 0;
}