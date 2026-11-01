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
#include <Hal/AA64/aa64cpu.h>
#include <Mm/kmalloc.h>
#include <list.h>
#include <string.h>
#include <_null.h>
#include <aucon.h>
#include <Drivers/uart.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Cred/group.h>
#include <aucomponent.h>

/*
 * AuDeviceFsInitialize -- initialise the device
 * file system
 */
void AuDeviceFsInitialize() {
	AuVFSContainer* entries = (AuVFSContainer*)kmalloc(sizeof(AuVFSContainer));
	entries->childs = initialize_list();

	AuVFSNode* node = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(node, 0, sizeof(AuVFSNode));
	strcpy(node->filename, "dev");
	node->device = entries;
	node->flags |= FS_FLAG_FILE_SYSTEM;
	node->open = AuDevFSOpen;
	AuVFSAddFileSystem(node);

	AuDevInputInitialise();
	AuTextOut("[aurora]: devfs initialized \r\n");
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
	} else {
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
	file->mode = 0660;
	list_add(first_list->childs, file);
	//aa64_data_cache_clean_range(first_list, sizeof(list_t));
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

		/* scan from next, not next+1; path "/" leaves next at the NUL
		 * and next+1 would read past the string literal */
		next = strchr(next, '/');
		if (next)
			next++;
	}

	list_add(first_list->childs, file);

	return 1;
}

/* For debug purpose */
void AuDevFSListDir(AuVFSNode* fs, AuVFSNode* dir) {
	AuVFSContainer* entries = (AuVFSContainer*)dir->device;
	UARTDebugOut("Listing Directory -> %s \r\n", dir->filename);
	for (int i = 0; i < entries->childs->pointer; i++) {
		AuVFSNode* node_ = (AuVFSNode*)list_get_at(entries->childs, i);
		if (!node_)
			continue;
		char* mode = "";
		if (node_->flags & FS_FLAG_DEVICE)
			mode = "Device";
		else if (node_->flags & FS_FLAG_DIRECTORY)
			mode = "Directory";
		else
			mode = "Unknown";
		UARTDebugOut("File ->  %s mode - %s \r\n", node_->filename, mode);
	}
}
/* for debug purpose */
void AuDevFSList(AuVFSNode* fs) {
	AuVFSContainer* entries = (AuVFSContainer*)fs->device;
	UARTDebugOut("Listing device fs \r\n");
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
		UARTDebugOut("%s mode - %s \r\n", node_->filename, mode);
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

	AuVFSContainer* first_list = entries;
	AuVFSNode* node_to_ret = NULL;
	//UARTDebugOut("[aurora]: DevFS opening : %s \r\n", path);
	while (next) {
		char pathname[16];
		int i;
		for (i = 0; i < 16; i++) {
			if (next[i] == '/' || next[i] == '\0')
				break;
			pathname[i] = next[i];
		}
		pathname[i] = 0;
		//aa64_data_cache_clean_range(&pathname, 16);
		//AA64SleepUS(600);
		UARTDebugOut("devfs: open: %s \r\n", pathname);
		for (int j = 0; j < first_list->childs->pointer; j++) {
			AuVFSNode* node_ = (AuVFSNode*)list_get_at(first_list->childs, j);
			//UARTDebugOut("node_->filename : %s \r\n", node_->filename);
			if (strcmp(node_->filename, pathname) == 0) {
				if (node_->flags & FS_FLAG_DIRECTORY)
					first_list = (AuVFSContainer*)node_->device;
				else if (node_->flags & FS_FLAG_DEVICE)
					node_to_ret = node_;
			}
		}
		/* scan from next, not next+1; a trailing-NUL next (path "/")
		 * would make next+1 read past the caller's string */
		next = strchr(next, '/');
		if (next)
			next++;
	}

	if (node_to_ret)
		return node_to_ret;

	UARTDebugOut(
		"[devfs]: open failed for '%s', container entries=%d\r\n", path, entries->childs->pointer);
	for (int j = 0; j < (int)entries->childs->pointer; j++) {
		AuVFSNode* node_ = (AuVFSNode*)list_get_at(entries->childs, j);
		if (!node_) {
			UARTDebugOut("[devfs]:   [%d] <null entry>\r\n", j);
			continue;
		}
		UARTDebugOut("[devfs]:   [%d] '%s' flags=%x\r\n", j, node_->filename, node_->flags);
	}
	return NULL;
}

/* A device node is shared by every fd opened through its devfs name. Keep
 * one component reference for the shared node rather than one per fd; the
 * existing fileCopyCount accounts for the individual fd/dup references. */
static AuDriver* AuDevFSNodeDriver(AuVFSNode* node) {
	if (!node)
		return NULL;
	AuDriver* driver = NULL;
	if (node->open)
		driver = AuDriverOwnerOf((uint64_t)(uintptr_t)node->open);
	if (!driver && node->read)
		driver = AuDriverOwnerOf((uint64_t)(uintptr_t)node->read);
	if (!driver && node->write)
		driver = AuDriverOwnerOf((uint64_t)(uintptr_t)node->write);
	if (!driver && node->close)
		driver = AuDriverOwnerOf((uint64_t)(uintptr_t)node->close);
	if (!driver && node->iocontrol)
		driver = AuDriverOwnerOf((uint64_t)(uintptr_t)node->iocontrol);
	return driver;
}

bool AuDevFSNodeOpen(AuVFSNode* node) {
	if (!node || !(node->flags & FS_FLAG_DEVICE) || node->fileCopyCount == UINT16_MAX)
		return false;
	if (node->fileCopyCount == 0) {
		AuDriver* driver = AuDevFSNodeDriver(node);
		if (driver && AuDriverRef(driver) < 0)
			return false;
	}
	node->fileCopyCount++;
	return true;
}

void AuDevFSNodeClose(AuVFSNode* node) {
	if (!node || !(node->flags & FS_FLAG_DEVICE))
		return;
	/* Keep the driver image referenced until its close callback returns. */
	if (node->close)
		node->close(node, node);
	if (!node->fileCopyCount)
		return;
	node->fileCopyCount--;
	if (node->fileCopyCount)
		return;
	AuDriver* driver = AuDevFSNodeDriver(node);
	if (driver)
		AuDriverUnref(driver);
	if (node->flags & FS_FLAG_DELETED)
		kfree(node);
}

/*
 * AuDevFSRemoveFile -- remove a file from device
 * file system
 * @param fs -- pointer to the file system
 * @param path -- path of the file
 */
int AuDevFSRemoveFile(AuVFSNode* fs, char* path) {
	AuVFSContainer* entries = (AuVFSContainer*)fs->device;
	/* now verify the path and add the directory to the list */
	char* next = strchr(path, '/');
	if (next)
		next++;

	AuVFSContainer* first_list = entries;
	AuVFSNode* node_to_rem = NULL;
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

	if (node_to_rem && (node_to_rem->flags & FS_FLAG_DEVICE)) {
		list_remove(first_list->childs, index);
		node_to_rem->flags |= FS_FLAG_DELETED;
		/* A removed node can still be the object stored in existing fds. Its
		 * close callback and driver image must remain live until the final fd
		 * release; AuDevFSNodeClose() performs that deferred destruction. */
		if (!node_to_rem->fileCopyCount) {
			if (node_to_rem->close)
				node_to_rem->close(node_to_rem, NULL);
			kfree(node_to_rem);
		}
		return 1;
	}

	return -1;
}
