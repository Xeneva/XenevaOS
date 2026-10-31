/*
 * DCL <linux/lib/cmdline.h> -- kernel command-line parsing.
 *
 * mainline keeps get_option() and get_options() in lib/cmdline.c and declares
 * them here; get_cmdline() is the odd one out, living in fs/exec.c and
 * declared here all the same. Same split here, with the bodies in
 * DCL/linux_cmdline_shim.c.
 *
 * Declarations rather than static inlines, and that is forced rather than
 * stylistic: the only caller is lib/string_helpers.c:146-154, whose include
 * list (slab.h, string.h, string_helpers.h) does not reach this header. An
 * implicit declaration there compiles -- the tree downgrades
 * -Wimplicit-function-declaration to a warning -- but a static inline emits no
 * symbol, so the two calls would link against nothing. An extern definition
 * works whether or not the caller can see the header.
 *
 * The calling convention is worth reading before the bodies:
 *
 *     get_options(buf, 0, &nints);          // count only
 *     if (!nints)
 *         return -ENOENT;
 *     ints = kzalloc_objs(*ints, nints + 1);
 *     get_options(buf, nints + 1, ints);    // now parse for real
 *
 * `nints` there is an int passed *as the ints array*, not as a count. That is
 * mainline's contract, not a slip in the caller: get_options() writes the
 * number of values it collected into ints[0] and the values after it, so an
 * uninitialized local comes back holding the count. With nints == 0 it counts
 * without writing past ints[0], which is why the first call can hand it a
 * pointer to a single int.
 */
#ifndef _LINUX_LIB_CMDLINE_H
#define _LINUX_LIB_CMDLINE_H

/*
 * get_option() -- read one integer off the front of *str, advancing it.
 *
 * Return values are mainline's and are what get_options() switches on:
 * 0 nothing parseable, 1 an int with nothing after it, 2 an int followed by a
 * comma (already consumed), 3 a hyphen, meaning the number just read is the
 * bottom of a range. A NULL pint turns the call into a pure validator.
 */
int get_option(const char** str, int* pint);

/*
 * get_options() -- parse a comma-separated list of integers and M-N ranges
 * into `ints`, writing the collected count into ints[0].
 *
 * Returns the position parsing stopped at. Neither caller in this tree reads
 * it; both read ints[0].
 */
char* get_options(const char* str, int nints, int* ints);

/*
 * get_cmdline() -- copy a task's command line into `buffer`, returning its
 * length.
 *
 * Takes void* rather than mainline's `struct task_struct *` because this tree
 * has no task_struct->mm to read; see the body for why 0 is the answer the
 * caller can actually use.
 */
int get_cmdline(void* task, char* buffer, int buflen);

#endif /* _LINUX_LIB_CMDLINE_H */
