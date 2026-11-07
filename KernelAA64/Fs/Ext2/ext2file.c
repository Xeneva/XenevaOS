#include <Fs/vfs.h>
#include <Fs/vdisk.h>
#include <Fs/Ext2/ext2file.h>
#include <Fs/Ext2/ext2dir.h>
#include <Fs/Ext2/ext2.h>
#include <string.h>
#include <Mm/pmmngr.h>
#include <aucon.h>
#include <_null.h>
#include <Mm/kmalloc.h>

void Ext2FlushSuperblock(Ext2Fs* fs) {
    if (!fs || !fs->superblock) return;

    int8_t* buffer = (uint8_t*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	if (!buffer) return;

    memset(buffer, 0, 4096);

    AuVDiskRead((AuVDisk*)fs->vdisk, 2, 2, (uint64_t*)buffer);
    memcpy(buffer, fs->superblock, sizeof(Ext2Superblock));
    AuVDiskWrite((AuVDisk*)fs->vdisk, 2, 2, (uint64_t*)buffer);

    AuPmmngrReleasePage((uint64_t)V2P((uint64_t)buffer));
};

void Ext2FlushBgdt(Ext2Fs* fs) {
    if (!fs || !fs->block_desc) return;

    uint32_t bgd_start_block = (fs->block_size == 1024) ? 2 : 1;
	uint32_t sector_per_block = fs->block_size / 512;
	uint64_t bgd_lba = (uint64_t)bgd_start_block * sector_per_block;

	uint32_t bgd_table_size = fs->block_group_count * sizeof(Ext2BlockDescriptor);
	uint32_t blocks_needed = (bgd_table_size + fs->block_size - 1) / fs->block_size;

	uint8_t* buffer = (uint8_t*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	if (!buffer) return;

	memset(buffer, 0, fs->block_size);
	memcpy(buffer, fs->block_desc, bgd_table_size);

	AuVDiskWrite((AuVDisk*)fs->vdisk, bgd_lba, blocks_needed * sector_per_block, (uint64_t*)buffer);

	AuPmmngrReleasePage((uint64_t)V2P((uint64_t)buffer));
};

uint32_t Ext2AllocBlock(Ext2Fs* fs) {
    if (!fs || !fs->superblock) {
		AuTextOut("[Ext2]: Invalid filesystem handle.\r\n");
		return 0;
	}

    if (fs->superblock->free_blocks_count == 0) {
		AuTextOut("[Ext2]: Filesystem is completely full!\r\n");
		return 0;
	}

    uint32_t block_size = fs->block_size;
	uint32_t sector_per_block = block_size / 512;
	uint32_t group_count = fs->block_group_count;

    uint8_t* buffer = (uint8_t*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
    if (!buffer) {
		AuTextOut("[Ext2]: Out of memory allocating bitmap buffer.\r\n");
		return 0;
	}

    uint32_t allocated_block_id = 0;
	uint32_t target_group = 0;

    for (uint32_t i = 0; i<group_count; i++) {
        if (fs->block_desc->free_blocks_count == 0) continue;

        uint32_t bitmap_block = fs->block_desc->block_bitmap;
        uint64_t bitmap_lba = (uint64_t)bitmap_block * sector_per_block;

        memset(buffer, 0, block_size);
        AuVDiskRead((AuVDisk*)fs->vdisk, bitmap_lba, sector_per_block, (uint64_t*)buffer);

        for (uint32_t byte = 0; byte < block_size; byte++) {
            if (buffer[byte] == 0xFF) continue;
            for  (uint8_t bit = 0; bit < 8; bit++) {
                if (!(buffer[byte] & (1 << bit))) {
                    uint32_t relative_block = (byte * 8) + bit;
                    allocated_block_id = (i * fs->block_group_count) + relative_block + fs->superblock->free_data_block;
                    buffer[byte] |= (1 << bit);
                    AuVDiskWrite((AuVDisk*)fs->vdisk, bitmap_lba, sector_per_block, (uint64_t*)buffer);
                    target_group = i;
                    goto success;
                }
            }
        }
    }
success:
    AuPmmngrReleasePage((uint64_t)V2P((uint64_t)buffer));
    if (allocated_block_id == 0) {
        AuTextOut("[Ext2]: failed to locate a free bit in bitmap.\r\n");
        return 0;
    }

    fs->superblock->free_blocks_count--;
    fs->block_desc->free_blocks_count--;

    Ext2FlushSuperblock(fs);
    Ext2FlushBgdt(fs);

    return allocated_block_id;
};

int Ext2InodeWrite(Ext2Fs* fs, uint32_t inode_num, Ext2Inode* inode) {
    if (!fs || !inode || inode_num == 0) return -1;

    uint32_t group = (inode_num - 1) / fs->inodes_per_group;
	uint32_t index = (inode_num - 1) % fs->inodes_per_group;

    uint32_t inode_table_block = fs->block_desc->inode_table;
    uint32_t inode_size = fs->inode_size;
    uint32_t byte_offset = index * inode_size;

    uint32_t block_offset = byte_offset / fs->block_size;
	uint32_t internal_offset = byte_offset % fs->block_size;

	uint32_t target_block = inode_table_block + block_offset;
	uint32_t sector_per_block = fs->block_size / 512;
	uint64_t target_lba = (uint64_t)target_block * sector_per_block;

    uint8_t* buffer = (uint8_t*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	if (!buffer) return -1;

	AuVDiskRead((AuVDisk*)fs->vdisk, target_lba, sector_per_block, (uint64_t*)buffer);
	memcpy(buffer + internal_offset, inode, inode_size);
	AuVDiskWrite((AuVDisk*)fs->vdisk, target_lba, sector_per_block, (uint64_t*)buffer);

	AuPmmngrReleasePage((uint64_t)V2P((uint64_t)buffer));

	return 0;
};

static uint32_t Ext2AssignTable(Ext2Fs* fs, uint32_t table_block_id, uint32_t index) {
    if(table_block_id == 0) return 0;
    uint32_t sector_per_block = fs->block_size / 512;

    uint32_t* table_buffer = (uint32_t*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
    if (!table_buffer) return 0;

    AuVDiskRead((AuVDisk*)fs->vdisk, (uint64_t)table_block_id * sector_per_block, sector_per_block, (uint64_t*)table_buffer);
    uint32_t target_block = table_buffer[index];

    if (target_block == 0) {
        target_block = Ext2AllocBlock(fs);
        if (target_block != 0) {
            table_buffer[index] = target_block;

            AuVDiskWrite((AuVDisk*)fs->vdisk, (uint64_t)table_block_id * sector_per_block, sector_per_block, (uint64_t*)table_buffer);

            uint32_t* zero_buffer = (uint32_t*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
            if (zero_buffer) {
                memset(zero_buffer, 0, fs->block_size);
                AuVDiskWrite((AuVDisk*)fs->vdisk, (uint64_t)table_block_id * sector_per_block, sector_per_block, (uint64_t*)zero_buffer);
                AuPmmngrReleasePage((void*)V2P((uint64_t)zero_buffer));
            }
        }
    }

    AuPmmngrReleasePage((void*)V2P((uint64_t)table_buffer));
    return target_block;
};

/**
 * Ext2GetBlock -- check for blocks if not allocated it allocates them
 */
static uint32_t Ext2GetBlock(Ext2Fs* fs, Ext2Inode* inode, uint32_t inode_num, uint32_t logical_block) {
    uint32_t sector_per_block = fs->block_size / 512;

    if (logical_block < 12) {
        if (inode->block[logical_block] !=0 ) return inode->block[logical_block];
        uint32_t new_block = Ext2AllocBlock(fs);
        if (new_block == 0) return 0;

        inode->block[logical_block] = new_block;
		Ext2InodeWrite(fs, inode_num, inode);
		return new_block;
    }

    if (logical_block < (12 + fs->pointers_per_block)) {
		uint32_t single_index = logical_block - 12;

		if (inode->block[12] == 0) {
			uint32_t new_table_block = Ext2AllocBlock(fs);
			if (new_table_block == 0) return 0;

			inode->block[12] = new_table_block;
			Ext2InodeWrite(fs, inode_num, inode);

			
			uint8_t* buffer = (uint8_t*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
			memset(buffer, 0, fs->block_size);
			AuVDiskWrite((AuVDisk*)fs->vdisk, (uint64_t)new_table_block * sector_per_block, sector_per_block, (uint64_t*)buffer);
			AuPmmngrReleasePage((void*)V2P((uint64_t)buffer));
		}

		return Ext2AssignTable(fs, inode->block[12], single_index);
	}

    if (logical_block < (12 + fs->pointers_per_block + (fs->pointers_per_block * fs->pointers_per_block))) {
        uint64_t double_index = logical_block - (12 + fs->pointers_per_block);
        uint32 lvl1_index = double_index / fs->pointers_per_block;
        uint32 lvl2_index = double_index % fs->pointers_per_block;

        if (inode->block[13] == 0) {
			uint32_t new_table_block = Ext2AllocBlock(fs);
			if (new_table_block == 0) return 0;

			inode->block[13] = new_table_block;
			Ext2InodeWrite(fs, inode_num, inode);

			
			uint8_t* buffer = (uint8_t*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
			memset(buffer, 0, fs->block_size);
			AuVDiskWrite((AuVDisk*)fs->vdisk, (uint64_t)new_table_block * sector_per_block, sector_per_block, (uint64_t*)buffer);
			AuPmmngrReleasePage((void*)V2P((uint64_t)buffer));
		}

        uint32 single_table = Ext2AssignTable(fs, inode->block[13], lvl1_index);
        if (single_table == 0) return 0;

        return Ext2AssignTable(fs, single_table, lvl2_index);
    }

    if (logical_block < (12 + fs->pointers_per_block + (fs->pointers_per_block * fs->pointers_per_block) + (fs->pointers_per_block * fs->pointers_per_block * fs->pointers_per_block))) {
        uint64_t triple_index = logical_block - (12 + fs->pointers_per_block + (fs->pointers_per_block * fs->pointers_per_block));
        uint32_t lvl1_index = triple_index / (fs->pointers_per_block);
        uint32_t lvl2_index = (triple_index / fs->pointers_per_block) % fs->pointers_per_block;
        uint32_t lvl3_index = triple_index % fs->pointers_per_block;

        if (inode->block[14] == 0){
            uint32_t new_table_block = Ext2AllocBlock(fs);
            if (new_table_block == 0) return 0;

			inode->block[14] = new_table_block;
			Ext2InodeWrite(fs, inode_num, inode);

			uint8_t* zero_buffer = (uint8_t*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
			memset(zero_buffer, 0, fs->block_size);
			AuVDiskWrite((AuVDisk*)fs->vdisk, (uint64_t)new_table_block * sector_per_block, sector_per_block, (uint64_t*)zero_buffer);
			AuPmmngrReleasePage((void*)V2P((uint64_t)zero_buffer));
        }

        uint32 double_table = Ext2AssignTable(fs, inode->block[14], lvl1_index);
        if (double_table == 0) return 0;

        uint32_t single_table = Ext2AssignTable(fs, double_table, lvl2_index);
        if (single_table) return 0;

        return Ext2AssignTable(fs, single_table, lvl3_index);
    }

    AuTextOut("[Ext2]: Logical block exceeds maximum supported Ext2 file boundary.\r\n");
	return 0;
};

AuVFSNode* Ext2CreateFile(AuVFSNode* parent, char* name) {
	if (!parent || !name) return NULL;

	Ext2Fs* fs = (Ext2Fs*)parent->device;
	Ext2Inode* parent_inode = (Ext2Inode*)parent->private_data;
	if (!fs || !parent_inode) return NULL;

	if (Ext2FindEntry(fs, parent_inode, name) != 0) {
		AuTextOut("[Ext2]: File already exists.\r\n");
		return NULL;
	}

	uint32_t new_inode_num = Ext2AllocInode(fs);
	if (new_inode_num == 0) return NULL;

	Ext2Inode new_inode;
	memset(&new_inode, 0, sizeof(Ext2Inode));
	new_inode.mode = EXT2_S_IFREG | 0644; 
	new_inode.size = 0;
	new_inode.blocks = 0;
	new_inode.links_count = 1;

	if (Ext2InodeWrite(fs, new_inode_num, &new_inode) != 0) {
		Ext2FreeInode(fs, new_inode_num);
		return NULL;
	}

	if (Ext2AddDirEntry(fs, parent_inode, parent->first_block, new_inode_num, name, 1) != 0) {
		Ext2FreeInode(fs, new_inode_num);
		return NULL;
	}

	return Ext2Open(parent, name);
}

int Ext2Close(AuVFSNode* fsys, AuVFSNode* file) {
    (void)fsys;
	if (!file) return -1;

	if (file->private_data) {
		kfree(file->private_data);
		file->private_data = NULL;
	}

	kfree(file);
	return 0;
}

int Ext2Unlink(AuVFSNode* parent, AuVFSNode* file) {
	if (!parent || !file) return -1;

	Ext2Fs* fs = (Ext2Fs*)parent->device;
	Ext2Inode* parent_inode = (Ext2Inode*)parent->private_data;
	if (!fs || !parent_inode) return -1;

	uint32_t target_inode_num = file->first_block;
	Ext2Inode target_inode;
	if (Ext2ReadInode(fs, target_inode_num, &target_inode) != 0) return -1;

	if (target_inode.mode & EXT2_S_IFDIR) {
		AuTextOut("[Ext2]: Cannot unlink directory via remove_file.\r\n");
		return -1;
	}

	if (Ext2RemoveDirEntry(fs, parent_inode, parent->first_block, file->filename) != 0) return -1;

	if (target_inode.links_count > 0) target_inode.links_count--;

	if (target_inode.links_count == 0) {
		Ext2Truncate(fs, &target_inode, target_inode_num);
		Ext2FreeInode(fs, target_inode_num);
	} else {
		Ext2InodeWrite(fs, target_inode_num, &target_inode);
	}

	return 0;
}

int Ext2Rename(AuVFSNode* old_parent, char* old_name, AuVFSNode* new_parent, char* new_name) {
	if (!old_parent || !old_name || !new_parent || !new_name) return -1;

	Ext2Fs* fs = (Ext2Fs*)old_parent->device;
	Ext2Inode* old_p_inode = (Ext2Inode*)old_parent->private_data;
	Ext2Inode* new_p_inode = (Ext2Inode*)new_parent->private_data;

	uint32_t target_inode_num = Ext2FindEntry(fs, old_p_inode, old_name);
	if (target_inode_num == 0) return -1;

	Ext2Inode target_inode;
	Ext2ReadInode(fs, target_inode_num, &target_inode);
	uint8_t file_type = (target_inode.mode & EXT2_S_IFDIR) ? 2 : 1;

	if (Ext2AddDirEntry(fs, new_p_inode, new_parent->first_block, target_inode_num, new_name, file_type) != 0) {
		return -1;
	}

	Ext2RemoveDirEntry(fs, old_p_inode, old_parent->first_block, old_name);

	if (file_type == 2 && old_parent->first_block != new_parent->first_block) {
		uint32_t sector_per_block = fs->block_size / 512;
		uint8_t* block_buf = (uint8_t*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
		if (block_buf) {
			AuVDiskRead((AuVDisk*)fs->vdisk, (uint64_t)target_inode.block[0] * sector_per_block, sector_per_block, (uint64_t*)block_buf);
			Ext2Dir* dotdot = (Ext2Dir*)(block_buf + 12);
			dotdot->inode = new_parent->first_block;
			AuVDiskWrite((AuVDisk*)fs->vdisk, (uint64_t)target_inode.block[0] * sector_per_block, sector_per_block, (uint64_t*)block_buf);
			AuPmmngrReleasePage((void*)V2P((uint64_t)block_buf));
		}

		old_p_inode->links_count--;
		new_p_inode->links_count++;
		Ext2InodeWrite(fs, old_parent->first_block, old_p_inode);
		Ext2InodeWrite(fs, new_parent->first_block, new_p_inode);
	}

	return 0;
}

/**
 * Ext2Wrtie -- writes data to a file in the ext2 filesystem
 * @param node -- the filesystem node
 * @param file -- the file node to write to
 * @param buffer -- the buffer to store write data
 * @param length -- the number of bytes to write
 */
size_t Ext2Write(AuVFSNode* node, AuVFSNode* file, uint64_t* buffer, uint32_t length){
    if (!node || !file || !buffer || !length){
        const char* missing = !node ? "node" : !file ? "file" : !buffer ? "buffer" : "length";
        AuTextOut("[Ext2]: %s parameter missing for file writing.\r\n", missing);
        return 0;
    }

    if (length <= 0) {
        AuTextOut("[Ext2]: lenght is zero or smaller");
        return 0;
    }

    Ext2Fs* fs = (Ext2Fs*)node->device;
	Ext2Inode* inode = (Ext2Inode*)file->private_data;
    
    uint64_t current_pos = file->pos;

    uint32_t block_size = fs->block_size;
    uint32_t sector_per_block = block_size / 512;
    uint32_t bytes_written = 0;

    if (current_pos >= file->size) {
		AuTextOut("[Ext2]: System reached EOF.\r\n");
		return 0;
	}

    uint64_t* bounce_page = (uint64_t*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
    if (!bounce_page) {
		AuTextOut("[Ext2]: out of memory during file write.\r\n");
		return 0;
	}

    while (bytes_written < length) {
        uint64_t current_pos_offset = current_pos + bytes_written;
		uint64_t logical_block = current_pos_offset / block_size;
		uint32_t internal_offset = current_pos_offset % block_size;

        uint32_t chunk = block_size - internal_offset;
        if (chunk > (length - bytes_written)) chunk = length - bytes_written;

        uint32_t physical_block = Ext2GetBlock(fs, inode, file->first_block, logical_block);
        if (physical_block == 0) {
			AuTextOut("[Ext2 Write Error]: Failed to allocate or resolve physical block.\r\n");
			break;
		}

        uint64_t target_lba = (uint64_t)physical_block * sector_per_block;

        if (internal_offset != 0 || chunk < block_size) {
			memset(bounce_page, 0, block_size);
			AuVDiskRead((AuVDisk*)fs->vdisk, target_lba, sector_per_block, (uint64_t*)bounce_page);
		} else {
			memset(bounce_page, 0, block_size);
		}

        memcpy(bounce_page + internal_offset, ((uint8_t*)buffer) + bytes_written, chunk);
        AuVDiskWrite((AuVDisk*)fs->vdisk, target_lba, sector_per_block, (uint64_t*)bounce_page);

        bytes_written += chunk;
    }
    AuPmmngrReleasePage((void*)V2P((uint64_t)bounce_page));

    if ((current_pos + bytes_written) > file->size) {
		file->size = current_pos + bytes_written;
		inode->size = file->size;
	}

    inode->blocks = (inode->size + 511) / 512;

    Ext2InodeWrite(fs, file->first_block, inode);

    file->pos += bytes_written;

	return bytes_written;
};