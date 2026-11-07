/*
 * DCL/serial_test.c -- stage 3's runtime gate for the 8250/serial core.
 *
 * Stage 3 vendors drivers/tty/serial/{serial_core.c,8250_port.c} and
 * drivers/tty/{tty_port.c,tty_ldisc.c,tty_ioctl.c,tty_ldsem.c,tty_mutex.c}
 * from source.  "It compiled and linked" proves almost nothing for a baud
 * rate generator, so this file drives the five functions the milestone is
 * really about against a register file that lives in ordinary memory:
 *
 *   uart_get_divisor()         clk / (16 * baud), rounded to nearest
 *   uart_get_baud_rate()       termios -> number, with the B0 and the
 *                              out-of-range fallbacks mainline specifies
 *   uart_update_timeout()      frame time in nanoseconds from c_cflag
 *   serial8250_do_set_termios() all of the above wired together, and the
 *                              answer landing in the emulated LCR + divisor
 *                              latch
 *   serial8250_tx_chars()      draining the tty transmit ring out of the
 *                              port one byte at a time
 *
 * The port is RAM-backed on purpose: iotype is UPIO_MEM but membase points
 * at a static array, and serial_in/serial_out are two small functions that
 * index it.  Nothing here touches a real UART, so the test cannot disturb
 * the console the boot log is printing on, and it runs identically on QEMU
 * and on the iMX8MP / RPi3b+ boards.
 *
 * The register file deliberately does *not* emulate the divisor-latch
 * alias.  On real hardware offsets 0 and 1 mean UART_TX / UART_IER or
 * UART_DLL / UART_DLM depending on LCR's DLAB bit, because the chip
 * multiplexes them.  A flat array cannot, so the mux is modelled exactly
 * once -- in t_dl_write(), the hook serial8250_do_set_divisor() uses to
 * program the divisor -- and every other access indexes its offset
 * directly.  That keeps serial_out(UART_TX, ...) meaning "transmit" for the
 * tx_chars half of the test, which is what the driver expects of the port
 * ops it is handed.
 *
 * t_dl_write() is also where DLAB gets cleared again.  Mainline's
 * serial8250_do_set_divisor() writes LCR = up->lcr | UART_LCR_DLAB and then
 * serial_dl_write(); nothing in serial8250_do_set_termios() restores LCR
 * afterwards, because on the real chip the next register access is not
 * offset 0.  Here it would be, so the emulation puts LCR back -- that is a
 * property of this test harness, not of the vendored driver, and the LCR
 * assertions below are written against up->lcr (the driver's own view) and
 * the divisor latch (the value it actually programmed), not against the
 * byte left in mem[UART_LCR].
 */

#include <linux/types.h>
#include <linux/string.h>
#include <linux/tty.h>
#include <linux/termios.h>
#include <linux/serial_core.h>
#include <linux/serial_8250.h>
#include <linux/serial_reg.h>
#include <linux/tty_port.h>
#include <linux/kfifo.h>
#include <linux/spinlock.h>
#include <Drivers/uart.h>	/* UARTDebugOut */

/* 1.8432 MHz is the canonical PC UART clock: 115200 baud is exactly divisor
 * 1 and 9600 is exactly divisor 12, so both answers are integers and a
 * rounding regression cannot hide inside them. */
#define T_UARTCLK	1843200u
#define T_DIV_9600	12u	/* 1843200 / (16 * 9600) */
#define T_DIV_115200	1u	/* 1843200 / (16 * 115200) */

/* tty_get_frame_size() of CS8 with no parity and one stop bit is
 * 2 + 8 = 10 bits, so the frame time is ceil(10e9 / baud) nsec. */
#define T_FRAME_9600	1041667u

/* offsets 8..9 stand in for the divisor latch (see the header comment) */
#define T_REG_DLL	8
#define T_REG_DLM	9

static unsigned char t_mem[32];
static unsigned int t_div_latch;	/* last value handed to dl_write */

static struct uart_8250_port t_up;
static struct uart_state t_state;
static unsigned char t_xmit[64];

/* ── the emulated register file ───────────────────────────────────────── */

static u32 t_serial_in(struct uart_port* port, unsigned int offset)
{
	(void)port;
	return (offset < sizeof(t_mem)) ? t_mem[offset] : 0;
}

static void t_serial_out(struct uart_port* port, unsigned int offset, u32 val)
{
	(void)port;
	if (offset < sizeof(t_mem))
		t_mem[offset] = (unsigned char)val;
}

static void t_dl_write(struct uart_8250_port* up, u32 value)
{
	t_div_latch = value & 0xffffu;
	t_mem[T_REG_DLL] = (unsigned char)(value & 0xffu);
	t_mem[T_REG_DLM] = (unsigned char)((value >> 8) & 0xffu);

	/* LCR still carries DLAB here; see the header comment. */
	t_mem[UART_LCR] = (unsigned char)up->lcr;
}

/* The port under test has no tty -- stage 3 has no file layer yet -- so the
 * test installs its own client ops instead of tty_port_init()'s default.
 * That is the same choice stage 2's test makes, for the same reason (see the
 * header of DCL/tty_shim.c): the default write_wakeup forwards to
 * tty_port_default_wakeup(), which hands a *NULL* tty to tty_wakeup(), and
 * tty_wakeup() -- mainline's, and therefore this shim's -- dereferences it.
 * On real hardware that cannot happen, because uart_write_wakeup() only runs
 * from a start_tx path that a tty opened.  Hooking write_wakeup here also
 * turns "the wakeup fired" into something the test can count. */
static int t_wakeups;

static void t_write_wakeup(struct tty_port* port)
{
	(void)port;
	t_wakeups++;
}

static const struct tty_port_client_operations t_client_ops = {
	.receive_buf = NULL,
	.lookahead_buf = NULL,
	.write_wakeup = t_write_wakeup,
};

/* ── DclSerialTestRun ─────────────────────────────────────────────────── */

void DclSerialTestRun(void)
{
	struct uart_port* port = &t_up.port;
	struct ktermios t, zeroed;
	int ok = 0, fail = 0;

#define CHECK(cond) do { if (cond) ok++; else fail++; } while (0)

	memset(&t_up, 0, sizeof(t_up));
	memset(&t_state, 0, sizeof(t_state));
	memset(t_mem, 0, sizeof(t_mem));
	t_div_latch = 0;
	t_wakeups = 0;

	port->membase = t_mem;
	port->iotype = UPIO_MEM;
	port->uartclk = T_UARTCLK;
	port->fifosize = 16;
	port->regshift = 0;
	port->serial_in = t_serial_in;
	port->serial_out = t_serial_out;
	port->state = &t_state;
	spin_lock_init(&port->lock);

	t_up.tx_loadsz = 16;
	t_up.dl_write = t_dl_write;

	tty_port_init(&t_state.port);
	t_state.port.client_ops = &t_client_ops;
	kfifo_init(&t_state.port.xmit_fifo, t_xmit, sizeof(t_xmit));

	/* 1. uart_get_divisor(): clk / (16 * baud), rounded to nearest. */
	CHECK(uart_get_divisor(port, 9600) == T_DIV_9600);
	CHECK(uart_get_divisor(port, 115200) == T_DIV_115200);

	/* 2. uart_get_baud_rate(): an in-range rate comes back verbatim. */
	memset(&t, 0, sizeof(t));
	t.c_cflag = B9600 | CS8 | CREAD | CLOCAL;
	CHECK(uart_get_baud_rate(port, &t, NULL, 300, 4000000) == 9600);

	/* B0 means "hang up", which decodes to a rate of 0; the driver
	 * answers max(min, 9600), and the min argument wins when it is
	 * the larger of the two. */
	memset(&zeroed, 0, sizeof(zeroed));
	zeroed.c_cflag = CS8 | CREAD | CLOCAL;
	CHECK(uart_get_baud_rate(port, &zeroed, NULL, 300, 4000000) == 9600);
	CHECK(uart_get_baud_rate(port, &zeroed, NULL, 20000, 4000000) == 20000);

	/* 3. uart_update_timeout(): a 10-bit frame at 9600 baud. */
	memset(t_mem, 0, sizeof(t_mem));
	uart_update_timeout(port, CS8 | CREAD | CLOCAL, 9600);
	CHECK(port->frame_time == T_FRAME_9600);

	/* 4. serial8250_do_set_termios(): the whole path at once.
	 * The cflag is written back through tty_termios_encode_baud_rate(),
	 * so 9600 in must still be 9600 out. */
	memset(&t, 0, sizeof(t));
	t.c_cflag = B9600 | CS8 | CREAD | CLOCAL;
	serial8250_do_set_termios(port, &t, NULL);

	CHECK(tty_termios_baud_rate(&t) == 9600);
	CHECK(port->frame_time == T_FRAME_9600);
	/* word length 8, no parity, one stop bit -- from CS8 alone */
	CHECK((t_up.lcr & UART_LCR_WLEN8) == UART_LCR_WLEN8);
	CHECK((t_up.lcr & UART_LCR_PARITY) == 0);
	CHECK((t_up.lcr & UART_LCR_STOP) == 0);
	/* the divisor the driver computed reached the hardware */
	CHECK(t_div_latch == T_DIV_9600);
	CHECK(t_mem[T_REG_DLL] == T_DIV_9600);
	CHECK(t_mem[T_REG_DLM] == 0);

	/* 5. serial8250_tx_chars(): five bytes queued on the tty side come
	 *    out of the shift register one at a time, and the ring ends up
	 *    empty so the tty learns the port drained. */
	CHECK(kfifo_in(&t_state.port.xmit_fifo, "HELLO", 5) == 5);
	CHECK(!kfifo_is_empty(&t_state.port.xmit_fifo));

	serial8250_tx_chars(&t_up);

	CHECK(kfifo_is_empty(&t_state.port.xmit_fifo));
	CHECK(t_mem[UART_TX] == 'O');		/* last byte to reach TX */
	CHECK(port->icount.tx == 5);		/* five bytes accounted for */
	CHECK(port->frame_time == T_FRAME_9600);
	/* uart_write_wakeup() reached the tty port's client ops */
	CHECK(t_wakeups == 1);

	tty_port_destroy(&t_state.port);

	UARTDebugOut("dcl serial: %d ok, %d failed\r\n", ok, fail);

#undef CHECK
}
