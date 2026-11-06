/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_DMA_DIRECTION_H
#define _LINUX_DMA_DIRECTION_H

/*
 * DCL <linux/dma-direction.h> -- the direction enum, and nothing else.
 *
 * mainline keeps it here so dma-mapping.h, dma-buf.h and the sg_table users
 * can share one name.  DCL's copy existed nowhere: every prototype that took
 * `enum dma_data_direction` had the tag first mentioned inside its own
 * parameter list, which C scopes to that declaration -- so dma-buf.h:579's
 * declaration and the definition in DCL/drm_stubs.c were two different types
 * with the same spelling, and clang said "conflicting types" rather than
 * "you forgot a header".  Pulling the enum into file scope fixes the pair and
 * every later declaration at once.
 *
 * The values are mainline's: the direction is also the argument DCL's
 * stubs ignore, but a caller that passes DMA_FROM_DEVICE into an sg_table
 * walk should not have it become 4.
 */
enum dma_data_direction {
	DMA_BIDIRECTIONAL = 0,
	DMA_TO_DEVICE = 1,
	DMA_FROM_DEVICE = 2,
	DMA_NONE = 3,
};

#endif /* _LINUX_DMA_DIRECTION_H */
