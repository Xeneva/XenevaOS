/*
 * DCL tracepoint.h -- TRACE_EVENT becomes an empty static inline.
 *
 * drm_trace.h and trace/events/dma_fence.h declare events the drm/dma-fence
 * .c files emit at runtime.  The declarations keep the call sites parsing;
 * the bodies do nothing (Xeneva has no ftrace).  CREATE_TRACE_POINTS and
 * DEFINE_TRACE/EXPORT_TRACEPOINT_SYMBOL expand to nothing, so the
 * define_trace.h include at the bottom of those headers is inert too.
 *
 * TP_PROTO parenthesizes and the three generators do *not* add a second
 * pair: `static inline void trace_##name proto { }` with proto expanding to
 * `(struct dma_fence *fence)` is the declaration.  Adding parens around
 * `proto` as well produced `trace_fence_signaled((struct dma_fence *fence))`
 * -- a parameter *expression* in parentheses, which has no name and so no
 * type, and clang reported every one of trace/events/dma_fence.h's seven
 * events as "type specifier missing ... implicit int".
 *
 * Each generator emits the call stub *and* mainline's `_enabled()` companion
 * (tracepoint.h:222, which mainline builds on static_key_false).  Every
 * call site is `if (trace_x_enabled()) trace_x(...)`, and without the
 * companion the condition is an implicit call at link time -- dma-fence.c:555
 * is the one that did it, `trace_dma_fence_wait_end_enabled()` inside
 * dma_fence_wait_timeout().  `false` rather than a static key: there is no
 * ftrace to flip it on, which is the same reason the bodies are empty.
 */
#ifndef _LINUX_TRACEPOINT_H
#define _LINUX_TRACEPOINT_H

#include <stdbool.h>

#define TP_PROTO(...)		(__VA_ARGS__)
#define TP_ARGS(...)		(__VA_ARGS__)
#define TP_CONDITION(c)		(c)

#define TRACE_EVENT(name, proto, args, ...)					\
	static inline void trace_##name proto { }				\
	static inline bool trace_##name##_enabled(void) { return false; }
#define DEFINE_EVENT(class, name, proto, args, ...)				\
	static inline void trace_##name proto { }				\
	static inline bool trace_##name##_enabled(void) { return false; }
#define DECLARE_EVENT_CLASS(name, proto, args, tstruct, assign, print, ...)
#define DECLARE_TRACE(name, proto, args)					\
	static inline void trace_##name proto { }				\
	static inline bool trace_##name##_enabled(void) { return false; }
#define DEFINE_TRACE(name)
#define DECLARE_TRACE_CONDITION(name, proto, args, cond)

#define EXPORT_TRACEPOINT_SYMBOL(name)
#define EXPORT_TRACEPOINT_SYMBOL_GPL(name)

#define CREATE_TRACE_POINTS

#endif /* _LINUX_TRACEPOINT_H */
