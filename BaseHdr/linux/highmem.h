#ifndef __LINUX_HIGHMEM_H__
#define __LINUX_HIGHMEM_H__

/*
 * DCL <linux/highmem.h> -- ARM64 has no highmem: every page is permanently
 * mapped, so kmap_local_page()/kunmap_local() degenerate to the address
 * itself in mainline too. mem.c only includes the header.
 */

#endif /* __LINUX_HIGHMEM_H__ */
