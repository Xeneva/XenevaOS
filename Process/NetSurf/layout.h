/**
 * Block/inline layout and painting for the XenevaOS NetSurf renderer.
 *
 * Takes the NsCssNode tree produced by dom.c, resolves libcss computed
 * styles into boxes, wraps inline text with Chitralekha font metrics and
 * paints the resulting display list.  Scrolling is in pixels.
 */

#ifndef NS_LAYOUT_H
#define NS_LAYOUT_H

#include <stdint.h>
#include <stddef.h>
#include "css.h"
#include <chitralekha.h>
#include <font.h>
#include <draw.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Decoded-image lookup.  The layout engine gives the callback the raw
 * src attribute; it should resolve it against the page URL and return
 * 0xAARRGGBB pixels plus the intrinsic size, or NULL when unavailable. */
typedef const uint32_t* (*NsImageLookupFn)(const char* src, int* width, int* height,
                                           int* intrinsic_width, int* intrinsic_height);
void NsLayoutSetImageLookup(NsImageLookupFn fn);

/* Inline <svg> elements have no fetchable src.  The host serialises the
 * subtree and rasterises it through the same SVG decoder used for files. */
typedef const uint32_t* (*NsInlineSvgLookupFn)(NsCssNode* node, int* width, int* height,
                                               int* intrinsic_width, int* intrinsic_height);
void NsLayoutSetInlineSvgLookup(NsInlineSvgLookupFn fn);

/* Build a layout for root.  viewport_w/h are the page widget's content
 * size in pixels.  Styles must still be alive (call before NsCssEnd). */
int NsLayoutBuild(NsCssNode* root, int viewport_w, int viewport_h);
void NsLayoutDestroy(void);

int NsLayoutActive(void);
int NsLayoutPageHeight(void);
int NsLayoutPageWidth(void);
int NsLayoutContentHeight(void);
/* Diagnostic counters for --selftest. */
void NsLayoutStats(int* blocks, int* lines, int* runs, int* links);
uint32_t NsLayoutPageBg(void);

/* Paint with the page coordinate origin at (vx,vy) and the given scroll
 * offset.  Everything is clipped to the viewport rectangle. */
void NsLayoutPaint(ChCanvas* canv, int vx, int vy, int vw, int vh, int scroll_y);

/* Hit-test a link.  x/y are page coordinates (caller adds scroll). */
int NsLayoutHitLink(int x, int y, char* href, size_t hrefsz);

/* Hit-test an interactive form control (input, textarea, button, select).
 * x/y are page coordinates (caller adds scroll). */
int NsLayoutHitControl(int x, int y, NsCssNode** node);

/* Live edit value painted over a form control.  node == NULL disables the
 * override.  Used while a field has keyboard focus. */
void NsLayoutSetEdit(NsCssNode* node, const char* text, int cursor, int active);

/* Plain text of the laid-out page, for --selftest and status output. */
const char* NsLayoutText(void);

/* Diagnostics: dump the first N link rects and their hit-test result. */
void NsLayoutPrintLinks(int max);
void NsLayoutDumpBoxes(void);
/* Paint into an offscreen Chitralekha canvas and count non-background
 * pixels.  Returns the count, or -1/-2 when no graph device is available. */
int NsLayoutPaintTest(int w, int h, int scroll);
/* Render to a PPM on the guest filesystem (used by the headless selftest). */
int NsLayoutDumpPpm(const char* path, int w, int h, int scroll);

#ifdef __cplusplus
}
#endif

#endif
