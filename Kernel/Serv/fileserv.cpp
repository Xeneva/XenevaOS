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

#include <Fs/vfs.h>
#include <Fs/Fat/Fat.h>
#include <Mm/pmmngr.h>
#include <Mm/vmmngr.h>
#include <Mm/kmalloc.h>
#include <process.h>
#include <Hal/x86_64_sched.h>
#include <Hal/serial.h>
#include <aucon.h>
#include <_null.h>
#include <Hal/x86_64_hal.h>
#include <Fs/pipe.h>
#include <Cap/capability.h>

/*
 * OpenFile -- opens a file for user process
 * @param file -- file path
 * @param mode -- mode of the file
 */
int OpenFile(char* filename, int mode) { SeTextOut("<<<<<<<<<< OPENFILE HIT >>>>>>>>>>\r\n");
	x64_cli();
	AuThread* current_thr = AuGetCurrentThread();
	if (!current_thr)
		return -1;
	AuProcess* current_proc = AuProcessFindThread(current_thr);
	if (!current_proc) {
		current_proc = AuProcessFindSubThread(current_thr);
		if (!current_proc)
			return -1;
	}
	int fd = AuProcessGetFileDesc(current_proc);
	AuVFSNode *fsys = AuVFSFind(filename);
	AuVFSNode* file = AuVFSOpen(filename);
	bool created = false;
	if (!file) {
		if (mode & FILE_OPEN_CREAT || mode & FILE_OPEN_WRITE) {
			SeTextOut("Creating file %s \r\n", filename);
			file = AuVFSCreateFile(fsys, filename);
			created = true;
		}
		else 
			return -1;
	}

	/* check for last time, if any error occured */
	if (!file)
		return -1;

	
	if (fd == -1)
		return -1;

	/* just to increase the reference count */
	if (file->flags & FS_FLAG_PIPE)
		SeTextOut("Opening file -> %s \r\n", file->filename);
		if (file->open)
			file->open(file, NULL);

	current_proc->fds[fd] = file;

	/*
 	* Bind a capability to the newly allocated fd.
	 */
	CapRights rights = CAP_SEEK;
	
	if (mode & FILE_OPEN_READ_ONLY)
    	rights |= CAP_READ;

	if (mode & (FILE_OPEN_WRITE | FILE_OPEN_CREAT))
    	rights |= CAP_WRITE;

	/* Preserve existing default behaviour. */
	if (mode == 0)
   	 rights |= CAP_READ;

	BordoisilaCapCreate(
    	current_proc,
    	fd,
    	file,
    	CAP_OBJ_FILE,
    	rights);
	
	SeTextOut("[CAP] Capability created fd=%d rights=%x\r\n", fd, rights);

	return fd;
	}

/*
 * FileSetOffset -- set a offset inorder to read the
 * specific position of the file
 * @param fd -- File descriptor
 * @param offset -- offset in bytes
 */
int FileSetOffset(int fd, size_t offset) {
	x64_cli();
	AuThread* current_thr = AuGetCurrentThread();
	if (!current_thr)
		return -1;
	AuProcess* current_proc = AuProcessFindThread(current_thr);
	if (!current_proc) {
		current_proc = AuProcessFindSubThread(current_thr);
		if (!current_proc)
			return -1;
	}

	AuVFSNode* file = current_proc->fds[fd];
	if (!file)
		return -1;
	if (!((file->flags & FS_FLAG_FILE_SYSTEM) || (file->flags & FS_FLAG_DEVICE) || (file->flags & FS_FLAG_PIPE)
		|| (file->flags & FS_FLAG_DIRECTORY) || (file->flags & FS_FLAG_TTY))){
		AuVFSNode* fsys = AuVFSFind("/");
		if (!fsys)
			return -1;
		size_t block = AuVFSGetBlockFor(fsys, file, offset);
		file->current = block;
	}
	else
		file->pos = offset;

	return 0;
}
/*
 * ReadFile -- reads a file into given buffer
 * @param fd -- file descriptor
 * @param buffer -- buffer where to put the data
 * @param length -- length in bytes
 */
size_t ReadFile(int fd, void* buffer, size_t length) {
	x64_cli();
	if (fd == -1)
		return 0;
	if (!buffer)
		return 0;
	if (!length)
		return 0;
	AuThread* current_thr = AuGetCurrentThread();
	if (!current_thr)
		return 0;
	AuProcess* current_proc = AuProcessFindThread(current_thr);
	if (!current_proc){
		current_proc = AuProcessFindSubThread(current_thr);
		if (!current_proc)
			return 0;
	}
	
	AuVFSNode* file = current_proc->fds[fd];
	uint64_t* aligned_buffer = (uint64_t*)buffer;

	//SeTextOut("Reading from file -> %d -> %x \r\n", fd, file);
	if (!file)
   	 return 0;

	if (!BordoisilaCapCheckRights(current_proc, fd, CAP_READ))
    	return 0;

	size_t ret_bytes = 0;	
	/* every general file will contain its
	 * file system node as device */
	AuVFSNode* fsys = (AuVFSNode*)file->device;
	if (file->flags & FS_FLAG_GENERAL && !(file->flags & FS_FLAG_TTY)) {
		ret_bytes = AuVFSNodeRead(fsys, file,aligned_buffer, length);
	}
	if (file->flags & FS_FLAG_DEVICE){
		/* devfs will handle*/
		if (file->read)
			ret_bytes = file->read(file, file, (uint64_t*)buffer, length);
	}

	if (file->flags & FS_FLAG_TTY) {
		if (file->read)
			ret_bytes = file->read(file, file, (uint64_t*)aligned_buffer, length);
	}
	if (file->flags == FS_FLAG_PIPE) {
		/* ofcourse, pipe subsystem will handle */
		if (file->read)
			ret_bytes = file->read(file, file, (uint64_t*)buffer, length);
	}

	return ret_bytes;
}


/*
 * WriteFile -- write system call
 * @param fd -- file descriptor
 * @param buffer -- buffer to write
 * @param length -- length in bytes
 */
size_t WriteFile(int fd, void* buffer, size_t length) {
	x64_cli();
	if (fd == -1)
		return 0;
	if (!buffer)
		return 0;
	if (!length)
		return 0;
	AuThread* current_thr = AuGetCurrentThread();
	if (!current_thr)
		return 0;
	AuProcess* current_proc = AuProcessFindThread(current_thr);
	if (!current_proc) {
		current_proc = AuProcessFindSubThread(current_thr);
		if (!current_proc) 
			return 0;
	}
	AuVFSNode* file = current_proc->fds[fd];
	uint8_t* aligned_buffer = (uint8_t*)buffer;
	if (!file)
		return 0;

	if (!BordoisilaCapCheckRights(current_proc, fd, CAP_WRITE))
    	return 0;

	/* every general file will contain its
 	* file system node as device */
	AuVFSNode* fsys = (AuVFSNode*)file->device;

	if (file->flags & FS_FLAG_GENERAL && !(file->flags & FS_FLAG_TTY)) {
		uint64_t* buff = (uint64_t*)P2V((size_t)AuPmmngrAlloc());
		memset(buff, 0, PAGE_SIZE);
		memcpy(buff,aligned_buffer, PAGE_SIZE);
		AuVFSNodeWrite(fsys, file, buff, length);
		AuPmmngrFree((void*)V2P((size_t)buff));
	}

	if (file->flags & FS_FLAG_TTY) {
		if (file->write)
			return file->write(file, file, (uint64_t*)buffer, length);
	}

	if (file->flags & FS_FLAG_DEVICE) {
		if (file->write) {
			return file->write(fsys, file, (uint64_t*)buffer, length);
		}
	}

	if (file->flags & FS_FLAG_PIPE) {
		if (file->write)
			return file->write(file, file, (uint64_t*)buffer, length);
	}
}

/*
 * CreateDir -- creates a directory
 * @param filename -- name of the directory
 */
int CreateDir(char* filename) {
	x64_cli();
	AuThread* current_thr = AuGetCurrentThread();
	if (!current_thr){
		return 0;
	}
	AuProcess* current_proc = AuProcessFindThread(current_thr);
	if (!current_proc) {
		current_proc = AuProcessFindSubThread(current_thr);
		if (!current_proc)
			return 0;
	}

	AuVFSNode *fsys = AuVFSFind(filename);
	AuVFSNode* dirfile = NULL;
	if (fsys){
		dirfile = AuVFSCreateDir(fsys, filename);
	}
	else {
		return -1;
	}

	if (dirfile) {
		kfree(dirfile);
		return 0;
	}

	return -1;
}

/*
 * RemoveFile -- remove a directory or file
 * @param dirname -- directory name
 */
int RemoveFile(char* pathname) {
	x64_cli();
	AuVFSNode* dir = AuVFSOpen(pathname);
	if (!dir)
		return -1;
	AuVFSNode* fsys = (AuVFSNode*)dir->device;
	if (fsys->flags & FS_FLAG_DIRECTORY)
		return AuVFSRemoveDir(fsys, dir);
	else
		return AuVFSRemoveFile(fsys, dir);
}

/*
 * CloseFile -- closes a general file
 * @param fd -- file descriptor to close
 */
int CloseFile(int fd) {
	x64_cli();
	if (fd == -1)
		return 0;
	AuThread* current_thr = AuGetCurrentThread();
	if (!current_thr)
		return 0;
	AuProcess* current_proc = AuProcessFindThread(current_thr);
	if (!current_proc) {
		current_proc = AuProcessFindSubThread(current_thr);
		if (!current_proc)
			return 0;
	}

	AuVFSNode* file = current_proc->fds[fd];
	if (file->flags & FS_FLAG_FILE_SYSTEM){
    	SeTextOut("Closing fs -> %s \r\n", file->filename);
    	BordoisilaCapDestroy(current_proc, fd);
    	current_proc->fds[fd] = 0;
    	return -1;
	}
	if (file->flags & FS_FLAG_GENERAL){
		kfree(file);
	}
	

	if (file->flags & FS_FLAG_DIRECTORY){
		kfree(file);
	}

	if (file->flags & FS_FLAG_PIPE) {
		if (file->close)
			file->close(file, file);
	}

	BordoisilaCapDestroy(current_proc, fd);
	current_proc->fds[fd] = 0;
	return 0;
}

/*
 * FileIoControl -- controls the file through I/O code
 * @param fd -- file descriptor
 * @param code -- code to pass
 * @param arg -- argument to pass
 */
int FileIoControl(int fd, int code, void* arg) {
	x64_cli();
	if (fd == -1)
		return -1;
	AuThread* current_thr = AuGetCurrentThread();
	if (!current_thr)
		return 0;
	AuProcess* current_proc = AuProcessFindThread(current_thr);
	if (!current_proc) {
		current_proc = AuProcessFindSubThread(current_thr);
		if (!current_proc)
			return 0;
	}
	AuVFSNode* file = current_proc->fds[fd];

	if (!file)
		return -1;

	int ret = 0;
	ret = AuVFSNodeIOControl(file, code, arg);
	return ret;
}

/*
 * FileStat -- writes information related
 * to file
 * @param fd -- file descriptor
 * @param buf -- Pointer to file structure
 */
int FileStat(int fd, void* buf) {
	x64_cli();
	if (fd == -1)
		return -1;
	AuThread* current_thr = AuGetCurrentThread();
	if (!current_thr){
		return 0;
	}
	AuProcess* current_proc = AuProcessFindThread(current_thr);
	if (!current_proc) {
		current_proc = AuProcessFindSubThread(current_thr);
		if (!current_proc)
			return 0;
	}
	AuVFSNode* file = current_proc->fds[fd];
	if (!file)
		return -1;

	AuFileStatus *status = (AuFileStatus*)buf;
	status->current_block = file->current;
	status->size = file->size;
	status->filemode = file->flags;
	status->eof = file->eof;
	status->start_block = file->first_block;
	status->user_id = 0;
	status->group_id = 0;
	return 0;
}

/*
 * OpenDir -- opens a directory
 * @param filename -- name of the directory
 */
int OpenDir(char* filename) {
	x64_cli();
	AuThread* current_thr = AuGetCurrentThread();
	if (!current_thr){
		return -1;
	}
	AuProcess* current_proc = AuProcessFindThread(current_thr);
	if (!current_proc) {
		current_proc = AuProcessFindSubThread(current_thr);
		if (!current_proc)
			return -1;
	}

	AuVFSNode *fsys = AuVFSFind(filename);
	AuVFSNode* dirfile = NULL;
	if (!fsys)
		return -1;
	if (fsys->opendir)
		dirfile = fsys->opendir(fsys, filename);

	if (!dirfile)
		return -1;

	int fd = AuProcessGetFileDesc(current_proc);
	if (fd == -1)
		return -1;

	current_proc->fds[fd] = dirfile;
	SeTextOut("dir opening -> %s , %x \r\n", dirfile->filename, dirfile);
	return fd;
}

/*
 * ReadDir -- reads a directory entry
 * @param dirfd -- directory file descriptor
 * @param dirent -- aurora directory entry struct
 */
int ReadDir(int dirfd, void* dirent) {
	x64_cli();
	if (!dirent)
		return -1;
	if (dirfd == -1)
		return -1;

	AuThread* current_thr = AuGetCurrentThread();
	if (!current_thr){
		return 0;
	}
	AuProcess* current_proc = AuProcessFindThread(current_thr);
	if (!current_proc) {
		current_proc = AuProcessFindSubThread(current_thr);
		if (!current_proc)
			return 0;
	}

	AuDirectoryEntry* dire_ = (AuDirectoryEntry*)dirent;

	AuVFSNode* dirfile = current_proc->fds[dirfd];
	if (!dirfile)
		return -1;
	AuVFSNode* fsys = (AuVFSNode*)dirfile->device;
	if (!fsys)
		return -1;
	if (fsys->read_dir)
		return fsys->read_dir(fsys, dirfile, dire_);
}

/*
 * ProcessGetFileDesc -- Searches all process file 
 * descriptor entries for
 * specific filename fd
 */
int ProcessGetFileDesc(const char* filename) {
	x64_cli();
	AuThread* thr = AuGetCurrentThread();
	if (!thr)
		return -1;
	AuProcess* currproc = AuProcessFindThread(thr);
	if (!currproc){
		currproc = AuProcessFindSubThread(thr);
		if (!currproc)
			return -1;
	}
	for (int i = 0; i < FILE_DESC_PER_PROCESS; i++) {
		AuVFSNode* file = currproc->fds[i];
		if (file) {
			if (strcmp(filename, file->filename) == 0) {
				return i;
			}
		}
	}

	return -1;
}