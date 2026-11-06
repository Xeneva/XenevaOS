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

#ifndef __FAT_FILE_H__
#define __FAT_FILE_H__

#include <Fs/Fat/Fat.h>
#include <Fs/vdisk.h>
#include <Fs/vfs.h>

/*
* FatCreateFile -- Creates a blank file
* @param fsys -- Pointer to file system
* @param parent_cluster -- Parent cluster, if it's
* 0 then parent_cluster equals to root_cluster
* @param filename -- name of the file
*/
extern AuVFSNode* FatCreateFile(AuVFSNode* fsys, char* filename);

/*
* FatFileWriteContent -- write contents to fat file (4kib)
* @param fsys -- Pointer to file system node
* @param first_cluster -- first cluster of the file
* @param buffer -- buffer to write
*/
extern void FatFileWriteContent(AuVFSNode* fsys, AuVFSNode* file, uint64_t* buffer);


/*
* FatFileWriteDone -- sets the current position
* to first cluster
* @param file -- Pointer to file
*/
extern void FatFileWriteDone(AuVFSNode* file);

/*
* FatFileUpdateSize -- updates the current file size
* @param fsys -- Pointer to file system
* @param file -- Pointer to file
* @param size -- size in bytes
*/
extern void FatFileUpdateSize(AuVFSNode* fsys, AuVFSNode* file, size_t size);

/*
* FatFileGetParent -- Returns the parent directory file
* for a new file
* @param fsys -- Pointer to file system
* @param filename -- file path and name to look upon
*/
extern AuVFSNode* FatFileGetParent(AuVFSNode* fsys, const char* filename);

/*
* FatWrite -- write callback
* @param fsys -- pointer to file system
* @param file -- pointer to file
* @param buffer -- buffer to write
* @param length -- bytes needed to write
*/
extern size_t FatWrite(AuVFSNode* fsys, AuVFSNode* file, uint64_t* buffer, uint32_t length);

/*
* FatFileUpdateFilename -- updates the current file name
* @param fsys -- Pointer to file system
* @param file -- Pointer to file
* @param newname -- new filename
*/
extern int FatFileUpdateFilename(AuVFSNode* fsys, AuVFSNode* file, char* newname);

/*
* FatFileRemove -- remove a file
* @param fsys -- Pointer to file
* @param file -- file to remove
*/
extern int FatFileRemove(AuVFSNode* fsys, AuVFSNode* file);

#endif