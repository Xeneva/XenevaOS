/**
* @file vdisk.c
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

#include <Fs/vdisk.h>
#include <Fs/_gpt.h>
#include <Mm/kmalloc.h>
#include <_null.h>
#include <aucon.h>
#include <Mm/pmmngr.h>
#include <string.h>
#include <stdio.h>
#include <Fs/Dev/devfs.h>
#include <Drivers/uart.h>
#include <Fs/fsprobe.h>
#include <Fs/Fat/Fat.h>
#include <Hal/AA64/profile.h>
#include <Fs/Ext2/ext2.h>

AuVDisk* VdiskArray[MAX_VDISK_DEVICES];
int _vdisk_num_;

/**
 * @brief AuVDiskInitialise -- initialise the vdisk
 */
void AuVDiskInitialise() {
	for (int i = 0; i < MAX_VDISK_DEVICES; i++)
		VdiskArray[i] = NULL;

	_vdisk_num_ = 0;
	AuTextOut("[aurora]: virtual disk system initialized \r\n");
}

/**
 * @brief AuVDiskCreateStorageFile -- creates a storage
 * directory in device file system
 * @param output -- Pointer to memory where to store
 * composite string value
 */
AU_EXTERN AU_EXPORT int AuVDiskCreateStorageFile(char* output) {
	int value = _vdisk_num_;
	char dirname[10];
	memset(dirname, 0, 10);
	strcpy(dirname, "disk");
	dirname[strlen(dirname) + 1] = '\0';
	int offset = strlen(dirname);
	char value_[5];
	sztoa(value, value_, 10);
	strcpy(dirname + offset, value_);
	value_[strlen(value_) + 1] = '\0';
	char path[32];
	memset(path, 0, 32);
	strcpy(path, "/dev/");
	path[strlen(path) + 1] = '\0';
	int path_offset = strlen(path);
	strcpy(path + path_offset, dirname);

	/* Now copy the entire path to output */
	strcpy(output, path);

	/* Create a directory for this disk */
	AuVFSNode* fs = AuVFSFind("/dev");
	AuDevFSCreateFile(fs, path, FS_FLAG_DIRECTORY);
	_vdisk_num_++;
	return value;
}

/**
 * @brief AuVDiskGetIndex -- returns a vdisk index
 */
uint8_t AuVDiskGetIndex() {
	for (uint8_t i = 0; i < MAX_VDISK_DEVICES; i++) {
		if (!VdiskArray[i])
			return i;
	}
	return UINT8_MAX;
}

/**
 * @brief AuCreateVDisk -- creates a vdisk and
 * return to the caller
 * @return return the newly created empty vdisk
 * structure
 */
AuVDisk* AuCreateVDisk() {
	AuVDisk* vdisk = (AuVDisk*)kmalloc(sizeof(AuVDisk));
	memset(vdisk, 0, sizeof(AuVDisk));
	return vdisk;
}

/**
 * @brief AuVDiskRead -- reads a disk block from registered disk
 * @param disk -- Pointer to vdsik structure
 * @param lba -- Linear block address to read
 * @param count -- number of blocks to read
 * @param buffer -- Buffer, where to store the data
 * @return return the amount of data being read in bytes
 */
size_t AuVDiskRead(AuVDisk* disk, uint64_t lba, uint32_t count, uint64_t* buffer) {
	if (disk->Read)
		return disk->Read(disk, disk->startingLBA + lba, count, buffer);
	return 0;
}

/**
* @brief AuVDiskWrite -- reads a disk block from registered disk
* @param disk -- Pointer to vdsik structure
* @param lba -- Linear block address to read
* @param count -- number of blocks to read
* @param buffer -- Buffer, where to store the data
* @return the amount of data being written in bytes
*/
size_t AuVDiskWrite(AuVDisk* disk, uint64_t lba, uint32_t count, uint64_t* buffer) {
	if (disk->Write)
		return disk->Write(disk, disk->startingLBA + lba, count, buffer);
	return 0;
}

/**
 * @brief AuVDiskFlush -- flushes a single registered disk's write
 * cache, if its driver provides one (no-op otherwise)
 * @param disk -- Pointer to vdisk structure
 */
int AuVDiskFlush(AuVDisk* disk) {
	if (!disk)
		return 0;
	if (disk->Flush)
		return disk->Flush(disk);
	return 0;
}

/**
 * @brief AuVDiskFlushAll -- flushes every registered disk's write
 * cache; called before power-down/reset so no dirty device-side
 * cache is lost
 */
void AuVDiskFlushAll() {
	for (int i = 0; i < MAX_VDISK_DEVICES; i++) {
		if (VdiskArray[i])
			AuVDiskFlush(VdiskArray[i]);
	}
}

/**
 * @brief AuVDiskRegisterPartition - Gether all informations about the partition
 * for now, only GUID Partition is supported
 * @param vdisk -- VDisk structure pointer
 */
void AuVDiskRegisterPartition(AuVDisk* vdisk) {
	uint64_t* buffer = (uint64_t*)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	memset(buffer, 0, 4096);
	if (!vdisk->Read)
		return;

	vdisk->Read(vdisk, 1, 1, buffer);
	uint8_t* aligned_buf = (uint8_t*)buffer;

	GPTHeader* header = (GPTHeader*)aligned_buf;

	/* check if it's Efi partition */
	if (strcmp(header->sig, "EFI PART") != 0) {
		AuTextOut("[aurora]: vdisk %s doesn't have valid GPT partition \r\n", vdisk->diskname);
		return;
	}
	for (int i = 0; i < 8; i++)
		UARTDebugOut("%c", aligned_buf[i]);

	uint64_t part_lba = header->part_table_lba;
	vdisk->num_partition = header->num_part_entries;
	for (int i = 0; i < header->num_part_entries; i++) {
		memset(buffer, 0, 4096);
		vdisk->Read(vdisk, part_lba, 1, buffer);
		aligned_buf = (uint8_t*)buffer;
		GPTPartition* part = (GPTPartition*)aligned_buf;
		if (part->first_lba != 0) {
			vdisk->startingLBA = part->first_lba;
			vdisk->currentLBA = vdisk->startingLBA;
			vdisk->num_partition = 1;
			vdisk->part_guid.Data1 = part->part_guid.Data1;
			vdisk->part_guid.Data2 = part->part_guid.Data2;
			vdisk->part_guid.Data3 = part->part_guid.Data3;
			for (int m = 0; m < 8; m++)
				vdisk->part_guid.Data4[m] = part->part_guid.Data4[m];

			vdisk->part_unique_guid.Data1 = part->part_unique_guid.Data1;
			vdisk->part_unique_guid.Data2 = part->part_unique_guid.Data2;
			vdisk->part_unique_guid.Data3 = part->part_unique_guid.Data3;
			for (int m = 0; m < 8; m++)
				vdisk->part_unique_guid.Data4[m] = part->part_guid.Data4[m];

			for (int j = 0; j < 70; j++)
				AuTextOut("%c", part->part_name[j]);
		}
		part_lba++;
	}
	AuTextOut("\r\n");
	AuTextOut("VDisk partition created startLBA -> %d \r\n", vdisk->startingLBA);
	AuTextOut("vDisk partition guid : ");
	AuTextOut("0x%x-0x%x-0x%x-0x",
			  vdisk->part_guid.Data1,
			  vdisk->part_guid.Data2,
			  vdisk->part_guid.Data3);
	for (int k = 0; k < 8; k++)
		AuTextOut("%x", vdisk->part_guid.Data4[k]);

	AuTextOut("\r\n");
	/* call gpt file system verifier to load
	 * the desired file system
	 */
	//AuGPTInitialise_FileSystem(vdisk);

	AuTextOut("\r\n");
	AuPmmngrReleasePage((uint64_t)buffer);
}

/**
* @brief AuVDiskRegister -- adds a vdisk service to the list
* @param disk -- disk to add
*/
void AuVDiskRegister(AuVDisk* disk) {
	uint8_t _index = AuVDiskGetIndex();
	/* check for last time, for any errors */
	if (VdiskArray[_index])
		return;

	VdiskArray[_index] = disk;
	AuTextOut("[aurora]: vdisk registered name : %s, serial : %s \r\n",
			  disk->diskname,
			  disk->serialNumber);

	disk->__VDiskID = _index;
	/* Register a partition and initialise the file system*/

	AuVDiskRegisterPartition(disk);

	aurora_fs_type type = AuProbeFileSystem(disk);
	AuTextOut("type : %d \r\n", type);

	switch (type) {
	case AURORA_FS_NTFS:
		AuTextOut("[aurora]: vdisk : %s, has NTFS file system \r\n", disk->diskname);
		break;
	case AURORA_FS_FAT32:
		AuTextOut("[aurora]: vdisk : %s, has FAT32 file system \r\n", disk->diskname);
		char* mpt = AuVFSReserveMountPointLetter();

		/** make letter /a reserved for root '/' **/
		if (strcmp(mpt, "a") == 0)
			FatInitialise(disk, "/");
		else
			FatInitialise(disk, mpt);
		break;
	case AURORA_FS_FAT16:
		AuTextOut("[aurora]: vdisk : %s has FAT16 file system \r\n", disk->diskname);
		break;
	case AURORA_FS_EXT2:
		AuTextOut("[aurora]: vdisk : %s has Ext2 file system \r\n", disk->diskname);
		mpt = AuVFSReserveMountPointLetter();
		Ext2Initialise(disk, mpt);
		break;
	}
}

/**
 * @brief AuVDiskDestroy -- destroy's a vdisk
 * @param vdisk -- pointer to vdisk
 */
void AuVDiskDestroy(AuVDisk* vdisk) {
	uint8_t _index = 0;
	for (uint8_t i = 0; i < MAX_VDISK_DEVICES; i++) {
		AuVDisk* disk = VdiskArray[i];
		if (disk == vdisk) {
			_index = i;
			break;
		}
	}

	VdiskArray[_index] = NULL;
	kfree(vdisk);
}

/**
 * @brief AuGetVDiskInfo -- returns virtual disk information
 * to application
 * @param vdiskID -- id of vdisk
 * @param buffer -- Pointer to memory where to store
 * the information
 * @return 0 on success,-1 on failure
 */
int AuGetVDiskInfo(uint8_t vdiskID, void* buffer) {
	if (!buffer)
		return -1;
	AuVDisk* vdisk = VdiskArray[vdiskID];
	if (!vdisk)
		return -1;
	AuVDiskInfo* info = (AuVDiskInfo*)buffer;
	strcpy(info->diskname, vdisk->diskname);
	strcpy(info->serialNumber, vdisk->serialNumber);
	info->vDiskID = vdiskID;
	info->maxBlocks = vdisk->max_blocks;
	info->blocksSize = vdisk->blockSize;
	info->num_partition = vdisk->num_partition;
	return 0;
}

/**
 * @brief AuGetVDiskPartitionInfo -- get partition information from
 * desired virtual disk
 * @param vdiskID -- virtual disk identifier
 * @param partitionID -- partition number
 * @param buffer -- memory pointer where to store the information
 * @return 0 on success, -1 on failure
 */
int AuGetVDiskPartitionInfo(uint8_t vdiskID, uint8_t partition_ID, void* buffer) {
	/*
	 *FUTURE WORK:
	 * VDisk *disk = VdiskArray[vdiskID];
	 * AuVDiskPartitionInfo* pinfo = (AuVDiskPartitionInfo*)buffer;
	 * strcpy(pinfo->mountedName, disk->partitions[partition_ID].mountedName);
	 * pinfo->startingLBA = disk->partition[partition_ID].startingLBA;
	 */
	if (!buffer)
		return -1;
	AuVDisk* vdisk = VdiskArray[vdiskID];
	if (!vdisk)
		return -1;
	if (!vdisk->fsys)
		return 1;
	AuVDiskPartitionInfo* pinfo = (AuVDiskPartitionInfo*)buffer;
	strcpy(pinfo->mountedName, vdisk->fsys->filename);
	//SeTextOut("FILE SYSTEM NAME -> %s \r\n", vdisk->fsys->filename);
	pinfo->partitionGUID.Data1 = vdisk->part_guid.Data1;
	pinfo->partitionGUID.Data2 = vdisk->part_guid.Data2;
	pinfo->partitionGUID.Data3 = vdisk->part_guid.Data3;
	for (int i = 0; i < 8; i++)
		pinfo->partitionGUID.Data4[i] = vdisk->part_guid.Data4[i];

	pinfo->uniqueGUID.Data1 = vdisk->part_unique_guid.Data1;
	pinfo->uniqueGUID.Data2 = vdisk->part_unique_guid.Data2;
	pinfo->uniqueGUID.Data3 = vdisk->part_unique_guid.Data3;
	for (int i = 0; i < 8; i++)
		pinfo->uniqueGUID.Data4[i] = vdisk->part_unique_guid.Data4[i];
	pinfo->startingLBA = vdisk->startingLBA;
	return 0;
}

/**
 * @brief AuGetVDisk -- get a vdisk from the
 * vdisk array
 * @param vdisk_idx -- virtual disk index
 * @return the vidisk structure respected to
 * the index
 */
AuVDisk* AuGetVDisk(int vdisk_idx) {
	return VdiskArray[vdisk_idx];
}
