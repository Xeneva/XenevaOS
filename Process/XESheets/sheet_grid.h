/**
 * @file sheet_grid.h
 * @brief Virtualized interactive spreadsheet grid widget for XESheets
 */

#ifndef __SHEET_GRID_H__
#define __SHEET_GRID_H__

#include <chitralekha.h>
#include <widgets/window.h>
#include "sheet_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Extended AT scancodes forwarded by virtio-keyboard in XenevaOS */
#define SHEET_KEY_UP_EXT     0xE048
#define SHEET_KEY_DOWN_EXT   0xE050
#define SHEET_KEY_LEFT_EXT   0xE04B
#define SHEET_KEY_RIGHT_EXT  0xE04D
#define SHEET_KEY_HOME_EXT   0xE047
#define SHEET_KEY_END_EXT    0xE04F
#define SHEET_KEY_PGUP_EXT   0xE049
#define SHEET_KEY_PGDN_EXT   0xE051
#define SHEET_KEY_DEL_EXT    0xE053

/* Standard make scancodes */
#define SHEET_KEY_ENTER_STD  0x1C
#define SHEET_KEY_TAB_STD    0x0F
#define SHEET_KEY_ESC_STD    0x01
#define SHEET_KEY_BS_STD     0x0E

typedef struct _SheetGridWidget {
    ChWidget base;
    SheetWorkbook* workbook;
    ChFont* font;

    /* Selection state */
    int activeCol;
    int activeRow;
    int anchorCol;
    int anchorRow;
    SheetRange selRange;
    bool isRangeSelecting;

    /* Viewport scroll */
    int scrollCol;
    int scrollRow;

    /* In-cell editing */
    bool isEditing;
    char editBuffer[SHEET_CELL_TEXT_LEN];
    int editCursor;

    /* Callback when selection or edit state changes (syncs with Formula Bar & Status Bar) */
    void (*OnSelectionChanged)(struct _SheetGridWidget* grid, ChWindow* win);
    void (*OnCellEdited)(struct _SheetGridWidget* grid, ChWindow* win, int col, int row, const char* text);
} SheetGridWidget;

/* Create and initialize virtualized grid widget */
SheetGridWidget* SheetGridCreate(int x, int y, int w, int h, SheetWorkbook* wb, ChFont* font);

/* Commit currently editing cell */
void SheetGridCommitEdit(SheetGridWidget* grid, ChWindow* win);

/* Cancel currently editing cell */
void SheetGridCancelEdit(SheetGridWidget* grid, ChWindow* win);

/* Start editing currently active cell with initial text (or NULL to keep current text) */
void SheetGridStartEdit(SheetGridWidget* grid, ChWindow* win, const char* initialText);

/* Handle key event dispatched to the grid widget */
bool SheetGridHandleKey(SheetGridWidget* grid, ChWindow* win, int keycode, char ascii);

/* Ensure active cell is within visible viewport, scrolling if necessary */
void SheetGridScrollToActive(SheetGridWidget* grid);

/* Repaint grid widget onto canvas and update window surface */
void SheetGridRepaint(SheetGridWidget* grid, ChWindow* win);

/* Ultra-fast 60 FPS in-cell editing repaint: repaints only active cell dirty rect */
void SheetGridRepaintActiveCell(SheetGridWidget* grid, ChWindow* win);

/* Safe window update helper ensuring compositor dirty flags are preserved */
static inline void SheetWindowUpdate(ChWindow* win, int x, int y, int w, int h, bool updateEntire) {
    if (updateEntire) {
        ChWindowUpdate(win, x, y, w, h, true, true);
    } else {
        ChWindowUpdate(win, x, y, w, h, false, true);
    }
}

#ifdef __cplusplus
}
#endif

#endif /* __SHEET_GRID_H__ */
