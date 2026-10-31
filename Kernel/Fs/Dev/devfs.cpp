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

#include <Fs/Dev/devfs.h>
#include <Fs/Dev/devinput.h>
#include <Mm/kmalloc.h>
#include <list.h>
#include <string.h>
#include <_null.h>
#include <Hal/serial.h>
#include <stdint.h>


/*
 * AuDeviceFsInitialize -- initialise the device
 * file system
 */
void AuDeviceFsInitialize() {
	AuVFSContainer *entries = (AuVFSContainer*)kmalloc(sizeof(AuVFSContainer));
	entries->childs = initialize_list();

	AuVFSNode *node = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(node, 0, sizeof(AuVFSNode));
	strcpy(node->filename, "dev");
	node->device = entries;
	node->flags |= FS_FLAG_FILE_SYSTEM;
	node->open = AuDevFSOpen;
	AuVFSAddFileSystem(node);

	AuDevInputInitialise();

}

/*
 * AuDevFSCreateFile -- create a file/directory
 * @param fs -- pointer to device file system
 * @param path -- path of the file
 * @param mode -- mode of the file either Directory
 * or General device file
 */
int AuDevFSCreateFile(AuVFSNode* fs, char* path, uint8_t mode) {
	AuVFSContainer* entries = (AuVFSContainer*)fs->device;
	if (!entries)
		return -1;

	AuVFSNode* file = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(file, 0, sizeof(AuVFSNode));

	if (mode & FS_FLAG_DIRECTORY) {
		AuVFSContainer* list = (AuVFSContainer*)kmalloc(sizeof(AuVFSContainer));
		list->childs = initialize_list();
		file->flags |= FS_FLAG_DIRECTORY;
		file->device = list;
	}
	else {
		file->flags = FS_FLAG_GENERAL | FS_FLAG_DEVICE;
		file->device = fs;
	}


	char* next = strchr(path, '/');
	if (next)
		next++;

	AuVFSContainer* first_list = entries;
	char pathname[16];
	while (next) {
		int i;
		for (i = 0; i < 16; i++) {
			if (next[i] == '/' || next[i] == '\0')
				break;
			pathname[i] = next[i];
		}
		pathname[i] = 0;

		for (int j = 0; j < first_list->childs->pointer; j++) {
			AuVFSNode* node_ = (AuVFSNode*)list_get_at(first_list->childs, j);
			if (strcmp(node_->filename, pathname) == 0) {
				if (node_->flags & FS_FLAG_DIRECTORY)
					first_list = (AuVFSContainer*)node_->device;
			}
		}

		next = strchr(next + 1, '/');
		if (next)
			next++;
	}
	strcpy(file->filename, pathname);
	list_add(first_list->childs, file);

	return 1;
}

/*
* AuDevFSAddFile -- adds a file/directory
* @param fs -- pointer to device file system
* @param path -- path of the file
* @param file -- file to add to dev fs
*/
int AuDevFSAddFile(AuVFSNode* fs, char* path, AuVFSNode* file) {
	AuVFSContainer* entries = (AuVFSContainer*)fs->device;
	if (!entries)
		return -1;

	char* next = strchr(path, '/');
	if (next)
		next++;

	AuVFSContainer* first_list = entries;
	char pathname[16];
	while (next) {
		int i;
		for (i = 0; i < 16; i++) {
			if (next[i] == '/' || next[i] == '\0')
				break;
			pathname[i] = next[i];
		}
		pathname[i] = 0;

		for (int j = 0; j < first_list->childs->pointer; j++) {
			AuVFSNode* node_ = (AuVFSNode*)list_get_at(first_list->childs, j);
			if (strcmp(node_->filename, pathname) == 0) {
				if (node_->flags & FS_FLAG_DIRECTORY)
					first_list = (AuVFSContainer*)node_->device;
			}
		}

		next = strchr(next + 1, '/');
		if (next)
			next++;
	}
	
	list_add(first_list->childs, file);

	return 1;
}

/* For debug purpose */
void AuDevFSListDir(AuVFSNode* fs, AuVFSNode* dir) {
	AuVFSContainer* entries = (AuVFSContainer*)dir->device;
	SeTextOut("Listing Directory -> %s \r\n", dir->filename);
	for (int i = 0; i < entries->childs->pointer; i++) {
		AuVFSNode* node_ = (AuVFSNode*)list_get_at(entries->childs, i);
		char* mode = "";
		if (node_->flags & FS_FLAG_DEVICE)
			mode = "Device";
		else if (node_->flags & FS_FLAG_DIRECTORY)
			mode = "Directory";
		else
			mode = "Unknown";
		SeTextOut("File ->  %s mode - %s \r\n", node_->filename, mode);
	}
}
/* for debug purpose */
void AuDevFSList(AuVFSNode* fs) {
	AuVFSContainer* entries = (AuVFSContainer*)fs->device;
	SeTextOut("Listing device fs \r\n");
	for (int i = 0; i < entries->childs->pointer; i++) {
		AuVFSNode* node_ = (AuVFSNode*)list_get_at(entries->childs, i);
		char* mode = "";
		if (node_->flags & FS_FLAG_DEVICE)
			mode = "Device";
		else if (node_->flags & FS_FLAG_DIRECTORY)
			mode = "Directory";
		else if (node_->flags & FS_FLAG_PIPE)
			mode = "Pipe";
		else
			mode = "Unknown";
		SeTextOut("\%s mode - %s \r\n", node_->filename, mode);
		if (node_->flags & FS_FLAG_DIRECTORY)
			AuDevFSListDir(fs, node_);
	}
}

/*
* AuDevFSOpen -- open a device file and return to the
* caller
* @param fs -- file system node
* @param path -- path of the dev file
*/
AuVFSNode* AuDevFSOpen(AuVFSNode* fs, char* path) {
	AuVFSContainer* entries = (AuVFSContainer*)fs->device;
	/* now verify the path and add the directory to the list */
	char* next = strchr(path, '/');
	if (next)
		next++;

	AuVFSContainer *first_list = entries;
	AuVFSNode *node_to_ret = NULL;
	while (next) {
		char pathname[16];
		int i;
		for (i = 0; i < 16; i++) {
			if (next[i] == '/' || next[i] == '\0')
				break;
			pathname[i] = next[i];
		}
		pathname[i] = 0;

		for (int j = 0; j < first_list->childs->pointer; j++) {
			AuVFSNode* node_ = (AuVFSNode*)list_get_at(first_list->childs, j);
			if (strcmp(node_->filename, pathname) == 0) {
				if (node_->flags & FS_FLAG_DIRECTORY)
					first_list = (AuVFSContainer*)node_->device;
				else if (node_->flags & FS_FLAG_DEVICE)
					node_to_ret = node_;
			}
		}

		next = strchr(next + 1, '/');
		if (next)
			next++;
	}

	if (node_to_ret)
		return node_to_ret;
	else
		return NULL;
}

/*
 * AuDevFSRemoveFile -- remove a file from device
 * file system
 * @param fs -- pointer to the file system 
 * @param path -- path of the file
 */
int AuDevFSRemoveFile(AuVFSNode* fs,char* path) {
	AuVFSContainer* entries = (AuVFSContainer*)fs->device;
	/* now verify the path and add the directory to the list */
	char* next = strchr(path, '/');
	if (next)
		next++;

	AuVFSContainer *first_list = entries;
	AuVFSNode *node_to_rem = NULL;
	int index = 0;
	while (next) {
		char pathname[16];
		int i;
		for (i = 0; i < 16; i++) {
			if (next[i] == '/' || next[i] == '\0')
				break;
			pathname[i] = next[i];
		}
		pathname[i] = 0;

		for (int j = 0; j < first_list->childs->pointer; j++) {
			AuVFSNode* node_ = (AuVFSNode*)list_get_at(first_list->childs, j);
			if (strcmp(node_->filename, pathname) == 0) {
				if (node_->flags & FS_FLAG_DIRECTORY)
					first_list = (AuVFSContainer*)node_->device;
				else if (node_->flags & FS_FLAG_DEVICE) {
					node_to_rem = node_;
					index = j;
				}
			}
		}

		next = strchr(next + 1, '/');
		if (next)
			next++;
	}

	/* only files can be removed */
	if (node_to_rem && node_to_rem->flags & FS_FLAG_DIRECTORY)
		return -1;

	if (node_to_rem && (node_to_rem->flags & FS_FLAG_DEVICE)){
		list_remove(first_list->childs, index);
		/* here, call node_to_rem->close(node_to_rem) in order
		 * to inform the device to shutdown itself */
		if (node_to_rem->close)
			node_to_rem->close(node_to_rem, NULL);
		kfree(node_to_rem);
		return 1;
	}

	return -1;
}