#ifndef _LINUX_TTY_PORT_H
#define _LINUX_TTY_PORT_H

/*
 * DCL <linux/tty_port.h> -- port-level state shared by every tty driver.
 *
 * Ported from mainline v7.2. A tty_port outlives the tty that opens it: the
 * port is the device, the tty is one open of it, which is why the two have
 * separate reference counts (port->kref for the port, tty->kref for the tty)
 * and why port->tty can go NULL on a hangup while the port stays.
 *
 * The parts that matter most to the 8250 port:
 *
 *   buf        -- the flip buffer head. uart_handle_rx_char() writes into it
 *                 from interrupt context; tty_flip_buffer_push() commits.
 *   xmit_buf / xmit_fifo
 *              -- the transmit ring. serial_core.c allocates xmit_buf on
 *                 demand (serial_core.c:262) and binds it to xmit_fifo with
 *                 kfifo_init(); uart_write() pushes, the driver's start_tx
 *                 drains, and WAKEUP_CHARS is the threshold for telling the
 *                 tty there is room again.
 *   iflags     -- TTY_PORT_* bits, read with test_bit() throughout.
 *   client_ops -- how buffered input reaches the ldisc. This is the indirection
 *                 tty_buffer.c calls through (port->client_ops->receive_buf),
 *                 added upstream so that a tty port can be fed without a tty
 *                 (serdev, and the console). tty_port.h:49 names the default.
 *
 * The tty_port_tty guard at the bottom of this file: mainline writes it with
 * __DEFINE_UNLOCK_GUARD and reads the guarded pointer back through
 * scoped_tty().  It was deferred on the grounds that inventing acquire/release
 * semantics without a call site would mean guessing rather than reading them
 * off mainline -- and then 8250_port.c arrived in stage 3 with the call site
 * at serial8250_update_uartclk(), line 2608.  So it is ported from that one
 * use, which is how it should have been written the first time.
 */

#include <linux/kfifo.h>
#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/tty_buffer.h>
#include <linux/tty_driver.h>
#include <linux/wait.h>

struct attribute_group;
struct tty_port;
struct tty_struct;

struct tty_port_operations {
	bool (*carrier_raised)(struct tty_port* port);
	void (*dtr_rts)(struct tty_port* port, bool active);
	void (*shutdown)(struct tty_port* port);
	int (*activate)(struct tty_port* port, struct tty_struct* tty);
	void (*destruct)(struct tty_port* port);
};

struct tty_port_client_operations {
	size_t (*receive_buf)(struct tty_port* port, const u8* cp,
			      const u8* fp, size_t count);
	void (*lookahead_buf)(struct tty_port* port, const u8* cp,
			      const u8* fp, size_t count);
	void (*write_wakeup)(struct tty_port* port);
};

extern const struct tty_port_client_operations tty_port_default_client_ops;

/*
 * `buf` is a struct tty_bufhead, which itself contains a variable-sized
 * member (see the note in <linux/tty_buffer.h>), so embedding it anywhere but
 * the end of tty_port raises the same -Wgnu-variable-sized-type-not-at-end.
 * It is first here in mainline's layout and the reason is the same as there:
 * the flip buffer is the part of a port that is allocated, everything after
 * it is bookkeeping, and uart_add_one_port() walks the port as one block.
 * Scoped suppression rather than a reordering, for the reason given above.
 */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-variable-sized-type-not-at-end"
#endif
struct tty_port {
	struct tty_bufhead	buf;
	struct tty_struct* tty;
	struct tty_struct* itty;
	const struct tty_port_operations* ops;
	const struct tty_port_client_operations* client_ops;
	spinlock_t		lock;
	int			blocked_open;
	int			count;
	wait_queue_head_t	open_wait;
	wait_queue_head_t	delta_msr_wait;
	unsigned long		flags;
	unsigned long		iflags;
	unsigned char		console:1;
	struct mutex		mutex;
	struct mutex		buf_mutex;
	u8*			xmit_buf;
	DECLARE_KFIFO_PTR(xmit_fifo, u8);
	unsigned int		close_delay;
	unsigned int		closing_wait;
	int			drain_delay;
	struct kref		kref;
	void*			client_data;
};
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

/* tty_port::iflags bits -- use atomic bit ops */
#define TTY_PORT_INITIALIZED	0	/* device is initialized */
#define TTY_PORT_SUSPENDED	1	/* device is suspended */
#define TTY_PORT_ACTIVE		2	/* device is open */

/*
 * uart drivers: use the uart_port::status field and the UPSTAT_* defines
 * for s/w-based flow control steering and carrier detection status
 */
#define TTY_PORT_CTS_FLOW	3	/* h/w flow control enabled */
#define TTY_PORT_CHECK_CD	4	/* carrier detect enabled */
#define TTY_PORT_KOPENED	5	/* device exclusively opened by kernel */

void tty_port_init(struct tty_port* port);
void tty_port_link_wq(struct tty_port* port, struct workqueue_struct* flip_wq);
void tty_port_link_device(struct tty_port* port, struct tty_driver* driver,
		unsigned index);
struct device* tty_port_register_device(struct tty_port* port,
		struct tty_driver* driver, unsigned index,
		struct device* device);
struct device* tty_port_register_device_attr(struct tty_port* port,
		struct tty_driver* driver, unsigned index,
		struct device* device, void* drvdata,
		const struct attribute_group** attr_grp);
struct device* tty_port_register_device_attr_serdev(struct tty_port* port,
		struct tty_driver* driver, unsigned index,
		struct device* host, struct device* parent, void* drvdata,
		const struct attribute_group** attr_grp);
void tty_port_unregister_device(struct tty_port* port,
		struct tty_driver* driver, unsigned index);
int tty_port_alloc_xmit_buf(struct tty_port* port);
void tty_port_free_xmit_buf(struct tty_port* port);
void tty_port_destroy(struct tty_port* port);
void tty_port_put(struct tty_port* port);

static inline struct tty_port* tty_port_get(struct tty_port* port)
{
	if (port && kref_get_unless_zero(&port->kref))
		return port;
	return NULL;
}

/*
 * Never overwrite the workqueue set by tty_port_link_wq().
 * No effect when %TTY_DRIVER_NO_WORKQUEUE is set, as driver->flip_wq is
 * %NULL.
 */
static inline void tty_port_link_driver_wq(struct tty_port* port,
		struct tty_driver* driver)
{
	if (!port->buf.flip_wq)
		tty_port_link_wq(port, driver->flip_wq);
}

/* If the cts flow control is enabled, return true. */
static inline bool tty_port_cts_enabled(const struct tty_port* port)
{
	return test_bit(TTY_PORT_CTS_FLOW, &port->iflags);
}

static inline void tty_port_set_cts_flow(struct tty_port* port, bool val)
{
	assign_bit(TTY_PORT_CTS_FLOW, &port->iflags, val);
}

static inline bool tty_port_active(const struct tty_port* port)
{
	return test_bit(TTY_PORT_ACTIVE, &port->iflags);
}

static inline void tty_port_set_active(struct tty_port* port, bool val)
{
	assign_bit(TTY_PORT_ACTIVE, &port->iflags, val);
}

static inline bool tty_port_check_carrier(const struct tty_port* port)
{
	return test_bit(TTY_PORT_CHECK_CD, &port->iflags);
}

static inline void tty_port_set_check_carrier(struct tty_port* port, bool val)
{
	assign_bit(TTY_PORT_CHECK_CD, &port->iflags, val);
}

static inline bool tty_port_suspended(const struct tty_port* port)
{
	return test_bit(TTY_PORT_SUSPENDED, &port->iflags);
}

static inline void tty_port_set_suspended(struct tty_port* port, bool val)
{
	assign_bit(TTY_PORT_SUSPENDED, &port->iflags, val);
}

static inline bool tty_port_initialized(const struct tty_port* port)
{
	return test_bit(TTY_PORT_INITIALIZED, &port->iflags);
}

static inline void tty_port_set_initialized(struct tty_port* port, bool val)
{
	assign_bit(TTY_PORT_INITIALIZED, &port->iflags, val);
}

static inline bool tty_port_kopened(const struct tty_port* port)
{
	return test_bit(TTY_PORT_KOPENED, &port->iflags);
}

static inline void tty_port_set_kopened(struct tty_port* port, bool val)
{
	assign_bit(TTY_PORT_KOPENED, &port->iflags, val);
}

struct tty_struct* tty_port_tty_get(struct tty_port* port);
void tty_port_tty_set(struct tty_port* port, struct tty_struct* tty);
bool tty_port_carrier_raised(struct tty_port* port);
void tty_port_raise_dtr_rts(struct tty_port* port);
void tty_port_lower_dtr_rts(struct tty_port* port);
void tty_port_hangup(struct tty_port* port);
void __tty_port_tty_hangup(struct tty_port* port, bool check_clocal,
		bool async);
void tty_port_tty_wakeup(struct tty_port* port);
int tty_port_block_til_ready(struct tty_port* port, struct tty_struct* tty,
		struct file* filp);
int tty_port_close_start(struct tty_port* port, struct tty_struct* tty,
		struct file* filp);
void tty_port_close_end(struct tty_port* port, struct tty_struct* tty);
void tty_port_close(struct tty_port* port, struct tty_struct* tty,
		struct file* filp);
int tty_port_install(struct tty_port* port, struct tty_driver* driver,
		struct tty_struct* tty);
int tty_port_open(struct tty_port* port, struct tty_struct* tty,
		struct file* filp);

static inline int tty_port_users(struct tty_port* port)
{
	return port->count + port->blocked_open;
}

/**
 * tty_port_tty_hangup - helper to hang up a tty asynchronously
 * @check_clocal: hang only ttys with %CLOCAL unset?
 */
static inline void tty_port_tty_hangup(struct tty_port* port, bool check_clocal)
{
	__tty_port_tty_hangup(port, check_clocal, true);
}

/**
 * tty_port_tty_vhangup - helper to hang up a tty synchronously
 */
static inline void tty_port_tty_vhangup(struct tty_port* port)
{
	__tty_port_tty_hangup(port, false, false);
}

void tty_kref_put(struct tty_struct* tty);


/*
 * Guard class `tty_port_tty` and scoped_tty().
 *
 * mainline spells the definitions
 *
 *     __DEFINE_UNLOCK_GUARD(tty_port_tty, struct tty_struct, tty_kref_put(_T->lock));
 *     static inline class_tty_port_tty_t class_tty_port_tty_constructor(struct tty_port *tport)
 *     { return (class_tty_port_tty_t){ .lock = tty_port_tty_get(tport) }; }
 *     #define scoped_tty() ((struct tty_struct *)(__guard_ptr(tty_port_tty)(&scope)))
 *
 * DCL spells them on its own machinery because __DEFINE_UNLOCK_GUARD is not
 * ported (see <linux/cleanup.h> for why the DEFINE_* family is only partly
 * here), and because the acquire argument is a *tty_port* while the guarded
 * object is a *tty* -- mainline's constructor function exists precisely to
 * cross that type boundary, so it cannot be expressed as DEFINE_LOCK_GUARD_1,
 * whose constructor takes the guarded type.
 *
 * The one real adaptation is scoped_tty(): mainline reads the variable the
 * guard macro named `scope`, DCL's scoped_guard() names it `_dclg`, so the
 * macro expands to `_dclg.lock`.  Everything else -- who takes a reference
 * (tty_port_tty_get) and who drops it (tty_kref_put), and that the release is
 * skipped when the port holds no tty, so a NULL guard must not call kref_put
 * on NULL -- is read straight off the two mainline lines above.
 *
 * Semantics worth stating, since it is easy to get wrong in a driver: the
 * guard holds one *reference*, not a borrowed pointer, so the tty cannot
 * disappear while scoped_guard()'s body runs, and leaving the scope drops that
 * reference.  That is exactly what 8250_port.c:2608 wants -- it goes on to
 * take termios_rwsem on the tty it just looked up.
 */
typedef struct {
	struct tty_struct* lock;
} dclg_tty_port_tty;

static inline void dclg_tty_port_tty_release(dclg_tty_port_tty* g)
{
	struct tty_struct* _T = g->lock;
	if (_T) {
		tty_kref_put(_T);
	}
}

static inline dclg_tty_port_tty dclg_tty_port_tty_ctor(struct tty_port* tport)
{
	dclg_tty_port_tty g;
	g.lock = tty_port_tty_get(tport);
	return g;
}

/*
 * The guarded tty of the enclosing scoped_guard(tty_port_tty, ...).
 *
 * Only valid inside that block -- it names the variable scoped_guard()
 * created, so there is no hidden second lookup and no way to outlive it
 * without a compile error.
 */
#define scoped_tty()	((struct tty_struct*)(_dclg.lock))

#endif /* __LINUX_TTY_PORT_H__ */
