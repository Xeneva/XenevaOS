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

#ifndef __FAT_DIR_H__
#define __FAT_DIR_H__

#include <Fs/Fat/Fat.h>
#include <Fs/Fat/FatFile.h>
#include <Fs/vfs.h>
#include <Fs/vdisk.h>
#include <stdint.h>


/*
* FatCreateDir -- create an empty directory
* @param fsys -- pointer to file system
* @param parent_clust -- parent cluster which will
* contain the newly created directory
* @param filename -- name of the directory
*/
extern AuVFSNode* FatCreateDir(AuVFSNode* fsys, char* filename);

/*
* FatRemoveDir -- removes an empty directory
* @param fsys -- Pointer to file system
* @param file -- Pointer to directory node
*/
extern int FatRemoveDir(AuVFSNode* fsys, AuVFSNode* file);

/*
* FatOpenDir -- opens a directory
* @param fs -- Pointer to file system node
* @param path -- path of the directory
*/
extern AuVFSNode* FatOpenDir(AuVFSNode* fs, char *path);

/*
* FatDirectoyRead -- read a fat directory entry
* @param fs -- File system node
* @param dir -- Directory file
* @param dirent -- Aurora Directory Entry
*/
extern int FatDirectoryRead(AuVFSNode* fs, AuVFSNode* dir, AuDirectoryEntry* dirent);
#endif