/*
 * DCL asm/processor.h -- linux/seqlock.h includes this name; the only
 * facility code outside seqlock asks for on this build is cpu_relax().
 */
#ifndef _DCL_ASM_PROCESSOR_H
#define _DCL_ASM_PROCESSOR_H

#ifndef cpu_relax
#define cpu_relax()	__asm__ volatile("yield" ::: "memory")
#endif

#endif /* _DCL_ASM_PROCESSOR_H */
