/**
* @file Ext2.c
*
* BSD 2-Clause License
*
* Copyright (c) 2022-2023, Manas Kamal Choudhury
* All rights reserved.
* 
* Author:
*      Pranav Bisht, Pranav0bisht@gmail.com
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
#include <Fs/vfs.h>
#include <Fs/vdisk.h>
#include <Mm/kmalloc.h>
#include <Mm/pmmngr.h>
#include <Drivers/uart.h>
#include <string.h>
#include <aucon.h>
#include <Fs/Ext2/ext2.h>
#include <Fs/Ext2/ext2dir.h>
#include <_null.h>

int Ext2FreeBlock(Ext2Fs* fs, uint32_t block_num) {
	if (!fs || block_num == 0) return -1;

	uint32_t block_size = fs->block_size;
	uint32_t sector_per_block = block_size / 512;
	uint32_t first_data_block = (block_size == 1024) ? 1 : 0;
	uint32_t adjusted_block = block_num - first_data_block;
	uint32_t group = adjusted_block / fs->superblock->blocks_per_group;
	uint32_t relative_block = adjusted_block % fs->superblock->blocks_per_group;
	uint32_t byte_idx = relative_block / 8;
	uint8_t bit_idx = relative_block % 8;
	uint32_t bitmap_block = fs->block_desc[group].block_bitmap;
	uint64_t bitmap_lba = (uint64_t)bitmap_block * sector_per_block;

	uint8_t* bitmap_buf = (uint8_t*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	if (!bitmap_buf) return -1;

	AuVDiskRead((AuVDisk*)fs->vdisk, bitmap_lba, sector_per_block, (uint64_t*)bitmap_buf);

	bitmap_buf[byte_idx] &= ~(1 << bit_idx);

	AuVDiskWrite((AuVDisk*)fs->vdisk, bitmap_lba, sector_per_block, (uint64_t*)bitmap_buf);
	AuPmmngrReleasePage((void*)V2P((uint64_t)bitmap_buf));

	fs->superblock->free_blocks_count++;
	fs->block_desc[group].free_blocks_count++;

	Ext2FlushSuperblock(fs);
	Ext2FlushBgdt(fs);
	return 0;
}

int Ext2FreeInode(Ext2Fs* fs, uint32_t inode_num) {
	if (!fs || inode_num == 0) return -1;

	uint32_t block_size = fs->block_size;
	uint32_t sector_per_block = block_size / 512;
	uint32_t group = (inode_num - 1) / fs->inodes_per_group;
	uint32_t relative_inode = (inode_num - 1) % fs->inodes_per_group;
	uint32_t byte_idx = relative_inode / 8;
	uint8_t bit_idx = relative_inode % 8;
	uint32_t bitmap_block = fs->block_desc[group].inode_bitmap;
	uint64_t bitmap_lba = (uint64_t)bitmap_block * sector_per_block;

	uint8_t* bitmap_buf = (uint8_t*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	if (!bitmap_buf) return -1;

	AuVDiskRead((AuVDisk*)fs->vdisk, bitmap_lba, sector_per_block, (uint64_t*)bitmap_buf);

	bitmap_buf[byte_idx] &= ~(1 << bit_idx);

	AuVDiskWrite((AuVDisk*)fs->vdisk, bitmap_lba, sector_per_block, (uint64_t*)bitmap_buf);
	AuPmmngrReleasePage((void*)V2P((uint64_t)bitmap_buf));

	fs->superblock->free_inodes_count++;
	fs->block_desc[group].free_inodes_count++;

	Ext2FlushSuperblock(fs);
	Ext2FlushBgdt(fs);
	return 0;
}


/**
* Ext2FindEntry -- scans the directory data block for matching name string
* @param fs -- file system
* @param dir_inode -- parsed inode structure we are searching inside
* @param name -- the target string we are matching
*/
uint32_t Ext2FindEntry(Ext2Fs* fs, Ext2Inode* dir_inode, const char* name) {
	if (!fs || !dir_inode || !name) {
		AuTextOut("[Ext2]: parameters missing for directory scanning.\r\n");
		return 0;
	}

	uint32_t block_size = fs->block_size;
	uint32_t sector_per_block = block_size / 512;
	uint32_t target_len = strlen(name);

	uint64_t* buffer = (uint64_t*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	if (!buffer) {
		AuTextOut("[Ext2]: out of memory during directory scanning.\r\n");
		return 0;
	}

	for (int i = 0; i < 12; i++) {
		uint32_t physical_block = dir_inode->block[i];

		if (physical_block == 0)
			continue;

		uint64_t target_lba = physical_block * sector_per_block;
		memset(buffer, 0, 4096);
		AuVDiskRead((AuVDisk*)fs->vdisk, target_lba, sector_per_block, buffer);

		uint32_t current_pos = 0;

		while (current_pos < block_size) {
			Ext2Dir* entry = (Ext2Dir*)((uint8_t*)buffer + current_pos);

			if (entry->rec_len == 0) {
				AuPmmngrReleasePage((uint64_t)V2P((uint64_t)buffer));
				return 0;
			}
			if (entry->inode != 0 && entry->name_len == target_len) {
				if (strncmp(entry->name, name, entry->name_len) == 0) {
					uint32_t found_inode = entry->inode;
					AuPmmngrReleasePage((uint64_t)V2P((uint64_t)buffer));
					return found_inode;
				}
			}

			current_pos += entry->rec_len;
		}
	}

	AuPmmngrReleasePage((uint64_t)V2P((uint64_t)buffer));
	return 0;
};

/**
* Ext2ReadInode -- reads the inode and updates the provided new inode
* @param fs -- the filesystem
* @param inode_num -- inode number to read
* @param out_inode -- inode to place the readings
*/
int Ext2ReadInode(Ext2Fs* fs, uint32_t inode_num, Ext2Inode* out_inode) {
	if (!fs || !out_inode || inode_num == 0) {
		AuTextOut("[Ext2]: parameters missing for inode reading.\r\n");
		return -1;
	}

	Ext2Superblock* sb = fs->superblock;

	uint32_t adjusted_inode = inode_num - 1;
	uint32_t group = adjusted_inode / sb->inodes_per_group;
	uint32_t local_index = adjusted_inode % sb->inodes_per_group;
	uint32_t inode_byte_offset = local_index * fs->inode_size;
	uint32_t block_offset = inode_byte_offset / fs->block_size;
	uint32_t internal_byte_offset = inode_byte_offset % fs->block_size;
	uint32_t inode_table_base_block = fs->block_desc[group].inode_table;
	uint32_t target_physical_block = inode_table_base_block + block_offset;
	uint32_t sector_per_block = fs->block_size / 512;
	uint32_t target_lba = target_physical_block * sector_per_block;

	uint64_t* buffer = (uint64_t*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	if (!buffer) {
		AuTextOut("[Ext2]: out of memory during inode reading.\r\n");
		return -1;
	}

	memset(buffer, 0, 4096);

	AuVDiskRead((AuVDisk*)fs->vdisk, target_lba, sector_per_block, buffer);

	uint8_t* target_adress = (uint8_t*)buffer + internal_byte_offset;
	memcpy(out_inode, target_adress, sizeof(Ext2Inode));

	AuPmmngrReleasePage((uint64_t)V2P((uint64_t)buffer));

	return 0;
};

/**
* Ext2ReadBlockIndex -- reads the block index from a given block
* @param fs -- the filesystem
* @param block_id -- the block id to read from
* @param index -- the index within the block to read
*/
uint32_t Ext2ReadBlockIndex(Ext2Fs* fs, uint32_t block_id, uint32_t index) {
	if (!fs || block_id == 0) {
		AuTextOut("[Ext2]: parameters missing for block index reading.\r\n");
		return 0;
	}

	uint32_t sector_per_block = fs->block_size / 512;
	uint64_t target_lba = (uint64_t)block_id * sector_per_block;

	uint32_t* buffer = (uint32_t*)P2V((uint32_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	if (!buffer) {
		AuTextOut("[Ext2]: out of memory during block index reading.\r\n");
		return 0;
	}
	memset(buffer, 0, 4096);

	AuVDiskRead((AuVDisk*)fs->vdisk, target_lba, sector_per_block, (uint64_t*)buffer);
	uint32_t resolved_physical_block = buffer[index];

	AuPmmngrReleasePage((uint64_t)V2P((uint64_t)buffer));

	return resolved_physical_block;
};

/**
* Ext2Read -- reads data from a file in the ext2 filesystem
* @param node -- the filesystem node
* @param file -- the file node to read from
* @param buffer -- the buffer to store read data
* @param length -- the number of bytes to read
*/
size_t Ext2Read(AuVFSNode* node, AuVFSNode* file, uint64_t* buffer, uint32_t length) {
	if (!node || !file || !buffer || !length) {
		AuTextOut("[Ext2]: parameters missing for file reading.\r\n");
		return 0;
	}

	Ext2Fs* fs = (Ext2Fs*)node->device;
	Ext2Inode* inode = (Ext2Inode*)file->private_data;

	uint64_t current_pos = file->pos;

	if (current_pos >= file->size) {
		AuTextOut("[Ext2]: System reached EOF.\r\n");
		return 0;
	}
	if (current_pos + length > file->size)
		length = file->size - current_pos;

	uint32_t block_size = fs->block_size;
	uint32_t sector_per_block = block_size / 512;
	uint32_t bytes_read = 0;

	uint64_t* bounce_page = (uint64_t*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	if (!bounce_page) {
		AuTextOut("[Ext2]: out of memory during file reading.\r\n");
		return 0;
	}

	while (bytes_read < length) {
		uint64_t current_pos_offset = current_pos + bytes_read;
		uint64_t logical_block = current_pos_offset / block_size;
		uint32_t internal_offset = current_pos_offset % block_size;

		uint32_t physical_block = 0;

		if (logical_block < 12)
			physical_block = inode->block[logical_block];
		else if (logical_block < (12 + fs->pointers_per_block)) {
			uint32_t singly_block_id = inode->block[12];
			if (singly_block_id == 0)
				physical_block = 0;
			else {
				uint32_t single_index = logical_block - 12;
				physical_block = Ext2ReadBlockIndex(fs, singly_block_id, single_index);
			}
		} else if (logical_block < (12 + fs->pointers_per_block +
									(fs->pointers_per_block * fs->pointers_per_block))) {
			uint32_t doubly_block_id = inode->block[13];
			if (doubly_block_id == 0)
				physical_block = 0;
			else {
				uint32_t double_index = logical_block - (12 + fs->pointers_per_block);
				uint32_t first_level_index = double_index / fs->pointers_per_block;
				uint32_t second_level_index = double_index % fs->pointers_per_block;
				uint32_t first_level_physical =
					Ext2ReadBlockIndex(fs, doubly_block_id, first_level_index);
				if (first_level_physical == 0)
					physical_block = 0;
				else
					physical_block =
						Ext2ReadBlockIndex(fs, first_level_physical, second_level_index);
			}
		} else {
			uint32_t triply_block_id = inode->block[14];
			if (triply_block_id == 0)
				physical_block = 0;
			else {
				uint32_t triple_index =
					logical_block - (12 + fs->pointers_per_block +
									 (fs->pointers_per_block * fs->pointers_per_block));
				uint32_t first_level_index =
					triple_index / (fs->pointers_per_block * fs->pointers_per_block);
				uint32_t second_level_index =
					(triple_index / fs->pointers_per_block) % fs->pointers_per_block;
				uint32_t third_level_index = triple_index % fs->pointers_per_block;
				uint32_t first_level_physical =
					Ext2ReadBlockIndex(fs, triply_block_id, first_level_index);
				if (first_level_physical == 0)
					physical_block = 0;
				else {
					uint32_t second_level_physical =
						Ext2ReadBlockIndex(fs, first_level_physical, second_level_index);
					if (second_level_physical == 0)
						physical_block = 0;
					else
						physical_block =
							Ext2ReadBlockIndex(fs, second_level_physical, third_level_index);
				}
			}
		}

		if (physical_block == 0) {
			uint32_t chunk = block_size - internal_offset;
			if (chunk > (length - bytes_read))
				chunk = length - bytes_read;
			memset((uint8_t*)buffer + bytes_read, 0, chunk);
			bytes_read += chunk;
			continue;
		}

		uint64_t target_lba = physical_block * sector_per_block;
		memset(bounce_page, 0, 4096);
		AuVDiskRead((AuVDisk*)fs->vdisk, target_lba, sector_per_block, bounce_page);

		uint32_t chunk = block_size - internal_offset;
		if (chunk > (length - bytes_read))
			chunk = length - bytes_read;

		memcpy((uint8_t*)buffer + bytes_read, (uint8_t*)bounce_page + internal_offset, chunk);
		bytes_read += chunk;
	}

	AuPmmngrReleasePage((uint64_t)V2P((uint64_t)bounce_page));
	file->pos += bytes_read;

	return bytes_read;
};

/**
* Ext2Open -- acting internal path crawler
* @param fsys -- pointer to the file system
* @param path -- path to the file
*/
AuVFSNode* Ext2Open(AuVFSNode* fsys, char* path) {
	if (!fsys || !path) {
		AuTextOut("[Ext2]: parameters missing for file opening.\r\n");
		return NULL;
	}

	Ext2Fs* fs = (Ext2Fs*)fsys->device;

	// edge case opening root directory
	if (strlen(path) == 0 || strcmp(path, "/") == 0) {
		AuVFSNode* root_session = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
		if (!root_session) {
			AuTextOut("[Ext2]: out of memory during root directory opening.\r\n");
			return NULL;
		}
		memset(root_session, 0, sizeof(AuVFSNode));

		Ext2Inode* cached_inode = (Ext2Inode*)kmalloc(sizeof(Ext2Inode));
		if (cached_inode) {
			if (Ext2ReadInode(fs, 2, cached_inode) == 0) {
				root_session->private_data = cached_inode;
				root_session->size = cached_inode->size;
			}
		}

		strcpy(root_session->filename, fsys->filename);
		root_session->flags = FS_FLAG_DIRECTORY | FS_FLAG_FILE_SYSTEM;
		root_session->first_block = 2;
		root_session->pos = 0;
		root_session->read = Ext2Read;
		root_session->device = fsys;
		return root_session;
	}

	uint32_t current_inode_number = 2;
	Ext2Inode current_inode;

	char path_local[256];
	strncpy(path_local, path, sizeof(path_local) - 1);
	path_local[sizeof(path_local) - 1] = '\0';

	char* token = path_local;
	char* nxt_token = NULL;

	while (token && *token != '\0') {
		while (*token == '/')
			token++;
		if (*token == '\0')
			break;
		nxt_token = strchr(token, '/');
		if (nxt_token) {
			*nxt_token = '\0';
			nxt_token++;
		}

		if (Ext2ReadInode(fs, current_inode_number, &current_inode) != 0) {
			AuTextOut("[Ext2]: failed to read the inode sector during path sweep.\r\n");
			return NULL;
		}

		if (!(current_inode.mode & EXT2_S_IFDIR)) {
			AuTextOut("[Ext2]: path element is not a directory container.\r\n");
			return NULL;
		}

		uint32_t found_inode_num = Ext2FindEntry(fs, &current_inode, token);
		if (found_inode_num == 0) {
			AuTextOut("[Ext2]: path element not found in directory.\r\n");
			return NULL;
		}

		current_inode_number = found_inode_num;
		token = nxt_token;
	}

	if (Ext2ReadInode(fs, current_inode_number, &current_inode) != 0) {
		AuTextOut("[Ext2]: failed to read the final inode sector during path sweep.\r\n");
		return NULL;
	}

	AuVFSNode* file_session = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	if (!file_session) {
		AuTextOut("[Ext2]: out of memory during file session creation.\r\n");
		return NULL;
	}
	memset(file_session, 0, sizeof(AuVFSNode));

	file_session->first_block = current_inode_number;
	file_session->size = current_inode.size;

	char* raw_filename = strrchr(path, '/');
	char* final_name = raw_filename ? (raw_filename + 1) : path;
	strncpy(file_session->filename, final_name, 31);
	file_session->filename[31] = '\0';

	if (current_inode.mode & EXT2_S_IFDIR)
		file_session->flags |= FS_FLAG_DIRECTORY;
	else if (current_inode.mode & EXT2_S_IFREG)
		file_session->flags |= FS_FLAG_GENERAL;

	Ext2Inode* cached_inode = (Ext2Inode*)kmalloc(sizeof(Ext2Inode));
	if (cached_inode) {
		memcpy(cached_inode, &current_inode, sizeof(Ext2Inode));
		file_session->private_data = cached_inode;
	}

	file_session->pos = 0;
	file_session->eof = 0;
	file_session->fileCopyCount = 1;

	file_session->read = Ext2Read;
	file_session->open = NULL;
	file_session->device = fsys;

	return file_session;
};

/**
* Ext2Initialise -- initialize the ext2 file system
* @param vdisk -- Pointer to vdisk structure
* @param mountname -- mount file system name
*/
AuVFSNode* Ext2Initialise(AuVDisk* vdisk, char* mountname) {
	uint64_t* buffer = (uint64_t*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	memset(buffer, 0, 4096);

	AuVDiskRead(vdisk, 2, 2, buffer);

	Ext2Fs* fs = (Ext2Fs*)kmalloc(sizeof(Ext2Fs));
	if (!fs) {
		AuTextOut("[ext2]: out of memory during file system initialization.\r\n");
		kfree(fs);
		return NULL;
	}
	memset(fs, 0, sizeof(Ext2Fs));
	fs->vdisk = vdisk;

	fs->superblock = (Ext2Superblock*)kmalloc(sizeof(Ext2Superblock));
	if (!fs->superblock) {
		AuTextOut("[ext2]: out of memory during superblock allocation.\r\n");
		kfree(fs->superblock);
		return NULL;
	}
	memcpy(fs->superblock, buffer, sizeof(Ext2Superblock));

	if (fs->superblock->magic != EXT2_SUPER_BLOCK_MAGIC) {
		AuPmmngrReleasePage((uint64_t)V2P((uint64_t)buffer));
		kfree(fs->superblock);
		kfree(fs);
		AuTextOut("[ext2]: EXT2 magic mismatch \n");
		for (;;)
			;
		return NULL;
	}

	fs->block_size = 1024 << fs->superblock->log_block_size;
	fs->pointers_per_block = fs->block_size / 4;
	fs->inode_size = (fs->superblock->inode_size == 0) ? 128 : fs->superblock->inode_size;

	fs->block_group_count = fs->superblock->blocks_count / fs->superblock->blocks_per_group;
	if ((fs->superblock->blocks_per_group * fs->block_group_count) < fs->superblock->blocks_count) {
		fs->block_group_count += 1;
	}
	fs->inodes_per_group = fs->superblock->inodes_count / fs->block_group_count;

	AuTextOut("[ext2]: EXT2 Volume Label: %s \r\n", fs->superblock->volume_name);
	AuTextOut("[ext2]: EXT2 block size: %d bytes \r\n", fs->block_size);
	AuTextOut("[ext2]: EXT2 inodes per group: %d \r\n", fs->inodes_per_group);
	AuTextOut("[ext2]: EXT2 mounted at %s \r\n", mountname);

	fs->bgd_offset = (fs->block_size > 1024) ? 1 : 2;

	uint32_t bgdt_bytes = fs->block_group_count * sizeof(Ext2BlockDescriptor);
	fs->bgd_block_span = bgdt_bytes / fs->block_size;
	if ((fs->bgd_block_span * fs->block_size) < bgdt_bytes) {
		fs->bgd_block_span += 1;
	}

	fs->block_desc = (Ext2BlockDescriptor*)kmalloc(fs->block_size * fs->bgd_block_span);
	memset(fs->block_desc, 0, fs->block_size * fs->bgd_block_span);

	uint32_t sectors_per_block = fs->block_size / 512;
	for (uint32_t i = 0; i < fs->bgd_block_span; ++i) {
		memset(buffer, 0, 4096);
		uint64_t target_lba = (fs->bgd_offset + i) * sectors_per_block;

		AuVDiskRead(vdisk, target_lba, sectors_per_block, buffer);

		memcpy((uint8_t*)fs->block_desc + (i * fs->block_size), buffer, fs->block_size);
	}

	AuVFSNode* fsys = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(fsys, 0, sizeof(AuVFSNode));

	strcpy(fsys->filename, mountname);
	fsys->flags |= FS_FLAG_FILE_SYSTEM | FS_FLAG_DIRECTORY;
	fsys->first_block = 2;
	fsys->device = fs;

	fsys->open = Ext2Open;
	fsys->opendir = Ext2Open;
	fsys->read = Ext2Read;
	fsys->read_dir = Ext2ReadDir;

	fsys->write = NULL;
	fsys->create_dir = NULL;
	fsys->create_file = NULL;

	vdisk->fsys = fsys;
	fs->root_node = fsys;

	AuVFSAddFileSystem(fsys);
	AuVFSRegisterRoot(fsys);

	AuPmmngrReleasePage((uint64_t)V2P((uint64_t)buffer));

	return fsys;
}