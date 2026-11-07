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

#include <stdlib.h>
#include <ctype.h>
#include <sys/_heap.h>
#include <sys/_keproc.h>
#include <sys/utf.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>

#define MB_CUR_MAX MB_LEN_MAX

int abs(int i) {
	if (i < 0)
		return (-i);
	else
		return (i);
}

int atoi(const char* s) {
	int n = 0, neg = 0;
	while (isspace(*s))
		s++;
	switch (*s) {
	case '-':
		neg = 1;
	case '+':
		s++;
	}

	while (isdigit(*s))
		n = 10 * n - (*s++ - '0');
	return neg ? n : -n;
}

double atof(const char* nptr) {
	return strtod(nptr, NULL);
}

//void* malloc (uint32_t s) {
//	return _malloc(s);
//}
//
//void free (void* ptr) {
//	return _free(ptr);
//}
//
//void* realloc (void* address, unsigned int new_size) {
//	return _realloc(address, new_size);
//}
//
//void* calloc (unsigned long long num, unsigned long long size) {
//	return _calloc(num, size);
//}

int mblen(const char* s, size_t n) {
	if (!s)
		return 0;

	if ((n >= 1) && UTF8_IS_1BYTE(s)) {
		if (s[0])
			return 1;
		else
			return 0;
	} else if ((n >= 2) && UTF8_IS_2BYTE(s)) {
		return 2;
	} else if ((n >= 3) && UTF8_IS_3BYTE(s)) {
		return 3;
	} else if ((n >= 4) && UTF8_IS_4BYTE(s)) {
		return 4;
	} else {
		return (-1);
	}
}

size_t mbstowcs(wchar_t* dest, const char* src, size_t n) {
	int num_bytes = 0;
	int max_bytes = (n * MB_CUR_MAX);
	size_t count;

	for (count = 0; (!dest || (count < n)); count++) {
		if (dest)
			num_bytes = mbtowc(dest, src, max_bytes);
		else
			num_bytes = mblen(src, max_bytes);

		if (num_bytes < 0)
			return (-1);

		if (*src == '\0')
			return count;

		dest += 1;
		src += num_bytes;
		max_bytes -= num_bytes;
	}

	return n;
}

int mbtowc(wchar_t* wc, const char* bytes, size_t n) {
	int num_bytes = 0;

	if (!bytes)
		return 0;

	num_bytes = mblen(bytes, n);
	if (num_bytes < 0)
		return -1;

	if (wc)
		*wc = (wchar_t)utf8CharToUnicode(bytes, n);
	return (num_bytes);
}

static uint32_t x = 123456789;
static uint32_t y = 362436069;
static uint32_t z = 521288629;
static uint32_t w = 88675123;

//! NOT IMPLEMENTED
int rand() {
	uint32_t t;
	t = x ^ (x << 11);
	x = y;
	y = z;
	z = w;
	w = w ^ (w >> 19) ^ t ^ (t >> 8);

	return (w & RAND_MAX);
}

int wctomb(char* s, wchar_t wc) {
	int numBytes = 0;

	if (!s)
		// Stateless
		return (0);

	numBytes = utf8CodeWidth(wc);
	if (!numBytes)
		// Too large
		return (-1);

	unicodeToUtf8Char(wc, (unsigned char*)s, sizeof(wchar_t) /* assumed */);

	return (numBytes);
}

size_t wcstombs(char* dest, const wchar_t* src, size_t n) {
	size_t out_bytes = 0;
	int num_bytes = 0;

	while (!dest || (out_bytes < n)) {
		if (dest)
			num_bytes = wctomb(dest, *src);
		else
			num_bytes = utf8CodeWidth(*src);

		if (num_bytes <= 0)
			return -1;

		if (*src == L'\0')
			break;

		dest += num_bytes;
		src += 1;
		out_bytes += num_bytes;
	}

	return out_bytes;
}

static void swap(void* a, void* b, size_t size) {
	if (a == b)
		return;

	char temp[32];
	memcpy(temp, a, size);
	memcpy(a, b, size);
	memcpy(b, temp, size);
}

static char* chars = "0123456789ABCDEF";

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

void exit(int errno) {
	//sys_exit();
	_KePrint("Exiting app \n");
	_KeProcessExit();
	// Should never return; halt if the syscall ever does.
	while (1) { }
}

/*
 * setenv -- set environment variables 
 * @param name -- Key name
 * @param value -- value of the variable
 * @param overwrite -- 0 - append at the last, 1 force-write
 */
int setenv(const char* name, const char* value, int overwrite) {
	_XESetEnvironmentVariable((char*)name, (char*)value, overwrite);
	return 0;
}

/* getenv -- returns value of given variable
 * @param name -- variable name
 */
char* getenv(const char* name) {
	return (char*)_XEGetEnvironmentVariable(name);
}

void itoa_s(int i, unsigned base, char* buf) {
	if (base > 16)
		return;
	if (i < 0) {
		*buf++ = '-';
		i *= -1;
	}
	itoa(i, buf);
}

//! converts a string to a long
long strtol(const char* nptr, char** endptr, int base) {
	const char* s = nptr;
	unsigned long acc;
	int c;
	unsigned long cutoff;
	int neg = 0, any, cutlim;

	/*
	* Skip white space and pick up leading +/- sign if any.
	* If base is 0, allow 0x for hex and 0 for octal, else
	* assume decimal; if base is already 16, allow 0x.
	*/
	do {
		c = *s++;
	} while (isspace(c));
	if (c == '-') {
		neg = 1;
		c = *s++;
	} else if (c == '+')
		c = *s++;
	if ((base == 0 || base == 16) && c == '0' && (*s == 'x' || *s == 'X')) {
		c = s[1];
		s += 2;
		base = 16;
	} else if ((base == 0 || base == 2) && c == '0' && (*s == 'b' || *s == 'B')) {
		c = s[1];
		s += 2;
		base = 2;
	}
	if (base == 0)
		base = c == '0' ? 8 : 10;

	/*
	* Compute the cutoff value between legal numbers and illegal
	* numbers.  That is the largest legal value, divided by the
	* base.  An input number that is greater than this value, if
	* followed by a legal input character, is too big.  One that
	* is equal to this value may be valid or not; the limit
	* between valid and invalid numbers is then based on the last
	* digit.  For instance, if the range for longs is
	* [-2147483648..2147483647] and the input base is 10,
	* cutoff will be set to 214748364 and cutlim to either
	* 7 (neg==0) or 8 (neg==1), meaning that if we have accumulated
	* a value > 214748364, or equal but the next digit is > 7 (or 8),
	* the number is too big, and we will return a range error.
	*
	* Set any if any `digits' consumed; make it negative to indicate
	* overflow.
	*/
	cutoff = neg ? -(unsigned long)LONG_MIN : LONG_MAX;
	cutlim = cutoff % (unsigned long)base;
	cutoff /= (unsigned long)base;
	for (acc = 0, any = 0;; c = *s++) {
		if (isdigit(c))
			c -= '0';
		else if (isalpha(c))
			c -= isupper(c) ? 'A' - 10 : 'a' - 10;
		else
			break;
		if (c >= base)
			break;
		if (any < 0 || acc > cutoff || acc == cutoff && c > cutlim)
			any = -1;
		else {
			any = 1;
			acc *= base;
			acc += c;
		}
	}
	if (any < 0) {
		acc = neg ? LONG_MIN : LONG_MAX;
		//		errno = ERANGE;
	} else if (neg)
		acc = -acc;
	if (endptr != 0)
		*endptr = (char*)(any ? s - 1 : nptr);
	return (acc);
}

//! converts a string to an unsigned long
unsigned long strtoul(const char* nptr, char** endptr, int base) {
	const char* s = nptr;
	unsigned long acc;
	int c;
	unsigned long cutoff;
	int neg = 0, any, cutlim;

	/*
	* See strtol for comments as to the logic used.
	*/
	do {
		c = *s++;
	} while (isspace(c));
	if (c == '-') {
		neg = 1;
		c = *s++;
	} else if (c == '+')
		c = *s++;
	if ((base == 0 || base == 16) && c == '0' && (*s == 'x' || *s == 'X')) {
		c = s[1];
		s += 2;
		base = 16;
	} else if ((base == 0 || base == 2) && c == '0' && (*s == 'b' || *s == 'B')) {
		c = s[1];
		s += 2;
		base = 2;
	}
	if (base == 0)
		base = c == '0' ? 8 : 10;
	cutoff = (unsigned long)ULONG_MAX / (unsigned long)base;
	cutlim = (unsigned long)ULONG_MAX % (unsigned long)base;
	for (acc = 0, any = 0;; c = *s++) {
		if (isdigit(c))
			c -= '0';
		else if (isalpha(c))
			c -= isupper(c) ? 'A' - 10 : 'a' - 10;
		else
			break;
		if (c >= base)
			break;
		if (any < 0 || acc > cutoff || acc == cutoff && c > cutlim)
			any = -1;
		else {
			any = 1;
			acc *= base;
			acc += c;
		}
	}
	if (any < 0) {
		acc = ULONG_MAX;
		//		errno = ERANGE;
	} else if (neg)
		acc = -acc;
	if (endptr != 0)
		*endptr = (char*)(any ? s - 1 : nptr);
	return (acc);
}

//void qsort(void * base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
//
//}

int atexit(void (*func)(void)) {
	(void)func();
	return 0;
}

void abort(void) {
	_KeProcessExit();
	while (1)
		;
}

int system(const char* command) {
	//NOT IMPLEMENTED YET
	return 0;
}
