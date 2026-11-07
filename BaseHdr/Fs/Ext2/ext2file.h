#include <Fs/vdisk.h>
#include <Fs/Ext2/ext2.h>

size_t Ext2Write(AuVFSNode* node, AuVFSNode* file, uint64_t* buffer, uint32_t length);

void Ext2FlushSuperblock(Ext2Fs* fs);

void Ext2FlushBgdt(Ext2Fs* fs);

uint32_t Ext2AllocBlock(Ext2Fs* fs);

int Ext2InodeWrite(Ext2Fs* fs, uint32_t inode_num, Ext2Inode* inode);

AuVFSNode* Ext2CreateFile(AuVFSNode* parent, char* name);

int Ext2Close(AuVFSNode* fsys, AuVFSNode* file);

int Ext2Unlink(AuVFSNode* parent, AuVFSNode* file);