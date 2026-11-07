#ifndef __PAINT_FS_H__
#define __PAINT_FS_H__

#include <stdint.h>
#include <stdbool.h>

bool PaintSaveBMP(const char* filepath, const uint32_t* buffer, int width, int height);
bool PaintLoadBMP(const char* filepath, uint32_t* buffer, int canvasW, int canvasH);

#endif
