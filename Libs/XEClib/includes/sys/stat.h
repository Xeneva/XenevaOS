/**
* @file stat.h
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

#ifndef __STAT_H__
#define __STAT_H__

#include <sys/types.h>
#include <time.h>
#include <_xeneva.h>

#ifdef __cplusplus
XE_EXTERN {
#endif

	// mode_t values for st_mode - values in octal
#define S_IFMT	 0170000 // bit mask for the file type
#define S_IFSOCK 0140000 // socket
#define S_IFLNK	 0120000 // symbolic link
#define S_IFREG	 0100000 // regular file
#define S_IFBLK	 0060000 // block device
#define S_IFDIR	 0040000 // directory
#define S_IFCHR	 0020000 // character device
#define S_IFIFO	 0010000 // FIFO
#define S_ISUID	 0004000 // set UID bit
#define S_ISGID	 0002000 // set-group-ID bit
#define S_ISVTX	 0001000 // sticky bit

	// Macros for interpreting st_mode
#define S_ISREG(m)	(((m) & S_IFREG) == S_IFREG)   // regular file?
#define S_ISDIR(m)	(((m) & S_IFDIR) == S_IFDIR)   // directory?
#define S_ISCHR(m)	(((m) & S_IFCHR) == S_IFCHR)   // character device?
#define S_ISBLK(m)	(((m) & S_IFBLK) == S_IFBLK)   // block device?
#define S_ISFIFO(m) (((m) & S_IFIFO) == S_IFIFO)   // FIFO (named pipe)?
#define S_ISLNK(m)	(((m) & S_IFLNK) == S_IFLNK)   // symbolic link?
#define S_ISSOCK(m) (((m) & S_IFSOCK) == S_IFSOCK) // socket?

	struct stat {
		dev_t st_dev;		  // device
		ino_t st_ino;		  // inode
		mode_t st_mode;		  // protection
		nlink_t st_nlink;	  // number of hard links
		uid_t st_uid;		  // user ID of owner
		gid_t st_gid;		  // group ID of owner
		dev_t st_rdev;		  // device type (if inode device)
		off_t st_size;		  // total size, in bytes
		blksize_t st_blksize; // blocksize for filesystem I/O
		blkcnt_t st_blocks;	  // number of blocks allocated
		time_t st_atime;	  // time of last access
		time_t st_mtime;	  // time of last modification
		time_t st_ctime;	  // time of last change
	};

	XE_LIB int mkdir(const char*, mode_t);
	XE_LIB int stat(const char*, struct stat*);

#ifdef __cplusplus
}
#endif

#endif