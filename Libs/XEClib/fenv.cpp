/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2026, Manas Kamal Choudhury
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice, this
*    list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following disclaimer in the documentation
*    and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
**/

#include <fenv.h>

/*
 * The AArch64 floating-point environment is two system registers that user
 * mode can read and write directly:
 *
 *   FPCR  control  RMode[23:22] rounding direction
 *                  FZ[24]       flush denormals to zero
 *                  DN[25]       default NaN
 *                  bits 8..15   per-exception trap enables (IOE..IDE)
 *   FPSR  status   bits 0..4    sticky IOC, DZC, OFC, UFC, IXC
 *                  bit 7        IDC (input denormal)
 *                  bit 27       QC (SIMD saturation)
 *
 * The scheduler saves and restores both registers with the rest of the
 * FP/SIMD context (KernelAA64/Hal/aa64vector.s), so the environment is
 * per-thread and follows a thread across context switches.
 *
 * C sets the "no traps" model: the enable bits are never turned on here, so
 * an exception only raises its sticky flag.  FE_DFL_ENV is the architectural
 * reset value: round-to-nearest, no flags, no traps.
 */

#define FPCR_RMODE_MASK (3ull << 22)
#define FPCR_TRAP_MASK	0x00009f00ull
#define FPSR_EXCEPTIONS ((uint64_t)FE_ALL_EXCEPT)

static inline uint64_t fenv_get_fpcr(void) {
	uint64_t value;

	__asm__ __volatile__("mrs %0, fpcr" : "=r"(value));
	return value;
}

static inline void fenv_set_fpcr(uint64_t value) {
	__asm__ __volatile__("msr fpcr, %0" : : "r"(value) : "memory");
}

static inline uint64_t fenv_get_fpsr(void) {
	uint64_t value;

	__asm__ __volatile__("mrs %0, fpsr" : "=r"(value));
	return value;
}

static inline void fenv_set_fpsr(uint64_t value) {
	__asm__ __volatile__("msr fpsr, %0" : : "r"(value) : "memory");
}

int feclearexcept(int excepts) {
	fenv_set_fpsr(fenv_get_fpsr() & ~((uint64_t)excepts & FPSR_EXCEPTIONS));
	return 0;
}

int fegetexceptflag(fexcept_t* flagp, int excepts) {
	*flagp = (fexcept_t)(fenv_get_fpsr() & (uint64_t)excepts & FPSR_EXCEPTIONS);
	return 0;
}

int fesetexceptflag(const fexcept_t* flagp, int excepts) {
	uint64_t mask = (uint64_t)excepts & FPSR_EXCEPTIONS;
	uint64_t fpsr = fenv_get_fpsr();

	fenv_set_fpsr((fpsr & ~mask) | ((uint64_t)*flagp & mask));
	return 0;
}

int feraiseexcept(int excepts) {
	fenv_set_fpsr(fenv_get_fpsr() | ((uint64_t)excepts & FPSR_EXCEPTIONS));
	return 0;
}

int fetestexcept(int excepts) {
	return (int)(fenv_get_fpsr() & (uint64_t)excepts & FPSR_EXCEPTIONS);
}

int fegetround(void) {
	return (int)(fenv_get_fpcr() & FPCR_RMODE_MASK);
}

int fesetround(int rounding_mode) {
	if (rounding_mode != FE_TONEAREST && rounding_mode != FE_UPWARD &&
		rounding_mode != FE_DOWNWARD && rounding_mode != FE_TOWARDZERO)
		return 1;

	fenv_set_fpcr((fenv_get_fpcr() & ~FPCR_RMODE_MASK) | (uint64_t)rounding_mode);
	return 0;
}

int fegetenv(fenv_t* envp) {
	envp->__fpcr = fenv_get_fpcr();
	envp->__fpsr = fenv_get_fpsr();
	return 0;
}

int fesetenv(const fenv_t* envp) {
	if (envp == FE_DFL_ENV) {
		fenv_set_fpcr(0);
		fenv_set_fpsr(0);
		return 0;
	}

	fenv_set_fpcr(envp->__fpcr);
	fenv_set_fpsr(envp->__fpsr);
	return 0;
}

int feholdexcept(fenv_t* envp) {
	fegetenv(envp);
	fenv_set_fpcr(fenv_get_fpcr() & ~FPCR_TRAP_MASK);
	fenv_set_fpsr(fenv_get_fpsr() & ~FPSR_EXCEPTIONS);
	return 0;
}

int feupdateenv(const fenv_t* envp) {
	int raised = fetestexcept(FE_ALL_EXCEPT);

	fesetenv(envp);
	feraiseexcept(raised);
	return 0;
}
