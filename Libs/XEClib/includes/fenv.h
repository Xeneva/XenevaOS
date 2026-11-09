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

#ifndef __FENV_H__
#define __FENV_H__

#include <_xeneva.h>
#include <stdint.h>

#ifdef __cplusplus
XE_EXTERN {
#endif

/* Exception flags.  These are the FPSR cumulative-exception bit positions,
 * so a flag set can be masked into the status register directly. */
#define FE_INVALID	  0x01
#define FE_DIVBYZERO  0x02
#define FE_OVERFLOW	  0x04
#define FE_UNDERFLOW  0x08
#define FE_INEXACT	  0x10
#define FE_ALL_EXCEPT 0x1f

/* Rounding directions, encoded as AArch64 FPCR.RMode[23:22]. */
#define FE_TONEAREST  0x00000000
#define FE_UPWARD	  0x00400000
#define FE_DOWNWARD	  0x00800000
#define FE_TOWARDZERO 0x00c00000

/** @brief fexcept_t -- implementation-defined container for exception flags
 */
typedef unsigned int fexcept_t;

/** @brief fenv_t -- saved floating-point control and status registers
 */
typedef struct {
	uint64_t __fpcr;
	uint64_t __fpsr;
} fenv_t;

/** @brief FE_DFL_ENV -- the default environment: round-to-nearest, no flags
 */
#define FE_DFL_ENV ((const fenv_t*)-1)

XE_LIB int feclearexcept(int excepts);
XE_LIB int fegetexceptflag(fexcept_t* flagp, int excepts);
XE_LIB int fesetexceptflag(const fexcept_t* flagp, int excepts);
XE_LIB int feraiseexcept(int excepts);
XE_LIB int fetestexcept(int excepts);

XE_LIB int fegetround(void);
XE_LIB int fesetround(int rounding_mode);

XE_LIB int fegetenv(fenv_t* envp);
XE_LIB int fesetenv(const fenv_t* envp);
XE_LIB int feholdexcept(fenv_t* envp);
XE_LIB int feupdateenv(const fenv_t* envp);

#ifdef __cplusplus
}
#endif

#endif
