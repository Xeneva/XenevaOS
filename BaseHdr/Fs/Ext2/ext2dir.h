#include <Fs/vdisk.h>
#include <Fs/Ext2/ext2.h>

AuVFSNode* Ext2CreateDir(AuVFSNode* parent, char* name);

uint32_t Ext2AllocInode(Ext2Fs* fs);

int Ext2AddDirEntry(Ext2Fs* fs, Ext2Inode* parent_inode, uint32_t parent_inode_num, uint32_t child_inode_num, const char* child_name, uint8_t file_type);

int Ext2RemoveDirEntry(Ext2Fs* fs, Ext2Inode* parent_inode, uint32_t parent_inode_num, const char* name);

int Ext2Truncate(Ext2Fs* fs, Ext2Inode* inode, uint32_t inode_num);

int Ext2ReadDir(AuVFSNode* fsys, AuVFSNode* dir_node, AuDirectoryEntry* entry);

int Ext2Rmdir(AuVFSNode* parent, AuVFSNode* dir);