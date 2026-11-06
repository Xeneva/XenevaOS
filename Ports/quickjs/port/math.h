/**
 * math.h shim for the QuickJS port on XenevaOS.
 *
 * XEClib's real math.h declares only a subset of double libm calls; the
 * rest live in port/libm.c (filling the double-precision gap on top of
 * XEClib's float primitives). quickjs.c needs correct prototypes for all
 * of them plus INFINITY/NAN/signbit -- so the QuickJS port build resolves
 * <math.h> to this header instead (include order puts Ports/quickjs/port
 * first for this library only).
 */

#ifndef NS_QJS_MATH_H
#define NS_QJS_MATH_H

#include <stdint.h>
#include <string.h>

/* ---- XEClib double subset (same functions, same names) ---- */
#ifdef __cplusplus
extern "C" {
#endif

double ceil(double);
double cos(double);
double fabs(double);
double floor(double);
double fmod(double, double);
double modf(double, double*);
double pow(double, double);
double sin(double);
double sqrt(double);
double tan(double);
float ceilf(float);
float acosf(float);
float cosf(float);
float fabsf(float);
float floorf(float);
float fmodf(float, float);
float sinf(float);
float sqrtf(float);
float tanf(float);
float atanf(float);
float atan2f(float, float);
double frexp(double, int*);
float roundf(float);
float fmaxf(float, float);
float fminf(float, float);
int fpclassify(double);

/* ---- double-precision gap, implemented in port/libm.c ---- */
double acos(double);
double acosh(double);
double asin(double);
double asinh(double);
double atan(double);
double atan2(double, double);
double atanh(double);
double cbrt(double);
double cosh(double);
double exp(double);
double expm1(double);
double fmax(double, double);
double fmin(double, double);
double hypot(double, double);
double log(double);
double log10(double);
double log1p(double);
double log2(double);
long lrint(double);
double round(double);
double sinh(double);
double tanh(double);
double trunc(double);

#ifdef __cplusplus
}
#endif

/* ---- constants / classification macros ---- */

#define M_E		   2.7182818284590452354
#define M_LOG2E	   1.4426950408889634074
#define M_LOG10E   0.43429448190325182765
#define M_LN2	   0.69314718055994530942
#define M_LN10	   2.30258509299404568402
#define M_PI	   3.14159265358979323846
#define M_PI_2	   1.57079632679489661923
#define M_PI_4	   0.78539816339744830962
#define M_1_PI	   0.31830988618379067154
#define M_2_PI	   0.63661977236758134308
#define M_2_SQRTPI 1.12837916709551257390
#define M_SQRT2	   1.41421356237309504880
#define M_SQRT1_2  0.70710678118654752440

#define HUGE_VAL  ((double)__builtin_huge_val())
#define HUGE_VALF ((float)__builtin_huge_valf())
#define INFINITY  ((float)__builtin_inff() + 0.0)
#define NAN		  ((float)__builtin_nanf("") + 0.0)

union ns_qjs_float_bits {
	float f;
	uint32_t i;
};

union ns_qjs_double_bits {
	double d;
	uint64_t i;
};

static inline uint32_t ns_fpclassify_pattern(double x) {
	union ns_qjs_double_bits b;
	b.d = x;
	uint32_t hi = (uint32_t)(b.i >> 32);
	uint32_t mant = hi & 0x000fffffu;
	uint32_t expn = hi >> 20;
	if (expn == 0)
		return (mant == 0) ? 0 /* zero */ : 3 /* sub */;
	if (expn == 0x7ff)
		return (mant == 0) ? 1 /* inf */ : 4 /* nan */;
	return 2 /* normal */;
}

static inline int signbit(double x) {
	union ns_qjs_double_bits b;
	b.d = x;
	return (int)((b.i >> 63) & 1u);
}

/* FP_ZERO / FP_NORMAL / FP_SUBNORMAL / FP_INFINITE / FP_NAN indexes match
 * the XEClib math.h enum { FP_NAN, FP_INFINITE, FP_ZERO, FP_SUBNORMAL,
 * FP_NORMAL } to keep isnan/isfinite behavior identical to the rest of
 * the OS. */
#define FP_NAN	  4
#define FP_INFINITE 1
#define FP_ZERO	  0
#define FP_SUBNORMAL 3
#define FP_NORMAL	  2

#define fpclassify(x) (ns_fpclassify_pattern(x))

#define isfinite(x) (ns_fpclassify_pattern(x) != FP_NAN && ns_fpclassify_pattern(x) != FP_INFINITE)
#define isnormal(x) (ns_fpclassify_pattern(x) == FP_NORMAL)
#define isnan(x)	(ns_fpclassify_pattern(x) == FP_NAN)
#define isinf(x)	(ns_fpclassify_pattern(x) == FP_INFINITE)

#endif
