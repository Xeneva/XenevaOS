/**
* BSD 2-Clause License
*
* Copyright (c) 2023-2024, Manas Kamal Choudhury
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

/*
 * Fat.cpp -- contains only Open functionalities
 * FatDir.cpp -- contains Fat directory creations
 * FatFile.cpp -- contains Fat file creations only with 
 * short file name entries
 */


#include <Fs/Fat/Fat.h>
#include <Fs/Fat/FatFile.h>
#include <Fs/Fat/FatDir.h>
#include <Fs/vdisk.h>
#include <Fs/vfs.h>
#include <Mm/pmmngr.h>
#include <Mm/vmmngr.h>
#include <string.h>
#include <aucon.h>
#include <ctype.h>
#include <_null.h>
#include <Hal/serial.h>
#include <Mm/kmalloc.h>
#include <Sync/spinlock.h>
#include <pe.h>
#include <Drivers/rtc.h>
#include <Sync/mutex.h>

extern bool _vfs_debug_on;

/*
 * FatClusterToSector32 -- Converts a cluster to sector
 * @param cluster -- cluster number
 */
uint64_t FatClusterToSector32(FatFS *fs, uint64_t cluster) {
	return fs->__ClusterBeginLBA + ((cluster - 2) * fs->__SectorPerCluster);
}

/**
* FatToDOSFilename -- converts a given filename to MSDOS file name
* format
* @param filename -- filename for conversion
* @param fname -- pointer to the buffer to store the conversion
* @param fname_length -- manly 11 buffer[0-8] -- filename, buffer[9-11] -- file extension
*/
void FatToDOSFilename(const char* filename, char* fname, unsigned int fname_length)
{
	unsigned int i = 0;

	if (fname_length > 11)
		return;

	if (!fname || !filename)
		return;

	memset(fname, ' ', fname_length);

	for (i = 0; i < strlen(filename) && i < fname_length; i++)
	{
		if (filename[i] == '.' || i == 8)
			break;

		fname[i] = toupper(filename[i]);
	}

	if (filename[i] == '.')
	{
		for (int k = 0; k < 3; k++)
		{
			++i;
			if (filename[i])
				fname[8 + k] = filename[i];
		}
	}

	for (i = 0; i < 3; i++)
		fname[8 + i] = toupper(fname[8 + i]);
}

/*
 * FatCalculateCheckSum -- returns an unsigned byte checksum computed on an
 * unsigned byte array. The array must be 11 bytes long and is assumed to 
 * contain a name stored in the format of a MS-DOS directory entry
 * @param filename -- Pointer to an unsigned byte array assumed to be 
 *                    11 bytes long
 */
uint8_t FatCalculateCheckSum(uint8_t* filename) {
	short nameLen;
	uint8_t sum = 0;
	for (nameLen = 11; nameLen != 0; nameLen--) 
		sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + *filename++;
	return sum;
}

/*
 * FatCheckDotCount -- checks the number of '.' dot
 * count in a given filename 
 * @param filename -- name of the file to check
 */
uint8_t FatCheckDotCount(char* filename) {
	uint8_t dot_count = 0;
	for (int i = 0; i < strlen(filename); i++) {
		if (filename[i] == '.')
			dot_count++;
	}
	return dot_count;
}

/*
 * FatFromDosToFilename -- converts fat directoy entry filename
 * to human readable filename
 */
void FatFromDosToFilename(char* filename, char* dirfname) {
	memset(filename, 0, 11);
	int index = 0;
	for (int i = 0; i < 8; i++) {
		if (dirfname[i] != 0x20 && dirfname[i] > 0x20) {
			filename[i] = dirfname[i];
			index++;
		}
	}
	int extension = index;
	filename[index] = '.';
	index++;
	bool _contain_ext = false;
	for (int i = 0; i < 3; i++) {
		if (dirfname[8 + i] != 0x20) {
			_contain_ext = true;
			filename[index + i] = dirfname[8 + i];
		}
	}
	if (!_contain_ext)
		filename[extension] = '\0';
}

/*
 * FatReadFAT -- read the file allocation table
 * @param vdisk -- pointer to vdisk
 * @param cluster_index -- index of the cluster
 */
uint32_t FatReadFAT(AuVFSNode *node, uint64_t cluster_index) {
	FatFS *fs = (FatFS*)node->device;
	AuVDisk *vdisk = (AuVDisk*)fs->vdisk;
	if (!vdisk){
		return NULL;
	}

	uint64_t fat_offset = cluster_index * 4;
	uint64_t fat_sector = fs->__FatBeginLBA + (fat_offset / fs->__BytesPerSector);
	
	size_t ent_offset = fat_offset % fs->__BytesPerSector;
	uint64_t *BuffArea = (uint64_t*)P2V((size_t)AuPmmngrAlloc());
	memset(BuffArea, 0, 4096);
	AuVDiskRead(vdisk, fat_sector, 1, (uint64_t*)V2P((size_t)BuffArea));
	unsigned char* buf = (unsigned char*)BuffArea;
	uint32_t value = *(uint32_t*)&buf[ent_offset];
	AuPmmngrFree((void*)V2P((size_t)BuffArea));
	return (value & 0x0FFFFFFF);
}

/*
 * FatFindFreeCluster -- finds a free cluster from FAT
 * data structure
 * @param node -- fs node
 */
uint32_t FatFindFreeCluster(AuVFSNode* node) {
	FatFS *fs = (FatFS*)node->device;
	AuVDisk *vdisk = (AuVDisk*)fs->vdisk;
	if (!vdisk)
		return NULL;

	uint64_t last_fat_sector = 0;
	uint64_t last_cluster_value = 0;
	int sector_num = 1;
	for (int i = 0; i < fs->__SectorPerFAT32; i++) {
		uint64_t fat_sector = fs->__FatBeginLBA + i;
		uint64_t* buffer = (uint64_t*)P2V((size_t)AuPmmngrAlloc());
		memset(buffer, 0, 4096);
		AuVDiskRead(vdisk, fat_sector, 1, (uint64_t*)V2P((size_t)buffer));
		uint8_t* buff = (uint8_t*)buffer;
		for (int clust_i = 0; clust_i < fs->__BytesPerSector / 4; clust_i++) {
			uint32_t value = *(uint32_t*)&buff[clust_i*4];
			if (value == 0x00000000) {
				if ((last_cluster_value == 0 && clust_i == 1) ||
					(last_cluster_value == 0 && clust_i == 0))
					continue;
				uint32_t total_entry_skipped = (sector_num - 1) * (fs->__BytesPerSector / 4);
			    uint32_t current_cluster_entry = total_entry_skipped + clust_i;
				/*SeTextOut("free cluster found -> %d sector -> %d \r\n",current_cluster_entry, fat_sector);
				SeTextOut("Original entry -> %d, last cluster -> %d \r\n", clust_i, total_entry_skipped);
				SeTextOut("Ent offset -> %d , sector num -> %d \r\n", clust_i, sector_num);
				SeTextOut("dadang dara value -> %x \r\n", value);*/
				return current_cluster_entry;
			}
			if (clust_i == (fs->__BytesPerSector / 4)-1) {
				last_cluster_value += clust_i;
			}
		}
		sector_num++;
	}
	return 0;
}

/*
 * FatAllocCluster -- allocates a cluster by writing
 * n_value 
 * @param fsys -- Pointer to file system node
 * @param position -- position of the cluster
 * @param n_value -- value to write
 */
void FatAllocCluster(AuVFSNode* fsys, int position, uint32_t n_value) {
	FatFS *fs = (FatFS*)fsys->device;
	AuVDisk* vdisk = (AuVDisk*)fs->vdisk;
	if (!vdisk)
		return;

	uint64_t fat_offset = static_cast<uint64_t>(position) * 4;
	uint64_t fat_sector = fs->__FatBeginLBA + (fat_offset / 512);
	size_t ent_offset = fat_offset % 512;
	
	uint32_t *buffer = (uint32_t*)P2V((size_t)AuPmmngrAlloc());
	memset(buffer, 0, PAGE_SIZE);
	AuVDiskRead(vdisk, fat_sector, 1, (uint64_t*)V2P((size_t)buffer));
	uint8_t* buf = (uint8_t*)buffer;
	uint32_t value2 = *(uint32_t*)&buf[ent_offset];
	/*SeTextOut("FatAllocCluster ->fat_offset -> %d fatsect -> %d \r\n", fat_offset,
		fat_sector);
	SeTextOut("EntOffset -> %d , value -> %x \r\n", ent_offset, value2);
	for (;;);*/


	uint32_t value = *(uint32_t*)&buf[ent_offset];
	*(uint32_t*)&buf[ent_offset] = n_value & 0x0FFFFFFF;


	AuVDiskWrite(vdisk, fat_sector, 1, (uint64_t*)V2P((size_t)buffer));
	AuPmmngrFree((void*)V2P((size_t)buffer));
}


/**
* FatClearCluster -- clears a cluster to 0
* @param cluster -- cluster to clear
*/
void FatClearCluster(AuVFSNode* node, uint32_t cluster) {
	FatFS* fs = (FatFS*)node->device;
	AuVDisk* vdisk = (AuVDisk*)fs->vdisk;
	if (!vdisk)
		return;

	uint64_t *buffer = (uint64_t*)P2V((size_t)AuPmmngrAlloc());
	memset(buffer, 0, PAGE_SIZE);
	//update_cluster (buffer,cluster);
	uint32_t sector = FatClusterToSector32(fs, cluster);
	AuVDiskWrite(vdisk, sector, fs->__SectorPerCluster, (uint64_t*)V2P((size_t)buffer));
	AuPmmngrFree((void*)V2P((size_t)buffer));
}


/**
* FatRead -- read just 4kb portion of a file
* @param file -- file structure pointer
* @param buf -- pointer to buffer to store the content
*/
size_t FatRead(AuVFSNode* fsys, AuVFSNode *file, uint64_t* buf) {
	FatFS* fs = (FatFS*)fsys->device;
	AuVDisk* vdisk = (AuVDisk*)fs->vdisk;
	if (!vdisk) {
		return NULL;
	}

	auto lba = FatClusterToSector32(fs, file->current);
	AuVDiskRead(vdisk, lba, fs->__SectorPerCluster, buf);

	uint32_t value = FatReadFAT(fsys,file->current);
	
	if (value >= (FAT_EOC_MARK & 0x0FFFFFFF)) {
		file->eof = 1;
		file->current = value;
		return static_cast<int64_t>(fs->__SectorPerCluster) * fs->__BytesPerSector;
	}

	if (value >= 0x0FFFFFF7) {
		file->eof = 1;
		file->current = value;
		return static_cast<int64_t>(fs->__SectorPerCluster) * fs->__BytesPerSector;
	}

	file->current = value;
	return static_cast<int64_t>(fs->__SectorPerCluster) * fs->__BytesPerSector;
}

/*
 * FatReadFile -- reads a file of some specified size in bytes
 * @param fsys -- Pointer to file system
 * @param file -- Pointer to file
 * @param buffer -- Pointer to reserved memory
 * @param length -- length to read in bytes
 */
size_t FatReadFile(AuVFSNode* fsys, AuVFSNode* file, uint64_t* buffer, uint32_t length) {
	if (!fsys)
		return 0;

	if (!file)
		return 0;

	FatFS* fs = (FatFS*)fsys->device;

	uint64_t read_bytes = 0;
	size_t ret_bytes = 0;
	uint8_t* aligned_buffer = (uint8_t*)buffer;

	size_t num_blocks = length / fs->cluster_sz_in_bytes +
		((length % fs->cluster_sz_in_bytes) ? 1 : 0);

	for (int i = 0; i < num_blocks; i++) {
		if (file->eof)
			break;
		uint64_t* buff = (uint64_t*)P2V((size_t)AuPmmngrAlloc());
		memset(buff, 0, PAGE_SIZE);
		read_bytes = FatRead(fsys, file, (uint64_t*)V2P((size_t)buff));
		memcpy(aligned_buffer, buff, PAGE_SIZE);
		AuPmmngrFree((void*)V2P((size_t)buff));
		aligned_buffer += PAGE_SIZE;
		ret_bytes += read_bytes;
		
	}

	/* Okay, might be we read one block, but length was less
	 * then 4KiB, just return that length 
	 */
	if (length < ret_bytes)
		ret_bytes = length;

	return ret_bytes;
}

AuVFSNode* FatLocateSubDir(AuVFSNode* fsys,AuVFSNode* kfile, const char* filename) {
	FatFS* _fs = (FatFS*)fsys->device;

	AuVFSNode* file = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(file, 0, sizeof(AuVFSNode));

	char dos_file_name[12];
	memset(dos_file_name, 0, 11);
	FatToDOSFilename(filename, dos_file_name, 11);
	dos_file_name[11] = 0;

	uint64_t* buf = (uint64_t*)P2V((size_t)AuPmmngrAlloc());
	if (kfile->flags != FS_FLAG_INVALID) {
		while (1){
			if (kfile->eof){
				break;
			}
			memset(buf, 0, PAGE_SIZE);
		
			FatRead(fsys, kfile, (uint64_t*)V2P((size_t)buf));
			
			FatDir* pkDir = (FatDir*)buf;
			for (unsigned int i = 0; i < (16*_fs->__SectorPerCluster); ++i) {
				char name[12];
				memcpy(name, pkDir->filename, 11);
				name[11] = '\0';

				if (strcmp(name, dos_file_name) == 0) {
					strcpy(file->filename, filename);
					file->current = pkDir->first_cluster;
					file->size = pkDir->file_size;
					file->eof = 0;
					file->pos = 0;
					file->status = FS_STATUS_FOUND;
					file->first_block = file->current;
					file->device = fsys;
					file->parent_block = kfile->first_block;
					if (pkDir->attrib & 0x10)
						file->flags |= FS_FLAG_DIRECTORY;
					else
						file->flags |= FS_FLAG_GENERAL;

					AuPmmngrFree((void*)V2P((size_t)buf));
					kfree(kfile);
					return file;
				}

				pkDir++;
			}
		}
	}

	AuPmmngrFree((void*)V2P((size_t)buf));
	kfree(file);
	if (kfile)
		kfree(kfile);
	return NULL;
}


AuVFSNode* FatLocateDir(AuVFSNode* fsys, const char* dir) {
	AuVFSNode* file = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(file, 0, sizeof(AuVFSNode));

	FatFS* fs = (FatFS*)fsys->device;
	AuVDisk *vdisk = (AuVDisk*)fs->vdisk;
	if (!vdisk)
		return NULL;

	uint64_t* buf;
	FatDir *dirent;
	char dos_file_name[12];

	FatToDOSFilename(dir, dos_file_name, 11);
	dos_file_name[11] = 0;
	
	buf = (uint64_t*)P2V((uint64_t)AuPmmngrAlloc());
	memset(buf, 0, PAGE_SIZE);
	for (unsigned int sector = 0; sector < fs->__SectorPerCluster; sector++) {

		memset(buf, 0, PAGE_SIZE);
		//ata_read_28 (root_sector + sector,1, buf);
		AuVDiskRead(vdisk, FatClusterToSector32(fs,fs->__RootDirFirstCluster) + sector, 1, (uint64_t*)V2P((uint64_t)buf));

		dirent = (FatDir*)buf;
	
		for (int i = 0; i < 16; i++) {

			char name[12];
			memcpy(name, dirent->filename, 11);
			name[11] = 0;
			if (strcmp(dos_file_name, name) == 0) {

				strcpy(file->filename, dir);
				file->current = dirent->first_cluster;
				file->size = dirent->file_size;
				file->eof = 0;
				file->status = FS_STATUS_FOUND;
				file->close = 0;
				file->first_block = file->current;
				file->pos = 0;
				file->device = fsys;
				file->parent_block = fs->__RootDirFirstCluster;
				if (dirent->attrib == 0x10)
					file->flags |= FS_FLAG_DIRECTORY;
				else
					file->flags |= FS_FLAG_GENERAL;
				SeTextOut("FAT OPEN -> %s %x \r\n", file->filename, file->current);
				AuPmmngrFree((void*)V2P((size_t)buf));
				return file;
			}
		
			dirent++;
		}
	}

	AuPmmngrFree((void*)V2P((size_t)buf));
	kfree(file);
	return NULL;
}


//! Opens a file 
//! @param filename -- name of the file
//! @example -- /EFI/BOOT/BOOTx64.efi
AuVFSNode * FatOpen(AuVFSNode * fsys, char* filename) {
	if (!fsys)
		return NULL;
	FatFS* _fs = (FatFS*)fsys->device;
	AuVFSNode *cur_dir = NULL;
	AuVDisk *vdisk = (AuVDisk*)fsys->device;
	char* p = 0;
	bool  root_dir = true;
	char* path = (char*)filename;
	
	//! any '\'s in path ?
	p = strchr(path, '/');
	if (!p) {

		//! nope, must be in root directory, search it
		cur_dir = FatLocateDir(fsys,path);

		//! found file ?
		if (cur_dir != NULL) {
			return cur_dir;
		}
		//! unable to find
		return NULL;
	}

	//! go to next character after first '\'
	p++;
	while (p) {

		//! get pathname
		char pathname[16];
		int i = 0;
		for (i = 0; i < 16; i++) {

			//! if another '\' or end of line is reached, we are done
			if (p[i] == '/' || p[i] == '\0')
				break;

			//! copy character
			pathname[i] = p[i];
		}
		pathname[i] = 0; //null terminate

		//! open subdirectory or file
		if (root_dir) {
			//! search root dir -- open pathname
			cur_dir = FatLocateDir(fsys,pathname);
			root_dir = false;
		}
		else {
			//! search a sub directory instead for pathname
			cur_dir = FatLocateSubDir(fsys,cur_dir, pathname);
		}

		//! found directory or file?
		if (cur_dir == NULL)
			break;

		//! find next '\'
		p = strchr(p + 1, '/');
		if (p)
			p++;
	}

	//! found file?
	if (cur_dir)
		return cur_dir;
	//! unable to find
	return NULL;
}

/*
 * FatGetClusterFor -- returns the cluster for provided byte offset
 * of the file
 * @param fs -- file system pointer
 * @param file -- pointer to file node
 * @param offset -- byte offset
 */
size_t FatGetClusterFor(AuVFSNode* fs,AuVFSNode* file, uint64_t offset){
	FatFS *fatfs = (FatFS*)fs->device;
	size_t index = offset / fatfs->cluster_sz_in_bytes;
	uint32_t cluster = file->first_block;
	for (int i = 0; i < index; i++) 
		cluster = FatReadFAT(fs, cluster);
	return cluster;
}




/*
* FatInitialise -- initialise the fat file system
* @param vdisk -- Pointer to vdisk structure
* @param mountname -- mount file system name
*/
AuVFSNode* FatInitialise(AuVDisk *vdisk, char* mountname){
	uint64_t* buffer = (uint64_t*)AuPmmngrAlloc();
	memset(buffer, 0, 4096);
	AuVDiskRead(vdisk, 0, 1, buffer);

	FatBPB* bpb = (FatBPB*)buffer;

	FatFS *fs = (FatFS*)kmalloc(sizeof(FatFS));
	memset(fs, 0, sizeof(FatFS));

	fs->bpb = bpb;
	fs->vdisk = vdisk;

	for (int i = 0; i < 8; i++) {
		AuTextOut("%c", bpb->oemid[i]);
		fs->oemid[i] = bpb->oemid[i];
	}
	fs->oemid[7] = '\0';

	AuTextOut("\n");
	fs->__FatBeginLBA = bpb->reserved_sectors;
	fs->__ClusterBeginLBA = bpb->reserved_sectors + (bpb->num_fats* bpb->info.FAT32.sect_per_fat32);
	fs->__SectorPerCluster = bpb->sectors_per_cluster;
	fs->__RootDirFirstCluster = bpb->info.FAT32.root_dir_cluster;
	fs->__RootSector = FatClusterToSector32(fs, fs->__RootDirFirstCluster);
	fs->__SectorPerFAT32 = bpb->info.FAT32.sect_per_fat32;
	fs->cluster_sz_in_bytes = static_cast<int64_t>(fs->__SectorPerCluster) * bpb->bytes_per_sector;
	AuTextOut("fs->cluster_Sz_in_bytes : %d \n", bpb->bytes_per_sector);
	fs->__BytesPerSector = bpb->bytes_per_sector;
	fs->fat_mutex = AuCreateMutex();
	fs->fat_write_mutex = AuCreateMutex();
	fs->fat_read_mutex = AuCreateMutex();
	fs->__TotalClusters = bpb->large_sector_count / fs->__SectorPerCluster;
	fs->__LastIndexInFat = 0;
	fs->__LastIndexSector = 0;
	size_t _root_dir_sectors = ((bpb->num_dir_entries * 32) + bpb->bytes_per_sector - 1) / bpb->bytes_per_sector;
	size_t _TotalSectors = (bpb->total_sectors_short == 0) ? bpb->large_sector_count : bpb->total_sectors_short;
	size_t fatsize = (bpb->sectors_per_fat == 0) ? bpb->info.FAT32.sect_per_fat32 : bpb->sectors_per_fat;
	size_t _dataSectors = _TotalSectors - (bpb->reserved_sectors + bpb->num_fats * fatsize + _root_dir_sectors);
	AuTextOut("data sectors : %d \n", _dataSectors);
	if (_dataSectors < 4085)
		fs->fatType = FSTYPE_FAT12;
	else if (_dataSectors < 65525)
		fs->fatType = FSTYPE_FAT16;
	else if (_dataSectors < 268435445)
		fs->fatType = FSTYPE_FAT32;

	if (fs->fatType != FSTYPE_FAT32) {
		AuPmmngrFree(buffer);
		kfree(fs);
		return NULL;
	}


	AuVFSNode* fsys = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(fsys, 0, sizeof(AuVFSNode));
	strcpy(fsys->filename, mountname);
	fsys->flags |= FS_FLAG_FILE_SYSTEM;
	fsys->open = FatOpen;
	fsys->device = fs;
	fsys->read = FatReadFile;
	fsys->read_block = FatRead;
	fsys->remove_dir = FatRemoveDir;
	fsys->remove_file = FatFileRemove;
	fsys->write = FatWrite;
	fsys->create_dir = FatCreateDir;
	fsys->create_file = FatCreateFile;
	fsys->get_blockfor = FatGetClusterFor;
	fsys->opendir = FatOpenDir;
	fsys->read_dir = FatDirectoryRead;
	vdisk->fsys = fsys;

	AuVFSAddFileSystem(fsys);
	AuVFSRegisterRoot(fsys);

	AuPmmngrFree(buffer);

	return fsys;
}

/*
 * FatFormatDate -- returns fat formated date stamp
 */
uint16_t FatFormatDate() {
	return (uint16_t)((2000 + AuRTCGetYear() - 1980) << 9 | AuRTCGetMonth() << 5 | AuRTCGetDay());
}

/*
 * FatFormatTime -- returns fat formated time stamp
 */
uint16_t FatFormatTime() {
	return (uint16_t)(AuRTCGetHour() << 11 | AuRTCGetMinutes() << 5 | AuRTCGetSecond() / 2);
}

