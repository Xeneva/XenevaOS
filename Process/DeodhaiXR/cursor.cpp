/**
* BSD 2 - Clause License
*
*Copyright(c) 2022 - 2023, Manas Kamal Choudhury
* All rights reserved.
*
* Redistributionand use in sourceand binary forms, with or without
* modification, are permitted provided that the following conditions are met :
*
*1. Redistributions of source code must retain the above copyright notice, this
* list of conditionsand the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright notice,
* this list of conditionsand the following disclaimer in the documentation
*and /or other materials provided with the distribution.
*
*THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
**/

#include "cursor.h"
#include <string.h>
#include <stdlib.h>
#include <sys/_kefile.h>
#include <sys/mman.h>
#include "deodxr.h"

int cur_w;
int cur_h;
unsigned char* imageData;
/*
 * CursorOpen -- open a cursor from file
 * @param path -- path of the cursor
 * @param type -- cursor type
 */
Cursor* CursorOpen(char* path, uint8_t type) {
	Cursor* cur = (Cursor*)malloc(sizeof(Cursor));
	memset(cur, 0, sizeof(Cursor));

	int fd = _KeOpenFile(path, FILE_OPEN_READ_ONLY);
	XEFileStatus stat;
	_KeFileStat(fd, &stat);

	cur->fileBuffer = (uint8_t*)_KeMemMap(NULL, stat.size, 0, 0, MEMMAP_NO_FILEDESC, 0);
	cur->type = type;
	cur->cursorFD = fd;
	cur->cursorFileSize = stat.size;
	return cur;
}

/* CursorRead -- read the cursor file
 * @param cur -- Pointer to cursor file
 */
void CursorRead(Cursor* cur) {
	if (!cur)
		return;
	_KeReadFile(cur->cursorFD, cur->fileBuffer, cur->cursorFileSize);

	uint8_t* buffer = (uint8_t*)cur->fileBuffer;

	//BMP* bmp = (BMP*)buffer;
	unsigned int offset; // = bmp->off_bits;
	memcpy(&offset, (uint8_t*)buffer + 10, sizeof(int));

	uint8_t* info = (uint8_t*)(buffer + sizeof(BMP));
	int width; // = info->biWidth;
	memcpy(&width, (uint8_t*)info + 4, sizeof(int));
	int height; // = info->biHeight;
	memcpy(&height, (uint8_t*)info + 8, sizeof(int));
	int bpp; // = info->biBitCount;
	memcpy(&bpp, (uint8_t*)info + 14, sizeof(unsigned short));

	void* image_bytes = (void*)(buffer + offset);
	cur->imageData = (uint8_t*)image_bytes;
	cur->width = width;
	cur->height = height;
	cur->bpp = bpp;
	cur_w = cur->width;
	cur_h = cur->height;
	cur->cursorBack = (uint32_t*)malloc(cur->width * cur->height * 32);
	//imageData = cur->imageData;
}

/*
 * CursorDraw -- draw the cursor to canvas
 * @param canv -- Pointer to canvas
 * @param cur - Pointer to cursor
 * @param x -- X position
 * @param y -- Y position
 */
void CursorDraw(ChCanvas* canv, Cursor* cur, unsigned int x, unsigned int y) {
	uint32_t width = cur_w;
	uint32_t height = cur_h;
	uint32_t j = 0;
	uint8_t* image = cur->imageData;
	for (int i = 0; i < height; i++) {
		unsigned char* image_row =
			(unsigned char*)(image + (static_cast<uint64_t>(height) - i - 1) *
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