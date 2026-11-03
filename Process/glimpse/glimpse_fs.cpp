#include "glimpse_fs.h"
#include <sys/_kefile.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

void GlimpseEnsureCacheDir(void) {
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

	// Extract base filename (ignoring parent directory path)
	const char* last_slash = strrchr(image_path, '/');
	const char* base = last_slash ? (last_slash + 1) : image_path;

	char name_buf[8]; // Max 6 chars so "t_" + name_buf <= 8 chars (8.3 FAT limit)
	memset(name_buf, 0, sizeof(name_buf));

	size_t n = 0;
	for (size_t i = 0; base[i] && base[i] != '.' && n < 6; i++) {
		char c = base[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
			name_buf[n++] = c;
		}
	}
	if (n == 0) {
		strcpy(name_buf, "img");
	}

	snprintf(out_thumb_path, max_len, "/thumbs/t_%s.bmp", name_buf);
}

static void scan_single_dir(const char* dir_path, GlimpseScanCallback cb, void* user_data, int* count) {
	int dirfd = _KeOpenDir((char*)dir_path);
	if (dirfd < 0) return;

	XEDirectoryEntry dirent;
	memset(&dirent, 0, sizeof(dirent));

	int prev_index = -1;
	int loop_guard = 0;
	while (loop_guard++ < 4096) {
		if (dirent.index == -1) break;
		prev_index = dirent.index;
		int code = _KeReadDir(dirfd, &dirent);
		if (dirent.index == -1) break;
		if (code == -1 && dirent.index == prev_index) break;
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

	// 1. Scan root directory "/"
	scan_single_dir("/", cb, user_data, &found_count);

	// 2. Scan "/image" directory
	scan_single_dir("/image", cb, user_data, &found_count);

	// 3. Scan "/images" directory
	scan_single_dir("/images", cb, user_data, &found_count);

	return found_count;
}
