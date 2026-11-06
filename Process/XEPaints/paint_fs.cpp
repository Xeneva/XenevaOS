#include "paint_fs.h"
#include <sys/_kefile.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <bmp.h>

bool PaintSaveBMP(const char* filepath, const uint32_t* buffer, int width, int height) {
    if (!filepath || !buffer || width <= 0 || height <= 0) {
        return false;
    }

    _KeCreateDir((char*)"/pictures");

    int rowStride = ((width * 3 + 3) / 4) * 4;
    uint32_t imageSize = rowStride * height;
    uint32_t fileSize = sizeof(BMP) + sizeof(BMPInfo) + imageSize;

    uint8_t* fileData = (uint8_t*)malloc(fileSize);
    if (!fileData) {
        return false;
    }
    memset(fileData, 0, fileSize);

    BMP* bmp = (BMP*)fileData;
    bmp->type = 0x4D42; // 'BM'
    bmp->size = fileSize;
    bmp->resv1 = 0;
    bmp->resv2 = 0;
    bmp->off_bits = sizeof(BMP) + sizeof(BMPInfo);

    BMPInfo* info = (BMPInfo*)(fileData + sizeof(BMP));
    info->biSize = sizeof(BMPInfo);
    info->biWidth = width;
    info->biHeight = height; // Bottom-up
    info->biPlanes = 1;
    info->biBitCount = 24;
    info->biCompression = 0; // BI_RGB
    info->biSizeImage = imageSize;
    info->biXPelsPerMeter = 2835;
    info->biYPelsPerMeter = 2835;
    info->biClrUsed = 0;
    info->biClrImportant = 0;

    uint8_t* pixelData = fileData + bmp->off_bits;
    for (int y = 0; y < height; ++y) {
        // Bottom-up: row 0 in BMP is the bottom row of the image
        int srcY = height - 1 - y;
        const uint32_t* srcRow = buffer + (srcY * width);
        uint8_t* dstRow = pixelData + (y * rowStride);

        int colIdx = 0;
        for (int x = 0; x < width; ++x) {
            uint32_t px = srcRow[x];
            dstRow[colIdx++] = (uint8_t)(px & 0xFF);         // Blue
            dstRow[colIdx++] = (uint8_t)((px >> 8) & 0xFF);  // Green
            dstRow[colIdx++] = (uint8_t)((px >> 16) & 0xFF); // Red
        }
    }

    int fd = _KeOpenFile((char*)filepath, FILE_OPEN_WRITE | FILE_OPEN_CREAT);
    if (fd < 0) {
        // Fallback to root directory if subfolder creation failed
        fd = _KeOpenFile((char*)"/paint.bmp", FILE_OPEN_WRITE | FILE_OPEN_CREAT);
        if (fd < 0) {
            free(fileData);
            return false;
        }
    }

    _KeWriteFile(fd, fileData, fileSize);
    _KeCloseFile(fd);
    free(fileData);

    return true;
}

bool PaintLoadBMP(const char* filepath, uint32_t* buffer, int canvasW, int canvasH) {
    if (!filepath || !buffer) return false;

    int fd = _KeOpenFile((char*)filepath, FILE_OPEN_READ_ONLY);
    if (fd < 0) {
        return false;
    }

    XEFileStatus stat;
    _KeFileStat(fd, &stat);
    if (stat.size < sizeof(BMP) + sizeof(BMPInfo)) {
        _KeCloseFile(fd);
        return false;
    }

    uint8_t* fileData = (uint8_t*)malloc(stat.size);
    if (!fileData) {
        _KeCloseFile(fd);
        return false;
    }

    _KeReadFile(fd, fileData, stat.size);
    _KeCloseFile(fd);

    BMP* bmp = (BMP*)fileData;
    if (bmp->type != 0x4D42) {
        free(fileData);
        return false;
    }

    BMPInfo* info = (BMPInfo*)(fileData + sizeof(BMP));
    int imgW = info->biWidth;
    int imgH = info->biHeight;
    bool bottomUp = true;
    if (imgH < 0) {
        bottomUp = false;
        imgH = -imgH;
    }

    int bpp = info->biBitCount;
    if (bpp != 24 && bpp != 32) {
        free(fileData);
        return false;
    }

    const uint8_t* pixelData = fileData + bmp->off_bits;
    int rowStride = (bpp == 24) ? (((imgW * 3 + 3) / 4) * 4) : (imgW * 4);

    int copyW = (imgW < canvasW) ? imgW : canvasW;
    int copyH = (imgH < canvasH) ? imgH : canvasH;

    for (int y = 0; y < copyH; ++y) {
        int srcY = bottomUp ? (imgH - 1 - y) : y;
        const uint8_t* srcRow = pixelData + (srcY * rowStride);
        uint32_t* dstRow = buffer + (y * canvasW);

        if (bpp == 24) {
            for (int x = 0; x < copyW; ++x) {
                uint8_t b = srcRow[x * 3 + 0];
                uint8_t g = srcRow[x * 3 + 1];
                uint8_t r = srcRow[x * 3 + 2];
                dstRow[x] = 0xFF000000 | (r << 16) | (g << 8) | b;
            }
        } else if (bpp == 32) {
            for (int x = 0; x < copyW; ++x) {
                uint8_t b = srcRow[x * 4 + 0];
                uint8_t g = srcRow[x * 4 + 1];
                uint8_t r = srcRow[x * 4 + 2];
                uint8_t a = srcRow[x * 4 + 3];
                if (a == 0) a = 0xFF;
                dstRow[x] = (a << 24) | (r << 16) | (g << 8) | b;
            }
        }
    }

    free(fileData);
    return true;
}
