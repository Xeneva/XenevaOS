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
#include <Drivers/uart.h>
#include <Mm/kmalloc.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Mm/mmfile.h>
#include <Cred/cred.h>
#include <Cred/group.h>
#include <Hal/AA64/profile.h>

extern bool _vfs_debug_on;

extern uint16_t FatModeFromAttrib(uint8_t attrib, const char* filename);

/*
 * FatClusterToSector32 -- Converts a cluster to sector
 * @param cluster -- cluster number
 */
uint64_t FatClusterToSector32(FatFS* fs, uint64_t cluster) {
	return fs->__ClusterBeginLBA + ((cluster - 2) * fs->__SectorPerCluster);
}

/**
* FatToDOSFilename -- converts a given filename to MSDOS file name
* format
* @param filename -- filename for conversion
* @param fname -- pointer to the buffer to store the conversion
* @param fname_length -- manly 11 buffer[0-8] -- filename, buffer[9-11] -- file extension
*/
void FatToDOSFilename(const char* filename, char* fname, unsigned int fname_length) {
	unsigned int i = 0;

	if (fname_length > 11)
		return;

	if (!fname || !filename)
		return;

	memset(fname, ' ', fname_length);

	for (i = 0; i < strlen(filename) && i < fname_length; i++) {
		if (filename[i] == '.' || i == 8)
			break;

		fname[i] = toupper(filename[i]);
	}

	if (filename[i] == '.') {
		for (int k = 0; k < 3; k++) {
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
	if (!filename || !dirfname)
		return;
	memset(filename, 0, 16);
	int index = 0;
	for (int i = 0; i < 8; i++) {
		if (dirfname[i] != 0x20 && dirfname[i] > 0x20) {
			filename[index++] = dirfname[i];
		}
	}
	bool contain_ext = false;
	for (int i = 0; i < 3; i++) {
		if (dirfname[8 + i] != 0x20 && dirfname[8 + i] > 0x20) {
			if (!contain_ext) {
				filename[index++] = '.';
				contain_ext = true;
			}
			filename[index++] = dirfname[8 + i];
		}
	}
	filename[index] = '\0';
}

/*
 * FatFatSector -- returns the FAT sector at lba through the one-sector
 * FAT cache. Consecutive clusters share a sector (128 entries per 512
 * byte sector), so chain walks and free-cluster scans read the disk
 * once per sector instead of once per cluster.
 * @param fs -- Pointer to fat file system
 * @param lba -- FAT sector to read
 */
static uint8_t* FatFatSector(FatFS* fs, uint64_t lba) {
	if (!fs->__FatCache)
		return NULL;
	if (!fs->__FatCacheValid || fs->__FatCacheLBA != lba) {
		fs->__FatCacheValid = false;
		if (AuVDiskRead(fs->vdisk, lba, 1, (uint64_t*)fs->__FatCache) == 0)
			return NULL;
		fs->__FatCacheLBA = lba;
		fs->__FatCacheValid = true;
	}
	return fs->__FatCache;
}

/*
 * FatDirSector -- read one directory sector through the directory
 * sector cache; the returned buffer must not be written to or kept
 * @param fs -- Pointer to fat file system
 * @param lba -- sector to read
 */
uint8_t* FatDirSector(FatFS* fs, uint64_t lba) {
	if (!fs->__DirCache)
		return NULL;
	if (!fs->__DirCacheValid || fs->__DirCacheLBA != lba) {
		fs->__DirCacheValid = false;
		if (AuVDiskRead(fs->vdisk, lba, 1, (uint64_t*)fs->__DirCache) == 0)
			return NULL;
		fs->__DirCacheLBA = lba;
		fs->__DirCacheValid = true;
	}
	return fs->__DirCache;
}

/*
 * FatWriteDirSector -- write a modified directory sector to disk
 * and drop it from the directory sector cache
 * @param fs -- Pointer to fat file system
 * @param lba -- sector to write
 * @param buf -- sector content, page sized buffer
 */
void FatWriteDirSector(FatFS* fs, uint64_t lba, void* buf) {
	if (fs->__DirCacheValid && fs->__DirCacheLBA == lba)
		fs->__DirCacheValid = false;
	aa64_data_cache_clean_range(buf, PAGE_SIZE);
	AuVDiskWrite(fs->vdisk, lba, 1, (uint64_t*)buf);
}

/*
 * FatInvalidateCaches -- forget everything cached for a volume, used
 * when a removable volume is detached or attached again
 * @param fsys -- Pointer to file system node
 */
void FatInvalidateCaches(AuVFSNode* fsys) {
	if (!fsys || !fsys->device)
		return;
	FatFS* fs = (FatFS*)fsys->device;
	fs->__FatCacheValid = false;
	fs->__DirCacheValid = false;
	fs->__LastIndexInFat = 0;
}

/*
 * FatReadFAT -- read the file allocation table
 * @param vdisk -- pointer to vdisk
 * @param cluster_index -- index of the cluster
 */
uint32_t FatReadFAT(AuVFSNode* node, uint64_t cluster_index) {
	FatFS* fs = (FatFS*)node->device;
	AuVDisk* vdisk = (AuVDisk*)fs->vdisk;
	if (!vdisk) {
		return 0;
	}

	/* used to allocate, read and free a whole page on every call; this is
	 * the hottest path in the driver (every cluster of every read, seek,
	 * directory walk and write) */
	uint64_t fat_offset = cluster_index * 4;
	uint64_t fat_sector = fs->__FatBeginLBA + (fat_offset / fs->__BytesPerSector);
	size_t ent_offset = fat_offset % fs->__BytesPerSector;

	uint8_t* sector = FatFatSector(fs, fat_sector);
	if (!sector)
		return 0;
	uint32_t value = *(uint32_t*)(sector + ent_offset);
	return (value & 0x0FFFFFFF);
}

/*
 * FatFindFreeCluster -- finds a free cluster from FAT
 * data structure
 * @param node -- fs node
 * @return a free cluster number, 0 when the volume is full
 */
uint32_t FatFindFreeCluster(AuVFSNode* node) {
	FatFS* fs = (FatFS*)node->device;
	AuVDisk* vdisk = (AuVDisk*)fs->vdisk;
	if (!vdisk)
		return 0;

	uint32_t per_sector = fs->__BytesPerSector / 4;
	uint32_t total = fs->__TotalClusters;
	uint32_t fat_entries = fs->__SectorPerFAT32 * per_sector;
	if (!total || total > fat_entries)
		total = fat_entries;
	if (total <= 2)
		return 0;

	/* resume where the last search stopped instead of rescanning the FAT
	 * from sector 0 for every cluster a write appends; wraps once */
	uint32_t span = total - 2;
	uint32_t start = fs->__LastIndexInFat;
	if (start < 2 || start >= total)
		start = 2;

	for (uint32_t n = 0; n < span; n++) {
		uint32_t cluster = 2 + ((start - 2 + n) % span);
		uint64_t fat_offset = (uint64_t)cluster * 4;
		uint8_t* sector =
			FatFatSector(fs, fs->__FatBeginLBA + (fat_offset / fs->__BytesPerSector));
		if (!sector)
			return 0;
		uint32_t value = *(uint32_t*)(sector + (fat_offset % fs->__BytesPerSector));
		if ((value & 0x0FFFFFFF) == 0) {
			fs->__LastIndexInFat = cluster + 1;
			return cluster;
		}
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
	FatFS* fs = fsys->device;
	AuVDisk* vdisk = (AuVDisk*)fs->vdisk;
	if (!vdisk)
		return;

	/* Never allow writing into reserved FAT clusters 0 or 1 */
	if (position < 2 || (fs->__TotalClusters && (uint32_t)position >= fs->__TotalClusters))
		return;

	uint32_t pos = position;
	uint64_t fat_offset = (uint64_t)pos * 4;
	uint32_t bytes_per_sector = fs->__BytesPerSector ? fs->__BytesPerSector : 512;

	uint64_t fat_sector = fs->__FatBeginLBA + (fat_offset / bytes_per_sector);
	size_t ent_offset = fat_offset % bytes_per_sector;

	/* write-through: the cached sector is updated and written, so the
	 * FAT cache never holds stale entries */
	uint8_t* buf = FatFatSector(fs, fat_sector);
	if (!buf)
		return;

	uint32_t value = *(uint32_t*)&buf[ent_offset];
	*(uint32_t*)&buf[ent_offset] = (value & 0xF0000000) | (n_value & 0x0FFFFFFF);

	/* a freed cluster below the search hint is the next one to reuse */
	if (n_value == 0 && pos < fs->__LastIndexInFat)
		fs->__LastIndexInFat = pos;

	aa64_data_cache_clean_range(buf, PAGE_SIZE);
	/* keep every FAT copy in step; only FAT #1 used to be written, so
	 * FAT #2 drifted and fsck/chkdsk reported the volume as damaged */
	uint8_t fats = fs->__NumFats ? fs->__NumFats : 1;
	for (uint8_t f = 0; f < fats; f++)
		AuVDiskWrite(vdisk, fat_sector + (uint64_t)f * fs->__SectorPerFAT32, 1, (uint64_t*)buf);
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

	uint64_t* buffer = (uint64_t*)P2V((size_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	if (!buffer)
		return;
	memset(buffer, 0, PAGE_SIZE);
	/* the zeroes must reach memory before the device reads the buffer */
	aa64_data_cache_clean_range(buffer, PAGE_SIZE);
	uint64_t sector = FatClusterToSector32(fs, cluster);
	/* a cleared cluster may have been a cached directory sector */
	if (fs->__DirCacheValid && fs->__DirCacheLBA >= sector &&
		fs->__DirCacheLBA < sector + fs->__SectorPerCluster)
		fs->__DirCacheValid = false;
	AuVDiskWrite(vdisk, sector, fs->__SectorPerCluster, buffer);
	AuPmmngrReleasePage((uint64_t)V2P((size_t)buffer));
}

/**
* FatRead -- read just 4kb portion of a file
* @param file -- file structure pointer
* @param buf -- pointer to buffer to store the content
*/
size_t FatRead(AuVFSNode* fsys, AuVFSNode* file, uint64_t* buf) {
	FatFS* fs = (FatFS*)fsys->device;
	AuVDisk* vdisk = (AuVDisk*)fs->vdisk;
	if (!vdisk) {
		return NULL;
	}

	if (FAT_IS_EOC(file->current))
		return 0;

	uint32 lba = FatClusterToSector32(fs, file->current);
	AuVDiskRead(vdisk, lba, fs->__SectorPerCluster, buf);

	uint32_t value = FatReadFAT(fsys, file->current);

	if (value >= (FAT_EOC_MARK & 0x0FFFFFFF)) {
		file->eof = 1;
		file->current = value;
		//data_cache_flush(file);
		return ((size_t)fs->__SectorPerCluster * fs->__BytesPerSector);
	}

	if (value >= 0x0FFFFFF7) {
		file->eof = 1;
		file->current = value;
		//data_cache_flush(file);
		return ((size_t)fs->__SectorPerCluster * fs->__BytesPerSector);
	}

	file->current = value;
	//data_cache_flush(file);
	return ((size_t)fs->__SectorPerCluster * fs->__BytesPerSector);
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

	size_t copied_bytes = 0;
	uint8_t* aligned_buffer = (uint8_t*)buffer;

	size_t skip = file->pos % fs->cluster_sz_in_bytes;
	size_t total = length + skip;
	size_t num_blocks = (total + fs->cluster_sz_in_bytes - 1) /
						fs->cluster_sz_in_bytes; /*length / fs->cluster_sz_in_bytes +
		((length % fs->cluster_sz_in_bytes) ? 1 : 0);*/

	skip = 0;
	size_t avail = 0;
	size_t to_copy = 0;

	uint64_t* buff = (uint64_t*)P2V((size_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	for (int i = 0; i < num_blocks; i++) {
		if (file->eof)
			break;
		skip = (i == 0) ? (file->pos % fs->cluster_sz_in_bytes) : 0;
		avail = fs->cluster_sz_in_bytes - skip;
		to_copy = (length < avail) ? length : avail;

		//	memset(buff, 0, PAGE_SIZE);
		FatRead(fsys, file, buff);
		memcpy(aligned_buffer, (uint8_t*)buff + skip, to_copy);
		aligned_buffer += to_copy;
		length -= to_copy;
		copied_bytes += to_copy;
	}

	// reset the position in bytes value
	file->pos = 0;

	AuPmmngrReleasePage((uint64_t)V2P((size_t)buff));
	return copied_bytes;
}

AuVFSNode* FatLocateSubDir(AuVFSNode* fsys, AuVFSNode* kfile, const char* filename) {
	FatFS* _fs = (FatFS*)fsys->device;

	AuVFSNode* file = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(file, 0, sizeof(AuVFSNode));

	char dos_file_name[12];
	memset(dos_file_name, 0, 11);
	FatToDOSFilename(filename, dos_file_name, 11);
	dos_file_name[11] = 0;
	uint64_t* buf = (uint64_t*)P2V((size_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	if (kfile->flags != FS_FLAG_INVALID) {
		while (1) {
			if (kfile->eof) {
				break;
			}
			//memset(buf, 0, PAGE_SIZE);

			FatRead(fsys, kfile, buf);

			FatDir* pkDir = (FatDir*)buf;
			for (unsigned int i = 0; i < (16 * _fs->__SectorPerCluster); ++i) {
				char name[12];
				memcpy(name, pkDir->filename, 11);
				name[11] = '\0';

				if (strcmp(name, dos_file_name) == 0) {
					strcpy(file->filename, filename);
					file->current = (pkDir->first_cluster_hi_bytes << 16) | pkDir->first_cluster;
					file->size = pkDir->file_size;
					file->eof = 0;
					file->pos = 0;
					file->status = FS_STATUS_FOUND;
					file->first_block = file->current;
					file->device = fsys;
					file->parent_block = kfile->first_block;
					file->gid = AuCredGetGroupID(AURORA_GID_MISC_WORLD);
					if (pkDir->attrib & 0x10) {
						file->flags |= FS_FLAG_DIRECTORY;
						file->mode = FatModeFromAttrib(pkDir->attrib, file->filename);
					} else {
						file->flags |= FS_FLAG_GENERAL;
						file->mode = FatModeFromAttrib(pkDir->attrib, file->filename);
					}
					AuPmmngrReleasePage((uint64_t)V2P((size_t)buf));
					kfree(kfile);
					return file;
				}

				pkDir++;
			}
		}
	}

	AuPmmngrReleasePage((uint64_t)V2P((size_t)buf));
	kfree(file);
	if (kfile)
		kfree(kfile);
	return NULL;
}

extern bool isSyscall();

extern uint64_t read_sp();

AuVFSNode* FatLocateDir(AuVFSNode* fsys, const char* dir) {
	FatFS* fs = (FatFS*)fsys->device;
	AuVDisk* vdisk = (AuVDisk*)fs->vdisk;

	/* checked before allocating: the node used to leak on this return */
	if (!vdisk)
		return NULL;

	FatDir* dirent;

	char dos_file_name[11];

	FatToDOSFilename(dir, dos_file_name, 11);

	/* sectors come from the directory cache: no page allocated here, so
	 * nothing to leak when the name is not found (the old page leaked on
	 * every failed lookup) */
	uint32_t current_cluster = fs->__RootDirFirstCluster;
	while (current_cluster < 0x0FFFFFF8 && current_cluster != 0) {
		for (unsigned int sector = 0; sector < fs->__SectorPerCluster; sector++) {
			uint8_t* sec = FatDirSector(fs, FatClusterToSector32(fs, current_cluster) + sector);
			if (!sec)
				return NULL;

			dirent = (FatDir*)sec;
			for (int i = 0; i < 16; i++) {
				/* 0x00 marks the end of the directory */
				if (dirent->filename[0] == 0x00)
					return NULL;
				if (strncmp(dos_file_name, (char*)dirent->filename, 11) == 0) {
					AuVFSNode* file = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
					if (!file)
						return NULL;
					memset(file, 0, sizeof(AuVFSNode));
					strcpy(file->filename, dir);
					file->current = (dirent->first_cluster_hi_bytes << 16) | dirent->first_cluster;
					file->size = dirent->file_size;
					file->eof = 0;
					file->status = FS_STATUS_FOUND;
					file->close = 0;
					file->first_block = file->current;
					file->pos = 0;
					file->device = fsys;
					file->parent_block = current_cluster;
					file->open = 0;
					file->write = 0;
					file->create_file = 0;

					/* FAT32 doesn't has UID/GID value stored in file, so
					 * using global misc world gid
					 */
					file->gid = AuCredGetGroupID(AURORA_GID_MISC_WORLD);
					if (dirent->attrib & 0x10) {
						file->flags |= FS_FLAG_DIRECTORY;
						file->mode = FatModeFromAttrib(dirent->attrib, file->filename);
					} else {
						file->flags |= FS_FLAG_GENERAL;
						file->mode = FatModeFromAttrib(dirent->attrib, file->filename);
					}
					aa64_data_cache_clean_range(file, sizeof(AuVFSNode));
					return file;
				}
				dirent++;
			}
		}
		current_cluster = FatReadFAT(fsys, current_cluster);
	}

	return NULL;
}

//! Opens a file
//! @param filename -- name of the file
//! @example -- /EFI/BOOT/BOOTx64.efi
AuVFSNode* FatOpen(AuVFSNode* fsys, char* filename) {
	if (!fsys)
		return NULL;
	FatFS* _fs = (FatFS*)fsys->device;
	AuVFSNode* cur_dir = NULL;
	char* p = 0;
	bool root_dir = true;
	char* path = (char*)filename;

	//! any '\'s in path ?
	p = strchr(path, '/');
	if (!p) {
		//! nope, must be in root directory, search it
		cur_dir = FatLocateDir(fsys, path);

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
		for (i = 0; i < 15; i++) {
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
			cur_dir = FatLocateDir(fsys, pathname);
			root_dir = false;
		} else {
			//! search a sub directory instead for pathname
			cur_dir = FatLocateSubDir(fsys, cur_dir, pathname);
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
size_t FatGetClusterFor(AuVFSNode* fs, AuVFSNode* file, uint64_t offset) {
	FatFS* fatfs = (FatFS*)fs->device;
	size_t index = offset / fatfs->cluster_sz_in_bytes;

	/** previously get_cluster_for was iterating from first block instead
	 * of caching the last cluster read, this increased read time latency
	 * in larger files
	 */
	size_t start_index;
	uint32_t cluster;

	/** use the last resolved cluster as base and start iterating from here */
	if (file->last_resolved_cluster != 0 && index >= file->last_resolved_index) {
		start_index = file->last_resolved_index;
		cluster = file->last_resolved_cluster;
	} else {
		/** fallback to first cluster, if index < last_resolved_index */
		start_index = 0;
		cluster = file->first_block;
	}

	for (int i = start_index; i < index; i++)
		cluster = FatReadFAT(fs, cluster);
	if (!FAT_IS_EOC(cluster) && file->eof == 1)
		file->eof = 0;
	file->pos = offset;
	file->last_resolved_index = index;
	file->last_resolved_cluster = cluster;
	return cluster;
}

/**
 * @brief FatGetDiskBlock -- convert fat understood cluster to disk sector
 * @param fs -- Pointer to fat file system
 * @param file -- Pointer to file
 * @param fs_block -- FAT cluster number
 */
uint32_t FatGetDiskBlock(AuVFSNode* fs, AuVFSNode* file, uint64_t fs_block) {
	FatFS* fatfs = (FatFS*)fs->device;
	if (!fatfs)
		return 0;
	return FatClusterToSector32(fatfs, fs_block);
}

/*
* FatInitialise -- initialise the fat file system
* @param vdisk -- Pointer to vdisk structure
* @param mountname -- mount file system name
*/
AuVFSNode* FatInitialise(AuVDisk* vdisk, char* mountname) {
	uint64_t* buffer = (uint64_t*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	memset(buffer, 0, 4096);
	AuVDiskRead(vdisk, 0, 1, buffer);

	/* avoiding alignment issues */
	FatBPB bpb;
	memcpy(&bpb.jmp, (uint8_t*)buffer, 3);
	memcpy(&bpb.oemid, (uint8_t*)buffer + 0x03, 8);
	memcpy(&bpb.bytes_per_sector, (uint8_t*)buffer + 0x00B, 2);
	memcpy(&bpb.sectors_per_cluster, (uint8_t*)buffer + 0x00D, 1);
	memcpy(&bpb.reserved_sectors, (uint8_t*)buffer + 0x00E, 2);
	memcpy(&bpb.num_fats, (uint8_t*)buffer + 0x010, 1);
	memcpy(&bpb.num_dir_entries, (uint8_t*)buffer + 0x011, 2);
	memcpy(&bpb.total_sectors_short, (uint8_t*)buffer + 0x013, 2);
	memcpy(&bpb.media_type, (uint8_t*)buffer + 0x015, 1);
	memcpy(&bpb.sectors_per_fat, (uint8_t*)buffer + 0x016, 2);
	memcpy(&bpb.sectors_per_track, (uint8_t*)buffer + 0x018, 2);
	memcpy(&bpb.heads, (uint8_t*)buffer + 0x01A, 2);
	memcpy(&bpb.hidden_sectors, (uint8_t*)buffer + 0x01C, 4);
	memcpy(&bpb.large_sector_count, (uint8_t*)buffer + 0x020, 4);
	memcpy(&bpb.info.FAT32.sect_per_fat32, (uint8_t*)buffer + 0x024, 4);
	memcpy(&bpb.info.FAT32.falgs, (uint8_t*)buffer + 0x028, 2);
	memcpy(&bpb.info.FAT32.fat_version, (uint8_t*)buffer + 0x02A, 2);
	memcpy(&bpb.info.FAT32.root_dir_cluster, (uint8_t*)buffer + 0x02C, 4);
	memcpy(&bpb.info.FAT32.fs_info_sect, (uint8_t*)buffer + 0x030, 2);
	memcpy(&bpb.info.FAT32.backup_boot_sect, (uint8_t*)buffer + 0x032, 2);
	memcpy(&bpb.info.FAT32.signature, (uint8_t*)buffer + 0x042, 1);
	memcpy(&bpb.info.FAT32.vol_label, (uint8_t*)buffer + 0x047, 11);
	memcpy(&bpb.info.FAT32.sys_id, (uint8_t*)buffer + 0x052, 8);

	FatFS* fs = (FatFS*)kmalloc(sizeof(FatFS));
	memset(fs, 0, sizeof(FatFS));

	fs->bpb = 0;
	fs->vdisk = vdisk;

	AuTextOut("[aurora]: FAT32 OEM :");
	for (int i = 0; i < 7; i++) {
		AuTextOut("%c", bpb.oemid[i]);
		fs->oemid[i] = bpb.oemid[i];
	}
	AuTextOut("\r\n");
	fs->oemid[7] = '\0';
	fs->__FatBeginLBA = bpb.reserved_sectors;
	fs->__ClusterBeginLBA = bpb.reserved_sectors + (bpb.num_fats * bpb.info.FAT32.sect_per_fat32);
	fs->__SectorPerCluster = bpb.sectors_per_cluster;
	fs->__RootDirFirstCluster = bpb.info.FAT32.root_dir_cluster;
	fs->__RootSector = FatClusterToSector32(fs, fs->__RootDirFirstCluster);
	fs->__SectorPerFAT32 = bpb.info.FAT32.sect_per_fat32;
	fs->cluster_sz_in_bytes = (size_t)fs->__SectorPerCluster * bpb.bytes_per_sector;
	fs->__BytesPerSector = bpb.bytes_per_sector;
	fs->__LastIndexInFat = 0;
	fs->__LastIndexSector = 0;
	fs->__NumFats = bpb.num_fats;
	size_t _root_dir_sectors =
		((bpb.num_dir_entries * 32) + bpb.bytes_per_sector - 1) / bpb.bytes_per_sector;
	size_t _TotalSectors =
		(bpb.total_sectors_short == 0) ? bpb.large_sector_count : bpb.total_sectors_short;
	size_t fatsize =
		(bpb.sectors_per_fat == 0) ? bpb.info.FAT32.sect_per_fat32 : bpb.sectors_per_fat;
	size_t _dataSectors =
		_TotalSectors - (bpb.reserved_sectors + bpb.num_fats * fatsize + _root_dir_sectors);
	/* data clusters are numbered from 2, so valid numbers are below
	 * count + 2. This used to be all sectors / sectors-per-cluster, which
	 * counts the reserved and FAT areas too and let the allocator hand
	 * out clusters past the end of the volume. */
	fs->__TotalClusters = fs->__SectorPerCluster ?
		(unsigned int)(_dataSectors / fs->__SectorPerCluster) + 2 : 0;

	AuTextOut("[aurora]: fat32 sector/cluster : %d \r\n", fs->__SectorPerCluster);
	AuTextOut("[aurora]: fat32 bytes/sector: %d \r\n", fs->__BytesPerSector);
	AuTextOut("[aurora]: fat32 mounted at %s \r\n", mountname);

	if (_dataSectors < 4085)
		fs->fatType = FSTYPE_FAT12;
	else if (_dataSectors < 65525)
		fs->fatType = FSTYPE_FAT16;
	else if (_dataSectors < 268435445)
		fs->fatType = FSTYPE_FAT32;

	/* Every cluster-sized transfer in this driver goes through one 4K
	 * page (FatRead, FatClearCluster, file writes, callers' blocks), so a
	 * bigger cluster would write past it. mkfs.fat uses 8K-32K clusters
	 * on larger volumes: refuse to mount those instead of corrupting
	 * memory. */
	bool cluster_ok = fs->cluster_sz_in_bytes && fs->cluster_sz_in_bytes <= PAGE_SIZE;
	if (!cluster_ok)
		AuTextOut("[aurora]: fat32 cluster size %d not supported (max %d) \r\n",
				  (int)fs->cluster_sz_in_bytes, PAGE_SIZE);

	if (fs->fatType != FSTYPE_FAT32 || !cluster_ok) {
		/* was released without V2P, freeing the wrong physical page */
		AuPmmngrReleasePage((uint64_t)V2P((uint64_t)buffer));
		kfree(fs);
		return NULL;
	}

	/* allocated only once the volume is accepted, so the failure path
	 * above has nothing else to free (the scratch page used to leak) */
	fs->_scratchBuffer = (void*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	memset(fs->_scratchBuffer, 0, PAGE_SIZE);
	fs->__FatCache = (uint8_t*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	fs->__DirCache = (uint8_t*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	fs->__FatCacheValid = false;
	fs->__DirCacheValid = false;

	AuVFSNode* fsys = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(fsys, 0, sizeof(AuVFSNode));
	strcpy(fsys->filename, mountname);
	fsys->flags |= FS_FLAG_FILE_SYSTEM | FS_FLAG_FILE_SYSTEM_GENERAL;
	fsys->open = FatOpen;
	fsys->device = fs;
	fsys->read = FatReadFile;
	fsys->read_block = FatRead;
	fsys->remove_dir = FatRemoveDir;
	fsys->remove_file = FatFileRemove;
	fsys->write = FatWrite;
	fsys->create_dir = FatCreateDir;
	fsys->create_file = FatCreateFile;
	fsys->get_disk_block = FatGetDiskBlock;
	fsys->get_blockfor = FatGetClusterFor;
	fsys->opendir = FatOpenDir;
	fsys->read_dir = FatDirectoryRead;
	fsys->setattr = FatSetAttribute;
	vdisk->fsys = fsys;
	AuVFSAddFileSystem(fsys);
	AuVFSRegisterRoot(fsys);
	UARTDebugOut("[fat]: %s mounted at %s oem %s\r\n", vdisk->diskname,
				 mountname, fs->oemid);

	AuPmmngrReleasePage((uint64_t)V2P((uint64_t)buffer));

	return fsys;
}

/*
 * FatFormatDate -- returns fat formated date stamp
 */
uint16_t FatFormatDate() {
	return 0; // (uint16_t)((2000 + AuRTCGetYear() - 1980) << 9 | AuRTCGetMonth() << 5 | AuRTCGetDay());
}

/*
 * FatFormatTime -- returns fat formated time stamp
 */
uint16_t FatFormatTime() {
	return 0; // (uint16_t)(AuRTCGetHour() << 11 | AuRTCGetMinutes() << 5 | AuRTCGetSecond() / 2);
}
