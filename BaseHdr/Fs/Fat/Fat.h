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

#ifndef __FAT_H__
#define __FAT_H__

#include <stdint.h>
#include <Fs/vdisk.h>
#ifdef ARCH_X64
#include <Sync/mutex.h>
#endif
#include <Fs/vfs.h>

#define FAT_ATTRIBUTE_MASK		0x3F
#define FAT_ATTRIBUTE_READ_ONLY 0x01
#define FAT_ATTRIBUTE_HIDDEN	0x02
#define FAT_ATTRIBUTE_SYSTEM	0x04
#define FAT_ATTRIBUTE_VOLUME	0x08
#define FAT_ATTRIBUTE_DIRECTORY 0x10
#define FAT_ATTRIBUTE_ARCHIVE	0x20
#define FAT_ATTRIBUTE_LONG_NAME 0x0F

#define FAT_EOC_MARK	0xFFFFFFF8
#define FAT_BAD_CLUSTER 0xFFFFFFF7

/* FAT32 end-of-chain marks span 0x0FFFFFF8-0x0FFFFFFF, and mkfs/mtools
 * terminate chains with 0x0FFFFFFF. Equality against FAT_EOC_MARK alone
 * misses that terminator, so cluster-chain walks must use this range
 * test -- otherwise they step off the end of the chain and loop forever. */
#define FAT_IS_EOC(clust) ((((uint32_t)(clust)) & 0x0FFFFFFF) >= (FAT_EOC_MARK & 0x0FFFFFFF))

#ifdef ARCH_X64
#pragma pack(push, 1)
#endif
/* FAT BPB */
typedef struct _FAT_BPB_ {
	uint8_t jmp[3];
	char oemid[8];				  //0x03 -- 8
	uint16_t bytes_per_sector;	  //0x00B 2
	uint8_t sectors_per_cluster;  //0x00D 1
	uint16_t reserved_sectors;	  //0x00E  2
	uint8_t num_fats;			  //0x010  1
	uint16_t num_dir_entries;	  //0x011 2
	uint16_t total_sectors_short; //0x013 2
	uint8_t media_type;			  //0x015 1
	uint16_t sectors_per_fat;	  //0x016 2
	uint16_t sectors_per_track;	  //0x018 2
	uint16_t heads;				  //0x01A 2
	uint32_t hidden_sectors;	  //0x01C 4
	uint32_t large_sector_count;  //0x020 4
	union {
		struct {
			uint8_t drive_num;
			uint8_t WinNtFlags;
			uint8_t signature;
			uint32_t vol_serial_num;
			char vol_label[11];
			char sys_id[8];
		} FAT16;

		struct {
			uint32_t sect_per_fat32;   //0x024  4
			uint16_t falgs;			   //0x028 2
			uint16_t fat_version;	   //0x02A 2
			uint32_t root_dir_cluster; //0x02C 4
			uint16_t fs_info_sect;	   //0x030 2
			uint16_t backup_boot_sect; //0x032 2
			uint8_t reserved[12];	   //0x034 12
			uint8_t drive_number;	   //0x040 1
			uint8_t flagsWinNT;		   //0x041 1
			uint8_t signature;		   //0x042 1
			uint32_t vol_serial_num;   //0x043 4
			char vol_label[11];		   //0x047 11
			char sys_id[8];			   //0x052 8
		} FAT32;
	} info;
} FatBPB;
#ifdef ARCH_X64
#pragma pack(pop)
#endif

/** _fat32_dir_ -- 32 byte fat directory structure */
#pragma pack(push, 1)
typedef struct _fat_dir_ {
	uint8_t filename[11];
	uint8_t attrib;
	uint8_t reserved;
	uint8_t time_created_ms;
	uint16_t time_created;
	uint16_t date_created;
	uint16_t date_last_accessed;
	uint16_t first_cluster_hi_bytes;
	uint16_t last_wrt_time;
	uint16_t last_wrt_date;
	uint16_t first_cluster;
	uint32_t file_size;
} FatDir;
#pragma pack(pop)

/*
 * Long file name entry
 */
#pragma pack(push, 1)
typedef struct _fat_lfn_ {
	uint8_t order;
	uint8_t nameOne[10];
	uint8_t attrib;
	uint8_t type;
	uint8_t checkSum;
	uint8_t nameTwo[12];
	uint16_t FstClusterLo;
	uint8_t nameThree[4];
} FatLFN;
#pragma pack(pop)

#define FSTYPE_FAT12 1
#define FSTYPE_FAT16 2
#define FSTYPE_FAT32 3

typedef struct _FatFS_ {
	FatBPB* bpb;
	AuVDisk* vdisk;
	char oemid[8];
	uint8_t fatType;
	unsigned long __FatBeginLBA;
	unsigned long __ClusterBeginLBA;
	unsigned char __SectorPerCluster;
	unsigned int __RootDirFirstCluster;
	unsigned long __RootSector;
	unsigned int __SectorPerFAT32;
	unsigned int __TotalClusters;
	unsigned char* __RootDirCache;
	unsigned int __LastIndexSector;
	unsigned int __LastIndexInFat;
	uint16_t __BytesPerSector;
	size_t cluster_sz_in_bytes;
	void* _scratchBuffer;
#ifdef ARCH_X64
	AuMutex* fat_mutex;
	AuMutex* fat_write_mutex;
	AuMutex* fat_read_mutex;
#endif
} FatFS;

/*
* FatInitialise -- initialise the fat file system
* @param vdisk -- Pointer to vdisk structure
* @param mountpoint -- mount point name
*/
extern AuVFSNode* FatInitialise(AuVDisk* vdisk, char* mountpoint);

/*
* FatClusterToSector32 -- Converts a cluster to sector
* @param cluster -- cluster number
*/
extern uint64_t FatClusterToSector32(FatFS* fs, uint64_t cluster);

/**
* FatToDOSFilename -- converts a given filename to MSDOS file name
* format
* @param filename -- filename for conversion
* @param fname -- pointer to the buffer to store the conversion
* @param fname_length -- manly 11 buffer[0-8] -- filename, buffer[9-11] -- file extension
*/
extern void FatToDOSFilename(const char* filename, char* fname, unsigned int fname_length);

/*
 * FatCalculateCheckSum -- returns an unsigned byte checksum computed on an
 * unsigned byte array. The array must be 11 bytes long and is assumed to
 * contain a name stored in the format of a MS-DOS directory entry
 * @param filename -- Pointer to an unsigned byte array assumed to be
 *                    11 bytes long
 */
extern uint8_t FatCalculateCheckSum(uint8_t* filename);

/*
 * FatCheckDotCount -- checks the number of '.' dot
 * count in a given filename
 * @param filename -- name of the file to check
 */
extern uint8_t FatCheckDotCount(char* filename);

/*
* FatFindFreeCluster -- finds a free cluster from FAT
* data structure
* @param node -- fs node
*/
extern uint32_t FatFindFreeCluster(AuVFSNode* node);

/*
* FatAllocCluster -- allocates a cluster by writing
* n_value
* @param fsys -- Pointer to file system node
* @param position -- position of the cluster
* @param n_value -- value to write
*/
extern void FatAllocCluster(AuVFSNode* fsys, int position, uint32_t n_value);

/**
* FatClearCluster -- clears a cluster to 0
* @param cluster -- cluster to clear
*/
extern void FatClearCluster(AuVFSNode* node, uint32_t cluster);

/*
* FatReadFAT -- read the file allocation table
* @param vdisk -- pointer to vdisk
* @param cluster_index -- index of the cluster
*/
extern uint32_t FatReadFAT(AuVFSNode* node, uint64_t cluster_index);

//! Opens a file
//! @param filename -- name of the file
//! @example -- /EFI/BOOT/BOOTx64.efi
extern AuVFSNode* FatOpen(AuVFSNode* fsys, char* filename);

/*
* FatFormatDate -- returns fat formated date stamp
*/
extern uint16_t FatFormatDate();

/*
* FatFormatTime -- returns fat formated time stamp
*/
extern uint16_t FatFormatTime();

AuVFSNode* FatLocateDir(AuVFSNode* fsys, const char* dir);

AuVFSNode* FatLocateSubDir(AuVFSNode* fsys, AuVFSNode* kfile, const char* filename);

/*
* FatFromDosToFilename -- converts fat directoy entry filename
* to human readable filename
*/
extern void FatFromDosToFilename(char* filename, char* dirfname);

/**
 * @bried FatSetAttribute -- set attribute to directory entry
 * @param fsys -- Pointer to file system 
 * @param file -- Pointer to file which need attribute update
 */
extern int FatSetAttribute(AuVFSNode* fsys, AuVFSNode* file);

#endif