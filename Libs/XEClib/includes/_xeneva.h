/**
* @file _xeneva.h
* 
* BSD 2-Clause License
*
* Copyright (c) 2022-2023, Manas Kamal Choudhury
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

#ifndef __XENEVA_H__
#define __XENEVA_H__

#include <stdint.h>

/** if, YES_DYNAMIC is set to 1, everything will
* be exported as dynamic library
*/

#ifdef YES_DYNAMIC
#define XE_EXPORT __declspec(dllexport)
#define XE_IMPORT __declspec(dllimport)
#else
/* nothing to define here*/
#define XE_EXPORT
#define XE_IMPORT
#endif

#define XE_EXTERN extern "C"

#define XE_API	XE_EXTERN
#define XE_MAIN XE_EXPORT

#define XE_LIB XE_EXPORT

#define AURORA_SYSCALL_MAGIC 0x15062023

#ifdef _MSC_VER
#define INLINE __inline
#else
#define INLINE inline
#endif

#ifdef __cplusplus
XE_EXTERN {
#endif

	/** @brief _KePrint -- kernel printing function, uses
	 * kernel serial console to print strings
	 */
	XE_LIB void _KePrint(const char* text, ...);

	XE_LIB void _XESetEnvironmentVariable(char* key, char* value, bool overwrite);

	XE_LIB int _XEPutEnvironmentVariable(char* keyval);

	XE_LIB const char* _XEGetEnvironmentVariable(const char* key);

	XE_LIB int _KePowerDown();

	XE_LIB int _KePowerReset();
#ifdef __cplusplus
}
#endif

#endif