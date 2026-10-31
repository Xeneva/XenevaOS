/**
 * pthread.h shim for the QuickJS port on XenevaOS.
 *
 * XEClib's pthread.h has mutexes, threads and rwlocks but no condition
 * variables; the porter's os.o stubbed pthread_cond_* as no-ops anyway
 * (XenevaOS userland runs quickjs single-threaded). This header keeps the
 * XEClib declarations (include_next) and adds cond types around them.
 */

#ifndef NS_QJS_PTHREAD_H
#define NS_QJS_PTHREAD_H

#include_next <pthread.h>

typedef struct {
	unsigned long long opaque;
} pthread_cond_t;

typedef unsigned int pthread_condattr_t;

#endif
