#ifndef __LINUX_MMAN_H__
#define __LINUX_MMAN_H__

/*
 * DCL <linux/mman.h> -- mmap(2) flag bits. mem.c tests MAP_SHARED in
 * get_unmapped_area_zero(); the values match mainline
 * (arch/arm64/include/uapi/asm/mman.h).
 */

#define MAP_SHARED	0x01
#define MAP_PRIVATE	0x02
#define MAP_FIXED	0x10
#define MAP_ANONYMOUS	0x20
#define MAP_POPULATE	0x008000

#define MMAP_PAGE_ZERO	0x01000000

#endif /* __LINUX_MMAN_H__ */
