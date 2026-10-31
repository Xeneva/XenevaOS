/* 8-bit non-interlaced PNG (gray, RGB, palette, RGBA) via miniz tinfl. */
#include "pngdec.h"
#include "miniz_tinfl.h"
#include <stdlib.h>
#include <string.h>

static unsigned be32(const unsigned char* p) {
	return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3];
}

static int paeth(int a, int b, int c) {
	int p = a + b - c;
	int pa = p - a; if (pa < 0) pa = -pa;
	int pb = p - b; if (pb < 0) pb = -pb;
	int pc = p - c; if (pc < 0) pc = -pc;
	if (pa <= pb && pa <= pc) return a;
	if (pb <= pc) return b;
	return c;
}

int ns_png_decode(const unsigned char* data, size_t len, unsigned** out_px, int* out_w, int* out_h) {
	unsigned char plte[256 * 3];
	unsigned char trns[256];
	unsigned char* idat = 0;
	size_t idat_len = 0, idat_cap = 0;
	unsigned w = 0, h = 0;
	int depth = 0, ctype = 0;
	int x, y, bpp, stride, i;
	unsigned char* raw = 0;
	unsigned* px = 0;
	size_t raw_len;
	if (out_px) *out_px = 0;
	if (!data || len < 8 || data[0] != 137 || data[1] != 'P') return -1;
	memset(trns, 255, sizeof trns);
	i = 8;
	while (i + 12 <= (int)len) {
		unsigned clen = be32(data + i);
		const unsigned char* type = data + i + 4;
		const unsigned char* chunk = data + i + 8;
		if (i + 12 + (int)clen > (int)len) break;
		if (type[0] == 'I' && type[1] == 'H' && type[2] == 'D' && type[3] == 'R') {
			w = be32(chunk); h = be32(chunk + 4);
			depth = chunk[8]; ctype = chunk[9];
			if (chunk[10] != 0 || chunk[11] != 0 || chunk[12] != 0) { free(idat); return -1; }
		} else if (type[0] == 'P' && type[1] == 'L' && type[2] == 'T' && type[3] == 'E') {
			if (clen <= sizeof plte) memcpy(plte, chunk, clen);
		} else if (type[0] == 't' && type[1] == 'R' && type[2] == 'N' && type[3] == 'S') {
			if (clen <= sizeof trns) memcpy(trns, chunk, clen);
		} else if (type[0] == 'I' && type[1] == 'D' && type[2] == 'A' && type[3] == 'T') {
			if (idat_len + clen > idat_cap) {
				size_t nc = idat_cap ? idat_cap * 2 : 4096;
				unsigned char* nbuf;
				while (nc < idat_len + clen) nc *= 2;
				nbuf = (unsigned char*)realloc(idat, nc);
				if (!nbuf) { free(idat); return -1; }
				idat = nbuf; idat_cap = nc;
			}
			memcpy(idat + idat_len, chunk, clen);
			idat_len += clen;
		} else if (type[0] == 'I' && type[1] == 'E' && type[2] == 'N' && type[3] == 'D') {
			break;
		}
		i += 12 + (int)clen;
	}
	/* 8-bit, non-interlaced. Larger photos still fit a page slot. */
	if (!w || !h || depth != 8 || w > 1920 || h > 1920) { free(idat); return -1; }
	if (ctype == 0) bpp = 1;
	else if (ctype == 2) bpp = 3;
	else if (ctype == 3) bpp = 1;
	else if (ctype == 6) bpp = 4;
	else { free(idat); return -1; }
	stride = (int)w * bpp;
	raw_len = (size_t)(stride + 1) * h;
	raw = (unsigned char*)malloc(raw_len);
	if (!raw || !idat) { free(raw); free(idat); return -1; }
	if (tinfl_decompress_mem_to_mem(raw, raw_len, idat, idat_len, TINFL_FLAG_PARSE_ZLIB_HEADER) == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) {
		free(raw); free(idat); return -1;
	}
	free(idat);
	px = (unsigned*)malloc((size_t)w * h * 4);
	if (!px) { free(raw); return -1; }
	for (y = 0; y < (int)h; y++) {
		unsigned char* row = raw + (size_t)y * (stride + 1);
		unsigned char* prev = y ? raw + (size_t)(y - 1) * (stride + 1) + 1 : 0;
		int filter = row[0];
		unsigned char* s = row + 1;
		for (x = 0; x < stride; x++) {
			int left = x >= bpp ? s[x - bpp] : 0;
			int up = prev ? prev[x] : 0;
			int ul = (prev && x >= bpp) ? prev[x - bpp] : 0;
			int v = s[x];
			if (filter == 1) v += left;
			else if (filter == 2) v += up;
			else if (filter == 3) v += (left + up) / 2;
			else if (filter == 4) v += paeth(left, up, ul);
			s[x] = (unsigned char)v;
		}
		for (x = 0; x < (int)w; x++) {
			unsigned char r, g, b, a = 255;
			unsigned char* p = s + x * bpp;
			if (ctype == 0) { r = g = b = p[0]; }
			else if (ctype == 2) { r = p[0]; g = p[1]; b = p[2]; }
			else if (ctype == 6) { r = p[0]; g = p[1]; b = p[2]; a = p[3]; }
			else {
				unsigned char ix = p[0];
				r = plte[ix * 3]; g = plte[ix * 3 + 1]; b = plte[ix * 3 + 2];
				a = trns[ix];
			}
			px[y * (int)w + x] = ((unsigned)a << 24) | ((unsigned)r << 16) | ((unsigned)g << 8) | b;
		}
	}
	free(raw);
	*out_px = px;
	*out_w = (int)w;
	*out_h = (int)h;
	return 0;
}
