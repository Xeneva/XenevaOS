/*
 * DCL asm-generic/bitops/__bitrev.h -- generic___bitrev{8,16,32}.
 *
 * linux/bitrev.h takes its #else branch (CONFIG_HAVE_ARCH_BITREVERSE is
 * off) and aliases these names.  clang's bit-reversal builtins are exact
 * and constant-fold, matching the tables mainline ships.
 */
#ifndef _DCL_ASM_GENERIC_BITOPS___BITREV_H
#define _DCL_ASM_GENERIC_BITOPS___BITREV_H

#include <linux/types.h>

static inline u8 generic___bitrev8(u8 byte)
{
	return (u8)__builtin_bitreverse8(byte);
}

static inline u16 generic___bitrev16(u16 word)
{
	return (u16)__builtin_bitreverse16(word);
}

static inline u32 generic___bitrev32(u32 word)
{
	return __builtin_bitreverse32(word);
}

#endif /* _DCL_ASM_GENERIC_BITOPS___BITREV_H */
