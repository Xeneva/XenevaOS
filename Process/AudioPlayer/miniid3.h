/*
 * miniid3.h -- minimal single-header ID3v2 album-art extractor
 *
 * Supports:
 *   ID3v2.2  frame "PIC"  (3-char ID, 3-byte big-endian size, 3-char image format)
 *   ID3v2.3  frame "APIC" (4-char ID, 4-byte big-endian size, MIME string)
 *   ID3v2.4  frame "APIC" (4-char ID, 4-byte synchsafe size -- with iTunes
 *                          broken-big-endian fallback)
 *   Extended header skipping (ID3v2.3 / v2.4 flag bit 6)
 *   UTF-16 / UTF-16BE description double-null termination
 *   ID3v2.4 data-length-indicator prefix (frame flag bit 0)
 *
 * Usage:
 *   #define MINIID3_IMPLEMENTATION   (in exactly ONE .c / .cpp)
 *   #include "miniid3.h"
 *
 *   uint8_t* imgBuf = NULL;
 *   size_t   imgLen = 0;
 *   if (miniid3_read_album_art("/music/song.mp3", &imgBuf, &imgLen)) {
 *       // imgBuf contains raw JPEG/PNG bytes
 *       free(imgBuf);
 *   }
 *
 * Public domain -- no warranty, use at your own risk.
 */

#ifndef MINIID3_H
#define MINIID3_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * miniid3_read_album_art
 *
 * Opens mp3_path, locates the first APIC (or PIC in v2.2) frame, and returns
 * a malloc'd buffer containing the raw image bytes (JPEG or PNG).
 *
 * Returns 1 on success, 0 if no art was found or on any error.
 * On success *out_buf must be free()'d by the caller.
 */
int miniid3_read_album_art(const char* mp3_path,
                           uint8_t**   out_buf,
                           size_t*     out_len);

#ifdef __cplusplus
}
#endif

#endif /* MINIID3_H */

/* =========================================================================
   IMPLEMENTATION
   ========================================================================= */
#ifdef MINIID3_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- internal helpers -------------------------------------------------- */

static uint32_t _id3_synchsafe(const uint8_t* p) {
    return ((uint32_t)(p[0] & 0x7f) << 21)
         | ((uint32_t)(p[1] & 0x7f) << 14)
         | ((uint32_t)(p[2] & 0x7f) <<  7)
         |  (uint32_t)(p[3] & 0x7f);
}

static uint32_t _id3_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24)
         | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] <<  8)
         |  (uint32_t)p[3];
}

static uint32_t _id3_be24(const uint8_t* p) {
    return ((uint32_t)p[0] << 16)
         | ((uint32_t)p[1] <<  8)
         |  (uint32_t)p[2];
}

/*
 * _id3_is_valid_jpeg -- check for JPEG SOI marker (0xFF 0xD8)
 * Most embedded album art is JPEG; this is a quick sanity check before
 * handing data to the JPEG decoder so we don't crash on garbage.
 */
static int _id3_is_valid_image(const uint8_t* p, size_t len) {
    if (len < 4) return 0;
    /* JPEG: FF D8 FF */
    if (p[0] == 0xFF && p[1] == 0xD8 && p[2] == 0xFF) return 1;
    /* PNG:  89 50 4E 47 */
    if (p[0] == 0x89 && p[1] == 0x50 && p[2] == 0x4E && p[3] == 0x47) return 1;
    return 0;
}

/*
 * _id3_skip_desc -- advance *pp past the description field.
 *   encoding 0/3 (Latin-1 / UTF-8)   → single null terminator
 *   encoding 1/2 (UTF-16 / UTF-16BE) → double null terminator (0x00 0x00)
 * Returns 1 if a valid terminator was found before fend.
 */
static int _id3_skip_desc(uint8_t encoding, uint8_t** pp, const uint8_t* fend) {
    uint8_t* p = *pp;
    if (encoding == 0x01 || encoding == 0x02) {
        /* UTF-16: step 2 bytes at a time looking for 0x00 0x00 */
        while (p + 1 < fend) {
            if (p[0] == 0 && p[1] == 0) { p += 2; break; }
            p += 2;
        }
    } else {
        /* Latin-1 / UTF-8: single null */
        while (p < fend && *p) p++;
        if (p < fend) p++; /* skip the null itself */
    }
    *pp = p;
    return (p <= fend);
}

/* =========================================================================
   Public API
   ========================================================================= */
int miniid3_read_album_art(const char* mp3_path,
                           uint8_t**   out_buf,
                           size_t*     out_len) {
    *out_buf = NULL;
    *out_len = 0;

    FILE* f = fopen(mp3_path, "rb");
    if (!f) return 0;

    /* ---- 10-byte ID3v2 main header ---- */
    uint8_t hdr[10];
    if (fread(hdr, 1, 10, f) != (size_t)10) { fclose(f); return 0; }

    /* Verify "ID3" magic */
    if (hdr[0] != 'I' || hdr[1] != 'D' || hdr[2] != '3') { fclose(f); return 0; }

    uint8_t ver   = hdr[3]; /* major version: 2, 3, or 4 */
    uint8_t flags = hdr[5];

    /* Tag body size is always synchsafe in all ID3v2.x */
    uint32_t tag_size = _id3_synchsafe(hdr + 6);
    if (tag_size == 0) { fclose(f); return 0; }

    uint8_t* tag = (uint8_t*)malloc(tag_size);
    if (!tag) { fclose(f); return 0; }

    if (fread(tag, 1, tag_size, f) != (size_t)tag_size) {
        free(tag); fclose(f); return 0;
    }
    fclose(f);

    uint32_t off = 0;

    /* ---- Skip extended header (v2.3 / v2.4, flag bit 6) ---- */
    if (ver >= 3 && (flags & 0x40) && off + 4 <= tag_size) {
        uint32_t ext_size;
        if (ver >= 4) {
            /* v2.4: synchsafe size *includes* the 4 size bytes */
            ext_size = _id3_synchsafe(tag + off);
            off += ext_size;
        } else {
            /* v2.3: plain big-endian size, does NOT include the 4 bytes */
            ext_size = _id3_be32(tag + off);
            off += 4 + ext_size;
        }
        if (off > tag_size) { free(tag); return 0; }
    }

    /* ---- Per-version frame header geometry ----
     * ID3v2.2:  [3-char ID][3-byte size]          = 6 bytes, no flags
     * ID3v2.3+: [4-char ID][4-byte size][2 flags] = 10 bytes            */
    uint32_t fhdr_sz = (ver == 2) ? 6 : 10;

    int found = 0;

    while (!found && off + fhdr_sz <= tag_size) {
        uint8_t* fh = tag + off;

        /* Padding / end of frames */
        if (fh[0] == 0) break;

        uint32_t fsize  = 0;
        int      is_pic = 0;

        if (ver == 2) {
            /* ID3v2.2 */
            fsize  = _id3_be24(fh + 3);
            is_pic = (fh[0]=='P' && fh[1]=='I' && fh[2]=='C');
        } else {
            /* ID3v2.3 / v2.4
             *
             * IMPORTANT: Many taggers (iTunes, TagLib pre-2014, fre:ac) write
             * ID3v2.4 files but store frame sizes as plain big-endian instead
             * of synchsafe integers.  We try synchsafe first; if that puts us
             * past the tag boundary we fall back to plain big-endian.         */
            if (ver >= 4) {
                fsize = _id3_synchsafe(fh + 4);
                if (off + fhdr_sz + fsize > tag_size) {
                    /* Synchsafe size is invalid -- try big-endian fallback */
                    fsize = _id3_be32(fh + 4);
                }
            } else {
                fsize = _id3_be32(fh + 4);
            }
            is_pic = (fh[0]=='A' && fh[1]=='P' && fh[2]=='I' && fh[3]=='C');
        }

        /* Sanity: frame must fit inside the tag */
        if (off + fhdr_sz + fsize > tag_size) break;

        if (is_pic) {
            uint8_t* fdata = fh + fhdr_sz;
            uint8_t* fend  = fdata + fsize;
            uint8_t* p     = fdata;

            /* ID3v2.4: data-length-indicator flag (frame flag byte [1], bit 0)
             * When set, 4 synchsafe bytes of original data length prefix the
             * actual APIC payload -- skip them.                                */
            if (ver >= 4 && fhdr_sz == 10 && (fh[9] & 0x01)) {
                p += 4;
            }

            if (p >= fend) break;

            /* ---- Parse APIC / PIC body ----
             *
             * ID3v2.3/v2.4 APIC:
             *   [1]  text encoding  (0=Latin1, 1=UTF16, 2=UTF16BE, 3=UTF8)
             *   [n]  MIME type, null-terminated ASCII  e.g. "image/jpeg\0"
             *   [1]  picture type  (0x03 = front cover, but accept any)
             *   [m]  description, null (or double-null) terminated
             *   [?]  image data
             *
             * ID3v2.2 PIC:
             *   [1]  text encoding
             *   [3]  image format  ("JPG" or "PNG", NOT null-terminated)
             *   [1]  picture type
             *   [m]  description
             *   [?]  image data                                                */

            uint8_t encoding = *p++;  /* save encoding for description field */

            if (ver == 2) {
                /* skip fixed 3-char format string */
                p += 3;
            } else {
                /* skip variable-length null-terminated MIME type */
                while (p < fend && *p) p++;
                if (p < fend) p++; /* skip the null */
            }

            if (p >= fend) break;
            p++; /* picture type byte */

            /* skip description (encoding-aware null termination) */
            if (!_id3_skip_desc(encoding, &p, fend)) break;

            size_t img_len = (size_t)(fend - p);

            /* Validate image signature before accepting */
            if (img_len > 0 && _id3_is_valid_image(p, img_len)) {
                uint8_t* img = (uint8_t*)malloc(img_len);
                if (img) {
                    memcpy(img, p, img_len);
                    *out_buf = img;
                    *out_len = img_len;
                    found    = 1;
                }
            } else if (img_len > 0) {
                /* Image signature check failed.
                 * Some taggers prepend a UTF-16 BOM (0xFF 0xFE or 0xFE 0xFF)
                 * to the image data by mistake.  Try skipping 2 bytes.       */
                if (img_len > 2 && _id3_is_valid_image(p + 2, img_len - 2)) {
                    p       += 2;
                    img_len -= 2;
                    uint8_t* img = (uint8_t*)malloc(img_len);
                    if (img) {
                        memcpy(img, p, img_len);
                        *out_buf = img;
                        *out_len = img_len;
                        found    = 1;
                    }
                }
            }
            break; /* APIC found -- stop regardless of whether we got data */
        }

        off += fhdr_sz + fsize;
    }

    free(tag);
    return found;
}

#endif /* MINIID3_IMPLEMENTATION */

