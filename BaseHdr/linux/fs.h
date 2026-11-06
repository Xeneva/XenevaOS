#ifndef __LINUX_FS_H__
#define __LINUX_FS_H__

/*
 * Linux VFS ABI pins for the DCL layer.
 *
 * Frozen against linux-7.2.6 arm64 defconfig built with clang -- the exact
 * headers and .config that produced virtio_console.ko. The module reads and
 * writes these structs with its own compiled-in offsets, so every member the
 * module touches MUST sit at the offset marked below; everything else is
 * padding. Offsets verified by offsetof probes compiled inside the same
 * kernel tree (file 176B, file_operations 272B, inode 560B, cdev 104B,
 * device 800B, poll_table_struct 16B).
 */

#include <stdint.h>
#include <stddef.h>

struct cdev;
struct inode;
struct file;
struct module;
struct kiocb;
struct iov_iter;
struct dir_context;
struct vm_area_struct;
struct file_lock;
struct pipe_inode_info;
struct seq_file;
struct io_uring_cmd;
struct address_space;
struct io_comp_batch;
struct vm_area_desc;
struct inode;
struct super_block;

#ifndef MKDEV
#define MKDEV(ma, mi)  (((unsigned int)(ma) << 20) | ((unsigned int)(mi)))
#define MAJOR(dev)     ((unsigned int)(dev) >> 20)
#define MINOR(dev)     ((unsigned int)(dev) & 0xFFFFFu)
#endif

/* The input core's initcall is compiled but never run; these keep its body
 * linkable without claiming a major from the DCL allocator. */
int register_chrdev_region(unsigned int from, unsigned int count,
						   const char* name);
void unregister_chrdev_region(unsigned int from, unsigned int count);

typedef unsigned int fop_flags_t;
typedef unsigned int __poll_t;

struct poll_table_struct {
	void (*_qproc)(struct file*, void* wait_queue_head, struct poll_table_struct*);
	unsigned int _key;
};

/*
 * Member order copied verbatim from linux-7.2.6 include/linux/fs.h.
 * Offsets: owner 0, fop_flags 8, llseek 16, read 24, write 32, read_iter 40,
 * write_iter 48, iopoll 56, iterate_shared 64, poll 72, unlocked_ioctl 80,
 * compat_ioctl 88, mmap 96, open 104, flush 112, release 120, fsync 128,
 * fasync 136, lock 144, get_unmapped_area 152, check_flags 160, flock 168,
 * splice_write 176, ... mmap_prepare 264, size 272.
 * DCL only calls read/write/open/release/poll/fasync; the rest are layout
 * slots so the module's static initializers land correctly.
 */
struct file_operations {
	struct module* owner;                   /* 0 */
	fop_flags_t fop_flags;                  /* 8 */
	unsigned int _pad_flags;                /* 12 */
	void* llseek;                           /* 16 */
	long (*read)(struct file*, char*, size_t, long long*);      /* 24 */
	long (*write)(struct file*, const char*, size_t, long long*); /* 32 */
	void* read_iter;                        /* 40 */
	void* write_iter;                       /* 48 */
	void* iopoll;                           /* 56 */
	void* iterate_shared;                   /* 64 */
	__poll_t (*poll)(struct file*, struct poll_table_struct*);  /* 72 */
	void* unlocked_ioctl;                   /* 80 */
	void* compat_ioctl;                     /* 88 */
	/*
	 * mmap and get_unmapped_area are the third deviation from this
	 * header's pinned rule, and the reason is the same one as i_mapping's
	 * below: the drm core *calls* them, and a call through a void* is
	 * not a call at all -- clang reports "called object type 'void *' is
	 * not a function or function pointer". Two sites, drm_gem.c:1353
	 * (get_unmapped_area) and drm_prime.c:798 (mmap), both through a
	 * `const struct file_operations *` that is this struct.
	 *
	 * Both are given mainline's own signatures and both keep their slot:
	 * a function pointer is 8 bytes, a void* is 8 bytes, so offset 96 and
	 * offset 152 and sizeof(struct file_operations) are all unchanged.
	 * Every existing assignment still fits, since it was already assigning
	 * a function to these fields -- DEFINE_DRM_GEM_FOPS at drm_gem.h:472-473
	 * and DCL/mem.c:641 and :675 all spell the same 5-argument
	 * unsigned-long-returning get_unmapped_area mainline does.
	 */
	int (*mmap)(struct file*, struct vm_area_struct*);         /* 96 */
	int (*open)(struct inode*, struct file*);                   /* 104 */
	void* flush;                            /* 112 */
	int (*release)(struct inode*, struct file*);                /* 120 */
	void* fsync;                            /* 128 */
	int (*fasync)(int, struct file*, int);                      /* 136 */
	void* lock;                             /* 144 */
	unsigned long (*get_unmapped_area)(struct file*, unsigned long, unsigned long, unsigned long, unsigned long);  /* 152 */
	void* check_flags;                      /* 160 */
	void* flock;                            /* 168 */
	void* splice_write;                     /* 176 */
	void* splice_read;                      /* 184 */
	void* splice_eof;                       /* 192 */
	void* setlease;                         /* 200 */
	void* fallocate;                        /* 208 */
	void* show_fdinfo;                      /* 216 */
	void* copy_file_range;                  /* 224 */
	void* remap_file_range;                 /* 232 */
	void* fadvise;                          /* 240 */
	void* uring_cmd;                        /* 248 */
	void* uring_cmd_iopoll;                 /* 256 */
	void* mmap_prepare;                     /* 264 */
};

/*
 * mmap and get_unmapped_area are the two slots the drm core *calls* through
 * this struct (drm_prime.c:798, drm_gem.c:1353 -- the note inside the struct
 * explains why they carry real signatures).  The asserts pin the offsets that
 * comment claims, so a member inserted above them is a compile error here
 * rather than a call to an 8-byte field that is no longer a function.
 */
_Static_assert(__builtin_offsetof(struct file_operations, mmap) == 96,
               "fs.h: f_op->mmap is pinned at 96 (drm_prime.c:798 calls it)");
_Static_assert(__builtin_offsetof(struct file_operations, get_unmapped_area) == 152,
               "fs.h: f_op->get_unmapped_area is pinned at 152 (drm_gem.c:1353 calls it)");

/*
 * linux-7.2.6 struct file: f_lock 0, f_mode 4, f_op 8, f_mapping 16,
 * private_data 24, f_inode 32, f_flags 40, f_iocb_flags 44, f_cred 48,
 * f_owner 56, f_path 64, f_pos_lock 80, f_pos 96, ... size 176.
 * The module touches private_data (rw) and f_flags (O_NONBLOCK reads);
 * the bridge passes &f_pos to ->read/->write as the loff_t* position.
 * tail/field behind pad are layout-only (CONFIG_SECURITY etc. vary).
 */
struct file {
	int f_lock;                             /* 0 */
	unsigned int f_mode;                    /* 4 */
	const struct file_operations* f_op;     /* 8 */
	void* f_mapping;                        /* 16 */
	void* private_data;                     /* 24 */
	void* f_inode;                          /* 32 */
	unsigned int f_flags;                   /* 40 */
	unsigned int f_iocb_flags;              /* 44 */
	void* f_cred;                           /* 48 */
	void* f_owner;                          /* 56 */
	void* f_path;                           /* 64 */
	unsigned char _pos_lock[16];            /* 80 */
	long long f_pos;                        /* 96 */
	unsigned char _tail[176 - 104];         /* 104..175 */
};

/*
 * linux-7.2.6 struct inode: i_rdev 76, i_cdev 528, size 560.
 * The module reads only i_cdev (port_fops_open resolves its port from
 * inode->i_cdev->dev); i_rdev is kept for the bridge's own bookkeeping.
 *
 * i_mapping is the second deviation from the "pinned, do not edit" rule for
 * this header, and it is the same shape as the first: a member the drm core
 * cannot work without, taken out of padding rather than added at the end.
 *
 * Four sites read it -- drm_file.c:386 and :1074, drm_gem.c:747 and
 * drm_prime.c:251 -- all of them `...->i_mapping` on a struct inode *, which
 * is the one form a macro cannot fix: the token is to the right of `->`, and
 * a member that does not exist cannot be named, aliased or discarded from the
 * outside. Every consumer is an address-and-store (`filp->f_mapping =
 * dev->anon_inode->i_mapping`), so the object it points at is never touched
 * through this header -- an opaque pointer is enough.
 *
 * It is placed at offset 80, the first byte of what used to be _mid[448],
 * and _mid is shortened to 440 to keep every other offset where it was:
 * i_rdev still at 76, i_cdev still at 528, sizeof still 560. Nothing is
 * persisted, but the ABI these offsets describe belongs to code this header
 * does not own, and a blob whose padding moved would be a blob whose one
 * readable member landed somewhere else.
 */
struct inode {
	unsigned char _head[76];                /* 0..75 */
	unsigned int i_rdev;                    /* 76 */
	struct address_space* i_mapping;        /* 80 */
	unsigned char _mid[440];               /* 88..527 */
	struct cdev* i_cdev;                    /* 528 */
	unsigned char _tail[24];                /* 536..559 */
};

/*
 * i_cdev is the member the module actually reads -- cdev_shim's open resolves
 * a port from inode->i_cdev->dev -- and the size is pinned because the
 * module's own struct inode is 560 bytes: a blob whose padding moved would be
 * a blob whose one readable member landed somewhere else.  Both numbers are
 * mainline's, as the comment above the struct records; the asserts are what
 * keeps them that way.
 */
_Static_assert(sizeof(struct inode) == 560,
               "fs.h: struct inode is pinned to 560B (linux-7.2.6 arm64)");
_Static_assert(__builtin_offsetof(struct inode, i_cdev) == 528,
               "fs.h: i_cdev is pinned at 528 (the module resolves its port from it)");

/*
 * linux-7.2.6 struct cdev: kobj 0 (64B), owner 64, ops 72, list 80,
 * dev 96, count 100, size 104. The module writes ->ops before cdev_add
 * and reads ->dev in find_port_by_devt().
 */
struct cdev {
	unsigned char kobj[64];                 /* 0 (kobject: name at 0) */
	void* owner;                            /* 64 */
	const struct file_operations* ops;      /* 72 */
	unsigned char list[16];                 /* 80 */
	unsigned int dev;                       /* 96 */
	unsigned int count;                     /* 100 */
};

/*
 * linux-7.2.6 struct device: kobj.name 0, devt 708, size 800.
 * device_create() sets name (dev_name() reads kobj.name) and the module's
 * device_destroy() reads ->devt. Everything between is padding; PM and
 * link-state members live there in mainline and are never dereferenced
 * from the module.
 */
/* Owned by <linux/of.h>; declared here only so the of_node member below has a
 * type to point at without this header having to know what a device tree is. */
struct device_node;
struct class;
struct device_type;

/*
 * Two DCL-only members are carved out of the padding this struct declares
 * above, rather than appended after it, and the three things that make this
 * struct an ABI pin all still hold afterwards: kobj_name at 0, devt at 708,
 * sizeof 800.  Appending would have kept the first two and silently broken
 * the third -- the module embeds `struct device` inside its own objects and
 * was compiled with a 800-byte one, so an 816-byte DCL would have DCL's
 * writes landing past the end of the module's allocation.
 *
 * The offsets are mainline's, not chosen: `struct device` opens with `struct
 * kobject kobj`, which the struct cdev above measures at 64 bytes, so
 * `parent` is the next member at 64 -- the same slot the module would read
 * if it ever read one.  `of_node` sits at 696, the last 8-byte-aligned hole
 * before the devt pin, with 704..707 left as `_pad` so devt does not move.
 * Both are read-only at their two call sites (serial_core.c:3093 and :3236),
 * and DCL's own devices come out of device_create()'s memset, so they read
 * zero rather than whatever the module left in those bytes.
 *
 * The three _Static_asserts below exist so that none of this has to be
 * re-argued: if a future edit moves devt or grows the struct, the build says
 * so at this line instead of at boot, when the failure would be a corrupt
 * device table and no message to explain it.
 */
struct device {
	union {
		const char* kobj_name;          /* 0 (kobject.name) */
		unsigned char kobj[64];         /* 0..63, same as struct kobject */
	};
	struct device* parent;                  /* 64 (after kobj, as in mainline) */
	const struct class* class;              /* 72 */
	const struct device_type* type;         /* 80 */
	void* driver_data;                      /* 88 */
	void (*release)(struct device*);        /* 96 */
	const struct attribute_group** groups;  /* 104 */
	unsigned char _mid[584];                /* 112..695 */
	struct device_node* of_node;            /* 696 */
	unsigned char _pad[4];                  /* 700..707 */
	unsigned int devt;                      /* 708 */
	unsigned char _tail[88];                /* 712..799 */
};

_Static_assert(sizeof(struct device) == 800,
               "fs.h: struct device is pinned to 800B (linux-7.2.6 arm64)");
_Static_assert(__builtin_offsetof(struct device, kobj_name) == 0,
               "fs.h: kobj_name is pinned at 0 (device_create writes it)");
_Static_assert(__builtin_offsetof(struct device, devt) == 708,
               "fs.h: devt is pinned at 708 (the module's device_destroy reads it)");

/* IS_ERR/PTR_ERR: same range check the module inlines from linux/err.h. */
#define DCL_MAX_ERRNO 4095
static inline int dcl_is_err(const void* ptr) {
	/* uintptr_t, not unsigned long: this target is LLP64 (aarch64
	 * windows), where unsigned long is 32 bits */
	return (uintptr_t)ptr >= (uintptr_t)(-(long)DCL_MAX_ERRNO);
}
#define ERR_PTR(err) ((void*)(long)(err))
#define PTR_ERR(ptr) ((long)(ptr))
#define IS_ERR(ptr)  dcl_is_err(ptr)

/*
 * fops_get() / replace_fops() -- mainline's two <linux/fs.h> helpers for
 * rebinding a file's operations from ->open(); drm_stub_open() (drm_drv.c
 * :1203, :1209) is the tree's only caller and includes this header directly.
 *
 * Both keep mainline's signature and its effect, not its implementation:
 * mainline's fops_get() is a macro over try_module_get() and its
 * replace_fops() a macro pairing fops_put() with a BUG_ON() on the new
 * pointer. DCL modules are compiled into the image and never unloaded, so
 * holding a reference is keeping the pointer -- the same trade get_file()
 * makes next door in <linux/file.h> -- and neither module_put() nor BUG_ON()
 * is reachable from this header. A NULL fops still answers NULL.
 */
static inline const struct file_operations*
fops_get(const struct file_operations* fops)
{
	return fops;
}

static inline void replace_fops(struct file* filp,
								const struct file_operations* fops)
{
	filp->f_op = fops;
}

#endif /* __LINUX_FS_H__ */
