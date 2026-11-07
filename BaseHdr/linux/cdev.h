/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_CDEV_H
#define _LINUX_CDEV_H

#include <linux/kobject.h>
#include <linux/kdev_t.h>
#include <linux/list.h>
#include <linux/device.h>

struct file_operations;
struct inode;
struct module;

/*
 * DCL delta: mainline declares `struct cdev` here, in its own words.  DCL
 * already carries it in <linux/fs.h> (:141), measured against linux-7.2.6 at
 * kobj 0 / owner 64 / ops 72 / list 80 / dev 96 / count 100 / size 104, and
 * pinned there by static asserts because fs.h's layout is the ABI the rest of
 * the tree is compiled against.  Two definitions of the same struct in one
 * translation unit is a hard redefinition error, and fs.h is the one file that
 * must not move -- so this header yields and routes instead: the struct comes
 * from where it is authoritative, and only the function declarations, which
 * DCL/linux_cdev_shim.c actually implements, are spelled here.
 *
 * __randomize_layout is dropped with it: DCL has no struct-randomization pass,
 * and the layout above is pinned by assertion anyway.
 */
#include <linux/fs.h>
#include <linux/file.h>	/* nonseekable_open() and the FMODE_ bits it clears;
			 * see file.h for why that helper is not here. */

void cdev_init(struct cdev *, const struct file_operations *);

struct cdev *cdev_alloc(void);

void cdev_put(struct cdev *p);

int cdev_add(struct cdev *, dev_t, unsigned);

void cdev_set_parent(struct cdev *p, struct kobject *kobj);
int cdev_device_add(struct cdev *cdev, struct device *dev);
void cdev_device_del(struct cdev *cdev, struct device *dev);

void cdev_del(struct cdev *);

void cd_forget(struct inode *);

#endif
