#include "glimpse_image.h"
#include "upng.h"
#include <nanojpg.h>
#include <bmp.h>
#include <sys/_kefile.h>
#include <stdlib.h>
#include <string.h>

void GlimpseFitDimensions(int src_w, int src_h, int max_w, int max_h, int* out_w, int* out_h) {
	if (src_w <= 0 || src_h <= 0) {
		*out_w = max_w;
		*out_h = max_h;
		return;
	}
	if (src_w * max_h > src_h * max_w) {
		*out_w = max_w;
		*out_h = (src_h * max_w) / src_w;
	} else {
		*out_h = max_h;
		*out_w = (src_w * max_h) / src_h;
	}
	if (*out_w < 1) *out_w = 1;
	if (*out_h < 1) *out_h = 1;
}

void GlimpseFreeImage(GlimpseRawImage* img) {
	if (img && img->pixels) {
		free(img->pixels);
		img->pixels = NULL;
		img->width = 0;
		img->height = 0;
	}
}

static bool load_jpeg(const uint8_t* buf, size_t sz, GlimpseRawImage* out_img) {
	Jpeg::Decoder* decoder = new Jpeg::Decoder(buf, (uint32_t)sz, malloc, free);
	if (!decoder || decoder->GetResult() != Jpeg::Decoder::OK) {
		if (decoder) delete decoder;
		return false;
	}

	int w = decoder->GetWidth();
	int h = decoder->GetHeight();
	uint8_t* rgb = decoder->GetImage();
	if (!rgb || w <= 0 || h <= 0) {
		delete decoder;
		return false;
	}

	uint8_t* rgba = (uint8_t*)malloc(w * h * 4);
	if (!rgba) {
		delete decoder;
		return false;
	}

	if (decoder->IsColor()) {
		for (int i = 0; i < w * h; i++) {
			rgba[i * 4 + 0] = rgb[i * 3 + 0];
			rgba[i * 4 + 1] = rgb[i * 3 + 1];
			rgba[i * 4 + 2] = rgb[i * 3 + 2];
			rgba[i * 4 + 3] = 0xFF;
		}
	} else {
		for (int i = 0; i < w * h; i++) {
			uint8_t lum = rgb[i];
			rgba[i * 4 + 0] = lum;
			rgba[i * 4 + 1] = lum;
			rgba[i * 4 + 2] = lum;
			rgba[i * 4 + 3] = 0xFF;
		}
	}

	out_img->pixels = rgba;
	out_img->width = w;
	out_img->height = h;

	delete decoder;
	return true;
}

static bool load_png(const uint8_t* buf, size_t sz, GlimpseRawImage* out_img) {
	upng_t* upng = upng_new_from_bytes(buf, (unsigned long)sz);
	if (!upng) return false;

	if (upng_decode(upng) != UPNG_EOK) {
		upng_free(upng);
		return false;
	}

	int w = (int)upng_get_width(upng);
	int h = (int)upng_get_height(upng);
	const uint8_t* src_buf = upng_get_buffer(upng);
	upng_format fmt = upng_get_format(upng);

	if (!src_buf || w <= 0 || h <= 0) {
		upng_free(upng);
		return false;
	}

	uint8_t* rgba = (uint8_t*)malloc(w * h * 4);
	if (!rgba) {
		upng_free(upng);
		return false;
	}

	if (fmt == UPNG_RGBA8) {
		memcpy(rgba, (void*)src_buf, w * h * 4);
	} else if (fmt == UPNG_RGB8) {
		for (int i = 0; i < w * h; i++) {
			rgba[i * 4 + 0] = src_buf[i * 3 + 0];
			rgba[i * 4 + 1] = src_buf[i * 3 + 1];
			rgba[i * 4 + 2] = src_buf[i * 3 + 2];
			rgba[i * 4 + 3] = 0xFF;
		}
	} else {
		free(rgba);
		upng_free(upng);
		return false;
	}

	out_img->pixels = rgba;
	out_img->width = w;
	out_img->height = h;

	upng_free(upng);
	return true;
}

static bool load_bmp(const uint8_t* buf, size_t sz, GlimpseRawImage* out_img) {
	if (sz < sizeof(BMP) + sizeof(BMPInfo)) return false;

	const BMP* bmp = (const BMP*)buf;
	if (bmp->type != 0x4D42) return false;

	const BMPInfo* info = (const BMPInfo*)(buf + sizeof(BMP));
	int w = info->biWidth;
	int h = info->biHeight;
	bool top_down = false;
	if (h < 0) {
		top_down = true;
		h = -h;
	}
	if (w <= 0 || h <= 0) return false;

	int bpp = info->biBitCount;
	if (bpp != 24 && bpp != 32) return false;

	const uint8_t* pixel_src = buf + bmp->off_bits;
	uint8_t* rgba = (uint8_t*)malloc(w * h * 4);
	if (!rgba) return false;

	int row_stride = (bpp == 32) ? (w * 4) : (((w * 3 + 3) / 4) * 4);

	for (int y = 0; y < h; y++) {
		int src_y = top_down ? y : (h - 1 - y);
		const uint8_t* src_row = pixel_src + (src_y * row_stride);
		uint8_t* dst_row = rgba + (y * w * 4);

		if (bpp == 32) {
			for (int x = 0; x < w; x++) {
				dst_row[x * 4 + 0] = src_row[x * 4 + 2]; // R
				dst_row[x * 4 + 1] = src_row[x * 4 + 1]; // G
				dst_row[x * 4 + 2] = src_row[x * 4 + 0]; // B
				dst_row[x * 4 + 3] = src_row[x * 4 + 3]; // A
			}
		} else {
			for (int x = 0; x < w; x++) {
				dst_row[x * 4 + 0] = src_row[x * 3 + 2]; // R
				dst_row[x * 4 + 1] = src_row[x * 3 + 1]; // G
				dst_row[x * 4 + 2] = src_row[x * 3 + 0]; // B
				dst_row[x * 4 + 3] = 0xFF;              // A
			}
		}
	}

	out_img->pixels = rgba;
	out_img->width = w;
	out_img->height = h;
	return true;
}

bool GlimpseLoadImage(const char* path, GlimpseRawImage* out_img) {
	if (!path || !out_img) return false;
	memset(out_img, 0, sizeof(GlimpseRawImage));

	int fd = _KeOpenFile((char*)path, FILE_OPEN_READ_ONLY);
	if (fd < 0) return false;

	XEFileStatus st;
	memset(&st, 0, sizeof(st));
	_KeFileStat(fd, &st);
	if (st.size < 8) {
		_KeCloseFile(fd);
		return false;
	}

	uint8_t* filebuf = (uint8_t*)malloc(st.size);
	if (!filebuf) {
		_KeCloseFile(fd);
		return false;
	}

	size_t read_bytes = _KeReadFile(fd, filebuf, st.size);
	_KeCloseFile(fd);

	if (read_bytes == 0) {
		free(filebuf);
		return false;
	}

	bool ok = false;
	// Detect image format by magic header
	if (filebuf[0] == 0xFF && filebuf[1] == 0xD8) {
		ok = load_jpeg(filebuf, st.size, out_img);
	} else if (filebuf[0] == 0x89 && filebuf[1] == 'P' && filebuf[2] == 'N' && filebuf[3] == 'G') {
		ok = load_png(filebuf, st.size, out_img);
	} else if (filebuf[0] == 'B' && filebuf[1] == 'M') {
		ok = load_bmp(filebuf, st.size, out_img);
	}

	free(filebuf);
	return ok;
}

bool GlimpseResizeImage(const GlimpseRawImage* src, int dst_w, int dst_h, GlimpseRawImage* out_resized) {
	if (!src || !src->pixels || src->width <= 0 || src->height <= 0 || dst_w <= 0 || dst_h <= 0) {
		return false;
	}

	uint8_t* dst_pixels = (uint8_t*)malloc(dst_w * dst_h * 4);
	if (!dst_pixels) return false;

	int src_w = src->width;
	int src_h = src->height;

	for (int dy = 0; dy < dst_h; dy++) {
		int sy0 = (dy * src_h) / dst_h;
		int sy1 = ((dy + 1) * src_h) / dst_h;
		if (sy1 <= sy0) sy1 = sy0 + 1;
		if (sy1 > src_h) sy1 = src_h;

		for (int dx = 0; dx < dst_w; dx++) {
			int sx0 = (dx * src_w) / dst_w;
			int sx1 = ((dx + 1) * src_w) / dst_w;
			if (sx1 <= sx0) sx1 = sx0 + 1;
			if (sx1 > src_w) sx1 = src_w;

			uint32_t r_sum = 0, g_sum = 0, b_sum = 0, a_sum = 0;
			uint32_t count = 0;

			for (int sy = sy0; sy < sy1; sy++) {
				const uint8_t* row = src->pixels + (sy * src_w * 4);
				for (int sx = sx0; sx < sx1; sx++) {
					r_sum += row[sx * 4 + 0];
					g_sum += row[sx * 4 + 1];
					b_sum += row[sx * 4 + 2];
					a_sum += row[sx * 4 + 3];
					count++;
				}
			}

			if (count == 0) count = 1;
			uint8_t* d = dst_pixels + ((dy * dst_w + dx) * 4);
			d[0] = (uint8_t)(r_sum / count);
			d[1] = (uint8_t)(g_sum / count);
			d[2] = (uint8_t)(b_sum / count);
			d[3] = (uint8_t)(a_sum / count);
		}
	}

	out_resized->pixels = dst_pixels;
	out_resized->width = dst_w;
	out_resized->height = dst_h;
	return true;
}

bool GlimpseSaveBmp(const char* path, const GlimpseRawImage* img) {
	if (!path || !img || !img->pixels || img->width <= 0 || img->height <= 0) return false;

	int w = img->width;
	int h = img->height;
	uint32_t pixel_bytes = (uint32_t)(w * h * 4);
	uint32_t total_size = sizeof(BMP) + sizeof(BMPInfo) + pixel_bytes;

	uint8_t* out_buf = (uint8_t*)malloc(total_size);
	if (!out_buf) return false;

	BMP* bmp = (BMP*)out_buf;
	bmp->type = 0x4D42; // 'BM'
	bmp->size = total_size;
	bmp->resv1 = 0;
	bmp->resv2 = 0;
	bmp->off_bits = sizeof(BMP) + sizeof(BMPInfo); // 54

	BMPInfo* info = (BMPInfo*)(out_buf + sizeof(BMP));
	memset(info, 0, sizeof(BMPInfo));
	info->biSize = sizeof(BMPInfo);
	info->biWidth = w;
	info->biHeight = h; // positive height = bottom-up, perfectly matches Chitralekha ChDrawIcon!
	info->biPlanes = 1;
	info->biBitCount = 32;
	info->biCompression = 0; // BI_RGB
	info->biSizeImage = pixel_bytes;
	info->biXPelsPerMeter = 3780;
	info->biYPelsPerMeter = 3780;

	uint8_t* dst_pixels = out_buf + bmp->off_bits;

	// In bottom-up BMP, row 0 in file corresponds to bottom row of image (y = h - 1)
	// Chitralekha ChDrawIcon expects BGRA byte ordering
	for (int y = 0; y < h; y++) {
		int src_y = h - 1 - y;
		const uint8_t* src_row = img->pixels + (src_y * w * 4);
		uint8_t* dst_row = dst_pixels + (y * w * 4);

		for (int x = 0; x < w; x++) {
			dst_row[x * 4 + 0] = src_row[x * 4 + 2]; // B
			dst_row[x * 4 + 1] = src_row[x * 4 + 1]; // G
			dst_row[x * 4 + 2] = src_row[x * 4 + 0]; // R
			dst_row[x * 4 + 3] = src_row[x * 4 + 3]; // A
		}
	}

	int fd = _KeOpenFile((char*)path, FILE_OPEN_WRITE | FILE_OPEN_CREAT);
	if (fd < 0) {
		free(out_buf);
		return false;
	}

	size_t written = _KeWriteFile(fd, out_buf, total_size);
	_KeCloseFile(fd);
	free(out_buf);

	return (written == total_size);
}
