#ifndef __LINUX_FCNTL_H__
#define __LINUX_FCNTL_H__

/*
 * DCL <linux/fcntl.h> -- open(2) flags and the F_* commands.
 *
 * Ported from mainline's uapi/asm-generic/fcntl.h (what arm64 uses), values
 * verbatim. The header exists because <linux/tty.h> tests file->f_flags:
 * tty_io_nonblock() is `file->f_flags & O_NONBLOCK || ...`, and an undefined
 * O_NONBLOCK would turn into an implicit declaration of an identifier the
 * preprocessor never saw -- it would not be "always false", it would not
 * compile.
 *
 * ---------------------------------------------------------------------------
 * KNOWN DIVERGENCE, recorded here because the day it matters will be the day
 * someone opens /dev/ttyS0 with O_NONBLOCK from userspace.
 *
 * Xeneva's userspace libc (Libs/XEClib/includes/fcntl.h) assigns a different
 * bit layout entirely:
 *
 *     userspace (XEClib)      this header (asm-generic)
 *       O_NONBLOCK  0x0080       O_NONBLOCK  0x0800  (1 << 11)
 *       O_NOCTTY    0x0004       O_NOCTTY    0x0100  (1 << 8)
 *       O_RDWR      0x0800       O_RDWR      0x0002
 *       F_SETFL     0x0010       F_SETFL     0x0004
 *
 * This header follows mainline for two reasons: every ported file is written
 * against mainline's numbers, and DCL's own cdev bridge already picked one --
 * linux_cdev_shim.c:507 defines DCL_O_NONBLOCK as 0x800 with the comment
 * "asm-generic fcntl.h: 00004000". Changing it now would silently change
 * behaviour that already works.
 *
 * It is safe *today* because nothing yet carries a userspace flag into
 * f_flags: there is no `f_flags =` assignment anywhere in the tree (the only
 * writer is that cdev selftest, which ORs the value in by hand), so every
 * open file reads as flags == 0 and the tty layer behaves blocking, which is
 * the correct default. The reconciliation -- most likely teaching XEClib the
 * Linux layout, since ported code assumes it -- belongs with the stage that
 * first wires a real open() through to tty_open(). It is listed in the
 * tracker rather than resolved here, because guessing which side should move
 * without a working open path would be exactly that: a guess.
 * ---------------------------------------------------------------------------
 */

#define O_ACCMODE	3
#define O_RDONLY	0
#define O_WRONLY	(1 << 0)
#define O_RDWR		(1 << 1)
#ifndef O_CREAT
#define O_CREAT		(1 << 6)	/* not fcntl */
#endif
#ifndef O_EXCL
#define O_EXCL		(1 << 7)	/* not fcntl */
#endif
#ifndef O_NOCTTY
#define O_NOCTTY	(1 << 8)	/* not fcntl */
#endif
#ifndef O_TRUNC
#define O_TRUNC		(1 << 9)	/* not fcntl */
#endif
#ifndef O_APPEND
#define O_APPEND	(1 << 10)
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK	(1 << 11)
#endif
#ifndef O_DSYNC
#define O_DSYNC		(1 << 12)
#endif
#ifndef FASYNC
#define FASYNC		(1 << 13)	/* fcntl, for BSD compatibility */
#endif
#ifndef O_DIRECT
#define O_DIRECT	(1 << 14)
#endif
#ifndef O_LARGEFILE
#define O_LARGEFILE	(1 << 15)
#endif
#ifndef O_DIRECTORY
#define O_DIRECTORY	(1 << 16)
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW	(1 << 17)
#endif
#ifndef O_NOATIME
#define O_NOATIME	(1 << 18)
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC	(1 << 19)
#endif
#ifndef O_SYNC
#define __O_SYNC	(1 << 20)
#define O_SYNC		(__O_SYNC | O_DSYNC)
#endif
#ifndef O_PATH
#define O_PATH		(1 << 21)
#endif
#ifndef __O_TMPFILE
#define __O_TMPFILE	(1 << 22)
#endif
#ifndef O_EMPTYPATH
#define O_EMPTYPATH	(1 << 26)
#endif

#define O_TMPFILE (__O_TMPFILE | O_DIRECTORY)

#ifndef O_NDELAY
#define O_NDELAY	O_NONBLOCK
#endif

#define F_DUPFD		0	/* dup */
#define F_GETFD		1	/* get close_on_exec */
#define F_SETFD		2	/* set/clear close_on_exec */
#define F_GETFL		3	/* get file->f_flags */
#define F_SETFL		4	/* set file->f_flags */
#ifndef F_GETLK
#define F_GETLK		5
#define F_SETLK		6
#define F_SETLKW	7
#endif
#ifndef F_SETOWN
#define F_SETOWN	8	/* for sockets. */
#define F_GETOWN	9	/* for sockets. */
#endif
#ifndef F_SETSIG
#define F_SETSIG	10	/* for sockets. */
#define F_GETSIG	11	/* for sockets. */
#endif
#ifndef F_GETLK64
#define F_GETLK64	12
#define F_SETLK64	13
#define F_SETLKW64	14
#endif
#ifndef F_SETOWN_EX
#define F_SETOWN_EX	15
#define F_GETOWN_EX	16
#endif
#ifndef F_GETOWNER_UIDS
#define F_GETOWNER_UIDS	17
#endif

#define FD_CLOEXEC	1	/* actually anything with low bit set goes */

#ifndef F_RDLCK
#define F_RDLCK		0
#define F_WRLCK		1
#define F_UNLCK		2
#endif

#define LOCK_SH		1	/* shared lock */
#define LOCK_EX		2	/* exclusive lock */
#define LOCK_NB		4	/* or'd with one of the above to prevent
				 * blocking */
#define LOCK_UN		8	/* remove lock */

#define F_LINUX_SPECIFIC_BASE	1024

/*
 * Deliberately *not* <linux/types.h>, which mainline's fcntl.h does include.
 *
 * mainline needs it because the full uapi surface carries types: struct
 * flock, struct f_owner_ex, flock64, struct falloc and the __kernel_* types
 * they are built from. DCL's port is every #define above and nothing else --
 * grep for a non-# line in this file and there are none -- so types.h was
 * carrying nothing.
 *
 * It was carrying something, though, and it is worth recording why it is
 * gone: types.h's only include is <linux/kernel.h> (types.h:21), which is the
 * aggregator -- virtio.h, completion.h, wait.h, sched.h. So a header of
 * numeric constants was reaching <linux/sched.h>, which `#undef current` and
 * redefines the word as a macro. struct __VFS_NODE__ (BaseHdr/Fs/vfs.h:105)
 * has a `uint64_t current` member, so a file that includes <Fs/vfs.h> after
 * anything which touched this header came out with "field declared as a
 * function" pointing at that member. linux_cdev_shim.c hit exactly that: it
 * includes <linux/file.h> at line 9 and <Fs/vfs.h> at line 11, and file.h
 * owes this header because mainline's fs.h includes it.
 *
 * If anyone adds struct flock or any other type here, <linux/types.h> comes
 * back with it, and the collision comes back with that. The way round it
 * then is to include <linux/fcntl.h> before <Fs/vfs.h>, not to make the
 * member go away -- Xeneva's VFS layout is not this tree's to move.
 */

#endif /* __LINUX_FCNTL_H__ */
