#ifndef __LINUX_PM_RUNTIME_H__
#define __LINUX_PM_RUNTIME_H__

/*
 * DCL <linux/pm_runtime.h> -- the four runtime-PM calls 8250_port.c makes,
 * plus pm_wakeup_event().
 *
 * Runtime PM asks "may this device's power/clock be cut, and when did it last
 * do something".  Xeneva manages no device power: there is no PM domain, no
 * reference-counted clock, no autosuspend timer, and DCL's UART console must
 * not have its clock cut under it.  So the get/put pair is deliberately
 * *neutral* -- get succeeds, put does nothing -- rather than pretending to
 * count references.  A get that failed would make 8250_port.c bail out of
 * serial8250_start_tx() and serial8250_rx_dma() with -EACCES, i.e. a driver
 * that refuses to transmit; a get that succeeds and a put that does nothing
 * is what an always-on device does, which is what this is.
 *
 * The call sites tell the same story: pm_runtime_get_sync() at
 * 8250_port.c:519/655 guards "device is awake" on the way into transmit,
 * pm_runtime_mark_last_busy() + pm_runtime_put_autosuspend() at :527/:668
 * record activity and request idle on the way out.  With nothing to suspend,
 * the only part that matters is that none of them returns an error.
 *
 * pm_wakeup_event() lives in linux/pm_wakeup.h upstream; DCL has no wakeup
 * source registry either, so it sits here next to the other PM no-ops rather
 * than earning a header of its own.
 *
 *   upstream  include/linux/pm_runtime.h  (mainline v7.2)
 */
#include <linux/kernel.h>

static inline int pm_runtime_get_sync(const void* dev)
{
	(void)dev;
	return 0;
}

static inline int pm_runtime_put_autosuspend(const void* dev)
{
	(void)dev;
	return 0;
}

static inline int pm_runtime_put_sync(const void* dev)
{
	(void)dev;
	return 0;
}

static inline void pm_runtime_mark_last_busy(const void* dev)
{
	(void)dev;
}

static inline void pm_wakeup_event(const void* dev, unsigned int msec)
{
	(void)dev;
	(void)msec;
}


/*
 * Four more runtime-PM entries serial_core.c calls, on the transmit path
 * (serial_core.c:149-160), plus the wakeup trio the system-sleep handlers
 * use at :2305-2324 and :3128.
 *
 * The interesting one is the pair at :160:
 *
 *     if (!pm_runtime_enabled(port->dev) || pm_runtime_active(&port_dev->dev))
 *             port->ops->start_tx(port);
 *
 * -- "runtime PM off, or the device is already up, then transmit".  Answering
 * enabled = 1 here would have made the second half load-bearing, and a
 * `pm_runtime_active` that said "yes" would be claiming a power state DCL
 * does not track; saying *disabled* is both the truth (nothing ever enabled
 * it) and the answer that makes mainline's own disjunct do the right thing,
 * because !0 short-circuits straight to start_tx.  A device with no runtime
 * PM is always usable, which is exactly what the first arm means.
 *
 * The wakeup family is DCL's answer for the same reason: no device is
 * wake-capable, so device_may_wakeup() is 0 and the enable/disable arms
 * around it -- which would arm the GIC to wake the SoC -- are never reached.
 * device_may_wakeup() still tolerates a NULL dev, because serial_core.c:2383
 * calls it on the result of device_find_child() with no NULL check.
 */
static inline int pm_runtime_get(const void* dev)
{
	(void)dev;
	return 0;
}

static inline void pm_runtime_put_noidle(const void* dev)
{
	(void)dev;
}

static inline int pm_runtime_enabled(const void* dev)
{
	(void)dev;
	return 0;	/* never enabled: mainline's "treat as always usable" */
}

static inline int pm_runtime_active(const void* dev)
{
	(void)dev;
	return 0;
}

/*
 * mainline keeps these three in <linux/pm_wakeup.h>; serial_core.c does not
 * include it, and DCL has no wakeup-source registry to hang a header off --
 * so they sit beside the other PM no-ops, with the reason stated once.
 */
static inline int device_may_wakeup(const void* dev)
{
	(void)dev;
	return 0;
}

static inline void device_set_wakeup_capable(const void* dev, int enable)
{
	(void)dev;
	(void)enable;
}

static inline void device_set_awake_path(const void* dev)
{
	(void)dev;
}
#endif /* __LINUX_PM_RUNTIME_H__ */
