/**
* @file mmfile.h
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

#ifndef __MM_FILE_H__
#define __MM_FILE_H__

#include <stdint.h>
#include <Fs/vfs.h>


typedef struct __page_cache__ {
	uint64_t physicalPage;
	uint64_t diskBlock;
	uint64_t pageIndex;
	struct __page_cache__* next;
	struct __page_cache__* prev;
}AuMMPageCache;


typedef struct __mm_file_back__ {
	AuVFSNode* file;
	AuMMPageCache* pageCache;
	AuMMPageCache* pageCacheLast;
	bool readComplete;
	uint32_t numPageIndex;
	struct __mm_file_back__* next;
	struct __mm_file_back__* prev;
}AuMMFileBack;

/**
 * @brief AuMmngrFileCacheInit -- initialize
 * copy-on-write manager
 */
extern void AuMmngrFileCacheInit();

/**
 * @brief AuMmngrAddFileBack -- add a file back
 * to file list
 * @param fb -- Pointer to file back struct
 */
extern void AuMmngrAddFileBack(AuMMFileBack* fb);

/**
* @brief AuThreadDelete -- remove a thread from thread list
* @param thread -- thread address to remove
*/
extern void AuMmngrRemoveFileBack(AuMMFileBack* fb);

/**
 * @brief AuMmngrFileCacheEnable -- enable file cache
 */
extern void AuMmngrFileCacheEnable();


/**
 * AuMmngrFileCacheLookup -- lookup the file cache list
 * for specific file if it's already opened
 * @param filename -- name of the file to lookup
 */
extern AuMMFileBack* AuMmngrFileCacheLookup(const char* filename);

/**
 * AuMmngrFileCacheGetPhysicalBlock -- returns a physical block of a file
 * relative to file offset in bytes
 * @param fb -- pointer to file back structure
 * @param fileoffset -- offset in bytes within the file
 * @return physical page number or UINT64_MAX if not found
 */
extern uint64_t AuMmngrFileCacheGetPhysicalBlock(AuMMFileBack* fb, uint32_t fileoffset);

/**
 * @brief AuMmngrPageCacheCreate -- create page cache
 * @return newly created page cache
 */
extern AuMMPageCache* AuMmngrPageCacheCreate();

extern AuMMPageCache* AuMmngrFileCacheGetByIndex(AuMMFileBack* fb, size_t pageIndex);



/**
 * @brief AuMmngrFileBackAddPageCache -- add a page cache to file back
 * @param fileb -- Pointer to file back struct
 * @param cache -- Cache to add
 */
extern void AuMmngrFileBackAddPageCache(AuMMFileBack* fileb, AuMMPageCache* cache);

#endif
