/**
* BSD 2-Clause License
*
* Copyright (c) 2022, Manas Kamal Choudhury
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

#ifndef __STDINT_H__
#define __STDINT_H__


#define __need_wint_t
#define __need_wchar_t

typedef unsigned char BOOL;

#ifdef ARCH_ARM64
#if !defined(bool)
#include <stdbool.h>
#endif
#endif


/* 7.18.1.1  Exact-width integer types */
typedef signed char int8_t;
typedef unsigned char   uint8_t;
typedef short  int16_t;
typedef unsigned short  uint16_t;
typedef int  int32_t;
typedef unsigned   uint32_t;
typedef long long  int64_t;
typedef unsigned long long   uint64_t;

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;
typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;


/* 7.18.1.2  Minimum-width integer types */
typedef signed char int_least8_t;
typedef unsigned char   uint_least8_t;
typedef short  int_least16_t;
typedef unsigned short  uint_least16_t;
typedef int  int_least32_t;
typedef unsigned   uint_least32_t;
typedef long long  int_least64_t;
typedef unsigned long long   uint_least64_t;

/*  7.18.1.3  Fastest minimum-width integer types
*  Not actually guaranteed to be fastest for all purposes
*  Here we use the exact-width types for 8 and 16-bit ints.
*/
typedef char int_fast8_t;
typedef unsigned char uint_fast8_t;
typedef short  int_fast16_t;
typedef unsigned short  uint_fast16_t;
typedef int  int_fast32_t;
typedef unsigned  int  uint_fast32_t;
typedef long long  int_fast64_t;
typedef unsigned long long   uint_fast64_t;

#if defined(ARCH_ARM64) || defined(__aarch64__) || defined(ARCH_X64) || defined(__x86_64__)
typedef long long intptr_t;
typedef unsigned long long uintptr_t;
#else
typedef int intptr_t;
typedef unsigned int uintptr_t;
#endif

/* 7.18.1.5  Greatest-width integer types */
typedef long long  intmax_t;
typedef unsigned long long   uintmax_t;

#ifdef __SIZE_TYPE__
typedef __SIZE_TYPE__ size_t;
#else
typedef uint64_t size_t;
#endif
/* 7.18.2  Limits of specified-width integer types */
#if defined ( __cplusplus) || defined (__STDC_LIMIT_MACROS)

/* 7.18.2.1  Limits of exact-width integer types */
#define INT8_MIN (-128) 
#define INT16_MIN (-32768)
#define INT32_MIN (-2147483647 - 1)
#define INT64_MIN  (-9223372036854775807LL - 1)

#define INT8_MAX 127
#define INT16_MAX 32767
#define INT32_MAX 2147483647
#define INT64_MAX 9223372036854775807LL
#define LONG_MAX  9223372036854775807L

#define UINT8_MAX 0xff /* 255U */
#define UINT16_MAX 0xffff /* 65535U */
#define UINT32_MAX 0xffffffff  /* 4294967295U */
#define UINT64_MAX 0xffffffffffffffffULL /* 18446744073709551615ULL */

/* 7.18.2.2  Limits of minimum-width integer types */
#define INT_LEAST8_MIN INT8_MIN
#define INT_LEAST16_MIN INT16_MIN
#define INT_LEAST32_MIN INT32_MIN
#define INT_LEAST64_MIN INT64_MIN

#define INT_LEAST8_MAX INT8_MAX
#define INT_LEAST16_MAX INT16_MAX
#define INT_LEAST32_MAX INT32_MAX
#define INT_LEAST64_MAX INT64_MAX

#define UINT_LEAST8_MAX UINT8_MAX
#define UINT_LEAST16_MAX UINT16_MAX
#define UINT_LEAST32_MAX UINT32_MAX
#define UINT_LEAST64_MAX UINT64_MAX

/* 7.18.2.3  Limits of fastest minimum-width integer types */
#define INT_FAST8_MIN INT8_MIN
#define INT_FAST16_MIN INT16_MIN
#define INT_FAST32_MIN INT32_MIN
#define INT_FAST64_MIN INT64_MIN

#define INT_FAST8_MAX INT8_MAX
#define INT_FAST16_MAX INT16_MAX
#define INT_FAST32_MAX INT32_MAX
#define INT_FAST64_MAX INT64_MAX

#define UINT_FAST8_MAX UINT8_MAX
#define UINT_FAST16_MAX UINT16_MAX
#define UINT_FAST32_MAX UINT32_MAX
#define UINT_FAST64_MAX UINT64_MAX

/* 7.18.2.4  Limits of integer types capable of holding
object pointers */
#define INTPTR_MIN INT32_MIN
#define INTPTR_MAX INT32_MAX
#define UINTPTR_MAX UINT32_MAX

/* 7.18.2.5  Limits of greatest-width integer types */
#define INTMAX_MIN INT64_MIN
#define INTMAX_MAX INT64_MAX
#define UINTMAX_MAX UINT64_MAX

/* 7.18.3  Limits of other integer types */
#define PTRDIFF_MIN INT32_MIN
#define PTRDIFF_MAX INT32_MAX

#define SIG_ATOMIC_MIN INT32_MIN
#define SIG_ATOMIC_MAX INT32_MAX
#ifndef SIZE_MAX
#if defined(ARCH_ARM64) || defined(__aarch64__) || defined(ARCH_X64) || defined(__x86_64__)
#define SIZE_MAX UINT64_MAX
#else
#define SIZE_MAX UINT32_MAX
#endif
#endif
#ifndef WCHAR_MIN  /* also in wchar.h */ 
#define WCHAR_MIN 0
#define WCHAR_MAX ((wchar_t)-1) /* UINT16_MAX */
#endif

/*
* wint_t is unsigned short for compatibility with MS runtime
*/
#define WINT_MIN 0
#define WINT_MAX ((wint_t)-1) /* UINT16_MAX */

#endif /* !defined ( __cplusplus) || defined __STDC_LIMIT_MACROS */


/* 7.18.4  Macros for integer constants */
#if !defined ( __cplusplus) || defined __STDC_LIMIT_MACROS 

/* 7.18.4.1  Macros for minimum-width integer constants */

#define INT8_C(val) ((int8_t) + (val))
#define UINT8_C(val) ((uint8_t) + (val##U))
#define INT16_C(val) ((int16_t) + (val))
#define UINT16_C(val) ((uint16_t) + (val##U))

#define INT32_C(val) val##L
#define UINT32_C(val) val##UL
#define INT64_C(val) val##LL
#define UINT64_C(val) val##ULL

/* 7.18.4.2  Macros for greatest-width integer constants */
#define INTMAX_C(val)  INT64_C(val)
#define UINTMAX_C(val) UINT64_C(val)

#endif  /* !defined ( __cplusplus) || defined __STDC_CONSTANT_MACROS */

#ifdef __cplusplus
extern "C++" {
	template <class T, class U> intptr_t raw_diff(T* p1, U* p2)
	{
		return (intptr_t)p1 - (intptr_t)p2;
	};
	template <class T, class U> T raw_offset(U p1, const intptr_t offset)
	{
		return (T)((size_t)p1 + offset);
	};
	template <class T, class U> T mem_after(U* p1)
	{
		return (T)(&p1[1]);
	};
};
#endif

#define RAW_OFFSET(type, x, offset)  (type)((size_t)x + offset)
#define RAW_DIFF(p1,p2) ((intptr_t)p1 - (intptr_t)p2)
#define MEM_AFTER(type, p) ((type)(&(p)[1]))

#define DIV_ROUND_UP(x, y) \
	((x + y - 1) / y)

#define ALIGN_UP(x, y) (DIV_ROUND_UP(x,y)*y)

/*
 * Spelled as a statement expression rather than a bare block: mainline's
 * do_div() yields the remainder, and callers assign from it -- lib/
 * string_helpers.c:98 is `remainder = do_div(size, divisor[units_base]);`.
 * A `{ ... }` macro in that position leaves the assignment with no rvalue and
 * the error lands on the call, not on the macro.  Div64's own definitions in
 * <asm/div64.h> already work this way; this one is for TUs that reach
 * do_div() through <linux/math.h>/<stdint.h> without them.
 *
 * Kept under #ifndef: <asm/div64.h> is the arch definition and wins where it
 * is present, exactly as before.
 */
#ifndef do_div
#define do_div(n, base) ({ \
    uint64_t __base = (base); \
    uint64_t __rem = n % __base; \
    n = n / __base; \
    __rem; \
    })
#endif

#define clamp_t(type, val, lo, hi) \
       ((type)(val) < (type)(lo) ? (type)(lo) : ((type)(val) > (type)(hi)? (type)(hi) : (type)(val)))

static inline int64_t div_round_closest_s64(int64_t x, int64_t divisor) {
	if ((x > 0) == (divisor > 0))
		return (x + divisor / 2) / divisor;
	else
		return (x - divisor / 2) / divisor;
}

static inline uint64_t div_round_closest_u64(uint64_t x, uint64_t divisor) {
	return (x + divisor / 2) / divisor;
}

static inline int32_t div_round_closest_s32(int32_t x, int32_t divisor) {
	if ((x > 0) == (divisor > 0))
		return (x + divisor / 2) / divisor;
	else
		return (x - divisor / 2) / divisor;
}


static inline uint32_t div_round_closest_u32(uint32_t x, uint32_t divisor) {
	return (x + divisor / 2) / divisor;
}

#define DIV_ROUND_CLOSEST(x, divisor) _Generic((x), \
      int64_t: div_round_closest_s64, \
      uint64_t: div_round_closest_u64, \
      int32_t: div_round_closest_s32, \
      uint32_t: div_round_closest_u32, \
      default: div_round_closest_s32 \
)(x,divisor)

#define CLAMP(val, lo, hi)  ((val) < (lo) ? (lo) : ((val) > (hi) ? (hi) : (val)))

#define ABS(x) ((x) < 0 ? -(x) : (x))

#define div64_ul(dividend, divisor)   ((uint64_t)(dividend) / (unsigned long)(divisor))
#endif