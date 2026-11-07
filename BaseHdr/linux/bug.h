/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_BUG_H
#define _LINUX_BUG_H

#include <asm/bug.h>
#include <linux/compiler.h>
#include <linux/build_bug.h>

enum bug_trap_type {
	BUG_TRAP_TYPE_NONE = 0,
	BUG_TRAP_TYPE_WARN = 1,
	BUG_TRAP_TYPE_BUG = 2,
};

struct pt_regs;

#ifdef __CHECKER__
#define MAYBE_BUILD_BUG_ON(cond) (0)
#else /* __CHECKER__ */

#define MAYBE_BUILD_BUG_ON(cond)			\
	do {						\
		if (__builtin_constant_p((cond)))       \
			BUILD_BUG_ON(cond);             \
		else                                    \
			BUG_ON(cond);                   \
	} while (0)

#endif	/* __CHECKER__ */

#ifdef CONFIG_GENERIC_BUG
#include <asm-generic/bug.h>

static inline int is_warning_bug(const struct bug_entry *bug)
{
	return bug->flags & BUGFLAG_WARNING;
}

void bug_get_file_line(struct bug_entry *bug, const char **file,
		       unsigned int *line);

struct bug_entry *find_bug(unsigned long bugaddr);

enum bug_trap_type report_bug(unsigned long bug_addr, struct pt_regs *regs);
enum bug_trap_type report_bug_entry(struct bug_entry *bug, struct pt_regs *regs);

/* These are defined by the architecture */
int is_valid_bugaddr(unsigned long addr);

void generic_bug_clear_once(void);

#else	/* !CONFIG_GENERIC_BUG */

/*
 * These stubs keep the prototypes' parameter names for signature parity with
 * the CONFIG_GENERIC_BUG declarations above, so the (void) casts are what
 * keeps -Wextra's unused-parameter warning quiet. That matters here because
 * <linux/bitops.h> includes this header at its bottom (WARN_ONCE
 * reachability), so every DRM TU in the KernelAA64 link parses these lines.
 */
static inline void *find_bug(unsigned long bugaddr)
{
	(void)bugaddr;
	return NULL;
}

static inline enum bug_trap_type report_bug(unsigned long bug_addr,
					    struct pt_regs *regs)
{
	(void)bug_addr;
	(void)regs;
	return BUG_TRAP_TYPE_BUG;
}

struct bug_entry;

static inline enum bug_trap_type
report_bug_entry(struct bug_entry *bug, struct pt_regs *regs)
{
	(void)bug;
	(void)regs;
	return BUG_TRAP_TYPE_BUG;
}

static inline void bug_get_file_line(struct bug_entry *bug, const char **file,
				     unsigned int *line)
{
	(void)bug;
	*file = NULL;
	*line = 0;
}

static inline void generic_bug_clear_once(void) {}

#endif	/* CONFIG_GENERIC_BUG */

#ifdef CONFIG_PRINTK
void mem_dump_obj(void *object);
#else
static inline void mem_dump_obj(void *object) { (void)object; }
#endif

/*
 * Since detected data corruption should stop operation on the affected
 * structures. Return value must be checked and sanely acted on by caller.
 */
/*
 * WARN_ONCE(cond, fmt, ...) -- report a condition once per call site and
 * return its truth, mainline's "warn but do not flood" spelling.
 *
 * The consumers are the DRM set's WARN_ONCE uses, reached three ways: the
 * files that include this header directly, the ones that include it through
 * <linux/kernel.h> -> <linux/bitmap.h> -> <linux/bitops.h> (which includes
 * it at its bottom for exactly this), and drm_print.h:783's drm_WARN_ONCE,
 * which folds a device prefix into it. drm_drv.c:579 is the shape that
 * makes the return value load-bearing:
 *
 *     if (drm_WARN_ONCE(dev, !recovery, "invalid recovery method %u\n", opt))
 *             break;
 *
 * -- the loop over set recovery method bits must not continue on an unknown
 * bit, and the warning is not an error (unlike BUG). It is also why this
 * cannot be a DCL shim function: fmt and __VA_ARGS__ are spliced into the
 * report at the call site, which only a macro can do.
 *
 * The `static int` inside the statement expression is one object per
 * expansion, which is what "once" means here; a per-frame call such as the
 * vkms or vblank paths would otherwise print on every scanout. The body is
 * kernel.h's WARN_ON_ONCE with the fmt form of WARN added, so the UART sink
 * and the file:line trailer stay identical to every other warning this tree
 * prints. Return is the condition's truth (mainline's contract), not
 * WARN_ON_ONCE's `!cond`.
 */
#ifndef WARN_ONCE
#define WARN_ONCE(cond, fmt, ...)					\
	({								\
		static int __warned_once;				\
		int __wret = !!(cond);					\
		if (__wret && !__warned_once) {				\
			__warned_once = 1;				\
			UARTDebugOut("[WARN] " fmt " (%s:%d)\r\n",	\
				##__VA_ARGS__, __FILE__, __LINE__);	\
		}							\
		__wret;							\
	})
#endif

static inline __must_check bool check_data_corruption(bool v) { return v; }
#define CHECK_DATA_CORRUPTION(condition, addr, fmt, ...)		 \
	check_data_corruption(({					 \
		bool corruption = unlikely(condition);			 \
		if (corruption) {					 \
			if (addr)					 \
				mem_dump_obj(addr);			 \
			if (IS_ENABLED(CONFIG_BUG_ON_DATA_CORRUPTION)) { \
				pr_err(fmt, ##__VA_ARGS__);		 \
				BUG();					 \
			} else						 \
				WARN(1, fmt, ##__VA_ARGS__);		 \
		}							 \
		corruption;						 \
	}))

#endif	/* _LINUX_BUG_H */
