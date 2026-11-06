/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_BITMAP_H_
#define _LINUX_BITMAP_H_

#include <string.h>
#include <linux/bitops.h>

#define set_bit(nr, addr) \
 ((addr)[(nr) / BITS_PER_LONG] |= (1UL << ((nr) % BITS_PER_LONG)))

#define clear_bit(nr, addr) \
  ((addr)[(nr) / BITS_PER_LONG] &= ~(1UL << ((nr) % BITS_PER_LONG)))

#define test_bit(nr, addr) \
  (!!((addr)[(nr) / BITS_PER_LONG] & (1UL << ((nr) % BITS_PER_LONG))))

#define test_and_set_bit(nr, addr) \
  ({int __old = test_bit(nr, addr); set_bit(nr, addr); __old; })

#define test_and_clear_bit(nr,addr) \
  ({ int __old = test_bit(nr, addr); clear_bit(nr,addr); __old;})

#define bitmap_zero(dst, nbits) \
 memset(dst, 0, BITS_TO_LONGS(nbits) * sizeof(unsigned long))

#define bitmap_copy(dst, src, nbits) \
 memcpy(dst, src, BITS_TO_LONGS(nbits) * sizeof(unsigned long))

 
 #endif