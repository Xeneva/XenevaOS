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

#include <Fs/Fat/Fat.h>
#include <Fs/vdisk.h>
#include <Fs/vfs.h>
#include <Mm/kmalloc.h>
#include <Mm/pmmngr.h>
#include <Mm/vmmngr.h>
#include <string.h>
#include <_null.h>
#include <aucon.h>
#include <Drivers/uart.h>

extern AuVFSNode* __RootFS;

/**
 * TODO: File re updating its content, expanding the cluster chain
 * or reupdating it's previous cluster chain with expand
 */

/*
 * FatFileGetParent -- Returns the parent directory file
 * for a new file
 * @param fsys -- Pointer to file system
 * @param filename -- file path and name to look upon
 */
AuVFSNode* FatFileGetParent(AuVFSNode* fsys, const char* filename) {
	if (!fsys || !filename)
		return NULL;
	FatFS* _fs = (FatFS*)fsys->device;
	if (!_fs)
		return NULL;

	AuVFSNode* retfile = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	if (!retfile)
		return NULL;
	memset(retfile, 0, sizeof(AuVFSNode));

	/* Find last slash to separate parent directory from filename */
	const char* last_slash = strrchr(filename, '/');
	if (!last_slash || last_slash == filename) {
		/* File is in root directory */
		retfile->current = _fs->__RootDirFirstCluster;
		retfile->first_block = _fs->__RootDirFirstCluster;
		retfile->flags = FS_FLAG_DIRECTORY;
		retfile->device = fsys;
		return retfile;
	}

	/* Copy directory portion into dirpath buffer */
	char dirpath[128];
	size_t dirlen = (size_t)(last_slash - filename);
	if (dirlen >= sizeof(dirpath)) {
		kfree(retfile);
		return NULL;
	}
	memcpy(dirpath, filename, dirlen);
	dirpath[dirlen] = '\0';

	char* p = strchr(dirpath, '/');
	if (p)
		p++;
	bool is_root = true;

	if (fsys != __RootFS && p)
		p += 2;

	AuVFSNode* parent = NULL;
	while (p && *p) {
		char pathname[16];
		int i = 0;
		for (i = 0; i < 15; i++) {
			if (p[i] == '/' || p[i] == '\0')
				break;
			pathname[i] = p[i];
		}
		pathname[i] = 0;

		if (is_root) {
			parent = FatLocateDir(fsys, pathname);
			if (!parent) {
				kfree(retfile);
				return NULL;
			}
			is_root = false;
		} else {
			AuVFSNode* next_dir = FatLocateSubDir(fsys, parent, pathname);
			if (!next_dir) {
				kfree(retfile);
				return NULL;
			}
			parent = next_dir;
		}

		char* next_p = strchr(p, '/');
		if (next_p)
			p = next_p + 1;
		else
			break;
	}

	if (parent) {
		memcpy(retfile, parent, sizeof(AuVFSNode));
		retfile->current = retfile->first_block;
		kfree(parent);
		return retfile;
	}

	/* Default fallback to root directory */
	retfile->current = _fs->__RootDirFirstCluster;
	retfile->first_block = _fs->__RootDirFirstCluster;
	retfile->flags = FS_FLAG_DIRECTORY;
	retfile->device = fsys;
	return retfile;
}
/*
 * FatCreateFile -- Creates a blank file
 * @param fsys -- Pointer to file system
 * @param parent_cluster -- Parent cluster, if it's
 * 0 then parent_cluster equals to root_cluster
 * @param filename -- name of the file
 */
AuVFSNode* FatCreateFile(AuVFSNode* fsys, char* filename) {
	if (!fsys)
		return NULL;
	FatFS* _fs = (FatFS*)fsys->device;

	/* A root path has no parent directory component. FatFileGetParent looks
	 * the filename up as a directory and returns NULL. Same fallback as
	 * FatCreateDir: create the entry in the root cluster. */
	AuVFSNode* parent = FatFileGetParent(fsys, filename);
	uint32_t parent_cluster = _fs->__RootDirFirstCluster;
	if (parent && parent->current)
		parent_cluster = parent->current;

	AuVFSNode* file = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(file, 0, sizeof(AuVFSNode));

	uint64_t* buff = (uint64_t*)P2V((size_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	memset(buff, 0, PAGE_SIZE);

	char* path = (char*)filename;
	char* p = strchr(path, '/');
	if (p)
		p++;

	char extract[16];
	memset(extract, 0, 16);
	while (p) {
		int i = 0;
		for (i = 0; i < 16; i++) {
			if (p[i] == '/' || p[i] == '\0')
				break;
			extract[i] = p[i];
		}
		p = strchr(p + 1, '/');
		if (p) {
			p++;
			memset(extract, 0, 16);
		}
	}

	char fname[11];
	memset(fname, 0, 11);
	FatToDOSFilename(extract, fname, 11);
	//fname[11] = 0;

	while (1) {
		for (int j = 0; j < _fs->__SectorPerCluster; j++) {
			memset(buff, 0, 512);
			AuVDiskRead(_fs->vdisk, FatClusterToSector32(_fs, parent_cluster) + j, 1, buff);

			FatDir* dirent = (FatDir*)buff;
			for (int i = 0; i < 16; i++) {
				if ((dirent->filename[0] == 0x00) || (dirent->filename[0] == 0xE5)) {
					memcpy(dirent->filename, fname, 11);

					uint32_t cluster = FatFindFreeCluster(fsys);
					FatAllocCluster(fsys, cluster, FAT_EOC_MARK);
					FatClearCluster(fsys, cluster);

					dirent->attrib = FAT_ATTRIBUTE_ARCHIVE;
					dirent->first_cluster = (uint16_t)(cluster & 0x0000FFFF);
					dirent->first_cluster_hi_bytes = (uint16_t)((cluster & 0x0FFF0000) >> 16);
					dirent->date_created = FatFormatDate();
					dirent->time_created = FatFormatTime();
					dirent->last_wrt_date = dirent->date_created;
					dirent->last_wrt_time = dirent->time_created;
					dirent->date_last_accessed = dirent->date_created;
					dirent->file_size = 0; //by default, 0 bytes

					uint64_t sector = FatClusterToSector32(_fs, parent_cluster);

					aa64_data_cache_clean_range(buff, PAGE_SIZE);
					AuVDiskWrite(_fs->vdisk, sector + j, 1, buff);

					AuPmmngrReleasePage((uint64_t)V2P((size_t)buff));

					strcpy(file->filename, extract);
					file->size = dirent->file_size;
					file->eof = 0;
					file->pos = 0;
					file->current = cluster;
					file->first_block = cluster;
					file->parent_block = parent_cluster;
					file->device = fsys;
					file->flags |= FS_FLAG_GENERAL;
					file->status = FS_STATUS_FOUND;
					kfree(parent);
					return file;
				}
				dirent++;
			}
		}
		parent_cluster = FatReadFAT(fsys, parent_cluster);
		if (FAT_IS_EOC(parent_cluster))
			break;
		/* actually, here we need to allocate a new
		 * cluster and write it to root cluster
		 * so that root directory expands
		 */
	}
	kfree(parent);
	kfree(file);
	return NULL;
}

/*
 * FatFileUpdateSize -- updates the current file size
 * @param fsys -- Pointer to file system
 * @param file -- Pointer to file
 * @param size -- size in bytes
 */
void FatFileUpdateSize(AuVFSNode* fsys, AuVFSNode* file, size_t size) {
	if (!fsys)
		return;
	if (!file)
		return;
	if (!file->parent_block)
		return;
	FatFS* _fs = (FatFS*)fsys->device;

	uint32_t dir_cluster = file->parent_block;
	char fname[11];
	memset(fname, 0, 11);
	FatToDOSFilename(file->filename, fname, 11);
	//fname[11] = 0;
	uint64_t* buff = (uint64_t*)P2V((size_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	memset(buff, 0, PAGE_SIZE);

	while (1) {
		for (int j = 0; j < _fs->__SectorPerCluster; j++) {
			memset(buff, 0, 512);
			AuVDiskRead(_fs->vdisk, FatClusterToSector32(_fs, dir_cluster) + j, 1, buff);

			FatDir* dirent = (FatDir*)buff;
			for (int i = 0; i < 16; i++) {
				char name[11];
				memcpy(name, dirent->filename, 11);
				//name[11] = 0;

				/* both sides are fixed 11-byte DOS names with no
				 * terminator; strcmp past the end reads stack garbage
				 * and matches only by luck --axiss */
				if (memcmp(fname, name, 11) == 0) {
					dirent->file_size += size;
					aa64_data_cache_clean_range(buff, PAGE_SIZE);
					AuVDiskWrite(_fs->vdisk, FatClusterToSector32(_fs, dir_cluster) + j, 1, buff);
					AuPmmngrReleasePage((uint64_t)V2P((size_t)buff));
					return;
				}
				dirent++;
			}
		}

		dir_cluster = FatReadFAT(fsys, dir_cluster);
		if (FAT_IS_EOC(dir_cluster)) {
			break;
		}
	}
	AuPmmngrReleasePage((uint64_t)V2P((size_t)buff));
	return;
}

/*
* FatFileUpdateFilename -- updates the current file name
* @param fsys -- Pointer to file system
* @param file -- Pointer to file
* @param newname -- new filename
*/
int FatFileUpdateFilename(AuVFSNode* fsys, AuVFSNode* file, char* newname) {
	if (!fsys)
		return -1;
	if (!file)
		return -1;
	/*if (!file->parent_block)
	return;*/
	FatFS* _fs = (FatFS*)fsys->device;

	uint32_t dir_cluster = file->parent_block;

	char fname[11];
	memset(fname, 0, 11);
	FatToDOSFilename(file->filename, fname, 11);
	//fname[11] = 0;

	char nname[11];
	memset(nname, 0, 11);
	FatToDOSFilename(newname, nname, 11);
	//nname[11] = 0;

	uint64_t* buff = (uint64_t*)P2V((size_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	memset(buff, 0, PAGE_SIZE);

	while (1) {
		for (int j = 0; j < _fs->__SectorPerCluster; j++) {
			memset(buff, 0, 512);
			AuVDiskRead(_fs->vdisk, FatClusterToSector32(_fs, dir_cluster) + j, 1, buff);

			FatDir* dirent = (FatDir*)buff;
			for (int i = 0; i < 16; i++) {
				char name[11];
				memcpy(name, dirent->filename, 11);
				//name[11] = 0;

				if (strcmp(name, fname) == 0) {
					memcpy(dirent->filename, nname, 11);
					AuVDiskWrite(_fs->vdisk, FatClusterToSector32(_fs, dir_cluster) + j, 1, buff);
					AuPmmngrReleasePage((uint64_t)V2P((size_t)buff));
					return 0;
				}
				dirent++;
			}
		}

		dir_cluster = FatReadFAT(fsys, dir_cluster);
		if (FAT_IS_EOC(dir_cluster))
			break;
	}

	return -1;
}

/*
 * FatFileWriteContent -- write contents to fat file (4kib)
 * @param fsys -- Pointer to file system node
 * @param first_cluster -- first cluster of the file
 * @param buffer -- buffer to write
 */
void FatFileWriteContent(AuVFSNode* fsys, AuVFSNode* file, uint64_t* buffer) {
	if (!fsys)
		return;

	FatFS* _fs = (FatFS*)fsys->device;

	uint64_t* buff = (uint64_t*)P2V((size_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	memset(buff, 0, PAGE_SIZE);

	uint32_t cluster = file->current;
	if (file->eof) {
		/* write the new cluster value to
		* old cluster */
		uint32_t new_cluster = FatFindFreeCluster(fsys);
		FatAllocCluster(fsys, cluster, new_cluster);
		FatAllocCluster(fsys, new_cluster, FAT_EOC_MARK);
		FatClearCluster(fsys, new_cluster);
		uint32_t clust_val = FatReadFAT(fsys, cluster);
		cluster = new_cluster;
		file->eof = 0;
	}

	/* EOC marks span 0x0FFFFFF8-0x0FFFFFFF; equality against FAT_EOC_MARK
	 * alone misses the 0x0FFFFFFF terminator mkfs/mtools write --axiss */
	if (!FAT_IS_EOC(cluster) && (cluster != (FAT_BAD_CLUSTER & 0x0fffffff))) {
		memcpy(buff, buffer, PAGE_SIZE);
		aa64_data_cache_clean_range(buff, PAGE_SIZE);
		AuVDiskWrite(_fs->vdisk, FatClusterToSector32(_fs, cluster), _fs->__SectorPerCluster, buff);
		file->pos++;
	}
	uint32_t return_cluster = FatReadFAT(fsys, cluster);
	if (FAT_IS_EOC(return_cluster)) {
		file->eof = 1;
	} else
		cluster = return_cluster;

	file->current = cluster;
	file->size = file->pos * _fs->__SectorPerCluster * _fs->__BytesPerSector;
	size_t sz = file->size;

	AuPmmngrReleasePage((uint64_t)V2P((size_t)buff));
}

/*
 * FatFileWriteDone -- sets the current position
 * to first cluster
 * @param file -- Pointer to file
 */
void FatFileWriteDone(AuVFSNode* file) {
	file->current = file->first_block;
	file->eof = 0;
	file->pos = 0;
}

/**
 * @brief FatWrite -- write callback
 * @param fsys -- pointer to file system
 * @param file -- pointer to file
 * @param buffer -- buffer to write
 * @param length -- bytes needed to write
 */
size_t FatWrite(AuVFSNode* fsys, AuVFSNode* file, uint64_t* buffer, uint32_t length) {
	if (!fsys)
		return 0;
	FatFS* _fs = (FatFS*)fsys->device;

	size_t num_cluster = length / ((_fs->__BytesPerSector) * _fs->__SectorPerCluster) +
						 ((length % (_fs->__BytesPerSector * _fs->__SectorPerCluster) ? 1 : 0));

	/* buffer is a uint64_t*, so a plain `buffer += clusterSize` advanced
	 * 8x too far since pointer arithmetic scales by sizeof(uint64_t),
	 * corrupted every cluster after the first on any write > 4KB --axiss */
	uint8_t* byte_buffer = (uint8_t*)buffer;
	for (int i = 0; i < num_cluster; i++) {
		FatFileWriteContent(fsys, file, (uint64_t*)byte_buffer);
		byte_buffer += (_fs->__BytesPerSector * _fs->__SectorPerCluster);
	}

	FatFileUpdateSize(fsys, file, length);
	//FatFileWriteDone(file);
	return length;
}
/**
 * @brief FatFileClearDirEntry -- clears an entry of a directory
 * @param fsys -- Pointer to file system node
 * @param file -- Pointer to file
 */
int FatFileClearDirEntry(AuVFSNode* fsys, AuVFSNode* file) {
	if (!fsys || !file)
		return -1;

	FatFS* _fs = (FatFS*)fsys->device;
	if (!_fs || !_fs->vdisk)
		return -1;

	uint32_t dir_clust = file->parent_block;
	if (!dir_clust) {
		UARTDebugOut("FatFileClearDirEntry: no parent directory %x \r\n", dir_clust);
		return -1;
	}

	const char* base = file->filename;
	char* slash = strrchr(base, '/');
	if (slash)
		base = slash + 1;

	char fname[11];
	memset(fname, 0, 11);
	FatToDOSFilename(base, fname, 11);

	uint64_t* buff = (uint64_t*)P2V((size_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	if (!buff)
		return -1;

	while (dir_clust >= 2 && (!(_fs->__TotalClusters) || dir_clust < _fs->__TotalClusters) && !FAT_IS_EOC(dir_clust)) {
		for (int j = 0; j < _fs->__SectorPerCluster; j++) {
			memset(buff, 0, PAGE_SIZE);
			AuVDiskRead(_fs->vdisk, FatClusterToSector32(_fs, dir_clust) + j, 1, buff);

			FatDir* dirent = (FatDir*)buff;
			for (int i = 0; i < 16; i++) {
				if (dirent->filename[0] == 0x00) {
					// End of directory entries
					AuPmmngrReleasePage((uint64_t)V2P((size_t)buff));
					return -1;
				}
				if ((uint8_t)dirent->filename[0] == 0xE5) {
					dirent++;
					continue;
				}
				if ((dirent->attrib & FAT_ATTRIBUTE_LONG_NAME) == FAT_ATTRIBUTE_LONG_NAME) {
					dirent++;
					continue;
				}
				if (memcmp(dirent->filename, fname, 11) == 0) {
					dirent->filename[0] = 0xE5;
					aa64_data_cache_clean_range(buff, PAGE_SIZE);
					AuVDiskWrite(_fs->vdisk, FatClusterToSector32(_fs, dir_clust) + j, 1, buff);
					AuPmmngrReleasePage((uint64_t)V2P((size_t)buff));
					return 0;
				}
				dirent++;
			}
		}

		uint32_t next_dir_clust = FatReadFAT(fsys, dir_clust);
		if (FAT_IS_EOC(next_dir_clust) || next_dir_clust < 2)
			break;
		dir_clust = next_dir_clust;
	}

	AuPmmngrReleasePage((uint64_t)V2P((size_t)buff));
	return -1;
}

/**
 * @brief FatFileRemove -- remove a file
 * @param fsys -- Pointer to file
 * @param file -- file to remove
 */
int FatFileRemove(AuVFSNode* fsys, AuVFSNode* file) {
	if (!fsys || !file)
		return -1;

	FatFS* _fs = (FatFS*)fsys->device;
	if (!_fs || !_fs->vdisk)
		return -1;

	/* First, clear the directory entry from the parent directory */
	int status = FatFileClearDirEntry(fsys, file);
	if (status != 0) {
		UARTDebugOut("FatFileRemove: failed to clear directory entry for %s\r\n", file->filename);
		return -1;
	}

	/* Deallocate cluster chain in FAT table */
	uint32_t cluster = file->first_block;
	if (cluster >= 2 && (!(_fs->__TotalClusters) || cluster < _fs->__TotalClusters) && !FAT_IS_EOC(cluster)) {
		while (cluster >= 2 && (!(_fs->__TotalClusters) || cluster < _fs->__TotalClusters) && !FAT_IS_EOC(cluster)) {
			uint32_t next_cluster = FatReadFAT(fsys, cluster);
			FatAllocCluster(fsys, cluster, 0x00);
			if (FAT_IS_EOC(next_cluster) || next_cluster < 2 || (_fs->__TotalClusters && next_cluster >= _fs->__TotalClusters))
				break;
			cluster = next_cluster;
		}
	}

	return 0;
}
