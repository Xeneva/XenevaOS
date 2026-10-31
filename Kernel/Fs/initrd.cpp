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
#include <Hal/serial.h>

uint64_t ramdisk_start;
uint64_t ramdisk_end;

#define RAMDISK_SECTOR_SIZE 512

#define RAMDISK_MAPPING_START 0xFFFFC00000900000

/*
 * AuRamdiskRead -- read data from ramdisk
 * @param lba -- LBA address
 * @param count -- number of sectors to read
 * @param buffer -- Pointer to buffer to write to
 */
void AuRamdiskRead(uint64_t lba, size_t count, uint8_t* buffer) {
	uint64_t offset = lba * RAMDISK_SECTOR_SIZE;
	if (ramdisk_start + offset + (RAMDISK_SECTOR_SIZE * count) > ramdisk_end)
		return;
	const uint8_t* src = (const uint8_t*)(ramdisk_start + offset);

	/* we're converting because file system was designed to 
	 * directly pass the physical buffer to disk controller 
	 * but we are using ramdisc
	 */
	buffer = (uint8_t*)P2V((uint64_t)buffer);
	for (int i = 0; i < RAMDISK_SECTOR_SIZE * count; i++)
		buffer[i] = src[i];
}

/*
 * AuRamdiskWrite -- write data to ramdisk
 * @param lba -- LBA address
 * @param count -- number of sectors to write
 * @param buffer -- Pointer to buffer from write to
 */
void AuRamdiskWrite(uint64_t lba, size_t count, uint8_t* buffer) {
	uint64_t offset = lba * RAMDISK_SECTOR_SIZE;
	if (ramdisk_start + offset + (RAMDISK_SECTOR_SIZE * count) > ramdisk_end)
		return;
	uint8_t* src = (uint8_t*)(ramdisk_start + offset);
	buffer = (uint8_t*)P2V((uint64_t)buffer);
	for (int i = 0; i < RAMDISK_SECTOR_SIZE * count; i++)
		src[i] = buffer[i];
}

int AuRamdiskReadCallback(AuVDisk* vdisk, uint64_t lba, uint32_t count, uint64_t* buffer) {
	AuRamdiskRead(vdisk->startingLBA + lba, count, (uint8_t*)buffer);
	return (count * RAMDISK_SECTOR_SIZE);
}

int AuRamdiskWriteCallback(AuVDisk* vdisk, uint64_t lba, uint32_t count, uint64_t* buffer) {
	AuRamdiskWrite(vdisk->startingLBA + lba, count, (uint8_t*)buffer);
	return (count * RAMDISK_SECTOR_SIZE);
}
/*
 * AuInitrdInitialize -- initialize ramdisk
 * @param info -- Pointer to Kernel Boot information
 */
void AuInitrdInitialize(KERNEL_BOOT_INFO* info) {

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


	if (ramdisk_start == 0) {
		AuTextOut("[aurora]: ramdisk failed to initialize \r\n");
		return;
	}
	/* okay before full initialization, we need to grab all the
	 * physical address from the ramdisk pointer and map it to
	 * kernel higher half address
	 */
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
	FatInitialise(disk, "/");
	AuTextOut("[aurora]: ramdisk initialized successfully \r\n");
}