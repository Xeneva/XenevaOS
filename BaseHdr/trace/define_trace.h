/*
 * DCL trace/define_trace.h -- deliberately empty.
 *
 * Mainline this file instantiates tracepoint definitions when
 * CREATE_TRACE_POINTS is set; DCL's linux/tracepoint.h never emits any, so
 * drm_trace.h's trailing #include <trace/define_trace.h> is a no-op.
 */
