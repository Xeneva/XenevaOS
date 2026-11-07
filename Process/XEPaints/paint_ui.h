#ifndef __PAINT_UI_H__
#define __PAINT_UI_H__

#include <widgets/window.h>
#include "paint_canvas.h"

void PaintUIInitialize(ChWindow* win, PaintCanvas* canvas);
void PaintUIRefreshStatus(ChWindow* win);
void PaintUIRefreshCanvas(void);
void PaintUIRefreshTools(ChWindow* win);
void PaintUIRefreshSizes(ChWindow* win);

#endif
