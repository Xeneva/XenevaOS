#ifndef __LINUX_MISCDEVICE_H__
#define __LINUX_MISCDEVICE_H__

/*
 * DCL <linux/miscdevice.h> -- mem.c still includes it, but mainline no
 * longer uses misc_register() for the memory devices: they go through
 * register_chrdev + device_create. Nothing here is needed; the header exists
 * so the vendored include list compiles unchanged. When a driver that does
 * use the misc class lands, extend it here.
 */

#endif /* __LINUX_MISCDEVICE_H__ */
