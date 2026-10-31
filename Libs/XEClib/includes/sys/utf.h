/**
* @file utf.h
* 
* BSD 2-Clause License
*
* Copyright (c) 2022-2023, Manas Kamal Choudhury
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

#ifndef __UTF_H__
#define __UTF_H__

#include <stdint.h>

#define UTF8_1BYTE_CODEMAX 0x7F
#define UTF8_2BYTE_CODEMAX 0x7FF
#define UTF8_3BYTE_CODEMAX 0xFFFF
#define UTF8_4BYTE_CODEMAX 0x10FFFF

#define UTF8_1BYTE_MASK	  0x80
#define UTF8_2BYTE_MASK	  0xE0
#define UTF8_3BYTE_MASK	  0xF0
#define UTF8_4BYTE_MASK	  0xF8
#define UTF8_EXTBYTE_MASK 0xC0

#define UTF8_1BYTE_PREFIX	0x00
#define UTF8_2BYTE_PREFIX	0xC0
#define UTF8_3BYTE_PREFIX	0xE0
#define UTF8_4BYTE_PREFIX	0xF0
#define UTF8_EXTBYTE_PREFIX 0x80

#define UTF8_IS_1BYTE(ptr) ((ptr[0] & UTF8_1BYTE_MASK) == UTF8_1BYTE_PREFIX)

#define UTF8_IS_2BYTE(ptr)                                                                         \
	(((ptr[0] & UTF8_2BYTE_MASK) == UTF8_2BYTE_PREFIX) &&                                          \
	 ((ptr[1] & UTF8_EXTBYTE_MASK) == UTF8_EXTBYTE_PREFIX))

#define UTF8_IS_3BYTE(ptr)                                                                         \
	(((ptr[0] & UTF8_3BYTE_MASK) == UTF8_3BYTE_PREFIX) &&                                          \
	 (((ptr[1] | ptr[2]) & UTF8_EXTBYTE_MASK) == UTF8_EXTBYTE_PREFIX))

#define UTF8_IS_4BYTE(ptr)                                                                         \
	(((ptr[0] & UTF8_4BYTE_MASK) == UTF8_4BYTE_PREFIX) &&                                          \
	 (((ptr[1] | ptr[2] | ptr[3]) & UTF8_EXTBYTE_MASK) == UTF8_EXTBYTE_PREFIX))

static inline unsigned utf8CharToUnicode(const char* ptr, unsigned len) {
	unsigned code = 0;

	if ((len >= 1) && UTF8_IS_1BYTE(ptr)) {
		code = (unsigned)(ptr[0] & (char)~UTF8_1BYTE_MASK);
	} else if ((len >= 2) && UTF8_IS_2BYTE(ptr)) {
		code = (((unsigned)(ptr[0] & (char)~UTF8_2BYTE_MASK) << 6) |
				(ptr[1] & (char)~UTF8_EXTBYTE_MASK));
	} else if ((len >= 3) && UTF8_IS_3BYTE(ptr)) {
		code = (((unsigned)(ptr[0] & (char)~UTF8_3BYTE_MASK) << 12) |
				((unsigned)(ptr[1] & (char)~UTF8_EXTBYTE_MASK) << 6) |
				(ptr[2] & (char)~UTF8_EXTBYTE_MASK));
	} else if ((len >= 4) && UTF8_IS_4BYTE(ptr)) {
		code = (((unsigned)(ptr[0] & (char)~UTF8_4BYTE_MASK) << 18) |
				((unsigned)(ptr[1] & (char)~UTF8_EXTBYTE_MASK) << 12) |
				((unsigned)(ptr[2] & (char)~UTF8_EXTBYTE_MASK) << 6) |
				(ptr[3] & (char)~UTF8_EXTBYTE_MASK));
	}

	return (code);
}

static inline unsigned utf8CodeWidth(unsigned code) {
	if (code <= UTF8_1BYTE_CODEMAX)
		return (1);
	else if (code <= UTF8_2BYTE_CODEMAX)
		return (2);
	else if (code <= UTF8_3BYTE_CODEMAX)
		return (3);
	else if (code <= UTF8_4BYTE_CODEMAX)
		return (4);
	else
		return (0);
}

static inline void unicodeToUtf8Char(unsigned code, unsigned char* ptr, unsigned len) {
	unsigned codeWidth = utf8CodeWidth(code);

	if ((codeWidth == 1) && (len >= codeWidth)) {
		ptr[0] = (code & UTF8_1BYTE_CODEMAX);
	} else if ((codeWidth == 2) && (len >= codeWidth)) {
		ptr[0] = (UTF8_2BYTE_PREFIX | ((code & 0x07D0) >> 6));
		ptr[1] = (UTF8_EXTBYTE_PREFIX | (code & 0x003F));
	} else if ((codeWidth == 3) && (len >= codeWidth)) {
		ptr[0] = (UTF8_3BYTE_PREFIX | ((code & 0xF000) >> 12));
		ptr[1] = (UTF8_EXTBYTE_PREFIX | ((code & 0x0FD0) >> 6));
		ptr[2] = (UTF8_EXTBYTE_PREFIX | (code & 0x003F));
	} else if ((codeWidth == 4) && (len >= codeWidth)) {
		ptr[0] = (UTF8_4BYTE_PREFIX | ((code & 0x001D0000) >> 18));
		ptr[1] = (UTF8_EXTBYTE_PREFIX | ((code & 0x0003F000) >> 12));
		ptr[2] = (UTF8_EXTBYTE_PREFIX | ((code & 0x00000FD0) >> 6));
		ptr[3] = (UTF8_EXTBYTE_PREFIX | (code & 0x0000003F));
	}
}

#define UTF8_ACCEPT 0
#define UTF8_REJECT 1

static uint32_t decode(uint32_t* state, uint32_t* codep, uint32_t byte) {
	static int state_table[32] = {
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0xxxxxxx */
		1, 1, 1, 1, 1, 1, 1, 1,							/* 10xxxxxx */
		2, 2, 2, 2,										/* 110xxxxx */
		3, 3,											/* 1110xxxx */
		4,												/* 11110xxx */
		1												/* 11111xxx */
	};

	static int mask_bytes[32] = {0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
								 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
								 0x00, 0x00, 0x1F, 0x1F, 0x1F, 0x1F, 0x0F, 0x0F, 0x07, 0x00};

	static int next[5] = {0, 1, 0, 2, 3};

	if (*state == UTF8_ACCEPT) {
		*codep = byte & mask_bytes[byte >> 3];
		*state = state_table[byte >> 3];
	} else if (*state > 0) {
		*codep = (byte & 0x3F) | (*codep << 6);
		*state = next[*state];
	}
	return *state;
	//return 0;
}

#endif