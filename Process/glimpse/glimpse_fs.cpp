#include "glimpse_fs.h"
#include <sys/_kefile.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

void GlimpseEnsureCacheDir(void) {
	_KeCreateDir((char*)"/appdata");
	_KeCreateDir((char*)"/appdata/glimpse");
	_KeCreateDir((char*)"/appdata/glimpse/thumbs");
	_KeCreateDir((char*)"/thumbs");
}

bool GlimpseFileExists(const char* path) {
	if (!path) return false;
	int fd = _KeOpenFile((char*)path, FILE_OPEN_READ_ONLY);
	if (fd < 0) return false;
	_KeCloseFile(fd);
	return true;
}

static bool has_image_extension(const char* filename) {
	if (!filename) return false;
	const char* dot = strrchr(filename, '.');
	if (!dot) return false;

	char ext[8];
	int i = 0;
	while (dot[i] && i < 7) {
		ext[i] = (char)tolower((unsigned char)dot[i]);
		i++;
	}
	ext[i] = '\0';

	return (strcmp(ext, ".jpg") == 0 ||
			strcmp(ext, ".jpeg") == 0 ||
			strcmp(ext, ".png") == 0 ||
			strcmp(ext, ".bmp") == 0);
}

void GlimpseGetThumbnailPath(const char* image_path, char* out_thumb_path, size_t max_len) {
	if (!image_path || !out_thumb_path || max_len < 32) return;

	// Use FNV-1a hash of full path to guarantee collision-free 8.3 FAT filename
	uint32_t hash = 2166136261u;
	for (const char* p = image_path; *p; p++) {
		hash ^= (uint8_t)(*p);
		hash *= 16777619u;
	}
	uint32_t h24 = (hash ^ (hash >> 24)) & 0x00FFFFFF;

	snprintf(out_thumb_path, max_len, "/thumbs/t_%06x.bmp", h24);
}

static void scan_single_dir(const char* dir_path, GlimpseScanCallback cb, void* user_data, int* count) {
	int dirfd = _KeOpenDir((char*)dir_path);
	if (dirfd < 0) return;

	XEDirectoryEntry dirent;
	memset(&dirent, 0, sizeof(dirent));

	while (1) {
		if (dirent.index == -1) break;
		int code = _KeReadDir(dirfd, &dirent);
		if (code != -1) {
			if (dirent.flags & FILE_GENERAL) {
				if (strcmp(dirent.filename, ".") != 0 && strcmp(dirent.filename, "..") != 0) {
					if (has_image_extension(dirent.filename)) {
						char full_path[256];
						if (strcmp(dir_path, "/") == 0) {
							snprintf(full_path, sizeof(full_path), "/%s", dirent.filename);
						} else {
							snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, dirent.filename);
						}
						if (GlimpseFileExists(full_path)) {
							if (cb) {
								cb(full_path, dirent.filename, user_data);
							}
							(*count)++;
						}
					}
				}
			}
		}
		memset(dirent.filename, 0, sizeof(dirent.filename));
	}

	_KeCloseFile(dirfd);
}

int GlimpseScanImages(GlimpseScanCallback cb, void* user_data) {
	int found_count = 0;

	// Scan common image directories
	scan_single_dir("/", cb, user_data, &found_count);
	scan_single_dir("/image", cb, user_data, &found_count);
	scan_single_dir("/images", cb, user_data, &found_count);
	scan_single_dir("/data", cb, user_data, &found_count);
	scan_single_dir("/photos", cb, user_data, &found_count);

	return found_count;
}
