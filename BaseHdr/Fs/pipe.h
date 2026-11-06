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

#ifndef __PIPE_H__
#define __PIPE_H__

#include <stdint.h>
#include <list.h>
#include <Fs/vfs.h>

#pragma pack(push,1)
typedef struct _pipe_ {
	uint8_t* buffer;
	size_t write_ptr;
	size_t read_ptr;
	size_t size;
	size_t refcount;
	list_t* readers_wait_queue;
	list_t* writers_wait_queue;
}AuPipe;
#pragma pack(pop)

/**
 * @brief AuPipeUnread -- get the available read
 * space from the pipe
 * @param pipe -- Pointer to the pipe
 */
extern size_t AuPipeUnread(AuPipe* pipe);

/**
* @brief AuCreatePipe -- creates a new pipe
* @param name -- name of the pipe
* @param sz -- Size of the pipe
*/
extern int AuCreatePipe(char* name, size_t sz);

/**
* @brief AuPipeFSAddFile -- adds a file/directory
* @param fs -- pointer to device file system
* @param path -- path of the file
* @param file -- file to add to dev fs
* @return 0 on success -1 failure
*/
extern int AuPipeFSAddFile(AuVFSNode* fs, char* path, AuVFSNode* file);

/**
 * @brief AuPipeFSInitialise -- initialise the pipe filesystem
 */
extern void AuPipeFSInitialise();
#endif