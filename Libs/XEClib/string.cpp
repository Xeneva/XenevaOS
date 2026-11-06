/**
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

#ifndef _MSC_VER
#define _cdecl
#endif

#include <string.h>
#include <values.h>
#include <stdlib.h>
#include <ctype.h>

#ifdef ARCH_ARM64
#ifdef __clang__
#include <arm_neon.h>
#else
#include <arm64_neon.h>
#endif
#endif

#define SS		   (sizeof(size_t))
#define ALIGN	   (sizeof(size_t) - 1)
#define ONES	   ((size_t)-1 / UCHAR_MAX)
#define HIGHS	   (ONES * (UCHAR_MAX / 2 + 1))
#define HASZERO(x) ((x) - ONES & ~(x) & HIGHS)

//#ifdef _MSC_VER
//#pragma function(memset)
//#endif

void* _cdecl memset(void* targ, int val, size_t len) {
	/*uint8_t *t = (uint8_t*)targ;
	while (len--)
		*t++ = val;*/
	uint8_t* t = (uint8_t*)targ;
	uint8_t byte_val = (uint8_t)val;

	while (len > 0 && ((uintptr_t)t & 7) != 0) {
		*t++ = byte_val;
		len--;
	}

	if (len >= 8) {
		uint64_t bulk_val = byte_val;
		bulk_val |= (bulk_val << 8);
		bulk_val |= (bulk_val << 16);
		bulk_val |= (bulk_val << 32);

		uint64_t* t64 = (uint64_t*)t;
		uint32_t blocks = len / 8;

		while (blocks--)
			*t64++ = bulk_val;

		t = (uint8_t*)t64;
		len %= 8;
	}

	while (len--)
		*t++ = byte_val;
	return t;
}

//void memset(void *targ, unsigned char val, uint32_t len){
//	/*uint8_t *t = (uint8_t*)targ;
//	while (len--)
//		*t++ = val;*/
//	uint8_t* t = (uint8_t*)targ;
//	uint8_t byte_val = (uint8_t)val;
//
//	while (len > 0 && ((uintptr_t)t & 7) != 0) {
//		*t++ = byte_val;
//		len--;
//	}
//
//	if (len >= 8) {
//		uint64_t bulk_val = byte_val;
//		bulk_val |= (bulk_val << 8);
//		bulk_val |= (bulk_val << 16);
//		bulk_val |= (bulk_val << 32);
//
//		uint64_t* t64 = (uint64_t*)t;
//		uint32_t blocks = len / 8;
//
//		while (blocks--)
//			*t64++ = bulk_val;
//
//		t = (uint8_t*)t64;
//		len %= 8;
//	}
//
//	while (len--)
//		*t++ = byte_val;
//}

//#ifdef _MSC_VER
//#pragma function(memcmp)
//#endif
int memcmp(const void* vl, const void* vr, size_t n) {
	const unsigned char *l = (unsigned char*)vl, *r = (unsigned char*)vr;
	for (; n && *l == *r; n--, l++, r++)
		;
	return n ? *l - *r : 0;
}

//#ifdef _MSC_VER
//#pragma function(memcpy)
//#endif

void* memcpy(void* dest, void* src, size_t len) {
	uint8_t* t = (uint8_t*)dest;
	const uint8_t* s = (const uint8_t*)src;

	if (len == 0 || dest == src)
		return 0;

	/* Keep the defensive backward copy for overlapping ranges, but move
	 * 32 bytes per iteration in both directions. Clang turns the four
	 * 64-bit pairs into ldp/stp q pairs, which is what the kernel's copy
	 * already does; the old 8-byte loop halved the receive throughput. */
	if ((t > s) && (t < (s + len))) {
		t += len;
		s += len;

		while (len > 0 && ((uintptr_t)t & 7) != 0) {
			len--;
			*(--t) = *(--s);
		}

		while (len >= 32) {
			uint64_t a, b, c, e;

			t -= 32;
			s -= 32;
			a = ((const uint64_t*)s)[0];
			b = ((const uint64_t*)s)[1];
			c = ((const uint64_t*)s)[2];
			e = ((const uint64_t*)s)[3];
			((uint64_t*)t)[0] = a;
			((uint64_t*)t)[1] = b;
			((uint64_t*)t)[2] = c;
			((uint64_t*)t)[3] = e;
			len -= 32;
		}

		while (len >= 8) {
			t -= 8;
			s -= 8;
			*(uint64_t*)t = *(const uint64_t*)s;
			len -= 8;
		}

		while (len--)
			*(--t) = *(--s);
	}

	else {
		while (len > 0 && ((uintptr_t)t & 7) != 0) {
			*t++ = *s++;
			len--;
		}

		while (len >= 32) {
			uint64_t a = ((const uint64_t*)s)[0];
			uint64_t b = ((const uint64_t*)s)[1];
			uint64_t c = ((const uint64_t*)s)[2];
			uint64_t e = ((const uint64_t*)s)[3];

			((uint64_t*)t)[0] = a;
			((uint64_t*)t)[1] = b;
			((uint64_t*)t)[2] = c;
			((uint64_t*)t)[3] = e;
			t += 32;
			s += 32;
			len -= 32;
		}

		while (len >= 8) {
			*(uint64_t*)t = *(const uint64_t*)s;
			t += 8;
			s += 8;
			len -= 8;
		}

		while (len--)
			*t++ = *s++;
	}
	return dest;
}

#if 0
void *memcpy(void *dest, void *src, uint32_t len) {
	/*const char *sp = (const char*)src;
	char *dp = (char*)dest;
	for (; count != 0; count--) *dp++ = *sp++;*/
	uint8_t* t = (uint8_t*)dest;
	const uint8_t* s = (const uint8_t*)src;

	if (len == 0 || dest == src) return 0;

	if ((t > s) && (t < (s + len))) {
		t += len;
		s += len;

		while (len > 0 && ((uintptr_t)t & 7) != 0) {
			len--;
			*(--t) = *(--s);
		}

		if (len >= 8) {
			uint32_t blocks = len / 8;
			uint64_t* t64 = (uint64_t*)t;
			const uint64_t* s64 = (const uint64_t*)s;

			while (blocks--)
				*(--t64) = *(--s64);

			t = (uint8_t*)t64;
			s = (uint8_t*)s64;
			len %= 8;
		}

		while (len--)
			*(--t) = *(--s);
	}

	else {
		while (len > 0 && ((uintptr_t)t & 7) != 0) {
			*t++ = *s++;
			len--;
		}

		if (len >= 8) {
			uint32_t blocks = len / 8;
			uint64_t* t64 = (uint64_t*)t;
			const uint64_t* s64 = (const uint64_t*)s;

			while (blocks--)
				*t64++ = *s64++;

			t = (uint8_t*)t64;
			s = (uint8_t*)s64;
			len %= 8;
		}

		while (len--)
			*t++ = *s++;
	}
	return dest;
}
#endif

#ifdef _MSC_VER
#pragma function(strcmp)
#endif

int strcmp(const char* str1, const char* str2) {
	int res = 0;
	while (!(res = *(unsigned char*)str1 - *(unsigned char*)str2) && *str2)
		++str1, ++str2;

	if (res < 0)
		res = -1;
	if (res > 0)
		res = 1;

	return res;
}

//#ifdef _MSC_VER
//#pragma function(strcpy)
//#endif

char* strcpy(char* __restrict s1, const char* __restrict s2) {
	char* s1_p = s1;
	for (; (*s1 = *s2); s1++, s2++)
		;
	return s1_p;
}

//#ifdef _MSC_VER
//#pragma function(strlen)
//#endif

size_t strlen(const char* s) {
	const char* a = s;
	for (; *s; s++)
		;
	return s - a;
}

size_t strnlen(const char* string, size_t maxlen) {
	size_t count = 0;

	if (!string) {
		//errno = ERR_NULLPARAMETER;
		return (count = 0);
	}

	while ((string[count] != '\0') && (count <= MAX_STRING_LENGTH) && (count < maxlen)) {
		count++;
	}

	// If this is true, then we probably have an unterminated string
	// constant.  Checking for a string that exceeds MAXSTRINGLENGTH will
	// help to prevent the function from running off too far into memory.
	if (count > MAX_STRING_LENGTH) {
		//errno = ERR_BOUNDS;
		return (count = 0);
	}

	return (count);
}

int strncmp(const char* s1, const char* s2, size_t n) {
	while (n > 0 && *s1 != '\0' && *s1 == *s2) {
		n--, s1++, s2++;
	}

	return (n == 0) ? 0 : (size_t)((unsigned char)*s1 - (unsigned char)*s2);
}

char* strncpy(char* destString, const char* sourceString, size_t maxLength) {
	unsigned count;

	if ((destString == (char*)NULL) || (sourceString == (char*)NULL)) {
		return (destString = NULL);
	}

	if (maxLength > MAX_STRING_LENGTH)
		maxLength = MAX_STRING_LENGTH;

	for (count = 0; count < maxLength; count++) {
		destString[count] = sourceString[count];

		if (sourceString[count] == '\0')
			break;
	}

	if (count >= MAX_STRING_LENGTH) {
		return (destString = NULL);
	}

	return (destString);
}

char* strchrnul(const char* s, int c) {
	size_t* w;
	size_t k;
	c = (unsigned char)c;
	if (!c) {
		return (char*)s + strlen(s);
	}

	for (; (uintptr_t)s % ALIGN; s++) {
		if (!*s || *(unsigned char*)s == c) {
			return (char*)s;
		}
	}
	k = ONES * c;
	for (w = (size_t*)s; !HASZERO(*w) && !HASZERO(*w ^ k); w++)
		;
	for (s = (const char*)w; *s && *(unsigned char*)s != c; s++)
		;
	return (char*)s;
}

//! locates first occurance of character in string
char* strchr(const char* str, int character) {
#ifdef ARCH_X64
	char* r = strchrnul(str, character);
	return *(unsigned char*)r == (unsigned char)character ? r : 0;
#elif ARCH_ARM64
	do {
		if (*str == character)
			return (char*)str;
	} while (*str++);
	return 0;
#endif
}

int strcasecmp(const char* s1, const char* s2) {
	for (; tolower(*s1) == tolower(*s2) && *s1; s1++, s2++)
		;
	return *(unsigned char*)s1 - *(unsigned char*)s2;
}

int strncasecmp(const char* s1, const char* s2, size_t length) {
	int result = 0;

	// Go through the strings, counting as we go.  If we get to the end, or
	// "length" and everything matches, we return 0.  Otherwise, if the strings
	// match partially, we return the count at which they diverge.  If they
	// don't match at all, we return -1

	for (result = 0; ((result < MAX_STRING_LENGTH) && ((unsigned)result < length)); result++) {
		if ((s1[result] == '\0') && (s2[result] == '\0'))
			return (result = 0);

		// Is the ascii code a lowercase alphabet character?
		else if ((s1[result] >= (char)97) && (s1[result] <= (char)122) &&
				 (s2[result] == (s1[result] - (char)32))) {
			// We call it a match
			continue;
		}

		// Is the ascii code an uppercase alphabet character?
		else if ((s1[result] >= (char)65) && (s1[result] <= (char)90) &&
				 (s2[result] == (s1[result] + (char)32))) {
			// We call it a match
			continue;
		} else if (s1[result] != s2[result]) {
			if (!result)
				return (result = -1);

			else
				return (result);
		}
	}

	// If we fall through to here, we matched as many as we could
	return (result = 0);
}

char* strcasestr(const char* s1, const char* s2) {
	int count = 0;
	char* ptr = NULL;
	int s1_length = strlen(s1);
	int s2_length = strlen(s2);

	ptr = (char*)s1;

	for (count = 0; count < s1_length; count++) {
		if (!strncasecmp(ptr, s2, s2_length))
			return (ptr);
		else
			ptr++;
	}

	return (ptr = NULL);
}

char* strcat(char* destString, const char* sourceString) {
	int count1, count2;

	// Find the end of the first String
	for (count1 = 0; count1 < MAX_STRING_LENGTH;) {
		if (!destString[count1])
			break;

		count1 += 1;
	}

	// If this is true, then we possibly have an unterminated string constant.
	// Checking for a string that exceeds MAXSTRINGLENGTH will help to prevent
	// the function from running off too far into memory.
	if (count1 >= MAX_STRING_LENGTH) {
		//errno = ERR_BOUNDS;
		return (destString = NULL);
	}

	// Now copy the source string into the dest until the source is a NULL
	// character.
	for (count2 = 0; count1 < MAX_STRING_LENGTH; count1++, count2++) {
		destString[count1] = sourceString[count2];

		if (!sourceString[count2])
			break;
	}

	return (destString);
}

char* strncat(char* destString, const char* sourceString, size_t maxLength) {
	unsigned count1, count2;
	int endFlag = 0;
	char sourceChar;

	// Find the end of the first String
	for (count1 = 0; count1 < MAX_STRING_LENGTH;) {
		if (destString[count1] == '\0')
			break;
		else
			count1++;
	}

	// If this is true, then we probably have an unterminated string
	// constant.  Checking for a string that exceeds MAXSTRINGLENGTH will
	// help to prevent the function from running off too far into memory.
	if (count1 >= MAX_STRING_LENGTH) {
		//errno = ERR_BOUNDS;
		return (destString = NULL);
	}

	// Now copy the source string into the dest.  If source is shorter than
	// maxLength, pad dest with NULL characters.
	for (count2 = 0; count2 < maxLength;) {
		if ((sourceString[count2] == '\0') || (endFlag == 1)) {
			endFlag = 1;
			sourceChar = (char)NULL;
		} else {
			sourceChar = sourceString[count2];
		}

		destString[count1] = sourceChar;
		count1++;
		count2++;
	}

	// Make sure there's a NULL at the end
	destString[count1] = NULL;

	// Return success
	return (destString);
}

char* strdup(const char* srcString) {
	// I don't like this function.  Anyway, it makes a copy of the string.

	int length = 0;
	char* destString = NULL;

	// Check params
	if (!srcString) {
		return (destString = NULL);
	}

	length = strnlen(srcString, MAX_STRING_LENGTH);

	destString = (char*)malloc(length + 1);
	if (!destString) {
		//errno = ERR_MEMORY;
		return (destString = NULL);
	}

	strncpy(destString, srcString, length);
	destString[length] = '\0';

	// Return success
	return (destString);
}

char* strerror(int error) {
	char* impl = (char*)"Not implemented yet.";
	return impl;
}

char* strrchr(const char* string, int character) {
	char* strptr = NULL;

	// Check params
	if (!string)
		return ((char*)string);

	strptr = (char*)(string + strlen(string) - 1);

	while (strptr >= string) {
		if (strptr[0] == (char)character)
			return (strptr);

		strptr -= 1;
	}

	// Return failure
	return (NULL);
}

size_t strspn(const char* s1, const char* s2) {
	// The strspn() function calculates the length of the initial segment of
	// s1 which consists entirely of characters in s2.

	int match = 0;
	int s1_length = strlen(s1);
	int s2_length = strlen(s2);
	int count;

	for (count = 0; ((count < s1_length) && (count < s2_length)); count++) {
		if (s1[count] != s2[count])
			break;

		match++;
	}

	return (match);
}

char* strstr(const char* s1, const char* s2) {
	int count = 0;
	int s2Len = strlen(s2);

	for (count = 0; s1[0]; count++) {
		if (!strncmp(s1, s2, s2Len))
			return ((char*)s1);
		else
			s1++;
	}

	return NULL;
}

static char* saveptr = NULL;
char* strtok(char* string, const char* delim) {
	// The strtok() function parses a string into a sequence of tokens.  The
	// string to be parsed is passed to the first call of the function along
	// with a second string containing delimiter characters.

	char* token = NULL;

	// Check params
	if (!string) {
		if (!saveptr)
			return (saveptr = NULL);
	} else {
		// This is the first call with this string
		saveptr = string;
	}

	if (!delim) {
		// We need delimiters
		return (saveptr = NULL);
	}

	// Skip any leading delimiter characters
	while (saveptr[0] && strchr((char*)delim, saveptr[0]))
		saveptr += 1;

	if (!saveptr[0]) {
		// Nothing left
		return (saveptr = NULL);
	}

	// Remember the start of the token.  This will be our return value.
	token = saveptr;

	// Move our save pointer along to the next delimiter or NULL
	while (saveptr[0] && !strchr((char*)delim, saveptr[0]))
		saveptr += 1;

	if (saveptr[0]) {
		// Insert a NULL at the delimiter
		saveptr[0] = '\0';

		// Move to the next char.  We don't care what it is (NULL, delimiter,
		// etc) because the next call will deal with that.
		saveptr += 1;
	}

	return (token);
}

char* strtok_r(char* string, const char* delim, char** saveptr) {
	// The strtok_r() function parses a string into a sequence of tokens.  The
	// string to be parsed is passed to the first call of the function along
	// with a second string containing delimiter characters, and a third
	// pointer for saving state between calls.  This is a reentrant version of
	// strtok().

	char* token = NULL;

	// Check params
	if (!saveptr) {
		return (token = NULL);
	}
	if (!string) {
		if (!*saveptr)
			return (*saveptr = NULL);
	} else {
		// This is the first call with this string
		*saveptr = string;
	}

	if (!delim) {
		// We need delimiters
		return (*saveptr = NULL);
	}

	// Skip any leading delimiter characters
	while (*saveptr[0] && strchr((char*)delim, *saveptr[0]))
		*saveptr += 1;

	if (!*saveptr[0]) {
		// Nothing left
		return (*saveptr = NULL);
	}

	// Remember the start of the token.  This will be our return value.
	token = *saveptr;

	// Move our save pointer along to the next delimiter or NULL
	while (*saveptr[0] && !strchr((char*)delim, *saveptr[0]))
		*saveptr += 1;

	if (*saveptr[0]) {
		// Insert a NULL at the delimiter
		*saveptr[0] = '\0';

		// Move to the next char.  We don't care what it is (NULL, delimiter,
		// etc) because the next call will deal with that.
		*saveptr += 1;
	}

	return (token);
}

long long int strtoll(const char* string, char** endString, int base) {
	long int ret = 0;
	int consumed = 0;

	if (!string)
		return 0;

	ret = (long long int)_str2num(string, base, 1, &consumed);

	if (endString)
		*endString = ((char*)string + consumed);

	return (ret);
}

//!===========================================================
//!==========================================================
//! Memory Functions
//!===========================================================
//!-----------------------------------------------------------

void bzero(void* string, size_t number) {
	// The description from the GNU man page reads as follows:
	// The bzero() function sets the first n bytes of the byte string to zero.
	// The bzero() function returns no value.
	memset(string, 0, number);
	return;
}

void bcopy(const void* src, void* dest, size_t len) {
	// The bcopy() function copies the first n bytes of the source string src
	// to the destination string dest.  If len is zero, no bytes are copied.

	size_t count = 0;

	for (count = 0; count < len; count++)
		((char*)dest)[count] = ((char*)src)[count];

	return;
}

int ffs(int i) {
	// Returns the least significant bit set in the word.

	int count;

	if (!i)
		return (0);

	for (count = 1; !(i & 1) && (count <= (int)INTBITS); count++)
		i >>= 1;

	return (count);
}

void* memmove_x64(void* dest, const void* src, size_t n) {
	char* d = (char*)dest;
	const char* s = (const char*)src;

	if (d == s) {
		return d;
	}

	if (s + n <= d || d + n <= s) {
		return memcpy(d, (void*)s, n);
	}

	if (d < s) {
		if ((uintptr_t)s % sizeof(size_t) == (uintptr_t)d % sizeof(size_t)) {
			while ((uintptr_t)d % sizeof(size_t)) {
				if (!n--) {
					return dest;
				}
				*d++ = *s++;
			}
			for (; n >= sizeof(size_t);
				 n -= sizeof(size_t), d += sizeof(size_t), s += sizeof(size_t)) {
				*(size_t*)d = *(size_t*)s;
			}
		}
		for (; n; n--) {
			*d++ = *s++;
		}
	} else {
		if ((uintptr_t)s % sizeof(size_t) == (uintptr_t)d % sizeof(size_t)) {
			while ((uintptr_t)(d + n) % sizeof(size_t)) {
				if (!n--) {
					return dest;
				}
				d[n] = s[n];
			}
			while (n >= sizeof(size_t)) {
				n -= sizeof(size_t);
				*(size_t*)(d + n) = *(size_t*)(s + n);
			}
		}
		while (n) {
			n--;
			d[n] = s[n];
		}
	}

	return dest;
}

void* memmove_aarch64(void* dest, const void* src, size_t n) {
	unsigned char* d = (unsigned char*)dest;
	const unsigned char* s = (const unsigned char*)src;

	if (d == s || n == 0) {
		return dest;
	}

	if (d < s || d >= (s + n)) {
		while (n >= 32) {
			uint8x16x2_t bytes = vld1q_u8_x2(s);
			vst1q_u8_x2(d, bytes);
			s += 32;
			d += 32;
			n -= 32;
		}

		if (n >= 16) {
			uint8x16_t bytes = vld1q_u8(s);
			vst1q_u8(d, bytes);
			s += 16;
			d += 16;
			n -= 16;
		}

		while (n > 0) {
			*d++ = *s++;
			n--;
		}
	}

	else {
		s += n;
		d += n;

		while (n >= 32) {
			s -= 32;
			d -= 32;
			uint8x16x2_t bytes = vld1q_u8_x2(s);
			vst1q_u8_x2(d, bytes);
			n -= 32;
		}
		if (n >= 16) {
			s -= 16;
			d -= 16;
			uint8x16_t bytes = vld1q_u8(s);
			vst1q_u8(d, bytes);
			n -= 16;
		}

		while (n > 0) {
			s--;
			d--;
			*d = *s;
			n--;
		}
	}

	return dest;
}

void* memmove(void* dest, void const* src, unsigned __int64 bytes) {
	unsigned dwords = (bytes >> 2);
#ifdef ARCH_X64
	memmove_x64(dest, src, bytes);
#elif ARCH_ARM64
	return memmove_aarch64(dest, src, bytes);
#endif
}

void* memchr(const void* src, int c, size_t n) {
	const unsigned char* s = (const unsigned char*)src;
	c = (unsigned char)c;
	for (; n && *s != c; s++, n--)
		;
	return n ? (void*)s : 0;
}
