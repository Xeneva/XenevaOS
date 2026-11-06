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

#include <Fs/tty.h>
#include <Fs/vfs.h>
#include <Fs/dev/devfs.h>
#include <_null.h>
#include <Mm/kmalloc.h>
#include <string.h>
#include <stdio.h>
#include <Hal/x86_64_sched.h>
#include <process.h>
#include <Hal/x86_64_signal.h>
#include <Hal/x86_64_hal.h>
#include <Hal/serial.h>
#include <aucon.h>

size_t master_count = 0;
size_t slave_count = 0;

TTY *root = NULL;
TTY* last = NULL;

void AuTTYInsert(TTY* tty) {
	tty->next = NULL;
	tty->prev = NULL;

	if (root == NULL) {
		last = tty;
		root = tty;
	}
	else {
		last->next = tty;
		tty->prev = last;
	}
	last = tty;
}

void AuTTYDelete(TTY* tty) {
	if (root == NULL)
		return;
	if (tty == root)
		root = root->next;
	else
		tty->prev->next = tty->next;

	if (tty == last)
		last = tty->prev;
	else
		tty->next->prev = tty->prev;

	kfree(tty);
}

void AuTTYWriteSlave(TTY* tty, uint8_t c) {
	AuCircBufPut(tty->slavebuf, c);
}

void AuTTYWriteMaster(TTY* tty, uint8_t c) {
	AuCircBufPut(tty->masterbuf, c);
}

/* AuTTYProcessLine -- Line Discipline layer*/
void AuTTYProcessLine(TTY* tty, uint8_t c) {
	if (tty->term.c_lflag & ISIG) {
		int sig = -1;
		if (c == tty->term.c_cc[VINTR])
			sig = SIGINT;
		else if (c == tty->term.c_cc[VQUIT])
			sig = SIGQUIT;
		else if (c == tty->term.c_cc[VSUSP])
			sig = SIGTSTP;

		if (sig != -1) {
			if (tty->term.c_lflag & ECHO) {
				AuTTYWriteMaster(tty, '^');
				AuTTYWriteMaster(tty, ('@' + c) % 128);
				AuTTYWriteMaster(tty, '\n');
			}
			//clear_input_buf
			//if foreground process group or process exist
				//send signal to foreground process, the sig number
			return;
		}
	}

	if (tty->term.c_iflag & ISTRIP)
		c &= 0x7f;

	/* ignore CR*/
	if ((tty->term.c_iflag & IGNCR) && c == '\r')
		return;

	if ((tty->term.c_iflag & INLCR) && c == '\n')
		c = '\r';
	else if ((tty->term.c_iflag & ICRNL) && c == '\r')
		c = '\n';

	/* handle canonical mode */
	if (tty->term.c_lflag & ICANON) {
		if (c == tty->term.c_cc[VERASE]) {
			/* erase the last character */
		}
	}
}


size_t AuTTYMasterRead(AuVFSNode* fs, AuVFSNode* file, uint64_t* buffer, uint32_t len) {
	x64_cli();
	TTY* type = (TTY*)file->device;
	if (!type)
		return 0;

	size_t bytes_to_ret = 0;
	uint8_t* aligned_buf = (uint8_t*)buffer;

	if (CircBufEmpty(type->masterbuf)) {
		return bytes_to_ret;
	}

	for (int i = 0; i < type->master_written; i++) {
		AuCircBufGet(type->masterbuf, &aligned_buf[i]);
		bytes_to_ret++;
	}

	type->master_written = 0;
	return bytes_to_ret;
}

/*
 * AuTTYMasterWrite -- writing to master goes to slave buffer
 */
size_t AuTTYMasterWrite(AuVFSNode* fs, AuVFSNode* file, uint64_t* buffer, uint32_t len) {
	x64_cli();
	uint8_t* aligned_buf = (uint8_t*)buffer;
	TTY* type = (TTY*)file->device;
	if (!type)
		return 0;
	SeTextOut("Writing caharacter len : %d \r\n", len);
	for (int i = 0; i < len; i++) {
		AuCircBufPut(type->slavebuf, aligned_buf[i]);
	}
}

size_t AuTTYSlaveRead(AuVFSNode* fsys, AuVFSNode* file, uint64_t* buffer, uint32_t len) {
	x64_cli();
	uint8_t* aligned_buf = (uint8_t*)buffer;
	TTY* tty = (TTY*)file->device;
	if (!tty)
		return 0;

	for (int i = 0; i < len; i++)
		AuCircBufGet(tty->slavebuf, &aligned_buf[i]);

	return 1;
}

/*
 * AuTTYSlaveWrite --- writing to slave goes to master buffer
 */
size_t AuTTYSlaveWrite(AuVFSNode* fsys, AuVFSNode* file, uint64_t* buffer, uint32_t len) {
	x64_cli();
	char* data = (char*)buffer;
	AuThread* curr_th = AuGetCurrentThread();
	uint8_t* aligned_buf = (uint8_t*)buffer;
	TTY* tty = (TTY*)file->device;
	if (!tty)
		return 0;
	if (len > 512)
		len = 512;

	if (CircBufFull(tty->masterbuf)) {
		AuForceScheduler();
		return 0;
	}

	for (int i = 0; i < len; i++) {
		AuCircBufPut(tty->masterbuf, aligned_buf[i]);
		tty->master_written++;
	}

	/* little bit slow down the slave process,
	 * it's too fast 
	 */
	//AuSleepThread(curr_th, 1000000000000);
	AuForceScheduler();
}

int AuTTYSlaveClose(AuVFSNode* fs, AuVFSNode* file) {
	AuVFSNode* _fs = AuVFSFind("/dev");
	if (!_fs)
		return 0;
}

int AuTTYMasterClose(AuVFSNode* fs, AuVFSNode* file) {

	return 0;
}

int AuTTYIoControl(AuVFSNode* file, int code, void* arg) {
	TTY* tty = (TTY*)file->device;
	if (!tty)
		return 0;

	switch (code) {
	case TIOCSWINSZ: {
						 WinSize *sz = (WinSize*)arg;
						 tty->size.ws_col = sz->ws_col;
						 tty->size.ws_row = sz->ws_row;
						 tty->size.ws_xpixel = sz->ws_xpixel;
						 tty->size.ws_ypixel = sz->ws_ypixel;
						 break;
	}
	case TIOCGWINSZ: {
						 WinSize* sz = (WinSize*)arg;
						 sz->ws_col = tty->size.ws_col;
						 sz->ws_row = tty->size.ws_row;
						 sz->ws_xpixel = tty->size.ws_xpixel;
						 sz->ws_ypixel = tty->size.ws_ypixel;
						 break;
	}

	case TIOSPGRP: {
					   AuThread* currthr = AuGetCurrentThread();
					   if (!currthr)
						   return 0;
					   AuProcess* proc = AuProcessFindThread(currthr);
					   if (!proc)
						   return 0;
					   tty->slave_pid = proc->proc_id;
					   break;
	}
	}

	return 1;
}

/*
 * AuTTYCreateMaster -- create a master tty end
 * and mount it to device directory
 * @param tty -- pointer to tty device
 */
AuVFSNode* AuTTYCreateMaster(TTY* tty) {
	AuVFSNode* fs = AuVFSFind("/dev");

	AuVFSNode* node = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(node, 0, sizeof(AuVFSNode));
	char name[5];
	strcpy(name, "ttym");
	sztoa(master_count, name + 4, 10);
	strcpy(node->filename, name);

	node->size = 512;
	node->flags |= FS_FLAG_TTY;
	node->device = tty;
	node->read = AuTTYMasterRead;
	node->write = AuTTYMasterWrite;
	node->close = AuTTYMasterClose;
	node->iocontrol = AuTTYIoControl;
	node->fileCopyCount = 0;

	AuDevFSAddFile(fs, "/dev/tty", node);
	master_count++;
	return node;
}

/*
* AuTTYCreateSlave -- create a slave tty end
* and mount it to device directory
* @param tty -- pointer to tty device
*/
AuVFSNode* AuTTYCreateSlave(TTY* tty) {
	AuVFSNode* fs = AuVFSFind("/dev");

	AuVFSNode* node = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(node, 0, sizeof(AuVFSNode));
	char name[5];
	strcpy(name, "ttys");
	sztoa(slave_count, name + 4, 10);
	strcpy(node->filename, name);

	node->size = 512;
	node->flags |= FS_FLAG_TTY;
	node->device = tty;
	node->read = AuTTYSlaveRead;
	node->write = AuTTYSlaveWrite;
	node->close = AuTTYSlaveClose;
	node->iocontrol = AuTTYIoControl;
	node->fileCopyCount = 0;

	AuDevFSAddFile(fs, "/dev/tty", node);
	slave_count++;
	return node;
}

/*
 * AuTTYCreate -- create tty syscall for process
 * @param master_fd -- Pointer to memory area
 * where to store master file descriptor
 * @param slave_fd -- Pointer to memory area
 * where to store slave file descriptor
 *
 */
int AuTTYCreate(int* master_fd, int* slave_fd) {
	x64_cli();

	AuThread* thr = AuGetCurrentThread();
	AuProcess* proc = AuProcessFindThread(thr);
	if (!proc)
		return -1;

	TTY* tty = (TTY*)kmalloc(sizeof(TTY));
	memset(tty, 0, sizeof(TTY));

	void* inbuffer = kmalloc(512);
	memset(inbuffer, 0, 512);
	void* outbuffer = kmalloc(512);
	memset(outbuffer, 0, 512);

	tty->masterbuf = AuCircBufInitialise((uint8_t*)inbuffer, 512);
	tty->slavebuf = AuCircBufInitialise((uint8_t*)outbuffer, 512);

	tty->id = slave_count;
	tty->master_written = 0;
	tty->slave_written = 0;

	tty->masterbuf_ptr = inbuffer;
	tty->slavebuf_ptr = outbuffer;

	tty->term.c_iflag = ICRNL | BRKINT;
	tty->term.c_oflag = ONLCR | OPOST;
	tty->term.c_iflag = ECHO | ECHOE | ECHOK | ICANON | ISIG | IEXTEN;
	tty->term.c_cflag = CREAD | CS8;
	tty->term.c_cc[VEOF] = 4;
	tty->term.c_cc[VEOL] = 0;
	tty->term.c_cc[VERASE] = 0x7f;

	AuVFSNode* master = AuTTYCreateMaster(tty);
	AuVFSNode* slave = AuTTYCreateSlave(tty);

	int fd = AuProcessGetFileDesc(proc);
	if (fd == -1)
		return 0;
	proc->fds[fd] = master;
	*master_fd = fd;

	fd = AuProcessGetFileDesc(proc);
	if (fd == -1)
		return 0;
	proc->fds[fd] = slave;
	*slave_fd = fd;

	return 1;
}

/*
 * AuTTYInitialise -- initialize the TTY kernel resource
 */
void AuTTYInitialise() {
	root = NULL;
	last = NULL;
	master_count = 0;
	slave_count = 0;
	/* create a directory for tty's */
	AuVFSNode* fs = AuVFSFind("/dev");
	AuDevFSCreateFile(fs, "/dev/tty", FS_FLAG_DIRECTORY);
}

