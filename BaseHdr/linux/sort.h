/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SORT_H
#define _LINUX_SORT_H

#include <linux/types.h>

/**
 * cmp_int - perform a three-way comparison of the arguments
 * @l: the left argument
 * @r: the right argument
 *
 * Return: 1 if the left argument is greater than the right one; 0 if the
 * arguments are equal; -1 if the left argument is less than the right one.
 */
#define cmp_int(l, r) (((l) > (r)) - ((l) < (r)))

void sort_r(void *base, size_t num, size_t size,
	    cmp_r_func_t cmp_func,
	    swap_r_func_t swap_func,
	    const void *priv);

void sort(void *base, size_t num, size_t size,
	  cmp_func_t cmp_func,
	  swap_func_t swap_func);

/* Versions that periodically call cond_resched(): */

void sort_r_nonatomic(void *base, size_t num, size_t size,
		      cmp_r_func_t cmp_func,
		      swap_r_func_t swap_func,
		      const void *priv);

void sort_nonatomic(void *base, size_t num, size_t size,
		    cmp_func_t cmp_func,
		    swap_func_t swap_func);

/*
 * DCL delta -- retire <linux/minmax.h>'s swap() macro for this unit.
 *
 * Vendored lib/sort.c keeps a swap function pointer in a member named `swap`
 * and calls it as `->swap(a, b, (int)size)` (sort.c:140).  The preprocessor
 * cannot tell a member call from an ordinary one, so minmax.h's two-argument
 * swap(a, b) swallows that call and the build stops with "too many arguments
 * provided to function-like macro invocation".  mainline never meets this:
 * its lib/sort.c opens with <linux/types.h>, <linux/export.h> and this
 * header, and none of the three carries minmax.h.  DCL's <linux/types.h> is
 * a re-export of <linux/kernel.h> (types.h:49) and kernel.h:105 includes
 * minmax.h, so every unit in the tree sees the macro.
 *
 * This header is where the retirement goes because it is the one header in
 * that unit's include list that nothing else includes *for* the macro: the
 * other two consumers of <linux/sort.h> (dma-fence-unwrap.c, drm_blend.c)
 * call sort()/sort_r() with no swap function and never name swap() at all,
 * while drm_crtc.c:688 and vkms_formats.c:267/:867-869 -- the calls the
 * macro exists for -- do not include this header.  Nothing in the tree today
 * includes <linux/sort.h> and calls swap(); if one ever does, the failure is
 * a compile error at the call, not a silent wrong expansion.
 *
 * The delta stays one block at the end so a re-port against a later kernel
 * reads as upstream's file plus this.
 */
#undef swap

#endif
