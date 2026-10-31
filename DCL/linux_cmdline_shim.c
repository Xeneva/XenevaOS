/*
 * DCL/linux_cmdline_shim.c -- lib/cmdline.c's three functions.
 *
 * Bodies are mainline's from lib/cmdline.c, with one substitution noted at
 * get_range() and one rewrite at get_cmdline(). Why they are here rather than
 * static inline in <linux/lib/cmdline.h> is at the top of that header: the
 * only caller cannot see the header.
 *
 * The caller is lib/string_helpers.c's parse_int_array() (:142-158), which is
 * the one place this tree parses an integer list.
 */

#include <linux/lib/cmdline.h>
#include <linux/kstrtox.h>	/* simple_strtoull -- the number parser */

/*
 * get_range() -- expand the upper half of an M-N range into the caller's
 * array, and report how many numbers the range covered.
 *
 * `n` is how many slots are left; when it is 0 (the count-only call) the loop
 * writes nothing and only the count comes back. The decrement at the call
 * site is mainline's: it drops the last number of the range so the next
 * get_option() pass reads it as an ordinary value rather than applying the
 * range twice.
 *
 * The upper bound is parsed with simple_strtoull() rather than mainline's
 * simple_strtol(): get_options() documents its ranges as *positive* integers,
 * so the unsigned parse answers the same number for every value the function
 * can be handed -- and the cast keeps the arithmetic in `int`, where the
 * caller's `n` and `pint` already are.  (simple_strtol() itself arrived with
 * kstrtox.h:321 after this shim was written; the parse above does not need to
 * change to match mainline, because every value it is handed is positive by
 * contract.)
 */
static int get_range(const char** str, int* pint, int n)
{
	int x, inc_counter, upper_range;

	(*str)++;
	upper_range = (int)simple_strtoull(*str, NULL, 0);
	inc_counter = upper_range - *pint;
	for (x = *pint; n && x < upper_range; x++, n--)
		*pint++ = x;
	return inc_counter;
}

int get_option(const char** str, int* pint)
{
	const char* cur = *str;
	int value;

	if (!cur || !(*cur))
		return 0;

	if (*cur == '-')
		value = -(int)simple_strtoull(++cur, (char**)str, 0);
	else
		value = (int)simple_strtoull(cur, (char**)str, 0);

	if (pint)
		*pint = value;
	if (cur == *str)
		return 0;
	if (**str == ',')
	{
		(*str)++;
		return 2;
	}
	if (**str == '-')
		return 3;

	return 1;
}

char* get_options(const char* str, int nints, int* ints)
{
	int validate = (nints == 0);
	int res, i = 1;

	/*
	 * `i < nints || validate` is mainline's spelling of "run to the end of
	 * the string when counting, stop when the array is full when filling".
	 */
	while (i < nints || validate)
	{
		int* pint = validate ? ints : ints + i;

		res = get_option(&str, pint);
		if (res == 0)
			break;
		if (res == 3)
		{
			int n = validate ? 0 : nints - i;
			int range_nums;

			range_nums = get_range(&str, pint, n);
			if (range_nums < 0)
				break;
			i += (range_nums - 1);
		}
		i++;
		if (res == 1)
			break;
	}

	/* The line parse_int_array() depends on: the count lands in ints[0]. */
	ints[0] = i - 1;
	return (char*)str;
}

/*
 * get_cmdline() -- copy a task's command line into `buffer`.
 *
 * The one body here that is not mainline's, and the reason is worth stating:
 * mainline's walks task->mm->arg_start through access_remote_vm(), which needs
 * another process's page tables and an MM subsystem to read them through.
 * This kernel has neither -- there is no task_struct->mm, and no remote memory
 * access at all (DCL/linux_mm_shim.c records that uaccess here is a direct
 * copy of the *calling* process's memory, which is not the same thing).
 *
 * 0 is the honest answer and also the safe one for the caller, which is
 * lib/string_helpers.c:689-694:
 *
 *     res = get_cmdline(task, buffer, PAGE_SIZE - 1);
 *     buffer[res] = '\0';
 *     while (--res >= 0 && buffer[res] == '\0')
 *         ;
 *
 * buffer[0] is a legal write, and the loop then steps res to -1 and stops, so
 * the caller sees an empty command line and frees the buffer. A negative
 * error would index before the start of the allocation instead.
 */
int get_cmdline(void* task, char* buffer, int buflen)
{
	(void)task;

	if (buflen > 0)
		buffer[0] = '\0';
	return 0;
}
