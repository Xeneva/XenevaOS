#ifndef __LINUX_KSTRTOX_H__
#define __LINUX_KSTRTOX_H__

#include <stdbool.h>	/* bool, for kstrtobool's out parameter */

/*
 * DCL <linux/kstrtox.h> -- kstrtou8(), the only string-to-number helper any
 * ported file has asked for so far.
 *
 * The caller is rx_trig_bytes_store at 8250_port.c:3078:
 *
 *     ret = kstrtou8(buf, 10, &bytes);
 *
 * which parses a number a userspace write() put in the sysfs attribute.  It
 * has to be a real parser, not a stub: this is the one place where a value
 * crosses from userspace into the driver, and a parser that returned a
 * constant would let a write appear to succeed while programming something
 * else.  The three failure returns matter for the same reason -- the caller
 * tests `ret` and answers write() with it, so -EINVAL is what turns a bad
 * write into EIO at the syscall rather than a silent acceptance.
 *
 * Deliberately hand-rolled: DCL has no strtol family anywhere in BaseHdr (the
 * only formatters are _sprintf/_snprintf in <Log/_print.h>), and pulling a
 * whole formatted-input facility in for one two-digit field would be the
 * "speculative surface" <linux/cleanup.h> warns about.  If a later stage
 * needs kstrtoint/kstrtoul, they belong here beside it, not in a second header.
 *
 *   upstream  include/linux/kstrtox.h  (mainline v7.2), return-value contract
 *   unchanged: 0, -EINVAL for a malformed string, -ERANGE if it does not fit.
 */
#include <linux/errno.h>	/* -EINVAL, -ERANGE */
#include <linux/types.h>	/* u8 */

static inline int kstrtou8(const char* s, unsigned int base, u8* res)
{
	unsigned long v = 0;
	int any = 0;

	if (!s || !res || (base != 0 && base != 10 && base != 16))
		return -EINVAL;

	while (*s == ' ' || *s == '\t')
		s++;
	if (*s == '-')
		return -EINVAL;	/* unsigned: a sign is a parse error, not a wrap */
	if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		base = 16;
		s += 2;
	} else if (base == 0) {
		base = 10;
	}

	for (;; s++) {
		unsigned int d;

		if (*s >= '0' && *s <= '9')
			d = (unsigned int)(*s - '0');
		else if (base == 16 && *s >= 'a' && *s <= 'f')
			d = (unsigned int)(*s - 'a') + 10;
		else if (base == 16 && *s >= 'A' && *s <= 'F')
			d = (unsigned int)(*s - 'A') + 10;
		else
			break;

		if (d >= base)
			break;
		v = v * base + d;
		if (v > 0xFFUL)
			return -ERANGE;
		any = 1;
	}

	/* sysfs buffers arrive newline-terminated; a single trailing \n is part
	 * of the format, anything else after the digits is not. */
	while (*s == ' ' || *s == '\t')
		s++;
	if (*s == '\n')
		s++;
	if (*s != '\0' || !any)
		return -EINVAL;

	*res = (u8)v;
	return 0;
}


/*
 * kstrtobool(buf, res) -- parse the text form of a boolean.
 *
 * The caller is the `console` sysfs store at serial_core.c:2980, which reads
 * a line from userspace and turns it into the CON_ENABLED bit.  So the token
 * set is mainline's, and it has to be *exactly* mainline's: userspace writes
 * "Y", "1", "on" and "N", "0", "off" expecting those spellings to work, and
 * an answer of -EINVAL for an unrecognised token is what makes a bad write
 * fail at the syscall rather than silently disable the console.
 *
 * A single trailing newline is tolerated (the sysfs buffer carries one);
 * everything else after the token is an error.  Unlike kstrtou8() above,
 * both branches are short -- no accumulation, no range to check -- so this
 * stays a two-comparison function rather than sharing its loop.
 *
 *   upstream  include/linux/kstrtox.h  (mainline v7.2)
 */
static inline int kstrtobool(const char* s, bool* res)
{
	const char* p = s;
	bool v;

	if (!s || !res)
		return -EINVAL;

	switch (*p)
	{
	case 'y': case 'Y': case '1':
		v = true;
		p++;
		break;
	case 'n': case 'N': case '0':
		v = false;
		p++;
		break;
	case 'o': case 'O':
		/* "on" and "off" are the two things 'o' can start, and they mean
		 * opposite things -- so both letters have to be read before the
		 * value is decided, not just the first.  The first version of
		 * this function branched on the first letter alone and reported
		 * "off" as true, which is exactly the bug a hand-rolled parser
		 * earns. */
		p++;
		if (*p == 'n' || *p == 'N')
		{
			v = true;
			p++;
		}
		else if ((*p == 'f' || *p == 'F') &&
			 (p[1] == 'f' || p[1] == 'F'))
		{
			v = false;
			p += 2;
		}
		else
		{
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	/* The sysfs store buffer carries one newline; anything else left over
	 * is a second token the writer did not mean to send ("1 1"), and
	 * accepting it would let a malformed write take effect off the
	 * well-formed prefix alone. */
	if (*p == '\n')
		p++;
	if (*p != '\0')
		return -EINVAL;

	*res = v;
	return 0;
}


/*
 * simple_strtoull() and simple_strtoul() -- and the "simple" in the name is
 * a warning about the contract, not a comment on the code.
 *
 * They differ from kstrtoull()/kstrtoul() in exactly the one way that
 * matters at their two call sites, and it is why mainline's own doc comment
 * on these reads "This function has caveats. Please use kstrtoull instead.":
 * no sign rejection, no end-of-string check, no error return.  They consume
 * digits until one does not fit the radix and hand back whatever accumulated,
 * leaving *endp at the stopping character.  Both callers here pass endp as
 * NULL, so what they want is the value and nothing else.  A kstrtoull() in
 * their place would reject every one of them:
 *
 *   serial_core.c:2136  *addr = simple_strtoull(p, NULL, 0);
 *       uart_parse_earlycon(), whose string is an MMIO address followed by
 *       ",options".  kstrtoull() demands a NUL immediately after the digits
 *       and would refuse the comma, so no earlycon= would ever parse.
 *       The comment mainline leaves above this line says the same thing in
 *       one line: "Before you replace it with kstrtoull(), think about
 *       options separator (',') it will not tolerate."
 *
 *   serial_core.c:2164  *baud = simple_strtoul(s, NULL, 10);
 *       uart_parse_options(), whose format is <baud><parity><bits><flow> --
 *       "115200n8r".  There is no NUL after the digits by design, so the
 *       strict parser would read nothing at all and every console= would
 *       come up with a baud rate of 0.
 *
 * The radix fixup is mainline's _parse_integer_fixup_radix() with the same
 * answers: base 0 means autodetect, where "0x" followed by a hex digit means
 * 16, a leading "0" means 8, otherwise 10 -- and the "0x" prefix is skipped
 * only when base 16 was asked for or arrived at.  Two consequences worth
 * writing down, because they look like bugs and are not: a bare "0" is
 * parsed as octal, so simple_strtoul("09", 0) stops after the '0' and
 * returns 0; and with base 0, "0x" *without* a hex digit after it selects
 * radix 8, which consumes nothing more.  That is what a 32-bit kernel does
 * too, and changing it here would make DCL disagree with the hardware it is
 * parsing command lines for.
 *
 * LLP64 note: simple_strtoul() returns `unsigned long`, four bytes on this
 * target, so it truncates what simple_strtoull() produced.  That is not a
 * defect this port introduced -- it is mainline's behaviour on every 32-bit
 * kernel -- and the saturating path below runs first, so an over-long parse
 * yields 0xFFFFFFFF rather than a value that merely happened to fit in the
 * low word.  The single caller assigns to an int baud rate.
 *
 * mainline's _parse_integer_limit() also ORs a KSTRTOX_OVERFLOW bit into
 * its return, which simple_strntoull() immediately masks back off -- so the
 * bit is not carried through to these two either, and there is nothing here
 * to carry it in.  Saturation to ULLONG_MAX is the whole overflow answer.
 *
 *   upstream  include/linux/kstrtox.h (the declarations), lib/vsprintf.c:112
 *             (simple_strtoull's wrapper), lib/kstrtox.c
 *             (_parse_integer_fixup_radix, _parse_integer_limit)
 */
/*
 * Kernel ctype, not <ctype.h>.  DCL's own <ctype.h> declares isspace() and
 * friends as exported *functions*; linux/ctype.h spells the same names as
 * macros over _ctype[].  Whichever lands second turns the other's
 * declarations into garbage -- a function declaration whose name is already
 * a macro parses as an expression, which is what produced the cascade of
 * "expected ')'" at the top of BaseHdr/ctype.h.  Mainline keeps these apart
 * because kernel code never sees the userspace header; here both are on the
 * include path, so the choice of which one a kernel header asks for has to
 * be deliberate.  It is the kernel one.
 */
#include <linux/ctype.h>	/* isxdigit, for base-0 autodetection */

static inline unsigned long long simple_strtoull(const char* cp, char** endp,
						 unsigned int base)
{
	const char* s = cp;
	unsigned long long result = 0;

	if (base == 0) {
		if (s[0] == '0') {
			if ((s[1] == 'x' || s[1] == 'X') && isxdigit(s[2]))
				base = 16;
			else
				base = 8;
		} else {
			base = 10;
		}
	}
	if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
		s += 2;

	for (;; s++) {
		unsigned int c = (unsigned char)*s;
		unsigned int val;

		if ('0' <= c && c <= '9')
			val = c - '0';
		else if ('a' <= c && c <= 'f')
			val = c - 'a' + 10;
		else if ('A' <= c && c <= 'F')
			val = c - 'A' + 10;
		else
			break;

		if (val >= base)
			break;

		/*
		 * mainline guards the multiply with `res & (~0ull << 60)`:
		 * below 2^60, times a base of at most 16, cannot reach 2^64,
		 * so the fast path needs no test at all.  Above it, saturate
		 * the way _parse_integer_limit() does.  Note the two-part
		 * condition -- the second multiply is only evaluated once the
		 * first has proved it safe.
		 */
		if (result & (~0ULL << 60)) {
			if (result > ~0ULL / base ||
			    result * base > ~0ULL - val)
				result = ~0ULL;
			else
				result = result * base + val;
		} else {
			result = result * base + val;
		}
	}

	if (endp)
		*endp = (char*)s;

	return result;
}

static inline unsigned long simple_strtoul(const char* cp, char** endp,
					   unsigned int base)
{
	return (unsigned long)simple_strtoull(cp, endp, base);
}

/*
 * simple_strtol() -- the signed form, mainline's three-line wrapper
 * (lib/vsprintf.c):
 *
 *     if (*cp == '-')
 *             return -simple_strtoul(cp + 1, endp, base);
 *     return simple_strtoul(cp, endp, base);
 *
 * drm_modes.c is the whole consumer set, all five calls inside the
 * command-line parser: :1947 and :1966 read a bpp and a refresh rate,
 * :2038/:2047 a resolution, :2105 a bare integer option. The sign branch is
 * the only thing this adds, and it is the point: handed "-32" directly,
 * simple_strtoul() stops at the '-' and returns 0 with endp on the sign, so
 * a command line asking for a negative value would parse as zero. Zero is a
 * value the mode validator later rejects or silently treats as "unset",
 * which is a wrong answer rather than the parse error the writer deserved --
 * whereas with the branch the caller receives -32 and can reject it itself.
 *
 * '+' is not consumed, and that is mainline's behaviour rather than an
 * omission: no caller distinguishes a leading plus, and simple_strtoul()
 * would stop on it identically to any other non-digit. The negation wraps
 * through unsigned long like mainline's does, so -2147483648 comes out as
 * LONG_MIN rather than saturating.
 */
static inline long simple_strtol(const char* cp, char** endp,
				 unsigned int base)
{
	if (*cp == '-')
		return -simple_strtoul(cp + 1, endp, base);
	return simple_strtoul(cp, endp, base);
}
#endif /* __LINUX_KSTRTOX_H__ */
