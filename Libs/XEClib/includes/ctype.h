/**
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

#ifndef __CTYPE_H__
#define __CTYPE_H__

#include <_xeneva.h>

#ifdef __cplusplus
extern "C" {
#endif

XE_LIB int isspace(int c);
XE_LIB int isascii(int c);
XE_LIB int isupper(int c);
XE_LIB int islower(int c);
XE_LIB int isalpha(int c);
XE_LIB int isdigit(int c);
XE_LIB int isxdigit(int c);
XE_LIB int isprint(int c);
XE_LIB int toupper(int c);
XE_LIB int tolower(int c);

#define toascii(c) ((unsigned)(c) & 0x7F)

#if 0

	extern char _ctype[257];

	/* Constants */

#define _LEADBYTE 0x8000					 // Multibyte leadbyte
#define _ALPHA	  (0x0100 | _UPPER | _LOWER) // Alphabetic character

#define CT_UP  0x01 /* upper case */
#define CT_LOW 0x02 /* lower case */
#define CT_DIG 0x04 /* digit */
#define CT_CTL 0x08 /* control */
#define CT_PUN 0x10 /* punctuation */
#define CT_WHT 0x20 /* white space (space/cr/lf/tab) */
#define CT_HEX 0x40 /* hex digit */
#define CT_SP  0x80 /* hard space (0x20) */

	/* Basic macros */

#define isalnum(c)	((_ctype + 1)[(unsigned)(c)] & (CT_UP | CT_LOW | CT_DIG))
#define isalpha(c)	((_ctype + 1)[(unsigned)(c)] & (CT_UP | CT_LOW))
#define iscntrl(c)	((_ctype + 1)[(unsigned)(c)] & (CT_CTL))
#define isdigit(c)	((_ctype + 1)[(unsigned)(c)] & (CT_DIG))
#define isgraph(c)	((_ctype + 1)[(unsigned)(c)] & (CT_PUN | CT_UP | CT_LOW | CT_DIG))
#define islower(c)	((_ctype + 1)[(unsigned)(c)] & (CT_LOW))
#define isprint(c)	((_ctype + 1)[(unsigned)(c)] & (CT_PUN | CT_UP | CT_LOW | CT_DIG | CT_SP))
#define ispunct(c)	((_ctype + 1)[(unsigned)(c)] & (CT_PUN))
#define isspace(c)	((_ctype + 1)[(unsigned)(c)] & (CT_WHT))
#define isupper(c)	((_ctype + 1)[(unsigned)(c)] & (CT_UP))
#define isxdigit(c) ((_ctype + 1)[(unsigned)(c)] & (CT_DIG | CT_HEX))

#endif

#ifdef __cplusplus
}
#endif

#endif