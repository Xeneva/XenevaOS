#ifndef __GLIMPSE_IMAGE_H__
#define __GLIMPSE_IMAGE_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	uint8_t* pixels; // RGBA 32-bit format [R, G, B, A]
	int width;
	int height;
} GlimpseRawImage;

/**
 * Load any supported image format (.jpg, .jpeg, .png, .bmp) into 32-bit RGBA.
 */
bool GlimpseLoadImage(const char* path, GlimpseRawImage* out_img);

/**
 * Free memory allocated for raw image pixels.
 */
void GlimpseFreeImage(GlimpseRawImage* img);

/**
 * Compute fitted width and height preserving aspect ratio.
 */
void GlimpseFitDimensions(int src_w, int src_h, int max_w, int max_h, int* out_w, int* out_h);

/**
 * High-performance integer area-sampling downscale / resize.
 */
bool GlimpseResizeImage(const GlimpseRawImage* src, int dst_w, int dst_h, GlimpseRawImage* out_resized);

/**
 * Save a 32-bit RGBA image to disk as a standard Chitralekha-compatible BMP file.
 */
bool GlimpseSaveBmp(const char* path, const GlimpseRawImage* img);

#ifdef __cplusplus
}
#endif

#endif // __GLIMPSE_IMAGE_H__
