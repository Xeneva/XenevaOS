#ifndef __PAINT_TOOLS_H__
#define __PAINT_TOOLS_H__

#include <stdint.h>
#include <stdbool.h>
#include "paint_types.h"

void PaintSetPixel(uint32_t* buffer, int bufW, int bufH, int x, int y, uint32_t color);
uint32_t PaintGetPixel(const uint32_t* buffer, int bufW, int bufH, int x, int y);

void PaintStampDisc(uint32_t* buffer, int bufW, int bufH, int cx, int cy, int radius, uint32_t color);
void PaintStampSquare(uint32_t* buffer, int bufW, int bufH, int cx, int cy, int size, uint32_t color);

void PaintEraseLine(uint32_t* buffer, int bufW, int bufH,
                    int x0, int y0, int x1, int y1,
                    int size, uint32_t color);

void PaintDrawBresenhamLine(uint32_t* buffer, int bufW, int bufH,
                            int x0, int y0, int x1, int y1,
                            int strokeWidth, uint32_t color);

void PaintDrawRectOutline(uint32_t* buffer, int bufW, int bufH,
                          int x0, int y0, int x1, int y1,
                          int strokeWidth, uint32_t color);

void PaintDrawRectFilled(uint32_t* buffer, int bufW, int bufH,
                         int x0, int y0, int x1, int y1,
                         uint32_t fillColor, uint32_t outlineColor, int strokeWidth);

void PaintDrawMidpointCircle(uint32_t* buffer, int bufW, int bufH,
                             int x0, int y0, int x1, int y1,
                             int strokeWidth, uint32_t color, bool lockAspect);

void PaintDrawFilledCircle(uint32_t* buffer, int bufW, int bufH,
                           int x0, int y0, int x1, int y1,
                           uint32_t fillColor, uint32_t outlineColor,
                           int strokeWidth, bool lockAspect);

void PaintScanlineFloodFill(uint32_t* buffer, int bufW, int bufH,
                            int startX, int startY, uint32_t fillColor);

void PaintInvertColors(uint32_t* buffer, int bufW, int bufH);

void PaintClearCanvas(uint32_t* buffer, int bufW, int bufH, uint32_t clearColor);

#endif
