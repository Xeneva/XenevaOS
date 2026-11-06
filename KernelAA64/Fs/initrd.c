/**
* @file initrd.c
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

#include <Fs/initrd.h>
#include <aucon.h>
#include <Fs/vdisk.h>
#include <Fs/vfs.h>
#include <Fs/Dev/devfs.h>
#include <Mm/vmmngr.h>
#include <string.h>
#include <Mm/kmalloc.h>
#include <_null.h>
#include <Fs/Fat/Fat.h>
#include <Mm/pmmngr.h>
#include <Drivers/uart.h>

uint64_t ramdisk_start;
uint64_t ramdisk_end;

#define RAMDISK_SECTOR_SIZE 512

#define RAMDISK_MAPPING_START 0xFFFFC00000900000

/**
 * @brief AuRamdiskRead -- read data from ramdisk
 * @param lba -- LBA address
 * @param count -- number of sectors to read
 * @param buffer -- Pointer to buffer to write to
 */
void AuRamdiskRead(uint64_t lba, size_t count, uint8_t* buffer) {
	uint64_t offset = lba * RAMDISK_SECTOR_SIZE;
	uint64_t ramdisk_size = ramdisk_end - ramdisk_start;
	uint64_t bytes_to_read = (uint64_t)count * RAMDISK_SECTOR_SIZE;

	if (offset + (RAMDISK_SECTOR_SIZE * count) > ramdisk_size) {
		return;
	}

	if (offset + bytes_to_read > ramdisk_size) {
		return;
	}

	if (offset >= (ramdisk_end - ramdisk_start) ||
		(RAMDISK_SECTOR_SIZE * count) > (ramdisk_end - ramdisk_start - offset)) {
		return;
	}

	const uint8_t* src = (const uint8_t*)(ramdisk_start + offset);
	memcpy(buffer, (void*)src, bytes_to_read);
	/*for (int i = 0; i < RAMDISK_SECTOR_SIZE * count; i++)
		buffer[i] = src[i];*/
}

/**
 * @brief AuRamdiskWrite -- write data to ramdisk
 * @param lba -- LBA address
 * @param count -- number of sectors to write
 * @param buffer -- Pointer to buffer from write to 
 */
void AuRamdiskWrite(uint64_t lba, size_t count, uint8_t* buffer) {
	uint64_t offset = lba * RAMDISK_SECTOR_SIZE;
	uint64_t ramdisk_size = ramdisk_end - ramdisk_start;
	uint64_t bytes_to_read = (uint64_t)count * RAMDISK_SECTOR_SIZE;
	if (offset + (RAMDISK_SECTOR_SIZE * count) > ramdisk_size)
		return;
	if (offset + bytes_to_read > ramdisk_size)
		return;

	if (offset >= (ramdisk_end - ramdisk_start) ||
		(RAMDISK_SECTOR_SIZE * count) > (ramdisk_end - ramdisk_start - offset))
		return;
	uint8_t* src = (uint8_t*)(ramdisk_start + offset);
	memcpy(src, buffer, bytes_to_read);
}

int AuRamdiskReadCallback(AuVDisk* vdisk, uint64_t lba, uint32_t count, uint64_t* buffer) {
	AuRamdiskRead(vdisk->startingLBA + lba, count, (uint8_t*)buffer);
	return (count * RAMDISK_SECTOR_SIZE);
}

int AuRamdiskWriteCallback(AuVDisk* vdisk, uint64_t lba, uint32_t count, uint64_t* buffer) {
	AuRamdiskWrite(vdisk->startingLBA + lba, count, (uint8_t*)buffer);
	return (count * RAMDISK_SECTOR_SIZE);
}
/**
 * @brief AuInitrdInitialize -- initialize ramdisk 
 * @param info -- Pointer to Kernel Boot information
 */
void AuInitrdInitialize(KERNEL_BOOT_INFO* info) {
	if (info->boot_type == BOOT_LITTLEBOOT_ARM64) {
		AuLittleBootProtocol* lb = (AuLittleBootProtocol*)info->driver_entry1;
		if (!lb) {
			AuTextOut("[aurora]: initrd initialization failed !! invalid Littleboot protocol \r\n");
			return;
		}
		ramdisk_start = P2V(lb->initrd_start);
		ramdisk_end = P2V(lb->initrd_end);
	} else {
		/* NOTE: Ramdisk is loaded by bootloader itself into EfiBootServiceData,
		 * memory areas other than EfiConventionalMemory area locaked by Physical
		 * memory manager during Kernel memory initialization phase, which is safe
		 * and will not get overwritten in future. We simply use P2V means converting
		 * the ramdisk physical memory to Higher half linear mapping, which is done
		 * during virtual memory manager initialization phase. EfiBootServiceData memories
		 * are all identity mapped, so everything is all set.
		 */
		ramdisk_start = P2V((uint64_t)info->driver_entry1);
		ramdisk_end = ramdisk_start + (info->hid - 1);
	}

	if (ramdisk_start == 0) {
		AuTextOut("[aurora]: ramdisk failed to initialize \r\n");
		return;
	}
	/** okay before full initialization, we need to grab all the
	 * physical address from the ramdisk pointer and map it to
	 * kernel higher half address
	 */
	AuTextOut("Loading ramdisc : %x \r\n", ramdisk_start);
	char* diskpath = (char*)kmalloc(32);
	memset(diskpath, 0, 32);
	AuVDiskCreateStorageFile(diskpath);

	AuVDisk* disk = AuCreateVDisk();
	strcpy(disk->diskname, "XERamdisc");
	disk->data = NULL;
	disk->Read = AuRamdiskReadCallback;
	disk->Write = AuRamdiskWriteCallback;
	disk->max_blocks = (ramdisk_end - ramdisk_start) / 512;
	disk->currentLBA = 0;
	disk->startingLBA = 0;
	disk->blockSize = RAMDISK_SECTOR_SIZE;

	strcpy(disk->diskPath, diskpath);
	int offset = strlen(disk->diskPath);
	strcpy(disk->diskPath + offset, "/");
	offset = strlen(disk->diskPath);
	strcpy(disk->diskPath + offset, "ramdisc");

	AuVDiskRegister(disk);

	AuVFSNode* file = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(file, 0, sizeof(AuVFSNode));
	strcpy(file->filename, "ramdisc");
	file->flags = FS_FLAG_DEVICE;
	file->device = NULL;
	file->write = 0;
	file->read = 0;

	AuVFSNode* dev = AuVFSFind("/dev");
	AuDevFSAddFile(dev, diskpath, file);

	AuTextOut("[aurora]: ramdisk mounted at %s full path : %s \r\n", diskpath, disk->diskPath);
	/* Mount FAT as root file system temporarily */
	//FatInitialise(disk, "/");
	AuTextOut("[aurora]: ramdisk initialized successfully \r\n");
}