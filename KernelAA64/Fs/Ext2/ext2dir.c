#include <Fs/vfs.h>
#include <Fs/vdisk.h>
#include <Fs/Ext2/ext2file.h>
#include <Fs/Ext2/ext2.h>
#include <string.h>
#include <Mm/pmmngr.h>
#include <aucon.h>
#include <_null.h>


#define EXT2_DIR_REC_LEN(name_len) (((8 + (name_len)) + 3) & ~3)


uint32_t Ext2AllocInode(Ext2Fs* fs) {
    if (!fs || !fs->superblock) return 0;

    if (fs->superblock->free_inodes_count == 0) {
        AuTextOut("[Ext2]: no free inodes available on volume.\r\n");
        return 0;
    }

    uint32_t block_size = fs->block_size;
	uint32_t sector_per_block = block_size / 512;
	uint32_t group_count = fs->block_group_count;

    uint8_t* bitmap_buffer = (uint8_t*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
    if (!bitmap_buffer) return 0;

    uint32_t allocated_inode_id = 0;
	uint32_t target_group = 0;

    for (uint32_t g = 0; g < group_count; g++) {
        if (fs->block_desc->free_inodes_count == 0) continue;

        uint32_t bitmap_block = fs->block_desc->inode_bitmap;
        uint32_t bitmap_lba = (uint64_t)bitmap_block * sector_per_block;

        memset(bitmap_buffer, 0, block_size);
        AuVDiskRead((AuVDisk*)fs->vdisk, bitmap_lba, sector_per_block, (uint64_t*)bitmap_buffer);

        uint32_t bytes_to_scan = fs->inodes_per_group / 8;
		if (bytes_to_scan > block_size) bytes_to_scan = block_size;

        for (uint32_t byte_index = 0; byte_index < bytes_to_scan; byte_index++) {
            if (bitmap_buffer[byte_index] == 0xFF) continue;
            for (uint8_t bit_index = 0; bit_index < 8; bit_index++) {
                if (!(bitmap_buffer[byte_index] & (1 << bit_index))) {
                    uint32_t relative_inode = (byte_index * 8) + bit_index + 1;
					allocated_inode_id = (g * fs->inodes_per_group) + relative_inode;

					bitmap_buffer[byte_index] |= (1 << bit_index);

					AuVDiskWrite((AuVDisk*)fs->vdisk, bitmap_lba, sector_per_block, (uint64_t*)bitmap_buffer);
					target_group = g;
					goto success;
                }
            }
        }
    }
success:
     AuPmmngrReleasePage((void*)V2P((uint64_t)bitmap_buffer));

	if (allocated_inode_id == 0) return 0;

	// Update metadata counters
	fs->superblock->free_inodes_count--;
	fs->block_desc[target_group].free_inodes_count--;

	Ext2FlushSuperblock(fs);
	Ext2FlushBgdt(fs);

	return allocated_inode_id;
};

int Ext2AddDirEntry(Ext2Fs* fs, Ext2Inode* parent_inode, uint32_t parent_inode_num, uint32_t child_inode_num, const char* child_name, uint8_t file_type) {
	if (!fs || !parent_inode || !child_name) return -1;

	uint32_t block_size = fs->block_size;
	uint32_t sector_per_block = block_size / 512;
	uint32_t name_len = strlen(child_name);
	uint16_t required_rec_len = EXT2_DIR_REC_LEN(name_len);

	uint8_t* block_buf = (uint8_t*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	if (!block_buf) return -1;

	for (uint32_t i = 0; i < 12; i++) {
		uint32_t physical_block = parent_inode->block[i];

		if (physical_block == 0) {
			physical_block = Ext2AllocBlock(fs);
			if (physical_block == 0) {
				AuPmmngrReleasePage((void*)V2P((uint64_t)block_buf));
				return -1;
			}

			parent_inode->block[i] = physical_block;
			parent_inode->size += block_size;
			parent_inode->blocks = (parent_inode->size + 511) / 512;
			Ext2InodeWrite(fs, parent_inode_num, parent_inode);

			memset(block_buf, 0, block_size);
			Ext2Dir* new_entry = (Ext2Dir*)block_buf;
			new_entry->inode = child_inode_num;
			new_entry->rec_len = block_size;
			new_entry->name_len = name_len;
			new_entry->file_type = file_type;
			memcpy(new_entry->name, (void*)child_name, name_len);

			AuVDiskWrite((AuVDisk*)fs->vdisk, (uint64_t)physical_block * sector_per_block, sector_per_block, (uint64_t*)block_buf);
			AuPmmngrReleasePage((void*)V2P((uint64_t)block_buf));
			return 0;
		}

		uint64_t target_lba = (uint64_t)physical_block * sector_per_block;
		memset(block_buf, 0, block_size);
		AuVDiskRead((AuVDisk*)fs->vdisk, target_lba, sector_per_block, (uint64_t*)block_buf);

		uint32_t current_pos = 0;
		while (current_pos < block_size) {
			Ext2Dir* entry = (Ext2Dir*)(block_buf + current_pos);

			if (entry->rec_len == 0) break;

			uint16_t actual_rec_len = EXT2_DIR_REC_LEN(entry->name_len);

			if (entry->rec_len - actual_rec_len >= required_rec_len) {
				uint16_t old_rec_len = entry->rec_len;

				entry->rec_len = actual_rec_len;

				Ext2Dir* new_entry = (Ext2Dir*)(block_buf + current_pos + actual_rec_len);
				new_entry->inode = child_inode_num;
				new_entry->rec_len = old_rec_len - actual_rec_len;
				new_entry->name_len = name_len;
				new_entry->file_type = file_type;
				memcpy(new_entry->name, (void*)child_name, name_len);

				AuVDiskWrite((AuVDisk*)fs->vdisk, target_lba, sector_per_block, (uint64_t*)block_buf);
				 AuPmmngrReleasePage((void*)V2P((uint64_t)block_buf));
				return 0;
			}

			current_pos += entry->rec_len;
		}
	}

	AuPmmngrReleasePage((void*)V2P((uint64_t)block_buf));
	return -1;
};

int Ext2Mkdir(AuVFSNode* parent_node, const char* name, uint16_t permissions) {
	if (!parent_node || !name) return -1;

	Ext2Fs* fs = (Ext2Fs*)parent_node->device;
	Ext2Inode* parent_inode = (Ext2Inode*)parent_node->private_data;

	if (!fs || !parent_inode) return -1;

	if (!(parent_node->flags & FS_FLAG_DIRECTORY)) {
		AuTextOut("[Ext2 Mkdir]: Parent node is not a directory.\r\n");
		return -1;
	}

	if (Ext2FindEntry(fs, parent_inode, name) != 0) {
		AuTextOut("[Ext2 Mkdir]: Directory entry already exists.\r\n");
		return -1;
	}

	uint32_t new_inode_num = Ext2AllocInode(fs);
	if (new_inode_num == 0) return -1;

	uint32_t new_block_num = Ext2AllocBlock(fs);
	if (new_block_num == 0) return -1;

	uint32_t block_size = fs->block_size;
	uint32_t sector_per_block = block_size / 512;

	uint8_t* dir_block = (uint8_t*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	if (!dir_block) return -1;

	memset(dir_block, 0, block_size);

	Ext2Dir* dot = (Ext2Dir*)dir_block;
	dot->inode = new_inode_num;
	dot->rec_len = 12;
	dot->name_len = 1;
	dot->file_type = 2;
	dot->name[0] = '.';

	Ext2Dir* dotdot = (Ext2Dir*)(dir_block + 12);
	dotdot->inode = parent_node->first_block;
	dotdot->rec_len = block_size - 12;
	dotdot->name_len = 2;
	dotdot->file_type = 2;
	dotdot->name[0] = '.';
	dotdot->name[1] = '.';

	AuVDiskWrite((AuVDisk*)fs->vdisk, (uint64_t)new_block_num * sector_per_block, sector_per_block, (uint64_t*)dir_block);
	AuPmmngrReleasePage((void*)V2P((uint64_t)dir_block));

	Ext2Inode new_inode;
	memset(&new_inode, 0, sizeof(Ext2Inode));

	new_inode.mode = EXT2_S_IFDIR | (permissions & 0x0FFF);
	new_inode.size = block_size;
	new_inode.links_count = 2;
	new_inode.blocks = block_size / 512;
	new_inode.block[0] = new_block_num;

	if (Ext2InodeWrite(fs, new_inode_num, &new_inode) != 0) return -1;

	if (Ext2AddDirEntry(fs, parent_inode, parent_node->first_block, new_inode_num, name, 2) != 0) return -1;

	parent_inode->links_count++;
	Ext2InodeWrite(fs, parent_node->first_block, parent_inode);

	uint32_t group = (new_inode_num - 1) / fs->inodes_per_group;
	fs->block_desc[group].used_dirs_count++;
	Ext2FlushBgdt(fs);

	return 0;
}

int Ext2ReadDir(AuVFSNode* fsys, AuVFSNode* dir_node, AuDirectoryEntry* entry) {
	(void)fsys;
	if (!dir_node || !entry) return -1;

	Ext2Fs* fs = (Ext2Fs*)dir_node->device;
	Ext2Inode* inode = (Ext2Inode*)dir_node->private_data;
	if (!fs || !inode) return -1;

	uint32_t block_size = fs->block_size;
	uint32_t sector_per_block = block_size / 512;

	uint8_t* block_buf = (uint8_t*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	if (!block_buf) return -1;

	while (dir_node->pos < inode->size) {
		uint32_t logical_block = dir_node->pos / block_size;
		uint32_t internal_offset = dir_node->pos % block_size;

		if (logical_block >= 12) break;

		uint32_t physical_block = inode->block[logical_block];
		if (physical_block == 0) {
			dir_node->pos += (block_size - internal_offset);
			continue;
		}

		uint64_t target_lba = (uint64_t)physical_block * sector_per_block;
		AuVDiskRead((AuVDisk*)fs->vdisk, target_lba, sector_per_block, (uint64_t*)block_buf);

		Ext2Dir* dir_ent = (Ext2Dir*)(block_buf + internal_offset);
		if (dir_ent->rec_len == 0) {
			dir_node->pos += (block_size - internal_offset);
			continue;
		}

		dir_node->pos += dir_ent->rec_len;

		if (dir_ent->inode == 0) continue;

		// Populate the AuDirectoryEntry structure
		memset(entry, 0, sizeof(AuDirectoryEntry));
		uint8_t copy_len = (dir_ent->name_len < 31) ? dir_ent->name_len : 31;
		memcpy(entry->filename, dir_ent->name, copy_len);
		entry->filename[copy_len] = '\0';

		if (dir_ent->file_type == 2) {
			entry->flags = FS_FLAG_DIRECTORY;
		} else {
			entry->flags = FS_FLAG_GENERAL;
		}

		AuPmmngrReleasePage((void*)V2P((uint64_t)block_buf));
		return 1;
	}

	AuPmmngrReleasePage((void*)V2P((uint64_t)block_buf));
	return 0;
}

int Ext2Stat(AuVFSNode* file, Ext2Inode* out_stat) {
	if (!file || !out_stat) return -1;

	Ext2Fs* fs = (Ext2Fs*)file->device;
	if (!fs) return -1;

	return Ext2ReadInode(fs, file->first_block, out_stat);
}

int Ext2RemoveDirEntry(Ext2Fs* fs, Ext2Inode* parent_inode, uint32_t parent_inode_num, const char* name) {
	uint32_t block_size = fs->block_size;
	uint32_t sector_per_block = block_size / 512;
	uint32_t target_len = strlen(name);

	uint8_t* block_buf = (uint8_t*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	if (!block_buf) return -1;

	for (int i = 0; i < 12; i++) {
		uint32_t physical_block = parent_inode->block[i];
		if (physical_block == 0) continue;

		uint64_t target_lba = (uint64_t)physical_block * sector_per_block;
		AuVDiskRead((AuVDisk*)fs->vdisk, target_lba, sector_per_block, (uint64_t*)block_buf);

		uint32_t current_pos = 0;
		Ext2Dir* prev_entry = NULL;

		while (current_pos < block_size) {
			Ext2Dir* entry = (Ext2Dir*)(block_buf + current_pos);
			if (entry->rec_len == 0) break;

			if (entry->inode != 0 && entry->name_len == target_len &&
			    strncmp(entry->name, name, target_len) == 0) {

				if (prev_entry) {
					prev_entry->rec_len += entry->rec_len;
				} else {
					entry->inode = 0;
				}

				AuVDiskWrite((AuVDisk*)fs->vdisk, target_lba, sector_per_block, (uint64_t*)block_buf);
				AuPmmngrReleasePage((void*)V2P((uint64_t)block_buf));
				return 0;
			}

			prev_entry = entry;
			current_pos += entry->rec_len;
		}
	}

	AuPmmngrReleasePage((void*)V2P((uint64_t)block_buf));
	return -1;
}

int Ext2Truncate(Ext2Fs* fs, Ext2Inode* inode, uint32_t inode_num) {
	if (!fs || !inode) return -1;

	uint32_t sector_per_block = fs->block_size / 512;
	uint32_t N = fs->pointers_per_block;

	for (int i = 0; i < 12; i++) {
		if (inode->block[i] != 0) {
			Ext2FreeBlock(fs, inode->block[i]);
			inode->block[i] = 0;
		}
	}

	if (inode->block[12] != 0) {
		uint32_t* table = (uint32_t*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
		if (table) {
			AuVDiskRead((AuVDisk*)fs->vdisk, (uint64_t)inode->block[12] * sector_per_block, sector_per_block, (uint64_t*)table);
			for (uint32_t i = 0; i < N; i++) {
				if (table[i] != 0) Ext2FreeBlock(fs, table[i]);
			}
			AuPmmngrReleasePage((void*)V2P((uint64_t)table));
		}
		Ext2FreeBlock(fs, inode->block[12]);
		inode->block[12] = 0;
	}

	if (inode->block[13] != 0) {
		uint32_t* dtable = (uint32_t*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
		uint32_t* stable = (uint32_t*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
		if (dtable && stable) {
			AuVDiskRead((AuVDisk*)fs->vdisk, (uint64_t)inode->block[13] * sector_per_block, sector_per_block, (uint64_t*)dtable);
			for (uint32_t i = 0; i < N; i++) {
				if (dtable[i] != 0) {
					AuVDiskRead((AuVDisk*)fs->vdisk, (uint64_t)dtable[i] * sector_per_block, sector_per_block, (uint64_t*)stable);
					for (uint32_t j = 0; j < N; j++) {
						if (stable[j] != 0) Ext2FreeBlock(fs, stable[j]);
					}
					Ext2FreeBlock(fs, dtable[i]);
				}
			}
		}
		if (dtable) AuPmmngrReleasePage((void*)V2P((uint64_t)dtable));
		if (stable) AuPmmngrReleasePage((void*)V2P((uint64_t)stable));
		Ext2FreeBlock(fs, inode->block[13]);
		inode->block[13] = 0;
	}

	inode->size = 0;
	inode->blocks = 0;
	return Ext2InodeWrite(fs, inode_num, inode);
}

int Ext2Rmdir(AuVFSNode* parent, AuVFSNode* dir) {
	if (!parent || !dir) return -1;

	Ext2Fs* fs = (Ext2Fs*)parent->device;
	Ext2Inode* parent_inode = (Ext2Inode*)parent->private_data;
	if (!fs || !parent_inode) return -1;

	uint32_t target_inode_num = dir->first_block;
	Ext2Inode target_inode;
	if (Ext2ReadInode(fs, target_inode_num, &target_inode) != 0) return -1;

	if (!(target_inode.mode & EXT2_S_IFDIR)) return -1;

	uint32_t sector_per_block = fs->block_size / 512;
	uint8_t* block_buf = (uint8_t*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	if (!block_buf) return -1;

	AuVDiskRead((AuVDisk*)fs->vdisk, (uint64_t)target_inode.block[0] * sector_per_block, sector_per_block, (uint64_t*)block_buf);

	uint32_t current_pos = 0;
	int entry_count = 0;
	while (current_pos < fs->block_size) {
		Ext2Dir* entry = (Ext2Dir*)(block_buf + current_pos);
		if (entry->rec_len == 0) break;
		if (entry->inode != 0) entry_count++;
		current_pos += entry->rec_len;
	}
	AuPmmngrReleasePage((void*)V2P((uint64_t)block_buf));

	if (entry_count > 2) {
		AuTextOut("[Ext2]: Directory not empty.\r\n");
		return -1;
	}

	if (Ext2RemoveDirEntry(fs, parent_inode, parent->first_block, dir->filename) != 0) return -1;

	if (parent_inode->links_count > 0) {
		parent_inode->links_count--;
		Ext2InodeWrite(fs, parent->first_block, parent_inode);
	}

	Ext2FreeBlock(fs, target_inode.block[0]);
	Ext2FreeInode(fs, target_inode_num);

	uint32_t group = (target_inode_num - 1) / fs->inodes_per_group;
	if (fs->block_desc[group].used_dirs_count > 0) {
		fs->block_desc[group].used_dirs_count--;
		Ext2FlushBgdt(fs);
	}

	return 0;
}

AuVFSNode* Ext2CreateDir(AuVFSNode* parent, char* name) {
	if (!parent || !name) return NULL;

	if (Ext2Mkdir(parent, name, 0755) != 0) {
		AuTextOut("[Ext2]: Failed to create directory on disk.\r\n");
		return NULL;
	}

	return Ext2Open(parent, name);
}