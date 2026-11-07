/**
 * WORK IN PROGRESS, Building Linux Compat Layer for drivers
 */
#ifndef __LINUX_KERNEL_H__
#define __LINUX_KERNEL_H__

#include <stdint.h>
#include <Hal/AA64/aa64cpu.h>
#include <Mm/kmalloc.h>
#include <_null.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <linux/build_bug.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef uint8_t  __u8;
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;
typedef int8_t __s8;
typedef int16_t __s16;
typedef int32_t __s32;
typedef int64_t __s64;

typedef uint8_t __le8;
typedef uint16_t __le16;
typedef uint32_t __le32;
typedef uint64_t __le64;
typedef uint16_t __be16;
typedef uint32_t __be32;
typedef uint64_t __be64;

typedef uintptr_t phys_addr_t;
typedef uintptr_t resource_size_t;
typedef uintptr_t dma_addr_t;
typedef uintptr_t io_addr_t;
typedef int spinlock_t;
typedef int mutex;

typedef uint64_t sector_t;
typedef uint64_t blkcnt_t;
typedef uint64_t loff_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef uint32_t dev_t;
typedef uint32_t ino_t;
typedef uint32_t nlink_t;

typedef unsigned int gfp_t;
typedef unsigned int fmode_t;
typedef unsigned int umode_t;
/*
 * irqreturn_t and the IRQ_NONE/IRQ_HANDLED/IRQ_WAKE_THREAD enumerators live
 * in <linux/irqreturn.h>, which is mainline's byte-port of that header and
 * spells the type as `enum irqreturn` rather than int.  This used to be a
 * `typedef int irqreturn_t;` here plus three `#define IRQ_*` macros further
 * down, and both halves collide with the port: the enum members get macro
 * expansion in their own declaration list ("expected identifier" at
 * irqreturn.h:12) and the second typedef is a redefinition with different
 * types.  Taking the header keeps the enumerators as enumerators, which is
 * what makes a handler's return value checkable at all -- with
 * IRQ_RETVAL(x) as `((x) != IRQ_NONE)` an int answer would compare, but a
 * typo'd one would too.
 */
#include <linux/irqreturn.h>
typedef unsigned long irq_hw_number_t;
typedef unsigned long pgoff_t;
typedef unsigned long kernel_ulong_t;

#include <linux/compiler.h>
/*
 * log2.h and math.h sit here because mainline's <linux/kernel.h> includes both
 * and a ported source reaches is_power_of_2(), roundup_pow_of_two() and the
 * DIV_ROUND_* family that way, never by naming either header. hexdump.c
 * (is_power_of_2 at :103), drm_mm.c (roundup_pow_of_two at :1096) and
 * drm_rect.c (DIV_ROUND_UP_ULL) are the current instances.
 *
 * The failure without them is quiet: the call is implicitly declared, which
 * compiles -- this tree downgrades -Wimplicit-function-declaration -- and the
 * definition is a header-side static inline that emitted its body into some
 * other translation unit. The reference survives to the link and stops there.
 */
#include <linux/log2.h>
#include <linux/math.h>
/*
 * minmax.h comes before bitmap/list/module so that min/max/min_t/max_t land
 * in the same place mainline puts them -- mainline's <linux/kernel.h>
 * includes <linux/minmax.h>, and a ported source reaches min_t() by including
 * <linux/kernel.h> through its own <linux/...> includes, never by naming
 * minmax.h itself. virtio-rng.c is the current instance (its copy_data()
 * writes min_t(unsigned int, ...) twice).
 *
 * It is safe this early: minmax.h includes nothing, and every one of its four
 * definitions is #ifndef-guarded, so anything below that defines its own
 * min/max (mm.h does, byte-identically) either wins or redefines to the same
 * tokens -- both legal. ARRAY_SIZE in minmax.h is guarded too, and identical
 * to the one kernel.h declares further down.
 */
#include <linux/minmax.h>
/*
 * string.h and sprintf.h: mainline reaches memcpy() and sprintf() from
 * <linux/kernel.h> too -- memcpy through its device/string chain, sprintf
 * explicitly (`#include <linux/sprintf.h>`). DCL has no device.h to walk, so
 * both come in directly. A ported source never names either header itself:
 * virtio-rng.c calls memcpy() at :94 and sprintf() at :165 while including
 * only <linux/err.h> and <linux/virtio.h>.
 *
 * Without these two the calls are implicitly declared, which still *links*
 * (implicit declarations are allowed here) and produces no warning by
 * default -- that is the failure mode: sprintf's return type happens to be
 * int, so the compiler has nothing to complain about until the day a call
 * passes a mismatched pointer, at which point it would have been checking an
 * assumption rather than a declaration.
 */
#include <linux/string.h>
#include <linux/sprintf.h>
#include <linux/bitmap.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/usb/ch9.h>
#include <linux/usb/otg.h>
#include <linux/virtio.h>

#define udelay(us)  AA64SleepUS(us)
#define mdelay(ms) AA64SleepMS(ms)
#define msleep(ms) mdelay(ms)
#define usleep(us) AA64SleepUS(us)

#define GFP_KERNEL 0
#define GFP_ATOMIC 1
#define GFP_DMA 2

/*
 * The two xarray.c names that are not among those three.  lib/xarray.c
 * includes <linux/slab.h>, and mainline's slab.h pulls <linux/gfp.h> in for
 * exactly these; DCL's does not, so they live beside the rest of the gfp
 * surface here rather than in a header xarray never reads.
 *
 * GFP_NOWAIT is "allocate without waiting", which in DCL's two-value scheme
 * is the same answer GFP_ATOMIC already gives: the allocator neither blocks
 * nor refuses, so the flag changes no path -- it has to *parse*, and 1 says
 * what the caller meant.  __GFP_ACCOUNT is memcg accounting, which this tree
 * does not have; 0 is not a placeholder but the whole of what accounting is
 * worth here, and it is written as a flag-shaped 0 so that
 * `GFP_NOWAIT | __GFP_ACCOUNT` (xarray.c:308, :338, :376) folds to GFP_NOWAIT
 * instead of to an undeclared identifier.
 */
#define GFP_NOWAIT 1
#define __GFP_ACCOUNT 0

/*
 * __GFP_BITS_SHIFT -- first bit above the gfp word, which is where xarray
 * keeps its three mark bits (XA_FLAGS_MARK, xarray.h:277).  mainline takes
 * it from the end of gfp_types.h's flag list; DCL's flags are the three
 * sentinels above, and the xa_flags low bits (4/8/16/32) claim 2..5, so 26
 * is mainline's clear high ground rather than a next-to-nothing guess.
 */
#define __GFP_BITS_SHIFT 26

#define container_of(ptr, type, member) \
     ((type*)((char*)(ptr)- offsetof(type,member)))

#ifndef offsetof
#if defined(__GNUC__) || defined(__clang__)
#define offsetof(type, member) __builtin_offsetof(type, member)
#else
#define offsetof(type, member) \
     ((size_t)&((type*)0)->member)
#endif
#endif

/*
 * IS_ENABLED(), IS_BUILTIN(), IS_MODULE(), IS_REACHABLE() and the CONFIG_
 * table they read are <linux/kconfig.h>'s -- mainline's kernel.h defines none
 * of them.  They are reached here rather than in kconfig.h's own callers
 * because this is the include DCL's drivers already carry, so hanging it on
 * the header they all take is what keeps the table in scope wherever the
 * macro is.  Without that the macro silently reads an undeclared identifier
 * and the error lands in the *driver*, three includes away, as "use of
 * undeclared identifier 'CONFIG_SERIAL_8250_CONSOLE'" at 8250_port.c:3197
 * while the definition sits correctly in autoconf.h:42.
 *
 * kconfig.h pulls <linux/autoconf.h> in itself (mainline: generated/
 * autoconf.h) and asks nothing else of the tree, so it cannot cycle.
 *
 * What goes with the old `!!(opt)` spelling is the rule it forced:
 * "define every option, even at 0".  mainline's token paste never names the
 * identifier, so an absent CONFIG_* reads as 0 in a #if *and* in a function
 * body -- while an option present at 0 is still present to an #ifdef, which
 * is how a defined-but-false option would have answered #ifdef CONFIG_CPU_BIG
 * _ENDIAN with "yes".  autoconf.h keeps only what is on, mainline's rule.
 */
#include <linux/kconfig.h>

/*
 * might_sleep() -- "this function may schedule, so do not call it from a
 * context that cannot".  tty_ldsem.c puts one on every ldsem acquisition,
 * because a contended semaphore really does park.
 *
 * mainline's is a real check when CONFIG_DEBUG_ATOMIC_SLEEP is on (it
 * complains if irqs are disabled) and might_resched() otherwise; the
 * preprocessor spelling `# define might_sleep() ...` in
 * include/linux/kernel.h:90 is the no-debug arm, and empty is what DCL
 * matches -- there is no atomic-context bookkeeping to compare against.
 * It has to be a macro, not a function: it is called with no arguments at
 * all, so a declaration would have to be `void might_sleep()` (a call, not a
 * definition) and would have left the body to a link error.
 *
 * The annotation is honest in the other direction too: DCL's schedule() is
 * mdelay(1), so sleeping here is always bounded, and nothing in this build
 * can be in an atomic section it did not choose.
 */
#define might_sleep()				do {} while (0)

#define spin_lock_init(l) do{} while(0)
#define spin_lock_irqsave(l,f) do{(f) = 0;}while(0)
#define spin_unlock_irqrestore(l, f) do{}while(0)
#define mutex_init(m)  do{} while(0)
#define mutex_lock(m)  do{}while(0)
#define mutex_unlock(m)  do{} while(0)

/* IRQ_NONE / IRQ_HANDLED / IRQ_WAKE_THREAD were defined here as 0/1/2 and
 * are now <linux/irqreturn.h>'s enumerators, included with the typedef
 * above. See the note there. */

#ifndef __iomem
#define __iomem
#endif
#ifndef __user
#define __user
#endif
#ifndef __kernel
#define __kernel
#endif
#ifndef __force
#define __force
#endif
#ifndef __nocast
#define __nocast
#endif
#ifndef __safe
#define __safe
#endif
/*
 * __aligned(x) is NOT an empty annotation, unlike the ones around it.
 * struct tty_buffer ends in `u8 data[] __aligned(sizeof(unsigned long))`
 * (tty_buffer.h:47), and that alignment is load-bearing rather than cosmetic:
 * the buffer is allocated as one block and the flag array is found by adding
 * b->size bytes, so an under-aligned `data[]` would put every flag byte -- and
 * every subsequent flag_buf_ptr() read -- at a wrong offset relative to what
 * the store used. Xeneva's allocator hands back word-aligned memory anyway,
 * but the attribute states the requirement instead of relying on it.
 */
#ifndef __aligned
#define __aligned(x) __attribute__((aligned(x)))
#endif
#define __chk_user_ptr(x) (void)0
#define __chk_io_ptr(x) (void)0

#define EPERM 1
#define ENOENT 2
#define EIO 5
#define ENOMEM 12
#define EBUSY 16
/* Two errno numbers serial_core.c names that DCL's set did not have.  Same
 * numbering as mainline's asm-generic/errno.h, because these are visible to
 * userspace through the value write() returns -- the number *is* the
 * interface, not an internal choice.
 *
 *   ENXIO   6  "no such device or address": uart_tiocmget's failed lookup
 *              at serial_core.c:1940, uart_ioctl's at :2987.
 *   EL3HLT 46  the tty layer's "hangup requested" answer, which
 *              uart_hangup() compares against at serial_core.c:618.  -46 is
 *              not a failure there; it is the recognised "this port was hung
 *              up underneath you" result the caller switches on.
 */
#define ENXIO 6
#define E2BIG 7
#define EPROBE_DEFER 517
#define EL3HLT 46
#define ENODEV 19
#define EINVAL 22
#define ENOSPC 28
#define ETIMEDOUT 110
#define ENOTSUPP 524
/* Kernel-internal restart code -- not a userspace errno. <linux/mm.h>
 * defines it too, under #ifndef, so whichever header loads first wins and
 * both agree on 512. It belongs in the errno block because
 * scoped_cond_guard(mutex_intr, return -ERESTARTSYS, ...) in serial_core.c
 * needs it visible from <linux/mutex.h> alone, without mm.h. */
#ifndef ERESTARTSYS
#define ERESTARTSYS 512
#endif
#define MAX_ERRNO 4095
#define IS_ERR_VALUE(x) ((x) >= (uintptr_t)-MAX_ERRNO)

#define EDEADLK 35
#define ENAMETOOLONG 36
#define ENOLCK 37
#define ENOSYS 38
#define ENOTEMPTY 39
#define ELOOP 40
#define EAGAIN      11
#define EWOULDBLOCK EAGAIN
#define ENOMSG 42
#define EIDRM 43
#define ENOSTR 60
#define ENODATA 61
#define ETIME 62
#define ENOSR 63
#define EREMOTE 66
#define ENOLINK 67
#define EPROTO 71
#define EMULTIHOP 72
#define EBADMSG 74
#define EOVERFLOW 75
#define EILSEQ 84
#define EUSERS 87
#define ENOTSOCK 88
#define EDESTADDRREQ 89
#define EMSGSIZE 90
#define ENOPROTOOPT 92
#define EPROTONOSUPPORT 93
#define ESOCKTNOSUPPORT 94
#define EOPNOTSUPP 95
#define EAFNOSUPPORT 97
#define EADDRINUSE 98
#define EADDRNOTAVAIL 99
#define ENETDOWN 100
#define ENETUNREACH 101
#define ENETRESET 102
#define ECONNABORTED 103
#define ECONNRESET 104
#define ENOBUFS 105
#define EISCONN 106
#define ENOTCONN 107
#define ESHUTDOWN 108
#define ETOOMANYREGS 109
#define ECONNREFUSED 111
#define EHOSTDOWN 112

#define readb(addr) (*(volatile u8*)((uint8_t*)addr))
#define readw(addr) (*(volatile u16*)((uint8_t*)addr))
#define readl(addr) (*(volatile u32*)((uint8_t*)addr))
#define writeb(v, addr) (*(volatile u8*)((uint8_t*)addr) = (v))
#define writew(v, addr) (*(volatile u16*)((uint8_t*)addr) = (v))
#define writel(v, addr) (*(volatile u32*)((uint8_t*)addr) = (v))

#define BITS_PER_BYTE 8

/*
 * BITS_PER_LONG -- bits in an `unsigned long`, which is NOT the same as bits
 * in a pointer on this target.
 *
 * aarch64-unknown-windows is LLP64: __SIZEOF_LONG__ is 4 while
 * __SIZEOF_SIZE_T__ and pointers are 8. This was hardcoded to 64, and every
 * consumer of it inherits the mistake:
 *
 *   - bitops.h's test_bit/set_bit compute the word index as (nr)/BITS_PER_LONG
 *     and the shift as (nr)%BITS_PER_LONG. With 64, bits 32..63 land in word 0
 *     instead of word 1, and `1UL << 32..63` is a shift by >= the width of a
 *     32-bit type, which is undefined. Nothing hit it yet only because
 *     tty_struct_flags runs 0..11 and TTY_PORT_* runs 0..5, so every index is
 *     < 32 and the two spellings happen to agree.
 *   - GENMASK(h,l) shifts `~0UL` -- a 32-bit value -- by BITS_PER_LONG-1-h,
 *     i.e. by 62-h with h small: also undefined, and in practice a 0.
 *
 * It is computed rather than typed as 32 so it stays true if the target ever
 * changes; this is exactly how BaseHdr/bordoisila_bits.h spells it, and that
 * header's GENMASK had to be correct for the iMX8MP PLL register fields.
 */
/*
 * 32, not (sizeof(long) * BITS_PER_BYTE): same number, but a number a #if can
 * read.  tty_ldsem.c:36 does `#if BITS_PER_LONG == 64` and rejected the
 * expression form outright ("function-like macro 'sizeof' is not defined").
 *
 * It is genuinely 32 here and not a guess -- this is aarch64 under the LLP64
 * ABI, where long is 4 bytes -- and 32 is what the reader wants: the only
 * #if consumer sizes the active-count mask in struct ld_semaphore against
 * atomic_long_t, whose counter is a long (atomic.h:141), so the word and the
 * mask have to agree on 32 and the 64-bit arm would not.
 *
 * mainline's value on aarch64 is 64 because its long is 64 there; nothing
 * about that portability transfers, since DCL's `long` is not the machine
 * word.  Consumers that want the machine word spell it as size_t.
 */
#define BITS_PER_LONG 32

#define BIT(n) (1UL << (n))
#define BIT_ULL(n) (1ULL << (n))
#define GENMASK(h, l) (((~0UL) << (l)) & (~0UL >> (BITS_PER_LONG-1-(h))))
#define GENMASK_ULL(h,l) (((~0ULL) << (l)) & (~0ULL >> (63-(h))))

typedef int64_t ktime_t;

#define NSEC_PER_USEC 1000LL
#define NSEC_PER_MSEC 1000000LL
#define NSEC_PER_SEC  1000000000LL
#define USEC_PER_MSEC 1000LL
#define USEC_PER_SEC  1000000LL
#define MSEC_PER_SEC  1000LL

#define KTIME_MAX ((ktime_t)~((uint64_t)1<<63))
#define KTIME_MIN (~KTIME_MAX - 1)
#define KTIME_SEC_MAX (KTIME_MAX / NSEC_PER_SEC)

static inline ktime_t ktime_set(long secs, unsigned long nsecs) {
    return (ktime_t)secs * NSEC_PER_SEC + (ktime_t)nsecs;
}

static inline ktime_t ktime_add(ktime_t a, ktime_t b) {
    return a + b;
}

static inline ktime_t ktime_sub(ktime_t a, ktime_t b) {
    return a - b;
}

static inline ktime_t ktime_add_ns(ktime_t a, uint64_t ns) {
    return a + (ktime_t)ns;
}

static inline ktime_t ktime_sub_ns(ktime_t a, uint64_t ns) {
    return a - (ktime_t)ns;
}

static inline ktime_t ktime_add_us(ktime_t a, uint64_t us) {
    return a + (ktime_t)(us * NSEC_PER_USEC);
}

static inline ktime_t ktime_add_ms(ktime_t a, uint64_t ms) {
    return a + (ktime_t)(ms * NSEC_PER_MSEC);
}

static inline int ktime_compare(ktime_t a, ktime_t b) {
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
}

static inline bool ktime_after(ktime_t a, ktime_t b) {
    return a > b;
}

static inline bool ktime_before(ktime_t a, ktime_t b) {
    return a < b;
}

static inline int64_t ktime_to_ns(ktime_t kt) {
    return (int64_t)kt;
}

static inline int64_t ktime_to_us(ktime_t kt) {
    return (int64_t)kt / NSEC_PER_USEC;
}

static inline int64_t ktime_to_ms(ktime_t kt) {
    return (int64_t)kt / NSEC_PER_MSEC;
}

static inline int64_t ktime_to_s(ktime_t kt) {
    return (int64_t)kt / NSEC_PER_SEC;
}

static inline ktime_t ns_to_ktime(uint64_t ns) {
    return (ktime_t)ns;
}

static inline ktime_t us_to_ktime(uint64_t us) {
    return (ktime_t)(us * NSEC_PER_USEC);
}

static inline ktime_t ms_to_ktime(uint64_t ms) {
    return (ktime_t)(ms * NSEC_PER_MSEC);
}

static inline ktime_t ktime_sub_us(ktime_t a, uint64_t us) {
    return a - (ktime_t)(us * NSEC_PER_USEC);
}

static inline ktime_t ktime_us_delta(ktime_t later, ktime_t earlier) {
    return ktime_to_us(ktime_sub(later, earlier));
}

static inline int64_t ktime_ms_delta(ktime_t later, ktime_t earlier) {
    return ktime_to_ms(ktime_sub(later, earlier));
}

#define TK_OFFS_BOOT 0
static inline ktime_t ktime_mono_to_real(ktime_t t) { return t; }
static inline ktime_t ktime_mono_to_any(ktime_t t, int offs) { (void)offs; return t; }

static inline ktime_t ktime_get(void) {
    uint64_t cnt = get_cntpct_el0();
    uint64_t freq = get_cntfrq_el0();

    return (ktime_t)(cnt / (freq / NSEC_PER_SEC));
}

static inline ktime_t ktime_get_mono_fast_ns(void) {
    return ktime_get();
}

/*
 * struct timespec64 / ktime_to_timespec64() -- the calendar split of a
 * ktime_t.  mainline has both in <linux/ktime.h>; they live here because
 * this is where DCL keeps ktime_t and its arithmetic, and drm_vblank.c's
 * timestamp paths (vblank.c:818/1057/1730) reach this header first.  The
 * nanosecond field is `long long` rather than mainline's `long` because
 * this target is LLP64 and `long` is 4 bytes -- tv_nsec only ever holds a
 * sub-second remainder, but a type that is right by coincidence is a trap
 * the first time someone adds tv_nsec arithmetic.
 */
struct timespec64 {
    int64_t tv_sec;
    long long tv_nsec;
};

static inline struct timespec64 ktime_to_timespec64(const ktime_t kt) {
    struct timespec64 ts;
    ts.tv_sec = (int64_t)(kt / NSEC_PER_SEC);
    ts.tv_nsec = (long long)(kt % NSEC_PER_SEC);
    return ts;
}

static inline uint64_t ktime_get_ns(void) {
    return (uint64_t)ktime_get();
}

#if defined(_MSC_VER)
/*
 * This branch was missing its `: <expression>` -- it read
 * `((cond) ? (UARTDebugOut(...)))`, a ternary with no colon, so every use of
 * WARN_ON() was a hard parse error rather than a warning. It never fired in
 * stage 1 because nothing in that stage expanded WARN_ON; tty_buffer.c:210 is
 * the first call site, and the target is `aarch64-unknown-windows`, which
 * defines _MSC_VER -- so this branch, not the clang one below, is the one DCL
 * actually builds with. The comma form also fixes the second problem:
 * UARTDebugOut() returns void (Drivers/uart.h:91), so a plain
 * `cond ? void : 0` would have been a type error even with the colon.
 */
#define WARN_ON(cond) \
     ((cond) ? (UARTDebugOut("[WARN] %s:%d %s \r\n", \
                             __FILE__, __LINE__, #cond), 1) : 0)
#elif defined(__GNUC__) && defined(__clang__)
#define WARN_ON(cond) ({                               \
int __c = !!(cond);                                    \
if (__c){                                              \
UARTDebugOut("[WARN] %s:%d \r\n", __FILE__, __LINE__); \
}                                                      \
})
#endif

/* Mainline's stack-banner helper.  DCL's input core calls it only on an
 * invalid event type; there is no unwinder, so the sink is silent. */
void dump_stack(void);

#define ARRAY_SIZE(a)   (sizeof(a) / sizeof((a)[0]))

#ifndef DIV_ROUND_UP
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#endif
#define ALIGN(x, a)     __ALIGN_KERNEL((x), (a))
#define __ALIGN_KERNEL(x, a) __ALIGN_KERNEL_MASK(x, (typeof(x))(a) - 1)
#define __ALIGN_KERNEL_MASK(x, mask) (((x) + (mask)) & ~(mask))
#define MIN(a, b)       ((a) < (b) ? (a) : (b))
#define MAX(a, b)       ((a) > (b) ? (a) : (b))
#define clamp(val, lo, hi) ((val) < (lo) ? (lo) : ((val) > (hi) ? (hi) : (val)))
#define BUILD_BUG_ON(cond) ((void)sizeof(char[1 - 2 * !!(cond)]))
#define upper_32_bits(n) ((uint32_t)((n) >> 32))
#define lower_32_bits(n) ((uint32_t)((n) & 0xFFFFFFFF))

/*
 * ------------------------------------------------------------------------
 * Access and barrier primitives (added for the tty/serial port)
 * ------------------------------------------------------------------------
 *
 * READ_ONCE/WRITE_ONCE are the "one access, no compiler cleverness" pair.
 * The tty code needs them structurally rather than for performance:
 * tty_buffer.c:64 reads buf->flip_wq with READ_ONCE() before handing it to a
 * work function, and the acquire/release forms around commit/next (lines 489,
 * 522, 290, 296) are what stop the producer's store of the byte *before* the
 * cursor from being observed after the cursor itself. On a uniprocessor with
 * no compiler reordering across a volatile access that would already hold --
 * but volatile is not an ordering primitive, so these are built on __atomic
 * with the right ordering instead of on volatile casts. The distinction costs
 * nothing and keeps the header honest if DCL ever runs on more than one core.
 *
 * smp_load_acquire/smp_store_release take pointers of any type, which is why
 * they are macros over __atomic_load_n/__atomic_store_n rather than
 * typed inlines: they are called on both unsigned int* (head->commit) and
 * struct tty_buffer** (head->next).
 */
#ifndef READ_ONCE
#define READ_ONCE(x)		__atomic_load_n(&(x), __ATOMIC_RELAXED)
#endif
#ifndef WRITE_ONCE
#define WRITE_ONCE(x, val)	__atomic_store_n(&(x), (val), __ATOMIC_RELAXED)
#endif
#ifndef smp_load_acquire
#define smp_load_acquire(p)		__atomic_load_n((p), __ATOMIC_ACQUIRE)
#endif
#ifndef smp_store_release
#define smp_store_release(p, v)		__atomic_store_n((p), (v), __ATOMIC_RELEASE)
#endif
#ifndef smp_mb
#define smp_mb()	__atomic_thread_fence(__ATOMIC_SEQ_CST)
#endif
#ifndef smp_rmb
#define smp_rmb()	__atomic_thread_fence(__ATOMIC_ACQUIRE)
#endif
#ifndef smp_wmb
#define smp_wmb()	__atomic_thread_fence(__ATOMIC_RELEASE)
#endif

/*
 * WARN(cond, fmt, ...) -- mainline's "report and continue" that is *not*
 * counted as an error (unlike BUG). tty_buffer.c:150 uses it to say a memory
 * accounting mismatch survived tty_buffer_free_all(); the allocation was
 * already freed, so the only useful response is to say so and carry on.
 *
 * WARN_ON exists above (both the MSVC and clang spellings). This adds the
 * printf form and WARN_ON_ONCE, which is the form used on paths that can run
 * once per received byte -- printing per byte would be worse than silence.
 */
#ifndef WARN
#define WARN(cond, fmt, ...)						\
	({								\
		int __wret = !!(cond);					\
		if (__wret)						\
			UARTDebugOut("[WARN] " fmt " (%s:%d)\r\n",	\
				##__VA_ARGS__, __FILE__, __LINE__);	\
		__wret;							\
	})
#endif

#ifndef WARN_ON_ONCE
#define WARN_ON_ONCE(cond)						\
	({								\
		static int __warned_once;				\
		int __wret = !!(cond);					\
		if (__wret && !__warned_once) {				\
			__warned_once = 1;				\
			UARTDebugOut("[WARN] %s:%d\r\n",		\
				__FILE__, __LINE__);			\
		}							\
		__wret;							\
	})
#endif

/*
 * __randomize_layout -- a GCC plugin attribute that shuffles struct field
 * order per build. DCL has no plugin and no threat model that needs one; the
 * ported tty structs simply drop it. Defined as nothing so a file that
 * carries it still parses.
 */
#ifndef __randomize_layout
#define __randomize_layout
#endif


/*
 * xchg() -- atomic exchange.
 *
 * Not a DCL invention: __atomic_exchange_n with sequential consistency is the
 * same guarantee mainline's arch-neutral xchg() asks for, and this target's
 * clang lowers it to a single ldaxr/stlxr pair rather than a lock.  Two call
 * sites, both in 8250_port.c, both swapping a *pointer* (the active RS485
 * timer under the port lock), which is the case where an int-sized
 * "exchange" would have been silent: the write would land with a truncated
 * address.
 */
#define xchg(ptr, v) __atomic_exchange_n((ptr), (v), __ATOMIC_SEQ_CST)

/*
 * oops_in_progress -- nonzero while the kernel is in a fault handler.
 *
 * mainline declares it in <linux/oops.h>; DCL hangs it off printk.h because
 * that is who reads it: serial8250_console_write() at 8250_port.c:3341 takes
 * the port lock only when this is clear, on the reasoning that during a fault
 * the lock may already be held by the CPU that is reporting, and taking it
 * then would deadlock *inside* the crash dump.
 *
 * It is a real variable rather than `#define oops_in_progress 0` for the same
 * reason: panic() below sets it, and a macro could not be.  Defined in
 * DCL/linux_irq_shim.c, next to the console registry it guards.
 */
extern int oops_in_progress;

/* kstrtou8 and friends -- see that header for why they are hand-rolled. */
#include <linux/kstrtox.h>

/*
 * BUG() / BUG_ON() -- mainline spells these in <linux/bug.h>, which
 * serial_core.c does not include; <linux/kernel.h> is where its own copies
 * live upstream too (via linux/bug.h), and it is the header every file in
 * the tree reaches, so it is where these go.
 *
 * DCL has no die(), no exception table and no oops-then-continue: BUG() is
 * panic(), which sets oops_in_progress, prints and stops.  That is the same
 * observable behaviour mainline gives a BUG() -- the machine does not carry
 * on with a corrupted invariant -- and the two call sites agree that it
 * should stop:
 *
 *   serial_core.c:120   BUG_ON(!state)      uart_write_wakeup() with no
 *                                           state means the tty layer has a
 *                                           port that was never set up; the
 *                                           next line dereferences it.
 *   serial_core.c:2731  BUG_ON(drv->state)  registering the same uart_driver
 *                                           twice would double every list.
 *
 * The alternative -- a no-op, which is what mainline compiles BUG_ON down to
 * when CONFIG_BUG=n -- would turn both into a null dereference a few
 * instructions later, with a fault instead of a message naming the line that
 * fired.
 */
#define BUG() \
	panic("BUG: failure at %s:%d/%s()!\n", __FILE__, __LINE__, __func__)

#define BUG_ON(condition) do { if (condition) { BUG(); } } while (0)

/*
 * sized_strscpy(dst, src, count) -- copy a NUL-terminated string into a
 * bounded buffer.  The result is discarded by every caller in the tree, but
 * the contract still has to be mainline's, because it is the *return* that
 * tells a future caller whether the string fit:
 *
 *   >= 0   bytes copied, excluding the terminator
 *   -E2BIG src did not fit in count bytes; dst is still NUL-terminated
 *
 * Byte loop rather than memcpy, and deliberately so: the terminator is
 * written as part of the copy, so there is no window in which dst holds an
 * unterminated run -- which is the whole point of strscpy against strncpy,
 * whose mainline comment says the same thing in more words.  count == 0 is
 * -E2BIG because nothing could be stored, matching upstream.
 *
 * The name is mainline's for this spelling: mainline v7.2 has the 3-argument
 * form here and builds strscpy() on top of it -- see the macros below.
 *
 * mainline: include/linux/string.h -> lib/string.c; see the note above this
 * block for why the definition sits in this file instead.
 */
static inline long sized_strscpy(char* dst, const char* src, size_t count)
{
	size_t i;

	if (count == 0)
		return -E2BIG;

	for (i = 0; i + 1 < count && src[i]; i++)
		dst[i] = src[i];

	if (src[i])
	{
		dst[0] = '\0';
		return -E2BIG;
	}

	dst[i] = '\0';
	return (long)i;
}

/*
 * strscpy(dst, src[, count]) -- mainline's two-form spelling of the above.
 *
 * mainline dispatches on the argument count rather than overloading, because
 * C cannot overload and this tree has both shapes in it at once:
 *
 *   virtio_console.c:1635   strscpy(port->name, ..., sizeof(port->name));
 *   serial_core.c:2485      strscpy(address, "*unknown*", sizeof(address));
 *   drm_client.c:253        strscpy(fb->comm, client->name, TASK_COMM_LEN);
 *   drm_modes.c:2322        strscpy(cmdline_mode->name, mode->name,
 *                                   sizeof(cmdline_mode->name));
 *   drm_framebuffer.c:886   strscpy(fb->comm, current->comm);
 *
 * The last line is what made this necessary. Against the plain 3-argument
 * function this file used to carry, it reported "too few arguments to
 * function call, expected 3, have 2" -- and adding a default parameter was
 * not the fix, because mainline's rule is that the 2-argument form is only
 * valid when dst *is* an array whose size is known, and the count then comes
 * from sizeof(dst). The four 3-argument sites pass a count that is not the
 * whole array (a TASK_COMM_LEN into a longer field), which is the other half
 * of why the argument cannot simply be dropped.
 *
 * The dispatch is mainline's in shape and different in mechanism, and the
 * difference is worth recording: mainline selects with COUNT_ARGS() over a
 * padded argument list, which needs the __ARG_PLACEHOLDER machinery for the
 * empty case. Checked rather than assumed, clang counts an empty __VA_ARGS__
 * as *one* empty argument, so the padded-list version selects the 3-argument
 * arm for a 2-argument call and then complains that its size argument is
 * missing.
 *
 * __VA_OPT__ is direct about the same question. __strscpy_sel returns its
 * middle argument; the first __VA_OPT__ supplies "the count, then the
 * 3-argument arm" only when a count was actually written, and when it was
 * not, the slot that __VA_OPT__ left empty is precisely the middle one --
 * which lands on __strscpy_2. __strscpy_2 is variadic so the trailing empty
 * __VA_ARGS__ that strscpy() passes has somewhere to go.
 *
 * Both arms are always parsed, so dst is sizeof'd even on the 3-argument
 * path; that is harmless and is the price of picking the arm after expansion
 * rather than before.
 */
#define __strscpy_2(dst, src, ...)	sized_strscpy(dst, src, sizeof(dst))
#define __strscpy_3(dst, src, size)	sized_strscpy(dst, src, size)
#define __strscpy_sel(_1, N, ...)	N
#define __strscpy_args(...)		__strscpy_sel(__VA_OPT__(__VA_ARGS__, __strscpy_3) , __strscpy_2)
#define strscpy(dst, src, ...)		__strscpy_args(__VA_ARGS__)(dst, src, __VA_ARGS__)
#endif

