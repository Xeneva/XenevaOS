/**
* @file bordoisila_ptr.h
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

#ifndef __BORDOISILA_PTR_H__
#define __BORDOISILA_PTR_H__

#include <stdint.h>
#include <aurora.h>
#include <Mm/vmmngr.h>

#ifndef __BORDOISILA_PTR_MIN
#define __BORDOISILA_PTR_MIN 0x10000ULL
#endif
#ifndef __BORDOISILA_PTR_USER_MAX
#define __BORDOISILA_PTR_USER_MAX 0x0000FFFFFFFFFFFFULL /* TTBR0 range, 48-bit VA */
#endif
/*
 * Kernel memory layout, low to high, fixed for now, should be randomized:
 *   0xFFFF800000000000  PHYSICAL_MEM_BASE   
 *   0xFFFFC00000000000  kernel image base   .text/.data/.bss (static objects live here)
 *   0xFFFFE00000000000  KERNEL_BASE_ADDRESS kmalloc/tlsf heap base
 *   0xFFFFFF1000000000  MMIO_BASE           AuMapMMIO()
 */
#ifndef __BORDOISILA_PTR_PHYS_BASE
#define __BORDOISILA_PTR_PHYS_BASE 0xFFFF800000000000ULL
#endif
#ifndef __BORDOISILA_PTR_KIMG_BASE
#define __BORDOISILA_PTR_KIMG_BASE 0xFFFFC00000000000ULL
#endif
#ifndef __BORDOISILA_PTR_HEAP_BASE
#define __BORDOISILA_PTR_HEAP_BASE 0xFFFFE00000000000ULL
#endif
#ifndef __BORDOISILA_PTR_HEAP_END
#define __BORDOISILA_PTR_HEAP_END 0xFFFFFF1000000000ULL
#endif
#ifndef __BORDOISILA_PTR_KERN_MAX
#define __BORDOISILA_PTR_KERN_MAX 0xFFFFFFFFFFFFEFFFULL
#endif
#define __BORDOISILA_PTR_KERN_MIN __BORDOISILA_PTR_PHYS_BASE

#define __BORDOISILA_PTR_REPEATED_BYTE(v)                                                          \
	((uint64_t)(v) == (((uint64_t)(v) & 0xFFULL) * 0x0101010101010101ULL))

#define __BORDOISILA_PTR_REPEATED_WORD(v)                                                          \
	((((uint64_t)(v) >> 32) == ((uint64_t)(v) & 0xFFFFFFFFULL)) ||                                 \
	 (((uint64_t)(v) & 0xFFFFFFFFULL) == 0xFFFFFFFFULL))

#define __BORDOISILA_PTR_POISONED(v)                                                               \
	((uint64_t)(v) == 0xDEADBEEFDEADBEEFULL || (uint64_t)(v) == 0xDEADDEADDEADDEADULL ||           \
	 (uint64_t)(v) == 0xBAADF00DBAADF00DULL || (uint64_t)(v) == 0xFEEEFEEEFEEEFEEEULL ||           \
	 (uint64_t)(v) == 0xDEADBEEFULL || (uint64_t)(v) == 0xBAADF00DULL)

#define __BORDOISILA_PTR_VALIDATE(p)                                                               \
	({                                                                                             \
		uint64_t _v = (uint64_t)(uintptr_t)(p);                                                    \
		int _user = (_v >= __BORDOISILA_PTR_MIN && _v <= __BORDOISILA_PTR_USER_MAX);               \
		int _kern = (_v >= __BORDOISILA_PTR_KERN_MIN && _v <= __BORDOISILA_PTR_KERN_MAX);          \
		/* Heap 0x3000000030 has equal halves; repeated-word is a kernel-pointer test. */         \
		(_user || _kern) && !__BORDOISILA_PTR_REPEATED_BYTE(_v) &&                                 \
			!__BORDOISILA_PTR_POISONED(_v) && (_user || !__BORDOISILA_PTR_REPEATED_WORD(_v));      \
	})

#define __BORDOISILA_HEAP_PTR_VALIDATE(p)                                                          \
	({                                                                                             \
		uint64_t _h = (uint64_t)(uintptr_t)(p);                                                    \
		__BORDOISILA_PTR_OK(_h) &&                                                                 \
			_h >= __BORDOISILA_PTR_HEAP_BASE&& _h < __BORDOISILA_PTR_HEAP_END;                     \
	})

#define __BORDOISILA_PHYS_PTR_VALIDATE(p)                                                          \
	({                                                                                             \
		uint64_t _f = (uint64_t)(uintptr_t)(p);                                                    \
		__BORDOISILA_PTR_VALIDATE(_f) &&                                                           \
			_f >= __BORDOISILA_PTR_PHYS_BASE&& _f < __BORDOISILA_PTR_KIMG_BASE;                    \
	})

#define __BORDOISILA_KE_IMAGE_PTR_VALIDATE(p)                                                      \
	({                                                                                             \
		uint64_t _i = (uint64_t)(uintptr_t)(p);                                                    \
		__BORDOISILA_PTR_VALIDATE(_i) &&                                                           \
			_i >= __BORDOISILA_PTR_KIMG_BASE&& _i < __BORDOISILA_PTR_HEAP_BASE;                    \
	})

#define __BORDOISILA_KE_PTR_VALIDATE(p)                                                            \
	({                                                                                             \
		uint64_t _k = (uint64_t)(uintptr_t)(p);                                                    \
		__BORDOISILA_HEAP_PTR_VALIDATE(_k) || __BORDOISILA_PHYS_PTR_VALIDATE(_k) ||                \
			__BORDOISILA_KIMG_PTR_VALIDATE(_k);                                                    \
	})

/* User-only. */
#define __BORDOISILA_UPTR_VALIDATE(p)                                                              \
	({                                                                                             \
		uint64_t _u = (uint64_t)(uintptr_t)(p);                                                    \
		__BORDOISILA_PTR_VALIDATE(_u) && _u <= __BORDOISILA_PTR_USER_MAX;                          \
	})

/* User buffer: pointer ok, in user range, and [p, p+len) cannot wrap or reach kernel space. */
#define __BORDOISILA_UBUF_VALIDATE(p, len)                                                         \
	({                                                                                             \
		uint64_t _bp = (uint64_t)(uintptr_t)(p);                                                   \
		uint64_t _bl = (uint64_t)(len);                                                            \
		__BORDOISILA_UPTR_VALIDATE(_bp) && _bl <= (__BORDOISILA_PTR_USER_MAX - _bp + 1ULL);        \
	})

#define __BORDOISILA_PTR_VALIDATE_ALIGNED(p, align)                                                \
	(__BORDOISILA_PTR_VALIDATE(p) && (((uintptr_t)(p) & ((align) - 1)) == 0))

#define __BORDOISILA_RETURN_ON_BADPTR(p, ret)                                                      \
	do {                                                                                           \
		if (!__BORDOISILA_PTR_VALIDATE(p))                                                         \
			return (ret);                                                                          \
	} while (0)
#define __BORDOISILA_CONTINUE_IF_BADPTR(p)                                                         \
	if (!__BORDOISILA_PTR_VALIDATE(p))                                                             \
	continue
#endif
