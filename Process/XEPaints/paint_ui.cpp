#include "paint_ui.h"
#include "paint_fs.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static PaintCanvas* s_canvas = NULL;
static ChWindow* s_win = NULL;
static ChWidget* s_bottomBar = NULL;

struct ToolBtn {
    ChWidget base;
    PaintTool tool;
    const char* label;
};

struct SizeBtn {
    ChWidget base;
    int size;
    const char* label;
};

struct ActionBtn {
    ChWidget base;
    const char* label;
    void (*action)(void);
};

static ToolBtn* s_toolBtns[TOOL_COUNT];
static SizeBtn* s_sizeBtns[5];
static int s_sizeBtnCount = 0;

void PaintUIRefreshTools(ChWindow* win) {
    for (int i = 0; i < TOOL_COUNT; ++i) {
        if (s_toolBtns[i] && s_toolBtns[i]->base.ChPaintHandler) {
            s_toolBtns[i]->base.ChPaintHandler(&s_toolBtns[i]->base, win);
            ChWindowUpdate(win, s_toolBtns[i]->base.x, s_toolBtns[i]->base.y,
                           s_toolBtns[i]->base.w, s_toolBtns[i]->base.h, false, true);
        }
    }
}

void PaintUIRefreshSizes(ChWindow* win) {
    for (int i = 0; i < s_sizeBtnCount; ++i) {
        if (s_sizeBtns[i] && s_sizeBtns[i]->base.ChPaintHandler) {
            s_sizeBtns[i]->base.ChPaintHandler(&s_sizeBtns[i]->base, win);
            ChWindowUpdate(win, s_sizeBtns[i]->base.x, s_sizeBtns[i]->base.y,
                           s_sizeBtns[i]->base.w, s_sizeBtns[i]->base.h, false, true);
        }
    }
}

void PaintUIRefreshCanvas(void) {
    if (s_canvas && s_win) {
        if (s_canvas->base.ChPaintHandler) {
            s_canvas->base.ChPaintHandler(&s_canvas->base, s_win);
        }
        ChWindowUpdate(s_win, s_canvas->base.x, s_canvas->base.y,
                       s_canvas->base.w, s_canvas->base.h, false, true);
    }
}

static void _ToolBtnPaint(ChWidget* wid, ChWindow* win) {
    ToolBtn* btn = (ToolBtn*)wid;
    bool isActive = (s_canvas && s_canvas->currentTool == btn->tool);

    uint32_t bg;
    if (isActive) {
        bg = 0xFF007ACC; // Bright active highlight
    } else if (wid->clicked) {
        bg = 0xFF1C1C1C;
    } else if (wid->hover) {
        bg = 0xFF454545;
    } else {
        bg = 0xFF303030;
    }

    ChDrawRect(win->canv, wid->x, wid->y, wid->w, wid->h, bg);
    uint32_t borderCol = isActive ? 0xFF00BFFF : (wid->hover ? 0xFF888888 : 0xFF505050);
    ChDrawRectUnfilled(win->canv, wid->x, wid->y, wid->w, wid->h, borderCol);

    int cx = wid->x + wid->w / 2;
    int cy = wid->y + wid->h / 2;
    uint32_t fg = 0xFFFFFFFF;

    switch (btn->tool) {
    case TOOL_PENCIL:
        ChDrawLine(win->canv, cx - 6, cy + 6, cx + 5, cy - 5, fg);
        ChDrawLine(win->canv, cx - 5, cy + 7, cx + 6, cy - 4, fg);
        ChDrawLine(win->canv, cx - 7, cy + 5, cx + 4, cy - 6, fg);
        ChDrawFilledCircle(win->canv, cx - 6, cy + 6, 1, 0xFFFFAA33);
        break;
    case TOOL_BRUSH:
        ChDrawLine(win->canv, cx - 6, cy + 6, cx, cy, 0xFFD2B48C);
        ChDrawLine(win->canv, cx - 5, cy + 7, cx + 1, cy + 1, 0xFFD2B48C);
        ChDrawFilledCircle(win->canv, cx + 3, cy - 3, 4, 0xFF00D2FF);
        break;
    case TOOL_ERASER:
        ChDrawRect(win->canv, cx - 7, cy - 5, 14, 10, 0xFFF0F0F0);
        ChDrawRect(win->canv, cx - 7, cy - 5, 5, 10, 0xFFFF7085);
        ChDrawRectUnfilled(win->canv, cx - 7, cy - 5, 14, 10, 0xFF303030);
        ChDrawLine(win->canv, cx - 2, cy - 5, cx - 2, cy + 4, 0xFF505050);
        break;
    case TOOL_FLOOD_FILL:
        ChDrawLine(win->canv, cx - 7, cy - 3, cx + 2, cy - 3, fg);
        ChDrawLine(win->canv, cx - 5, cy + 4, cx, cy + 4, fg);
        ChDrawLine(win->canv, cx - 7, cy - 3, cx - 5, cy + 4, fg);
        ChDrawLine(win->canv, cx + 2, cy - 3, cx, cy + 4, fg);
        ChDrawFilledCircle(win->canv, cx + 5, cy + 4, 3, 0xFF00BFFF);
        ChDrawLine(win->canv, cx + 2, cy - 1, cx + 5, cy + 2, 0xFF00BFFF);
        break;
    case TOOL_LINE:
        ChDrawLine(win->canv, cx - 7, cy + 7, cx + 7, cy - 7, fg);
        ChDrawLine(win->canv, cx - 7, cy + 6, cx + 6, cy - 7, fg);
        break;
    case TOOL_RECTANGLE:
        ChDrawRectUnfilled(win->canv, cx - 7, cy - 5, 14, 11, fg);
        ChDrawRectUnfilled(win->canv, cx - 6, cy - 4, 12, 9, fg);
        break;
    case TOOL_FILLED_RECTANGLE:
        ChDrawRect(win->canv, cx - 7, cy - 5, 14, 11, fg);
        ChDrawRectUnfilled(win->canv, cx - 7, cy - 5, 14, 11, 0xFF222222);
        break;
    case TOOL_CIRCLE:
        ChDrawCircleUnfilled(win->canv, cx, cy, 7, fg);
        ChDrawCircleUnfilled(win->canv, cx, cy, 6, fg);
        break;
    case TOOL_FILLED_CIRCLE:
        ChDrawFilledCircle(win->canv, cx, cy, 7, fg);
        ChDrawCircleUnfilled(win->canv, cx, cy, 7, 0xFF222222);
        break;
    case TOOL_EYEDROPPER:
        ChDrawFilledCircle(win->canv, cx + 5, cy - 5, 3, 0xFFFF4444);
        ChDrawLine(win->canv, cx + 4, cy - 4, cx - 3, cy + 3, fg);
        ChDrawLine(win->canv, cx + 3, cy - 3, cx - 4, cy + 4, fg);
        ChDrawFilledCircle(win->canv, cx - 6, cy + 6, 2, 0xFF00BFFF);
        break;
    default:
        break;
    }
}

static void _ToolBtnMouse(ChWidget* wid, ChWindow* win, int x, int y, int button) {
    (void)x; (void)y;
    ToolBtn* btn = (ToolBtn*)wid;
    if (button == 1) {
        wid->clicked = true;
    } else if (button == 0) {
        if (wid->clicked && s_canvas) {
            if (btn->tool != TOOL_EYEDROPPER) {
                s_canvas->previousTool = btn->tool;
            }
            s_canvas->currentTool = btn->tool;
            PaintUIRefreshTools(win);
            PaintUIRefreshStatus(win);
        }
        wid->clicked = false;
    }
    if (wid->ChPaintHandler) {
        wid->ChPaintHandler(wid, win);
        ChWindowUpdate(win, wid->x, wid->y, wid->w, wid->h, false, true);
    }
}

static void _SizeBtnPaint(ChWidget* wid, ChWindow* win) {
    SizeBtn* btn = (SizeBtn*)wid;
    bool isActive = (s_canvas && s_canvas->strokeWidth == btn->size);

    uint32_t bg = isActive ? 0xFF007ACC : (wid->clicked ? 0xFF1E1E1E : (wid->hover ? 0xFF444444 : 0xFF2E2E2E));
    ChDrawRect(win->canv, wid->x, wid->y, wid->w, wid->h, bg);
    uint32_t borderCol = isActive ? 0xFF00BFFF : 0xFF4D4D4D;
    ChDrawRectUnfilled(win->canv, wid->x, wid->y, wid->w, wid->h, borderCol);

    ChRect clip = { wid->x, wid->y, wid->w, wid->h };
    int textW = ChFontGetWidth(win->app->baseFont, (char*)btn->label);
    int tx = wid->x + (wid->w - textW) / 2;
    ChFontSetSize(win->app->baseFont, 11);
    int ty = wid->y + (wid->h / 2) + 4;
    ChFontDrawTextClipped(win->canv, win->app->baseFont, (char*)btn->label, tx, ty, 0xFFFFFFFF, &clip);
}

static void _SizeBtnMouse(ChWidget* wid, ChWindow* win, int x, int y, int button) {
    (void)x; (void)y;
    SizeBtn* btn = (SizeBtn*)wid;
    if (button == 1) {
        wid->clicked = true;
    } else if (button == 0) {
        if (wid->clicked && s_canvas) {
            s_canvas->strokeWidth = btn->size;
            PaintUIRefreshSizes(win);
            PaintUIRefreshStatus(win);
        }
        wid->clicked = false;
    }
    if (wid->ChPaintHandler) {
        wid->ChPaintHandler(wid, win);
        ChWindowUpdate(win, wid->x, wid->y, wid->w, wid->h, false, true);
    }
}

static void _ActionBtnPaint(ChWidget* wid, ChWindow* win) {
    ActionBtn* btn = (ActionBtn*)wid;
    uint32_t bg = wid->clicked ? 0xFF1E1E1E : (wid->hover ? 0xFF484848 : 0xFF323232);
    ChDrawRect(win->canv, wid->x, wid->y, wid->w, wid->h, bg);
    ChDrawRectUnfilled(win->canv, wid->x, wid->y, wid->w, wid->h, wid->hover ? 0xFF888888 : 0xFF555555);

    ChRect clip = { wid->x, wid->y, wid->w, wid->h };
    int textW = ChFontGetWidth(win->app->baseFont, (char*)btn->label);
    int tx = wid->x + (wid->w - textW) / 2;
    ChFontSetSize(win->app->baseFont, 12);
    int ty = wid->y + (wid->h / 2) + 4;
    ChFontDrawTextClipped(win->canv, win->app->baseFont, (char*)btn->label, tx, ty, 0xFFFFFFFF, &clip);
}

static void _ActionBtnMouse(ChWidget* wid, ChWindow* win, int x, int y, int button) {
    (void)x; (void)y;
    ActionBtn* btn = (ActionBtn*)wid;
    if (button == 1) {
        wid->clicked = true;
    } else if (button == 0) {
        if (wid->clicked && btn->action) {
            btn->action();
        }
        wid->clicked = false;
    }
    if (wid->ChPaintHandler) {
        wid->ChPaintHandler(wid, win);
        ChWindowUpdate(win, wid->x, wid->y, wid->w, wid->h, false, true);
    }
}

// Integrated Bottom Bar: Color 1 & 2 indicators, 24 Palette Swatches, and Status text
static void _BottomBarPaint(ChWidget* wid, ChWindow* win) {
    ChDrawRect(win->canv, wid->x, wid->y, wid->w, wid->h, 0xFF242424);
    ChDrawRectUnfilled(win->canv, wid->x, wid->y, wid->w, wid->h, 0xFF3D3D3D);

    if (!s_canvas) return;

    // Color 1 box (X = 10, Y = 6, 26x26)
    int c1X = wid->x + 10;
    int c1Y = wid->y + 6;
    ChDrawRect(win->canv, c1X, c1Y, 26, 26, s_canvas->primaryColor);
    ChDrawRectUnfilled(win->canv, c1X, c1Y, 26, 26, 0xFFFFFFFF);
    ChFontSetSize(win->app->baseFont, 10);
    ChRect clip1 = { c1X, c1Y, 26, 26 };
    ChFontDrawTextClipped(win->canv, win->app->baseFont, (char*)"1", c1X + 9, c1Y + 16,
                          (s_canvas->primaryColor == 0xFFFFFFFF) ? 0xFF000000 : 0xFFFFFFFF, &clip1);

    // Color 2 box (X = 42, Y = 6, 26x26)
    int c2X = wid->x + 42;
    int c2Y = wid->y + 6;
    ChDrawRect(win->canv, c2X, c2Y, 26, 26, s_canvas->secondaryColor);
    ChDrawRectUnfilled(win->canv, c2X, c2Y, 26, 26, 0xFFFFFFFF);
    ChRect clip2 = { c2X, c2Y, 26, 26 };
    ChFontDrawTextClipped(win->canv, win->app->baseFont, (char*)"2", c2X + 9, c2Y + 16,
                          (s_canvas->secondaryColor == 0xFFFFFFFF) ? 0xFF000000 : 0xFFFFFFFF, &clip2);

    // 24 Color Swatches Palette (2 rows of 12 swatches)
    // Row 0: Y = 4, Row 1: Y = 24
    int palStartX = wid->x + 76;
    for (int i = 0; i < 12; ++i) {
        // Row 0
        int sx0 = palStartX + (i * 18);
        int sy0 = wid->y + 4;
        uint32_t col0 = PRESET_PALETTE[i];
        ChDrawRect(win->canv, sx0, sy0, 16, 16, col0);
        uint32_t border0 = (col0 == s_canvas->primaryColor) ? 0xFF00D2FF : 0xFF505050;
        ChDrawRectUnfilled(win->canv, sx0, sy0, 16, 16, border0);
        if (col0 == s_canvas->primaryColor) {
            ChDrawRectUnfilled(win->canv, sx0 + 2, sy0 + 2, 12, 12, (col0 == 0xFFFFFFFF) ? 0xFF000000 : 0xFFFFFFFF);
        }

        // Row 1
        int sx1 = palStartX + (i * 18);
        int sy1 = wid->y + 24;
        uint32_t col1 = PRESET_PALETTE[12 + i];
        ChDrawRect(win->canv, sx1, sy1, 16, 16, col1);
        uint32_t border1 = (col1 == s_canvas->primaryColor) ? 0xFF00D2FF : 0xFF505050;
        ChDrawRectUnfilled(win->canv, sx1, sy1, 16, 16, border1);
        if (col1 == s_canvas->primaryColor) {
            ChDrawRectUnfilled(win->canv, sx1 + 2, sy1 + 2, 12, 12, (col1 == 0xFFFFFFFF) ? 0xFF000000 : 0xFFFFFFFF);
        }
    }

    // Divider line between Palette and Status info
    int divX = wid->x + 298;
    ChDrawLine(win->canv, divX, wid->y + 6, divX, wid->y + 38, 0xFF4D4D4D);

    // Status text
    const char* toolNames[TOOL_COUNT] = {
        "Pencil", "Brush", "Eraser", "Flood Fill", "Line",
        "Rectangle", "Filled Rectangle", "Circle", "Filled Circle", "Color Picker (Eyedropper)"
    };

    const char* tName = (s_canvas->currentTool < TOOL_COUNT) ? toolNames[s_canvas->currentTool] : "Tool";
    char statusBuf[160];
    if (s_canvas->currentTool == TOOL_EYEDROPPER) {
        snprintf(statusBuf, sizeof(statusBuf), "Tool: %s [Click canvas to sample color]  |  Pos: %d, %d  |  Canvas: %dx%d",
                 tName, s_canvas->currentX, s_canvas->currentY, CANVAS_W, CANVAS_H);
    } else {
        snprintf(statusBuf, sizeof(statusBuf), "Tool: %s  |  Size: %dpx  |  Pos: %d, %d  |  Canvas: %dx%d",
                 tName, s_canvas->strokeWidth, s_canvas->currentX, s_canvas->currentY, CANVAS_W, CANVAS_H);
    }

    int textX = wid->x + 308;
    int textY = wid->y + 24;
    ChFontSetSize(win->app->baseFont, 12);
    ChRect textClip = { textX, wid->y, wid->w - 312, wid->h };
    ChFontDrawTextClipped(win->canv, win->app->baseFont, statusBuf, textX, textY, 0xFFCCCCCC, &textClip);
}

static void _BottomBarMouse(ChWidget* wid, ChWindow* win, int x, int y, int button) {
    if (!s_canvas || !win || !win->info) return;

    int rx = x - win->info->x - wid->x;
    int ry = y - win->info->y - wid->y;

    if (button == 1 || button == 2) {
        // Check 24 Swatches area: X from 76 to 76 + 12*18 = 292
        if (rx >= 76 && rx < 292) {
            int col = (rx - 76) / 18;
            int offset = (rx - 76) % 18;
            if (col >= 0 && col < 12 && offset < 16) {
                int row = -1;
                if (ry >= 4 && ry < 20) {
                    row = 0;
                } else if (ry >= 24 && ry < 40) {
                    row = 1;
                }
                if (row >= 0) {
                    int idx = row * 12 + col;
                    if (idx >= 0 && idx < COLOR_PALETTE_SIZE) {
                        if (button == 1) {
                            s_canvas->primaryColor = PRESET_PALETTE[idx];
                        } else {
                            s_canvas->secondaryColor = PRESET_PALETTE[idx];
                        }
                        PaintUIRefreshStatus(win);
                    }
                }
            }
        }
    }
}

// Action handlers
static void _OnNewCanvas(void) {
    if (s_canvas) {
        PaintCanvasClear(s_canvas, 0xFFFFFFFF);
        PaintUIRefreshCanvas();
    }
}

static void _OnOpenBMP(void) {
    if (s_canvas) {
        if (!PaintLoadBMP("/pictures/paint.bmp", s_canvas->buffer, CANVAS_W, CANVAS_H)) {
            PaintLoadBMP("/paint.bmp", s_canvas->buffer, CANVAS_W, CANVAS_H);
        }
        PaintUIRefreshCanvas();
    }
}

static void _OnSaveBMP(void) {
    if (s_canvas) {
        PaintSaveBMP("/pictures/paint.bmp", s_canvas->buffer, CANVAS_W, CANVAS_H);
    }
}

static void _OnUndo(void) {
    if (s_canvas) {
        PaintCanvasUndo(s_canvas);
        PaintUIRefreshCanvas();
    }
}

static void _OnRedo(void) {
    if (s_canvas) {
        PaintCanvasRedo(s_canvas);
        PaintUIRefreshCanvas();
    }
}

static void _OnClear(void) {
    if (s_canvas) {
        PaintCanvasClear(s_canvas, s_canvas->secondaryColor);
        PaintUIRefreshCanvas();
    }
}

static void _OnInvert(void) {
    if (s_canvas) {
        PaintCanvasInvert(s_canvas);
        PaintUIRefreshCanvas();
    }
}

static void _CreateToolBtn(ChWindow* win, int x, int y, int w, int h, PaintTool tool, const char* label) {
    ToolBtn* b = (ToolBtn*)malloc(sizeof(ToolBtn));
    memset(b, 0, sizeof(ToolBtn));
    b->base.x = x;
    b->base.y = y;
    b->base.w = w;
    b->base.h = h;
    b->base.ChPaintHandler = _ToolBtnPaint;
    b->base.ChMouseEvent = _ToolBtnMouse;
    b->tool = tool;
    b->label = label;
    if (tool < TOOL_COUNT) {
        s_toolBtns[tool] = b;
    }
    ChWindowAddWidget(win, (ChWidget*)b);
}

static void _CreateSizeBtn(ChWindow* win, int x, int y, int w, int h, int size, const char* label) {
    SizeBtn* b = (SizeBtn*)malloc(sizeof(SizeBtn));
    memset(b, 0, sizeof(SizeBtn));
    b->base.x = x;
    b->base.y = y;
    b->base.w = w;
    b->base.h = h;
    b->base.ChPaintHandler = _SizeBtnPaint;
    b->base.ChMouseEvent = _SizeBtnMouse;
    b->size = size;
    b->label = label;
    if (s_sizeBtnCount < 5) {
        s_sizeBtns[s_sizeBtnCount++] = b;
    }
    ChWindowAddWidget(win, (ChWidget*)b);
}

static void _CreateActionBtn(ChWindow* win, int x, int y, int w, int h, const char* label, void (*act)(void)) {
    ActionBtn* b = (ActionBtn*)malloc(sizeof(ActionBtn));
    memset(b, 0, sizeof(ActionBtn));
    b->base.x = x;
    b->base.y = y;
    b->base.w = w;
    b->base.h = h;
    b->base.ChPaintHandler = _ActionBtnPaint;
    b->base.ChMouseEvent = _ActionBtnMouse;
    b->label = label;
    b->action = act;
    ChWindowAddWidget(win, (ChWidget*)b);
}

void PaintUIInitialize(ChWindow* win, PaintCanvas* canvas) {
    s_win = win;
    s_canvas = canvas;
    s_sizeBtnCount = 0;

    // Top action bar buttons (Y = 32, H = 26)
    _CreateActionBtn(win, 10,  32, 50, 26, "New",    _OnNewCanvas);
    _CreateActionBtn(win, 66,  32, 50, 26, "Open",   _OnOpenBMP);
    _CreateActionBtn(win, 122, 32, 50, 26, "Save",   _OnSaveBMP);
    _CreateActionBtn(win, 184, 32, 50, 26, "Undo",   _OnUndo);
    _CreateActionBtn(win, 240, 32, 50, 26, "Redo",   _OnRedo);
    _CreateActionBtn(win, 302, 32, 50, 26, "Clear",  _OnClear);
    _CreateActionBtn(win, 358, 32, 54, 26, "Invert", _OnInvert);

    // Stroke size selectors (Y = 32, H = 26)
    _CreateSizeBtn(win, 430, 32, 28, 26, 1,  "1px");
    _CreateSizeBtn(win, 464, 32, 28, 26, 2,  "2px");
    _CreateSizeBtn(win, 498, 32, 28, 26, 4,  "4px");
    _CreateSizeBtn(win, 532, 32, 28, 26, 8,  "8px");
    _CreateSizeBtn(win, 566, 32, 34, 26, 12, "12px");

    // Left Tool Palette (2 columns of 28x28 buttons)
    _CreateToolBtn(win, 10, 72,  28, 28, TOOL_PENCIL,           NULL);
    _CreateToolBtn(win, 42, 72,  28, 28, TOOL_BRUSH,            NULL);

    _CreateToolBtn(win, 10, 104, 28, 28, TOOL_ERASER,           NULL);
    _CreateToolBtn(win, 42, 104, 28, 28, TOOL_FLOOD_FILL,       NULL);

    _CreateToolBtn(win, 10, 136, 28, 28, TOOL_LINE,             NULL);
    _CreateToolBtn(win, 42, 136, 28, 28, TOOL_RECTANGLE,        NULL);

    _CreateToolBtn(win, 10, 168, 28, 28, TOOL_FILLED_RECTANGLE, NULL);
    _CreateToolBtn(win, 42, 168, 28, 28, TOOL_CIRCLE,           NULL);

    _CreateToolBtn(win, 10, 200, 28, 28, TOOL_FILLED_CIRCLE,    NULL);
    _CreateToolBtn(win, 42, 200, 28, 28, TOOL_EYEDROPPER,       NULL);

    // Add Canvas widget
    ChWindowAddWidget(win, (ChWidget*)canvas);

    // Bottom Bar (X = 0, Y = 496, W = 780, H = 44)
    // Directly hosts Color 1 & 2 indicators, 24 Palette Swatches, and Live Status info
    ChWidget* bottomBar = (ChWidget*)malloc(sizeof(ChWidget));
    memset(bottomBar, 0, sizeof(ChWidget));
    bottomBar->x = 0;
    bottomBar->y = 496;
    bottomBar->w = PAINT_WIN_WIDTH;
    bottomBar->h = 44;
    bottomBar->ChPaintHandler = _BottomBarPaint;
    bottomBar->ChMouseEvent = _BottomBarMouse;
    s_bottomBar = bottomBar;
    ChWindowAddWidget(win, bottomBar);
}

void PaintUIRefreshStatus(ChWindow* win) {
    if (win && s_bottomBar && s_bottomBar->ChPaintHandler) {
        s_bottomBar->ChPaintHandler(s_bottomBar, win);
        ChWindowUpdate(win, s_bottomBar->x, s_bottomBar->y, s_bottomBar->w, s_bottomBar->h, false, true);
    }
}
