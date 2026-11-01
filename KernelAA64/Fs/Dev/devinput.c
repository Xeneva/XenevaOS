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

#include <Fs/Dev/devinput.h>
#include <Fs/vfs.h>
#include <_null.h>
#include <Fs/Dev/devfs.h>
#include <string.h>
#include <Mm/kmalloc.h>
#include <aurora.h>
#include <aucon.h>
#include <Sync/poll.h>
#include <Drivers/uart.h>
#include <Sync/waitq.h>
#include <string.h>

AuVFSNode* mice_;
AuVFSNode* kybrd_;
/* Mouse and keyboard have different IRQ producers. Keep independent SPSC
 * queues so neither producer races the other's write index. --axiss */
static AuInputRing mouse_ring;
static AuInputRing keyboard_ring;
static uint8_t ring_read_preference;
static AuWaitQueue* kybrdWaitQueue;
static bool _key_test = 0;

static void AuDevInputRingPublish(AuInputRing* ring, AuInputMessage* msg) {
	if (!msg)
		return;
	uint32_t write = ring->write;
	uint32_t next = (write + 1) % NUM_INPUT_RING_PACKETS;
	if (next == ring->read) {
		/* The producer never mutates the consumer-owned read index. */
		ring->dropped++;
		return;
	}
	memcpy(&ring->packets[write], msg, sizeof(AuInputMessage));
	dsb_sy_barrier();
	ring->write = next;
}

static AuInputMessage kbd_q[NUM_KEYBOARD_PACKETS];
static uint32_t kbd_r;
static uint32_t kbd_w;

static AuInputMessage console_kbd_q[NUM_KEYBOARD_PACKETS];
static uint32_t console_kbd_r;
static uint32_t console_kbd_w;

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
	AuDevInputRingPublish(&mouse_ring, outmsg);
}

/*
* AuDevReadkybrd -- reads packets from pipe
* to buffer
* @para, inputmsg -- Pointer to the buffer
*/
void AuDevReadKybrd(AuInputMessage* inputmsg) {
	if (!inputmsg)
		return;
	memset(inputmsg, 0, sizeof(AuInputMessage));
	if (kbd_r == kbd_w)
		return;
	memcpy(inputmsg, &kbd_q[kbd_r], sizeof(AuInputMessage));
	kbd_r = (kbd_r + 1) % NUM_KEYBOARD_PACKETS;
}

/*
 * AuDevReadConsoleKybrd -- reads packets from console-specific keyboard queue
 * @para, inputmsg -- Pointer to the buffer
 */
void AuDevReadConsoleKybrd(AuInputMessage* inputmsg) {
	if (!inputmsg)
		return;
	memset(inputmsg, 0, sizeof(AuInputMessage));
	if (console_kbd_r == console_kbd_w)
		return;
	memcpy(inputmsg, &console_kbd_q[console_kbd_r], sizeof(AuInputMessage));
	console_kbd_r = (console_kbd_r + 1) % NUM_KEYBOARD_PACKETS;
}

/*
* AuDevWritekybrd -- writes a packet to pipe
* @param outmsg -- packet to write
*/
void AuDevWriteKybrd(AuInputMessage* outmsg) {
	uint32_t next;
	if (!outmsg)
		return;
	AuDevInputRingPublish(&keyboard_ring, outmsg);
	next = (kbd_w + 1) % NUM_KEYBOARD_PACKETS;
	if (next == kbd_r)
		kbd_r = (kbd_r + 1) % NUM_KEYBOARD_PACKETS;
	memcpy(&kbd_q[kbd_w], outmsg, sizeof(AuInputMessage));
	kbd_w = next;

	/* Duplicate for console input to avoid race with /dev/kybrd consumers */
	uint32_t cnext = (console_kbd_w + 1) % NUM_KEYBOARD_PACKETS;
	if (cnext == console_kbd_r)
		console_kbd_r = (console_kbd_r + 1) % NUM_KEYBOARD_PACKETS;
	memcpy(&console_kbd_q[console_kbd_w], outmsg, sizeof(AuInputMessage));

	//wake all the pollers
	AuWaitQueue_Wake(kybrdWaitQueue);

	console_kbd_w = cnext;
}

size_t AuDevInputRingRead(AuVFSNode* fs, AuVFSNode* file, uint64_t* buffer, uint32_t length) {
	(void)fs;
	if (!file || !buffer || length < sizeof(AuInputMessage))
		return 0;
	AuInputRing* ring = NULL;
	/* Alternate when both queues are ready; otherwise drain the available one. */
	if (keyboard_ring.read != keyboard_ring.write &&
		(mouse_ring.read == mouse_ring.write || ring_read_preference == 0)) {
		ring = &keyboard_ring;
		ring_read_preference = 1;
	} else if (mouse_ring.read != mouse_ring.write) {
		ring = &mouse_ring;
		ring_read_preference = 0;
	} else if (keyboard_ring.read != keyboard_ring.write) {
		ring = &keyboard_ring;
		ring_read_preference = 1;
	}
	if (!ring)
		return 0;
	uint32_t read = ring->read;
	dsb_sy_barrier();
	memcpy(buffer, &ring->packets[read], sizeof(AuInputMessage));
	dsb_sy_barrier();
	ring->read = (read + 1) % NUM_INPUT_RING_PACKETS;
	return sizeof(AuInputMessage);
}

static int AuDevInputRingIoControl(AuVFSNode* file, int code, void* arg) {
	if (!file || !arg || code != INPUT_RING_IOCODE_GET_STATS)
		return 0;
	AuInputRingStats* stats = (AuInputRingStats*)arg;
	stats->mouse_dropped = mouse_ring.dropped;
	stats->keyboard_dropped = keyboard_ring.dropped;
	stats->mouse_pending =
		(mouse_ring.write + NUM_INPUT_RING_PACKETS - mouse_ring.read) % NUM_INPUT_RING_PACKETS;
	stats->keyboard_pending = (keyboard_ring.write + NUM_INPUT_RING_PACKETS - keyboard_ring.read) %
							  NUM_INPUT_RING_PACKETS;
	return 1;
}

/*
* AuPipeWrite -- write to pipe
* @param fs -- Pointer to the file system node
* @param file -- Pointer to the file, here we don't need it
* @param buffer -- Pointer to buffer where to put the data
* @param length -- length to read
*/
size_t AuDevInputMiceWrite(AuVFSNode* fs, AuVFSNode* file, uint64_t* buffer, uint32_t length) {
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
size_t AuDevInputMiceRead(AuVFSNode* fs, AuVFSNode* file, uint64_t* buffer, uint32_t length) {
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
size_t AuDevInputKybrdWrite(AuVFSNode* fs, AuVFSNode* file, uint64_t* buffer, uint32_t length) {
	if (!file)
		return 0;
	if (!buffer)
		return 0;
	AuDevWriteKybrd((AuInputMessage*)buffer);
	return (sizeof(AuInputMessage));
}

/*
* AuPipeRead -- reads from pipe
* @param fs -- Pointer to the file system node
* @param file -- Pointer to the file, here we don't need it
* @param buffer -- Pointer to buffer where to put the data
* @param length -- length to read
*/
size_t AuDevInputKybrdRead(AuVFSNode* fs, AuVFSNode* file, uint64_t* buffer, uint32_t length) {
	if (!file)
		return 0;
	if (!buffer)
		return 0;
	AuDevReadKybrd((AuInputMessage*)buffer);
	return (sizeof(AuInputMessage));
}

int AuDevInputKybrdPoll(AuVFSNode* fs, AuVFSNode* file, AuPollTable* pt) {
	AuPollWait(kybrdWaitQueue, pt);
	if (_key_test) {
		_key_test = 0;
		return POLLIN;
	}
	return 0;
}
/*
 * AuDevMouseIoControl -- controls the mouse device by command
 * @param file -- Pointer to mouse file
 * @param code -- code to pass as command
 * @param arg -- pointer to AuFileIoControl structure
 */
int AuDevMouseIoControl(AuVFSNode* file, int code, void* arg) {
	if (!file)
		return 0;
	AuFileIOControl* ioctl = (AuFileIOControl*)arg;
	if (!arg)
		return 0;
	/*if (ioctl->syscall_magic != AURORA_SYSCALL_MAGIC)
		return 0;*/

	switch (code) {
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
	if (!devfs) {
		AuTextOut("[aurora]: critical error in devinput, no dev file system found \r\n");
		return;
	}
	void* mice_input_buf = kmalloc(sizeof(AuInputMessage));
	memset(mice_input_buf, 0, sizeof(AuInputMessage));
	/* avoiding using pipe for latency issue */
	AuVFSNode* node = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(node, 0, sizeof(AuVFSNode));
	strcpy(node->filename, "mice");
	node->flags |= FS_FLAG_DEVICE;
	node->device = mice_input_buf;
	node->mode = 0660;
	node->read = AuDevInputMiceRead;
	node->write = AuDevInputMiceWrite;
	node->open = 0;
	node->close = 0;
	node->iocontrol = AuDevMouseIoControl;
	mice_ = node;
	AuDevFSAddFile(devfs, "/", mice_);

	void* keybuf = kmalloc(sizeof(AuInputMessage));
	memset(keybuf, 0, sizeof(AuInputMessage));

	kbd_r = kbd_w = 0;

	kybrd_ = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(kybrd_, 0, sizeof(AuVFSNode));
	strcpy(kybrd_->filename, "kybrd");
	kybrd_->flags |= FS_FLAG_DEVICE;
	kybrd_->device = keybuf;
	kybrd_->mode = 0660;
	kybrd_->read = AuDevInputKybrdRead;
	kybrd_->write = AuDevInputKybrdWrite;
	kybrd_->poll = &AuDevInputKybrdPoll;
	AuDevFSAddFile(devfs, "/", kybrd_);

	memset(&mouse_ring, 0, sizeof(mouse_ring));
	memset(&keyboard_ring, 0, sizeof(keyboard_ring));
	ring_read_preference = 0;
	AuVFSNode* ring = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(ring, 0, sizeof(AuVFSNode));
	strcpy(ring->filename, "input-ring");
	ring->flags |= FS_FLAG_DEVICE;
	ring->device = &mouse_ring;
	ring->read = AuDevInputRingRead;
	ring->iocontrol = AuDevInputRingIoControl;
	ring->mode = 0660;
	AuDevFSAddFile(devfs, "/", ring);
	_key_test = 0;
	kybrdWaitQueue = (AuWaitQueue*)kmalloc(sizeof(AuWaitQueue));

	AuTextOut("[aurora]: device input : kybrd, mouse, and input-ring registered \r\n");
}
