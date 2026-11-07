#include "upng.h"
#include <stdlib.h>
#include <string.h>

#define MAKE_BYTE(a,b,c,d) (((a) << 24) | ((b) << 16) | ((c) << 8) | (d))

#define CHUNK_IHDR MAKE_BYTE('I', 'H', 'D', 'R')
#define CHUNK_IDAT MAKE_BYTE('I', 'D', 'A', 'T')
#define CHUNK_IEND MAKE_BYTE('I', 'E', 'N', 'D')

#define FIRST_LENGTH_CODE_INDEX 257
#define LAST_LENGTH_CODE_INDEX 285
#define NUM_DEFLATE_CODE_SYMBOLS 288
#define NUM_DISTANCE_SYMBOLS 32
#define NUM_CODE_LENGTH_CODES 19
#define MAX_BITS 15
#define MAX_BITS_SPECIAL 7

#define SET_ERROR(upng,code) do { (upng)->error = (code); (upng)->error_line = __LINE__; } while (0)

#define upng_chunk_length(chunk) ( \
	((unsigned long)(chunk)[0] << 24) | \
	((unsigned long)(chunk)[1] << 16) | \
	((unsigned long)(chunk)[2] << 8) | \
	((unsigned long)(chunk)[3]))

#define upng_chunk_type(chunk) MAKE_BYTE((chunk)[4], (chunk)[5], (chunk)[6], (chunk)[7])
#define upng_chunk_critical(chunk) (((chunk)[4] & 32) == 0)

typedef struct huffman_tree {
	unsigned int* tree2d;
	unsigned int maxbitlen;
	unsigned int numcodes;
} huffman_tree;

struct upng_t {
	unsigned int width;
	unsigned int height;

	upng_format format;
	unsigned int bpp;
	unsigned int depth;

	upng_error error;
	unsigned int error_line;

	upng_error state;

	const unsigned char* buffer;
	unsigned long size;

	unsigned char* data;
	unsigned int data_size;
};

static const unsigned int LENGTH_BASE[29] = {
	3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
	67, 83, 99, 115, 131, 163, 195, 227, 258
};

static const unsigned int LENGTH_EXTRA[29] = {
	0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
	4, 4, 4, 4, 5, 5, 5, 5, 0
};

static const unsigned int DISTANCE_BASE[30] = {
	1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513,
	769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};

static const unsigned int DISTANCE_EXTRA[30] = {
	0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8,
	9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

static const unsigned int CLCL_ORDER[NUM_CODE_LENGTH_CODES] = {
	16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

static unsigned char read_bit(unsigned long* bitpointer, const unsigned char* bitstream) {
	unsigned char result = (unsigned char)((bitstream[(*bitpointer) >> 3] >> ((*bitpointer) & 0x7)) & 1);
	(*bitpointer)++;
	return result;
}

static unsigned int read_bits(unsigned long* bitpointer, const unsigned char* bitstream, unsigned long nbits) {
	unsigned int result = 0;
	for (unsigned long i = 0; i < nbits; i++) {
		result |= ((unsigned int)read_bit(bitpointer, bitstream)) << i;
	}
	return result;
}

static void huffman_tree_init(huffman_tree* tree, unsigned int* buffer, unsigned int numcodes, unsigned int maxbitlen) {
	tree->tree2d = buffer;
	tree->numcodes = numcodes;
	tree->maxbitlen = maxbitlen;
}

static upng_error huffman_tree_create_lengths(upng_t* upng, huffman_tree* tree, const unsigned int* bitlen) {
	unsigned int tree1d[NUM_DEFLATE_CODE_SYMBOLS];
	unsigned int blcount[MAX_BITS + 1];
	unsigned int nextcode[MAX_BITS + 1];
	unsigned int nodefilled = 0;
	unsigned int treepos = 0;

	memset(blcount, 0, sizeof(blcount));
	memset(nextcode, 0, sizeof(nextcode));

	for (unsigned int bits = 0; bits < tree->numcodes; bits++) {
		blcount[bitlen[bits]]++;
	}

	for (unsigned int bits = 1; bits <= tree->maxbitlen; bits++) {
		nextcode[bits] = (nextcode[bits - 1] + blcount[bits - 1]) << 1;
	}

	for (unsigned int n = 0; n < tree->numcodes; n++) {
		if (bitlen[n] != 0) {
			tree1d[n] = nextcode[bitlen[n]]++;
		}
	}

	for (unsigned int n = 0; n < tree->numcodes * 2; n++) {
		tree->tree2d[n] = 32767;
	}

	for (unsigned int n = 0; n < tree->numcodes; n++) {
		if (bitlen[n] != 0) {
			unsigned int code = tree1d[n];
			treepos = 0;
			for (int m = bitlen[n] - 1; m >= 0; m--) {
				unsigned char bit = (unsigned char)((code >> m) & 1);
				if (treepos > tree->numcodes * 2) {
					SET_ERROR(upng, UPNG_EMALFORMED);
					return UPNG_EMALFORMED;
				}
				if (tree->tree2d[2 * treepos + bit] == 32767) {
					nodefilled++;
					tree->tree2d[2 * treepos + bit] = nodefilled;
					treepos = nodefilled;
				} else {
					treepos = tree->tree2d[2 * treepos + bit];
				}
			}
			tree->tree2d[2 * treepos] = n;
			tree->tree2d[2 * treepos + 1] = n;
		}
	}
	return UPNG_EOK;
}

static unsigned int huffman_decode_symbol(upng_t* upng, const unsigned char* in, unsigned long* bp, const huffman_tree* codetree, unsigned long inlength) {
	unsigned int treepos = 0;
	while (1) {
		if (*bp >= inlength * 8) {
			SET_ERROR(upng, UPNG_EMALFORMED);
			return 0;
		}
		unsigned char bit = read_bit(bp, in);
		unsigned int ct = codetree->tree2d[(treepos << 1) | bit];
		if (ct < codetree->numcodes) {
			return ct;
		}
		treepos = ct;
		if (treepos >= codetree->numcodes * 2) {
			SET_ERROR(upng, UPNG_EMALFORMED);
			return 0;
		}
	}
}

static upng_error unfilter_scanline(unsigned char* recon, const unsigned char* scanline, const unsigned char* precon, unsigned long bytewidth, unsigned char filterType, unsigned long length) {
	switch (filterType) {
	case 0:
		memcpy(recon, (void*)scanline, length);
		break;
	case 1:
		for (unsigned long i = 0; i < bytewidth; i++) recon[i] = scanline[i];
		for (unsigned long i = bytewidth; i < length; i++) recon[i] = scanline[i] + recon[i - bytewidth];
		break;
	case 2:
		if (precon) {
			for (unsigned long i = 0; i < length; i++) recon[i] = scanline[i] + precon[i];
		} else {
			memcpy(recon, (void*)scanline, length);
		}
		break;
	case 3:
		if (precon) {
			for (unsigned long i = 0; i < bytewidth; i++) recon[i] = scanline[i] + (precon[i] >> 1);
			for (unsigned long i = bytewidth; i < length; i++) recon[i] = scanline[i] + ((recon[i - bytewidth] + precon[i]) >> 1);
		} else {
			for (unsigned long i = 0; i < bytewidth; i++) recon[i] = scanline[i];
			for (unsigned long i = bytewidth; i < length; i++) recon[i] = scanline[i] + (recon[i - bytewidth] >> 1);
		}
		break;
	case 4: {
		if (precon) {
			for (unsigned long i = 0; i < bytewidth; i++) recon[i] = scanline[i] + precon[i];
			for (unsigned long i = bytewidth; i < length; i++) {
				int a = recon[i - bytewidth];
				int b = precon[i];
				int c = precon[i - bytewidth];
				int p = a + b - c;
				int pa = p > a ? p - a : a - p;
				int pb = p > b ? p - b : b - p;
				int pc = p > c ? p - c : c - p;
				recon[i] = scanline[i] + ((pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c));
			}
		} else {
			for (unsigned long i = 0; i < bytewidth; i++) recon[i] = scanline[i];
			for (unsigned long i = bytewidth; i < length; i++) recon[i] = scanline[i] + recon[i - bytewidth];
		}
		break;
	}
	default:
		return UPNG_EMALFORMED;
	}
	return UPNG_EOK;
}

static upng_error unfilter(upng_t* upng, unsigned char* out, const unsigned char* in, unsigned int w, unsigned int h, unsigned int bpp) {
	unsigned long linebytes = (w * bpp + 7) >> 3;
	unsigned long bytewidth = (bpp + 7) >> 3;
	const unsigned char* prevline = NULL;

	for (unsigned int y = 0; y < h; y++) {
		unsigned char filterType = in[y * (linebytes + 1)];
		const unsigned char* currentline = &in[y * (linebytes + 1) + 1];
		unsigned char* outline = &out[y * linebytes];

		if (unfilter_scanline(outline, currentline, prevline, bytewidth, filterType, linebytes) != UPNG_EOK) {
			SET_ERROR(upng, UPNG_EMALFORMED);
			return UPNG_EMALFORMED;
		}
		prevline = outline;
	}
	return UPNG_EOK;
}

static upng_error inflate_uncompressed(upng_t* upng, unsigned char* out, unsigned long* pos, unsigned long* bp, const unsigned char* in, unsigned long inlength, unsigned long outlength) {
	while (((*bp) & 0x7) != 0) (*bp)++;
	unsigned long p = (*bp) >> 3;
	if (p + 4 > inlength) {
		SET_ERROR(upng, UPNG_EMALFORMED);
		return UPNG_EMALFORMED;
	}
	unsigned int len = (unsigned int)in[p] | ((unsigned int)in[p + 1] << 8);
	unsigned int nlen = (unsigned int)in[p + 2] | ((unsigned int)in[p + 3] << 8);
	p += 4;
	if (len != (~nlen & 0xFFFF) || *pos + len > outlength || p + len > inlength) {
		SET_ERROR(upng, UPNG_EMALFORMED);
		return UPNG_EMALFORMED;
	}
	for (unsigned int n = 0; n < len; n++) {
		out[(*pos)++] = in[p++];
	}
	*bp = p << 3;
	return UPNG_EOK;
}

static upng_error inflate_huffman(upng_t* upng, unsigned char* out, unsigned long* outpos, unsigned long* bp, const unsigned char* in, unsigned long inlength, unsigned long outlength, const huffman_tree* tree_ll, const huffman_tree* tree_d) {
	while (1) {
		unsigned int code_ll = huffman_decode_symbol(upng, in, bp, tree_ll, inlength);
		if (upng->error != UPNG_EOK) return upng->error;
		if (code_ll <= 255) {
			if (*outpos >= outlength) {
				SET_ERROR(upng, UPNG_EMALFORMED);
				return UPNG_EMALFORMED;
			}
			out[(*outpos)++] = (unsigned char)code_ll;
		} else if (code_ll == 256) {
			break;
		} else if (code_ll >= FIRST_LENGTH_CODE_INDEX && code_ll <= LAST_LENGTH_CODE_INDEX) {
			unsigned int length = LENGTH_BASE[code_ll - FIRST_LENGTH_CODE_INDEX];
			unsigned int numextrabits = LENGTH_EXTRA[code_ll - FIRST_LENGTH_CODE_INDEX];
			length += read_bits(bp, in, numextrabits);

			unsigned int code_d = huffman_decode_symbol(upng, in, bp, tree_d, inlength);
			if (upng->error != UPNG_EOK) return upng->error;
			if (code_d >= 30) {
				SET_ERROR(upng, UPNG_EMALFORMED);
				return UPNG_EMALFORMED;
			}
			unsigned int distance = DISTANCE_BASE[code_d] + read_bits(bp, in, DISTANCE_EXTRA[code_d]);
			if (*outpos < distance || *outpos + length > outlength) {
				SET_ERROR(upng, UPNG_EMALFORMED);
				return UPNG_EMALFORMED;
			}
			for (unsigned int forward = 0; forward < length; forward++) {
				out[*outpos] = out[*outpos - distance];
				(*outpos)++;
			}
		} else {
			SET_ERROR(upng, UPNG_EMALFORMED);
			return UPNG_EMALFORMED;
		}
	}
	return UPNG_EOK;
}

static upng_error uzlib_inflate(upng_t* upng, unsigned char* out, unsigned long outsize, const unsigned char* in, unsigned long insize) {
	if (insize < 2) {
		SET_ERROR(upng, UPNG_EMALFORMED);
		return UPNG_EMALFORMED;
	}
	if ((in[0] * 256 + in[1]) % 31 != 0 || (in[0] & 15) != 8 || ((in[1] >> 5) & 1) != 0) {
		SET_ERROR(upng, UPNG_EMALFORMED);
		return UPNG_EMALFORMED;
	}

	unsigned long bp = 16;
	unsigned long pos = 0;
	unsigned int bfinal = 0;

	unsigned int tree_ll_buf[NUM_DEFLATE_CODE_SYMBOLS * 2];
	unsigned int tree_d_buf[NUM_DISTANCE_SYMBOLS * 2];
	huffman_tree tree_ll, tree_d;

	while (!bfinal) {
		bfinal = read_bit(&bp, in);
		unsigned int btype = read_bits(&bp, in, 2);

		if (btype == 0) {
			if (inflate_uncompressed(upng, out, &pos, &bp, in, insize, outsize) != UPNG_EOK) return upng->error;
		} else if (btype == 1) {
			unsigned int bitlen_ll[NUM_DEFLATE_CODE_SYMBOLS];
			unsigned int bitlen_d[NUM_DISTANCE_SYMBOLS];
			for (int i = 0; i <= 143; i++) bitlen_ll[i] = 8;
			for (int i = 144; i <= 255; i++) bitlen_ll[i] = 9;
			for (int i = 256; i <= 279; i++) bitlen_ll[i] = 7;
			for (int i = 280; i < NUM_DEFLATE_CODE_SYMBOLS; i++) bitlen_ll[i] = 8;
			for (int i = 0; i < NUM_DISTANCE_SYMBOLS; i++) bitlen_d[i] = 5;

			huffman_tree_init(&tree_ll, tree_ll_buf, NUM_DEFLATE_CODE_SYMBOLS, 15);
			huffman_tree_init(&tree_d, tree_d_buf, NUM_DISTANCE_SYMBOLS, 15);
			huffman_tree_create_lengths(upng, &tree_ll, bitlen_ll);
			huffman_tree_create_lengths(upng, &tree_d, bitlen_d);

			if (inflate_huffman(upng, out, &pos, &bp, in, insize, outsize, &tree_ll, &tree_d) != UPNG_EOK) return upng->error;
		} else if (btype == 2) {
			unsigned int hlit = read_bits(&bp, in, 5) + 257;
			unsigned int hdist = read_bits(&bp, in, 5) + 1;
			unsigned int hclen = read_bits(&bp, in, 4) + 4;

			unsigned int clcl_buffer[NUM_CODE_LENGTH_CODES * 2];
			unsigned int bitlen_cl[NUM_CODE_LENGTH_CODES];
			memset(bitlen_cl, 0, sizeof(bitlen_cl));

			for (unsigned int i = 0; i < hclen; i++) {
				bitlen_cl[CLCL_ORDER[i]] = read_bits(&bp, in, 3);
			}

			huffman_tree tree_cl;
			huffman_tree_init(&tree_cl, clcl_buffer, NUM_CODE_LENGTH_CODES, 7);
			huffman_tree_create_lengths(upng, &tree_cl, bitlen_cl);

			unsigned int lit_len_lengths[NUM_DEFLATE_CODE_SYMBOLS + NUM_DISTANCE_SYMBOLS];
			unsigned int i = 0;
			while (i < hlit + hdist) {
				unsigned int code = huffman_decode_symbol(upng, in, &bp, &tree_cl, insize);
				if (upng->error != UPNG_EOK) return upng->error;
				if (code <= 15) {
					lit_len_lengths[i++] = code;
				} else if (code == 16) {
					unsigned int replength = 3 + read_bits(&bp, in, 2);
					unsigned int value = (i == 0) ? 0 : lit_len_lengths[i - 1];
					while (replength--) lit_len_lengths[i++] = value;
				} else if (code == 17) {
					unsigned int replength = 3 + read_bits(&bp, in, 3);
					while (replength--) lit_len_lengths[i++] = 0;
				} else if (code == 18) {
					unsigned int replength = 11 + read_bits(&bp, in, 7);
					while (replength--) lit_len_lengths[i++] = 0;
				}
			}

			huffman_tree_init(&tree_ll, tree_ll_buf, hlit, 15);
			huffman_tree_init(&tree_d, tree_d_buf, hdist, 15);
			huffman_tree_create_lengths(upng, &tree_ll, lit_len_lengths);
			huffman_tree_create_lengths(upng, &tree_d, &lit_len_lengths[hlit]);

			if (inflate_huffman(upng, out, &pos, &bp, in, insize, outsize, &tree_ll, &tree_d) != UPNG_EOK) return upng->error;
		} else {
			SET_ERROR(upng, UPNG_EMALFORMED);
			return UPNG_EMALFORMED;
		}
	}
	return UPNG_EOK;
}

upng_t* upng_new_from_bytes(const unsigned char* buffer, unsigned long size) {
	upng_t* upng = (upng_t*)malloc(sizeof(upng_t));
	if (!upng) return NULL;
	memset(upng, 0, sizeof(upng_t));
	upng->buffer = buffer;
	upng->size = size;
	upng->state = UPNG_EOK;
	return upng;
}

void upng_free(upng_t* upng) {
	if (upng) {
		if (upng->data) free(upng->data);
		free(upng);
	}
}

upng_error upng_header(upng_t* upng) {
	if (upng->size < 29 || memcmp(upng->buffer, "\x89PNG\r\n\x1a\n", 8) != 0) {
		SET_ERROR(upng, UPNG_ENOTPNG);
		return UPNG_ENOTPNG;
	}
	const unsigned char* chunk = upng->buffer + 8;
	if (upng_chunk_type(chunk) != CHUNK_IHDR) {
		SET_ERROR(upng, UPNG_EMALFORMED);
		return UPNG_EMALFORMED;
	}
	const unsigned char* data = chunk + 8;
	upng->width = ((unsigned int)data[0] << 24) | ((unsigned int)data[1] << 16) | ((unsigned int)data[2] << 8) | data[3];
	upng->height = ((unsigned int)data[4] << 24) | ((unsigned int)data[5] << 16) | ((unsigned int)data[6] << 8) | data[7];
	upng->depth = data[8];
	unsigned char color_type = data[9];

	if (color_type == 2 && upng->depth == 8) {
		upng->format = UPNG_RGB8;
		upng->bpp = 24;
	} else if (color_type == 6 && upng->depth == 8) {
		upng->format = UPNG_RGBA8;
		upng->bpp = 32;
	} else {
		SET_ERROR(upng, UPNG_EUNFORMAT);
		return UPNG_EUNFORMAT;
	}
	return UPNG_EOK;
}

upng_error upng_decode(upng_t* upng) {
	if (upng->data) return UPNG_EOK;
	if (upng_header(upng) != UPNG_EOK) return upng->error;

	// Concatenate all IDAT chunks
	unsigned long total_idat = 0;
	const unsigned char* chunk = upng->buffer + 8;
	while (chunk < upng->buffer + upng->size) {
		unsigned long len = upng_chunk_length(chunk);
		if (upng_chunk_type(chunk) == CHUNK_IDAT) total_idat += len;
		chunk += 12 + len;
	}

	unsigned char* idat_buffer = (unsigned char*)malloc(total_idat);
	if (!idat_buffer) {
		SET_ERROR(upng, UPNG_ENOMEM);
		return UPNG_ENOMEM;
	}

	unsigned long offset = 0;
	chunk = upng->buffer + 8;
	while (chunk < upng->buffer + upng->size) {
		unsigned long len = upng_chunk_length(chunk);
		if (upng_chunk_type(chunk) == CHUNK_IDAT) {
			memcpy(idat_buffer + offset, (void*)(chunk + 8), len);
			offset += len;
		}
		chunk += 12 + len;
	}

	unsigned long linebytes = (upng->width * upng->bpp + 7) >> 3;
	unsigned long inflated_size = (linebytes + 1) * upng->height;
	unsigned char* inflated = (unsigned char*)malloc(inflated_size);

	if (!inflated) {
		free(idat_buffer);
		SET_ERROR(upng, UPNG_ENOMEM);
		return UPNG_ENOMEM;
	}

	upng_error err = uzlib_inflate(upng, inflated, inflated_size, idat_buffer, total_idat);
	free(idat_buffer);
	if (err != UPNG_EOK) {
		free(inflated);
		return err;
	}

	upng->data_size = linebytes * upng->height;
	upng->data = (unsigned char*)malloc(upng->data_size);
	if (!upng->data) {
		free(inflated);
		SET_ERROR(upng, UPNG_ENOMEM);
		return UPNG_ENOMEM;
	}

	err = unfilter(upng, upng->data, inflated, upng->width, upng->height, upng->bpp);
	free(inflated);
	return err;
}

unsigned int upng_get_width(const upng_t* upng) { return upng ? upng->width : 0; }
unsigned int upng_get_height(const upng_t* upng) { return upng ? upng->height : 0; }
unsigned int upng_get_bpp(const upng_t* upng) { return upng ? upng->bpp : 0; }
const unsigned char* upng_get_buffer(const upng_t* upng) { return upng ? upng->data : NULL; }
unsigned int upng_get_size(const upng_t* upng) { return upng ? upng->data_size : 0; }
upng_format upng_get_format(const upng_t* upng) { return upng ? upng->format : UPNG_BADFORMAT; }
upng_error upng_get_error(const upng_t* upng) { return upng ? upng->error : UPNG_EPARAM; }
