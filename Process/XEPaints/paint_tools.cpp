#include "paint_tools.h"
#include <string.h>

void PaintSetPixel(uint32_t* buffer, int bufW, int bufH, int x, int y, uint32_t color) {
    if (x >= 0 && x < bufW && y >= 0 && y < bufH) {
        buffer[y * bufW + x] = color;
    }
}

uint32_t PaintGetPixel(const uint32_t* buffer, int bufW, int bufH, int x, int y) {
    if (x >= 0 && x < bufW && y >= 0 && y < bufH) {
        return buffer[y * bufW + x];
    }
    return 0;
}

void PaintStampDisc(uint32_t* buffer, int bufW, int bufH, int cx, int cy, int radius, uint32_t color) {
    if (radius <= 0) {
        PaintSetPixel(buffer, bufW, bufH, cx, cy, color);
        return;
    }

    int r2 = radius * radius;
    int yMin = (cy - radius < 0) ? 0 : cy - radius;
    int yMax = (cy + radius >= bufH) ? bufH - 1 : cy + radius;

    for (int y = yMin; y <= yMax; ++y) {
        int dy = y - cy;
        int dy2 = dy * dy;
        int dxMax = 0;
        while ((dxMax + 1) * (dxMax + 1) + dy2 <= r2) {
            dxMax++;
        }

        int xMin = (cx - dxMax < 0) ? 0 : cx - dxMax;
        int xMax = (cx + dxMax >= bufW) ? bufW - 1 : cx + dxMax;
        uint32_t* row = buffer + (y * bufW);
        for (int x = xMin; x <= xMax; ++x) {
            row[x] = color;
        }
    }
}

void PaintStampSquare(uint32_t* buffer, int bufW, int bufH, int cx, int cy, int size, uint32_t color) {
    if (size <= 0) {
        PaintSetPixel(buffer, bufW, bufH, cx, cy, color);
        return;
    }

    int half = size / 2;
    int minX = (cx - half < 0) ? 0 : cx - half;
    int maxX = (cx + half >= bufW) ? bufW - 1 : cx + half;
    int minY = (cy - half < 0) ? 0 : cy - half;
    int maxY = (cy + half >= bufH) ? bufH - 1 : cy + half;

    for (int y = minY; y <= maxY; ++y) {
        uint32_t* row = buffer + (y * bufW);
        for (int x = minX; x <= maxX; ++x) {
            row[x] = color;
        }
    }
}

void PaintEraseLine(uint32_t* buffer, int bufW, int bufH,
                    int x0, int y0, int x1, int y1,
                    int size, uint32_t color) {
    int dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 >= y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        PaintStampSquare(buffer, bufW, bufH, x0, y0, size, color);
        if (x0 == x1 && y0 == y1) break;

        int e2 = err * 2;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void PaintDrawBresenhamLine(uint32_t* buffer, int bufW, int bufH,
                            int x0, int y0, int x1, int y1,
                            int strokeWidth, uint32_t color) {
    int radius = (strokeWidth > 1) ? (strokeWidth / 2) : 0;
    int dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 >= y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        if (radius > 0) {
            PaintStampDisc(buffer, bufW, bufH, x0, y0, radius, color);
        } else {
            PaintSetPixel(buffer, bufW, bufH, x0, y0, color);
        }

        if (x0 == x1 && y0 == y1) break;

        int e2 = err * 2;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void PaintDrawRectOutline(uint32_t* buffer, int bufW, int bufH,
                          int x0, int y0, int x1, int y1,
                          int strokeWidth, uint32_t color) {
    int minX = (x0 < x1) ? x0 : x1;
    int maxX = (x0 < x1) ? x1 : x0;
    int minY = (y0 < y1) ? y0 : y1;
    int maxY = (y0 < y1) ? y1 : y0;

    int halfW = (strokeWidth > 1) ? (strokeWidth / 2) : 0;

    // Top and bottom edges
    for (int x = minX; x <= maxX; ++x) {
        if (halfW > 0) {
            PaintStampDisc(buffer, bufW, bufH, x, minY, halfW, color);
            PaintStampDisc(buffer, bufW, bufH, x, maxY, halfW, color);
        } else {
            PaintSetPixel(buffer, bufW, bufH, x, minY, color);
            PaintSetPixel(buffer, bufW, bufH, x, maxY, color);
        }
    }

    // Left and right edges
    for (int y = minY; y <= maxY; ++y) {
        if (halfW > 0) {
            PaintStampDisc(buffer, bufW, bufH, minX, y, halfW, color);
            PaintStampDisc(buffer, bufW, bufH, maxX, y, halfW, color);
        } else {
            PaintSetPixel(buffer, bufW, bufH, minX, y, color);
            PaintSetPixel(buffer, bufW, bufH, maxX, y, color);
        }
    }
}

void PaintDrawRectFilled(uint32_t* buffer, int bufW, int bufH,
                         int x0, int y0, int x1, int y1,
                         uint32_t fillColor, uint32_t outlineColor, int strokeWidth) {
    int minX = (x0 < x1) ? x0 : x1;
    int maxX = (x0 < x1) ? x1 : x0;
    int minY = (y0 < y1) ? y0 : y1;
    int maxY = (y0 < y1) ? y1 : y0;

    // Fill interior
    int clipMinX = (minX < 0) ? 0 : minX;
    int clipMaxX = (maxX >= bufW) ? bufW - 1 : maxX;
    int clipMinY = (minY < 0) ? 0 : minY;
    int clipMaxY = (maxY >= bufH) ? bufH - 1 : maxY;

    for (int y = clipMinY; y <= clipMaxY; ++y) {
        uint32_t* row = buffer + (y * bufW);
        for (int x = clipMinX; x <= clipMaxX; ++x) {
            row[x] = fillColor;
        }
    }

    // Outline perimeter if strokeWidth > 0
    if (strokeWidth > 0) {
        PaintDrawRectOutline(buffer, bufW, bufH, minX, minY, maxX, maxY, strokeWidth, outlineColor);
    }
}

static void _PlotEllipsePoints(uint32_t* buffer, int bufW, int bufH,
                               int cx, int cy, int x, int y,
                               int halfW, uint32_t color) {
    if (halfW > 0) {
        PaintStampDisc(buffer, bufW, bufH, cx + x, cy + y, halfW, color);
        PaintStampDisc(buffer, bufW, bufH, cx - x, cy + y, halfW, color);
        PaintStampDisc(buffer, bufW, bufH, cx + x, cy - y, halfW, color);
        PaintStampDisc(buffer, bufW, bufH, cx - x, cy - y, halfW, color);
    } else {
        PaintSetPixel(buffer, bufW, bufH, cx + x, cy + y, color);
        PaintSetPixel(buffer, bufW, bufH, cx - x, cy + y, color);
        PaintSetPixel(buffer, bufW, bufH, cx + x, cy - y, color);
        PaintSetPixel(buffer, bufW, bufH, cx - x, cy - y, color);
    }
}

void PaintDrawMidpointCircle(uint32_t* buffer, int bufW, int bufH,
                             int x0, int y0, int x1, int y1,
                             int strokeWidth, uint32_t color, bool lockAspect) {
    int dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 >= y0) ? (y1 - y0) : (y0 - y1);

    int rx = dx / 2;
    int ry = dy / 2;

    if (lockAspect) {
        int r = (rx > ry) ? rx : ry;
        rx = r;
        ry = r;
    }

    if (rx <= 0 && ry <= 0) {
        PaintSetPixel(buffer, bufW, bufH, (x0 + x1) / 2, (y0 + y1) / 2, color);
        return;
    }
    if (rx <= 0) rx = 1;
    if (ry <= 0) ry = 1;

    int cx = (x0 + x1) / 2;
    int cy = (y0 + y1) / 2;
    int halfW = (strokeWidth > 1) ? (strokeWidth / 2) : 0;

    int64_t rx2 = (int64_t)rx * rx;
    int64_t ry2 = (int64_t)ry * ry;
    int64_t twoRx2 = 2 * rx2;
    int64_t twoRy2 = 2 * ry2;

    // Region 1
    int64_t x = 0;
    int64_t y = ry;
    int64_t p = (int64_t)(ry2 - (rx2 * ry) + (0.25 * rx2));
    int64_t px = 0;
    int64_t py = twoRx2 * y;

    _PlotEllipsePoints(buffer, bufW, bufH, cx, cy, (int)x, (int)y, halfW, color);

    while (px < py) {
        x++;
        px += twoRy2;
        if (p < 0) {
            p += ry2 + px;
        } else {
            y--;
            py -= twoRx2;
            p += ry2 + px - py;
        }
        _PlotEllipsePoints(buffer, bufW, bufH, cx, cy, (int)x, (int)y, halfW, color);
    }

    // Region 2
    p = (int64_t)(ry2 * (x * 2 + 1) * (x * 2 + 1) / 4 + rx2 * (y - 1) * (y - 1) - rx2 * ry2);
    while (y > 0) {
        y--;
        py -= twoRx2;
        if (p > 0) {
            p += rx2 - py;
        } else {
            x++;
            px += twoRy2;
            p += rx2 - py + px;
        }
        _PlotEllipsePoints(buffer, bufW, bufH, cx, cy, (int)x, (int)y, halfW, color);
    }
}

static void _FillEllipseScanlines(uint32_t* buffer, int bufW, int bufH,
                                  int cx, int cy, int x, int y, uint32_t fillColor) {
    int yTop = cy - y;
    int yBottom = cy + y;

    int xMin = cx - x;
    int xMax = cx + x;
    if (xMin < 0) xMin = 0;
    if (xMax >= bufW) xMax = bufW - 1;

    if (yTop >= 0 && yTop < bufH && xMin <= xMax) {
        uint32_t* row = buffer + (yTop * bufW);
        for (int k = xMin; k <= xMax; ++k) {
            row[k] = fillColor;
        }
    }

    if (yBottom >= 0 && yBottom < bufH && yBottom != yTop && xMin <= xMax) {
        uint32_t* row = buffer + (yBottom * bufW);
        for (int k = xMin; k <= xMax; ++k) {
            row[k] = fillColor;
        }
    }
}

void PaintDrawFilledCircle(uint32_t* buffer, int bufW, int bufH,
                           int x0, int y0, int x1, int y1,
                           uint32_t fillColor, uint32_t outlineColor,
                           int strokeWidth, bool lockAspect) {
    int dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 >= y0) ? (y1 - y0) : (y0 - y1);

    int rx = dx / 2;
    int ry = dy / 2;

    if (lockAspect) {
        int r = (rx > ry) ? rx : ry;
        rx = r;
        ry = r;
    }

    if (rx <= 0 && ry <= 0) {
        PaintSetPixel(buffer, bufW, bufH, (x0 + x1) / 2, (y0 + y1) / 2, fillColor);
        return;
    }
    if (rx <= 0) rx = 1;
    if (ry <= 0) ry = 1;

    int cx = (x0 + x1) / 2;
    int cy = (y0 + y1) / 2;

    int64_t rx2 = (int64_t)rx * rx;
    int64_t ry2 = (int64_t)ry * ry;
    int64_t twoRx2 = 2 * rx2;
    int64_t twoRy2 = 2 * ry2;

    int64_t x = 0;
    int64_t y = ry;
    int64_t p = (int64_t)(ry2 - (rx2 * ry) + (0.25 * rx2));
    int64_t px = 0;
    int64_t py = twoRx2 * y;

    _FillEllipseScanlines(buffer, bufW, bufH, cx, cy, (int)x, (int)y, fillColor);

    while (px < py) {
        x++;
        px += twoRy2;
        if (p < 0) {
            p += ry2 + px;
        } else {
            y--;
            py -= twoRx2;
            p += ry2 + px - py;
        }
        _FillEllipseScanlines(buffer, bufW, bufH, cx, cy, (int)x, (int)y, fillColor);
    }

    p = (int64_t)(ry2 * (x * 2 + 1) * (x * 2 + 1) / 4 + rx2 * (y - 1) * (y - 1) - rx2 * ry2);
    while (y > 0) {
        y--;
        py -= twoRx2;
        if (p > 0) {
            p += rx2 - py;
        } else {
            x++;
            px += twoRy2;
            p += rx2 - py + px;
        }
        _FillEllipseScanlines(buffer, bufW, bufH, cx, cy, (int)x, (int)y, fillColor);
    }

    // Outline perimeter if strokeWidth > 0
    if (strokeWidth > 0) {
        PaintDrawMidpointCircle(buffer, bufW, bufH, x0, y0, x1, y1, strokeWidth, outlineColor, lockAspect);
    }
}

struct FillSpan {
    int xLeft;
    int xRight;
    int y;
    int dy;
};

#define FILL_STACK_SIZE 16384
static FillSpan s_fillStack[FILL_STACK_SIZE];

void PaintScanlineFloodFill(uint32_t* buffer, int bufW, int bufH,
                            int startX, int startY, uint32_t fillColor) {
    if (startX < 0 || startX >= bufW || startY < 0 || startY >= bufH) return;

    uint32_t targetColor = buffer[startY * bufW + startX];
    if (targetColor == fillColor) return;

    int stackPtr = 0;

    // Push initial spans going downwards and upwards
    s_fillStack[stackPtr++] = { startX, startX, startY, 1 };
    s_fillStack[stackPtr++] = { startX, startX, startY - 1, -1 };

    while (stackPtr > 0) {
        FillSpan span = s_fillStack[--stackPtr];
        int x1 = span.xLeft;
        int x2 = span.xRight;
        int y = span.y;
        int dy = span.dy;

        if (y < 0 || y >= bufH) continue;

        int x = x1;
        if (buffer[y * bufW + x] == targetColor) {
            while (x > 0 && buffer[y * bufW + (x - 1)] == targetColor) {
                x--;
            }
            if (x < x1 && stackPtr < FILL_STACK_SIZE - 2) {
                s_fillStack[stackPtr++] = { x, x1 - 1, y - dy, -dy };
            }
        }

        while (x1 <= x2) {
            while (x1 <= x2 && buffer[y * bufW + x1] != targetColor) {
                x1++;
            }
            if (x1 > x2) break;

            int startScan = x1;
            while (x1 < bufW && buffer[y * bufW + x1] == targetColor) {
                buffer[y * bufW + x1] = fillColor;
                x1++;
            }
            int endScan = x1 - 1;

            if (stackPtr < FILL_STACK_SIZE - 2) {
                s_fillStack[stackPtr++] = { startScan, endScan, y + dy, dy };
            }
            if (startScan < span.xLeft && stackPtr < FILL_STACK_SIZE - 2) {
                s_fillStack[stackPtr++] = { startScan, span.xLeft - 1, y - dy, -dy };
            }
            if (endScan > span.xRight && stackPtr < FILL_STACK_SIZE - 2) {
                s_fillStack[stackPtr++] = { span.xRight + 1, endScan, y - dy, -dy };
            }
        }
    }
}

void PaintInvertColors(uint32_t* buffer, int bufW, int bufH) {
    int total = bufW * bufH;
    for (int i = 0; i < total; ++i) {
        uint32_t px = buffer[i];
        buffer[i] = (px & 0xFF000000) | (~px & 0x00FFFFFF);
    }
}

void PaintClearCanvas(uint32_t* buffer, int bufW, int bufH, uint32_t clearColor) {
    int total = bufW * bufH;
    for (int i = 0; i < total; ++i) {
        buffer[i] = clearColor;
    }
}
