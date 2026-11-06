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

#ifndef __DEVFS_H__
#define __DEVFS_H__

#include <stdint.h>
#include <aurora.h>
#include <Fs/vfs.h>

/* Device file system, where all driver registers
 * their own device node
 */


/*
* AuDeviceFsInitialize -- initialise the device
* file system
*/
extern void AuDeviceFsInitialize();

/*
* AuDevFSCreateFile -- create a file/directory
* @param fs -- pointer to device file system
* @param path -- path of the file
* @param mode -- mode of the file either Directory
* or General device file
*/
extern int AuDevFSCreateFile(AuVFSNode* fs, char* path, uint8_t mode);

/*
* AuDevFSAddFile -- adds a file/directory
* @param fs -- pointer to device file system
* @param path -- path of the file
* @param file -- file to add to dev fs
*/
AU_EXTERN AU_EXPORT int AuDevFSAddFile(AuVFSNode* fs, char* path, AuVFSNode* file);
/*
* AuDevFSOpen -- open a device file and return to the
* caller
* @param fs -- file system node
* @param path -- path of the dev file
*/
extern AuVFSNode* AuDevFSOpen(AuVFSNode* fs, char* path);

/* Hold/release a user-visible device node reference. Loadable-driver-backed
 * nodes also hold one driver reference while at least one fd is open. */
extern bool AuDevFSNodeOpen(AuVFSNode* node);
extern void AuDevFSNodeClose(AuVFSNode* node);

/* For debug purpose */
extern void AuDevFSList(AuVFSNode* fs);

/*
* AuDevFSRemoveFile -- remove a file from device
* file system
* @param fs -- pointer to the file system
* @param path -- path of the file
*/
AU_EXTERN AU_EXPORT int AuDevFSRemoveFile(AuVFSNode* fs, char* path);
#endif
