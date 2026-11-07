#ifndef __LINUX_VMALLOC_H__
#define __LINUX_VMALLOC_H__

/*
 * DCL <linux/vmalloc.h> -- mem.c includes it for the bounce-buffer path,
 * which it services with kmalloc(PAGE_SIZE) instead. vmalloc() arrives when
 * a driver actually needs it; until then this is the empty shell that keeps
 * the include list intact. The address helpers that share a name with this
 * header (is_vmalloc_addr, vmalloc_to_page) live in <linux/mm.h>, which is
 * where mainline declares them too.
 */

#endif /* __LINUX_VMALLOC_H__ */
