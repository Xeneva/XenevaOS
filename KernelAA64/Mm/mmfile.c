/**
* @file mmcow.c
*
* BSD 2-Clause License
*
* Copyright (c) 2022, Manas Kamal Choudhury
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

#include <Mm/pmmngr.h>
#include <Mm/vmmngr.h>
#include <Mm/kmalloc.h>
#include <list.h>
#include <Mm/mmfile.h>
#include <_null.h>
#include <Drivers/uart.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <string.h>

static AuMMFileBack* fb_first;
static AuMMFileBack* fb_last;
static bool _page_cache_enable;

/**
 * @brief AuMmngrFileCacheInit -- initialize
 * copy-on-write manager
 */
void AuMmngrFileCacheInit() {
	//baalor kela, enei bonai thoisu eitu function
	fb_first = NULL;
	fb_last = NULL;
	_page_cache_enable = false;
	//eikeitai thakok de kelaa, taiu phone tu nodhore
	//mathai kam kora nai mur, enekei solibo lagibo de
}

/**
 * @brief AuMmngrAddFileBack -- add a file back
 * to file list
 * @param fb -- Pointer to file back struct
 */
void AuMmngrAddFileBack(AuMMFileBack* fb) {
	if (_page_cache_enable == false)
		return;
	fb->next = NULL;
	fb->prev = NULL;
	if (fb_first == NULL) {
		fb_last = fb;
		fb_first = fb;
	} else {
		fb_last->next = fb;
		fb->prev = fb_last;
	}
	fb_last = fb;
}

/**
* @brief AuThreadDelete -- remove a thread from thread list
* @param thread -- thread address to remove
*/
void AuMmngrRemoveFileBack(AuMMFileBack* fb) {
	UARTDebugOut("Removing file back :%s \r\n", fb->file->filename);
	if (fb_first == NULL)
		return;

	if (fb == fb_first) {
		fb_first = fb_first->next;
		if (fb_first)
			fb_first->prev = NULL;
	} else {
		if (fb->prev)
			fb->prev->next = fb->next;
	}
	if (fb == fb_last) {
		fb_last = fb->prev;
		if (fb_last)
			fb_last->next = NULL;
	} else {
		if (fb->next)
			fb->next->prev = fb->prev;
	}

	fb->next = (AuMMFileBack*)0xDEADDEADDEADDEADULL;
	fb->prev = (AuMMFileBack*)0xDEADDEADDEADDEADULL;
}

/**
 * @brief AuMmngrFileCacheEnable -- enable file cache
 */
void AuMmngrFileCacheEnable() {
	_page_cache_enable = true;
}

/**
 * AuMmngrFileCacheLookup -- lookup the file cache list
 * for specific file if it's already opened 
 * @param filename -- name of the file to lookup
 */
AuMMFileBack* AuMmngrFileCacheLookup(const char* filename) {
	if (_page_cache_enable == false)
		return NULL;

	char* fname = filename;
	while (*filename) {
		if (*filename == '/')
			fname = (char*)filename + 1;
		filename++;
	}
	//#ifdef __TARGET_BOARD_QEMU_VIRT__
	//	data_cache_flush(fname);
	//#endif

	AuMMFileBack* fileback = NULL;

	for (AuMMFileBack* fileb_ = fb_first; fileb_ != NULL; fileb_ = fileb_->next) {
		if (strcmp(fname, fileb_->file->filename) == 0) {
			fileback = fileb_;
			break;
		}
	}

	return fileback;
}

/**
 * AuMmngrFileCacheGetPhysicalBlock -- returns a physical block of a file
 * relative to file offset in bytes
 * @param fb -- pointer to file back structure
 * @param fileoffset -- offset in bytes within the file
 * @return physical page number or UINT64_MAX if not found
 */
uint64_t AuMmngrFileCacheGetPhysicalBlock(AuMMFileBack* fb, uint32_t fileoffset) {
	if (!fb)
		return NULL;
	size_t pageIndex = fileoffset / PAGE_SIZE;
	for (AuMMPageCache* cache = fb->pageCache; cache != NULL; cache = cache->next) {
		if (cache->pageIndex == pageIndex)
			return cache->physicalPage;
	}

	return UINT64_MAX;
}

AuMMPageCache* AuMmngrFileCacheGetByIndex(AuMMFileBack* fb, size_t pageIndex) {
	if (!fb)
		return NULL;
	for (AuMMPageCache* cache = fb->pageCache; cache != NULL; cache = cache->next) {
		if (cache->pageIndex == pageIndex)
			return cache;
	}
	return NULL;
}
/**
 * @brief AuMmngrPageCacheCreate -- create page cache
 * @return newly created page cache
 */
AuMMPageCache* AuMmngrPageCacheCreate() {
	AuMMPageCache* pc = (AuMMPageCache*)kmalloc(sizeof(AuMMPageCache));
	memset(pc, 0, sizeof(AuMMPageCache));
	return pc;
}

/**
 * @brief AuMmngrFileBackAddPageCache -- add a page cache to file back
 * @param fileb -- Pointer to file back struct
 * @param cache -- Cache to add
 */
void AuMmngrFileBackAddPageCache(AuMMFileBack* fileb, AuMMPageCache* cache) {
	cache->next = NULL;
	cache->prev = NULL;
	if (fileb->pageCache == NULL) {
		fileb->pageCacheLast = cache;
		fileb->pageCache = cache;
	} else {
		fileb->pageCacheLast->next = cache;
		cache->prev = fileb->pageCacheLast;
	}
	if (cache->diskBlock != -1)
		AuPmmngrSetBackingBlock(cache->physicalPage, cache->diskBlock);

	fileb->pageCacheLast = cache;
}
