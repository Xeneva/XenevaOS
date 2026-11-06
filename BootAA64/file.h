/**
* BSD 2-Clause License
*
* Copyright (c) 2023-2025, Manas Kamal Choudhury
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

#ifndef __XE_FILE_H__
#define __XE_FILE_H__

#include "xnldr.h"
#include "clib.h"
#include "xnout.h"
#include <Uefi.h>

typedef struct _XEFILE_ {
	VOID* kBuffer;
	UINTN FileSize;
	UINTN PageCount;
} XEFile;

/*
 * XEOpenAndReadFile -- open and reads a file
 * @param ImageHandle -- Image handle passed by EFI firmware
 * @param Filename -- name and path of the file
 */
extern XEFile* XEOpenAndReadFile(EFI_HANDLE ImageHandle, CHAR16* Filename);

/*
 * XECloseFile -- Close an opened file
 * it just free up the buffer allocated
 * @param file -- Pointer to the file buffer
 */
extern VOID XECloseFile(XEFile* file);

/*
 * XEFileExists -- probe for a file without reading it
 * @param ImageHandle -- Image handle passed by EFI firmware
 * @param Filename -- name and path of the file
 * @return true when the file can be opened for reading
 *
 * Silent on miss: absence is the normal case (used for the NOMENU
 * boot-menu skip marker). Prints only when the ESP itself is broken.
 */
extern bool XEFileExists(EFI_HANDLE ImageHandle, CHAR16* Filename);

#endif
