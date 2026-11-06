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

#ifndef __STDARG_H__
#define __STDARG_H__

#ifdef __cplusplus
extern "C" {
#endif

/* va list parameter list.
 * On AArch64 va_list is the compiler builtin (see below); the flat
 * pointer version only applies to the legacy x86/x64 path. */
#if !defined(ARCH_ARM64) && !defined(__aarch64__)
typedef unsigned char* va_list;
#endif

/* width of stack == width of int */
#define STACKITEM int64_t

/* round up width of objects pushed on stack. The expression before the
	& ensures that we get 0 for objects of size 0. */
#define VA_SIZE(TYPE) ((sizeof(TYPE) + sizeof(STACKITEM) - 1) & ~(sizeof(STACKITEM) - 1))

#ifdef ARCH_X64
/* &(LASTARG) points to the LEFTMOST argument of the function call
	(before the ...) */
#define va_start(AP, LASTARG) (AP = ((va_list) & (LASTARG) + VA_SIZE(LASTARG)))

/* nothing for va_end */
#define va_end(AP)

#define va_arg(AP, TYPE) (AP += VA_SIZE(TYPE), *((TYPE*)(AP - VA_SIZE(TYPE))))
#elif defined(ARCH_ARM64) || defined(__aarch64__)
/* AArch64: use the compiler's native va_list/va_start/va_arg.
 * The old hand-rolled "ap = &last + 8" pointer walk is wrong on AArch64:
 * it only works when combined with the _store_stack_param register
 * snapshot, which covers printf/sprintf/snprintf but NOT forwarded
 * va_lists (vfprintf/vprintf/vsnprintf/vsprintf, I_Error, M_snprintf).
 * The builtins are correct for the target ABI (AAPCS64 or WoA). */

typedef __builtin_va_list va_list;

#define va_start(ap, last) __builtin_va_start(ap, last)

#define va_arg(ap, T) __builtin_va_arg(ap, T)

#define va_end(ap) __builtin_va_end(ap)

#define va_copy(dst, src) __builtin_va_copy(dst, src)
#endif

#ifdef __cplusplus
}
#endif

#endif