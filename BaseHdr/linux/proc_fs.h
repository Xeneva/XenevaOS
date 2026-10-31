#ifndef __LINUX_PROC_FS_H__
#define __LINUX_PROC_FS_H__

/*
 * DCL <linux/proc_fs.h> -- Xeneva has no procfs, so this header exists to
 * resolve the include and to declare what a ported file might reach for.
 *
 * None of the nine tty/serial files creates a proc entry: uart_proc_show() is
 * only *assigned* under #ifdef CONFIG_PROC_FS (serial_core.c:2690), and DCL's
 * autoconf does not define it -- so the whole /proc/tty/driver/serial path is
 * compiled out, exactly as on a mainline kernel built without procfs. The
 * declarations below are what the rest of that path would use, and the
 * functions answer NULL/void rather than pretending an entry was created:
 * a caller that checks the return (mainline does, and unwinds) then sees the
 * truth.
 *
 * seq_printf()/seq_puts(), which uart_proc_show() writes through, live in
 * <linux/seq_file.h> because they are used outside procfs too.
 */

#include <linux/kernel.h>	/* umode_t -- kernel.h:57 owns the typedef */
#include <linux/seq_file.h>

struct proc_dir_entry;
struct file;
struct inode;

struct proc_ops {
	unsigned int (*proc_open)(struct inode*, struct file*);
	long (*proc_read)(struct file*, char*, unsigned long, long long*);
	long (*proc_lseek)(struct file*, long, int);
	int (*proc_release)(struct inode*, struct file*);
};

struct proc_dir_entry* proc_create(const char* name, umode_t mode,
				struct proc_dir_entry* parent,
				const struct proc_ops* proc_ops);
struct proc_dir_entry* proc_create_single(const char* name, umode_t mode,
				struct proc_dir_entry* parent,
				int (*show)(struct seq_file*, void*));
void remove_proc_entry(const char* name, struct proc_dir_entry* parent);
void proc_remove(struct proc_dir_entry* de);

#endif /* __LINUX_PROC_FS_H__ */
