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

#include <aucon.h>
#include <_null.h>
#include <Mm/vmmngr.h>
#include <Mm/pmmngr.h>
#include <Mm/kmalloc.h>
#include <string.h>
#include <va_list.h>
#include <stdio.h>
#include <Fs/vfs.h>
#include <Serv/sysserv.h>
#include <Fs/Dev/devfs.h>

uint8_t *font_data;
uint32_t console_x;
uint32_t console_y;
uint32_t *__framebuffer;
AuConsole *aucon;
bool early_;

#define CONSOLE_BACKGROUND 0x00000000
#define CONSOLE_FOREGROUND 0xFFFFFFFF
void(*_print_func) (const char* text, ...);

#pragma pack (push, 1)
typedef struct {
	uint32_t magic;
	uint32_t version;
	uint32_t headersize;
	uint32_t flags;
	uint32_t numglyph;
	uint32_t bytesperglyph;
	uint32_t height;
	uint32_t width;
	uint8_t glyphs;
}psf2_t;
#pragma pack (pop)



/*
 * AuConsoleInitialize -- initialize kernel direct screen
 * console
 * @param info -- Pointer to kernel boot info structure
 */
void AuConsoleInitialize(PKERNEL_BOOT_INFO info, bool early) {
	if (early) {
		_print_func = info->printf_gui;
		early_ = early;
	}
	aucon = NULL;
}

/* A manual loader desktop override names a size the firmware GOP never
 * scans out. Until a driver backs it the console framebuffer is all
 * there is, and reporting the larger size let init's splash paint past
 * the framebuffer mapping. Report the real console size while the
 * override is bigger than what can be drawn. --axiss */
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

/*
 * AuConsoleIoControl -- io control of console file
 */
int AuConsoleIoControl(AuVFSNode* node, int code, void* args) {
	int ret = 0;
	AuFileIOControl *ioctl = (AuFileIOControl*)args;
	/*if (ioctl->syscall_magic != AURORA_SYSCALL_MAGIC)
		return 0;*/

	if (!aucon)
		return 0;
	
	switch (code) {
	case SCREEN_GETWIDTH:{
							 uint32_t width = 0, height = 0;
							 aucon_desktop_size(&width, &height);
							 ioctl->uint_1 = width;
							 break;
	}
	case SCREEN_GETHEIGHT:{
							  uint32_t width = 0, height = 0;
							  aucon_desktop_size(&width, &height);
							  ioctl->uint_1 = height;
							  break;
	}
	case SCREEN_GETBPP:{
						   uint32_t bpp = aucon->bpp;
						   ioctl->uint_1 = bpp;
						   break;
	}
	case SCREEN_GET_SCANLINE: {
								  uint16_t scanline = aucon->scanline;
								  ioctl->ushort_1 = scanline;
								  break;
	}
	case SCREEN_GET_PITCH:{
							  uint32_t pitch = aucon->pitch;
							  ioctl->uint_1 = pitch;
							  break;
	}
	case SCREEN_GET_FB:{
						   uint64_t buffaddr = (uint64_t)aucon->buffer;
						   ioctl->ulong_1 = buffaddr;
						   break;
	}

	case SCREEN_REG_MNGR:{
							 return 1;
							 break;
	}
	}
	return ret;
}

/* 
 * AuConsolePostInitialise -- initialise the post console process
 * @param info -- pointer to kernel boot info structure
 */
void AuConsolePostInitialise(PKERNEL_BOOT_INFO info) {

	uint8_t* font_ = (uint8_t*)P2V((uint64_t)AuPmmngrAlloc());
	memset(font_, 0, 4096);
	memcpy(font_, info->psf_font_data, 4096);
	font_data = font_;

	aucon = (AuConsole*)kmalloc(sizeof(AuConsole));
	memset(aucon, 0, sizeof(AuConsole));

	for (int i = 0; i < info->fb_size / PAGE_SIZE; i++)
		AuMapPage((uint64_t)info->graphics_framebuffer + static_cast<uint64_t>(i) * PAGE_SIZE, 
			0xFFFFD00000200000 + static_cast<uint64_t>(i) * 4096, X86_64_PAGING_USER);
	early_ = false;
	aucon->buffer = (uint32_t*)0xFFFFD00000200000;
	aucon->width = info->X_Resolution;
	aucon->height = info->Y_Resolution;
	/* Desktop size for the compositor (manual loader override if present). --axiss */
	aucon->desktop_width = info->X_Resolution;
	aucon->desktop_height = info->Y_Resolution;
	if (info->DesktopOverrideWidth >= 640 && info->DesktopOverrideWidth <= 4096 &&
		info->DesktopOverrideHeight >= 480 && info->DesktopOverrideHeight <= 4096) {
		aucon->desktop_width = info->DesktopOverrideWidth;
		aucon->desktop_height = info->DesktopOverrideHeight;
	}
	aucon->bpp = 32;
	aucon->scanline = info->pixels_per_line;
	aucon->pitch = 4 * info->pixels_per_line;
	aucon->size = info->fb_size;
	aucon->early_mode = false;
	console_x = console_y = 0;

	for (int w = 0; w < info->X_Resolution; w++) {
		for (int h = 0; h < info->Y_Resolution; h++) {
			aucon->buffer[w + h * info->X_Resolution] = CONSOLE_BACKGROUND;
		}
	}

	AuVFSNode* fsys = AuVFSFind("/dev");
	AuVFSNode* file = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(file, 0, sizeof(AuVFSNode));
	strcpy(file->filename, "graph");
	file->flags = FS_FLAG_DEVICE;
	file->device = fsys;
	file->read = 0;
	file->write = 0;
	file->iocontrol = AuConsoleIoControl;
	AuDevFSAddFile(fsys, "/", file);
}

//! Put a character to console output
void AuPutC(char c) {
	if (font_data == NULL)
		return;

	psf2_t *font = (psf2_t*)font_data;
	int x, y, kx = 0, line, mask, offs;
	int bpl = (font->width + 7) / 8;


	unsigned char *glyph = (unsigned char*)font_data + font->headersize +
		(c>0 && c<font->numglyph ? c : 0)*font->bytesperglyph;
	offs = kx * (font->width + 1);// * 4);
	for (y = 0; y<font->height; y++) {
		line = offs; mask = 1 << (font->width - 1);
		for (x = 0; x<font->width; x++) {
			aucon->buffer[line + console_x + console_y * aucon->width] = ((int)*glyph) & (mask) ? CONSOLE_FOREGROUND :
				0;
			mask >>= 1; line += 1;
		}
		aucon->buffer[line + console_x + console_y * aucon->width] = 0; glyph += bpl; offs += aucon->scanline;
	}
	console_x += font->width + 1;
}



//! Prints string to console output
void AuPutS(char *s){
	if (font_data == NULL)
		return;

	psf2_t *font = (psf2_t*)font_data;
	int x, y, line, mask, offs;
	int bpl = (font->width + 7) / 8;
	while (*s) {
		if (*s == '\n') {

			console_y += 16;
			console_x = 0;
			//!Scroll
			/*if (console_y >= aucon->height) {
				for (int line_y = 0; line_y < aucon->height - 16; line_y++) {
					for (int code_x = 0; code_x < aucon->width - 8; code_x++) {
						aucon->buffer[line_y * aucon->width + code_x] = aucon->buffer[(line_y + 16) * aucon->width + code_x];
					}
				}
				console_y -= 16;
			}*/

		}
		else if (*s == '\b') {
			if (console_x > 0) {
				console_x--;
			}
		}
		else {
			unsigned char *glyph = (unsigned char*)font_data + font->headersize +
				(*s>0 && *s<font->numglyph ? *s : 0)*font->bytesperglyph;
			offs = console_x * (font->width + 1);// * 4);
			for (y = 0; y<font->height; y++) {
				line = offs; mask = 1 << (font->width - 1);
				for (x = 0; x<font->width; x++) {
					aucon->buffer[line + console_y * aucon->width] = ((int)*glyph) & (mask) ? CONSOLE_FOREGROUND:
						CONSOLE_BACKGROUND;
					mask >>= 1; line += 1;
				}
				aucon->buffer[line + console_y * aucon->width] = 0; glyph += bpl; offs += aucon->scanline;
			}
			console_x++;
		}
		s++;
	}


}

/*
 * AuTextOut -- standard text printing function
 * for entire kernel
 * @param text -- text to output
 */
void AuTextOut(const char* format, ...) {

	if (early_) {
		_print_func(format);
		return;
	}


	_va_list_ args;
	va_start(args, format);

	while (*format)
	{
		if (*format == '%')
		{
			++format;
			if (*format == 'd')
			{
				size_t width = 0;
				if (format[1] == '.')
				{
					for (size_t i = 2; format[i] >= '0' && format[i] <= '9'; ++i)
					{
						width *= 10;
						width += format[i] - '0';
					}
				}
				size_t i = va_arg(args, size_t);
				char buffer[sizeof(size_t)* 8 + 1];
				//	size_t len
				if ((int)i < 0) {
					AuPutS("-");
					i = ((int)i * -1);
					sztoa(i, buffer, 10);
				}
				else {
					sztoa(i, buffer, 10);
					size_t len = strlen(buffer);
				}
				/*	while (len++ < width)
				puts("0");*/
				AuPutS(buffer);
			}
			else if (*format == 'c')
			{
				char c = (char)va_arg(args, int);
				//char buffer[sizeof(size_t) * 8 + 1];
				//sztoa(c, buffer, 10);
				//puts(buffer);
				AuPutC(c);
			}
			else if (*format == 'x')
			{
				size_t x = va_arg(args, size_t);
				char buffer[sizeof(size_t)* 8 + 1];
				sztoa(x, buffer, 16);
				//puts("0x");
				AuPutS(buffer);
			}
			else if (*format == 's')
			{
				char* x = va_arg(args, char*);
				AuPutS(x);
			}
			else if (*format == 'f')
			{
				double x = va_arg(args, double);
				AuPutS(ftoa(x, 2));
			}
			else if (*format == '%')
			{
				AuPutS(".");
			}
			else
			{
				char buf[3];
				buf[0] = '%'; buf[1] = *format; buf[2] = '\0';
				AuPutS(buf);
			}
		}
		else
		{
			char buf[2];
			buf[0] = *format; buf[1] = '\0';
			AuPutS(buf);
		}
		++format;
	}
	va_end(args);
}

/*
 * AuConsoleEarlyEnable -- enables or disable early
 * mode text output 
 * @param value -- boolean value
 */
void AuConsoleEarlyEnable(bool value) {
	aucon->early_mode = value;
}
