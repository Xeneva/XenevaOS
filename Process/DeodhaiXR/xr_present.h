#ifndef DEODHAI_XR_PRESENT_H
#define DEODHAI_XR_PRESENT_H

#include <chitralekha.h>

#ifdef __XENEVA_OPENXR__
void XrPresentInit(ChCanvas* canv);
void XrPresentFrame(ChCanvas* canv);
void XrPresentShutdown(void);
int XrPresentEnabled(void);
#else
static inline void XrPresentInit(ChCanvas* canv) { (void)canv; }
static inline void XrPresentFrame(ChCanvas* canv) { (void)canv; }
static inline void XrPresentShutdown(void) {}
static inline int XrPresentEnabled(void) { return 0; }
#endif

#endif
