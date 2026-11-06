#ifndef __GLIMPSE_FS_H__
#define __GLIMPSE_FS_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*GlimpseScanCallback)(const char* full_path, const char* filename, void* user_data);

/**
 * Ensures `/appdata/glimpse/icons/` directory tree exists on disk.
 */
void GlimpseEnsureCacheDir(void);

/**
 * Checks if a file exists on disk and is readable.
 */
bool GlimpseFileExists(const char* path);

/**
 * Derives a deterministic cached thumbnail path in `/appdata/glimpse/icons/`.
 */
void GlimpseGetThumbnailPath(const char* image_path, char* out_thumb_path, size_t max_len);

/**
 * Scans `/` (root) and `/image` (or `/images`) for .jpg, .jpeg, .png, and .bmp files.
 */
int GlimpseScanImages(GlimpseScanCallback cb, void* user_data);

#ifdef __cplusplus
}
#endif

#endif // __GLIMPSE_FS_H__
