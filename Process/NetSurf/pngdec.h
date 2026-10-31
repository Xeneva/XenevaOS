#ifndef NS_PNGDEC_H
#define NS_PNGDEC_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Returns 0 and a malloc'd 0xAARRGGBB buffer. Caller frees *out_px. */
int ns_png_decode(const unsigned char* data, size_t len, unsigned** out_px, int* out_w, int* out_h);
#ifdef __cplusplus
}
#endif
#endif
