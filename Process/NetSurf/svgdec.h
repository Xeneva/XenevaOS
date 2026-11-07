#ifndef NS_SVGDEC_H
#define NS_SVGDEC_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Rasterize an SVG buffer. *out_px is malloc'd 0xAARRGGBB. Returns 0 on success. */
int ns_svg_decode(char* data, size_t len, unsigned** out_px, int* out_w, int* out_h,
                  int* out_iw, int* out_ih);
#ifdef __cplusplus
}
#endif
#endif
