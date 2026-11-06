/*
 * DCL instruction_pointer.h -- _RET_IP_/_THIS_IP_ as plain addresses.
 *
 * linux/ww_mutex.h names these inside CONFIG_DEBUG_LOCK_ALLOC blocks that
 * do not compile here; the definitions exist so the macros are still
 * meaningful if any caller outside that guard asks.
 */
#ifndef _LINUX_INSTRUCTION_POINTER_H
#define _LINUX_INSTRUCTION_POINTER_H

/*
 * upstream include/linux/instruction_pointer.h includes this too, and it has
 * to come first: <asm/linkage.h>:46 defines arm64's _THIS_IP_ with `adr`, so
 * pulling it in here means the real instruction address wins and the #ifndef
 * below is what falls back.  Without it, the order of the two headers decided
 * the answer, and whichever came second redefined the macro -- the source of
 * the "macro redefined: _THIS_IP_" warning in build6.
 */
#include <asm/linkage.h>

#ifndef _RET_IP_
#define _RET_IP_		((unsigned long)__builtin_return_address(0))
#endif
#ifndef _THIS_IP_
#define _THIS_IP_		((unsigned long)__builtin_extract_return_addr(0))
#endif

#endif /* _LINUX_INSTRUCTION_POINTER_H */
