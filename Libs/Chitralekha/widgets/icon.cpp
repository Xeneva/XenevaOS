/**
* @file icon.cpp
* 
* BSD 2-Clause License
*
* Copyright (c) 2022, Manas Kamal Choudhury
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

#include "icon.h"
#include <_xeneva.h>
#include <stdlib.h>
#include <sys/_kefile.h>
#include <sys/mman.h>
#include "../bmp.h"

/**
 * @brief ChCreateIcon -- create a blank icon slot
 * @return icon slot
 */
XE_EXTERN XE_LIB ChIcon* ChCreateIcon() {
	ChIcon* ico = (ChIcon*)malloc(sizeof(ChIcon));
	memset(ico, 0, sizeof(ChIcon));
	ico->image.fd = -1;
	return ico;
}

/**
 * @brief ChIconOpen -- open an icon file
 * @param ico -- pointer to icon file
 * @param filename -- icon file's path
 */
XE_EXTERN XE_LIB void ChIconOpen(ChIcon* ico, char* filename) {
	int fd = _KeOpenFile(filename, FILE_OPEN_READ_ONLY);

	if (fd == -1)
		return;

	XEFileStatus stat;
	_KeFileStat(fd, &stat);

	ico->image.image = (uint8_t*)_KeMemMap(NULL, stat.size, 0, 0, MEMMAP_NO_FILEDESC, 0);
	ico->format = CHITRALEKHA_ICON_BMP;
	ico->image.fd = fd;
	ico->image.filesz = stat.size;
}

/**
 * @brief ChIconRead -- read an icon file
 * @param ico -- pointer to icon structure
 */
XE_EXTERN XE_LIB void ChIconRead(ChIcon* ico) {
	if (ico->image.fd == -1)
		return;
	if (!ico->image.image)
		return;
	_KeReadFile(ico->image.fd, ico->image.image, ico->image.filesz);
	uint8_t* buffer = (uint8_t*)ico->image.image;

	BMP* bmp = (BMP*)buffer;
	unsigned int offset = bmp->off_bits;

	BMPInfo* info = (BMPInfo*)(buffer + sizeof(BMP));
	int width = info->biWidth;
	int height = info->biHeight;
	int bpp = info->biBitCount;

	void* image_bytes = (void*)(buffer + offset);
	ico->image.data = (uint8_t*)image_bytes;
	ico->image.width = width;
	ico->image.height = height;
}

/**
* @brief ChDrawIcon -- draws an icon to canvas
* @param canv -- Pointer to canvas
* @param ico -- pointer to icon file
* @param x -- X coord
* @param y -- Y coord
*/
XE_EXTERN XE_LIB void ChDrawIcon(ChCanvas* canv, ChIcon* ico, int x, int y) {
	if (!ico)
		return;

	if (ico->image.fd == -1)
		return;

	uint32_t width = ico->image.width;
	uint32_t height = ico->image.height;
	uint32_t j = 0;

	uint8_t* image = ico->image.data;
	for (int i = 0; i < height; i++) {
		char* image_row = ((char*)image + (static_cast<uint64_t>(height) - i - 1) *
											  (static_cast<uint64_t>(width) * 4));
		uint32_t h = height - 1 - i;
		j = 0;
		for (int k = 0; k < width; k++) {
			uint32_t b = image_row[j++] & 0xff;
			uint32_t g = image_row[j++] & 0xff;
			uint32_t r = image_row[j++] & 0xff;
			uint32_t a = image_row[j++] & 0xff;
			uint32_t rgb = ((a << 24) | (r << 16) | (g << 8) | (b));
			if (rgb & 0xFF000000)
				ChDrawPixel(canv, x + k, y + i, rgb);
		}
	}
}

/**
* @brief ChDrawIconClipped -- draws an icon to canvas within clipped boundary
* @param canv -- Pointer to canvas
* @param ico -- pointer to icon file
* @param x -- X coord
* @param y -- Y coord
* @param limit -- Pointer to limit rect
*/
XE_EXTERN XE_LIB void ChDrawIconClipped(ChCanvas* canv, ChIcon* ico, int x, int y, ChRect* limit) {
	if (!ico)
		return;

	if (ico->image.fd == -1)
		return;

	uint32_t width = ico->image.width;
	uint32_t height = ico->image.height;
	uint32_t imageHeight = height;
	uint32_t j = 0;

	if (x > (limit->x + limit->w))
		return;

	if (y > (limit->y + limit->h))
		return;

	if (y <= limit->y) {
		int diff = limit->y - y;
		y = limit->y;
		height -= diff;
		imageHeight -= diff;
	}

	if (x <= limit->x)
		x = limit->x;

	if ((y + height) <= limit->y)
		return;

	if ((x + width) <= limit->x)
		return;

	if ((x + width) > (limit->x + limit->w))
		width = (limit->x + limit->w) - x;

	if ((y + height) > (limit->y + limit->h))
		height = (limit->y + limit->h) - y;

	uint8_t* image = ico->image.data;
	for (int i = 0; i < height; i++) {
		char* image_row = (char*)image + (static_cast<uint64_t>(imageHeight) - i - 1) *
											 (static_cast<uint64_t>(width) * 4);
		uint32_t h = imageHeight - 1 - i;
		j = 0;
		for (int k = 0; k < width; k++) {
			uint32_t b = image_row[j++] & 0xff;
			uint32_t g = image_row[j++] & 0xff;
			uint32_t r = image_row[j++] & 0xff;
			uint32_t a = image_row[j++] & 0xff;
			uint32_t rgb = ((a << 24) | (r << 16) | (g << 8) | b);
			if (rgb & 0xFF000000)
				ChDrawPixel(canv, x + k, y + i, rgb);
		}
	}
}

ChIcon* warning;
ChIcon* success;
ChIcon* exclaim;
ChIcon* information;

/**
 * @brief ChInitialiseDefaultIcons -- initialise all chitralekha
 * default icons
 */
void ChInitialiseDefaultIcons() {
#ifdef LOAD_DEFAULT_ICONS
	success = ChCreateIcon();
	information = ChCreateIcon();
	ChIconOpen(success, "/icons/succi.bmp");
	ChIconOpen(information, "/icons/infoi.bmp");
	ChIconRead(success);
	ChIconRead(information);
#endif
}

/**
 * @brief ChIconGetSystemIcon -- returns all system 
 * icons
 * @param iconnum -- Icon code number
 */
ChIcon* ChIconGetSystemIcon(uint8_t iconnum) {
	switch (iconnum) {
	case CHITRALEKHA_ICON_WARNING:
		return 0;
	case CHITRALEKHA_ICON_SUCCESS:
		return success;
	case CHITRALEKHA_ICON_EXCLAIMATION:
		return 0;
	case CHITRALEKHA_ICON_INFORMATION:
		return information;
	default:
		return NULL;
	}
}
