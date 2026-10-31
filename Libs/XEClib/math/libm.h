/*
 * Internal helpers for the musl-derived double-precision math functions.
 *
 * The imported sources are MIT-licensed (Copyright (c) 2018, Arm Limited and
 * other musl contributors).  XEClib supplies the public <math.h> macros and
 * the bit-manipulation helpers; this header only adds the private pieces the
 * vendored files need.
 */

#ifndef _LIBM_H
#define _LIBM_H

#include <stdint.h>
#include <float.h>
#include <math.h>
#include <features.h>

/* Support non-nearest rounding mode.  */
#define WANT_ROUNDING 1
/* AArch64 has no libm-specific integer rounding intrinsic in this build. */
#ifndef TOINT_INTRINSICS
#define TOINT_INTRINSICS 0
#endif
/* XEClib does not track signaling NaN state. */
#define issignaling_inline(x) 0

/* Count leading zeroes; used by the fused multiply-add implementation. */
#ifndef a_clz_64
#define a_clz_64 a_clz_64
static inline int a_clz_64(uint64_t x) {
	return x ? __builtin_clzll(x) : 64;
}
#endif

#ifndef predict_true
#define predict_true(x) __builtin_expect(!!(x), 1)
#endif
#ifndef predict_false
#define predict_false(x) __builtin_expect(x, 0)
#endif

static inline float eval_as_float(float x) {
	float y = x;
	return y;
}

static inline double eval_as_double(double x) {
	double y = x;
	return y;
}

#ifndef fp_barrier
#define fp_barrier fp_barrier
static inline double fp_barrier(double x) {
	volatile double y = x;
	return y;
}
#endif

#ifndef fp_barrierf
#define fp_barrierf fp_barrierf
static inline float fp_barrierf(float x) {
	volatile float y = x;
	return y;
}
#endif

/* Argument reduction and polynomial kernels shared by sin/cos/tan. */
hidden int __rem_pio2_large(double*, double*, int, int, int);
hidden int __rem_pio2(double, double*);
hidden double __sin(double, double, int);
hidden double __cos(double, double);
hidden double __tan(double, double, int);
hidden double __expo2(double, double);

/* Error-result helpers. */
hidden double __math_xflow(uint32_t, double);
hidden double __math_uflow(uint32_t);
hidden double __math_oflow(uint32_t);
hidden double __math_divzero(uint32_t);
hidden double __math_invalid(double);

/* Gamma-function internals. */
extern int __signgam;
hidden double __lgamma_r(double, int*);

#endif
