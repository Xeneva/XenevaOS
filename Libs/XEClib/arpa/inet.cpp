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

#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <errno.h>

static int inet_pton4(const char* src, void* dst);

uint32_t htonl(uint32_t hostlong) {
	return ((((hostlong) & 0xFF) << 24) | (((hostlong) & 0xFF00) << 8) |
			(((hostlong) & 0xFF0000) >> 8) | (((hostlong) & 0xFF000000) >> 24));
}

uint16_t htons(uint16_t hostshort) {
	return ((((hostshort) & 0xFF) << 8) | (((hostshort) & 0xFF00) >> 8));
}

uint32_t ntohl(uint32_t netlong) {
	return htonl(netlong);
}

uint16_t ntohs(uint16_t netshort) {
	return htons(netshort);
}

in_addr_t inet_addr(const char* in) {
	struct in_addr a;

	/* inet_addr(3) returns INADDR_NONE for every malformed form.  The
	 * old parser called strchr() on each dot and dereferenced NULL for
	 * "1.2.3" and similar input, taking the whole process down. */
	if (!in || inet_pton4(in, &a) != 1)
		return INADDR_NONE;
	return a.s_addr;
}
extern char* inet_ntoa(struct in_addr in) {
	static char buf[17];
	uint32_t hostOrder = ntohl(in.s_addr);
	snprintf(buf,
			 17,
			 "%d.%d.%d.%d",
			 (hostOrder >> 24) & 0xFF,
			 (hostOrder >> 16) & 0xFF,
			 (hostOrder >> 8) & 0xFF,
			 (hostOrder >> 0) & 0xFF);

	return buf;
}

static int hexval(char c) {
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int inet_pton4(const char* src, void* dst) {
	uint8_t tmp[4];
	int i;
	const char* p = src;

	for (i = 0; i < 4; i++) {
		unsigned int v = 0;
		int digits = 0;
		if (*p < '0' || *p > '9')
			return 0;
		while (*p >= '0' && *p <= '9') {
			v = v * 10 + (unsigned)(*p - '0');
			if (v > 255)
				return 0;
			p++;
			digits++;
			if (digits > 3)
				return 0;
		}
		tmp[i] = (uint8_t)v;
		if (i < 3) {
			if (*p != '.')
				return 0;
			p++;
		}
	}
	if (*p != '\0')
		return 0;
	memcpy(dst, tmp, 4);
	return 1;
}

/*
 * Parse IPv6 text form into 16 network-order bytes.
 * Supports :: compression and RFC 4291 dotted-quad suffixes
 * (::ffff:192.0.2.1 and 1:2:3:4:5:6:192.0.2.1).
 */
static int inet_pton6(const char* src, void* dst) {
	uint8_t tmp[16];
	uint8_t* out = tmp;
	uint8_t* end = tmp + sizeof(tmp);
	int compress = -1;
	const char* p;
	char mapped[128];

	memset(tmp, 0, sizeof(tmp));
	if (!src || !dst)
		return 0;

	/*
	 * Convert a dotted-quad suffix into two hexadecimal groups so the
	 * normal parser below only ever sees hex groups.  Keep the second
	 * colon of a "::" run when it is the separator (::1.2.3.4).
	 */
	if (strchr(src, '.')) {
		const char* last = strrchr(src, ':');
		char prefix[96];
		size_t plen;
		uint8_t a4[4];

		if (!last)
			return 0;
		plen = (size_t)(last - src);
		if (plen > 0 && src[plen - 1] == ':')
			plen++;
		if (plen == 0 || plen >= sizeof(prefix))
			return 0;
		memcpy(prefix, (void*)src, plen);
		prefix[plen] = '\0';
		if (inet_pton4(last + 1, a4) != 1)
			return 0;
		if (plen + 12 > sizeof(mapped))
			return 0;
		memcpy(mapped, prefix, plen);
		if (mapped[plen - 1] != ':')
			mapped[plen++] = ':';
		sprintf(mapped + plen, "%x:%x",
				(unsigned)((a4[0] << 8) | a4[1]),
				(unsigned)((a4[2] << 8) | a4[3]));
		src = mapped;
	}

	p = src;
	if (*p == ':') {
		if (p[1] != ':')
			return 0;
		p += 2;
		compress = 0;
	}

	while (*p) {
		unsigned int cur = 0;
		int digits = 0;

		while (hexval(*p) >= 0) {
			cur = (cur << 4) | (unsigned)hexval(*p);
			if (cur > 0xFFFF)
				return 0;
			p++;
			digits++;
		}
		if (!digits)
			return 0;
		if (out + 2 > end)
			return 0;
		*out++ = (uint8_t)((cur >> 8) & 0xFF);
		*out++ = (uint8_t)(cur & 0xFF);

		if (*p == ':') {
			p++;
			if (*p == '\0')
				return 0;
			if (*p == ':') {
				if (compress >= 0)
					return 0;
				compress = (int)(out - tmp);
				p++;
				if (*p == '\0')
					break;
			}
		} else if (*p != '\0') {
			return 0;
		}
	}

	if (compress >= 0) {
		int used = (int)(out - tmp);
		int zeros = (int)sizeof(tmp) - used;

		if (zeros <= 0)
			return 0;
		memmove(tmp + compress + zeros, tmp + compress,
			(size_t)used - (size_t)compress);
		memset(tmp + compress, 0, (size_t)zeros);
		out = end;
	}
	if (out != end)
		return 0;

	memcpy(dst, tmp, sizeof(tmp));
	return 1;
}

int inet_pton(int af, const char* src, void* dst) {
	if (af != AF_INET && af != AF_INET6) {
		errno = EAFNOSUPPORT;
		return -1;
	}
	if (!src || !dst) {
		errno = EINVAL;
		return -1;
	}
	if (af == AF_INET)
		return inet_pton4(src, dst);
	return inet_pton6(src, dst);
}

static const char* inet_ntop4(const void* src, char* dst, size_t size) {
	const uint8_t* a = (const uint8_t*)src;
	char buf[16];
	int n = snprintf(buf, sizeof(buf), "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);

	/*
	 * XEClib snprintf() returns the number of bytes actually stored, not
	 * the length the full conversion needs, so size the result with
	 * strlen() instead of trusting n.
	 */
	if (n < 0 || strlen(buf) >= size) {
		errno = ENOSPC;
		return NULL;
	}
	strcpy(dst, buf);
	return dst;
}

static const char* inet_ntop6(const void* src, char* dst, size_t size) {
	const uint8_t* a = (const uint8_t*)src;
	uint16_t words[8];
	int best_start = -1;
	int best_len = 0;
	int cur_start = -1;
	int cur_len = 0;
	char buf[64];
	char* out = buf;
	int i;

	for (i = 0; i < 8; i++)
		words[i] = (uint16_t)((a[i * 2] << 8) | a[i * 2 + 1]);

	/* RFC 5952 §5: IPv4-mapped addresses keep the dotted-quad suffix. */
	if (words[0] == 0 && words[1] == 0 && words[2] == 0 && words[3] == 0 &&
		words[4] == 0 && words[5] == 0xFFFF) {
		int n = snprintf(buf, sizeof(buf), "::ffff:%u.%u.%u.%u",
						 a[12], a[13], a[14], a[15]);
		if (n < 0 || strlen(buf) >= size) {
			errno = ENOSPC;
			return NULL;
		}
		strcpy(dst, buf);
		return dst;
	}

	for (i = 0; i < 8; i++) {
		if (words[i] == 0) {
			if (cur_start < 0) {
				cur_start = i;
				cur_len = 1;
			} else {
				cur_len++;
			}
		} else {
			if (cur_len > best_len) {
				best_start = cur_start;
				best_len = cur_len;
			}
			cur_start = -1;
			cur_len = 0;
		}
	}
	if (cur_len > best_len) {
		best_start = cur_start;
		best_len = cur_len;
	}
	if (best_len < 2) {
		best_start = -1;
		best_len = 0;
	}

	i = 0;
	while (i < 8) {
		if (best_start >= 0 && i == best_start) {
			*out++ = ':';
			i += best_len;
			if (i >= 8)
				*out++ = ':';
			continue;
		}
		if (out != buf)
			*out++ = ':';
		out += sprintf(out, "%x", words[i]);
		i++;
	}
	*out = '\0';

	if (strlen(buf) >= size) {
		errno = ENOSPC;
		return NULL;
	}
	strcpy(dst, buf);
	return dst;
}

const char* inet_ntop(int af, const void* src, char* dst, size_t size) {
	if (af != AF_INET && af != AF_INET6) {
		errno = EAFNOSUPPORT;
		return NULL;
	}
	if (!src || !dst || size == 0) {
		errno = EINVAL;
		return NULL;
	}
	if (af == AF_INET)
		return inet_ntop4(src, dst, size);
	return inet_ntop6(src, dst, size);
}