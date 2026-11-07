/**
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

#ifndef __BORDOISILA_BITS_H__
#define __BORDOISILA_BITS_H__

#include <stdint.h>

#define BITS_PER_BYTE 8
#define BITS_PER_LONG (sizeof(long) * BITS_PER_BYTE)

#define BORDOISILA_DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define BITS_TO_LONGS(nr)  BORDOISILA_DIV_ROUND_UP(nr, BITS_PER_LONG)
#define BORDOISILA_BITS_PER_TYPE(type)  (sizeof(type) * BITS_PER_BYTE)


#define BORDOISILA_BIT(nr)   (1UL << (nr))
#define BORDOISILA_GENMASK(h, l)  (((~0UL) - (1UL << (l)) + 1) & (~0UL >> (BITS_PER_LONG - 1 - (h))))

static inline int __bf_shf(uint32_t mask) {
    if (mask == 0) return 0;
    int shift = 0;
    while ((mask & 1) == 0) {
        mask >>= 1;
        shift++;
    }
    return shift;
}

#define BORDOISILA_GET_FIELD(_mask, _reg)    (((_reg) & (_mask)) >> __bf_shf(_mask))

#define BORDOISILA_PREP_FIELD(_mask, _val) \
       (((_val) << __bf_shf(_mask)) & (_mask))

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#endif