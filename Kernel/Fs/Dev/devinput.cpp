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

#include <Fs/dev/devinput.h>
#include <Fs/vfs.h>
#include <Fs/pipe.h>
#include <_null.h>
#include <Fs/dev/devfs.h>
#include <string.h>
#include <Mm/kmalloc.h>
#include <aurora.h>
#include <Serv/sysserv.h>
#include <Drivers/mouse.h>
#include <Hal/serial.h>
#include <Hal/x86_64_hal.h>


AuVFSNode* mice_;
AuVFSNode* kybrd_;

/*
 * AuDevReadMice -- reads packets from pipe
 * to buffer
 * @para, inputmsg -- Pointer to the buffer
 */
void AuDevReadMice(AuInputMessage* inputmsg) {
	if (!mice_)
		return;
	memcpy(inputmsg, mice_->device, sizeof(AuInputMessage));
	memset(mice_->device, 0, sizeof(AuInputMessage));
}

/*
 * AuDevWriteMice -- writes a packet to pipe
 * @param outmsg -- packet to write
 */
void AuDevWriteMice(AuInputMessage* outmsg) {
	if (!mice_)
		return;
	memcpy(mice_->device, outmsg, sizeof(AuInputMessage));
}

/*
* AuDevReadkybrd -- reads packets from pipe
* to buffer
* @para, inputmsg -- Pointer to the buffer
*/
void AuDevReadKybrd(AuInputMessage* inputmsg) {
	if (!kybrd_)
		return;
	memcpy(inputmsg, kybrd_->device, sizeof(AuInputMessage));
	memset(kybrd_->device, 0, sizeof(AuInputMessage));
}

/*
* AuDevWritekybrd -- writes a packet to pipe
* @param outmsg -- packet to write
*/
void AuDevWriteKybrd(AuInputMessage* outmsg) {
	if (!kybrd_)
		return;
	memcpy(kybrd_->device, outmsg, sizeof(AuInputMessage));
}

/*
* AuPipeWrite -- write to pipe
* @param fs -- Pointer to the file system node
* @param file -- Pointer to the file, here we don't need it
* @param buffer -- Pointer to buffer where to put the data
* @param length -- length to read
*/
size_t AuDevInputMiceWrite(AuVFSNode *fs, AuVFSNode *file, uint64_t* buffer, uint32_t length){
#ifdef ARCH_X64
	x64_cli();
#endif
	if (!file)
		return 0;
	if (!buffer)
		return 0;
	void* mice_buf = file->device;
	memcpy(mice_buf, buffer, sizeof(AuInputMessage));
	return (sizeof(AuInputMessage));
}

/*
* AuPipeRead -- reads from pipe
* @param fs -- Pointer to the file system node
* @param file -- Pointer to the file, here we don't need it
* @param buffer -- Pointer to buffer where to put the data
* @param length -- length to read
*/
size_t AuDevInputMiceRead(AuVFSNode *fs, AuVFSNode *file, uint64_t* buffer, uint32_t length){
#ifdef ARCG_X64
	x64_cli();
#endif
	if (!file)
		return 0;
	if (!buffer)
		return 0;
	void* mice_buf = file->device;
	memcpy(buffer, mice_buf, sizeof(AuInputMessage));
	memset(mice_buf, 0, sizeof(AuInputMessage));
	return (sizeof(AuInputMessage));
}

/*
* AuPipeWrite -- write to pipe
* @param fs -- Pointer to the file system node
* @param file -- Pointer to the file, here we don't need it
* @param buffer -- Pointer to buffer where to put the data
* @param length -- length to read
*/
size_t AuDevInputKybrdWrite(AuVFSNode *fs, AuVFSNode *file, uint64_t* buffer, uint32_t length){
#ifdef ARCH_X64
	x64_cli();
#endif
	if (!file)
		return 0;
	if (!buffer)
		return 0;
	void* key_buf = file->device;
	memcpy(key_buf, buffer, sizeof(AuInputMessage));
	return (sizeof(AuInputMessage));
}

/*
* AuPipeRead -- reads from pipe
* @param fs -- Pointer to the file system node
* @param file -- Pointer to the file, here we don't need it
* @param buffer -- Pointer to buffer where to put the data
* @param length -- length to read
*/
size_t AuDevInputKybrdRead(AuVFSNode *fs, AuVFSNode *file, uint64_t* buffer, uint32_t length){
#ifdef ARCH_X64
	x64_cli();
#endif
	if (!file)
		return 0;
	if (!buffer)
		return 0;
	void* key_buf = file->device;
	memcpy(buffer, key_buf, sizeof(AuInputMessage));
	memset(key_buf, 0, sizeof(AuInputMessage));
	return (sizeof(AuInputMessage));
}

/*
 * AuDevMouseIoControl -- controls the mouse device by command
 * @param file -- Pointer to mouse file
 * @param code -- code to pass as command
 * @param arg -- pointer to AuFileIoControl structure
 */
int AuDevMouseIoControl(AuVFSNode* file, int code, void* arg) {
	SeTextOut("Mouse ioCtl \r\n");
	if (!file)
		return 0;
	AuFileIOControl *ioctl = (AuFileIOControl*)arg;
	if (!arg)
		return 0;
	if (ioctl->syscall_magic != AURORA_SYSCALL_MAGIC)
		return 0;

	switch (code)
	{
	case MOUSE_IOCODE_SETPOS:
#ifdef ARCH_X64
		AuPS2MouseSetPos(ioctl->uint_1, ioctl->uint_2);
#endif
		break;
	default:
		break;
	}

	return 1;
}

/*
 * AuDevInputInitialise -- mounts to pipe
 * for hid devices, @mice and @kybrd
 */
void AuDevInputInitialise() {
	AuVFSNode* devfs = AuVFSFind("/dev");

	void* mice_input_buf = kmalloc(sizeof(AuInputMessage));
	memset(mice_input_buf, 0, sizeof(AuInputMessage));
	/* avoiding using pipe for latency issue */
	AuVFSNode* node = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(node, 0, sizeof(AuVFSNode));
	strcpy(node->filename, "mice");
	node->flags |= FS_FLAG_DEVICE;
	node->device = mice_input_buf;
	node->read = AuDevInputMiceRead;
	node->write = AuDevInputMiceWrite;
	node->open = 0;
	node->close = 0;
	node->iocontrol = AuDevMouseIoControl;
	mice_ = node;
	AuDevFSAddFile(devfs, "/", mice_);

	void* keybuf = kmalloc(sizeof(AuInputMessage));
	memset(keybuf, 0, sizeof(AuInputMessage));
	
	kybrd_ = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(kybrd_, 0, sizeof(AuVFSNode));
	strcpy(kybrd_->filename, "kybrd");
	kybrd_->flags |= FS_FLAG_DEVICE;
	kybrd_->device = keybuf;
	kybrd_->read = AuDevInputKybrdRead;
	kybrd_->write = AuDevInputKybrdWrite;
	AuDevFSAddFile(devfs, "/", kybrd_);
}