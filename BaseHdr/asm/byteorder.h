/*
 * DCL asm/byteorder.h -- route to the generic little-endian scheme.
 *
 * mainline arm64 reaches linux/byteorder/little_endian.h through the arch
 * wrapper; little_endian.h provides the __cpu_to_le and __swab primitives
 * that linux/byteorder/generic.h (fetched byte-identical) aliases, and
 * drm_edid.c's direct include of generic.h then hits its include guard.
 * Xeneva is little-endian, so all le_* conversions are identities.
 *
 * The spellings above matter as text: an earlier version of this comment
 * spelled the two primitives together with a comment-closing run between
 * them, which ended the block four lines early -- the prose after it was
 * then parsed as code and reported as "unknown type name" from
 * <linux/unaligned.h>, the one header that reaches <asm/byteorder.h> before
 * linux/byteorder's own users do.
 */
#ifndef _DCL_ASM_BYTEORDER_H
#define _DCL_ASM_BYTEORDER_H

#include <linux/byteorder/little_endian.h>

#endif /* _DCL_ASM_BYTEORDER_H */
