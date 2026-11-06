#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"
#include "svgdec.h"
#include <stdlib.h>

int ns_svg_decode(char* data, size_t len, unsigned** out_px, int* out_w, int* out_h,
                  int* out_iw, int* out_ih) {
	NSVGimage* image;
	NSVGrasterizer* rast;
	unsigned char* rgba;
	unsigned* px;
	float scale;
	int w, h, i, n;
	float maxdim;
	*out_px = 0;
	if (!data || !len || len > 2u * 1024u * 1024u)
		return -1;
	data[len] = 0;
	image = nsvgParse(data, "px", 96.0f);
	if (!image || image->width < 1.0f || image->height < 1.0f) {
		nsvgDelete(image);
		return -1;
	}
	if (out_iw)
		*out_iw = (int)(image->width + 0.5f);
	if (out_ih)
		*out_ih = (int)(image->height + 0.5f);
	/* Rasterise above the SVG's own pixel grid.  A 1200px illustration
	 * (Zerodha's landing art) used to be clamped to 1024 and then
	 * stretched, so the dashboard text turned into blocks.  Two times
	 * the longest side, and never below the intrinsic size when that
	 * size fits, keeps the fine strokes.  2048 is the memory cap. */
	maxdim = image->width > image->height ? image->width : image->height;
	{
		float target = maxdim * 2.0f;
		if (target < 512.0f)
			target = 512.0f;
		if (target > 2048.0f)
			target = 2048.0f;
		if (maxdim <= 2048.0f && target < maxdim)
			target = maxdim;
		scale = target / maxdim;
	}
	w = (int)(image->width * scale);
	h = (int)(image->height * scale);
	if (w < 1)
		w = 1;
	if (h < 1)
		h = 1;
	rgba = (unsigned char*)malloc((size_t)w * (size_t)h * 4);
	rast = nsvgCreateRasterizer();
	if (!rgba || !rast) {
		free(rgba);
		nsvgDeleteRasterizer(rast);
		nsvgDelete(image);
		return -1;
	}
	nsvgRasterize(rast, image, 0, 0, scale, rgba, w, h, w * 4);
	n = w * h;
	px = (unsigned*)malloc((size_t)n * 4);
	if (!px) {
		free(rgba);
		nsvgDeleteRasterizer(rast);
		nsvgDelete(image);
		return -1;
	}
	for (i = 0; i < n; i++) {
		unsigned char* p = rgba + i * 4;
		px[i] = ((unsigned)p[3] << 24) | ((unsigned)p[0] << 16) | ((unsigned)p[1] << 8) | p[2];
	}
	free(rgba);
	nsvgDeleteRasterizer(rast);
	nsvgDelete(image);
	*out_px = px;
	*out_w = w;
	*out_h = h;
	return 0;
}
