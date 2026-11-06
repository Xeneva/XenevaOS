/**
 * Minimal <fenv.h> for XenevaOS: quickjs.c includes it but only uses the
 * rounding mode constants / no-op entry points, none of which map to OS
 * support in XEClib. IEEE-754 default rounding is assumed throughout.
 */

#ifndef NS_QJS_FENV_H
#define NS_QJS_FENV_H

#define FE_INVALID	1
#define FE_DIVBYZERO	2
#define FE_OVERFLOW	4
#define FE_UNDERFLOW	8
#define FE_INEXACT	16
#define FE_ALL_EXCEPT	31

#define FE_TONEAREST	0
#define FE_DOWNWARD	1
#define FE_UPWARD	2
#define FE_TOWARDZERO	3

typedef int fexcept_t;

typedef struct {
	unsigned short int cw;
} fenv_t;

static inline int feclearexcept(int exc) {
	(void)exc;
	return 0;
}

static inline int fetestexcept(int exc) {
	(void)exc;
	return 0;
}

static inline int fegetround(void) {
	return FE_TONEAREST;
}

static inline int fesetround(int mode) {
	(void)mode;
	return 0;
}

static inline int fegetenv(fenv_t* envp) {
	if (envp)
		envp->cw = 0;
	return 0;
}

static inline int fesetenv(const fenv_t* envp) {
	(void)envp;
	return 0;
}

#endif
