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

#ifndef __LOADER_H__
#define __LOADER_H__

#include <stdint.h>
#include <pe.h>
#include <process.h>

#ifdef ARCH_X64
#include <Sync/mutex.h>
#endif

/* the base address from where every executable image
 * starts
 */
#define PROC_IMAGE_BASE_START 0x0000000000600000

extern void AuInitialiseLoader();

/*
* AuLoadExecToProcess -- loads an executable to the
* process
* @param proc -- pointer to process data structure
* @param filename -- executable file name
* @param argc -- number of arguments
* @param argv -- array of argument in strings
*/
extern int AuLoadExecToProcess(AuProcess* proc, char* filename, int argc, char** argv);

/*
* AuProcessEntUser -- main kernel thread call
* in order to enter user for processes
* @param rcx -- user entry structure
*/
extern void AuProcessEntUser(uint64_t rcx);

extern void AuProcessEntSubThread(uint64_t rcx);

struct __mm_file_back__;

/*
 * AuLoadElfImage -- map a static aarch64 ELF from the file page cache.
 * Offset P is P2V(cache->physicalPage) + (P % PAGE_SIZE) on the
 * AuMMPageCache whose pageIndex is P / PAGE_SIZE. Not a PE image.
 */
extern int AuLoadElfImage(AuProcess* proc, struct __mm_file_back__* fb, size_t image_len,
						  int argc, char** argv);

/* Build the musl initial stack (argc, argv, envp, auxv) and return the sp. */
extern uint64_t AuLinuxBuildUserStack(AuUserEntry* uentry);

#ifdef ARCH_X64
/*
* AuLoaderGetMutex -- returns loader
* mutex
*/
extern AuMutex* AuLoaderGetMutex();

#endif

extern bool AuIsLoaderBusy();
#endif