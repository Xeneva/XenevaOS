/*
 * DCL/linux_irq_shim.c -- stage 1 ("primitives") of milestone 3, the 8250 /
 * serial core port. Every body the new BaseHdr/linux headers declared, plus
 * a boot self-test so this stage is verifiable at runtime rather than merely
 * compiling.
 *
 * The decisions, in one place:
 *
 *   request_irq()   -> GIC SPI handler, via a (irq -> handler, dev_id) table
 *                      and one dispatcher. Linux's irq number on these boards
 *                      is the HW irq, and the GIC numbers SGIs 0-15, PPIs
 *                      16-31 and SPIs from 32, so the SPI id is irq - 32; an
 *                      irq below 32 is refused rather than silently folded
 *                      onto the wrong vector. GICRegisterSPIHandler() is
 *                      first-writer-wins and returns no feedback, so the slot
 *                      is read back through GICGetSPIHandler() first: a native
 *                      driver already owning the line reports -EBUSY instead
 *                      of handing 8250 a handler that will never be called.
 *
 *   enable/disable_irq  -> accepted, not implemented. Xeneva's GIC has no
 *                      per-SPI mask here, and 8250_port.c:2181/2203 bracket a
 *                      reprogram with disable/enable. Leaving the line enabled
 *                      is safe *because* serial8250_interrupt() reads the IIR
 *                      first and answers IRQ_NONE when nothing is pending --
 *                      a invocation during shutdown exits without touching
 *                      registers. It is a smaller guarantee than masking, so
 *                      it is stated rather than assumed.
 *
 *   inb/outb        -> answer 0 and log once. ARM64 has no x86 I/O space, and
 *                      every port we intend to bind is MMIO (QEMU's pci-serial
 *                      BAR, iMX8MP's UART -- both go through readb/writeb,
 *                      already in <linux/kernel.h>). 0 rather than 0xff: an
 *                      all-ones LSR looks like "transmitter always empty",
 *                      which would let the driver believe it is talking to a
 *                      live UART, while 0 says "nothing ready" and drives
 *                      every polling loop into its timeout and out again.
 *                      Failing toward the timeout is what keeps a
 *                      misconfigured port from becoming a boot hang.
 *
 *   jiffies         -> derived from the ARM generic timer. See <linux/timer.h>
 *                      for why an extern backed by a variable nothing
 *                      increments would be worse than no symbol at all.
 *
 *   schedule()      -> a millisecond of yield. tty_open() retries with
 *                      `schedule(); goto retry_open;` (tty_io.c:2127, :2153);
 *                      with no yield that is a hard spin on a condition only
 *                      an interrupt can change.
 *
 *   console         -> recorded, not driven. UARTDebugOut() already owns the
 *                      serial port; letting mainline's uart console write the
 *                      same registers would interleave two writers at register
 *                      level. See <linux/console.h>.
 *
 * The self-test at the bottom is what makes the stage checkable: jiffies
 * actually advancing is the property every driver timeout depends on, and
 * getting it wrong looks exactly like a working kernel that never notices a
 * deadline.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>

#include <Mm/pmmngr.h>		/* P2V */
#include <Hal/AA64/gic.h>	/* GICRegisterSPIHandler + read-back */
#include <Drivers/uart.h>	/* UARTDebugOut */
/* No <process.h>: it includes Xeneva's <list.h>, whose
 * list_add(list_t*, void*) cannot share a translation unit with mainline's
 * list_add(list_head*, list_head*). See the note in <linux/list.h>. The
 * process name is read from the thread instead, which costs 8 characters of
 * `comm` and keeps the two APIs apart. */

/* The new headers are all included here on purpose: this file is the compile
 * test for the set, and a header nothing parses is a header nobody has
 * checked. */
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/cleanup.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/hashtable.h>
#include <linux/ioport.h>
#include <linux/console.h>
#include <linux/poll.h>
#include <linux/ktime.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>

/* ── interrupt registration ─────────────────────────────────────────────── */

#define DCL_IRQ_SPI_BASE	32	/* first GIC SPI, i.e. Linux irq 32 */
#define DCL_IRQ_MAX		1024	/* bounds every port-irq validity check */
#define DCL_MAX_IRQS		16	/* registered handlers at any one time */

struct dcl_irq {
	unsigned int irq;
	int spi;
	int used;
	irq_handler_t handler;
	void* dev_id;
	const char* name;
};

static struct dcl_irq dcl_irqs[DCL_MAX_IRQS];

/* One dispatcher for every slot: the GIC hands it the SPI, and the table maps
 * that back to the (irq, dev_id) the driver asked for. */
static void dcl_irq_dispatch(int spi)
{
	int i;
	for (i = 0; i < DCL_MAX_IRQS; i++) {
		if (dcl_irqs[i].used && dcl_irqs[i].spi == spi) {
			dcl_irqs[i].handler(dcl_irqs[i].irq, dcl_irqs[i].dev_id);
			return;
		}
	}
	/* An SPI with no table entry means a native driver owns it and the
	 * GIC routed here by mistake -- nothing to do, and nothing to crash. */
}

int request_irq(unsigned int irq, irq_handler_t handler, unsigned long flags,
			const char* name, void* dev_id)
{
	int spi, i, free_slot = -1;

	(void)flags;

	if (!handler || irq >= DCL_IRQ_MAX)
		return -EINVAL;

	spi = (int)irq - DCL_IRQ_SPI_BASE;
	if (spi < 0) {
		/* SGI or PPI: a per-CPU line no serial driver owns. */
		UARTDebugOut("[DCL] request_irq(%u): below the SPI range, "
				"refusing rather than remapping\r\n", irq);
		return -EINVAL;
	}

	for (i = 0; i < DCL_MAX_IRQS; i++) {
		if (dcl_irqs[i].used && dcl_irqs[i].irq == irq)
			return -EBUSY;
		if (!dcl_irqs[i].used && free_slot < 0)
			free_slot = i;
	}
	if (free_slot < 0)
		return -EBUSY;

	/* First-writer-wins underneath: if the slot is taken the registration
	 * is ignored, which would leave this table claiming a handler the GIC
	 * will never call. Ask before believing. */
	if (GICGetSPIHandler(spi) &&
			GICGetSPIHandler(spi) != (void*)dcl_irq_dispatch) {
		UARTDebugOut("[DCL] request_irq(%u): spi %d already owned "
				"natively\r\n", irq, spi);
		return -EBUSY;
	}

	dcl_irqs[free_slot].irq = irq;
	dcl_irqs[free_slot].spi = spi;
	dcl_irqs[free_slot].handler = handler;
	dcl_irqs[free_slot].dev_id = dev_id;
	dcl_irqs[free_slot].name = name;
	dcl_irqs[free_slot].used = 1;

	GICRegisterSPIHandler((void*)dcl_irq_dispatch, spi);
	return 0;
}

void free_irq(unsigned int irq, void* dev_id)
{
	int i;
	for (i = 0; i < DCL_MAX_IRQS; i++) {
		if (!dcl_irqs[i].used || dcl_irqs[i].irq != irq)
			continue;
		if (dev_id && dcl_irqs[i].dev_id != dev_id)
			continue;	/* a different owner: leave it alone */
		dcl_irqs[i].used = 0;
		dcl_irqs[i].handler = 0;
		/* Only clear the GIC slot if it still holds our dispatcher --
		 * never a native driver's registration. */
		if (GICGetSPIHandler(dcl_irqs[i].spi) ==
				(void*)dcl_irq_dispatch)
			GICClearSPIHandler(dcl_irqs[i].spi);
		return;
	}
}

void enable_irq(unsigned int irq)
{
	(void)irq;
}

void disable_irq(unsigned int irq)
{
	(void)irq;
}

void synchronize_irq(unsigned int irq)
{
	(void)irq;
	/* Nothing is masked, so no interrupt can be in flight against a
	 * handler that has been unregistered -- there is no interrupt
	 * pipeline state to wait for. */
}

int irq_get_nr_irqs(void)
{
	return DCL_IRQ_MAX;
}

/* ── port I/O ───────────────────────────────────────────────────────────── */

static int dcl_pio_warned;

static void dcl_pio_note(void)
{
	if (dcl_pio_warned)
		return;
	dcl_pio_warned = 1;
	UARTDebugOut("[DCL] port I/O: ARM64 has no x86 I/O space, answering 0 "
			"(a PIO-bound UART cannot bind on this platform)\r\n");
}

unsigned char inb(unsigned long addr)
{
	(void)addr;
	dcl_pio_note();
	return 0;
}

void outb(unsigned char v, unsigned long addr)
{
	(void)v;
	(void)addr;
	dcl_pio_note();
}

unsigned short inw(unsigned long addr)
{
	(void)addr;
	dcl_pio_note();
	return 0;
}

void outw(unsigned short v, unsigned long addr)
{
	(void)v;
	(void)addr;
	dcl_pio_note();
}

unsigned int inl(unsigned long addr)
{
	(void)addr;
	dcl_pio_note();
	return 0;
}

void outl(unsigned int v, unsigned long addr)
{
	(void)v;
	(void)addr;
	dcl_pio_note();
}

/* ── windowing ──────────────────────────────────────────────────────────── */

void* ioremap(unsigned long phys_addr, unsigned long size)
{
	(void)size;
	/* No window to carve: the linear physical->virtual map covers it, so
	 * iounmap() has nothing to tear down either. */
	return (void*)(uintptr_t)P2V((uint64_t)phys_addr);
}

void iounmap(void* addr)
{
	(void)addr;
}

/* ── time ───────────────────────────────────────────────────────────────── */

extern uint64_t get_cntpct_el0(void);
extern uint64_t get_cntfrq_el0(void);

static uint64_t dcl_cntfrq(void)
{
	uint64_t f = get_cntfrq_el0();
	return f ? f : 1;
}

unsigned long dcl_jiffies_now(void)
{
	uint64_t cnt = get_cntpct_el0();
	uint64_t frq = dcl_cntfrq();
	uint64_t secs = cnt / frq;
	uint64_t rem = cnt % frq;

	return (unsigned long)(secs * (uint64_t)HZ + (rem * (uint64_t)HZ) / frq);
}

/* ktime_get() is deliberately not defined here: <linux/kernel.h>:287 already
 * provides it as a static inline over this same counter, and a second
 * definition is a compile error -- which is what the first draft of this file
 * did, and the smoke-include caught. */
long long ktime_get_real_seconds(void)
{
	/* Seconds since boot: Xeneva carries no RTC-backed wall clock, and
	 * tty_io.c:800 only stamps a "first opened" time for a deprecation
	 * warning, where a monotonic figure is the truthful answer. */
	return (long long)(dcl_jiffies_now() / HZ);
}

/* ── task and scheduling ────────────────────────────────────────────────── */

static struct signal_struct dcl_signal;
static struct task_struct dcl_task = { "kernel", 0, &dcl_signal };

/*
 * The native half is another translation unit: <Hal/AA64/sched.h> carries
 * Xeneva's <list.h>, whose list_add(list_t*, void*) cannot share a TU with
 * mainline's static inline list_add (see the landmine note there), and this
 * file needs mainline's for its wait queues. DCL/current_ident.c keeps the
 * thread layout on the native side and hands over three scalars.
 */
extern int dcl_current_ident(char* buf, int cap, int* pid);

struct task_struct* dcl_current(void)
{
	int pid;

	/*
	 * One shared task rather than a per-thread table: the nine files read
	 * `current` for comm (two deprecation warnings) and signal->tty (one
	 * permission gate), and never write either, so refreshing comm on the
	 * way out answers truthfully for whichever context asked.
	 *
	 * A thread with no name keeps the static "kernel" instead of leaving a
	 * warning to print an empty one -- dcl_current_ident() only writes when
	 * there is a name to write, and only reports a pid when there is a
	 * thread to take one from.
	 */
	if (dcl_current_ident(dcl_task.comm, (int)sizeof(dcl_task.comm), &pid) >= 0)
		dcl_task.pid = pid;
	return &dcl_task;
}

void schedule(void)
{
	/* No scheduler to switch to: a millisecond of yield keeps
	 * tty_open()'s `schedule(); goto retry_open;` a retry loop rather than
	 * a hard spin. */
	mdelay(1);
}

long schedule_timeout(long timeout)
{
	long ms;

	if (timeout <= 0)
		return 0;

	if (timeout >= MAX_SCHEDULE_TIMEOUT) {
		/* Cannot wait forever, but must not claim the wait expired
		 * either: yield, and hand the full timeout back so a caller's
		 * loop still sees itself as waiting. */
		mdelay(1);
		return timeout;
	}

	/* Clamp before converting: timeout can approach LONG_MAX/2, and
	 * timeout * 1000 would overflow on the way to milliseconds. */
	if (timeout > 100 * HZ)
		ms = 100;
	else
		ms = (timeout * 1000) / HZ;
	if (ms < 1)
		ms = 1;

	mdelay((unsigned int)ms);
	return timeout - (ms * HZ) / 1000;
}

long schedule_timeout_interruptible(long timeout)
{
	/*
	 * Identical to schedule_timeout() on purpose, and the reason is in
	 * <linux/sched.h>: signal_pending() is always 0 in DCL, so the one
	 * thing that makes mainline's _interruptible form return early -- a
	 * pending signal -- cannot happen.  Delegating rather than copying
	 * keeps the clamp and the yield in one place: two bodies that both
	 * converted `timeout` jiffies to milliseconds would be two places for
	 * the overflow guard at the bottom of schedule_timeout() to be
	 * forgotten.
	 */
	return schedule_timeout(timeout);
}

long schedule_timeout_killable(long timeout)
{
	/* "Killable" would return -ERESTARTSYS on a fatal signal; there are no
	 * signals to deliver (see signal_pending below), so the wait is the
	 * plain one. */
	return schedule_timeout(timeout);
}

int signal_pending(struct task_struct* tsk)
{
	(void)tsk;
	/* Xeneva has no signals. Recorded consequence: uart_wait_modem_status()
	 * (serial_core.c:1200) loops on modem change or signal, so TIOCMWAIT
	 * on a quiet port never returns -- that needs a bounded wait at the
	 * serial_core call site, stage 4's list. */
	return 0;
}

int fatal_signal_pending(struct task_struct* tsk)
{
	(void)tsk;
	return 0;
}

void task_lock(struct task_struct* p)
{
	(void)p;
}

void task_unlock(struct task_struct* p)
{
	(void)p;
}

/* ── wait queues ────────────────────────────────────────────────────────── */

int default_wake_function(struct wait_queue_entry* wq_entry,
				unsigned mode, int flags, void* key)
{
	(void)mode;
	(void)flags;
	(void)key;
	/* No task to actually resume, so "handled" is the honest answer for
	 * the exclusive-wake accounting -- and it stops the walk at the first
	 * waiter, which is what nr_exclusive asked for. */
	wq_entry->flags |= WQ_FLAG_WOKEN;
	return 1;
}

int woken_wake_function(struct wait_queue_entry* wq_entry,
				unsigned mode, int flags, void* key)
{
	(void)mode;
	(void)flags;
	(void)key;
	/* Mainline's form for the tty read/write loops: mark the waiter woken
	 * and leave it on the queue for the caller to remove. */
	wq_entry->flags |= WQ_FLAG_WOKEN;
	return 1;
}

void add_wait_queue(struct wait_queue_head* wq_head,
			struct wait_queue_entry* wq_entry)
{
	wq_entry->flags &= ~WQ_FLAG_EXCLUSIVE;
	list_del_init(&wq_entry->entry);
	list_add_tail(&wq_entry->entry, &wq_head->head);
}

void remove_wait_queue(struct wait_queue_head* wq_head,
			struct wait_queue_entry* wq_entry)
{
	(void)wq_head;
	list_del_init(&wq_entry->entry);
}

void __wake_up(struct wait_queue_head* wq_head, unsigned int mode,
			int nr_exclusive, void* key)
{
	struct wait_queue_entry* wq_entry;
	struct wait_queue_entry* tmp;

	list_for_each_entry_safe(wq_entry, tmp, &wq_head->head, entry) {
		if (wq_entry->func(wq_entry, mode, 0, key) && nr_exclusive) {
			nr_exclusive--;
			if (!nr_exclusive)
				break;
		}
	}
}

unsigned int wait_woken(struct wait_queue_entry* wq_entry,
			unsigned int state, long timeout)
{
	(void)state;
	(void)timeout;
	/* Returns immediately: there is nothing to sleep on, so the caller's
	 * loop re-tests its own condition on the next pass. Bounding those
	 * loops is stage 5's cut (n_tty.c:2232 and :2354). */
	return wq_entry->flags;
}

void dcl_wait_event_timeout(const char* func)
{
	/* Falling through after a bounded wait is the wrong answer dressed as
	 * the right one, so it says so instead of looking like a normal boot:
	 * an unconditional block is a hung kernel, an unconditional
	 * fall-through is a corrupt one. */
	UARTDebugOut("[DCL] wait_event in %s gave up: no scheduler to sleep "
			"in, proceeding\r\n", func ? func : "?");
}

/* ── console registration ───────────────────────────────────────────────── */

#define DCL_MAX_CONSOLES 4

int console_suspend_enabled = 0;	/* no CONFIG_PM_SLEEP, no suspend */
static struct console* dcl_consoles[DCL_MAX_CONSOLES];

int register_console(struct console* con)
{
	int i;

	if (!con)
		return -EINVAL;

	for (i = 0; i < DCL_MAX_CONSOLES; i++)
		if (dcl_consoles[i] == con)
			return 0;

	for (i = 0; i < DCL_MAX_CONSOLES; i++) {
		if (!dcl_consoles[i]) {
			/* Recorded so console_is_registered() answers true and
			 * serial_core.c:2574 stops asking -- but the UART is
			 * not handed over; UARTDebugOut() keeps writing it. */
			dcl_consoles[i] = con;
			return 0;
		}
	}
	return -ENOSPC;
}

int unregister_console(struct console* con)
{
	int i;
	for (i = 0; i < DCL_MAX_CONSOLES; i++) {
		if (dcl_consoles[i] == con) {
			dcl_consoles[i] = 0;
			return 0;
		}
	}
	return -EINVAL;
}

/*
 * Set by panic() in <linux/printk.h> and read by serial8250_console_write()
 * (8250_port.c:3341), which skips taking the port lock when it is nonzero --
 * the CPU reporting a fault may already hold that lock, and locking here would
 * deadlock inside the crash output.  See <linux/kernel.h> for why it is a
 * variable and not a macro.
 *
 * Nothing else in DCL sets it today: exceptions are reported by
 * KernelAA64's handlers without going through printk's panic(), so a normal
 * fault leaves this clear and console writes keep locking as usual.  Wiring
 * the exception path to set it is a separate change and belongs with
 * whatever makes that path print -- see DCL_TODO.md.
 */
int oops_in_progress = 0;

int console_is_registered(struct console* con)
{
	int i;
	if (!con)
		return 0;
	for (i = 0; i < DCL_MAX_CONSOLES; i++)
		if (dcl_consoles[i] == con)
			return 1;
	return 0;
}

void console_lock(void) { }
void console_unlock(void) { }
void console_trylock(void) { }

void console_suspend(struct console* con)
{
	(void)con;
	/* uart_suspend_port()/uart_resume_port() only -- Xeneva has no suspend
	 * path to run them on. */
}

void console_resume(struct console* con)
{
	(void)con;
	/* The other half of console_suspend() above: there is nothing to
	 * resume, for the same reason there was nothing to suspend.  Called
	 * from uart_resume_port() with console_suspend_enabled already
	 * tested (serial_core.c:2419), and declared in <linux/console.h>
	 * next to its partner. */
}

/* ── I/O window claims ──────────────────────────────────────────────────── */

#define DCL_MAX_REGIONS 32

static struct {
	resource_size_t start;
	resource_size_t end;
	const char* name;
	int used;
} dcl_regions[DCL_MAX_REGIONS];

static struct resource dcl_region_res[DCL_MAX_REGIONS];

static struct resource* dcl_request_region(resource_size_t start,
					resource_size_t n, const char* name,
					unsigned long flags)
{
	resource_size_t end = start + (n ? n : 1);
	int i, free_slot = -1;

	for (i = 0; i < DCL_MAX_REGIONS; i++) {
		if (!dcl_regions[i].used) {
			if (free_slot < 0)
				free_slot = i;
			continue;
		}
		/* Overlap: the honest answer is NULL, so mainline's callers
		 * unwind. Two drivers on one window is what corrupts a LCR. */
		if (start < dcl_regions[i].end && dcl_regions[i].start < end)
			return 0;
	}
	if (free_slot < 0)
		return 0;

	dcl_regions[free_slot].start = start;
	dcl_regions[free_slot].end = end;
	dcl_regions[free_slot].name = name;
	dcl_regions[free_slot].used = 1;

	dcl_region_res[free_slot].start = start;
	dcl_region_res[free_slot].end = end - 1;
	dcl_region_res[free_slot].name = name;
	dcl_region_res[free_slot].flags = flags;
	return &dcl_region_res[free_slot];
}

static void dcl_release_region(resource_size_t start, resource_size_t n)
{
	resource_size_t end = start + (n ? n : 1);
	int i;
	for (i = 0; i < DCL_MAX_REGIONS; i++) {
		if (dcl_regions[i].used && dcl_regions[i].start == start &&
				dcl_regions[i].end == end) {
			dcl_regions[i].used = 0;
			return;
		}
	}
}

/*
 * iomem_resource -- the root of the I/O memory *tree*, kept empty.
 *
 * drm_cache.c:204 walks it to find the highest mapped address and decide
 * whether a device with a narrower DMA mask needs bounce buffers:
 *
 *     for (tmp = iomem_resource.child; tmp; tmp = tmp->sibling)
 *         max_iomem = max(max_iomem, tmp->end);
 *     return max_iomem > ((u64)1 << dma_bits);
 *
 * This kernel maps nothing through that tree, so child is NULL, the loop
 * never runs, max_iomem keeps the 0 it was initialised with, and
 * drm_need_swiotlb() answers false. That is the correct answer and not a
 * stubbed one: there is no SWIOTLB in Xeneva to bounce against.
 *
 * The regions this file does track live in dcl_regions[] above as a flat
 * array with a used flag, which is why a NULL child does not contradict it --
 * the array is a claim table for hand-registered UART and virtio windows, not
 * a hierarchy, and nothing has ever asked it for its children.
 */
struct resource iomem_resource = {
	.name = "PCI mem",
};

struct resource* request_mem_region(resource_size_t start, resource_size_t n,
					const char* name)
{
	return dcl_request_region(start, n, name, IORESOURCE_MEM);
}

struct resource* request_region(resource_size_t start, resource_size_t n,
				const char* name)
{
	return dcl_request_region(start, n, name, IORESOURCE_IO);
}

void release_mem_region(resource_size_t start, resource_size_t n)
{
	dcl_release_region(start, n);
}

void release_region(resource_size_t start, resource_size_t n)
{
	dcl_release_region(start, n);
}

/* ── poll ───────────────────────────────────────────────────────────────── */

void poll_wait(struct file* file, struct wait_queue_head* wait_address,
			poll_table* p)
{
	(void)file;
	(void)wait_address;
	(void)p;
	/* Deliberately empty: there is no sleeping scheduler to block in, so
	 * registering a waiter could not wake anyone. A blocking select()
	 * therefore re-reads state on the caller's own timeout, which is what
	 * Xeneva's select takes as an argument. See <linux/poll.h>. */
}

/* ── boot self-test ─────────────────────────────────────────────────────── */

static irqreturn_t dcl_prim_test_handler(int irq, void* dev_id)
{
	(void)irq;
	(void)dev_id;
	return IRQ_HANDLED;
}

/*
 * DclPrimTestRun -- stage 1's runtime gate, called from init.c beside
 * DclMemTestRun(). It checks the properties the 8250 port will silently rely
 * on later, so a regression here shows up as one line on the serial log
 * instead of a UART that never receives a byte.
 */
void DclPrimTestRun(void)
{
	int ok = 0, fail = 0;
	unsigned long j_before, j_after;

	/* 1. an SPI irq registers */
	if (request_irq(DCL_IRQ_SPI_BASE + 900, dcl_prim_test_handler, 0,
			"dclprim", 0) == 0)
		ok++;
	else
		fail++;

	/* 2. a second handler on the same irq is refused, not stacked */
	if (request_irq(DCL_IRQ_SPI_BASE + 900, dcl_prim_test_handler, 0,
			"dclprim", 0) == -EBUSY)
		ok++;
	else
		fail++;

	/* 3. an SGI/PPI number is refused rather than remapped */
	if (request_irq(5, dcl_prim_test_handler, 0, "dclprim", 0) == -EINVAL)
		ok++;
	else
		fail++;

	/* 4. release, then register again */
	free_irq(DCL_IRQ_SPI_BASE + 900, 0);
	if (request_irq(DCL_IRQ_SPI_BASE + 900, dcl_prim_test_handler, 0,
			"dclprim", 0) == 0)
		ok++;
	else
		fail++;
	free_irq(DCL_IRQ_SPI_BASE + 900, 0);

	/* 5. ioremap is the linear map */
	if (ioremap(0x1000, 0x1000) == (void*)(uintptr_t)P2V(0x1000))
		ok++;
	else
		fail++;

	/* 6. jiffies advances -- the property every timeout depends on, and
	 *    the one a missing tick would quietly break. */
	j_before = jiffies;
	mdelay(8);
	j_after = jiffies;
	if (j_after > j_before)
		ok++;
	else
		fail++;

	/* 7. a PIO read answers 0 (and logs once, by design) */
	if (inb(0x3f8) == 0)
		ok++;
	else
		fail++;

	/* 8. an overlapping window claim is refused, a disjoint one granted */
	if (request_mem_region(0x90000000ULL, 0x1000, "dclprim")) {
		if (request_mem_region(0x90000400ULL, 0x1000, "dclprim2") == 0)
			ok++;
		else
			fail++;
		release_mem_region(0x90000000ULL, 0x1000);
	} else {
		fail++;
	}

	/* 9. a wake reaches a waiter's own wake function */
	{
		DECLARE_WAIT_QUEUE_HEAD(wqh);
		DECLARE_WAITQUEUE(ent, dcl_current());

		add_wait_queue(&wqh, &ent);
		wake_up(&wqh);
		if (ent.flags & WQ_FLAG_WOKEN)
			ok++;
		else
			fail++;
		remove_wait_queue(&wqh, &ent);
	}

	/* 10. current resolves to a named task */
	if (dcl_current() && dcl_current()->comm[0])
		ok++;
	else
		fail++;

	UARTDebugOut("dcl primitives: %d ok, %d failed\r\n", ok, fail);
}
