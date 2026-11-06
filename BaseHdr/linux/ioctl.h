#ifndef __LINUX_IOCTL_H__
#define __LINUX_IOCTL_H__

/*
 * DCL <linux/ioctl.h> -- the _IOC encoding, from uapi/asm-generic/ioctl.h.
 *
 * WHY THIS EXISTS ALONGSIDE BaseHdr/Fs/tty.h
 * ------------------------------------------
 * Xeneva's own devfs already has an ioctl vocabulary (Fs/tty.h: TIOCGWINSZ
 * 0x5401 and friends), written as bare numbers because that is what Xeneva's
 * dispatch compares against. The mainline tty stack does not work that way:
 * several of its request numbers are *computed* from a direction/size/type
 * tuple, and serial_core.c compares against two of them --
 *
 *     case TIOCSISO7816:  case TIOCGISO7816:      (serial_core.c:1601,1605)
 *
 * -- which are _IOWR('T', 0x43, struct serial_iso7816) and
 * _IOR('T', 0x42, struct serial_iso7816), i.e. they cannot be written as
 * plain integers without baking in sizeof(struct serial_iso7816).
 *
 * So there are two encodings in the tree for now. That is a real
 * inconsistency, not an oversight, and it is recorded as an open question in
 * DCL_TODO.md for the stage that brings tty_io.c's dispatch over: whoever
 * wires request routing has to decide which encoding userspace speaks. Until
 * then this header is mainline's, unmodified in layout, so nothing that
 * depends on the encoding is quietly wrong.
 *
 *   upstream  include/uapi/asm-generic/ioctl.h  (mainline v7.2)
 */

#define _IOC_NRBITS	8
#define _IOC_TYPEBITS	8
#define _IOC_SIZEBITS	14
#define _IOC_DIRBITS	2

#define _IOC_NRSHIFT	0
#define _IOC_TYPESHIFT	(_IOC_NRSHIFT + _IOC_NRBITS)
#define _IOC_SIZESHIFT	(_IOC_TYPESHIFT + _IOC_TYPEBITS)
#define _IOC_DIRSHIFT	(_IOC_SIZESHIFT + _IOC_SIZEBITS)

#define _IOC_NONE	0U
#define _IOC_WRITE	1U
#define _IOC_READ	2U

#define _IOC(dir, type, nr, size)					\
	(((dir)  << _IOC_DIRSHIFT) |					\
	 ((type) << _IOC_TYPESHIFT) |					\
	 ((nr)   << _IOC_NRSHIFT) |					\
	 ((size) << _IOC_SIZESHIFT))

#define _IO(type, nr)		_IOC(_IOC_NONE, (type), (nr), 0)
#define _IOR(type, nr, size)	_IOC(_IOC_READ, (type), (nr), sizeof(size))
#define _IOW(type, nr, size)	_IOC(_IOC_WRITE, (type), (nr), sizeof(size))
#define _IOWR(type, nr, size)	_IOC(_IOC_READ | _IOC_WRITE, (type), (nr), sizeof(size))

#define _IOC_DIR(nr)		(((nr) >> _IOC_DIRSHIFT) & ((1 << _IOC_DIRBITS) - 1))
#define _IOC_TYPE(nr)		(((nr) >> _IOC_TYPESHIFT) & ((1 << _IOC_TYPEBITS) - 1))
#define _IOC_NR(nr)		(((nr) >> _IOC_NRSHIFT) & ((1 << _IOC_NRBITS) - 1))
#define _IOC_SIZE(nr)		(((nr) >> _IOC_SIZESHIFT) & ((1 << _IOC_SIZEBITS) - 1))

#endif /* __LINUX_IOCTL_H__ */
