
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
extern "C"
{
#endif


	/*
	 * __clang__ is deliberately *not* tested above. mainline's
	 * <linux/stdarg.h> forwards here, so whichever type is typedef'd
	 * below is the one every drm source sees -- and the two drm users
	 * want opposite things from it. drm_print.c needs the builtins
	 * (clang's va_start rejects a hand-rolled pointer), and
	 * KernelAA64/Drivers/uart.c:129 needs the flat one, because
	 * `va_list args = ((va_list)reg_save_area + 8)` only compiles when
	 * va_list is a pointer it can cast and add to.
	 *
	 * XEClib's userspace copy of this header
	 * (Libs/XEClib/includes/stdarg.h) *has* moved arm-64 to the builtins,
	 * with a note naming the forwarded-va_list cases the pointer walk gets
	 * wrong. When this side follows it, the thing that has to change is
	 * uart.c's register-save-area walk -- not this typedef back and forth.
	 */
#ifdef __GNUC__
	typedef __builtin_va_list va_list;
#else
	/* va list parameter list */
	typedef unsigned char* va_list;
#endif


	/* width of stack == width of int */
#define	STACKITEM	int64_t

	/* round up width of objects pushed on stack. The expression before the
	& ensures that we get 0 for objects of size 0. */
#define	VA_SIZE(TYPE)					\
	((sizeof(TYPE)+sizeof(STACKITEM)-1)	\
	& ~(sizeof(STACKITEM)-1))

#if defined(ARCH_X64) || defined(__x86_64__)
	/* &(LASTARG) points to the LEFTMOST argument of the function call
	(before the ...) */
#define	va_start(AP, LASTARG)	\
	(AP = ((va_list)&(LASTARG)+VA_SIZE(LASTARG)))

	/* nothing for va_end */
#define va_end(AP)

#define va_arg(AP, TYPE)	\
	(AP += VA_SIZE(TYPE), *((TYPE *)(AP - VA_SIZE(TYPE))))
#elif defined(ARCH_ARM64) || defined(__aarch64__)
#ifdef __GNUC__
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)
#else
#define va_start(ap,last) \
     ((ap) = (va_list)(&(last)) + 8)

#define va_arg(ap,T) \
     (*(T*)((ap) += 8, (ap) - 8))

#define va_end(ap) \
     ((ap) = (va_list)0)
#endif
#endif

#ifdef __cplusplus
}
#endif


#endif