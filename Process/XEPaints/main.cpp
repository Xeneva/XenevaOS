#include <stdint.h>
#include <_xeneva.h>
#include <stdio.h>
#include <sys/_keproc.h>
#include <sys/_kefile.h>
#include <sys/iocodes.h>
#include <chitralekha.h>
#include <widgets/base.h>
#include <widgets/window.h>
#include <string.h>
#include <stdlib.h>
#include <keycode.h>
#include "paint_types.h"
#include "paint_canvas.h"
#include "paint_ui.h"
#include "paint_fs.h"

ChitralekhaApp* app = NULL;
ChWindow* mainWin = NULL;
PaintCanvas* paintCanvas = NULL;

void PaintAppClose(ChWindow* win, ChWinGlobalControl* ctl) {
    (void)ctl;
    ChWindowCloseWindow(win);
}

void PaintHandleKey(int code, char ascii_code) {
    if (!paintCanvas) return;

    (void)code;
    paintCanvas->shiftPressed = ChitralekhaKeyGetShift();

    // Keyboard shortcuts
    switch (ascii_code) {
    case 'z':
    case 'Z':
        PaintCanvasUndo(paintCanvas);
        PaintUIRefreshCanvas();
        break;
    case 'y':
    case 'Y':
        PaintCanvasRedo(paintCanvas);
        PaintUIRefreshCanvas();
        break;
    case 's':
    case 'S':
        PaintSaveBMP("/pictures/paint.bmp", paintCanvas->buffer, CANVAS_W, CANVAS_H);
        break;
    case 'o':
    case 'O':
        if (!PaintLoadBMP("/pictures/paint.bmp", paintCanvas->buffer, CANVAS_W, CANVAS_H)) {
            PaintLoadBMP("/paint.bmp", paintCanvas->buffer, CANVAS_W, CANVAS_H);
        }
        PaintUIRefreshCanvas();
        break;
    case 'n':
    case 'N':
        PaintCanvasClear(paintCanvas, 0xFFFFFFFF);
        PaintUIRefreshCanvas();
        break;
    case 'i':
    case 'I':
        PaintCanvasInvert(paintCanvas);
        PaintUIRefreshCanvas();
        break;
    case 'p':
    case 'P':
        paintCanvas->previousTool = TOOL_PENCIL;
        paintCanvas->currentTool = TOOL_PENCIL;
        PaintUIRefreshTools(mainWin);
        PaintUIRefreshStatus(mainWin);
        break;
    case 'b':
    case 'B':
        paintCanvas->previousTool = TOOL_BRUSH;
        paintCanvas->currentTool = TOOL_BRUSH;
        PaintUIRefreshTools(mainWin);
        PaintUIRefreshStatus(mainWin);
        break;
    case 'e':
    case 'E':
        paintCanvas->previousTool = TOOL_ERASER;
        paintCanvas->currentTool = TOOL_ERASER;
        PaintUIRefreshTools(mainWin);
        PaintUIRefreshStatus(mainWin);
        break;
    case 'f':
    case 'F':
        paintCanvas->previousTool = TOOL_FLOOD_FILL;
        paintCanvas->currentTool = TOOL_FLOOD_FILL;
        PaintUIRefreshTools(mainWin);
        PaintUIRefreshStatus(mainWin);
        break;
    case 'l':
    case 'L':
        paintCanvas->previousTool = TOOL_LINE;
        paintCanvas->currentTool = TOOL_LINE;
        PaintUIRefreshTools(mainWin);
        PaintUIRefreshStatus(mainWin);
        break;
    case 'r':
    case 'R':
        paintCanvas->previousTool = TOOL_RECTANGLE;
        paintCanvas->currentTool = TOOL_RECTANGLE;
        PaintUIRefreshTools(mainWin);
        PaintUIRefreshStatus(mainWin);
        break;
    case 'c':
    case 'C':
        paintCanvas->previousTool = TOOL_CIRCLE;
        paintCanvas->currentTool = TOOL_CIRCLE;
        PaintUIRefreshTools(mainWin);
        PaintUIRefreshStatus(mainWin);
        break;
    case 'k':
    case 'K':
        paintCanvas->currentTool = TOOL_EYEDROPPER;
        PaintUIRefreshTools(mainWin);
        PaintUIRefreshStatus(mainWin);
        break;
    case '1':
        paintCanvas->strokeWidth = 1;
        PaintUIRefreshSizes(mainWin);
        PaintUIRefreshStatus(mainWin);
        break;
    case '2':
        paintCanvas->strokeWidth = 2;
        PaintUIRefreshSizes(mainWin);
        PaintUIRefreshStatus(mainWin);
        break;
    case '4':
        paintCanvas->strokeWidth = 4;
        PaintUIRefreshSizes(mainWin);
        PaintUIRefreshStatus(mainWin);
        break;
    case '8':
        paintCanvas->strokeWidth = 8;
        PaintUIRefreshSizes(mainWin);
        PaintUIRefreshStatus(mainWin);
        break;
    default:
        break;
    }
}

void WindowHandleMessage(PostEvent* e) {
    switch (e->type) {
    case DEODHAI_REPLY_MOUSE_EVENT: {
        int handle = e->dword4;
        if (e->dword5 == WINDOW_HANDLE_TYPE_NORMAL) {
            ChWindow* mouseWin = ChGetWindowByHandle(mainWin, handle);
            ChWindowHandleMouse(mouseWin, e->dword, e->dword2, e->dword3);
        } else if (e->dword5 == WINDOW_HANDLE_TYPE_POPUP) {
            ChWindow* pw = ChGetPopupWindowByHandle(mainWin, handle);
            ChPopupWindowHandleMouse(pw, e->dword, e->dword2, e->dword3);
        }
        memset(e, 0, sizeof(PostEvent));
        break;
    }
    case DEODHAI_REPLY_MOUSE_LEAVE: {
        memset(e, 0, sizeof(PostEvent));
        break;
    }
    case DEODHAI_REPLY_KEY_EVENT: {
        int code = e->dword;
        ChitralekhaProcessKey(code);
        char c = ChitralekhaKeyToASCII(code);
        PaintHandleKey(code, c);
        memset(e, 0, sizeof(PostEvent));
        break;
    }
    case DEODHAI_REPLY_FOCUS_CHANGED: {
        int focus_val = e->dword;
        int handle = e->dword2;
        ChWindow* focWin = ChGetWindowByHandle(mainWin, handle);
        ChWindowHandleFocus(focWin, focus_val, handle);
        memset(e, 0, sizeof(PostEvent));
        break;
    }
    }
}

int main(int argc, char* argv[]) {
    app = ChitralekhaStartApp(argc, argv);

    mainWin = ChCreateWindow(app, WINDOW_FLAG_MOVABLE, (char*)"XEPaints", 80, 60, PAINT_WIN_WIDTH, PAINT_WIN_HEIGHT);
    mainWin->color = 0xFF2A2A2A; // Modern dark theme canvas container

    for (unsigned int i = 0; i < mainWin->GlobalControls->pointer; i++) {
        ChWinGlobalControl* ctl = (ChWinGlobalControl*)list_get_at(mainWin->GlobalControls, i);
        if (ctl->type == WINDOW_GLOBAL_CONTROL_CLOSE) {
            ctl->ChGlobalActionEvent = PaintAppClose;
            break;
        }
    }

    ChWindowBroadcastIcon(app, (char*)"/icons/paint.bmp");

    paintCanvas = CreatePaintCanvas(CANVAS_X, CANVAS_Y, CANVAS_W, CANVAS_H);

    PaintUIInitialize(mainWin, paintCanvas);

    ChWindowPaint(mainWin);

    PostEvent e;
    memset(&e, 0, sizeof(PostEvent));

    setjmp(mainWin->jump);

    while (1) {
        int err = _KeFileIoControl(app->postboxfd, POSTBOX_GET_EVENT, &e);
        WindowHandleMessage(&e);
        if (err == POSTBOX_NO_EVENT) {
            _KePauseThread();
        }
    }

    DestroyPaintCanvas(paintCanvas);
    return 0;
}
