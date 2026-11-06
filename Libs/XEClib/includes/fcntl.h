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

#ifndef __FCNTL_H__
#define __FCNTL_H__

#include <sys/types.h>

#define F_DUPFD	 0x01
#define F_GETFD	 0x02
#define F_SETFD	 0x04
#define F_GETFL	 0x08
#define F_SETFL	 0x10
#define F_GETLK	 0x11
#define F_SETLK	 0x12
#define F_SETLKW 0x14

#define FD_CLOEXEC 0x01

#define F_RDLCK 0x01
#define F_UNLCK 0x02
#define F_WRLCK 0x04

#define O_CREAT		0x0001
#define O_EXCL		0x0002
#define O_NOCTTY	0x0004
#define O_TRUNC		0x0008
#define O_DIRECTORY 0x0010

#define O_APPEND 0x0020
#define O_DSYNC	 0x0040

#define O_NONBLOCK 0x0080
#define O_RSYNC	   0x0100
#define O_SYNC	   0x0200

#define O_RDONLY  0x0400
#define O_RDWR	  0x0800
#define O_WRONLY  0x1000
#define O_ACCMODE 0x1C00

typedef struct {
	short l_type;
	short l_whence;
	off_t l_start;
	off_t l_len;
	pid_t l_pid;
} flock;

#endif