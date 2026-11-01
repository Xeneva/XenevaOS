/**
* @file tty.c
* 
* BSD 2-Clause License
*
* Copyright (c) 2022-2025, Manas Kamal Choudhury
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
#include <Fs/Dev/devfs.h>
#include <_null.h>
#include <Mm/kmalloc.h>
#include <string.h>
#include <stdio.h>
#include <process.h>
#include <Hal/AA64/sched.h>
#include <aucon.h>
#include <Serv/sysserv.h>
#include <Drivers/uart.h>

/** @brief master_count -- internal count value of total
 * master tty
 */
static size_t master_count = 0;
/** @brief slave_count -- internal slave tty count */
static size_t slave_count = 0;

TTY* root = NULL;
TTY* last = NULL;

/**
 * @brief AuTTYInsert -- insert a new tty to tty
 * list
 * @param tty -- Pointer to newly created tty
 */
void AuTTYInsert(TTY* tty) {
	tty->next = NULL;
	tty->prev = NULL;

	if (root == NULL) {
		last = tty;
		root = tty;
	} else {
		last->next = tty;
		tty->prev = last;
	}
	last = tty;
}

/**
 * @brief AuTTYDelete -- remove a tty from the tty list
 * @param tty -- Pointer to the tty to delete
 */
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

/**
 * @brief AuTTYWriteSlave -- writes a character to
 * to slave tty
 * @param tty -- Pointer to the tty where to write
 * @param c -- character to write
 */
void AuTTYWriteSlave(TTY* tty, uint8_t c) {
	AuCircBufPut(tty->slavebuf, c);
}

/**
 * @brief AuTTYWriteMaster -- writes a character to master
 * tty
 * @param tty -- Pointer to the tty where to write
 * @param c -- character to write
 */
void AuTTYWriteMaster(TTY* tty, uint8_t c) {
	AuCircBufPut(tty->masterbuf, c);
}

/** 
 * @brief AuTTYProcessLine -- Line Discipline layer
 * @param tty -- Pointer to current tty
 * @param c -- character to process
 */
void AuTTYProcessLine(TTY* tty, uint8_t c) {
	if (tty->term.c_lflag & ISIG) {
		int sig = -1;
		/* Xeneva will have its own event based system
		 * so need to emulate signals values to Xeneva
		 * event system
		 */
		if (c == tty->term.c_cc[VINTR])
			sig = -1; // SIGINT;
		else if (c == tty->term.c_cc[VQUIT])
			sig = -1; // SIGQUIT;
		else if (c == tty->term.c_cc[VSUSP])
			sig = -1; // SIGTSTP;

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

/**
 * @brief AuTTYMasterRead -- vfs read callback for master tty
 * @param fs -- Pointer to current file system
 * @param file -- Pointer to tty file
 * @param buffer -- Pointer to buffer to read onto
 * @param len -- total length in bytes to read
 * @return return the amount of bytes read
 */
size_t AuTTYMasterRead(AuVFSNode* fs, AuVFSNode* file, uint64_t* buffer, uint32_t len) {
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

/**
 * @brief AuTTYMasterWrite -- writing to master goes to slave buffer
 * @param fs -- Pointer to current file system
 * @param file -- Pointer to tty file
 * @param buffer -- Pointer to buffer to write to
 * @param len -- to length in bytes to write
 * @return return amount of data written in bytes
 */
size_t AuTTYMasterWrite(AuVFSNode* fs, AuVFSNode* file, uint64_t* buffer, uint32_t len) {
	uint8_t* aligned_buf = (uint8_t*)buffer;
	TTY* type = (TTY*)file->device;
	if (!type)
		return 0;
	for (int i = 0; i < len; i++) {
		AuCircBufPut(type->slavebuf, aligned_buf[i]);
	}
	return len;
}

size_t AuTTYSlaveRead(AuVFSNode* fsys, AuVFSNode* file, uint64_t* buffer, uint32_t len) {
	uint8_t* aligned_buf = (uint8_t*)buffer;
	TTY* tty = (TTY*)file->device;
	if (!tty)
		return 0;
	for (int i = 0; i < len; i++) {
		AuCircBufGet(tty->slavebuf, &aligned_buf[i]);
	}

	return 1;
}

/**
 * @brief AuTTYSlaveWrite --- writing to slave goes to master buffer
 * @param fsys -- self explanatory
 * @param file -- self explanatory
 * @param buffer -- self explanatory
 * @param len -- self explanatory
 * @return return the amount of data written in bytes
 */
size_t AuTTYSlaveWrite(AuVFSNode* fsys, AuVFSNode* file, uint64_t* buffer, uint32_t len) {
	//char* data = (char*)buffer;
	AA64Thread* curr_th = AuGetCurrentThread();
	uint8_t* aligned_buf = (uint8_t*)buffer;
	TTY* tty = (TTY*)file->device;
	if (!tty)
		return 0;
	if (len > 1024)
		len = 1024;

	if (CircBufFull(tty->masterbuf)) {
		/*AA64Registers* regs = AA64GetCurrentRegCtx();
		AuScheduleThread(regs);*/
		AuSleepThread(curr_th, 10);
		AuScheduleNext();
		return 0;
	}

	for (int i = 0; i < len; i++) {
		AuCircBufPut(tty->masterbuf, aligned_buf[i]);
		tty->master_written++;
	}

	/* little bit slow down the slave process,
	 * it's too fast
	 */
	//AuSleepThread(curr_th,10);
	//AuScheduleThread(AA64GetCurrentRegCtx());
	//AuScheduleNext();
	return len;
}

int AuTTYSlaveClose(AuVFSNode* fs, AuVFSNode* file) {
	AuVFSNode* _fs = AuVFSFind("/dev");
	if (!_fs)
		return 1;
	return 0;
}

int AuTTYMasterClose(AuVFSNode* fs, AuVFSNode* file) {
	return 0;
}

/**
 * @breif AuTTYIoControl -- vfs io control callback for tty
 * @param file -- Pointer to tty file
 * @param code -- control code
 * @param arg -- Pointer to user data
 * @return requested value on success, -1 on failure
 */
int AuTTYIoControl(AuVFSNode* file, int code, void* arg) {
	UARTDebugOut("TTYIoControl : %x \r\n", file);
	TTY* tty = (TTY*)file->device;
	if (!tty)
		return 0;

	switch (code) {
	case TIOCSWINSZ: {
		WinSize* sz = (WinSize*)arg;
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
		AA64Thread* currthr = AuGetCurrentThread();
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

/**
 * @brief AuTTYCreateMaster -- create a master tty end
 * and mount it to device directory
 * @param tty -- pointer to tty device
 * @return pointer to newly created tty file
 */
AuVFSNode* AuTTYCreateMaster(TTY* tty) {
	AuVFSNode* fs = AuVFSFind("/dev");

	AuVFSNode* node = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(node, 0, sizeof(AuVFSNode));
	char name[32]; // = (char*)kmalloc(10);
	strcpy(name, "ttym");
	sztoa(master_count, name + 4, 10);
	strcpy(node->filename, name);

	//kfree(name);

	node->size = 1024;
	node->flags |= FS_FLAG_TTY;
	node->device = tty;
	node->uid = 0;
	node->gid = 0;
	node->read = AuTTYMasterRead;
	node->write = AuTTYMasterWrite;
	node->close = AuTTYMasterClose;
	node->iocontrol = AuTTYIoControl;
	node->fileCopyCount = 0;

	AuDevFSAddFile(fs, "/dev/tty", node);
	master_count++;
	return node;
}

/**
* @brief AuTTYCreateSlave -- create a slave tty end
* and mount it to device directory
* @param tty -- pointer to tty device
* @return pointer to newly created slave tty file
*/
AuVFSNode* AuTTYCreateSlave(TTY* tty) {
	AuVFSNode* fs = AuVFSFind("/dev");

	AuVFSNode* node = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(node, 0, sizeof(AuVFSNode));
	char name[32];
	strcpy(name, "ttys");
	sztoa(slave_count, name + 4, 10);
	strcpy(node->filename, name);

	//kfree(name);

	node->size = 1024;
	node->flags |= FS_FLAG_TTY;
	node->device = tty;
	node->read = AuTTYSlaveRead;
	node->write = AuTTYSlaveWrite;
	node->close = AuTTYSlaveClose;
	node->iocontrol = AuTTYIoControl;
	node->uid = 0;
	node->gid = 0;
	node->fileCopyCount = 0;

	AuDevFSAddFile(fs, "/dev/tty", node);
	slave_count++;
	return node;
}

/**
 * @brief AuTTYCreate -- create tty syscall for process
 * @param master_fd -- Pointer to memory area
 * where to store master file descriptor
 * @param slave_fd -- Pointer to memory area
 * where to store slave file descriptor
 * @return 1 on success and -1 on failure
 */
int AuTTYCreate(int* master_fd, int* slave_fd) {
	AA64Thread* thr = AuGetCurrentThread();
	AuProcess* proc = AuProcessFindThread(thr);
	if (!proc)
		return -1;

	TTY* tty = (TTY*)kmalloc(sizeof(TTY));
	memset(tty, 0, sizeof(TTY));
	void* inbuffer = kmalloc(1024);
	memset(inbuffer, 0, 1024);
	void* outbuffer = kmalloc(1024);
	memset(outbuffer, 0, 1024);

	tty->masterbuf = AuCircBufInitialise((uint8_t*)inbuffer, 1024);
	tty->slavebuf = AuCircBufInitialise((uint8_t*)outbuffer, 1024);

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

	/** now current process owns the both file **/
	master->uid = proc->creds.uid;
	master->gid = proc->creds.gid;
	master->mode = 0666;
	slave->uid = proc->creds.uid;
	slave->gid = proc->creds.gid;
	slave->mode = 0620;

	CapRights rights;
	rights = CAP_READ | CAP_WRITE;

	int fd = AuProcessGetFileDesc(proc);
	if (fd == -1)
		return 0;
	proc->fds[fd] = master;
	*master_fd = fd;
	BordoisilaCapCreate(proc, fd, master, CAP_OBJ_FILE, rights);

	fd = AuProcessGetFileDesc(proc);
	if (fd == -1)
		return 0;
	proc->fds[fd] = slave;
	*slave_fd = fd;
	BordoisilaCapCreate(proc, fd, slave, CAP_OBJ_FILE, rights);
	return 1;
}

/**
 * @brief AuTTYInitialise -- initialize the TTY kernel resource
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
