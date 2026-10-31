/*
 * DCL asm/set_memory.h -- cache/RO page attribute calls are accepted and
 * ignored.  drm_gem_shmem_helper.c includes this for mainline's WC/UC
 * mappings of dumb buffers; DCL has no per-page attribute tracking, and
 * its write-combine behaviour is a property of the mapping, not the page.
 */
#ifndef _DCL_ASM_SET_MEMORY_H
#define _DCL_ASM_SET_MEMORY_H

static inline int set_memory_wc(unsigned long addr, int numpages)
{
	(void)addr; (void)numpages;
	return 0;
}

static inline int set_memory_uc(unsigned long addr, int numpages)
{
	(void)addr; (void)numpages;
	return 0;
}

static inline int set_memory_wb(unsigned long addr, int numpages)
{
	(void)addr; (void)numpages;
	return 0;
}

static inline int set_memory_x(unsigned long addr, int numpages)
{
	(void)addr; (void)numpages;
	return 0;
}

static inline int set_memory_ro(unsigned long addr, int numpages)
{
	(void)addr; (void)numpages;
	return 0;
}

static inline int set_memory_rw(unsigned long addr, int numpages)
{
	(void)addr; (void)numpages;
	return 0;
}

#endif /* _DCL_ASM_SET_MEMORY_H */
