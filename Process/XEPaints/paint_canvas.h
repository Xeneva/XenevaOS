#ifndef __PAINT_CANVAS_H__
#define __PAINT_CANVAS_H__

#include <widgets/base.h>
#include <widgets/window.h>
#include "paint_types.h"

struct PaintCanvas {
    ChWidget base;

    uint32_t* buffer;           // Main committed pixel canvas [CANVAS_W * CANVAS_H]
    uint32_t* previewBuffer;    // Live shape rubber-band preview buffer
    uint32_t* undoBuffers[MAX_UNDO_LEVELS];
    int undoCount;
    uint32_t* redoBuffers[MAX_UNDO_LEVELS];
    int redoCount;

    PaintTool currentTool;
    PaintTool previousTool;
    int strokeWidth;
    uint32_t primaryColor;      // Color 1
    uint32_t secondaryColor;    // Color 2

    bool isDrawing;
    int mouseButton;            // 1 = left, 2 = right
    int startX;
    int startY;
    int lastX;
    int lastY;
    int currentX;
    int currentY;

    bool shiftPressed;
    PaintRect prevShapeRect;
};

PaintCanvas* CreatePaintCanvas(int x, int y, int w, int h);
void DestroyPaintCanvas(PaintCanvas* canvas);

void PaintCanvasPushUndo(PaintCanvas* c);
void PaintCanvasUndo(PaintCanvas* c);
void PaintCanvasRedo(PaintCanvas* c);

void PaintCanvasClear(PaintCanvas* c, uint32_t color);
void PaintCanvasInvert(PaintCanvas* c);

#endif
