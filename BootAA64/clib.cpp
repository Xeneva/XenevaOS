/**
* BSD 2-Clause License
*
* Copyright (c) 2023-2025, Manas Kamal Choudhury
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
#include <intrin.h>
#include "clib.h"

#if defined(_MSC_VER) && !defined(__clang__)
#pragma function(memcpy, memset, strlen)
#endif

extern "C" {
void* memset(void* targ, uint8_t val, uint32_t len) {
	uint8_t* t = (uint8_t*)targ;
	while (len--)
		*t++ = val;
	return targ;
}

void* _memcpy(void* targ, void* src, uint32_t len) {
	uint8_t* t = (uint8_t*)targ;
	uint8_t* s = (uint8_t*)src;
	if (len > 0) {
		// check to see if target is in the range of src and if so, do a memmove() instead
		if ((t > s) && (t < (s + len))) {
			t += (len - 1);
			s += (len - 1);
			while (len--)
				*t-- = *s--;
		} else {
			while (len--)
				*t++ = *s++;
		}
	}
	return targ;
}

void* memcpy(void* targ, void* src, uint32_t len) {
	return _memcpy(targ, src, len);
}

size_t strlen(const char* s) {
	size_t l = 0;
	while (*s++)
		++l;
	return l;
}
}

wchar_t* wstrchr(wchar_t* s, int c) {
	while (*s) {
		if (*s == c)
			return s;
		s++;
	}

	return 0;
}

int wstrlen(wchar_t* s) {
	int i = 0;
	while (*s)
		i++, s++;
	return i;
}

uint32_t wstrsize(wchar_t* s) {
	return (wstrlen(s) + 1) * sizeof(unsigned short);
}

int to_upper(int c) {
	if ((c >= 'a') && (c <= 'z'))
		return (c - ('a' - 'A'));
	return c;
}

int to_lower(int c) {
	if ((c >= 'A') && (c <= 'Z'))
		return (c + ('a' - 'A'));
	return c;
}

int is_digit(int c) {
	return ((c >= '0') && (c <= '9'));
}

char* chars = (char*)"0123456789ABCDEF";

char* sztoa(size_t value, char* str, int base) {
	if (base < 2 || base > 16)
		return nullptr;
	unsigned int i = 0;
	do {
		str[++i] = chars[value % base];
		value /= base;
	} while (value != 0);
	str[0] = '\0';
	for (unsigned int z = 0; z < i; ++z, --i) {
		char tmp = str[z];
		str[z] = str[i];
		str[i] = tmp;
	}
	return str;
}
