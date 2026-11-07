#include <Fs/Dev/devclip.h>
#include <Fs/Dev/devfs.h>
#include <string.h>
#include <Mm/kmalloc.h>
#include <aurora.h>
#include <Cred/group.h>
#include <Drivers/uart.h>

/* The one copy of the text. Everything -- the device, and the console's
 * Ctrl+V -- goes through these four functions, so there is no second buffer
 * to keep in step. */
static uint8_t clip_buf[CLIPBOARD_MAX];
static volatile uint32_t clip_len;

/*
 * AuClipboardSet -- replace the clipboard
 */
void AuClipboardSet(const void* src, uint32_t len) {
	if (!src) {
		clip_len = 0;
		return;
	}
	if (len > CLIPBOARD_MAX)
		len = CLIPBOARD_MAX;
	memcpy(clip_buf, src, len);
	/* Published last: a reader sees either the previous contents or this
	 * one, never a length that runs past what has been copied. */
	clip_len = len;
}

/*
 * AuClipboardGet -- copy the clipboard out without consuming it
 */
uint32_t AuClipboardGet(void* dst, uint32_t max) {
	uint32_t len = clip_len;
	if (!dst || !max || !len)
		return 0;
	if (len > max)
		len = max;
	memcpy(dst, clip_buf, len);
	return len;
}

uint32_t AuClipboardLength() {
	return clip_len;
}

void AuClipboardClear() {
	clip_len = 0;
}

/*
 * ClipRead -- report the clipboard; reading is not a paste, it is a look
 */
static size_t ClipRead(AuVFSNode* node, AuVFSNode* file, uint64_t* buffer,
					   uint32_t length) {
	(void)node;
	(void)file;
	if (!buffer || !length)
		return 0;
	return (size_t)AuClipboardGet(buffer, length);
}

/*
 * ClipWrite -- copy the caller's bytes in, replacing whatever was there
 */
static size_t ClipWrite(AuVFSNode* node, AuVFSNode* file, uint64_t* buffer,
						uint32_t length) {
	(void)node;
	(void)file;
	if (!buffer || !length)
		return 0;
	AuClipboardSet(buffer, length);
	return length;
}

static int ClipIoControl(AuVFSNode* file, int code, void* arg) {
	(void)file;
	(void)arg;
	if (code == CLIP_IOCODE_CLEAR) {
		AuClipboardClear();
		return 1;
	}
	return 0;
}

/*
 * AuDevClipInitialise -- add /dev/clipboard to the device file system
 */
void AuDevClipInitialise() {
	AuVFSNode* devfs = AuVFSFind("/dev");
	if (!devfs) {
		/* UARTDebugOut, not AuTextOut: this runs after the graphics console
		 * takes over, where AuPutS paints the framebuffer and never reaches
		 * the serial console the boot log is captured from. */
		UARTDebugOut("[aurora]: no dev file system, clipboard not registered \r\n");
		return;
	}
	AuVFSNode* node = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(node, 0, sizeof(AuVFSNode));
	strcpy(node->filename, "clipboard");
	node->flags |= FS_FLAG_DEVICE;
	/* Same group the misc devices take: AuCredCheckPermissions has no mode
	 * bits in it -- owner, root or matching group only -- so a gid of 0
	 * would leave the clipboard readable by root alone, and there is no
	 * clipboard an ordinary process cannot share. */
	node->gid = AuCredGetGroupID(AURORA_GID_MISC_WORLD);
	node->device = clip_buf;
	node->read = ClipRead;
	node->write = ClipWrite;
	node->iocontrol = ClipIoControl;
	AuDevFSAddFile(devfs, "/", node);
	UARTDebugOut("[aurora]: device clipboard registered \r\n");
}
