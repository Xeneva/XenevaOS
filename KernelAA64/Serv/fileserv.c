/**
* @file fileserv.c
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

#include <Serv/sysserv.h>
#include <Cap/capability.h>
#include <Fs/vfs.h>
#include <Fs/Fat/Fat.h>
#include <Hal/AA64/sched.h>
#include <process.h>
#include <Drivers/uart.h>
#include <_null.h>
#include <Mm/kmalloc.h>
#include <string.h>
#include <Mm/pmmngr.h>
#include <Mm/vmmngr.h>
#include <Cred/cred.h>
#include <Cred/group.h>
#include <aucon.h>
#include <Log/klog.h>
#include <Fs/srvfs.h>
#include <Fs/Dev/devfs.h>
#include <bordoisila_ptr.h>
#include <fixup.h>
#include <Fs/vfsmod.h>

extern uint64_t read_sp();
extern uint64_t read_sp_el1();
/**
 * @brief OpenFile -- opens a file for user process
 * @param file -- file path
 * @param mode -- mode of the file
 */
int OpenFile(char* filename, int mode) {
	AA64Thread* current_thr = AuGetCurrentThread();
	if (!current_thr)
		return -1;
	AuProcess* current_proc = AuProcessFindThread(current_thr);
	if (!current_proc) {
		current_proc = AuProcessFindSubThread(current_thr);
		if (!current_proc)
			return -1;
	}
	char fname[128];
	memset(fname, 0, 128);
	fname[127] = '\0';
	/* filename is a raw user pointer with no length guarantee, strcpy into
	 * this fixed 128 byte kernel stack buffer with no bound check is a
	 * straight up stack smash for anything >= 128 bytes, and any
	 * unprivileged process can hit it (open() is syscall #12, anyone can
	 * call it). found this chasing an unrelated crash in nmdapha, its a
	 * real bug on its own either way but i couldnt confirm it actually
	 * caused that crash (register dump at fault time didnt line up with
	 * this call site), so dont treat this as "the fix" for that. rejecting
	 * rather than overflowing regardless --axiss */
	if (!filename || strlen(filename) >= sizeof(fname))
		return -1;
	strcpy(fname, filename);
	AuVFSNode* fsys = AuVFSFind(fname);
	int fd = AuProcessGetFileDesc(current_proc);
	AuSrvfsNoteOpenMode(mode);
	AuVFSNode* file = AuVFSOpen(fname);

	uint8_t acc = 0;
	if (mode & FILE_OPEN_READ_ONLY)
		acc |= VFS_ACCESS_READ;
	if (mode & FILE_OPEN_WRITE)
		acc |= VFS_ACCESS_WRITE;
	if (!acc)
		acc = VFS_ACCESS_READ;

	int want = 0;
	if (acc & VFS_ACCESS_READ)
		want |= 4;
	if (acc & VFS_ACCESS_WRITE)
		want |= 2;

	/** check permissions before procedding **/
	/* A write or create mode used to skip the permission check altogether, so any
	 * process could open any existing node for writing. Device nodes now always
	 * get the check, whatever the mode: they are interfaces to hardware and to
	 * kernel services. Ordinary files keep the old rule, because creating or
	 * rewriting files on the boot volume depends on the FAT layer's ownership
	 * defaults, which are not a security boundary. A device node that ordinary
	 * applications must open is owned by the world group (see Fs/Dev/*). */
	bool privileged_node = file && (file->flags & FS_FLAG_DEVICE);
	if (AuCredCheckPermissions(file, &current_proc->creds) &&
		(privileged_node || !((mode & FILE_OPEN_CREAT) || (mode & FILE_OPEN_WRITE)))) {
		if (!file) {
			UARTDebugOut("[fserv]: open '%s' denied: no node\r\n", fname);
			return -1;
		}
		UARTDebugOut("[fserv]: open '%s' denied: creds uid=%d gid=%d nsgid=%d vs node uid=%d "
					 "gid=%d flags=%x\r\n",
					 fname,
					 current_proc->creds.uid,
					 current_proc->creds.gid,
					 current_proc->creds.num_sgid,
					 file->uid,
					 file->gid,
					 file->flags);
		AuTextOut("[aurora]: file : %s is not accessible to this user with uid : %d \r\n",
				  file->filename,
				  current_proc->creds.uid);
		return -1;
	} else {
		bool created = false;
		if (!file) {
			if (mode & FILE_OPEN_CREAT || mode & FILE_OPEN_WRITE) {
				file = AuVFSCreateFile(fsys, filename);
				created = true;
			} else {
				UARTDebugOut("[fserv]: open '%s' failed: no node, mode=%x\r\n", fname, mode);
				return -1;
			}
		}
	}
	/* check for last time, if any error occured */
	if (!file) {
		UARTDebugOut("[fserv]: open '%s' failed: create returned NULL\r\n", fname);
		return -1;
	}

	if (fd == -1) {
		UARTDebugOut("[fserv]: open '%s' failed: no free fd\r\n", fname);
		return -1;
	}
	if ((file->flags & FS_FLAG_DEVICE) && !AuDevFSNodeOpen(file)) {
		UARTDebugOut("[fserv]: open '%s' failed: device is unloading\r\n", fname);
		return -1;
	}

	/** need to properly check, buggy */
	if (!AuVFSCheckAccess(&current_proc->creds, file, want)) {
		UARTDebugOut("[vfs]: opened failed bad file access modes \r\n");
		//AuVFSNodeClose(fsys, file);
		//return -1;
	}

	/* just to increase the reference count */
	if (file->open)
		file->open(file, NULL);
	current_proc->fds[fd] = file;
	CapRights rights = CAP_SEEK;

	if (mode & FILE_OPEN_READ_ONLY)
		rights |= CAP_READ | CAP_WRITE;

	if (mode & (FILE_OPEN_WRITE | FILE_OPEN_CREAT))
		rights |= CAP_WRITE;

	/* Preserve current default behaviour */
	if (mode == 0)
		rights |= CAP_READ;

	BordoisilaCapCreate(current_proc, fd, file, CAP_OBJ_FILE, rights);

	current_proc->access[fd] = acc;
	//_setdebug = 1;
	return fd;
}

/**
 * @brief FileSetOffset -- set a offset inorder to read the
 * specific position of the file
 * @param fd -- File descriptor
 * @param offset -- offset in bytes
 */
int FileSetOffset(int fd, size_t offset) {
	if (fd >= FILE_DESC_PER_PROCESS)
		return -1;

	if (fd < 0)
		return -1;

	AA64Thread* current_thr = AuGetCurrentThread();
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
	if (!((file->flags & FS_FLAG_FILE_SYSTEM) || (file->flags & FS_FLAG_DEVICE) ||
		  (file->flags & FS_FLAG_PIPE) || (file->flags & FS_FLAG_DIRECTORY) ||
		  (file->flags & FS_FLAG_TTY))) {
		AuVFSNode* fsys = AuVFSFind("/");
		if (!fsys)
			return -1;
		size_t block = AuVFSGetBlockFor(fsys, file, offset);
		file->current = block;
	} else
		file->pos = offset;

	return 0;
}

/**
 * @brief ReadFile -- reads a file into given buffer
 * @param fd -- file descriptor
 * @param buffer -- buffer where to put the data
 * @param length -- length in bytes
 */
size_t ReadFile(int fd, void* buffer, size_t length) {
	/** check for user mode buffer validity  */
	if (!__BORDOISILA_UBUF_VALIDATE(buffer, length)) {
		UARTDebugOut("[aurora]: Read file denied fd : %d, invalid buffer : %x \r\n", fd, buffer);
		return 0;
	}
	/** also need to check access rights  */

	if (fd < 0)
		return 0;
	if (!buffer)
		return 0;
	if (!length)
		return 0;
	if (fd >= FILE_DESC_PER_PROCESS)
		return -1;
	AA64Thread* current_thr = AuGetCurrentThread();
	if (!current_thr)
		return 0;
	AuProcess* current_proc = AuProcessFindThread(current_thr);
	if (!current_proc) {
		current_proc = AuProcessFindSubThread(current_thr);
		if (!current_proc)
			return 0;
	}

	AuVFSNode* file = current_proc->fds[fd];
	uint64_t* aligned_buffer = (uint64_t*)buffer;

	//SeTextOut("Reading from file -> %d -> %x \r\n", fd, file);
	if (!file) {
		return 0;
	}
	if (!BordoisilaCapCheckRights(current_proc, fd, CAP_READ)) {
		return 0;
	}
	size_t ret_bytes = 0;

	/* every general file will contain its
	 * file system node as device */
	AuVFSNode* fsys = (AuVFSNode*)file->device;
	if (file->flags & FS_FLAG_GENERAL && !(file->flags & FS_FLAG_TTY)) {
		ret_bytes = AuVFSNodeRead(fsys, file, aligned_buffer, length);
	}
	if (file->flags & FS_FLAG_DEVICE) {
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

/**
 * @brief WriteFile -- write system call
 * @param fd -- file descriptor
 * @param buffer -- buffer to write
 * @param length -- length in bytes
 */
size_t WriteFile(int fd, void* buffer, size_t length) {
	if (!__BORDOISILA_UBUF_VALIDATE(buffer, length)) {
		BPrintK(
			BORDOISILA_ERROR, "WriteFile denied, invalid buffer : %x , fd : %d \r\n", buffer, fd);
		return 0;
	}
	if (fd < 0)
		return 0;
	if (!buffer)
		return 0;
	if (!length)
		return 0;
	if (fd >= FILE_DESC_PER_PROCESS)
		return 0;
	AA64Thread* current_thr = AuGetCurrentThread();
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

	if (!BordoisilaCapCheckRights(current_proc, fd, CAP_WRITE)) {
		return 0;
	}
	size_t write_bytes = 0;
	size_t ret_bytes;
	/* every general file will contain its
	* file system node as device */
	AuVFSNode* fsys = (AuVFSNode*)file->device;

	if (AuSrvfsOwns(fsys)) {
		/* /srv is served by a user-space process: its write may have to wait for
		 * the server and restart this syscall, which would leak a staging page
		 * every time, so hand it the caller's own buffer one page at a time */
		size_t done = 0;
		while (done < length) {
			size_t chunk = length - done > PAGE_SIZE ? PAGE_SIZE : length - done;
			size_t w = fsys->write
						   ? fsys->write(fsys, file, (uint64_t*)(aligned_buffer + done), chunk)
						   : 0;
			if (!w)
				break;
			done += w;
		}
		return done;
	}

	if (file->flags & FS_FLAG_GENERAL && !(file->flags & FS_FLAG_TTY)) {
		/* the staging buffer is one physical page and FatWrite only ever
		 * sees that single page, but this used to hand it the *full*
		 * length in one shot. for length > PAGE_SIZE that walked the
		 * cluster loop right off the end of the staged page. it also
		 * always memcpy'd a full PAGE_SIZE from the caller's buffer no
		 * matter what length was, over-reading past shorter buffers.
		 * fixed by feeding FatWrite one page at a time so each call
		 * stays inside the page it actually has. FatFileWriteContent
		 * tracks the write cursor on the node itself and
		 * FatFileUpdateSize adds its size arg to the on-disk entry
		 * instead of overwriting it, so the per-chunk calls here add up
		 * correctly across the loop --axiss */
		size_t remaining = length;
		uint8_t* src = aligned_buffer;
		while (remaining > 0) {
			size_t chunk = remaining > PAGE_SIZE ? PAGE_SIZE : remaining;
			uint64_t* buff = (uint64_t*)P2V((size_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
			memset(buff, 0, PAGE_SIZE);
			//memcpy(buff, src, chunk);
			if (__bordoisila_copy_from_user(buff, aligned_buffer, chunk) != 0) {
				AuPmmngrReleasePage((uint64_t)V2P((size_t)buff));
				return remaining;
			}
			AuVFSNodeWrite(fsys, file, buff, chunk);
			AuPmmngrReleasePage((uint64_t)V2P((size_t)buff));
			src += chunk;
			remaining -= chunk;
		}
		return length;
	}

	if (file->flags & FS_FLAG_TTY) {
		uint64_t* ubuff = buffer;
		uint8_t safe_ptr[1024];
		memset(&safe_ptr, 0, 1024);

		if (__bordoisila_copy_from_user(safe_ptr, ubuff, length) != 0)
			return ret_bytes;

		if (file->write)
			ret_bytes = file->write(file, file, (uint64_t*)safe_ptr, length);

		return ret_bytes;
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

	return 0;
}
/**
 * @brief CloseFile -- closes a general file
 * @param fd -- file descriptor to close
 */
int CloseFile(int fd) {
	if (fd < 0)
		return 0;
	if (fd >= FILE_DESC_PER_PROCESS)
		return 0;
	AA64Thread* current_thr = AuGetCurrentThread();
	if (!current_thr)
		return 0;
	AuProcess* current_proc = AuProcessFindThread(current_thr);
	if (!current_proc) {
		current_proc = AuProcessFindSubThread(current_thr);
		if (!current_proc)
			return 0;
	}

	AuVFSNode* file = current_proc->fds[fd];
	/* closing an fd that was never opened used to just crash here, no NULL check --axiss */
	if (!file)
		return -1;
	if (file->flags & FS_FLAG_FILE_SYSTEM) {
		current_proc->fds[fd] = 0;
		BordoisilaCapDestroy(current_proc, fd);
		return -1;
	}

	if (file->flags & FS_FLAG_CACHED) {
		current_proc->fds[fd] = 0;
		BordoisilaCapDestroy(current_proc, fd);
		return 0;
	}
	/* Device nodes are permanent and shared between every open, so the
	 * node is never freed here -- but the close hook must still run: DCL
	 * routes it to the module's release, and a DRM file's master lives
	 * there. Without this, closing /dev/dri/cardN left the master set and
	 * the next opener had every DRM_MASTER ioctl refused with -EACCES.
	 * See AuProcessCloseFiles for the process-exit half. --axiss */
	if (file->flags & FS_FLAG_DEVICE) {
		current_proc->fds[fd] = 0;
		BordoisilaCapDestroy(current_proc, fd);
		if (file->close)
			file->close(file, file);
		return 0;
	}
	if (file->flags & FS_FLAG_GENERAL) {
		current_proc->fds[fd] = 0;
		BordoisilaCapDestroy(current_proc, fd);
		/* this used to kfree() unconditionally (there was a TODO here
		 * that just said "NEED to fix, freeing the file causes crash").
		 * a dup'd fd, a fork-inherited fd, or a second open() of the
		 * same path hitting the AuVFSOpen cache all bump fileCopyCount
		 * and share the same AuVFSNode*, so freeing on the first
		 * close() left every other reference dangling. AuProcessExit
		 * already uses this same "<=0 means free" check for these
		 * flags so at least this is consistent with that. heads up
		 * though, FAT leaves a fresh node's fileCopyCount at 0 (memset)
		 * while Ext2.c:406 sets it to 1, so the two filesystems dont
		 * agree on what the field even means at open time. on an Ext2
		 * node with no dups this never frees, same one-node-per-close
		 * leak AuProcessExit already has. not fixing that here, out of
		 * scope for this pass and theres no Ext2 image on this board
		 * anyway --axiss */
		if (file->fileCopyCount <= 0)
			kfree(file);
		else
			file->fileCopyCount -= 1;
		return 0;
	}

	if (file->flags & FS_FLAG_DIRECTORY) {
		current_proc->fds[fd] = 0;
		BordoisilaCapDestroy(current_proc, fd);
		if (file->fileCopyCount <= 0)
			kfree(file);
		else
			file->fileCopyCount -= 1;
		return 0;
	}

	if (file->flags & FS_FLAG_DEVICE) {
		current_proc->fds[fd] = 0;
		BordoisilaCapDestroy(current_proc, fd);
		AuDevFSNodeClose(file);
		return 0;
	}

	if (file->flags & FS_FLAG_PIPE) {
		if (file->close)
			file->close(file, file);
		current_proc->fds[fd] = 0;
		BordoisilaCapDestroy(current_proc, fd);
		return 0;
	}

	if (file->flags & FS_FLAG_SOCKET) {
		/* close() on a socket fd used to fall through to the -1 below, so a socket
		 * lived until its process exited. That matters for ICMP, where the whole
		 * system has one socket slot: a process that closed its socket never gave
		 * the slot back. The close hooks free the node themselves (process exit
		 * relies on the same), so I clear the fd first and never touch file again --axiss */
		current_proc->fds[fd] = 0;
		BordoisilaCapDestroy(current_proc, fd);
		if (file->close)
			file->close(file, file);
		return 0;
	}

	/* flags matched none of the known types above, this used to just
	 * fall off the end of the function with no return --axiss */
	return -1;
}

/**
 * @brief FileIoControl -- controls the file through I/O code
 * @param fd -- file descriptor
 * @param code -- code to pass
 * @param arg -- argument to pass
 */
int FileIoControl(int fd, int code, void* arg) {
	if (fd < 0)
		return -1;
	if (fd >= FILE_DESC_PER_PROCESS)
		return 0;
	AA64Thread* current_thr = AuGetCurrentThread();
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

/**
 * @brief FileStat -- writes information related
 * to file
 * @param fd -- file descriptor
 * @param buf -- Pointer to file structure
 */
int FileStat(int fd, void* buf) {
	if (fd < 0)
		return -1;
	if (fd >= FILE_DESC_PER_PROCESS)
		return -1;
	AA64Thread* current_thr = AuGetCurrentThread();
	if (!current_thr) {
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

	AuFileStatus* status = (AuFileStatus*)buf;
	status->current_block = file->current;
	status->size = file->size;
	status->filemode = file->flags;
	status->eof = file->eof;
	status->start_block = file->first_block;
	status->user_id = 0;
	status->group_id = 0;
	return 0;
}

/**
 * @brief OpenDir -- opens a directory
 * @param filename -- name of the directory
 */
int OpenDir(char* filename) {
	AA64Thread* current_thr = AuGetCurrentThread();
	if (!current_thr) {
		return -1;
	}
	AuProcess* current_proc = AuProcessFindThread(current_thr);
	if (!current_proc) {
		current_proc = AuProcessFindSubThread(current_thr);
		if (!current_proc)
			return -1;
	}

	AuVFSNode* fsys = AuVFSFind(filename);
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
	return fd;
}

/**
 * @brief ReadDir -- reads a directory entry
 * @param dirfd -- directory file descriptor
 * @param dirent -- aurora directory entry struct
 */
int ReadDir(int dirfd, void* dirent) {
	if (!dirent)
		return -1;
	if (dirfd < 0)
		return -1;
	/* only checked == -1 here before, no upper bound at all, so any
	 * dirfd >= FILE_DESC_PER_PROCESS indexed straight past fds[] --axiss */
	if (dirfd >= FILE_DESC_PER_PROCESS)
		return -1;

	AA64Thread* current_thr = AuGetCurrentThread();
	if (!current_thr) {
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
	return 1;
}

/**
 * @brief ProcessGetFileDesc -- Searches all process file
 * descriptor entries for
 * specific filename fd
 */
int ProcessGetFileDesc(const char* filename) {
	AA64Thread* thr = AuGetCurrentThread();
	if (!thr)
		return -1;
	AuProcess* currproc = AuProcessFindThread(thr);
	if (!currproc) {
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

/**
 * @brief RemoveFile -- remove a directory or file
 * Supports both /srv server filesystem and general VFS (e.g. FAT32).
 * @param pathname -- path of directory or file to remove
 */
int RemoveFile(char* pathname) {
	if (!pathname || strlen(pathname) >= 128)
		return -1;

	char fname[128];
	memset(fname, 0, sizeof(fname));
	strcpy(fname, pathname);

	AuVFSNode* fsys_check = AuVFSFind(fname);
	if (AuSrvfsOwns(fsys_check))
		return AuSrvfsRemove(fname);

	AuVFSNode* target = AuVFSOpen(fname);
	if (!target)
		return -1;

	AuVFSNode* fsys = (AuVFSNode*)target->device;
	if (!fsys) {
		kfree(target);
		return -1;
	}

	int ret = -1;
	if (target->flags & FS_FLAG_DIRECTORY)
		ret = AuVFSRemoveDir(fsys, target);
	else
		ret = AuVFSRemoveFile(fsys, target);

	kfree(target);
	return ret;
}

/**
 * @brief CreateDir -- create a directory
 * @param filename -- directory pathname to create
 */
int CreateDir(char* filename) {
	if (!filename)
		return -1;

	char fname[128];
	memset(fname, 0, sizeof(fname));
	if (strlen(filename) >= sizeof(fname))
		return -1;
	strcpy(fname, filename);

	/* If directory or node already exists, return success */
	AuVFSNode* existing = AuVFSOpen(fname);
	if (existing) {
		kfree(existing);
		return 0;
	}

	AuVFSNode* fsys = AuVFSFind(fname);
	if (!fsys)
		return -1;

	AuVFSNode* dirfile = AuVFSCreateDir(fsys, fname);
	if (dirfile) {
		kfree(dirfile);
		return 0;
	}

	return -1;
}
