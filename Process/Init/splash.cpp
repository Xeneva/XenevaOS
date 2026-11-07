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

#include <stdint.h>
#include <stdio.h>
#include <_xeneva.h>
#include <sys/_keproc.h>
#include <string.h>
#include <sys/_heap.h>
#include <sys/mman.h>
#include <sys/_kefile.h>
#include <stdlib.h>
#include <sys/iocodes.h>

static int screen_w;
static int screen_h;
static uint32_t* fb;

#pragma pack(push, 1)
typedef struct _bmp_ {
	unsigned short type;
	unsigned int size;
	unsigned short resv1;
	unsigned short resv2;
	unsigned int off_bits;
} BMP;

typedef struct _info_ {
	unsigned int biSize;
	long biWidth;
	long biHeight;
	unsigned short biPlanes;
	unsigned short biBitCount;
	unsigned int biCompression;
	unsigned int biSizeImage;
	long biXPelsPerMeter;
	long biYPelsPerMeter;
	unsigned int biClrUsed;
	unsigned int biClrImportant;
} BMPInfo;
#pragma pack(pop)

void _fb_draw_pixel(int x, int y, uint32_t color) {
	unsigned int* lfb = fb;
	lfb[static_cast<uint64_t>(y) * screen_w + x] = color;
}

void _draw_bitmap_(int x, int y, int w, int h, uint8_t* imageData) {
	uint32_t width = w;
	uint32_t height = h;
	uint32_t imageHeight = height;
	uint32_t j = 0;

	uint8_t* image = imageData;
	for (int i = 0; i < height; i++) {
		char* image_row = (char*)image + (static_cast<int64_t>(height) - i - 1) *
											 (static_cast<int64_t>(width) * 4);
		uint32_t h = height - 1 - i;
		j = 0;
		for (int k = 0; k < width; k++) {
			uint32_t b = image_row[j++] & 0xff;
			uint32_t g = image_row[j++] & 0xff;
			uint32_t r = image_row[j++] & 0xff;
			uint32_t a = image_row[j++] & 0xff;
			uint32_t rgb = ((a << 24) | (r << 16) | (g << 8) | (b));
			if (rgb & 0xFF000000)
				_fb_draw_pixel(x + k, y + i, rgb);
		}
	}
}

void _fill_screen(unsigned x, unsigned y, unsigned w, unsigned h, uint32_t col) {
	for (int i = 0; i < w; i++)
		for (int j = 0; j < h; j++)
			_fb_draw_pixel(x + i, y + j, col);
}

void SplashScreenShow() {
	int graphFd = _KeOpenFile("/dev/graph", FILE_OPEN_WRITE);
	_KePrint("graphFD : %d \r\n", graphFd);
	if (graphFd == -1)
		return;
	XEFileIOControl ioctl;
	memset(&ioctl, 0, sizeof(XEFileIOControl));
	ioctl.syscall_magic = AURORA_SYSCALL_MAGIC;
	int ret = 0;

	ret = _KeFileIoControl(graphFd, SCREEN_GETWIDTH, &ioctl);
	screen_w = ioctl.uint_1;
	ret = _KeFileIoControl(graphFd, SCREEN_GETHEIGHT, &ioctl);
	screen_h = ioctl.uint_1;

	ret = _KeFileIoControl(graphFd, SCREEN_GET_FB, &ioctl);
	fb = (uint32_t*)ioctl.ulong_1;

	_fill_screen(0, 0, screen_w, screen_h, 0xFF0F0F0F);

	int logo = _KeOpenFile("/xelogo.bmp", FILE_OPEN_READ_ONLY);
	if (logo == -1)
		return;

	XEFileStatus stat;
	memset(&stat, 0, sizeof(stat));
	if (_KeFileStat(logo, &stat) == -1 || stat.size < sizeof(BMP) + sizeof(BMPInfo))
		return;

	uint8_t* buffer = (uint8_t*)_KeMemMap(NULL, stat.size, 0, 0, -1, 0);
	if (!buffer)
		return;

	_KeReadFile(logo, buffer, stat.size);

	BMP* bmp = (BMP*)buffer;
	unsigned int offset = bmp->off_bits;

	BMPInfo* info = (BMPInfo*)(buffer + sizeof(BMP));
	int width = info->biWidth;
	int height = info->biHeight;
	int bpp = info->biBitCount;

	if (offset >= stat.size)
		return;

	void* image_bytes = (void*)(buffer + offset);
	_draw_bitmap_(
		screen_w / 2 - width / 2, screen_h / 2 - height / 2, width, height, (uint8_t*)image_bytes);

	/* Push the painted splash to the scanout mirror, if one is registered
	 * (with none, the boot framebuffer is scanned directly and this is a
	 * no-op). Zero width/height requests the whole screen. --axiss */
	memset(&ioctl, 0, sizeof(ioctl));
	ioctl.syscall_magic = AURORA_SYSCALL_MAGIC;
	_KeFileIoControl(graphFd, SCREEN_UPDATE, &ioctl);
}
