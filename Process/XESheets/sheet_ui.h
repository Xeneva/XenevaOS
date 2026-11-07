/**
 * @file sheet_ui.h
 * @brief User interface shell, Action Ribbon, Formula Bar, Tab Strip, and Status Bar for XESheets
 */

#ifndef __SHEET_UI_H__
#define __SHEET_UI_H__

#include <chitralekha.h>
#include <widgets/window.h>
#include <widgets/button.h>
#include "sheet_types.h"
#include "sheet_grid.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _SheetRibbonBtn {
    ChWidget base;
    const char* label;
    void (*action)(void);
    bool prevHover;
} SheetRibbonBtn;

typedef struct _SheetUI {
    ChWindow* window;
    ChFont* font;
    SheetWorkbook workbook;
    SheetGridWidget* grid;

    /* Ribbon bar & buttons */
    ChWidget ribbonBar;
    SheetRibbonBtn* buttons[12];
    int buttonCount;

    /* Formula Bar */
    ChWidget formulaBar;
    char formulaText[SHEET_CELL_TEXT_LEN];
    bool formulaFocused;
    int formulaCursor;

    /* Tab Strip */
    ChWidget tabStrip;
    bool tabRenaming;
    int renamingTabIdx;
    char renameBuffer[SHEET_TAB_NAME_LEN];
    int renameCursor;
    size_t lastTabClickTick;
    int lastClickedTab;

    /* Status Bar */
    ChWidget statusBar;
    char statusMsg[64];
    SheetStats activeStats;
} SheetUI;

extern SheetUI g_SheetUI;

/* Initialize and assemble all UI components in main window */
void SheetUIInit(SheetUI* ui, ChWindow* win, ChFont* font);

/* Synchronize Formula Bar & Status Bar with currently selected cell */
void SheetUISyncSelection(SheetUI* ui);

/* Repaint entire UI (formula bar, grid, tab strip, status bar) and flush to compositor */
void SheetUIRefresh(SheetUI* ui);

/* Update status bar text message */
void SheetUISetStatus(SheetUI* ui, const char* msg);

/* Handle key event for Formula Bar if focused */
bool SheetUIHandleKey(SheetUI* ui, int keycode, char ascii);

#ifdef __cplusplus
}
#endif

#endif /* __SHEET_UI_H__ */
