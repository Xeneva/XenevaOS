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

#include <Fs/vfs.h>
#include <Mm/kmalloc.h>
#include <Fs/Dev/devfs.h>
#include <_null.h>
#include <aucon.h>
#include <string.h>
#include <Hal/serial.h>
#include <Fs/pipe.h>


AuVFSContainer* __RootContainer;
AuVFSNode* __RootFS;
bool _vfs_debug_on;
static char _mount_letters[26];
/*
 * AuVFSInitialise -- initialise the virtual
 * file system
 */
void AuVFSInitialise() {
	AuVFSContainer* _root = (AuVFSContainer*)kmalloc(sizeof(AuVFSContainer));
	_root->childs = initialize_list();
	__RootContainer = _root;
	__RootFS = NULL;
	_vfs_debug_on = false;
	/* initialise the device file system */
	AuDeviceFsInitialize();
	AuPipeFSInitialise();
	/* here we need to mount the
	 * root file system
	 */

	for (int i = 0; i < 26; i++)
		_mount_letters[i] = 0;
}

/** 
 * @brief AuVFSReserveMountPointLetter -- reserve a letter to 
 * mount a file system
 */
char* AuVFSReserveMountPointLetter() {
	static char path[4];
	for (int i = 0; i < 26; i++) {
		if (_mount_letters[i] == 1) 
			continue;
		_mount_letters[i] = 1;
		path[0] = '/';
		path[1] = 'a' + i;
		path[2] = '\0';
		return path;
	}
	return NULL;
}


/*
 * AuVFSFind -- Searches a filesystem an return it to 
 * the caller
 * @param path -- path of the file system
 */
AuVFSNode* AuVFSFind(char* path) {

	AuVFSNode* Returnable = NULL;
	/* first of all search all file system 
	 * skipping '/' of the path 
	 */
	char* next = strchr(path, '/');
	if (next)
		next++;

	char pathname[16];
	int i;
	for (i = 0; i < 16; i++) {
		if (next[i] == '/' || next[i] == '\0')
			break;
		pathname[i] = next[i];
	}
	pathname[i] = 0;

	for (int j = 0; j < __RootContainer->childs->pointer; j++) {
		AuVFSNode* node = (AuVFSNode*)list_get_at(__RootContainer->childs, j);
		if ((strcmp(node->filename, pathname) == 0) && (node->flags & FS_FLAG_FILE_SYSTEM)) {
			Returnable = node;
			break;
		}
	}

	if (!Returnable)
		Returnable = __RootFS;

	return Returnable;
}


/*
 * AuVFSAddFileSystem -- adds a file system to the
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


/*
 * AuVFSRegisterRoot -- register the root file 
 * system
 * @param fs -- fsnode to add
 */
void AuVFSRegisterRoot(AuVFSNode *fs) {
	if (__RootFS)
		return;
	__RootFS = fs;
}


/*
 * AuVFSOpen -- Opens a file 
 * @param path -- path to open
 */
AU_EXTERN AU_EXPORT AuVFSNode* AuVFSOpen(char* path){
	AuVFSNode *Returnable = NULL;
	AuVFSNode* fs = AuVFSFind(path);
	
	if (!fs) {
		AuTextOut("[aurora-vfs]: failed to find filesystem assigned to path -> %s \n", path);
		return NULL;
	}
	if (fs == __RootFS) {
		
		/* just skip the '/' from the path */
		char* next = strchr(path, '/');
		if (next)
			next++;

		if (fs->open)
			Returnable = fs->open(fs, path);

	}
	else {
		char* next = strchr(path, '/');
		if (next)
			next++;

		char pathname[16];
		int i = 0;
		for (i = 0; i < 16; i++) {
			if ((next[i] == '/') || (next[i] == '\0'))
				break;
			pathname[i] = next[i];
		}
		pathname[i] = 0;

		/* skip the fs filename, from the path
		 * and just pass the required path */
		if (strcmp(fs->filename, pathname) == 0) 
			next += i;
		if (fs->open)
			Returnable = fs->open(fs,next);
	}
	return Returnable;
}

/*
 * AuVFSNodeIOControl -- Calls node's iocontrol pointer
 * @param node -- pointer to fsnode
 * @param code -- code to pass
 * @param arg -- extra arguments
 */
AU_EXTERN AU_EXPORT int AuVFSNodeIOControl(AuVFSNode* node, int code, void* arg) {
	if (node->iocontrol){
		int val = node->iocontrol(node, code, arg);
		return val;
	}
	return 0;
}

/*
 * AuVFSNodeRead -- read from file system
 * @param node -- file system node to use
 * @param file -- file information to use
 * @param buffer -- buffer to write to
 * @param length -- length of the file
 */
AU_EXTERN AU_EXPORT size_t AuVFSNodeRead(AuVFSNode* node, AuVFSNode* file, uint64_t* buffer, uint32_t length) {
	if (node) {
		if (node->read)
			return node->read(node, file, buffer, length);
	}
	return -1;
}

/*
 * AuVFSNodeWrite -- write to file system
 * @param node -- file system node to use
 * @param file -- file node to use
 * @param buffer -- buffer to write
 * @param length -- length of the data
 */
AU_EXTERN AU_EXPORT void AuVFSNodeWrite(AuVFSNode* node, AuVFSNode * file, uint64_t *buffer, uint32_t length) {
	if (!node)
		return;
	if (node->write)
		node->write(node, file, buffer, length);
}

/*
 * AuVFSNodeReadBlock -- read a block size data from file system
 * @param node -- file system node to use
 * @param file -- file node to use
 * @param buffer -- buffer area to read to
 */
AU_EXTERN AU_EXPORT size_t AuVFSNodeReadBlock(AuVFSNode* node, AuVFSNode* file, uint64_t *buffer) {
	size_t read_bytes = 0;
	if (!node)
		return read_bytes;
	if (node->read_block)
		read_bytes = node->read_block(node, file, buffer);
	return read_bytes;
}

/*
 * AuVFSCreateDir -- creates a new dir
 * @param fsys -- Pointer to file system
 * @param dirname -- directory name
 */
AU_EXTERN AU_EXPORT AuVFSNode* AuVFSCreateDir(AuVFSNode* fsys, char* dirname) {
	if (!fsys)
		return NULL;
	AuVFSNode* ret = NULL;
	if (fsys->create_dir)
		ret = fsys->create_dir(fsys, dirname);
	return ret;
}

/*
 * AuVFSCreateFile -- create a new file
 * @param fsys -- Pointer to file system
 * @param filename -- filename
 */
AU_EXTERN AU_EXPORT AuVFSNode* AuVFSCreateFile(AuVFSNode* fsys, char* filename){
	if (!fsys)
		return NULL;
	AuVFSNode* ret = NULL;
	if (fsys->create_file)
		ret = fsys->create_file(fsys, filename);
	return ret;
}

/*
 * AuVFSRemoveFile --remove a file from a file system
 * @param fsys -- Pointer to file system
 * @param file -- Pointer to file to remove
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
}

/*
 * AuVFSRemoveDir -- removes an empty directory
 * @param fsys -- Pointer to file system
 * @param file -- Pointer to directory needs to 
 * be deleted
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
}


/*
 * AuVFSNodeClose -- close a file system or file
 * @param node -- file system node to use
 * @param file -- file to close
 */
AU_EXTERN AU_EXPORT void AuVFSNodeClose(AuVFSNode* node, AuVFSNode* file) {
	if (node->close)
		node->close(node, file);
}

/*
 * AuVFSRemoveFileSystem -- removes a file system 
 * @param node -- file system node to remove
 */
AU_EXTERN AU_EXPORT int AuVFSRemoveFileSystem(AuVFSNode* node) {
	int index = 0;
	for (int i = 0; i < __RootContainer->childs->pointer; i++) {
		AuVFSNode *_node = (AuVFSNode*)list_get_at(__RootContainer->childs, i);
		if (_node == node){
			index = i;
			break;
		}
	}
	list_remove(__RootContainer->childs, index);
	if (node->close)
		return node->close(node, NULL);
	else {
		kfree(node);
		return 0;
	}

}

/*
* AuVFSGetBlockFor -- returns a block number for
* certain byte offset of file
* @param node -- file system node
* @param file -- pointer to file structure
* @param offset -- byte offset
*/
AU_EXTERN AU_EXPORT size_t AuVFSGetBlockFor(AuVFSNode* node, AuVFSNode* file, uint64_t offset) {
	if (node){
		if (node->get_blockfor)
			return node->get_blockfor(node, file, offset);
	}
	return -1;
}


