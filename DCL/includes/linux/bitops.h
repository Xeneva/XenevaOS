/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_BITOPS_H_
#define _LINUX_BITOPS_H_

#include <stdint.h>
#include <bordoisila_bits.h>


#define BITS_TO_LONGS(nr)  BORDOISILA_DIV_ROUND_UP(nr, BORDOISILA_BITS_PER_TYPE(long))
#define BITS_TO_U64(nr)    BORDOISILA_DIV_ROUND_UP(nr, BORDOISILA_BITS_PER_TYPE(uint64_t))
#define BITS_TO_U32(nr)    BORDOISILA_DIV_ROUND_UP(nr, BORDOISILA_BITS_PER_TYPE(uint32_t))
#define BITS_TO_BYTES(nr) BORDOISILA_DIV_ROUND_UP(nr, BORDOISILA_BITS_PER_TYPE(char))
#define BYTES_TO_BITS(nb)  ((nb)* BITS_PER_BYTE)

#endif