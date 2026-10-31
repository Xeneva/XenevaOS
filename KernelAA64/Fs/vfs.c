/**
* @file vfs.c
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

#include <Fs/vfs.h>
#include <Fs/Dev/devfs.h>
#include <_null.h>
#include <aucon.h>
#include <Mm/kmalloc.h>
#include <Drivers/uart.h>
#include <Fs/Dev/devfs.h>
#include <Fs/vdisk.h>
#include <string.h>
#include <Ipc/postbox.h>
#include <Fs/pipe.h>
#include <Board/RPI3bp/rpi3bp.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Mm/mmfile.h>
#include <Serv/sysserv.h>

AuVFSContainer* __RootContainer;
AuVFSNode* __RootFS;
static char _mount_letters[26];

void AuVFSInitialise() {
	AuVFSContainer* _root = (AuVFSContainer*)kmalloc(sizeof(AuVFSContainer));
	_root->childs = initialize_list();
	__RootContainer = _root;
	__RootFS = NULL;
	AuDeviceFsInitialize();
	AuVDiskInitialise();
	AuPipeFSInitialise();

	for (int i = 0; i < 26; i++)
		_mount_letters[i] = 0;
}

/**
 * @brief AuVFSReserveMountPointLetter -- reserve a letter to
 * mount a file system
 */
char* AuVFSReserveMountPointLetter() {
	static char path[2];
	for (int i = 0; i < 26; i++) {
		if (_mount_letters[i] == 1)
			continue;
		_mount_letters[i] = 1;
		path[0] = 'a' + i;
		path[1] = '\0';
		return path;
	}
	return NULL;
}

/**
 * @brief AuVFSFind -- Searches a filesystem an return it to
 * the caller
 * @param path -- path of the file system
 * @return pointer to file system extracted from path
 */
AuVFSNode* AuVFSFind(char* path) {
	AuVFSNode* Returnable = NULL;
	char* next;
	/* first of all search all file system
	 * skipping '/' of the path
	 */
	/* A path with no slash used to load byte 1 of a null pointer. */
	if (!path)
		return __RootFS;
	next = strchr(path, '/');
	if (!next)
		return __RootFS;
	next++;

	char pathname[16];
	int i;
	for (i = 0; i < 15; i++) {
		if (next[i] == '/' || next[i] == '\0')
			break;
		pathname[i] = next[i];
	}
	pathname[i] = 0;
	aa64_data_cache_clean_range(&pathname, 16);

	for (int j = 0; j < __RootContainer->childs->pointer; j++) {
		AuVFSNode* node = (AuVFSNode*)list_get_at(__RootContainer->childs, j);
		if (!node)
			continue;
		if ((strcmp(node->filename, pathname) == 0) && (node->flags & FS_FLAG_FILE_SYSTEM)) {
			Returnable = node;
			break;
		}
	}

	if (!Returnable)
		Returnable = __RootFS;

	return Returnable;
}

/**
 * @brief AuVFSAddFileSystem -- adds a file system to the
 * vfs list
 * @param node -- file system node to add
 */
AU_EXTERN AU_EXPORT void AuVFSAddFileSystem(AuVFSNode* node) {
	for (int i = 0; i < __RootContainer->childs->pointer; i++) {
		AuVFSNode* node_ = (AuVFSNode*)list_get_at(__RootContainer->childs, i);
		if (node_ == node)
			return;
	}
	list_add(__RootContainer->childs, node);
}

/**
 * @brief AuVFSRegisterRoot -- register the root file
 * system
 * @param fs -- fsnode to add
 */
void AuVFSRegisterRoot(AuVFSNode* fs) {
	if (__RootFS)
		return;
	__RootFS = fs;
}

extern uint64_t read_sp();
/**
 * @brief AuVFSOpen -- Opens a file
 * @param path -- path to open
 * @return pointer to file node, on success
 */
AU_EXTERN AU_EXPORT AuVFSNode* AuVFSOpen(char* path) {
	AuVFSNode* Returnable = NULL;
	dmb_sy();
	isb_flush();

	AuVFSNode* fs = AuVFSFind(path);
	//AA64SleepUS(200);
	if (!fs) {
		AuTextOut("[aurora-vfs]: failed to find filesystem assigned to path -> %s \r\n", path);
		return NULL;
	}

	/* first go through file cache layer */
	if (fs->flags & FS_FLAG_FILE_SYSTEM_GENERAL) {
		AuMMFileBack* fileb = AuMmngrFileCacheLookup(path);

		/** Here check the eof bit, if EOF bit is not marked 1
		 * this file is on used, we need to create a 
		 * new file on memory, if eof is marked, already file 
		 * is free on cache, we can reuse it, 
		 * memory bachane ka clever technique broo/babe 
		 */
		if (fileb) {
			Returnable = fileb->file;
			if (Returnable->eof) {
				Returnable->eof = 0;
				Returnable->current = Returnable->first_block;
				Returnable->fileCopyCount += 1;
				return Returnable;
			}
		}
		//else executing rest of the code
	}
	if (fs == __RootFS) {
		/* just skip the '/' from the path */
		char* next = strchr(path, '/');
		if (next)
			next++;

		if (fs->open)
			Returnable = fs->open(fs, path);
	} else {
		char* next = strchr(path, '/');
		if (next)
			next++;

		char pathname[16];
		//UARTDebugOut("[aurora]: vfs pathname : %s \r\n", path);
		int i = 0;
		for (i = 0; i < 15; i++) {
			if ((next[i] == '/') || (next[i] == '\0'))
				break;
			pathname[i] = next[i];
			dsb_ish();
		}
		pathname[i] = 0;
		//aa64_data_cache_clean_range(&pathname, 16);

		/* skip the fs filename, from the path
		 * and just pass the required path */
		if (strcmp(fs->filename, pathname) == 0)
			next += i;
		dsb_ish();
		dsb_sy_barrier();
		if (fs->open) {
			//AA64SleepUS(600);
			Returnable = fs->open(fs, next);
			/* wait for sometimes, let the cache things get completed
			 * let it fetch the data from main memory  */
			//AA64SleepUS(600);
		}
	}
	data_cache_flush((uint64_t*)Returnable);
	return Returnable;
}

/**
 * @brief AuVFSNodeIOControl -- Calls node's iocontrol pointer
 * @param node -- pointer to fsnode
 * @param code -- code to pass
 * @param arg -- extra arguments
 * @return return value returned by specific iocontrol handler
 * to the caller
 */
AU_EXTERN AU_EXPORT int AuVFSNodeIOControl(AuVFSNode* node, int code, void* arg) {
	if (node->iocontrol) {
		int val = node->iocontrol(node, code, arg);
		return val;
	}
	return 0;
}

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
										 uint32_t length) {
	if (node) {
		if (node->read)
			return node->read(node, file, buffer, length);
	}
	return -1;
}

/**
 * @brief AuVFSNodeWrite -- write to file system
 * @param node -- file system node to use
 * @param file -- file node to use
 * @param buffer -- buffer to write
 * @param length -- length of the data
 */
AU_EXTERN AU_EXPORT void
AuVFSNodeWrite(AuVFSNode* node, AuVFSNode* file, uint64_t* buffer, uint32_t length) {
	if (!node)
		return;
	if (node->write)
		node->write(node, file, buffer, length);
}

/**
 * @brief AuVFSNodeReadBlock -- read a block size data from file system
 * @param node -- file system node to use
 * @param file -- file node to use
 * @param buffer -- buffer area to read to
 * @return always <= 4kib
 */
AU_EXTERN AU_EXPORT size_t AuVFSNodeReadBlock(AuVFSNode* node, AuVFSNode* file, uint64_t* buffer) {
	size_t read_bytes = 0;
	if (!node)
		return read_bytes;
	if (node->read_block)
		read_bytes = node->read_block(node, file, buffer);
	return read_bytes;
}

/**
 * @brief AuVFSCreateDir -- creates a new dir
 * @param fsys -- Pointer to file system
 * @param dirname -- directory name
 * @return pointer to newly created directory node
 */
AU_EXTERN AU_EXPORT AuVFSNode* AuVFSCreateDir(AuVFSNode* fsys, char* dirname) {
	if (!fsys)
		return NULL;
	AuVFSNode* ret = NULL;
	if (fsys->create_dir)
		ret = fsys->create_dir(fsys, dirname);
	return ret;
}

/**
 * @brief AuVFSCreateFile -- create a new file
 * @param fsys -- Pointer to file system
 * @param filename -- filename
 * @return pointer to newly created file node
 */
AU_EXTERN AU_EXPORT AuVFSNode* AuVFSCreateFile(AuVFSNode* fsys, char* filename) {
	if (!fsys)
		return NULL;
	AuVFSNode* ret = NULL;
	if (fsys->create_file)
		ret = fsys->create_file(fsys, filename);
	return ret;
}

/**
 * @brief AuVFSRemoveFile --remove a file from a file system
 * @param fsys -- Pointer to file system
 * @param file -- Pointer to file to remove
 * @return 0 on success, -1 on failure
 */
int AuVFSRemoveFile(AuVFSNode* fsys, AuVFSNode* file) {
	if (!fsys)
		return -1;
	if (!file)
		return -1;
	if (!(file->flags & FS_FLAG_GENERAL))
		return -1;
	if ((file->flags & FS_FLAG_DEVICE) || (file->flags & FS_FLAG_FILE_SYSTEM))
		return -1;
	int ret = -1;
	if (fsys->remove_file)
		ret = fsys->remove_file(fsys, file);
	return ret;
}

/**
 * @brief AuVFSRemoveDir -- removes an empty directory
 * @param fsys -- Pointer to file system
 * @param file -- Pointer to directory needs to
 * be deleted
 * @return 0 on success, -1 on failure
 */
int AuVFSRemoveDir(AuVFSNode* fsys, AuVFSNode* file) {
	if (!fsys)
		return -1;
	if (!file)
		return -1;
	if ((file->flags & FS_FLAG_DEVICE) || (file->flags & FS_FLAG_FILE_SYSTEM))
		return -1;
	if (fsys->remove_dir && (file->flags & FS_FLAG_DIRECTORY))
		return fsys->remove_dir(fsys, file);

	return 0;
}

/**
 * @brief AuVFSNodeClose -- close a file system or file
 * @param node -- file system node to use
 * @param file -- file to close
 */
AU_EXTERN AU_EXPORT void AuVFSNodeClose(AuVFSNode* node, AuVFSNode* file) {
	if (node->close)
		node->close(node, file);
}

/**
 * @brief AuVFSRemoveFileSystem -- removes a file system
 * @param node -- file system node to remove
 */
AU_EXTERN AU_EXPORT int AuVFSRemoveFileSystem(AuVFSNode* node) {
	if (!node)
		return -1;
	int index = -1;
	for (unsigned int i = 0; i < __RootContainer->childs->pointer; i++) {
		AuVFSNode* _node = (AuVFSNode*)list_get_at(__RootContainer->childs, i);
		if (_node == node) {
			index = (int)i;
			break;
		}
	}

	if (index < 0)
		return -1;
	list_remove(__RootContainer->childs, (unsigned int)index);
	if (node->close)
		return node->close(node, NULL);
	else {
		kfree(node);
		return 0;
	}
}

/**
* @brief AuVFSGetBlockFor -- returns a block number for
* certain byte offset of file
* @param node -- file system node
* @param file -- pointer to file structure
* @param offset -- byte offset
* @return block number of that offset
*/
AU_EXTERN AU_EXPORT size_t AuVFSGetBlockFor(AuVFSNode* node, AuVFSNode* file, uint64_t offset) {
	if (node) {
		if (node->get_blockfor)
			return node->get_blockfor(node, file, offset);
	}
	return -1;
}

/**
 * @brief AuVFSSetCred -- set gid/uid value for a file
 * @param node -- file node
 * @param uid -- user id 
 * @param gid -- group id 
 * @return zero on success, one on failure
 */
AU_EXTERN AU_EXPORT int AuVFSSetCred(AuVFSNode* node, UID_NUM uid, GID_NUM gid) {
	if (!node)
		return 1;
	node->uid = uid;
	node->gid = gid;
	return 0;
}

/**
 * @brief AuVFSPoll -- poll callback interface 
 * @param file -- pointer to pollable file
 * @param pt -- Pointer to PollTable holding information about caller
 */
AU_EXTERN AU_EXPORT int AuVFSPoll(AuVFSNode* file, AuPollTable* pt) {
	if (!file)
		return -1;
	if (!pt)
		return -1;
	if (file->poll)
		return file->poll(file, file, pt);
}
