/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_TYPES_H_
#define _LINUX_TYPES_H_

#define DECLARE_BITMAP(name,bits) \
     unsigned long name[BITS_TO_LONGS(bits)]




#endif