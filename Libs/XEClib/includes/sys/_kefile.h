/**
* @file _kefile.h
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

#ifndef __KE_FILE_H__
#define __KE_FILE_H__

#include <_xeneva.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
XE_EXTERN {
#endif

	/* file open modes*/
#define FILE_OPEN_READ_ONLY (1 << 1)
#define FILE_OPEN_WRITE		(1 << 2)
#define FILE_OPEN_CREAT		(1 << 3)

#define FILE_DIRECTORY	 (1 << 1)
#define FILE_GENERAL	 (1 << 2)
#define FILE_DEVICE		 (1 << 3)
#define FILE_DELETED	 (1 << 4)
#define FILE_INVALID	 (1 << 5)
#define FILE_FILE_SYSTEM (1 << 6)
#define FILE_PIPE		 (1 << 7)

#define XENEVA_STDIN  0
#define XENEVA_STDOUT 1
#define XENEVA_STDERR 2

/*  number of supported storage device*/
#define XE_MAX_STORAGE_DEVICE 26
/* number of partitions in each storage device */
#define XE_MAX_PARTITION 128

#ifdef ARCH_X64
#pragma pack(push, 1)
#endif
	typedef struct _GUID {
		uint32_t Data1;
		uint16_t Data2;
		uint16_t Data3;
		uint8_t Data4[8];
	} GUID;
#ifdef ARCH_X64
#pragma pack(pop)
#endif

#ifdef ARCH_X64
#pragma pack(push, 1)
#endif
	typedef struct _disk_info_ {
		char diskname[40];
		char serialNumber[20];
		uint8_t vDiskID;
		int num_partition;
		uint64_t maxBlocks;
		uint64_t blocksSize;
	} XEVDiskInfo;
#ifdef ARCH_X64
#pragma pack(pop)
#endif

#ifdef ARCH_X64
#pragma pack(push, 1)
#endif
	typedef struct _disk_partition_info_ {
		char mountedName[32];
		GUID partitionGUID;
		GUID uniqueGUID;
		uint64_t startingLBA;
	} XEVDiskPartitionInfo;
#ifdef ARCH_X64
#pragma pack(pop)
#endif

	typedef struct _XEFileStatus_ {
		uint8_t filemode; //mode of the file
		size_t size;	  //size in bytes
		uint32_t current_block;
		uint32_t start_block;
		uint32_t user_id;  //for future use
		uint32_t group_id; //for future use
		uint32_t num_links;
		uint8_t eof;
	} XEFileStatus;

	//#pragma pack(push,1)
	typedef struct _XEFileControl_ {
		int syscall_magic;
		uint8_t uchar_1;
		uint8_t uchar_2;
		uint16_t ushort_1;
		uint16_t ushort_2;
		uint32_t uint_1;
		uint32_t uint_2;
		uint64_t ulong_1;
		uint64_t ulong_2;
	} XEFileIOControl;
	//#pragma pack(pop)

	//#pragma pack(push,1)
	typedef struct _XEDirectoryEnty_ {
		char filename[32];
		int index;
		int size;
		int date;
		int time;
		uint8_t flags;
	} XEDirectoryEntry;
	//#pragma pack(pop)

	XE_LIB int _KeOpenFile(char* pathname, int mode);
	XE_LIB size_t _KeReadFile(int fd, void* buffer, size_t length);
	XE_LIB size_t _KeWriteFile(int fd, void* buffer, size_t length);
	XE_LIB int _KeCreateDir(char* filename);
	XE_LIB int _KeRemoveFile(char* pathname);
	XE_LIB int _KeCloseFile(int fd);
	XE_LIB int _KeFileIoControl(int fd, int code, void* arg);
	XE_LIB int _KeFileStat(int fd, void* buf);
	XE_LIB int _KeOpenDir(char* filename);
	XE_LIB int _KeReadDir(int dirfd, void* dirent);
	XE_LIB int _KeFileSetOffset(int fd, size_t offset);
	XE_LIB int _KeCreatePipe(char* name, size_t sz);
	XE_LIB int _KeGetStorageDiskInfo(uint8_t diskID, void* buffer);
	XE_LIB int _KeGetStoragePartitionInfo(uint8_t diskID, uint8_t partitionID, void* buffer);
	XE_LIB int _KeOwnFsys(const char* path, uid_t uid, gid_t gid);
#ifdef __cplusplus
}
#endif

#endif