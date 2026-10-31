/**
 * devinputsrv.c -- /dev/inputsrv: kernel side of a user-space input driver.
 *
 * A server that owns the input devices through /dev/hwdev injects already
 * decoded events here. The kernel validates them, scales absolute pointer
 * positions to the screen, and feeds them into the normal input layer.
 */
#include <Fs/Dev/devhw.h>
#include <Fs/Dev/devfs.h>
#include <Fs/Dev/devinput.h>
#include <Fs/vfs.h>
#include <Cred/group.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Mm/kmalloc.h>
#include <Drivers/uart.h>
#include <aucon.h>
#include <inputsrv.h>
#include <string.h>
#include <_null.h>

static InputsrvStats stats;
static uint32_t logged;

static bool user_ptr(const void* p, size_t len) {
	uint64_t a = (uint64_t)p;
	return a && a < 0x0000800000000000ULL && a + len > a && a + len < 0x0000800000000000ULL;
}

static int inputsrv_ioctl(AuVFSNode* file, int code, void* arg) {
	(void)file;
	if (!AuHwdevCallerAllowed(AuHwdevCurrentProcess()))
		return INPUTSRV_E_PERM;

	if (code == INPUTSRV_IOC_STATS) {
		if (!user_ptr(arg, sizeof(InputsrvStats)))
			return INPUTSRV_E_INVAL;
		*(InputsrvStats*)arg = stats;
		return INPUTSRV_OK;
	}
	if (code != INPUTSRV_IOC_INJECT || !user_ptr(arg, sizeof(InputsrvEvent)))
		return INPUTSRV_E_INVAL;

	InputsrvEvent ev = *(InputsrvEvent*)arg;
	AuInputMessage msg;
	memset(&msg, 0, sizeof msg);

	if (ev.type == INPUTSRV_KEY) {
		msg.type = AU_INPUT_KEYBOARD;
		msg.code = ev.code & 0xFF;
	} else if (ev.type == INPUTSRV_ABS) {
		int w = AuConsoleGetScreenWidth(), h = AuConsoleGetScreenHeight();
		if (!ev.maxx || !ev.maxy || w <= 0 || h <= 0 || ev.x < 0 || ev.y < 0 ||
			(uint32_t)ev.x > ev.maxx || (uint32_t)ev.y > ev.maxy) {
			stats.rejected++;
			return INPUTSRV_E_INVAL;
		}
		msg.type = AU_INPUT_MOUSE;
		msg.xpos = (int32_t)(((uint64_t)ev.x * (uint64_t)w) / ev.maxx);
		msg.ypos = (int32_t)(((uint64_t)ev.y * (uint64_t)h) / ev.maxy);
		msg.button_state = ev.buttons;
	} else {
		stats.rejected++;
		return INPUTSRV_E_INVAL;
	}

	/* the input layer's queues are lock-free and written from interrupt handlers by
	 * the in-kernel drivers; keep this write atomic with respect to them */
	uint64_t daif = read_daif();
	mask_irqs();
	if (msg.type == AU_INPUT_KEYBOARD) {
		AuDevWriteKybrd(&msg);
		stats.keys++;
	} else {
		AuDevWriteMice(&msg);
		stats.mice++;
	}
	restore_daif(daif);

	if (logged < 12) {             /* enough to see the path work in a serial log */
		logged++;
		if (msg.type == AU_INPUT_KEYBOARD)
			UARTDebugOut("[inputsrv]: key %x injected\r\n", msg.code);
		else
			UARTDebugOut("[inputsrv]: pointer %d,%d buttons %x injected\r\n", msg.xpos, msg.ypos, msg.button_state);
	}
	return INPUTSRV_OK;
}

void AuInputSrvInitialize(void) {
	AuVFSNode* fsys = AuVFSFind("/dev");
	if (!fsys)
		return;
	AuVFSNode* node = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(node, 0, sizeof(AuVFSNode));
	strcpy(node->filename, "inputsrv");
	node->flags = FS_FLAG_DEVICE;
	node->uid = 0;
	node->gid = AuCredGetGroupID(AURORA_GID_MISC_DAEMONS);
	node->device = fsys;
	node->iocontrol = inputsrv_ioctl;
	AuDevFSAddFile(fsys, "/", node);
	UARTDebugOut("[inputsrv]: /dev/inputsrv registered\r\n");
}
