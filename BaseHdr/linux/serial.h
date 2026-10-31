/*
 * DCL <linux/linux/serial.h> -- assembled from mainline v7.2.
 *
 * include/linux/serial.h
 * include/uapi/linux/serial.h
 *
 * Provenance, verbatim upstream:
 *   (unresolved)
 *
 * How this file was produced (python3 /tmp/mkbasehdr.py):
 *   - upstream text is copied as-is; these are layout-critical declarations
 *     and a hand-typed `struct uart_port` that disagrees with the vendor
 *     sources by one field would compile cleanly and then read the wrong
 *     offset at runtime, which is exactly the class of bug this whole
 *     exercise exists to avoid
 *   - <uapi/linux/X> content is inlined in place of its include: DCL has no
 *     uapi tree, BaseHdr is flat, and no vendored .c names a uapi path
 *   - the driver-local headers (8250.h, serial_base.h,
 *     serial_mctrl_gpio.h) are NOT here -- they are private to their driver
 *     and live in Vendored/ beside the .c that includes them, resolved by
 *     quoted-include rules
 *
 * Everything below this comment is mainline's. DCL-specific deviations are
 * made where the errors demand them, not speculatively.
 */

/*
 * include/linux/serial.h
 *
 * Copyright (C) 1992 by Theodore Ts'o.
 * 
 * Redistribution of this file is permitted under the terms of the GNU 
 * Public License (GPL)
 */
#ifndef _LINUX_SERIAL_H
#define _LINUX_SERIAL_H
/* SPDX-License-Identifier: GPL-1.0+ WITH Linux-syscall-note */
/*
 * include/linux/serial.h
 *
 * Copyright (C) 1992 by Theodore Ts'o.
 * 
 * Redistribution of this file is permitted under the terms of the GNU 
 * Public License (GPL)
 */

#ifndef _UAPI_LINUX_SERIAL_H
#define _UAPI_LINUX_SERIAL_H

#include <linux/const.h>
#include <linux/types.h>

#include <linux/tty_flags.h>


struct serial_struct {
	int	type;
	int	line;
	unsigned int	port;
	int	irq;
	int	flags;
	int	xmit_fifo_size;
	int	custom_divisor;
	int	baud_base;
	unsigned short	close_delay;
	char	io_type;
	char	reserved_char[1];
	int	hub6;
	unsigned short	closing_wait; /* time to wait before closing */
	unsigned short	closing_wait2; /* no longer used... */
	unsigned char	*iomem_base;
	unsigned short	iomem_reg_shift;
	unsigned int	port_high;
	unsigned long	iomap_base;	/* cookie passed into ioremap */
};

/*
 * For the close wait times, 0 means wait forever for serial port to
 * flush its output.  65535 means don't wait at all.
 */
#define ASYNC_CLOSING_WAIT_INF	0
#define ASYNC_CLOSING_WAIT_NONE	65535

/*
 * These are the supported serial types.
 */
#define PORT_UNKNOWN	0
#define PORT_8250	1
#define PORT_16450	2
#define PORT_16550	3
#define PORT_16550A	4
#define PORT_CIRRUS     5
#define PORT_16650	6
#define PORT_16650V2	7
#define PORT_16750	8
#define PORT_STARTECH	9
#define PORT_16C950	10	/* Oxford Semiconductor */
#define PORT_16654	11
#define PORT_16850	12
#define PORT_RSA	13	/* RSA-DV II/S card */
#define PORT_MAX	13

#define SERIAL_IO_PORT	0
#define SERIAL_IO_HUB6	1
#define SERIAL_IO_MEM	2
#define SERIAL_IO_MEM32	  3
#define SERIAL_IO_AU	  4
#define SERIAL_IO_TSI	  5
#define SERIAL_IO_MEM32BE 6
#define SERIAL_IO_MEM16	7

#define UART_CLEAR_FIFO		0x01
#define UART_USE_FIFO		0x02
#define UART_STARTECH		0x04
#define UART_NATSEMI		0x08


/*
 * Multiport serial configuration structure --- external structure
 */
struct serial_multiport_struct {
	int		irq;
	int		port1;
	unsigned char	mask1, match1;
	int		port2;
	unsigned char	mask2, match2;
	int		port3;
	unsigned char	mask3, match3;
	int		port4;
	unsigned char	mask4, match4;
	int		port_monitor;
	int	reserved[32];
};

/*
 * Serial input interrupt line counters -- external structure
 * Four lines can interrupt: CTS, DSR, RI, DCD
 */
struct serial_icounter_struct {
	int cts, dsr, rng, dcd;
	int rx, tx;
	int frame, overrun, parity, brk;
	int buf_overrun;
	int reserved[9];
};

/**
 * struct serial_rs485 - serial interface for controlling RS485 settings.
 * @flags:			RS485 feature flags.
 * @delay_rts_before_send:	Delay before send (milliseconds).
 * @delay_rts_after_send:	Delay after send (milliseconds).
 * @addr_recv:			Receive filter for RS485 addressing mode
 *				(used only when %SER_RS485_ADDR_RECV is set).
 * @addr_dest:			Destination address for RS485 addressing mode
 *				(used only when %SER_RS485_ADDR_DEST is set).
 * @padding0:			Padding (set to zero).
 * @padding1:			Padding (set to zero).
 * @padding:			Deprecated, use @padding0 and @padding1 instead.
 *				Do not use with @addr_recv and @addr_dest (due to
 *				overlap).
 *
 * Serial interface for controlling RS485 settings on chips with suitable
 * support. Set with TIOCSRS485 and get with TIOCGRS485 if supported by your
 * platform. The set function returns the new state, with any unsupported bits
 * reverted appropriately.
 *
 * The flag bits are:
 *
 * * %SER_RS485_ENABLED		- RS485 enabled.
 * * %SER_RS485_RTS_ON_SEND	- Logical level for RTS pin when sending.
 * * %SER_RS485_RTS_AFTER_SEND	- Logical level for RTS pin after sent.
 * * %SER_RS485_RX_DURING_TX	- Full-duplex RS485 line.
 * * %SER_RS485_TERMINATE_BUS	- Enable bus termination (if supported).
 * * %SER_RS485_ADDRB		- Enable RS485 addressing mode.
 * * %SER_RS485_ADDR_RECV - Receive address filter (enables @addr_recv). Requires %SER_RS485_ADDRB.
 * * %SER_RS485_ADDR_DEST - Destination address (enables @addr_dest). Requires %SER_RS485_ADDRB.
 * * %SER_RS485_MODE_RS422	- Enable RS422. Requires %SER_RS485_ENABLED.
 */
struct serial_rs485 {
	__u32	flags;
#define SER_RS485_ENABLED		_BITUL(0)
#define SER_RS485_RTS_ON_SEND		_BITUL(1)
#define SER_RS485_RTS_AFTER_SEND	_BITUL(2)
/* Placeholder for bit 3: SER_RS485_RTS_BEFORE_SEND, which isn't used anymore */
#define SER_RS485_RX_DURING_TX		_BITUL(4)
#define SER_RS485_TERMINATE_BUS		_BITUL(5)
#define SER_RS485_ADDRB			_BITUL(6)
#define SER_RS485_ADDR_RECV		_BITUL(7)
#define SER_RS485_ADDR_DEST		_BITUL(8)
#define SER_RS485_MODE_RS422		_BITUL(9)

	__u32	delay_rts_before_send;
	__u32	delay_rts_after_send;

	/* The fields below are defined by flags */
	union {
		__u32	padding[5];		/* Memory is cheap, new structs are a pain */

		struct {
			__u8	addr_recv;
			__u8	addr_dest;
			__u8	padding0[2];
			__u32	padding1[4];
		};
	};
};

/*
 * Serial interface for controlling ISO7816 settings on chips with suitable
 * support. Set with TIOCSISO7816 and get with TIOCGISO7816 if supported by
 * your platform.
 */
struct serial_iso7816 {
	__u32	flags;			/* ISO7816 feature flags */
#define SER_ISO7816_ENABLED		(1 << 0)
#define SER_ISO7816_T_PARAM		(0x0f << 4)
#define SER_ISO7816_T(t)		(((t) & 0x0f) << 4)
	__u32	tg;
	__u32	sc_fi;
	__u32	sc_di;
	__u32	clk;
	__u32	reserved[5];
};

#endif /* _UAPI_LINUX_SERIAL_H */

#include <linux/ioctl.h>
#include <linux/serial_reg.h>

#define UART_IER_ALL_INTR	(UART_IER_MSI | \
				 UART_IER_RLSI | \
				 UART_IER_THRI | \
				 UART_IER_RDI)

/* Helper for dealing with UART_LCR_WLEN* defines */
#define UART_LCR_WLEN(x)	((x) - 5)

/* FIFO and shifting register empty */
#define UART_LSR_BOTH_EMPTY	(UART_LSR_TEMT | UART_LSR_THRE)

static inline bool uart_lsr_tx_empty(u16 lsr)
{
	return (lsr & UART_LSR_BOTH_EMPTY) == UART_LSR_BOTH_EMPTY;
}

#define UART_MSR_STATUS_BITS	(UART_MSR_DCD | \
				 UART_MSR_RI | \
				 UART_MSR_DSR | \
				 UART_MSR_CTS)

/*
 * Counters of the input lines (CTS, DSR, RI, CD) interrupts
 */

struct async_icount {
	__u32	cts, dsr, rng, dcd, tx, rx;
	__u32	frame, parity, overrun, brk;
	__u32	buf_overrun;
};

#include <linux/compiler.h>


/*
 * Serial ioctl request numbers.
 *
 * mainline keeps these in include/uapi/asm-generic/ioctls.h, mixed in with
 * every other request number on the architecture; DCL's tree has no
 * asm-generic layer and no other home for them, so they sit here next to the
 * rest of the serial vocabulary they operate on (struct serial_struct,
 * struct serial_iso7816 -- both defined above, which is what makes the two
 * _IOWR/_IOR entries below computable at all).
 *
 * Every value is an ABI constant: userspace passes these straight through
 * tty_io.c to uart_ioctl(), and 8250_port.c's ioctl() dispatch switches on
 * them. Copied verbatim from asm-generic/ioctls.h (mainline v7.2) -- the
 * numbers are the interesting part, not the spelling.
 *
 * Note the encoding split these reveal: all but two are bare integers, while
 * TIOCGISO7816/TIOCSISO7816 are built with _IOR/_IOWR from <linux/ioctl.h>.
 * See that header for why two encodings coexist in the tree right now.
 */
#define TIOCGSERIAL	0x541E
#define TIOCSSERIAL	0x541F
#define TIOCGRS485	0x542E
#define TIOCSRS485	0x542F
#define TIOCSERCONFIG	0x5453
#define TIOCSERGETLSR   0x5459 /* Get line status register */
#define TIOCSERGETMULTI 0x545A /* Get multiport config  */
#define TIOCSERSETMULTI 0x545B /* Set multiport config */
#define TIOCMIWAIT	0x545C	/* wait for a change on serial input line(s) */
#define TIOCGICOUNT	0x545D	/* read serial port inline interrupt counts */
#define TIOCSER_TEMT	0x01	/* Transmitter physically empty */
#define TIOCGISO7816	_IOR('T', 0x42, struct serial_iso7816)
#define TIOCSISO7816	_IOWR('T', 0x43, struct serial_iso7816)

#endif /* _LINUX_SERIAL_H */
