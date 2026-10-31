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
	char ip[16];
	char* c = ip;
	uint32_t out[4];
	char* i;
	memcpy(ip, (void*)in, strlen(in) < 15 ? strlen(in) + 1 : 15);
	ip[15] = '\0';

	i = (char*)strchr(c, '.');
	*i = '\0';
	out[0] = atoi(c);
	c += strlen(c) + 1;

	i = (char*)strchr(c, '.');
	*i = '\0';
	out[1] = atoi(c);
	c += strlen(c) + 1;

	i = (char*)strchr(c, '.');
	*i = '\0';
	out[2] = atoi(c);
	c += strlen(c) + 1;

	out[3] = atoi(c);

	return htonl((out[0] << 24) | (out[1] << 16) | (out[2] << 8) | (out[3]));
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
 * Supports :: compression. Does not support IPv4-mapped dotted suffix.
 */
static int inet_pton6(const char* src, void* dst) {
	uint8_t tmp[16];
	uint16_t hextets[8];
	int hi = 0;
	int compress = -1;
	const char* p = src;
	int saw_digit = 0;
	unsigned int cur = 0;

	memset(tmp, 0, 16);
	memset(hextets, 0, sizeof(hextets));

	if (*p == ':') {
		if (*(p + 1) != ':')
			return 0;
	}

	while (*p) {
		if (*p == ':') {
			if (!saw_digit) {
				if (compress >= 0)
					return 0;
				compress = hi;
				p++;
				if (*p == '\0')
					break;
				continue;
			}
			if (hi >= 8)
				return 0;
			hextets[hi++] = (uint16_t)cur;
			cur = 0;
			saw_digit = 0;
			p++;
			if (*p == '\0')
				return 0;
			continue;
		}
		{
			int hv = hexval(*p);
			if (hv < 0)
				return 0;
			cur = (cur << 4) | (unsigned)hv;
			if (cur > 0xFFFF)
				return 0;
			saw_digit = 1;
			p++;
		}
	}
	if (saw_digit) {
		if (hi >= 8)
			return 0;
		hextets[hi++] = (uint16_t)cur;
	}

	if (compress >= 0) {
		int zeros = 8 - hi;
		int i;
		uint16_t out[8];
		if (zeros <= 0)
			return 0;
		memset(out, 0, sizeof(out));
		for (i = 0; i < compress; i++)
			out[i] = hextets[i];
		for (i = compress; i < hi; i++)
			out[i + zeros] = hextets[i];
		memcpy(hextets, out, sizeof(hextets));
		hi = 8;
	}

	if (hi != 8)
		return 0;

	for (int i = 0; i < 8; i++) {
		tmp[i * 2] = (uint8_t)((hextets[i] >> 8) & 0xFF);
		tmp[i * 2 + 1] = (uint8_t)(hextets[i] & 0xFF);
	}
	memcpy(dst, tmp, 16);
	return 1;
}

int inet_pton(int af, const char* src, void* dst) {
	if (!src || !dst)
		return -1;
	if (af == AF_INET)
		return inet_pton4(src, dst);
	if (af == AF_INET6)
		return inet_pton6(src, dst);
	return -1;
}

static const char* inet_ntop4(const void* src, char* dst, size_t size) {
	const uint8_t* a = (const uint8_t*)src;
	int n = snprintf(dst, size, "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
	if (n < 0 || (size_t)n >= size)
		return NULL;
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

	if (strlen(buf) >= size)
		return NULL;
	strcpy(dst, buf);
	return dst;
}

const char* inet_ntop(int af, const void* src, char* dst, size_t size) {
	if (!src || !dst || size == 0)
		return NULL;
	if (af == AF_INET)
		return inet_ntop4(src, dst, size);
	if (af == AF_INET6)
		return inet_ntop6(src, dst, size);
	return NULL;
}