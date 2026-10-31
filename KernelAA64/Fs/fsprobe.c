/**
* @file fsprobe.c
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

#include <string.h>
#include <Fs/vdisk.h>
#include <Mm/kmalloc.h>
#include <Mm/pmmngr.h>
#include <Drivers/uart.h>

/**
 * fsprobe -- file system matching function, called by vdisk when
 * registering new block device
 */

typedef enum {
	AURORA_FS_UNKNOWN,
	AURORA_FS_FAT16,
	AURORA_FS_FAT32,
	AURORA_FS_EXT2,
	AURORA_FS_NTFS,
} aurora_fs_type;

/**
 * @brief AuProbeFileSystem -- get the currently used file system type of given block
 * device
 * @param vdisk -- pointer to vdisk 
 */
aurora_fs_type AuProbeFileSystem(AuVDisk* vdisk) {
	if (!vdisk->Read)
		return 0;

	uint8_t* sector = (uint8_t*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	memset(sector, 0, 512);

	vdisk->Read(vdisk, 0, 1, sector);

	/** check Windows type file systems first */
	// it should be char[9] like "NTFS-----" total
	// 9 characters
	if (memcmp(sector + 0x03, "NTFS    ", 8) == 0) {
		UARTDebugOut("ntfs \r\n");
		AuPmmngrReleasePage((uint64_t)V2P((uint64_t)sector));
		return AURORA_FS_NTFS;
	}

	if (memcmp(sector + 0x52, "FAT32   ", 8) == 0) {
		UARTDebugOut("fat32 \r\n");
		AuPmmngrReleasePage((uint64_t)V2P((uint64_t)sector));
		return AURORA_FS_FAT32;
	}

	if (memcmp(sector + 0x36, "FAT16   ", 8) == 0) {
		UARTDebugOut("fat16 \r\n");
		AuPmmngrReleasePage((uint64_t)V2P((uint64_t)sector));
		return AURORA_FS_FAT16;
	}

	/* check ext2/3/4 -- superblocks */
	memset(sector, 0, 512);

	vdisk->Read(vdisk, 2, 1, sector);

	uint16_t ext_magic = *(uint16_t*)(sector + 0x38);
	if (ext_magic == 0xEF53) {
		AuPmmngrReleasePage((uint64_t)V2P((uint64_t)sector));
		return AURORA_FS_EXT2;
	}

	AuPmmngrReleasePage((uint64_t)V2P((uint64_t)sector));
	return AURORA_FS_UNKNOWN;
}
