/**
 * Double libm for the QuickJS port.
 *
 * XEClib ships sin, cos, tan, sqrt, pow, fabs, floor, ceil, fmod, and
 * frexp. QuickJS also calls the functions below. This file is what
 * produces the object; nothing here is a checked-in binary.
 */

#include <stdint.h>

double ceil(double);
double fabs(double);
double floor(double);
double frexp(double, int*);
double sqrt(double);

static double copysign_d(double x, double y) {
	union {
		double d;
		uint64_t u;
	} a, b;
	a.d = x;
	b.d = y;
	a.u = (a.u & ~((uint64_t)1 << 63)) | (b.u & ((uint64_t)1 << 63));
	return a.d;
}

static int is_nan(double x) {
	return x != x;
}

static int is_inf(double x) {
	union {
		double d;
		uint64_t u;
	} v;
	v.d = x;
	return (v.u & 0x7fffffffffffffffULL) == 0x7ff0000000000000ULL;
}

static double inf_d(void) {
	union {
		double d;
		uint64_t u;
	} v;
	v.u = 0x7ff0000000000000ULL;
	return v.d;
}

static double nan_d(void) {
	union {
		double d;
		uint64_t u;
	} v;
	v.u = 0x7ff8000000000000ULL;
	return v.d;
}

/* Scale by 2^n. n is a small integer from exp/log range reduction. */
static double scalbn_i(double x, int n) {
	union {
		double d;
		uint64_t u;
	} v;
	int e;
	if (x == 0.0 || is_nan(x) || is_inf(x))
		return x;
	v.d = x;
	e = (int)((v.u >> 52) & 0x7ff);
	if (e == 0) {
		x *= 18014398509481984.0; /* 2^54 */
		v.d = x;
		e = (int)((v.u >> 52) & 0x7ff) - 54;
	}
	e += n;
	if (e >= 0x7ff)
		return copysign_d(inf_d(), x);
	if (e > 0) {
		v.u = (v.u & ~((uint64_t)0x7ff << 52)) | ((uint64_t)e << 52);
		return v.d;
	}
	if (e <= -54)
		return copysign_d(0.0, x);
	v.u = (v.u & ((uint64_t)1 << 63)) | ((uint64_t)(e + 54) << 52) |
		  (v.u & (((uint64_t)1 << 52) - 1));
	return v.d * (1.0 / 18014398509481984.0);
}

double trunc(double x) {
	if (x >= 0.0)
		return floor(x);
	return ceil(x);
}

double round(double x) {
	if (is_nan(x) || is_inf(x))
		return x;
	if (x >= 0.0)
		return floor(x + 0.5);
	return ceil(x - 0.5);
}

/* Ties to even. Uint8ClampedArray uses this, not half-away round. */
long lrint(double x) {
	union {
		double d;
		uint64_t u;
	} v;
	double ax, ip, frac;
	int neg, raw, e, odd;
	if (is_nan(x) || is_inf(x))
		return 0;
	neg = copysign_d(1.0, x) < 0.0;
	ax = fabs(x);
	if (ax >= 2147483648.0)
		return neg ? (long)-2147483647 - 1 : (long)2147483647;
	ip = floor(ax);
	frac = ax - ip;
	if (frac > 0.5) {
		ip += 1.0;
	} else if (frac == 0.5) {
		v.d = ip;
		raw = (int)((v.u >> 52) & 0x7ff);
		e = raw - 1023;
		odd = 0;
		if (raw != 0 && e == 0)
			odd = 1;
		else if (e > 0 && e <= 52)
			odd = (int)((v.u >> (52 - e)) & 1);
		if (odd)
			ip += 1.0;
	}
	if (neg)
		return (long)(-ip);
	return (long)ip;
}

double fmax(double x, double y) {
	if (is_nan(x))
		return y;
	if (is_nan(y))
		return x;
	return x > y ? x : y;
}

double fmin(double x, double y) {
	if (is_nan(x))
		return y;
	if (is_nan(y))
		return x;
	return x < y ? x : y;
}

double hypot(double x, double y) {
	double t;
	if (is_nan(x) || is_nan(y))
		return nan_d();
	x = fabs(x);
	y = fabs(y);
	if (is_inf(x) || is_inf(y))
		return inf_d();
	if (x < y) {
		t = x;
		x = y;
		y = t;
	}
	if (x == 0.0)
		return 0.0;
	t = y / x;
	return x * sqrt(1.0 + t * t);
}

double cbrt(double x) {
	double a, y, y2;
	int e, r, i;
	if (x == 0.0 || is_nan(x) || is_inf(x))
		return x;
	a = fabs(x);
	a = frexp(a, &e);
	r = e % 3;
	if (r < 0)
		r += 3;
	e -= r;
	a = scalbn_i(a, r);
	y = 0.7 + 0.3 * a;
	for (i = 0; i < 6; i++) {
		y2 = y * y;
		y = (2.0 * y + a / y2) / 3.0;
	}
	return copysign_d(scalbn_i(y, e / 3), x);
}

static double atan_small(double x) {
	double x2 = x * x;
	double num = x;
	double s = x;
	int k;
	for (k = 1; k <= 18; k++) {
		num *= -x2;
		s += num / (double)(2 * k + 1);
	}
	return s;
}

double atan(double x) {
	int neg;
	int flip = 0;
	double y;
	if (is_nan(x))
		return x;
	neg = copysign_d(1.0, x) < 0.0;
	x = fabs(x);
	if (is_inf(x))
		return neg ? -1.5707963267948966 : 1.5707963267948966;
	if (x > 1.0) {
		x = 1.0 / x;
		flip = 1;
	}
	if (x > 0.41421356237309505) {
		y = 0.7853981633974483 + atan_small((x - 1.0) / (x + 1.0));
	} else {
		y = atan_small(x);
	}
	if (flip)
		y = 1.5707963267948966 - y;
	return neg ? -y : y;
}

double atan2(double y, double x) {
	double a;
	int yneg, xneg;
	if (is_nan(y) || is_nan(x))
		return nan_d();
	yneg = copysign_d(1.0, y) < 0.0;
	xneg = copysign_d(1.0, x) < 0.0;
	y = fabs(y);
	x = fabs(x);
	if (y == 0.0 && x == 0.0)
		a = xneg ? 3.141592653589793 : 0.0;
	else if (is_inf(x) && is_inf(y))
		a = xneg ? 2.356194490192345 : 0.7853981633974483;
	else if (x == 0.0 || is_inf(y))
		a = 1.5707963267948966;
	else if (is_inf(x))
		a = xneg ? 3.141592653589793 : 0.0;
	else {
		a = atan(y / x);
		if (xneg)
			a = 3.141592653589793 - a;
	}
	return yneg ? -a : a;
}

double asin(double x) {
	double t;
	if (is_nan(x))
		return x;
	if (fabs(x) > 1.0)
		return nan_d();
	t = sqrt(1.0 - x * x);
	return atan2(x, t);
}

double acos(double x) {
	double t;
	if (is_nan(x))
		return x;
	if (fabs(x) > 1.0)
		return nan_d();
	t = sqrt(1.0 - x * x);
	return atan2(t, x);
}

double exp(double x) {
	double n_d, r, r2, p;
	int n;
	if (is_nan(x))
		return x;
	if (x > 709.782712893384)
		return inf_d();
	if (x < -745.1332191019412)
		return 0.0;
	/* n*ln2_hi is exact for the exponents exp can return. */
	n_d = round(x * 1.4426950408889634);
	n = (int)n_d;
	r = x - n_d * 6.93147180369123816490e-01;
	r -= n_d * 1.90821492927058770002e-10;
	r2 = r * r;
	p = 1.0 + r +
		r2 * (0.5 +
			  r * (0.16666666666666666 +
				   r * (0.041666666666666664 +
						r * (0.008333333333333333 +
							 r * (0.001388888888888889 +
								  r * (0.0001984126984126984 +
									   r * (2.48015873015873e-5 +
											r * (2.7557319223985893e-6 +
												 r * (2.755731922398589e-7 +
													  r * (2.505210838544172e-8 +
														   r * (2.08767569878681e-9 +
																r * (1.6059043836821613e-10 +
																	 r * 1.1470745597729725e-11))))))))))));
	return scalbn_i(p, n);
}

double expm1(double x) {
	double s, term;
	int k;
	if (is_nan(x))
		return x;
	if (x > 709.782712893384)
		return inf_d();
	if (x < -745.1332191019412)
		return -1.0;
	if (fabs(x) > 1.0)
		return exp(x) - 1.0;
	s = 0.0;
	term = x;
	for (k = 1; k <= 24; k++) {
		s += term;
		term *= x / (double)(k + 1);
	}
	return s;
}

double log(double x) {
	double m, f, s, s2, p;
	int e;
	if (is_nan(x))
		return x;
	if (x < 0.0)
		return nan_d();
	if (x == 0.0)
		return -inf_d();
	if (is_inf(x))
		return x;
	m = frexp(x, &e);
	if (m < 0.7071067811865476) {
		m *= 2.0;
		e--;
	}
	f = m - 1.0;
	s = f / (2.0 + f);
	s2 = s * s;
	p = s2 * (1.0 / 3.0 +
			  s2 * (1.0 / 5.0 +
					s2 * (1.0 / 7.0 +
						  s2 * (1.0 / 9.0 +
								s2 * (1.0 / 11.0 +
									  s2 * (1.0 / 13.0 +
											s2 * (1.0 / 15.0 + s2 * (1.0 / 17.0 + s2 / 19.0))))))));
	s = 2.0 * (s + s * p);
	return (s + (double)e * 1.90821492927058770002e-10) +
		   (double)e * 6.93147180369123816490e-01;
}

double log1p(double x) {
	double s, term;
	int k;
	if (is_nan(x))
		return x;
	if (x < -1.0)
		return nan_d();
	if (x == -1.0)
		return -inf_d();
	if (is_inf(x))
		return x;
	if (fabs(x) >= 0.25)
		return log(1.0 + x);
	s = 0.0;
	term = x;
	for (k = 1; k <= 40; k++) {
		s += term / (double)k;
		term *= -x;
	}
	return s;
}

double log2(double x) {
	return log(x) * 1.4426950408889634;
}

double log10(double x) {
	return log(x) * 0.4342944819032518;
}

double sinh(double x) {
	double a, e, s, term, x2;
	int k;
	if (is_nan(x) || is_inf(x))
		return x;
	a = fabs(x);
	if (a <= 1.0) {
		s = x;
		term = x;
		x2 = x * x;
		for (k = 1; k <= 17; k += 2) {
			term *= x2 / (double)((k + 1) * (k + 2));
			s += term;
		}
		return s;
	}
	if (a > 20.0) {
		if (a > 710.4758600739439)
			return copysign_d(inf_d(), x);
		return copysign_d(exp(a - 0.6931471805599453), x);
	}
	e = exp(a);
	return copysign_d((e - 1.0 / e) * 0.5, x);
}

double cosh(double x) {
	double e, s, term, x2;
	int k;
	if (is_nan(x))
		return x;
	if (is_inf(x))
		return inf_d();
	x = fabs(x);
	if (x <= 1.0) {
		s = 1.0;
		term = 1.0;
		x2 = x * x;
		for (k = 0; k <= 16; k += 2) {
			term *= x2 / (double)((k + 1) * (k + 2));
			s += term;
		}
		return s;
	}
	if (x > 20.0) {
		if (x > 710.4758600739439)
			return inf_d();
		return exp(x - 0.6931471805599453);
	}
	e = exp(x);
	return (e + 1.0 / e) * 0.5;
}

double tanh(double x) {
	double e;
	if (is_nan(x))
		return x;
	if (is_inf(x) || fabs(x) > 20.0)
		return copysign_d(1.0, x);
	e = expm1(2.0 * x);
	return e / (e + 2.0);
}

double asinh(double x) {
	double a, t;
	if (is_nan(x) || is_inf(x))
		return x;
	a = fabs(x);
	if (a > 1e8)
		return copysign_d(log(a) + 0.6931471805599453, x);
	t = a * a;
	return copysign_d(log1p(a + t / (1.0 + sqrt(t + 1.0))), x);
}

double acosh(double x) {
	double y;
	if (is_nan(x))
		return x;
	if (x < 1.0)
		return nan_d();
	if (is_inf(x))
		return x;
	if (x > 1e8)
		return log(x) + 0.6931471805599453;
	y = x - 1.0;
	return log1p(y + sqrt(y * (x + 1.0)));
}

double atanh(double x) {
	double a;
	if (is_nan(x))
		return x;
	a = fabs(x);
	if (a > 1.0)
		return nan_d();
	if (a == 1.0)
		return copysign_d(inf_d(), x);
	return copysign_d(0.5 * log1p((2.0 * a) / (1.0 - a)), x);
}
