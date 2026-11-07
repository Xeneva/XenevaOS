#ifndef __LINUX_TYPES_H__
#define __LINUX_TYPES_H__

/*
 * DCL <linux/types.h> -- the fixed-width typedefs a ported file starts with.
 *
 * Everything mainline puts here already exists in <linux/kernel.h> (u8 through
 * u64, s8 through s64, __u8 through __u64, phys_addr_t, gfp_t), so this header
 * is a re-export: it exists because a ported file opens with
 * `#include <linux/types.h>` and the include has to resolve.
 *
 * It also pulls in <stdbool.h> for `bool`/`true`/`false`, which the tty_buffer
 * struct fields (`bool flags`) and every `if (likely(...))` in the set need.
 * Xeneva's stdbool.h defines `bool` as _Bool (stdbool.h:35).
 *
 * Deliberately does *not* re-typedef anything: two typedefs of the same name
 * are only legal in C11 when they are identical, and "identical" here would
 * mean byte-for-byte matching an expression from a header we do not own.
 */

/*
 * __kernel_long_t / __kernel_ulong_t -- <uapi/linux/sysinfo.h>'s struct
 * sysinfo hangs every field on them, and that header arrives through
 * uapi/linux/kernel.h:5, which linux/math.h:7 includes, which kernel.h:90
 * includes.
 *
 * They sit *above* the include of kernel.h, and that placement is the whole
 * point: when the chain above runs, sysinfo.h includes <linux/types.h> from
 * inside kernel.h -- by then this header's guard is already set, so the
 * re-entry is skipped and anything defined *below* the kernel.h include does
 * not exist yet.  The first build with the kernel.h -> math.h edge failed at
 * sysinfo.h:9 with "unknown type name '__kernel_long_t'" for exactly that
 * reason.  These two are self-contained -- no size_t, nothing from kernel.h --
 * so they can go first; the rest of the file cannot.
 *
 * The fixed-width spelling rather than mainline's `long`/`unsigned long` is
 * about the target: aarch64-unknown-windows is LLP64, so `long` is 32 bits
 * here where Linux/arm64's is 64 (verified: sizeof(long) == 4).  Following
 * mainline's spelling would silently shrink struct sysinfo from 104 bytes to
 * 64 and turn the `_f[20 - 2*sizeof(__kernel_ulong_t) - sizeof(__u32)]`
 * trailer into 8 bytes of padding where Linux has none.  Nothing in DCL
 * implements sysinfo() or ships the struct over an ABI boundary today, but a
 * uapi struct whose size nobody has checked is exactly the thing that gets
 * found out by the first syscall that does.
 */
typedef long long __kernel_long_t;
typedef unsigned long long __kernel_ulong_t;

#include <linux/kernel.h>
#include <stdbool.h>

/*
 * pid_t -- mainline has it at include/linux/types.h:26 over __kernel_pid_t,
 * which is `int` on this ABI.  DCL's <uapi/linux/posix_types.h> does not
 * carry __kernel_pid_t (nothing in userspace names it), so the typedef is
 * written directly.  First consumer: drm_device.h:46, the wedge task info
 * the panic path fills in.
 */
typedef int pid_t;

/*
 * __kernel_size_t -- the kernel-side spelling of size_t, which uapi/drm/drm.h
 * hangs three struct drm_version length fields on (drm.h:127/129/139).  It is
 * <asm/posix_types.h>'s job on mainline; DCL has no posix_types, and this
 * header is what drm.h's __KERNEL__ branch includes, so the typedef lives
 * here.  size_t rather than `unsigned long`: aarch64-unknown-windows is LLP64
 * (long is 32 bits), and the userspace half of drm.h -- which takes the BSD
 * arm and writes `typedef size_t __kernel_size_t;` itself -- would otherwise
 * disagree about the width of name_len/date_len/desc_len across the ioctl.
 */
typedef size_t __kernel_size_t;

/*
 * uint -- mainline keeps it in <linux/types.h> too (:93, in the sysv block
 * beside ushort/ulong), and drm/drm_color_mgmt.h:72 and :74 name it in
 * drm_crtc_enable_color_mgmt()'s prototype.  C has no `uint`; without the
 * typedef the declaration fails at the parameter name with "unknown type
 * name 'uint'", three lines before the function it belongs to.
 */
typedef unsigned int		uint;

/*
 * The four sort() callback types.  mainline keeps them in <linux/types.h>
 * (:261-:265) rather than in <linux/sort.h>, which only ever mentions them in
 * prototypes -- so a TU that reads sort.h first needs them here.  Vendored
 * lib/sort.c names all four, and without them the header fails at every one
 * of its four declarations with "unknown type name".
 */
typedef void (*swap_r_func_t)(void *a, void *b, int size, const void *priv);
typedef void (*swap_func_t)(void *a, void *b, int size);
typedef int (*cmp_r_func_t)(const void *a, const void *b, const void *priv);
typedef int (*cmp_func_t)(const void *a, const void *b);

/*
 * DCL delta -- byte order.
 *
 * mainline reaches le16_to_cpu() and friends through its byteorder layer
 * (<linux/byteorder/generic.h> via <asm/byteorder.h>), which comes in with the
 * rest of the driver's own include set.  DCL has no byteorder layer -- nothing
 * needed one before this port -- so the twelve conversions are defined here,
 * which is the first header <linux/virtio_byteorder.h> includes (line 6).
 *
 * __LITTLE_ENDIAN is the one that is not merely convenience: clang does *not*
 * predefined it for aarch64-unknown-windows (only __BYTE_ORDER__ comes from
 * the compiler, verified), and virtio_byteorder.h:9 tests __LITTLE_ENDIAN to
 * decide whether the legacy virtio device layout is little-endian.  Taken at
 * face value that reads "big-endian", which would run every virtio16/virtio32
 * field through bswap on a little-endian target and hand the device garbage.
 * The 1234 matches mainline's.
 *
 * aarch64 is little-endian, so the le_* forms are identity and the be_* forms
 * bswap -- which is exactly mainline's little_endian.h, written out rather
 * than fetched, because pulling in its arch indirection would reintroduce the
 * <asm/byteorder.h> hop this tree has no file for.
 *
 * __BIG_ENDIAN is deliberately absent, and that is not a sibling omission of
 * the paragraph above.  mainline defines exactly one of the pair -- <linux/
 * kconfig.h> chooses from CONFIG_CPU_BIG_ENDIAN -- and four vendored files
 * read *presence* as the endianness test: drm_fourcc.h:42 (the DRM_FORMAT_HOST_*
 * aliases), drm_fourcc.c:221, drm_framebuffer.c:357 and seq_buf.c:278.  Both
 * macros published means all four take the big-endian arm here, so every
 * scanout format gets stamped with DRM_FORMAT_BIG_ENDIAN and the seq_buf hex
 * dumps come out byte-reversed -- precisely the failure the Stage 3
 * writeback pixel-compare is there to catch, but a month earlier.
 */
#define __LITTLE_ENDIAN		1234
#define __PDP_ENDIAN		3412

#define le16_to_cpu(x)		((__u16)(x))
#define cpu_to_le16(x)		((__le16)(__u16)(x))
#define le32_to_cpu(x)		((__u32)(x))
#define cpu_to_le32(x)		((__le32)(__u32)(x))
#define le64_to_cpu(x)		((__u64)(x))
#define cpu_to_le64(x)		((__le64)(__u64)(x))

#define be16_to_cpu(x)		((__u16)__builtin_bswap16((unsigned short)(x)))
#define cpu_to_be16(x)		((__be16)__builtin_bswap16((unsigned short)(x)))
#define be32_to_cpu(x)		((__u32)__builtin_bswap32((unsigned int)(x)))
#define cpu_to_be32(x)		((__be32)__builtin_bswap32((unsigned int)(x)))
#define be64_to_cpu(x)		((__u64)__builtin_bswap64((unsigned long long)(x)))
#define cpu_to_be64(x)		((__be64)__builtin_bswap64((unsigned long long)(x)))

#endif /* __LINUX_TYPES_H__ */
