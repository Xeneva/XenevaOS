/**
 * @file main.cpp
 * @brief Main entry point and IPC event loop for XESheets spreadsheet application
 */

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
#include "sheet_types.h"
#include "sheet_model.h"
#include "sheet_eval.h"
#include "sheet_fs.h"
#include "sheet_grid.h"
#include "sheet_ui.h"

ChitralekhaApp* g_App = NULL;
ChWindow* g_MainWin = NULL;
ChFont* g_AppFont = NULL;

static void SheetsClose(ChWindow* win, ChWinGlobalControl* ctl) {
    (void)ctl;
    if (g_SheetUI.workbook.isDirty) {
        SheetFSExportCSV(&g_SheetUI.workbook, "/sheet/SHEET1.CSV");
    }
    ChWindowCloseWindow(win);
}

static bool g_ShiftHeld = false;

static void WindowHandleMessage(PostEvent* e) {
    switch (e->type) {
    case DEODHAI_REPLY_MOUSE_EVENT: {
        int handle = e->dword4;
        if (e->dword3 != 0) {
            _KePrint("[XESheets] Mouse click: x=%d y=%d btn=%d handle=%d\n", e->dword, e->dword2, e->dword3, handle);
        }
        if (e->dword5 == WINDOW_HANDLE_TYPE_NORMAL) {
            ChWindow* mouseWin = ChGetWindowByHandle(g_MainWin, handle);
            ChWindowHandleMouse(mouseWin, e->dword, e->dword2, e->dword3);
        } else if (e->dword5 == WINDOW_HANDLE_TYPE_POPUP) {
            ChWindow* pw = ChGetPopupWindowByHandle(g_MainWin, handle);
            ChPopupWindowHandleMouse(pw, e->dword, e->dword2, e->dword3);
        }
        memset(e, 0, sizeof(PostEvent));
        break;
    }
    case DEODHAI_REPLY_KEY_EVENT: {
        int code = e->dword;
        ChitralekhaProcessKey(code);

        /* Track Shift state explicitly from hardware make/break codes */
        if (code == 42 || code == 54) {
            g_ShiftHeld = true;
            ChitralekhaKeySetShift(true);
            memset(e, 0, sizeof(PostEvent));
            break;
        } else if (code == 170 || code == 182) {
            g_ShiftHeld = false;
            ChitralekhaKeySetShift(false);
            memset(e, 0, sizeof(PostEvent));
            break;
        } else if (code == 29 || code == 56 || code == 157 || code == 184) {
            /* Ctrl / Alt press and release -> ignore modifier-only events */
            memset(e, 0, sizeof(PostEvent));
            break;
        }

        /* Ignore key release events (scancode release bit 0x80) */
        if (code & 0x80) {
            memset(e, 0, sizeof(PostEvent));
            break;
        }

        bool isShift = g_ShiftHeld || ChitralekhaKeyGetShift();
        char c = ChitralekhaKeyToASCII(code);

        /* Map Numpad scancodes directly to numbers and symbols */
        switch (code) {
        case 0x47: c = '7'; break; /* Numpad 7 */
        case 0x48: c = '8'; break; /* Numpad 8 */
        case 0x49: c = '9'; break; /* Numpad 9 */
        case 0x4A: c = '-'; break; /* Numpad - */
        case 0x4B: c = '4'; break; /* Numpad 4 */
        case 0x4C: c = '5'; break; /* Numpad 5 */
        case 0x4D: c = '6'; break; /* Numpad 6 */
        case 0x4E: c = '+'; break; /* Numpad + */
        case 0x4F: c = '1'; break; /* Numpad 1 */
        case 0x50: c = '2'; break; /* Numpad 2 */
        case 0x51: c = '3'; break; /* Numpad 3 */
        case 0x52: c = '0'; break; /* Numpad 0 */
        case 0x53: c = '.'; break; /* Numpad . */
        case 0x37: c = '*'; break; /* Numpad * */
        case 0xE035: c = '/'; break; /* Numpad / */
        default: break;
        }

        /* Top horizontal number and punctuation row */
        if (isShift) {
            switch (code) {
            case 2:  c = '!'; break;
            case 3:  c = '@'; break;
            case 4:  c = '#'; break;
            case 5:  c = '$'; break;
            case 6:  c = '%'; break;
            case 7:  c = '^'; break;
            case 8:  c = '&'; break;
            case 9:  c = '*'; break;
            case 10: c = '('; break;  /* Shift + 9 -> '(' */
            case 11: c = ')'; break;  /* Shift + 0 -> ')' */
            case 12: c = '_'; break;  /* Shift + '-' -> '_' */
            case 13: c = '+'; break;  /* Shift + '=' -> '+' */
            case 0x1A: c = '{'; break;/* Shift + '[' -> '{' */
            case 0x1B: c = '}'; break;/* Shift + ']' -> '}' */
            case 0x27: c = ':'; break;/* Shift + ';' -> ':' */
            case 0x28: c = '"'; break;/* Shift + '\'' -> '"' */
            case 0x2B: c = '|'; break;/* Shift + '\\' -> '|' */
            case 0x33: c = '<'; break;/* Shift + ',' -> '<' */
            case 0x34: c = '>'; break;/* Shift + '.' -> '>' */
            case 0x35: c = '?'; break;/* Shift + '/' -> '?' */
            default: break;
            }
        } else {
            switch (code) {
            case 2:  c = '1'; break;
            case 3:  c = '2'; break;
            case 4:  c = '3'; break;
            case 5:  c = '4'; break;
            case 6:  c = '5'; break;
            case 7:  c = '6'; break;
            case 8:  c = '7'; break;
            case 9:  c = '8'; break;
            case 10: c = '9'; break;
            case 11: c = '0'; break;
            case 12: c = '-'; break;
            case 13: c = '='; break;
            case 0x1A: c = '['; break;
            case 0x1B: c = ']'; break;
            case 0x27: c = ';'; break;
            case 0x28: c = '\''; break;
            case 0x2B: c = '\\'; break;
            case 0x33: c = ','; break;
            case 0x34: c = '.'; break;
            case 0x35: c = '/'; break;
            default: break;
            }
        }

        SheetUIHandleKey(&g_SheetUI, code, c);
        memset(e, 0, sizeof(PostEvent));
        break;
    }
    case DEODHAI_REPLY_FOCUS_CHANGED: {
        int focusVal = e->dword;
        int handle = e->dword2;
        ChWindow* focWin = ChGetWindowByHandle(g_MainWin, handle);
        ChWindowHandleFocus(focWin, focusVal, handle);
        memset(e, 0, sizeof(PostEvent));
        break;
    }
    case DEODHAI_REPLY_MOUSE_LEAVE: {
        memset(e, 0, sizeof(PostEvent));
        break;
    }
    default:
        memset(e, 0, sizeof(PostEvent));
        break;
    }
}

int main(int argc, char* argv[]) {
    g_App = ChitralekhaStartApp(argc, argv);

    /* Create 780x520 modern desktop window */
    g_MainWin = ChCreateWindow(g_App, WINDOW_FLAG_MOVABLE, (char*)"XESheets - Spreadsheet", 60, 40, 780, 520);
    g_MainWin->color = 0xFFF3F2F1;

    /* Bind window close button */
    for (unsigned int i = 0; i < (unsigned int)g_MainWin->GlobalControls->pointer; i++) {
        ChWinGlobalControl* ctl = (ChWinGlobalControl*)list_get_at(g_MainWin->GlobalControls, i);
        if (ctl->type == WINDOW_GLOBAL_CONTROL_CLOSE) {
            ctl->ChGlobalActionEvent = SheetsClose;
            break;
        }
    }

    /* Broadcast application icon */
    ChWindowBroadcastIcon(g_App, (char*)"/icons/sheet.bmp");

    /* Initialize system font */
    g_AppFont = ChInitialiseFont((char*)CALIBRI);

    /* Initialize full spreadsheet UI */
    SheetUIInit(&g_SheetUI, g_MainWin, g_AppFont);

    /* Initial paint */
    ChWindowPaint(g_MainWin);

    PostEvent e;
    memset(&e, 0, sizeof(PostEvent));

    /* Setup recovery jump buffer */
    setjmp(g_MainWin->jump);

    /* Event Loop */
    while (1) {
        int err = _KeFileIoControl(g_App->postboxfd, POSTBOX_GET_EVENT, &e);
        WindowHandleMessage(&e);
        if (err == POSTBOX_NO_EVENT) {
            _KePauseThread();
        }
    }

    return 0;
}
