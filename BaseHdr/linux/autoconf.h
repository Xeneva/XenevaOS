#ifndef __LINUX_AUTOCONF_H__
#define __LINUX_AUTOCONF_H__

/*
 * DCL stand-in for <autoconf.h>: the CONFIG_* surface mainline drivers read.
 *
 * Only what an in-tree DCL port actually consumes gets defined here; an
 * undefined CONFIG_* makes the corresponding mainline #ifdef arm drop out,
 * which is how /dev/port (x86-only), STRICT_DEVMEM and THP stay out of the
 * build without editing the vendored source.
 */

#define CONFIG_MMU 1		/* else mem.c compiles the NOMMU mmap arms */
#define CONFIG_PRINTK 1	/* mem.c's /dev/kmsg (kmsg_fops) arm */
#define CONFIG_DEVMEM 1	/* /dev/mem (mem_fops) arm */

/*
 * mem.c's own valid_phys_addr_range() falls back to __pa(high_memory), which
 * DCL does not model. Claiming the arch hook (mainline:
 * arch/arm64/include/asm/io.h) hands the job to linux/mm.h's declaration and
 * the body in DCL/linux_mm_shim.c instead.
 */
#define ARCH_HAS_VALID_PHYS_ADDR_RANGE 1

/*
 * CONFIG_DRM_CLIENT -- on, because the client half of DRM is in this image:
 * Vendored/drivers/gpu/drm/drm_client.c and drm_client_event.c are both
 * compiled, so there are real bodies for drm_client_dev_hotplug(), _restore(),
 * _suspend(), _resume(), _unregister() and drm_client_debugfs_init().
 *
 * Leaving it undefined takes the #else arm of <drm/drm_client_event.h>:16 and
 * of drm_internal.h:52, which answers those six names with empty *static
 * inline* definitions.  The .c file then defines them at file scope and clang
 * reports each one as a redefinition of a previous definition -- five errors
 * at drm_client_event.c:32/:86/:105/:143/:176 pointing back at
 * drm_client_event.h:17.  A guard whose off-arm collides with the source it
 * is guarding is the wrong polarity: the option is what makes the header
 * agree with the file.
 *
 * It gates nothing else -- the only other reader in the tree is
 * drm_internal.h:60, paired with CONFIG_MAGIC_SYSRQ, which stays undefined
 * so the sysrq half keeps its no-op stubs.
 */
#define CONFIG_DRM_CLIENT 1

/*
 * Stage 3 (tty/serial): the two arms 8250_port.c reads with IS_ENABLED().
 *
 * CONFIG_SERIAL_8250_CONSOLE is the reason this stage exists -- it gates
 * 8250_port.c:3197 `port->has_sysrq = IS_ENABLED(CONFIG_SERIAL_8250_CONSOLE)`
 * and, more importantly, the #ifdef at :3241 that registers 8250 as a
 * console, so leaving it undefined would build every file, run every probe,
 * and still print nothing. IS_ENABLED() needs it *defined* (to 1), not just
 * present: an undefined identifier reads as 0, which is the wrong answer.
 *
 * CONFIG_SERIAL_8250_16550A_VARIANTS gates the extra divisor steps in
 * serial8250_do_set_termios (921). It is on because the hardware this is for
 * -- iMX8MP and RPi3b+ UARTs -- is 16550A-compatible, and a wrong divisor
 * table produces a port that transmits at the wrong baud rather than one that
 * fails to build.
 */

#define CONFIG_SERIAL_8250_CONSOLE 1
#define CONFIG_SERIAL_8250_16550A_VARIANTS 1

/*
 * CONFIG_CPU_BIG_ENDIAN -- deliberately left *undefined*, which is not a
 * placeholder but the answer: iMX8MP, RPi3b+ and QEMU virt are all
 * little-endian, and the two places it is read say so either way now that
 * <linux/kconfig.h> is mainline's token paste:
 *
 *     *iotype = IS_ENABLED(CONFIG_CPU_BIG_ENDIAN) ? UPIO_MEM32BE : UPIO_MEM32;
 * (serial_core.c:2120, false -> UPIO_MEM32, the byte order those UARTs are
 * wired in)
 *
 * and kconfig.h's own byte-order block, `#ifdef CONFIG_CPU_BIG_ENDIAN`.
 *
 * The second read is why this cannot be defined-to-0.  An option present at
 * 0 is still present to an #ifdef, so a defined-but-false CONFIG_CPU_BIG_
 * ENDIAN would take that arm, publish __BIG_ENDIAN -- and then drm_fourcc.h:42,
 * drm_fourcc.c:221, drm_framebuffer.c:357 and seq_buf.c:278, all of which
 * test `#ifdef __BIG_ENDIAN` to pick the host byte order, would stamp every
 * little-endian framebuffer format with DRM_FORMAT_BIG_ENDIAN and hexdump
 * buffers byte-reversed.  The same guard #warns from byteorder/
 * little_endian.h:7.  Undefined reads correctly in both places.
 *
 * Defined-to-0 used to be required because IS_ENABLED was `!!(opt)`, an
 * expression that evaluates its argument; mainline's is token pasting that
 * never touches the identifier, so absent is a plain 0 in a #if and in a
 * function body alike.  With that gone, every option in this file follows
 * mainline's rule: present only when on.
 */

/*
 * CONFIG_LDISC_AUTOLOAD -- undefined, i.e. off, read by exactly one line,
 * tty_ldisc.c:119, `int tty_ldisc_autoload = IS_BUILTIN(CONFIG_LDISC_AUTOLOAD)`,
 * which is the flag tty_ldisc_get() consults before it calls
 * request_module("tty-ldisc-%d") (:154).
 *
 * 0 is the truth and not a default: DCL's loader (DCL/module_loader.c) runs
 * the initcalls baked into this image and has no search path to ask for a
 * module by name, so request_module() here returns -ENOSYS and the second
 * get_ldops() attempt after it fails.  The gate above it reads
 * `!capable(CAP_SYS_MODULE) && !tty_ldisc_autoload`, capable() answers yes,
 * and the `&&` short-circuits -- so the -EPERM arm is skipped, the no-op
 * request runs, and the caller gets a plain "no such discipline" back.  That
 * is the correct outcome; defining it 1 would promise a lookup that cannot
 * happen.
 *
 * It used to be spelled `#define CONFIG_LDISC_AUTOLOAD 0` purely so that
 * IS_BUILTIN's `!!(opt)` could evaluate it.  IS_BUILTIN is now mainline's
 * token paste, which never names the option, so absent gives the same 0 --
 * and leaves it invisible to an #ifdef, which is what the rest of this file
 * wants.
 */

/*
 * CONFIG_SERIAL_CORE_CONSOLE -- serial_core.c's own console half, and the
 * reason it is *on* rather than left to the else-branch is a link error, not
 * a preference: the block at serial_core.c:2063 `#if defined(CONFIG_SERIAL_CORE_CONSOLE)
 * || defined(CONFIG_CONSOLE_POLL)` is where uart_parse_options() and
 * uart_set_options() are defined, and 8250_port.c calls both from its
 * console setup path (which is compiled, because CONFIG_SERIAL_8250_CONSOLE
 * above is on).  Leaving this off gave `undefined symbol: uart_set_options`
 * at link -- the guard had compiled the caller and dropped the callee.
 *
 * CONFIG_CONSOLE_POLL stays undefined: uart_poll_init() and the polled
 * console helpers at :2586/:2697 are for consoles that poll instead of take
 * interrupts, and DCL's console is interrupt-driven.  That the two guards
 * share one `#if` is why serial_core.c compiles with console output enabled
 * even though only one of the two conditions is true.
 */
#define CONFIG_SERIAL_CORE_CONSOLE 1

/*
 * CONFIG_REMOTEPROC -- off, and so undefined, which is the mainline
 * spelling (unset options are simply left out of autoconf.h).
 *
 * Nothing here has a remoteproc subsystem: the rproc serial transport is the
 * path that allocates its buffers through dma_alloc_coherent() and hangs them
 * off vdev->dev.parent, and with this off every use of is_rproc_serial()
 * folds out -- including the vdev->id.device comparison beside it, which is
 * why vdev->id can stay zeroed.  Both reads become 0:
 * `is_rproc_enabled` in the return at virtio_console.c:332, and the
 * `#if IS_ENABLED(CONFIG_REMOTEPROC)` at :2087.
 *
 * It was `#define CONFIG_REMOTEPROC 0` before, because IS_ENABLED was
 * `!!(opt)`: a C expression, so an undefined option there would have been an
 * undeclared identifier in a function body rather than a silent 0.  The
 * comment used to say mainline's own #if wrapper was what was missing and
 * that this line supplied its effect at the source -- that wrapper is now
 * what <linux/kconfig.h> provides, so the line can go too.
 */

/*
 * lib/xarray: multi-index entries and the chunk layout.  Both defaults are
 * mainline's (xarray: default y, BASE_SMALL default n).  Only XARRAY_MULTI
 * is defined, because only it is *on*: xarray.h:1622 reads it, and it keeps
 * marks[] in struct xa_node, which xas_set_mark() at :1745 indexes without a
 * guard of its own.  BASE_SMALL is off and so absent -- XA_CHUNK_SHIFT at
 * :1154 is `IS_ENABLED(CONFIG_BASE_SMALL) ? 4 : 6`, which is 6 either way,
 * now that IS_ENABLED accepts an undefined option.
 */
#define CONFIG_XARRAY_MULTI	1

/*
 * CONFIG_DRM_VIRTIO_GPU_KMS -- on, and it has to be: virtio_gpu_init()
 * (Vendored/drivers/gpu/drm/virtio/virtgpu_kms.c:240) gates the modeset half
 * on it with IS_ENABLED(), and its "#else" clears DRIVER_MODESET and
 * DRIVER_ATOMIC from the driver features and writes num_scanouts = 0. A
 * virtio-gpu device without KMS is a driver that registers and then has no
 * output to prove anything on -- the scanout stage exists to prove exactly
 * that half, so the option is the feature table of this port, not a
 * preference. It gates nothing else in the tree; the driver is the only
 * reader.
 */
#define CONFIG_DRM_VIRTIO_GPU_KMS 1

/* Deliberately undefined:
 *   CONFIG_DEVPORT           no x86 I/O ports on ARM64 -> no /dev/port
 *   CONFIG_STRICT_DEVMEM     page_is_allowed() collapses to "allow all"
 *   CONFIG_HAVE_IOREMAP_PROT mmap_mem_ops stays empty (no generic_access_phys)
 *   CONFIG_TRANSPARENT_HUGEPAGE get_unmapped_area_zero uses mm_get_unmapped_area
 *   CONFIG_SECURITY          struct security does not exist; security_locked_down()
 *                            is still provided by DCL/linux_mm_shim.c
 */

#endif /* __LINUX_AUTOCONF_H__ */
