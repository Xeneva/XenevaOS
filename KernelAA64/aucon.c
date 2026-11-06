/**
* @file aucon.c
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

#include <stdint.h>
#if defined(__GNUC__) || defined(__clang__)
#ifndef __cplusplus
#include <stdbool.h>
#endif
#endif
#include <aucon.h>
#include <_null.h>
#include "font.h"
#include <Mm/vmmngr.h>
#include <Mm/pmmngr.h>
#include <Mm/kmalloc.h>
#include <string.h>
#include <va_list.h>
#include <stdio.h>
#include <Hal/AA64/sched.h>
#include <process.h>
#include <Fs/vfs.h>
#include <Fs/tty.h>
#include <Fs/Dev/devfs.h>
#include <Fs/Dev/devinput.h>
#include <Fs/Dev/devclip.h>
#include <Fs/Dev/devgpu.h>
#include <Drivers/uart.h>
#include <Hal/AA64/aa64cpu.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Ipc/postbox.h>

/* temporary input-freeze diagnostics */
extern void AuVirtioKbdDebug(void);

uint8_t* font_data;
uint32_t console_x;
uint32_t console_y;
uint32_t* __framebuffer;
AuConsole* aucon;
uint32_t redmask, bluemask, greenmask;
uint32_t resvmask;
size_t h_res, v_res;
BOOL early_;
bool bypass_autextout;
/* Set once the compositor owns the display. The compositor's canvas aliases
 * this same framebuffer, so further text output here would scribble over the
 * live desktop. UART output never routes through here and is unaffected. */
static bool display_owned;

/* Present hook for the no-compositor case (see aucon.h). Skipped once the
 * display token is claimed; from there the compositor owns presentation
 * and must not fight a second submitter over the GPU command queue. */
static AuConsolePresentFn present_hook;

/* Dirty-row tracking for the mirror. Low-level painters (AuPutC/AuPutS)
 * only mark rows; high-level entries (AuTextOut, AuConsoleWrite) suppress
 * per-fragment submits and flush once, so a userspace write that paints
 * char-by-char still costs exactly one transfer+flush. Rows are text rows
 * (16 px each); conversion to pixels happens only here. --axiss */
static int mirror_suppress;
static int mirror_dirty_top;
static int mirror_dirty_bot;
static bool mirror_dirty_valid;

static void AuConsoleMarkDirty(int y_top, int y_bot) {
	if (y_bot < y_top) {
		/* Cursor wrapped backwards: a scroll moved every row. */
		mirror_dirty_top = 0;
		mirror_dirty_bot = 0x7fffffff;
		mirror_dirty_valid = true;
		return;
	}
	if (!mirror_dirty_valid) {
		mirror_dirty_top = y_top;
		mirror_dirty_bot = y_bot;
		mirror_dirty_valid = true;
	} else {
		if (y_top < mirror_dirty_top)
			mirror_dirty_top = y_top;
		if (y_bot > mirror_dirty_bot)
			mirror_dirty_bot = y_bot;
		if (mirror_dirty_bot == 0x7fffffff)
			mirror_dirty_top = 0;
	}
}

static void AuConsoleFlushMirror(void) {
	int y_px, bot_px;
	if (!mirror_dirty_valid)
		return;
	mirror_dirty_valid = false;
	if (display_owned || !aucon)
		return;
	if (!present_hook) {
#ifdef __XENEVA_GPUSRV__
		/* TERM has no compositor/token owner. Keep console damage flowing to
		 * the user-space GPU proxy without waiting for its server. */
		if (mirror_dirty_bot == 0x7fffffff)
			AuGpuProxyDamage(0, 0, (int)aucon->width, (int)aucon->height);
		else {
			y_px = mirror_dirty_top * 16;
			bot_px = (mirror_dirty_bot + 1) * 16;
			if (y_px < 0) y_px = 0;
			if (bot_px > (int)aucon->height) bot_px = aucon->height;
			if (bot_px > y_px)
				AuGpuProxyDamage(0, y_px, (int)aucon->width, bot_px - y_px);
		}
#endif
		return;
	}
	if (mirror_dirty_bot == 0x7fffffff) {
		present_hook(aucon->buffer, aucon->pitch, 0, 0,
			(int)aucon->width, (int)aucon->height);
		return;
	}
	y_px = mirror_dirty_top * 16;
	bot_px = (mirror_dirty_bot + 1) * 16;
	if (y_px < 0)
		y_px = 0;
	if (bot_px > (int)aucon->height)
		bot_px = aucon->height;
	if (bot_px > y_px)
		present_hook(aucon->buffer, aucon->pitch, 0, y_px,
			(int)aucon->width, bot_px - y_px);
}

void (*_print_func)(const char* text, ...);

#define CONSOLE_BACKGROUND 0x00000000
#define CONSOLE_FOREGROUND 0xFFFFFFFF

extern void AuUartPutString(const char* s);

void AuTestPrint() {
	AuUartPutString("Upto here \r\n");
}
/**
 * @brief AuConsoleInitialize -- initialize kernel direct screen
 * console
 * @param info -- Pointer to kernel boot info structure
 */
void AuConsoleInitialize(PKERNEL_BOOT_INFO info, bool early) {
	if (early) {
		/* the UEFI loader callback points into XNLDR's image, cant rely on
		 * that bs mapping once VM bootstrap kicks in since its not part
		 * of the kernel address space --axiss */
		_print_func = UARTDebugOut;
		early_ = early;
		AuUartPutString("[aurora]: early console set to UARTDebugOut \r\n");
	}
	aucon = NULL;
	bypass_autextout = false;
}

/* A manual loader desktop override names a size the firmware GOP never
 * scans out; only a GPU driver repointing the console through
 * AuConsoleSetConInfo can back it. Until then the console framebuffer is
 * all there is, and reporting the larger size let init's splash paint
 * past the SCREEN_GET_FB mapping and die on the guest black. Report the
 * real console size while the override is bigger than what can be
 * drawn. --axiss */
static void aucon_desktop_size(uint32_t* width, uint32_t* height) {
	uint32_t w = aucon->desktop_width ? aucon->desktop_width : aucon->width;
	uint32_t h = aucon->desktop_height ? aucon->desktop_height : aucon->height;
	if (w > aucon->width || h > aucon->height) {
		w = aucon->width;
		h = aucon->height;
	}
	*width = w;
	*height = h;
}

/**
 * @brief AuConsoleIoControl -- iocontrol implementation
 * for au console, called from user space
 * @param file -- Pointer to console file
 * @param code -- control code number
 * @param arg -- Pointer to any user data
 */
int AuConsoleIoControl(AuVFSNode* file, int code, void* arg) {
	int ret = 0;
	AuFileIOControl* ioctl = (AuFileIOControl*)arg;
	(void)file;

	if (!aucon)
		return 0;

	switch (code) {
	case TIOCGWINSZ: {
		WinSize* sz = (WinSize*)arg;
		if (!sz)
			return 0;
		sz->ws_col = (uint16_t)(aucon->width / 9);
		sz->ws_row = (uint16_t)(aucon->height / 16);
		sz->ws_xpixel = (uint16_t)aucon->width;
		sz->ws_ypixel = (uint16_t)aucon->height;
		return 0;
	}
	case TIOCSWINSZ:
	case TIOSPGRP:
		return 0;
	case SCREEN_GETWIDTH:
		ioctl->uint_1 = AuConsoleGetScreenWidth();
		break;
	case SCREEN_GETHEIGHT:
		ioctl->uint_1 = AuConsoleGetScreenHeight();
		break;
	case SCREEN_GETBPP: {
		uint32_t bpp = aucon->bpp;
		ioctl->uint_1 = bpp;
		break;
	}
	case SCREEN_GET_SCANLINE: {
		uint16_t scanline = aucon->scanline;
		ioctl->ushort_1 = scanline;
		break;
	}
	case SCREEN_GET_PITCH: {
		uint32_t pitch = aucon->pitch;
		ioctl->uint_1 = pitch;
		break;
	}
	case SCREEN_GET_FB: {
		/* here need to map the physical framebuffer for desired process */
		AA64Thread* thr = AuGetCurrentThread();
		if (!thr)
			break;
		AuProcess* proc = AuProcessFindThread(thr);
		if (!proc) {
			proc = AuProcessFindSubThread(thr);
			if (!proc)
				break;
		}
		uint64_t vmaddr = (uint64_t)AuGetFreePage(1, NULL);
		uint64_t fbaddr = (uint64_t)__framebuffer;
		/* 800x600 (and any size not a multiple of 4096) used to omit the
		 * last partial page. Init splash then died on _fill_screen and the
		 * guest stayed black. 1024x768 happened to be exact. --axiss */
		size_t fb_pages = (aucon->size + PAGE_SIZE - 1) / PAGE_SIZE;
		for (size_t i = 0; i < fb_pages; i++) {
			AuMapPage((uint64_t)fbaddr + (i * PAGE_SIZE),
					  vmaddr + (i * PAGE_SIZE),
					  PTE_NORMAL_NON_CACHEABLE | PTE_AP_RW_USER);
		}
		//uint64_t buffaddr = (uint64_t)aucon->buffer;
		ioctl->ulong_1 = vmaddr;
		break;
	}

	case SCREEN_REG_MNGR: {
		return 1;
		break;
	}
	case SCREEN_UPDATE: {
		/* Userspace present request: (x, y, w, h) in ushort_1/ushort_2/
		 * ulong_1/ulong_2, a zero width or height meaning "whole screen".
		 * The compositor sends one bounding box per frame once it has
		 * blitted into the mapped framebuffer; the text console never
		 * uses this path. --axiss */
		int x = ioctl->ushort_1;
		int y = ioctl->ushort_2;
		int w = (int)ioctl->ulong_1;
		int h = (int)ioctl->ulong_2;
		if (w <= 0 || h <= 0) {
			x = 0;
			y = 0;
			w = (int)aucon->width;
			h = (int)aucon->height;
		}
		AuConsolePresentRect(x, y, w, h);
		break;
	}
	case SCREEN_RESTORE_BOOT_FB: {
		ret = AuConsoleRestoreBootFb();
		break;
	}
	}
	return ret;
}

static int _con_shift;
static int _con_ctrl;
static int _con_esc;

static const char _con_map[58] = {
	0,	 0,	  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
	'\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
	'a',  's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,	 '\\',
	'z',  'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,	0,	 0,	 ' '
};

static const char _con_map_s[58] = {
	0,	 0,	  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
	'\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,
	'A',  'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,	 '|',
	'Z',  'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,	0,	 0,	 ' '
};

/* Bytes waiting for the next reader of stdin. Ctrl+V fills this from the
 * clipboard, so a pasted line arrives exactly like one that was typed and
 * every console application gets paste without knowing it exists. */
static uint8_t con_paste[CLIPBOARD_MAX];
static uint32_t con_paste_r;
static uint32_t con_paste_w;

/*
 * AuConsolePasteClipboard -- queue the clipboard ahead of the keyboard
 */
static void AuConsolePasteClipboard() {
	/* Drop what the reader has already taken, then append. Compacting
	 * instead of wrapping keeps both indices trivial. There is no memmove
	 * in this libc, but the destination is below the source, so a forward
	 * byte copy is the overlap-safe direction. */
	if (con_paste_r) {
		uint32_t left = con_paste_w - con_paste_r;
		uint32_t i;
		for (i = 0; i < left; i++)
			con_paste[i] = con_paste[con_paste_r + i];
		con_paste_w = left;
		con_paste_r = 0;
	}
	uint32_t space = CLIPBOARD_MAX - con_paste_w;
	if (!space)
		return;
	con_paste_w += AuClipboardGet(&con_paste[con_paste_w], space);
}

/**
 * @brief AuConsoleMapKey -- map a virtio/XT scancode to ASCII
 * @param code -- scancode from /dev/kybrd
 */
static char AuConsoleMapKey(uint32_t code) {
	uint8_t sc = (uint8_t)(code & 0xFF);
	if (sc == 0x2a || sc == 0x36) {
		_con_shift = 1;
		return 0;
	}
	if (sc == 0xaa || sc == 0xb6) {
		_con_shift = 0;
		return 0;
	}
	if (sc == 0x1d) {
		_con_ctrl = 1;
		return 0;
	}
	if (sc == 0x9d) {
		_con_ctrl = 0;
		return 0;
	}
	if (sc & 0x80)
		return 0;
	if (sc >= sizeof(_con_map))
		return 0;
	/* Ctrl held: V pastes instead of typing a 'v', C becomes ^C so the
	 * application can copy its own selection back. Before the plain map,
	 * so paste reaches whoever owns stdin. */
	if (_con_ctrl) {
		if (sc == 0x2f) {
			AuConsolePasteClipboard();
			return 0;
		}
		if (sc == 0x2e)
			return 3;
	}
	return _con_shift ? _con_map_s[sc] : _con_map[sc];
}

/**
 * @brief AuConsoleRead -- read ASCII from the virtio keyboard
 * @param node -- unused
 * @param file -- unused
 * @param buffer -- destination
 * @param length -- max bytes
 */
static size_t AuConsoleRead(AuVFSNode* node, AuVFSNode* file, uint64_t* buffer, uint32_t length) {
	uint8_t* out;
	uint32_t n;
	(void)node;
	(void)file;
	if (!buffer || !length)
		return 0;
	out = (uint8_t*)buffer;
	n = 0;
	/* temporary input-freeze diagnostics */
	static uint32_t con_dbg_polls;
	static uint32_t con_dbg_msgs;
	while (n < length) {
		/* Pasted bytes first: they were meant for this reader and must
		 * not wait behind an idle keyboard. */
		if (con_paste_r < con_paste_w) {
			out[n++] = con_paste[con_paste_r++];
			if (out[n - 1] == '\n')
				break;
			continue;
		}
		AuInputMessage msg;
		char c;
		memset(&msg, 0, sizeof(msg));
		AuDevReadConsoleKybrd(&msg);
		c = 0;
		if (msg.type == AU_INPUT_KEYBOARD){
			c = AuConsoleMapKey(msg.code);
			UARTDebugOut("Reading console key++ \r\n");
		}if (c == '\r')
			c = '\n';
		if (msg.type != 0) {
			con_dbg_msgs++;
			UARTDebugOut("[con-dbg]: type=%d code=%x ch=%c \n",
				(int)msg.type, (int)msg.code, c ? c : '.');
		}
		if (c) {
			out[n++] = (uint8_t)c;
			if (c == '\n')
				break;
		} else if (msg.type != 0) {
			/* key-up / modifier: drain the queue, do not sleep */
			continue;
		} else {
			if (n)
				break;
			con_dbg_polls++;
			if ((con_dbg_polls % 500) == 0) {
				UARTDebugOut("[con-dbg]: polls=%d msgs=%d \n",
					(int)con_dbg_polls, (int)con_dbg_msgs);
				AuVirtioKbdDebug();
			}
			AA64Thread* thr = AuGetCurrentThread();
			if (thr) {
				AuSleepThread(thr, 10);
				AuScheduleNext();
			}
		}
	}
	return n;
}

/**
 * @brief AuConsoleWrite -- draw bytes on the GOP/ramfb console
 * @param node -- unused
 * @param file -- unused
 * @param buffer -- source
 * @param length -- byte count
 */
static size_t AuConsoleWrite(AuVFSNode* node, AuVFSNode* file, uint64_t* buffer, uint32_t length) {
	uint8_t* in;
	uint32_t i;
	(void)node;
	(void)file;
	if (!buffer)
		return 0;
	/* Batch the per-char AuPutS paints below into one mirror submit. --axiss */
	mirror_suppress++;
	in = (uint8_t*)buffer;
	for (i = 0; i < length; i++) {
		char c = (char)in[i];
		char tmp[2];
		if (_con_esc) {
			if (c == 0x07 || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
				_con_esc = 0;
			continue;
		}
		if (c == 0x1b) {
			_con_esc = 1;
			continue;
		}
		tmp[0] = c;
		tmp[1] = 0;
		AuPutS(tmp);
	}
	mirror_suppress--;
	AuConsoleFlushMirror();
	return length;
}

/**
 * @brief AuConsolePostInitialise -- initialise the post console process
 * @param info -- pointer to kernel boot info structure
 */
void AuConsolePostInitialise(PKERNEL_BOOT_INFO info) {
	/* little boot won't support framebuffer, need to
	 * rely on graphics driver
	 */
	//if (info->boot_type == BOOT_LITTLEBOOT_ARM64) {
	//	info->graphics_framebuffer =
	//}
	if (!info->graphics_framebuffer) {
		AuTextOut(
			"[aurora]: tur luck nai, framebuffer daalu napali, iss iss beya lagi jai deii \r\n");
		AuTextOut("[aurora]: return marisu deii, byee byee \r\n");
		return;
	}
	aucon = (AuConsole*)kmalloc(sizeof(AuConsole));
	memset(aucon, 0, sizeof(AuConsole));
	size_t fb_sz = (info->fb_size + PAGE_SIZE - 1) / PAGE_SIZE;
	for (int i = 0; i < fb_sz; i++)
		AuMapPage((uint64_t)info->graphics_framebuffer + (i * PAGE_SIZE),
				  0xFFFFD00000200000 + (i * 4096),
				  PTE_NORMAL_NON_CACHEABLE);

	AuTextOut("[aucon]: graphics framebuffer : %x \r\n", info->graphics_framebuffer);
	AuTextOut(
		"[aucon]: width : %d px , height : %d px \r\n", info->X_Resolution, info->Y_Resolution);
	early_ = false;
	aucon->buffer = (uint32_t*)0xFFFFD00000200000;
	aucon->width = info->X_Resolution;
	aucon->height = info->Y_Resolution;
	/* Desktop size for the compositor: manual loader override if present,
	 * else the GOP mode. Text output keeps using width/height above. --axiss */
	aucon->desktop_width = info->X_Resolution;
	aucon->desktop_height = info->Y_Resolution;
	#ifdef __XENEVA_GPUSRV__
	/* GPUSRV starts with the firmware framebuffer as its stable staging
	 * surface. Do not expose a loader-only desktop override until the user
	 * server owns a suitably sized backing surface. */
	#else
	if (info->DesktopOverrideWidth >= 640 && info->DesktopOverrideWidth <= 4096 &&
		info->DesktopOverrideHeight >= 480 && info->DesktopOverrideHeight <= 4096) {
		aucon->desktop_width = info->DesktopOverrideWidth;
		aucon->desktop_height = info->DesktopOverrideHeight;
	}
	#endif
	aucon->bpp = 32;
	aucon->scanline = info->pixels_per_line;
	aucon->pitch = 4 * info->pixels_per_line;
	aucon->size = info->fb_size;

	/* the _framebuffer global variable was just there lying freely
	 * decided to use it to store the address of physical framebuffer
	 * from BOOT info, let's use that
	 */
	__framebuffer = info->graphics_framebuffer;

	aucon->early_mode = false;
	console_x = console_y = 0;
	redmask = info->redmask;
	greenmask = info->greenmask;
	bluemask = info->bluemask;
	h_res = info->X_Resolution;
	v_res = info->Y_Resolution;
	for (int w = 0; w < info->X_Resolution; w++) {
		for (int h = 0; h < info->Y_Resolution; h++) {
			aucon->buffer[h * info->X_Resolution + w] = CONSOLE_BACKGROUND;
		}
	}

	early_ = 0;

	AuVFSNode* fsys = AuVFSFind("/dev");
	AuVFSNode* file = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(file, 0, sizeof(AuVFSNode));
	strcpy(file->filename, "graph");
	file->flags = FS_FLAG_DEVICE;
	file->uid = 0;
	/* The stable staging framebuffer is a desktop-user surface, not a hardware
	 * capability. Hardware ownership remains gated by /dev/hwdev and
	 * /dev/gpusrv. */
	file->gid = AuCredGetGroupID(AURORA_GID_MISC_WORLD);
	file->device = fsys;
	file->read = 0;
	file->write = 0;
	file->iocontrol = AuConsoleIoControl;
	AuDevFSAddFile(fsys, "/", file);

	file = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(file, 0, sizeof(AuVFSNode));
	strcpy(file->filename, "console");
	file->flags = FS_FLAG_DEVICE;
	file->read = AuConsoleRead;
	file->write = AuConsoleWrite;
	file->iocontrol = AuConsoleIoControl;
	AuDevFSAddFile(fsys, "/", file);
}

int_fast8_t high_set_bit(size_t sz) {
	int_fast8_t count = -1;
	while (sz) {
		sz >>= 1;
		count += 1;
	}
	return count;
}

int_fast8_t low_set_bit(size_t sz) {
	int_fast8_t count = -1;
	while (sz) {
		count += 1;
		if ((sz & 1) != 0)
			break;
		sz >>= 1;
	}
	return count;
}

#define RGB(r, g, b) ((r & 0xFF) | ((g << 8) & 0xFF00) | ((b << 16) & 0xFF0000))

#define RED(col) (col & 0xFF)

#define GREEN(col) ((col >> 8) & 0xFF)

#define BLUE(col) ((col >> 16) & 0xFF)

/**
 * @brief AuPutPixel -- puts a pixel on the screen
 * @param x -- x location of the screen
 * @param y -- y location of the screen
 * @param col -- color of the pixel
 */
void AuPutPixel(size_t x, size_t y, uint32_t col) {
	uint32_t* framebuffer = aucon->buffer;
	uint32_t bpp = aucon->bpp;
	uint32_t pixelPerLine = aucon->scanline;

	/*uint32_t* pixelloc = RAW_OFFSET(uint32_t*,framebuffer, (pixelPerLine * y + x) * (bpp / 8));
	size_t pixel = ((RED(col) << low_set_bit(redmask)) & redmask) |
		((GREEN(col) << low_set_bit(greenmask)) & greenmask) |
		((BLUE(col) << low_set_bit(bluemask)) & bluemask) |
		(*pixelloc & resvmask);
	*pixelloc = pixel;*/
	/* A glyph drawn below the last row (the console can do this when a message
	 * lands on the bottom row) used to write past the end of the framebuffer and
	 * fault the whole kernel. Drop pixels that are off screen. */
	if (x >= aucon->width || y >= aucon->height)
		return;
	framebuffer[y * aucon->width + x] = col;
}

/**
 * @brief Put a character to console output 
 * @param c -- character to print
 */
void AuConsoleSetDisplayOwned(void) {
	display_owned = true;
}

void AuConsoleSetPresentHook(AuConsolePresentFn fn) {
	present_hook = fn;
}

/**
 * @brief AuConsolePresentRect -- hand an explicit damage rectangle to the
 * scanout mirror installed by AuConsoleSetPresentHook.
 *
 * The text console marks its own damage and flushes through
 * AuConsoleFlushMirror(); this entry is for userspace, which paints into the
 * mapped framebuffer directly and has no way to say "this much changed"
 * other than SCREEN_UPDATE. Unlike the text mirror it is deliberately not
 * gated on display_owned: the request comes from the process that owns the
 * display, and muzzling it would leave the scanout frozen on its last text
 * frame once the compositor takes over.
 */
void AuConsolePresentRect(int x, int y, int w, int h) {
	if (!present_hook || !aucon)
		return;

	int right = x + w;
	int bottom = y + h;
	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	if (right > (int)aucon->width)
		right = (int)aucon->width;
	if (bottom > (int)aucon->height)
		bottom = (int)aucon->height;
	if (right <= x || bottom <= y)
		return;

	present_hook(aucon->buffer, aucon->pitch, x, y, right - x, bottom - y);
}

void AuPutC(char c) {
	if (early_) {
		if (is_uart_initialized())
			uartPutc(c);
		else if (_print_func) {
			char buf[2] = {c, 0};
			_print_func(buf);
		}
		return;
	}

	/* Compositor owns the scanout; keep the pixels, drop the glyph. */
	if (display_owned)
		return;

	int pc_top = (int)console_y;
	bool pc_scrolled = false;

	if (console_x > v_res / 9) {
		console_x = 0;
		console_y++;
	}

	for (size_t y = 0; y < 16; ++y) {
		for (size_t x = 0; x < 8; ++x) {
			const bx_fontcharbitmap_t entry = bx_vgafont[c];
			if (entry.data[y] & (1 << x)) {
				AuPutPixel(x + console_x * 9, y + console_y * 16, CONSOLE_FOREGROUND);
			} else {
				AuPutPixel(x + console_x * 9, y + console_y * 16, CONSOLE_BACKGROUND);
			}
		}
		AuPutPixel(8 + console_x * 9, y + console_y * 16, CONSOLE_BACKGROUND);
	}

	console_x++;

	uint32_t* lfb = aucon->buffer;
	if (console_y + 1 > v_res / 16) {
		for (int i = 0; i < (v_res - 16) * h_res; i++)
			lfb[i] = lfb[i + h_res * 16];
		for (int i = (v_res - 16) * h_res; i < v_res * h_res; i++)
			lfb[i] = CONSOLE_BACKGROUND;
		for (int i = 0; i < h_res * (v_res - 16); i++)
			lfb[i] = lfb[i + h_res * 16];

		memset(lfb + h_res * (v_res - 16), 0, h_res * 16 * sizeof(uint32_t));
		console_y--;
		pc_scrolled = true;
	}
	if (pc_scrolled)
		AuConsoleMarkDirty(1, 0); /* reversed => full screen */
	else
		AuConsoleMarkDirty(pc_top, (int)console_y);
	if (!mirror_suppress)
		AuConsoleFlushMirror();
}

/**
 * @brief Prints string to console output
 * @param str -- string to print
 */
void AuPutS(char* str) {
	if (early_) {
		if (is_uart_initialized())
			uartPuts(str);
		else if (_print_func)
			_print_func(str);
		return;
	}

	/* Compositor owns the scanout; keep the pixels, drop the glyph. */
	if (display_owned)
		return;

	int ps_top = (int)console_y;
	bool ps_scrolled = false;
	uint32_t* lfb = aucon->buffer;
	while (*str) {
		if (*str > 0xFF) {
			//unicode
		} else if (*str == '\n') {
			++console_y;
			console_x = 0;
		} else if (*str == '\r') {
		} else if (*str == '\b') {
			/* Destructive: this console has no line discipline to echo
			 * erase sequences, so clear the cell or the glyph stays
			 * behind forever. Geometry matches the glyph painter below:
			 * 8 px plus the background column, 16 rows. --axiss */
			if (console_x > 0) {
				--console_x;
				for (size_t y = 0; y < 16; ++y)
					for (size_t x = 0; x < 9; ++x)
						AuPutPixel(x + console_x * 9, y + console_y * 16,
							CONSOLE_BACKGROUND);
			}
		} else {
			const bx_fontcharbitmap_t entry = bx_vgafont[*str];
			for (size_t y = 0; y < 16; ++y) {
				for (size_t x = 0; x < 8; ++x) {
					if (entry.data[y] & (1 << x)) {
						AuPutPixel(x + console_x * 9, y + console_y * 16, CONSOLE_FOREGROUND);
					} else {
						AuPutPixel(x + console_x * 9, y + console_y * 16, CONSOLE_BACKGROUND);
					}
				}
				AuPutPixel(8 + console_x * 9, y + console_y * 16, CONSOLE_BACKGROUND);
			}
			++console_x;
			if (console_x > h_res / 9) {
				console_x = 0;
				++console_y;
			}
		}

		++str;
	}

	/* Scroll */
	if (console_y + 1 > v_res / 16) {
		for (int i = 0; i < (v_res - 16) * h_res; i++)
			lfb[i] = lfb[i + h_res * 16];
		for (int i = (v_res - 16) * h_res; i < v_res * h_res; i++)
			lfb[i] = CONSOLE_BACKGROUND;
		for (int i = 0; i < h_res * (v_res - 16); i++)
			lfb[i] = lfb[i + h_res * 16];

		memset(lfb + h_res * (v_res - 16), 0, h_res * 16 * sizeof(uint32_t));
		console_y--;
		ps_scrolled = true;
	}
	if (ps_scrolled)
		AuConsoleMarkDirty(1, 0); /* reversed => full screen */
	else
		AuConsoleMarkDirty(ps_top, (int)console_y);
	if (!mirror_suppress)
		AuConsoleFlushMirror();
}

/**
 * @brief Prints string to console output
 * @param str -- string to print
 * @param col -- foreground color
 */
void AuPutS_Color(char* str, uint32_t color) {
	if (early_) {
		if (is_uart_initialized())
			uartPuts(str);
		else if (_print_func)
			_print_func(str);
		return;
	}

	/* Compositor owns the scanout; keep the pixels, drop the glyph. */
	if (display_owned)
		return;

	int pc2_top = (int)console_y;
	bool pc2_scrolled = false;
	uint32_t* lfb = aucon->buffer;
	while (*str) {
		if (*str > 0xFF) {
			//unicode
		} else if (*str == '\n') {
			++console_y;
			console_x = 0;
		} else if (*str == '\r') {
		} else if (*str == '\b') {
			/* Destructive, same rationale as AuPutS above. --axiss */
			if (console_x > 0) {
				--console_x;
				for (size_t y = 0; y < 16; ++y)
					for (size_t x = 0; x < 9; ++x)
						AuPutPixel(x + console_x * 9, y + console_y * 16,
							CONSOLE_BACKGROUND);
			}
		} else {
			const bx_fontcharbitmap_t entry = bx_vgafont[*str];
			for (size_t y = 0; y < 16; ++y) {
				for (size_t x = 0; x < 8; ++x) {
					if (entry.data[y] & (1 << x)) {
						AuPutPixel(x + console_x * 9, y + console_y * 16, color);
					} else {
						AuPutPixel(x + console_x * 9, y + console_y * 16, CONSOLE_BACKGROUND);
					}
				}
				AuPutPixel(8 + console_x * 9, y + console_y * 16, CONSOLE_BACKGROUND);
			}
			++console_x;
			if (console_x > h_res / 9) {
				console_x = 0;
				++console_y;
			}
		}

		++str;
	}

	/* Scroll */
	if (console_y + 1 > v_res / 16) {
		for (int i = 0; i < (v_res - 16) * h_res; i++)
			lfb[i] = lfb[i + h_res * 16];
		for (int i = (v_res - 16) * h_res; i < v_res * h_res; i++)
			lfb[i] = CONSOLE_BACKGROUND;
		for (int i = 0; i < h_res * (v_res - 16); i++)
			lfb[i] = lfb[i + h_res * 16];

		memset(lfb + h_res * (v_res - 16), 0, h_res * 16 * sizeof(uint32_t));
		console_y--;
		pc2_scrolled = true;
	}
	if (pc2_scrolled)
		AuConsoleMarkDirty(1, 0); /* reversed => full screen */
	else
		AuConsoleMarkDirty(pc2_top, (int)console_y);
	if (!mirror_suppress)
		AuConsoleFlushMirror();
}

#ifdef _MSC_VER
extern void store_x0_x7(uint64_t* buffer);
#endif
/**
 * @brief AuTextOut -- standard text printing function
 * for entire kernel
 * @param text -- text to output
 */
void AuTextOut(const char* format, ...) {
	if (bypass_autextout)
		return;

#ifdef __GNUC__
	va_list args;
	va_start(args, format);
#else
	uint64_t buffer[7];
	store_x0_x7(buffer);

	va_list args = (va_list)buffer;
#endif
	/* Batch the AuPutS fragments below into one mirror submit. --axiss */
	mirror_suppress++;
	while (*format) {
		if (*format == '%') {
			++format;
			if (*format == 'd') {
				size_t width = 0;
				if (format[1] == '.') {
					for (size_t i = 2; format[i] >= '0' && format[i] <= '9'; ++i) {
						width *= 10;
						width += format[i] - '0';
					}
				}
				size_t i = va_arg(args, size_t);
				char buffer[sizeof(size_t) * 8 + 1];
				//	size_t len
				if ((int)i < 0) {
					AuPutS("-");
					i = ((int)i * -1);
					sztoa(i, buffer, 10);
				} else {
					sztoa(i, buffer, 10);
					size_t len = strlen(buffer);
				}
				/*	while (len++ < width)
				puts("0");*/
				AuPutS(buffer);
			} else if (*format == 'c') {
				int c = va_arg(args, int);
				//char buffer[sizeof(size_t) * 8 + 1];
				//sztoa(c, buffer, 10);
				//puts(buffer);
				AuPutC(c);
			} else if (*format == 'x') {
				size_t x = va_arg(args, size_t);
				char buffer[sizeof(size_t) * 8 + 1];
				sztoa(x, buffer, 16);
				//puts("0x");
				AuPutS(buffer);
			} else if (*format == 's') {
				char* x = va_arg(args, char*);
				AuPutS(x);
			} else if (*format == 'f') {
				double x = va_arg(args, double);
				AuPutS(ftoa(x, 2));
			} else if (*format == '%') {
				AuPutS(".");
			} else {
				char buf[3];
				buf[0] = '%';
				buf[1] = *format;
				buf[2] = '\0';
				AuPutS(buf);
			}
		} else {
			char buf[2];
			buf[0] = *format;
			buf[1] = '\0';
			AuPutS(buf);
		}
		++format;
	}
	mirror_suppress--;
	AuConsoleFlushMirror();
	va_end(args);
}

/**
 * @brief AuTextOut -- standard text printing function
 * for entire kernel
 * @param text -- text to output
 */
void AuTextOutpro_Call(const char* format, void* reg_save_area, void* entry_sp) {
#ifdef __GNUC__
	uint64_t* arg_ptr = (uint64_t*)((uint8_t*)reg_save_area + 8);
#define AU_VA_ARG(type) ((type)(*arg_ptr++))
#else
	va_list args = ((va_list)reg_save_area + 8);
#define AU_VA_ARG(type) va_arg(args, type)
#endif
	/* Batch the AuPutS fragments below into one mirror submit. --axiss */
	mirror_suppress++;
	while (*format) {
		if (*format == '%') {
			++format;
			if (*format == 'd') {
				size_t width = 0;
				if (format[1] == '.') {
					for (size_t i = 2; format[i] >= '0' && format[i] <= '9'; ++i) {
						width *= 10;
						width += format[i] - '0';
					}
				}
				size_t i = AU_VA_ARG(size_t);
				char buffer[sizeof(size_t) * 8 + 1];
				//	size_t len
				if ((int)i < 0) {
					AuPutS("-");
					i = ((int)i * -1);
					sztoa(i, buffer, 10);
				} else {
					sztoa(i, buffer, 10);
					size_t len = strlen(buffer);
				}
				/*	while (len++ < width)
				puts("0");*/
				AuPutS(buffer);
			} else if (*format == 'c') {
				int c = AU_VA_ARG(int);
				//char buffer[sizeof(size_t) * 8 + 1];
				//sztoa(c, buffer, 10);
				//puts(buffer);
				AuPutC(c);
			} else if (*format == 'x') {
				size_t x = AU_VA_ARG(size_t);
				char buffer[sizeof(size_t) * 8 + 1];
				sztoa(x, buffer, 16);
				//puts("0x");
				AuPutS(buffer);
			} else if (*format == 's') {
				char* x = AU_VA_ARG(char*);
				AuPutS(x);
			} else if (*format == 'f') {
				double x = AU_VA_ARG(double);
				AuPutS(ftoa(x, 2));
			} else if (*format == '%') {
				AuPutS(".");
			} else {
				char buf[3];
				buf[0] = '%';
				buf[1] = *format;
				buf[2] = '\0';
				AuPutS(buf);
			}
		} else {
			char buf[2];
			buf[0] = *format;
			buf[1] = '\0';
			AuPutS(buf);
		}
		++format;
	}
	mirror_suppress--;
	AuConsoleFlushMirror();
#ifndef __GNUC__
	va_end(args);
#endif
}

/**
 * @brief AuConsoleEarlyEnable -- enables or disable early
 * mode text output
 * @param value -- boolean value
 */
void AuConsoleEarlyEnable(bool value) {
	aucon->early_mode = value;
}

/**
 * @brief AuConsoleGetScreenWidth -- return the screen
 * width
 * @return return screen width
 * The only caller is the tablet driver: report the desktop size the
 * compositor draws at, not the firmware GOP console size, so absolute
 * pointer input spans the whole desktop at any resolution.
 * Falls back to the real console geometry while no driver backs a
 * manual override, same value SCREEN_GETWIDTH hands out. --axiss
 */
uint32_t AuConsoleGetScreenWidth() {
	if (!aucon)
		return 0;
	uint32_t width = 0, height = 0;
	aucon_desktop_size(&width, &height);
	return width;
}

/**
 * @brief AuConsoleGetScreenHeight -- return the screen
 * height
 * @return return screen height
 * See AuConsoleGetScreenWidth: desktop size for pointer mapping.
 * --axiss
 */
uint32_t AuConsoleGetScreenHeight() {
	if (!aucon)
		return 0;
	uint32_t width = 0, height = 0;
	aucon_desktop_size(&width, &height);
	return height;
}

void AuConsoleFlushFramebuffer() {
	aa64_data_cache_clean_range(aucon->buffer, aucon->size);
}

/* Boot-framebuffer geometry saved on the first repoint, so the console
 * can be moved back when no compositor will ever present the GPU backing
 * (TERM mode). Cursor position is deliberately not stashed: text continues
 * where it left off. --axiss */
static bool bootfb_saved;
static uint32_t* bootfb_phys;
static uint32_t* bootfb_virt;
static size_t bootfb_w;
static size_t bootfb_h;
static size_t bootfb_scanline;
static size_t bootfb_pitch;
static size_t bootfb_size;

void AuConsoleSetConInfo(uint64_t phys, uint64_t virtual, size_t xres, size_t yres) {
	if (aucon && !bootfb_saved) {
		bootfb_phys = __framebuffer;
		bootfb_virt = aucon->buffer;
		bootfb_w = aucon->width;
		bootfb_h = aucon->height;
		bootfb_scanline = aucon->scanline;
		bootfb_pitch = aucon->pitch;
		bootfb_size = aucon->size;
		bootfb_saved = true;
	}
	__framebuffer = (uint32_t*)phys;
	aucon->buffer = (uint32_t*)virtual;
	aucon->width = xres;
	aucon->height = yres;
	aucon->scanline = (uint16_t)xres;
	aucon->pitch = (uint32_t)(xres * 4);
	aucon->size = xres * yres * 4;
	h_res = (uint32_t)xres;
	v_res = (uint32_t)yres;
}

/**
 * @brief AuConsoleRestoreBootFb -- move the kernel console back onto the
 * boot (GOP/ramfb) framebuffer saved by the first SetConInfo repoint
 * @return 1 when restored, 0 when there is nothing to restore
 */
int AuConsoleRestoreBootFb(void) {
	if (!aucon || !bootfb_saved || !bootfb_virt)
		return 0;
	__framebuffer = bootfb_phys;
	aucon->buffer = bootfb_virt;
	aucon->width = bootfb_w;
	aucon->height = bootfb_h;
	aucon->desktop_width = bootfb_w;
	aucon->desktop_height = bootfb_h;
	aucon->scanline = (uint16_t)bootfb_scanline;
	aucon->pitch = (uint32_t)bootfb_pitch;
	aucon->size = bootfb_size;
	h_res = (uint32_t)bootfb_w;
	v_res = (uint32_t)bootfb_h;
	UARTDebugOut("[aucon]: console restored to boot framebuffer\r\n");
	/* Paint everything written so far in one go, so the mirror does not
	 * start from a black scanout. Drop any pending dirty range first:
	 * the full repaint supersedes it. --axiss */
	mirror_dirty_valid = false;
	if (present_hook && !display_owned)
		present_hook(aucon->buffer, aucon->pitch, 0, 0,
			(int)aucon->width, (int)aucon->height);
	return 1;
}

void AuConsoleBypassAuTextOut() {
	bypass_autextout = 1;
}
