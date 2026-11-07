#ifndef __LINUX_TERMIOS_H__
#define __LINUX_TERMIOS_H__

/*
 * DCL <linux/termios.h> -- the termios structures and the flag vocabulary
 * every tty file speaks.
 *
 * The structures are mainline's (uapi/asm-generic/termbits.h, which is what
 * arm64 includes): struct termios/termios2/ktermios and struct winsize, with
 * the full c_cc index list and all four flag sets copied verbatim. There is no
 * room for interpretation in a flag table -- `CS8` is 0x30 because the UART
 * divides c_cflag by 16 to program the line control register, and 8250_port.c
 * does exactly that at serial8250_do_set_termios(). Inventing a "close enough"
 * value here would silently produce a port that transmits but never receives.
 *
 * The kernel-side helpers (tty_termios_baud_rate and friends) are the reason
 * this header is load-bearing rather than just a table: serial_core.c:504
 * reads the line rate through tty_termios_baud_rate() to decide whether the
 * port can be started, and 8250_port.c:2817 re-encodes a rate back into
 * c_cflag so the divisor can be recomputed. Both directions have to agree, so
 * they are written as one table read one way and one table scanned the other:
 *
 *   tty_termios_baud_rate()      c_cflag & CBAUD  ->  bits per second
 *   tty_termios_encode_baud_rate() bits per second -> c_cflag & CBAUD
 *
 * BOTHER (0x1000) means "not a standard rate": the real number lives in
 * c_ospeed/c_ispeed, which is how a caller requests 4 MHz from a UART that has
 * no named constant for it.
 *
 * Mainline keeps these as non-inline functions in drivers/tty/tty_termios.c.
 * They are static inline here because stage 2 has no tty_termios.c to link
 * against and a declaration nothing defines is a link error waiting for its
 * first caller. The bodies are self-contained (a switch and a table), so the
 * cost of inlining is nothing next to the port I/O it guards.
 */


/* --- uapi/asm-generic/termios.h: modem-control line states (TIOCM_*) ------
 * mainline keeps these next to termios rather than in serial_core.h, which is
 * why vendored 8250.h reaches TIOCM_CAR through <linux/serial.h> and gets
 * them via termios.h. The values are the modem lines, not flags: DCL's flat
 * BaseHdr has no uapi/ tree to import them from, so they are copied verbatim
 * from asm-generic/termios.h -- every one of them is an ABI constant that
 * userspace's TIOCMGET/TIOCMSET sees.
 */
#define TIOCM_LE	0x001
#define TIOCM_DTR	0x002
#define TIOCM_RTS	0x004
#define TIOCM_ST	0x008
#define TIOCM_SR	0x010
#define TIOCM_CTS	0x020
#define TIOCM_CAR	0x040
#define TIOCM_RNG	0x080
#define TIOCM_DSR	0x100
#define TIOCM_CD	TIOCM_CAR
#define TIOCM_RI	TIOCM_RNG
#define TIOCM_OUT1	0x2000
#define TIOCM_OUT2	0x4000
#define TIOCM_LOOP	0x8000

#include <linux/kernel.h>	/* tcflag_t users get u8/u32 here, and bool */
/*
 * The TC and TIOC request numbers (TCGETS, TCSETS, TCXONC, TIOCGSOFTCAR,
 * TIOCGLCKTRMIOS ...).  14 of them are spelled by tty_ioctl.c and all 14 came
 * back undeclared until this include existed; TIOCM_* already in this file
 * are the *modem line* bits from uapi/asm-generic/termios.h, a different
 * file from the request numbers in uapi/asm-generic/ioctls.h, which is why
 * one existed without the other.
 *
 * The #ifdef _IO guard on the _IOR()-computed entries inside it (TCGETS2 and
 * friends) is why this works: those are function-like macro *definitions*,
 * never expanded unless a file uses them, so a DCL with no struct termios2
 * still reads the header cleanly.
 */
#include <asm-generic/ioctls.h>

/* --- uapi/asm-generic/termbits-common.h -------------------------------- */
typedef unsigned char	cc_t;
typedef unsigned int	speed_t;

/* c_iflag bits */
#define IGNBRK	0x001		/* Ignore break condition */
#define BRKINT	0x002		/* Signal interrupt on break */
#define IGNPAR	0x004		/* Ignore characters with parity errors */
#define PARMRK	0x008		/* Mark parity and framing errors */
#define INPCK	0x010		/* Enable input parity check */
#define ISTRIP	0x020		/* Strip 8th bit off characters */
#define INLCR	0x040		/* Map NL to CR on input */
#define IGNCR	0x080		/* Ignore CR */
#define ICRNL	0x100		/* Map CR to NL on input */
#define IXANY	0x800		/* Any character will restart after stop */

/* c_iflag bits (from termbits.h) */
#define IUCLC	0x0200
#define IXON	0x0400
#define IXOFF	0x1000
#define IMAXBEL	0x2000
#define IUTF8	0x4000

/* c_oflag bits */
#define OPOST	0x01		/* Perform output processing */
#define OLCUC	0x00002
#define ONLCR	0x00004
#define OCRNL	0x08
#define ONOCR	0x10
#define ONLRET	0x20
#define OFILL	0x40
#define OFDEL	0x80

#define NLDLY	0x00100
#define   NL0	0x00000
#define   NL1	0x00100
#define CRDLY	0x00600
#define   CR0	0x00000
#define   CR1	0x00200
#define   CR2	0x00400
#define   CR3	0x00600
#define TABDLY	0x01800
#define   TAB0	0x00000
#define   TAB1	0x00800
#define   TAB2	0x01000
#define   TAB3	0x01800
#define XTABS	0x01800
#define BSDLY	0x02000
#define   BS0	0x00000
#define   BS1	0x02000
#define VTDLY	0x04000
#define   VT0	0x00000
#define   VT1	0x04000
#define FFDLY	0x08000
#define   FF0	0x00000
#define   FF1	0x08000

/* c_cflag bit meaning */
#define CBAUD		0x0000100f
#define CSIZE		0x00000030
#define   CS5		0x00000000
#define   CS6		0x00000010
#define   CS7		0x00000020
#define   CS8		0x00000030
#define CSTOPB		0x00000040
#define CREAD		0x00000080
#define PARENB		0x00000100
#define PARODD		0x00000200
#define HUPCL		0x00000400
#define CLOCAL		0x00000800
#define CBAUDEX		0x00001000
#define BOTHER		0x00001000
#define     B57600	0x00001001
#define    B115200	0x00001002
#define    B230400	0x00001003
#define    B460800	0x00001004
#define    B500000	0x00001005
#define    B576000	0x00001006
#define    B921600	0x00001007
#define   B1000000	0x00001008
#define   B1152000	0x00001009
#define   B1500000	0x0000100a
#define   B2000000	0x0000100b
#define   B2500000	0x0000100c
#define   B3000000	0x0000100d
#define   B3500000	0x0000100e
#define   B4000000	0x0000100f
#define CIBAUD		0x100f0000	/* input baud rate */
#define ADDRB		0x20000000
#define CMSPAR		0x40000000	/* mark or space (stick) parity */
#define CRTSCTS		0x80000000	/* flow control */
#define IBSHIFT		16		/* Shift from CBAUD to CIBAUD */

/* The classic rates. Values are the CBAUD encodings, not the bit rates --
 * that is what `c_cflag & CBAUD` compares against. */
#define     B0	0x00000000
#define    B50	0x00000001
#define    B75	0x00000002
#define   B110	0x00000003
#define   B134	0x00000004
#define   B150	0x00000005
#define   B200	0x00000006
#define   B300	0x00000007
#define   B600	0x00000008
#define  B1200	0x00000009
#define  B1800	0x0000000a
#define  B2400	0x0000000b
#define  B4800	0x0000000c
#define  B9600	0x0000000d
#define B19200	0x0000000e
#define B38400	0x0000000f
#define EXTA	B19200
#define EXTB	B38400

/* c_lflag bits */
#define ISIG	0x00001
#define ICANON	0x00002
#define XCASE	0x00004
#define ECHO	0x00008
#define ECHOE	0x00010
#define ECHOK	0x00020
#define ECHONL	0x00040
#define NOFLSH	0x00080
#define TOSTOP	0x00100
#define ECHOCTL	0x00200
#define ECHOPRT	0x00400
#define ECHOKE	0x00800
#define FLUSHO	0x01000
#define PENDIN	0x04000
#define IEXTEN	0x08000
#define EXTPROC	0x10000

/* tcflow() ACTION argument and TCXONC use these */
#define TCOOFF	0
#define TCOON	1
#define TCIOFF	2
#define TCION	3

/* tcflush() QUEUE_SELECTOR argument and TCFLSH use these */
#define TCIFLUSH	0
#define TCOFLUSH	1
#define TCIOFLUSH	2

/* tcsetattr uses these */
#define	TCSANOW		0
#define	TCSADRAIN	1
#define	TCSAFLUSH	2

/* --- uapi/asm-generic/termbits.h --------------------------------------- */
typedef unsigned int	tcflag_t;

#define NCCS 19

struct termios {
	tcflag_t c_iflag;		/* input mode flags */
	tcflag_t c_oflag;		/* output mode flags */
	tcflag_t c_cflag;		/* control mode flags */
	tcflag_t c_lflag;		/* local mode flags */
	cc_t c_line;			/* line discipline */
	cc_t c_cc[NCCS];		/* control characters */
};

struct termios2 {
	tcflag_t c_iflag;
	tcflag_t c_oflag;
	tcflag_t c_cflag;
	tcflag_t c_lflag;
	cc_t c_line;
	cc_t c_cc[NCCS];
	speed_t c_ispeed;
	speed_t c_ospeed;
};

struct ktermios {
	tcflag_t c_iflag;
	tcflag_t c_oflag;
	tcflag_t c_cflag;
	tcflag_t c_lflag;
	cc_t c_line;
	cc_t c_cc[NCCS];
	speed_t c_ispeed;
	speed_t c_ospeed;
};

/* c_cc character indices */
#define VINTR		 0
#define VQUIT		 1
#define VERASE		 2
#define VKILL		 3
#define VEOF		 4
#define VTIME		 5
#define VMIN		 6
#define VSWTC		 7
#define VSTART		 8
#define VSTOP		 9
#define VSUSP		10
#define VEOL		11
#define VREPRINT	12
#define VDISCARD	13
#define VWERASE		14
#define VLNEXT		15
#define VEOL2		16

/* uapi/asm-generic/termios.h -- the winsize block used by TIOCGWINSZ */
struct winsize {
	unsigned short ws_row;
	unsigned short ws_col;
	unsigned short ws_xpixel;
	unsigned short ws_ypixel;
};

#define NCC 8
struct termio {
	unsigned short c_iflag;
	unsigned short c_oflag;
	unsigned short c_cflag;
	unsigned short c_lflag;
	unsigned char c_line;
	unsigned char c_cc[NCC];
};

/*
 * --- kernel-side rate helpers ------------------------------------------
 *
 * One table, read in both directions. The order matters: the index *is* the
 * CBAUD encoding for 1..15 and 0x1001..0x100f, so baud_table[1] is 50 and
 * baud_table[0x1001 - 1] would be wrong -- hence the two explicit ranges
 * below rather than one array with holes in it.
 */
static inline speed_t __dcl_cbaud_to_rate(unsigned int cbaud)
{
	static const speed_t std_rate[16] = {
		0, 50, 75, 110, 134, 150, 200, 300,
		600, 1200, 1800, 2400, 4800, 9600, 19200, 38400
	};
	static const speed_t high_rate[16] = {
		/* index 0 == BOTHER: the rate is not encoded in c_cflag */
		0, 57600, 115200, 230400, 460800, 500000, 576000, 921600,
		1000000, 1152000, 1500000, 2000000, 2500000, 3000000,
		3500000, 4000000
	};

	if (cbaud & CBAUDEX)
		return high_rate[cbaud & 0x0f];
	return std_rate[cbaud & 0x0f];
}

static inline speed_t tty_termios_baud_rate(const struct ktermios* termios)
{
	unsigned int cbaud = termios->c_cflag & CBAUD;

	if (cbaud == BOTHER)
		return termios->c_ospeed;
	return __dcl_cbaud_to_rate(cbaud);
}

/*
 * Input rate: CIBAUD is the output encoding shifted left by IBSHIFT, so the
 * same table answers for it. A c_cflag that never mentions CIBAUD (every
 * port before someone has set an input rate) falls back to the output rate --
 * the common case, since tcsetattr() writes both together.
 */
static inline speed_t tty_termios_input_baud_rate(const struct ktermios* termios)
{
	unsigned int ibaud = (termios->c_cflag & CIBAUD) >> IBSHIFT;

	if (ibaud == 0)
		return tty_termios_baud_rate(termios);
	if (ibaud == BOTHER)
		return termios->c_ispeed;
	return __dcl_cbaud_to_rate(ibaud);
}

static inline unsigned int __dcl_rate_to_cbaud(speed_t rate)
{
	switch (rate) {
	case 0:	return B0;
	case 50:	return B50;
	case 75:	return B75;
	case 110:	return B110;
	case 134:	return B134;
	case 150:	return B150;
	case 200:	return B200;
	case 300:	return B300;
	case 600:	return B600;
	case 1200:	return B1200;
	case 1800:	return B1800;
	case 2400:	return B2400;
	case 4800:	return B4800;
	case 9600:	return B9600;
	case 19200:	return B19200;
	case 38400:	return B38400;
	case 57600:	return B57600;
	case 115200:	return B115200;
	case 230400:	return B230400;
	case 460800:	return B460800;
	case 500000:	return B500000;
	case 576000:	return B576000;
	case 921600:	return B921600;
	case 1000000:	return B1000000;
	case 1152000:	return B1152000;
	case 1500000:	return B1500000;
	case 2000000:	return B2000000;
	case 2500000:	return B2500000;
	case 3000000:	return B3000000;
	case 3500000:	return B3500000;
	case 4000000:	return B4000000;
	default:	return BOTHER;
	}
}

/*
 * Encode a pair of rates into c_cflag. Note the shift: CIBAUD is CBAUD in the
 * high half, so the input encoding is written one IBSHIFT to the left. Getting
 * this backwards yields a port whose *output* runs at the *input* rate, which
 * is invisible until someone opens a modem at 9600 and sees garbage.
 */
static inline void tty_termios_encode_baud_rate(struct ktermios* termios,
						speed_t ibaud, speed_t obaud)
{
	termios->c_cflag &= ~(CBAUD | CIBAUD);
	termios->c_cflag |= __dcl_rate_to_cbaud(obaud) |
			    ((unsigned int)__dcl_rate_to_cbaud(ibaud) << IBSHIFT);
	termios->c_ispeed = ibaud;
	termios->c_ospeed = obaud;
}

/*
 * tty_termios_copy_hw -- declaration only, because drivers/tty/tty_ioctl.c:231
 * has mainline's definition and is now compiled from source.
 *
 * It used to be a static inline here, and that was wrong twice over.  A
 * non-static declaration next to a static definition is a compile error, so
 * vendoring tty_ioctl.c made the two collide outright.  And the body it
 * displaced was not mainline's: this file had
 *
 *     new_t->c_cflag = old_t->c_cflag;
 *
 * where mainline keeps only the hardware half (HUPCL | CREAD | CLOCAL are the
 * ones the caller may not change) and drops the processing flags.  The
 * comment that used to sit above it said "Mainline defines this in
 * tty_termios.c" -- it does not; tty_ioctl.c:231 does, alongside nothing of
 * the sort, and the rate helpers above really are separate.  The declaration
 * matches mainline's spelling exactly, which is what lets the call at
 * tty_ioctl.c:343 take it.
 *
 * The other two of <linux/tty.h>'s note-2 trio, tty_termios_baud_rate() and
 * tty_termios_encode_baud_rate(), stay static inline here: their definition
 * is drivers/tty/tty_baudrate.c, which is not staged, and an inline beside an
 * uncalled extern would only create a link error.
 */
void tty_termios_copy_hw(struct ktermios* new, const struct ktermios* old);

/*
 * tty_std_termios -- what a driver gets before anyone has configured it.
 * 9600 8N1 with ICANON/ECHO on: the settings a text console wants on first
 * open.
 *
 * Declared here, defined by drivers/tty/tty_io.c:124 now that tty_io.c is
 * compiled from source -- which is where mainline puts it and why the
 * comment that used to name DCL/tty_shim.c was wrong: that file never had a
 * definition, it only ever had this declaration, and the symbol stayed
 * undefined until tty_io.c landed.
 */
extern struct ktermios tty_std_termios;

#endif /* __LINUX_TERMIOS_H__ */
