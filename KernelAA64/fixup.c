/**
* @file dtb.c
* 
* BSD 2-Clause License
*
* Copyright (c) 2022-2026, Manas Kamal Choudhury
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

#include <fixup.h>
#include <Drivers/uart.h>
#include <Mm/vmmngr.h>
#include <bordoisila_ptr.h>

__attribute__((section(".rdata$exA"))) struct _fixup_entry __fixup_start_anchor = {0, 0};
__attribute__((section(".rdata$exZ"))) struct _fixup_entry __fixup_end_anchor = {0, 0};

uint64_t __search_fixup_table(uint64_t fault_pc) {
	struct _fixup_entry* __start__fixup_table = &__fixup_start_anchor + 1;
	struct _fixup_entry* __stop__fixup_table = &__fixup_end_anchor;
	for (struct _fixup_entry* e = __start__fixup_table; e < __stop__fixup_table; e++) {
		if (e->instruction == fault_pc)
			return e->fixup_address;
	}

	return 0;
}

#define USER_LD(dstreg, uptr, faillabel)                                                           \
	__asm__ volatile goto("1: ldtrb %w0, [%1]\n"                                                   \
						  " .pushsection .rdata$exM,\"a\"\n"                                       \
						  ".align 3\n"                                                             \
						  ".quad 1b, %l[" #faillabel "]\n"                                         \
						  " .popsection\n"                                                         \
						  : "=&r"(dstreg)                                                          \
						  : "r"(uptr)                                                              \
						  : "memory"                                                               \
						  : faillabel)

#define USER_ST(srcreg, uptr, faillabel)                                                           \
	__asm__ volatile goto("1: sttrb %w0, [%1]\n"                                                   \
						  ".pushsection .rdata$exM,\"a\"\n"                                        \
						  ".align 3\n"                                                             \
						  ".quad 1b, %l[" #faillabel "]\n"                                         \
						  ".popsection\n" ::"r"(srcreg),                                           \
						  "r"(uptr)                                                                \
						  : "memory"                                                               \
						  : faillabel)

#define USER_MAX 0x0001000000000000

bool _validate_upointer_range(const void* p, size_t n) {
	uintptr_t a = (uintptr_t)p;
	if (a >= USER_MAX)
		return false;
	if (n > USER_MAX - a)
		return false;
	return true;
}
long __bordoisila_copy_from_user(void* dst, const void* usrc, size_t n) {
	if (!_validate_upointer_range(usrc, n))
		return -1;
	uint8_t* d = dst;
	const uint8_t* s = usrc;
	size_t remaining = n;

	while (remaining) {
		uint32_t b;
		USER_LD(b, s, cfu_fail);
		*d = (uint8_t)b;
		d++;
		s++;
		remaining--;
	}
	return 0;

cfu_fail:
	return (long)remaining;
}

long __bordoisila_copy_to_user(void* udst, const void* src, size_t n) {
	if (!_validate_upointer_range(udst, n))
		return -1;

	uint8_t* d = udst;
	const uint8_t* s = src;
	size_t remaining = n;

	while (remaining) {
		uint32_t b = *s;
		USER_ST(b, d, ctu_fail);
		d++;
		s++;
		remaining--;
	}
	return 0;

ctu_fail:
	return (long)remaining;
}

/**
 * @brief __bordoisila_fixup_table_check -- could be called
 * during kernel initialization to check fixup addresses
 */
void __bordoisila_fixup_table_check() {
	struct _fixup_entry* __start__fixup_table = &__fixup_start_anchor + 1;
	struct _fixup_entry* __stop__fixup_table = &__fixup_end_anchor;
	for (struct _fixup_entry* e = __start__fixup_table; e < __stop__fixup_table; e++) {
		UARTDebugOut(
			"__fixup_entry: pc=%x and fault_alt=%x \r\n", e->instruction, e->fixup_address);
	}
}

bool __bordoisila_mm_user_range_mapped(uint64_t addr, size_t len, bool write) {
	if (len == 0)
		return true;
	if (!__BORDOISILA_UBUF_VALIDATE(addr, len))
		return false;

	uint64_t cur = addr;
	uint64_t end = addr + len;

	while (cur < end) {
		uint64_t span = 0;

		if (!AuVmmngrUserWalk(cur, write, &span))
			return false;
		cur += span;
	}
	return true;
}

long _bordoisila_strncpy_from_user(char* dst, const char* usrc, size_t max) {
	if (max == 0 || !__BORDOISILA_KE_PTR_VALIDATE(dst))
		return -1;

	uint64_t addr = (uint64_t)(uintptr_t)usrc;
	size_t i = 0;

	while (i < max) {
		uint64_t cur = addr + i;

		size_t chunk = PAGE_SIZE - (cur & (PAGE_SIZE - 1));
		if (chunk > max - i)
			chunk = max - i;

		if (!__BORDOISILA_UBUF_VALIDATE(cur, chunk))
			goto efault;
		if (!__bordoisila_mm_user_range_mapped(cur, chunk, false))
			goto efault;

		const volatile char* s = (const volatile char*)(uintptr_t)cur;
		for (size_t j = 0; j < chunk; j++) {
			char c = s[j];
			dst[i + j] = c;
			if (c == '\0')
				return (long)(i + j);
		}
		i += chunk;
	}
	dst[0] = '\0';
	return -1;
efault:
	dst[0] = '\0';
	return -1;
}

long _bordoisila_strncpy_to_user(char* udst, const char* ksrc, size_t max) {
	if (!__BORDOISILA_KE_PTR_VALIDATE(ksrc))
		return -1;

	size_t len = 0;
	while (len < max && ksrc[len])
		len++;
	if (len == max)
		return -2;

	size_t total = len + 1;
	uint64_t addr = (uint64_t)(uintptr_t)udst;

	if (!__BORDOISILA_UBUF_VALIDATE(addr, total))
		return -1;
	if (!__bordoisila_mm_user_range_mapped(addr, total, true))
		return -1;

	volatile char* d = (volatile char*)(uintptr_t)addr;
	for (size_t i = 0; i < total; i++)
		d[i] = ksrc[i];

	return (long)len;
}