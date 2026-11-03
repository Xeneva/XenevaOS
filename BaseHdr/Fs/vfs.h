/**
* @file vfs.h
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

#ifndef __VFS_H__
#define __VFS_H__

#include <stdint.h>
#include <list.h>
#include <aurora.h>
#include <Cred/group.h>
#include <Cred/user.h>
#include <Sync/poll.h>

#define FS_FLAG_DIRECTORY			(1 << 1)  //temporary/freeable
#define FS_FLAG_GENERAL				(1 << 2)  //tempoary/freeable
#define FS_FLAG_DEVICE				(1 << 3)  //permanent/non-freeable
#define FS_FLAG_DELETED				(1 << 4)  //state
#define FS_FLAG_INVALID				(1 << 5)  //state
#define FS_FLAG_FILE_SYSTEM			(1 << 6)  //parment/non-freeable
#define FS_FLAG_PIPE				(1 << 7)  //temporary/freeable
#define FS_FLAG_TTY					(1 << 8)  //temporary/freeable with count
#define FS_FLAG_SOCKET				(1 << 9)  //temporary/freeable
#define FS_FLAG_FILE_SYSTEM_GENERAL (1 << 10) //general file system
#define FS_FLAG_CACHED				(1 << 11) //file is being cached

/** file modes */
#define S_ISUID	  04000
#define S_ISGID	  02000
#define S_ISVTX	  01000
#define S_IRWXU	  00700
#define S_IRUSR	  00400
#define S_IWUSR	  00200
#define S_IXUSR	  00100
#define S_IRWXG	  00070
#define S_IRWXO	  00007
#define S_IALLUGO 07777

#define FS_STATUS_FOUND 0x1
#define FS_STATUS_NF	0x0

#define VFS_MEDIA_TYPE_AHCI	 0x1
#define VFS_MEDIA_TYPE_IDE	 0x2
#define VFS_MEDIA_TYPE_OTHER 0x3

/* file open modes*/
#define FILE_OPEN_READ_ONLY (1 << 1)
#define FILE_OPEN_WRITE		(1 << 2)
#define FILE_OPEN_CREAT		(1 << 3)

struct __VFS_NODE__;

#ifdef ARCH_X64
#pragma pack(push, 1)
#endif
typedef struct _AuDirectoryEnty_ {
	char filename[32];
	int index;
	int size;
	int date;
	int time;
	uint8_t flags;
} AuDirectoryEntry;
#ifdef ARCH_X64
#pragma pack(pop)
#endif

typedef struct __VFS_NODE__* (*open_callback)(struct __VFS_NODE__* node, char* path);
typedef size_t (*read_callback)(struct __VFS_NODE__* node,
								struct __VFS_NODE__* file,
								uint64_t* buffer,
								uint32_t length);
typedef size_t (*read_block_callback)(struct __VFS_NODE__* node,
									  struct __VFS_NODE__* file,
									  uint64_t* buffer);
typedef size_t (*write_callback)(struct __VFS_NODE__* node,
								 struct __VFS_NODE__* file,
								 uint64_t* buffer,
								 uint32_t length);
typedef struct __VFS_NODE__* (*create_dir_callback)(struct __VFS_NODE__* node, char* dirname);
typedef struct __VFS_NODE__* (*create_file_callback)(struct __VFS_NODE__* node, char* filename);
typedef int (*remove_dir_callback)(struct __VFS_NODE__* node, struct __VFS_NODE__* file);
typedef int (*remove_file_callback)(struct __VFS_NODE__* node, struct __VFS_NODE__* file);
typedef int (*close_callback)(struct __VFS_NODE__* node, struct __VFS_NODE__* file);
typedef int (*iocontrol_callback)(struct __VFS_NODE__* file, int code, void* arg);
typedef struct __VFS_NODE__* (*opendir_callback)(struct __VFS_NODE__* fs, char* dirname);
typedef int (*readdir_callback)(struct __VFS_NODE__* fs,
								struct __VFS_NODE__* dir,
								AuDirectoryEntry* dirent);
typedef uint32_t (*get_disk_block)(struct __VFS_NODE__* fs,
								   struct __VFS_NODE__* file,
								   uint64_t fs_block);
typedef size_t (*fs_getblockfor)(struct __VFS_NODE__* fs,
								 struct __VFS_NODE__* file,
								 uint64_t offset);
typedef int (*poll_callback)(struct __VFS_NODE__* fs,
							 struct __VFS_NODE__* file,
							 AuPollTable* table);
typedef int (*setattr_callback)(struct __VFS_NODE__* fs, struct __VFS_NODE__* file);

//#pragma pack(push,1)
typedef struct __VFS_NODE__ {
	char filename[32];
	uint32_t size;
	uint8_t eof;
	uint32_t pos;
	uint32_t parent_block;
	uint32_t first_block;
	uint64_t current;
	uint16_t flags;
	uint8_t status;
	UID_NUM uid;
	GID_NUM gid;
	uint16_t mode;
	void* device;
	void* private_data;
	uint16_t fileCopyCount; //important for tty files
	uint64_t last_resolved_index;
	uint32_t last_resolved_cluster;
	/* callback specific */
	open_callback open;
	opendir_callback opendir;
	read_callback read;
	write_callback write;
	create_dir_callback create_dir;
	create_file_callback create_file;
	remove_dir_callback remove_dir;
	remove_file_callback remove_file;
	close_callback close;
	read_block_callback read_block;
	readdir_callback read_dir;
	get_disk_block get_disk_block;
	fs_getblockfor get_blockfor;
	iocontrol_callback iocontrol;
	poll_callback poll;
	setattr_callback setattr;
} AuVFSNode;
//#pragma pack(pop)

//#pragma pack(push,1)
typedef struct _AuFileStatus_ {
	uint8_t filemode; //mode of the file
	size_t size;	  //size in bytes
	uint32_t current_block;
	uint32_t start_block;
	uint32_t user_id;  //for future use
	uint32_t group_id; //for future use
	uint32_t num_links;
	uint8_t eof;
} AuFileStatus;
//#pragma pack(pop)

typedef struct _FileControl_ {
	int syscall_magic;
	uint8_t uchar_1;
	uint8_t uchar_2;
	uint16_t ushort_1;
	uint16_t ushort_2;
	uint32_t uint_1;
	uint32_t uint_2;
	uint64_t ulong_1;
	uint64_t ulong_2;
} AuFileIOControl;

/**
 * @brief AuVFSContainer -- is a container
 * which contains list of nodes/directories
 * another directory will point to
 * another newly created container
 */
typedef struct __VFS_Container__ {
	list_t* childs;
} AuVFSContainer;

/**
* @brief AuVFSInitialise -- initialise the virtual
* file system
*/
extern void AuVFSInitialise();

/**
 * @brief AuVFSReserveMountPointLetter -- reserve a letter to
 * mount a file system
 */
extern char* AuVFSReserveMountPointLetter();

/**
* @brief AuVFSOpen -- Opens a file
* @param path -- path to open
*/
AU_EXTERN AU_EXPORT AuVFSNode* AuVFSOpen(char* path);

/**
 * @brief AuVFSNodeIOControl -- Calls node's iocontrol pointer
 * @param node -- pointer to fsnode
 * @param code -- code to pass
 * @param arg -- extra arguments
 * @return return value returned by specific iocontrol handler
 * to the caller
 */
AU_EXTERN AU_EXPORT int AuVFSNodeIOControl(AuVFSNode* node, int code, void* arg);

/**
 * @brief AuVFSAddFileSystem -- adds a file system to the
 * vfs list
 * @param node -- file system node to add
 */
AU_EXTERN AU_EXPORT void AuVFSAddFileSystem(AuVFSNode* node);

/**
 * @brief AuVFSRegisterRoot -- register the root file
 * system
 * @param fs -- fsnode to add
 */
AU_EXTERN AU_EXPORT void AuVFSRegisterRoot(AuVFSNode* fs);

/**
 * @brief AuVFSFind -- Searches a filesystem an return it to
 * the caller
 * @param path -- path of the file system
 * @return pointer to file system extracted from path
 */
AU_EXTERN AU_EXPORT AuVFSNode* AuVFSFind(char* path);

/**
 * @brief AuVFSNodeRead -- read from file system
 * @param node -- file system node to use
 * @param file -- file information to use
 * @param buffer -- buffer to write to
 * @param length -- length of the file
 * @return amount of data being read in bytes
 */
AU_EXTERN AU_EXPORT size_t AuVFSNodeRead(AuVFSNode* node,
										 AuVFSNode* file,
										 uint64_t* buffer,
										 uint32_t length);

/**
 * @brief AuVFSNodeReadBlock -- read a block size data from file system
 * @param node -- file system node to use
 * @param file -- file node to use
 * @param buffer -- buffer area to read to
 * @return always <= 4kib
 */
AU_EXTERN AU_EXPORT size_t AuVFSNodeReadBlock(AuVFSNode* node, AuVFSNode* file, uint64_t* buffer);

/**
 * @brief AuVFSCreateDir -- creates a new dir
 * @param fsys -- Pointer to file system
 * @param dirname -- directory name
 * @return pointer to newly created directory node
 */
AU_EXTERN AU_EXPORT AuVFSNode* AuVFSCreateDir(AuVFSNode* fsys, char* dirname);

/**
 * @brief AuVFSCreateFile -- create a new file
 * @param fsys -- Pointer to file system
 * @param filename -- filename
 * @return pointer to newly created file node
 */
AU_EXTERN AU_EXPORT AuVFSNode* AuVFSCreateFile(AuVFSNode* fsys, char* filename);

/**
 * @brief AuVFSRemoveFile --remove a file from a file system
 * @param fsys -- Pointer to file system
 * @param file -- Pointer to file to remove
 * @return 0 on success, -1 on failure
 */
extern int AuVFSRemoveFile(AuVFSNode* fsys, AuVFSNode* file);

/**
 * @brief AuVFSRemoveFile --remove a file from a file system
 * @param fsys -- Pointer to file system
 * @param file -- Pointer to file to remove
 * @return 0 on success, -1 on failure
 */
extern int AuVFSRemoveDir(AuVFSNode* fsys, AuVFSNode* file);

/**
 * @brief AuVFSNodeWrite -- write to file system
 * @param node -- file system node to use
 * @param file -- file node to use
 * @param buffer -- buffer to write
 * @param length -- length of the data
 */
AU_EXTERN AU_EXPORT void
AuVFSNodeWrite(AuVFSNode* node, AuVFSNode* file, uint64_t* buffer, uint32_t length);

/**
 * @brief AuVFSNodeClose -- close a file system or file
 * @param node -- file system node to use
 * @param file -- file to close
 */
AU_EXTERN AU_EXPORT void AuVFSNodeClose(AuVFSNode* node, AuVFSNode* file);

/**
* @brief AuVFSGetBlockFor -- returns a block number for
* certain byte offset of file
* @param node -- file system node
* @param file -- pointer to file structure
* @param offset -- byte offset
* @return block number of that offset
*/
AU_EXTERN AU_EXPORT size_t AuVFSGetBlockFor(AuVFSNode* node, AuVFSNode* file, uint64_t offset);

/**
 * @brief AuVFSSetCred -- set gid/uid value for a file
 * @param node -- file node
 * @param uid -- user id
 * @param gid -- group id
 * @return zero on success, one on failure
 */
AU_EXTERN AU_EXPORT int AuVFSSetCred(AuVFSNode* node, UID_NUM uid, GID_NUM gid);

/**
 * @brief AuVFSPoll -- poll callback interface 
 * @param file -- pointer to pollable file
 * @param pt -- Pointer to PollTable holding information about caller
 */
AU_EXTERN AU_EXPORT int AuVFSPoll(AuVFSNode* file, AuPollTable* pt);

/**
 * @brief AuVFSOwnFsys -- give the capability to root process, to
 * own the file system for specific user id and group id
 * @param path -- file system path
 * @param uid -- User ID value
 * @param gid -- Group ID value
 */
AU_EXTERN AU_EXPORT int AuVFSOwnFsys(char* path, UID_NUM uid, GID_NUM gid);

#endif