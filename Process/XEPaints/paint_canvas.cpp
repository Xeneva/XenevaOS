#include "paint_canvas.h"
#include "paint_tools.h"
#include "paint_ui.h"
#include <stdlib.h>
#include <string.h>

static inline int _Min(int a, int b) { return (a < b) ? a : b; }
static inline int _Max(int a, int b) { return (a > b) ? a : b; }

// Full canvas blit handler for initial paint, window resize, and full operations (Undo, Redo, Clear)
static void _CanvasPaint(ChWidget* wid, ChWindow* win) {
    PaintCanvas* c = (PaintCanvas*)wid;
    if (!c || !c->buffer || !win || !win->canv || !win->canv->buffer) return;

    const uint32_t* src = c->buffer;
    uint32_t* dst = win->canv->buffer;
    int canvW = (int)win->canv->canvasWidth;
    int canvH = (int)win->canv->canvasHeight;
    int copyW = wid->w;
    int copyH = wid->h;

    for (int y = 0; y < copyH; ++y) {
        int dstY = wid->y + y;
        if (dstY < 0 || dstY >= canvH) continue;

        const uint32_t* srcRow = src + (y * copyW);
        uint32_t* dstRow = dst + (dstY * canvW + wid->x);
        memcpy(dstRow, (void*)srcRow, copyW * sizeof(uint32_t));
    }

    // Outer border around canvas
    ChDrawRectUnfilled(win->canv, wid->x - 1, wid->y - 1, wid->w + 2, wid->h + 2, 0xFF444444);
}

// Ultra-fast sub-rectangle dirty blit: copies ONLY modified pixels to the window buffer
static void _BlitDirtyRect(PaintCanvas* c, ChWindow* win, int x0, int y0, int x1, int y1, bool isPreview) {
    if (!c || !win || !win->canv || !win->canv->buffer) return;

    int minX = _Max(0, _Min(x0, x1));
    int maxX = _Min(c->base.w - 1, _Max(x0, x1));
    int minY = _Max(0, _Min(y0, y1));
    int maxY = _Min(c->base.h - 1, _Max(y0, y1));

    if (minX > maxX || minY > maxY) return;

    int dirtyW = maxX - minX + 1;
    int dirtyH = maxY - minY + 1;

    const uint32_t* src = isPreview ? c->previewBuffer : c->buffer;
    uint32_t* dst = win->canv->buffer;
    int canvW = (int)win->canv->canvasWidth;
    int canvH = (int)win->canv->canvasHeight;
    int copyW = c->base.w;

    for (int y = minY; y <= maxY; ++y) {
        int dstY = c->base.y + y;
        if (dstY < 0 || dstY >= canvH) continue;

        const uint32_t* srcRow = src + (y * copyW + minX);
        uint32_t* dstRow = dst + (dstY * canvW + (c->base.x + minX));
        memcpy(dstRow, (void*)srcRow, dirtyW * sizeof(uint32_t));
    }

    ChWindowUpdate(win, c->base.x + minX, c->base.y + minY, dirtyW, dirtyH, false, true);
}

static int _GetEraserSize(int strokeWidth) {
    if (strokeWidth <= 1) return 8;
    if (strokeWidth == 2) return 14;
    if (strokeWidth == 4) return 22;
    if (strokeWidth == 8) return 34;
    return 48;
}

static PaintRect _ComputeShapeBoundingBox(int x0, int y0, int x1, int y1, int strokeWidth, bool isCircle, bool shiftPressed) {
    PaintRect r;
    int pad = strokeWidth + 4;
    if (isCircle) {
        int rx = abs(x1 - x0) / 2;
        int ry = abs(y1 - y0) / 2;
        if (shiftPressed) {
            rx = _Max(rx, ry);
            ry = rx;
        }
        int cx = (x0 + x1) / 2;
        int cy = (y0 + y1) / 2;
        r.x = cx - rx - pad;
        r.y = cy - ry - pad;
        r.w = (rx * 2) + (pad * 2) + 1;
        r.h = (ry * 2) + (pad * 2) + 1;
    } else {
        int minX = _Min(x0, x1) - pad;
        int maxX = _Max(x0, x1) + pad;
        int minY = _Min(y0, y1) - pad;
        int maxY = _Max(y0, y1) + pad;
        r.x = minX;
        r.y = minY;
        r.w = maxX - minX + 1;
        r.h = maxY - minY + 1;
    }
    return r;
}

static PaintRect _UnionRect(PaintRect a, PaintRect b) {
    if (a.w <= 0 || a.h <= 0) return b;
    if (b.w <= 0 || b.h <= 0) return a;
    int minX = _Min(a.x, b.x);
    int minY = _Min(a.y, b.y);
    int maxX = _Max(a.x + a.w, b.x + b.w);
    int maxY = _Max(a.y + a.h, b.y + b.h);
    PaintRect u;
    u.x = minX;
    u.y = minY;
    u.w = maxX - minX;
    u.h = maxY - minY;
    return u;
}

static void _RenderLiveShape(PaintCanvas* c, uint32_t* targetBuf,
                             int x0, int y0, int x1, int y1,
                             uint32_t primaryCol, uint32_t secondaryCol) {
    int w = c->strokeWidth;
    switch (c->currentTool) {
    case TOOL_LINE:
        PaintDrawBresenhamLine(targetBuf, CANVAS_W, CANVAS_H, x0, y0, x1, y1, w, primaryCol);
        break;
    case TOOL_RECTANGLE:
        PaintDrawRectOutline(targetBuf, CANVAS_W, CANVAS_H, x0, y0, x1, y1, w, primaryCol);
        break;
    case TOOL_FILLED_RECTANGLE:
        PaintDrawRectFilled(targetBuf, CANVAS_W, CANVAS_H, x0, y0, x1, y1, secondaryCol, primaryCol, w);
        break;
    case TOOL_CIRCLE:
        PaintDrawMidpointCircle(targetBuf, CANVAS_W, CANVAS_H, x0, y0, x1, y1, w, primaryCol, c->shiftPressed);
        break;
    case TOOL_FILLED_CIRCLE:
        PaintDrawFilledCircle(targetBuf, CANVAS_W, CANVAS_H, x0, y0, x1, y1, secondaryCol, primaryCol, w, c->shiftPressed);
        break;
    default:
        break;
    }
}

static void _CanvasMouse(ChWidget* wid, ChWindow* win, int x, int y, int button) {
    PaintCanvas* c = (PaintCanvas*)wid;
    if (!c || !c->buffer || !win || !win->info) return;

    int cx = x - win->info->x - wid->x;
    int cy = y - win->info->y - wid->y;

    if (cx < 0) cx = 0;
    if (cx >= wid->w) cx = wid->w - 1;
    if (cy < 0) cy = 0;
    if (cy >= wid->h) cy = wid->h - 1;

    c->currentX = cx;
    c->currentY = cy;

    // Hover optimization: if mouse button is up and not drawing, skip all blits
    if (!c->isDrawing && (button == 0 || wid->KillFocus)) {
        return;
    }

    if (button == 1 || button == 2) {
        uint32_t activeCol = (button == 1) ? c->primaryColor : c->secondaryColor;
        uint32_t altCol = (button == 1) ? c->secondaryColor : c->primaryColor;

        if (!c->isDrawing) {
            c->isDrawing = true;
            c->mouseButton = button;
            c->startX = cx;
            c->startY = cy;
            c->lastX = cx;
            c->lastY = cy;
            c->prevShapeRect.x = 0;
            c->prevShapeRect.y = 0;
            c->prevShapeRect.w = 0;
            c->prevShapeRect.h = 0;

            win->focusedWidget = (void*)wid;

            if (c->currentTool == TOOL_EYEDROPPER) {
                uint32_t sampled = PaintGetPixel(c->buffer, wid->w, wid->h, cx, cy);
                if (button == 1) {
                    c->primaryColor = sampled;
                } else {
                    c->secondaryColor = sampled;
                }
                c->currentTool = (c->previousTool != TOOL_EYEDROPPER) ? c->previousTool : TOOL_BRUSH;
                c->isDrawing = false;
                win->focusedWidget = NULL;
                PaintUIRefreshTools(win);
                PaintUIRefreshStatus(win);
                return;
            }

            PaintCanvasPushUndo(c);

            if (c->currentTool == TOOL_FLOOD_FILL) {
                PaintScanlineFloodFill(c->buffer, wid->w, wid->h, cx, cy, activeCol);
                c->isDrawing = false;
                win->focusedWidget = NULL;
                _BlitDirtyRect(c, win, 0, 0, wid->w - 1, wid->h - 1, false);
                return;
            }

            if (c->currentTool == TOOL_PENCIL) {
                PaintSetPixel(c->buffer, wid->w, wid->h, cx, cy, activeCol);
                _BlitDirtyRect(c, win, cx - 1, cy - 1, cx + 1, cy + 1, false);
            } else if (c->currentTool == TOOL_BRUSH) {
                int r = (c->strokeWidth / 2) + 1;
                PaintStampDisc(c->buffer, wid->w, wid->h, cx, cy, r, activeCol);
                _BlitDirtyRect(c, win, cx - r - 1, cy - r - 1, cx + r + 1, cy + r + 1, false);
            } else if (c->currentTool == TOOL_ERASER) {
                int eSize = _GetEraserSize(c->strokeWidth);
                int half = (eSize / 2) + 1;
                PaintStampSquare(c->buffer, wid->w, wid->h, cx, cy, eSize, c->secondaryColor);
                _BlitDirtyRect(c, win, cx - half - 1, cy - half - 1, cx + half + 1, cy + half + 1, false);
            } else {
                bool isCircle = (c->currentTool == TOOL_CIRCLE || c->currentTool == TOOL_FILLED_CIRCLE);
                c->prevShapeRect = _ComputeShapeBoundingBox(cx, cy, cx, cy, c->strokeWidth, isCircle, c->shiftPressed);
            }
        } else {
            // Dragging continuation: incremental bounding-box blits
            if (c->currentTool == TOOL_PENCIL) {
                int minX = _Min(c->lastX, cx) - 1;
                int maxX = _Max(c->lastX, cx) + 1;
                int minY = _Min(c->lastY, cy) - 1;
                int maxY = _Max(c->lastY, cy) + 1;
                PaintDrawBresenhamLine(c->buffer, wid->w, wid->h, c->lastX, c->lastY, cx, cy, 1, activeCol);
                c->lastX = cx;
                c->lastY = cy;
                _BlitDirtyRect(c, win, minX, minY, maxX, maxY, false);
            } else if (c->currentTool == TOOL_BRUSH) {
                int r = (c->strokeWidth / 2) + 1;
                int minX = _Min(c->lastX, cx) - r;
                int maxX = _Max(c->lastX, cx) + r;
                int minY = _Min(c->lastY, cy) - r;
                int maxY = _Max(c->lastY, cy) + r;
                PaintDrawBresenhamLine(c->buffer, wid->w, wid->h, c->lastX, c->lastY, cx, cy, c->strokeWidth, activeCol);
                c->lastX = cx;
                c->lastY = cy;
                _BlitDirtyRect(c, win, minX, minY, maxX, maxY, false);
            } else if (c->currentTool == TOOL_ERASER) {
                int eSize = _GetEraserSize(c->strokeWidth);
                int r = (eSize / 2) + 1;
                int minX = _Min(c->lastX, cx) - r;
                int maxX = _Max(c->lastX, cx) + r;
                int minY = _Min(c->lastY, cy) - r;
                int maxY = _Max(c->lastY, cy) + r;
                PaintEraseLine(c->buffer, wid->w, wid->h, c->lastX, c->lastY, cx, cy, eSize, c->secondaryColor);
                c->lastX = cx;
                c->lastY = cy;
                _BlitDirtyRect(c, win, minX, minY, maxX, maxY, false);
            } else {
                // Shape preview: restore ONLY the dirty union region
                bool isCircle = (c->currentTool == TOOL_CIRCLE || c->currentTool == TOOL_FILLED_CIRCLE);
                PaintRect curRect = _ComputeShapeBoundingBox(c->startX, c->startY, cx, cy, c->strokeWidth, isCircle, c->shiftPressed);
                PaintRect dirtyUnion = _UnionRect(curRect, c->prevShapeRect);

                int clMinX = _Max(0, dirtyUnion.x);
                int clMaxX = _Min(wid->w - 1, dirtyUnion.x + dirtyUnion.w);
                int clMinY = _Max(0, dirtyUnion.y);
                int clMaxY = _Min(wid->h - 1, dirtyUnion.y + dirtyUnion.h);
                int restoreW = clMaxX - clMinX + 1;

                if (restoreW > 0) {
                    for (int ry = clMinY; ry <= clMaxY; ++ry) {
                        memcpy(c->previewBuffer + (ry * wid->w + clMinX),
                               c->buffer + (ry * wid->w + clMinX),
                               restoreW * sizeof(uint32_t));
                    }
                }

                _RenderLiveShape(c, c->previewBuffer, c->startX, c->startY, cx, cy, activeCol, altCol);
                _BlitDirtyRect(c, win, clMinX, clMinY, clMaxX, clMaxY, true);
                c->prevShapeRect = curRect;
            }
        }
    } else if (button == 0) {
        if (c->isDrawing) {
            uint32_t activeCol = (c->mouseButton == 1) ? c->primaryColor : c->secondaryColor;
            uint32_t altCol = (c->mouseButton == 1) ? c->secondaryColor : c->primaryColor;

            if (c->currentTool == TOOL_LINE || c->currentTool == TOOL_RECTANGLE ||
                c->currentTool == TOOL_FILLED_RECTANGLE || c->currentTool == TOOL_CIRCLE ||
                c->currentTool == TOOL_FILLED_CIRCLE) {
                _RenderLiveShape(c, c->buffer, c->startX, c->startY, cx, cy, activeCol, altCol);
                bool isCircle = (c->currentTool == TOOL_CIRCLE || c->currentTool == TOOL_FILLED_CIRCLE);
                PaintRect finalRect = _ComputeShapeBoundingBox(c->startX, c->startY, cx, cy, c->strokeWidth, isCircle, c->shiftPressed);
                PaintRect dirtyUnion = _UnionRect(finalRect, c->prevShapeRect);
                _BlitDirtyRect(c, win, dirtyUnion.x, dirtyUnion.y, dirtyUnion.x + dirtyUnion.w, dirtyUnion.y + dirtyUnion.h, false);
            }

            c->isDrawing = false;
            win->focusedWidget = NULL;
            c->prevShapeRect.x = 0;
            c->prevShapeRect.y = 0;
            c->prevShapeRect.w = 0;
            c->prevShapeRect.h = 0;

            // Trigger status bar update after drawing stroke finishes
            PaintUIRefreshStatus(win);
        }
    }
}

PaintCanvas* CreatePaintCanvas(int x, int y, int w, int h) {
    PaintCanvas* c = (PaintCanvas*)malloc(sizeof(PaintCanvas));
    if (!c) return NULL;
    memset(c, 0, sizeof(PaintCanvas));

    c->base.x = x;
    c->base.y = y;
    c->base.w = w;
    c->base.h = h;
    c->base.ChPaintHandler = _CanvasPaint;
    c->base.ChMouseEvent = _CanvasMouse;

    size_t bufBytes = w * h * sizeof(uint32_t);
    c->buffer = (uint32_t*)malloc(bufBytes);
    c->previewBuffer = (uint32_t*)malloc(bufBytes);

    for (int i = 0; i < MAX_UNDO_LEVELS; ++i) {
        c->undoBuffers[i] = (uint32_t*)malloc(bufBytes);
        c->redoBuffers[i] = (uint32_t*)malloc(bufBytes);
        if (c->undoBuffers[i]) memset(c->undoBuffers[i], 0xFF, bufBytes);
        if (c->redoBuffers[i]) memset(c->redoBuffers[i], 0xFF, bufBytes);
    }

    if (c->buffer) {
        for (int i = 0; i < w * h; ++i) c->buffer[i] = 0xFFFFFFFF; // Pure white canvas
    }
    if (c->previewBuffer) {
        memcpy(c->previewBuffer, c->buffer, bufBytes);
    }

    c->currentTool = TOOL_PENCIL;
    c->previousTool = TOOL_PENCIL;
    c->strokeWidth = 2;
    c->primaryColor = 0xFF000000;   // Black default foreground
    c->secondaryColor = 0xFFFFFFFF; // White default background
    c->undoCount = 0;
    c->redoCount = 0;

    return c;
}

void DestroyPaintCanvas(PaintCanvas* c) {
    if (!c) return;
    if (c->buffer) free(c->buffer);
    if (c->previewBuffer) free(c->previewBuffer);
    for (int i = 0; i < MAX_UNDO_LEVELS; ++i) {
        if (c->undoBuffers[i]) free(c->undoBuffers[i]);
        if (c->redoBuffers[i]) free(c->redoBuffers[i]);
    }
    free(c);
}

void PaintCanvasPushUndo(PaintCanvas* c) {
    if (!c || !c->buffer) return;
    size_t bufBytes = c->base.w * c->base.h * sizeof(uint32_t);

    if (c->undoCount >= MAX_UNDO_LEVELS) {
        uint32_t* oldest = c->undoBuffers[0];
        for (int i = 0; i < MAX_UNDO_LEVELS - 1; ++i) {
            c->undoBuffers[i] = c->undoBuffers[i + 1];
        }
        c->undoBuffers[MAX_UNDO_LEVELS - 1] = oldest;
        memcpy(c->undoBuffers[MAX_UNDO_LEVELS - 1], c->buffer, bufBytes);
    } else {
        memcpy(c->undoBuffers[c->undoCount], c->buffer, bufBytes);
        c->undoCount++;
    }
    c->redoCount = 0; // New action clears redo history
}

void PaintCanvasUndo(PaintCanvas* c) {
    if (!c || !c->buffer || c->undoCount <= 0) return;
    size_t bufBytes = c->base.w * c->base.h * sizeof(uint32_t);

    // Save current state to redo stack
    if (c->redoCount >= MAX_UNDO_LEVELS) {
        uint32_t* oldest = c->redoBuffers[0];
        for (int i = 0; i < MAX_UNDO_LEVELS - 1; ++i) {
            c->redoBuffers[i] = c->redoBuffers[i + 1];
        }
        c->redoBuffers[MAX_UNDO_LEVELS - 1] = oldest;
        memcpy(c->redoBuffers[MAX_UNDO_LEVELS - 1], c->buffer, bufBytes);
    } else {
        memcpy(c->redoBuffers[c->redoCount], c->buffer, bufBytes);
        c->redoCount++;
    }

    // Pop from undo stack into active buffer
    c->undoCount--;
    memcpy(c->buffer, c->undoBuffers[c->undoCount], bufBytes);
}

void PaintCanvasRedo(PaintCanvas* c) {
    if (!c || !c->buffer || c->redoCount <= 0) return;
    size_t bufBytes = c->base.w * c->base.h * sizeof(uint32_t);

    // Save current state to undo stack
    if (c->undoCount >= MAX_UNDO_LEVELS) {
        uint32_t* oldest = c->undoBuffers[0];
        for (int i = 0; i < MAX_UNDO_LEVELS - 1; ++i) {
            c->undoBuffers[i] = c->undoBuffers[i + 1];
        }
        c->undoBuffers[MAX_UNDO_LEVELS - 1] = oldest;
        memcpy(c->undoBuffers[MAX_UNDO_LEVELS - 1], c->buffer, bufBytes);
    } else {
        memcpy(c->undoBuffers[c->undoCount], c->buffer, bufBytes);
        c->undoCount++;
    }

    // Pop from redo stack into active buffer
    c->redoCount--;
    memcpy(c->buffer, c->redoBuffers[c->redoCount], bufBytes);
}

void PaintCanvasClear(PaintCanvas* c, uint32_t color) {
    if (!c || !c->buffer) return;
    PaintCanvasPushUndo(c);
    PaintClearCanvas(c->buffer, c->base.w, c->base.h, color);
}

void PaintCanvasInvert(PaintCanvas* c) {
    if (!c || !c->buffer) return;
    PaintCanvasPushUndo(c);
    PaintInvertColors(c->buffer, c->base.w, c->base.h);
}
