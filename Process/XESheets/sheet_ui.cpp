/**
 * @file sheet_ui.cpp
 * @brief User interface shell, Action Ribbon, Formula Bar, Tab Strip, and Status Bar for XESheets
 */

#include "sheet_ui.h"
#include "sheet_model.h"
#include "sheet_eval.h"
#include "sheet_fs.h"
#include <keycode.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/_keproc.h>

SheetUI g_SheetUI;

#define COLOR_RIBBON_BG         0xFFF8F9FA
#define COLOR_RIBBON_BORDER     0xFFDADCE0
#define COLOR_FORMULA_BG        0xFFFFFFFF
#define COLOR_FORMULA_BORDER    0xFFDADCE0
#define COLOR_FORMULA_FX        0xFF107C41
#define COLOR_TAB_BAR_BG        0xFFF1F3F4
#define COLOR_TAB_ACTIVE_BG     0xFFFFFFFF
#define COLOR_TAB_INACTIVE_BG   0xFFE8EAED
#define COLOR_STATUS_BG         0xFF107C41
#define COLOR_STATUS_TEXT       0xFFFFFFFF

/* -------------------------------------------------------------
 * UI Refresh Helper
 * ------------------------------------------------------------- */

void SheetUIRefresh(SheetUI* ui) {
    if (!ui || !ui->window) return;
    ChWindow* win = ui->window;
    if (ui->formulaBar.ChPaintHandler) {
        ui->formulaBar.ChPaintHandler(&ui->formulaBar, win);
    }
    if (ui->grid && ui->grid->base.ChPaintHandler) {
        ui->grid->base.ChPaintHandler((ChWidget*)ui->grid, win);
    }
    if (ui->tabStrip.ChPaintHandler) {
        ui->tabStrip.ChPaintHandler(&ui->tabStrip, win);
    }
    if (ui->statusBar.ChPaintHandler) {
        ui->statusBar.ChPaintHandler(&ui->statusBar, win);
    }
    ChWindowUpdate(win, 0, 26, win->info->width, win->info->height - 26, 0, 1);
}

/* -------------------------------------------------------------
 * Action Handlers
 * ------------------------------------------------------------- */

static void ActionNew(void) {
    SheetModelInit(&g_SheetUI.workbook);
    g_SheetUI.grid->activeCol = 0;
    g_SheetUI.grid->activeRow = 0;
    g_SheetUI.grid->anchorCol = 0;
    g_SheetUI.grid->anchorRow = 0;
    g_SheetUI.grid->selRange.startCol = 0;
    g_SheetUI.grid->selRange.endCol = 0;
    g_SheetUI.grid->selRange.startRow = 0;
    g_SheetUI.grid->selRange.endRow = 0;
    SheetUISetStatus(&g_SheetUI, "New workbook created");
    SheetUISyncSelection(&g_SheetUI);
    SheetUIRefresh(&g_SheetUI);
}

static void ActionOpen(void) {
    if (SheetFSImportCSV(&g_SheetUI.workbook, "/sheet/SHEET1.CSV")) {
        SheetUISetStatus(&g_SheetUI, "Loaded /sheet/SHEET1.CSV");
    } else {
        SheetUISetStatus(&g_SheetUI, "Could not open /sheet/SHEET1.CSV");
    }
    SheetUISyncSelection(&g_SheetUI);
    SheetUIRefresh(&g_SheetUI);
}

static void ActionSave(void) {
    if (SheetFSExportCSV(&g_SheetUI.workbook, "/sheet/SHEET1.CSV")) {
        SheetUISetStatus(&g_SheetUI, "Saved to /sheet/SHEET1.CSV");
    } else {
        SheetUISetStatus(&g_SheetUI, "Error saving file");
    }
    SheetUIRefresh(&g_SheetUI);
}

static void ActionSum(void) {
    SheetGridWidget* grid = g_SheetUI.grid;
    char formula[SHEET_CELL_TEXT_LEN];
    if (grid->activeRow > 0) {
        snprintf(formula, sizeof(formula), "=SUM(%c1:%c%d)",
                 (char)('A' + grid->activeCol), (char)('A' + grid->activeCol), grid->activeRow);
    } else {
        snprintf(formula, sizeof(formula), "=SUM(A1:A5)");
    }

    SheetModelSetCellText(&g_SheetUI.workbook, grid->activeCol, grid->activeRow, formula);
    SheetUISyncSelection(&g_SheetUI);
    SheetUIRefresh(&g_SheetUI);
}

static void ActionAvg(void) {
    SheetGridWidget* grid = g_SheetUI.grid;
    char formula[SHEET_CELL_TEXT_LEN];
    if (grid->activeRow > 0) {
        snprintf(formula, sizeof(formula), "=AVERAGE(%c1:%c%d)",
                 (char)('A' + grid->activeCol), (char)('A' + grid->activeCol), grid->activeRow);
    } else {
        snprintf(formula, sizeof(formula), "=AVERAGE(A1:A5)");
    }

    SheetModelSetCellText(&g_SheetUI.workbook, grid->activeCol, grid->activeRow, formula);
    SheetUISyncSelection(&g_SheetUI);
    SheetUIRefresh(&g_SheetUI);
}

static void ActionSortAsc(void) {
    SheetGridWidget* grid = g_SheetUI.grid;
    SheetModelSortRange(&g_SheetUI.workbook, &grid->selRange, grid->activeCol, true);
    char buf[64];
    snprintf(buf, sizeof(buf), "Sorted by %c (Ascending)", (char)('A' + grid->activeCol));
    SheetUISetStatus(&g_SheetUI, buf);
    SheetUISyncSelection(&g_SheetUI);
    SheetUIRefresh(&g_SheetUI);
}

static void ActionSortDesc(void) {
    SheetGridWidget* grid = g_SheetUI.grid;
    SheetModelSortRange(&g_SheetUI.workbook, &grid->selRange, grid->activeCol, false);
    char buf[64];
    snprintf(buf, sizeof(buf), "Sorted by %c (Descending)", (char)('A' + grid->activeCol));
    SheetUISetStatus(&g_SheetUI, buf);
    SheetUISyncSelection(&g_SheetUI);
    SheetUIRefresh(&g_SheetUI);
}

static void ActionBold(void) {
    SheetGridWidget* grid = g_SheetUI.grid;
    SheetModelToggleFormat(&g_SheetUI.workbook, grid->activeCol, grid->activeRow, CELL_FMT_BOLD);
    SheetGridRepaint(grid, g_SheetUI.window);

    /* Immediately reflect bold active state on Bold ribbon button */
    for (int i = 0; i < g_SheetUI.buttonCount; i++) {
        if (g_SheetUI.buttons[i] && g_SheetUI.buttons[i]->label && strcmp(g_SheetUI.buttons[i]->label, "Bold") == 0) {
            if (g_SheetUI.buttons[i]->base.ChPaintHandler) {
                g_SheetUI.buttons[i]->base.ChPaintHandler((ChWidget*)g_SheetUI.buttons[i], g_SheetUI.window);
                ChWindowUpdate(g_SheetUI.window, g_SheetUI.buttons[i]->base.x, g_SheetUI.buttons[i]->base.y,
                               g_SheetUI.buttons[i]->base.w, g_SheetUI.buttons[i]->base.h, 0, 1);
            }
            break;
        }
    }
}

static void ActionAlign(void) {
    SheetGridWidget* grid = g_SheetUI.grid;
    SheetCell* cell = SheetModelGetActiveCell(&g_SheetUI.workbook, grid->activeCol, grid->activeRow);
    if (!cell) return;

    if (cell->format & CELL_FMT_ALIGN_CENTER) {
        cell->format &= ~CELL_FMT_ALIGN_CENTER;
        cell->format |= CELL_FMT_ALIGN_RIGHT;
    } else if (cell->format & CELL_FMT_ALIGN_RIGHT) {
        cell->format &= ~CELL_FMT_ALIGN_RIGHT;
    } else {
        cell->format |= CELL_FMT_ALIGN_CENTER;
    }

    SheetGridRepaint(grid, g_SheetUI.window);
}

static void ActionClear(void) {
    SheetGridWidget* grid = g_SheetUI.grid;
    SheetModelClearRange(&g_SheetUI.workbook, &grid->selRange);
    SheetUISyncSelection(&g_SheetUI);
    SheetUIRefresh(&g_SheetUI);
}

/* -------------------------------------------------------------
 * Custom Ribbon Button Widget (Smooth flat modern styling)
 * ------------------------------------------------------------- */

static void RibbonBtnPaint(ChWidget* wid, ChWindow* win) {
    SheetRibbonBtn* btn = (SheetRibbonBtn*)wid;
    bool isBoldActive = false;
    if (btn->label && strcmp(btn->label, "Bold") == 0 && g_SheetUI.grid) {
        SheetCell* c = SheetModelGetActiveCell(&g_SheetUI.workbook, g_SheetUI.grid->activeCol, g_SheetUI.grid->activeRow);
        if (c && (c->format & CELL_FMT_BOLD)) {
            isBoldActive = true;
        }
    }

    uint32_t bg;
    uint32_t border;
    uint32_t textColor;

    if (isBoldActive) {
        bg = wid->clicked ? 0xFFBDD7EE : (wid->hover ? 0xFFCCE3FA : 0xFFD8E8F8);
        border = 0xFF107C41;
        textColor = 0xFF0D5F32;
    } else {
        bg = wid->clicked ? 0xFFE2E7EB : (wid->hover ? 0xFFEDF2F7 : 0xFFFFFFFF);
        border = wid->hover ? COLOR_FORMULA_FX : COLOR_RIBBON_BORDER;
        textColor = 0xFF3C4043;
    }

    ChDrawRect(win->canv, wid->x, wid->y, wid->w, wid->h, bg);
    ChDrawRectUnfilled(win->canv, wid->x, wid->y, wid->w, wid->h, border);

    ChRect clip = { wid->x, wid->y, wid->w, wid->h };
    ChFontSetSize(g_SheetUI.font, 12);
    int textW = (int)ChFontGetWidth(g_SheetUI.font, (char*)btn->label);
    int tx = wid->x + (wid->w - textW) / 2;
    int ty = wid->y + 17;
    ChFontDrawTextClipped(win->canv, g_SheetUI.font, (char*)btn->label, tx, ty, textColor, &clip);
    if (isBoldActive) {
        ChFontDrawTextClipped(win->canv, g_SheetUI.font, (char*)btn->label, tx + 1, ty, textColor, &clip);
    }
}

static void RibbonBtnMouse(ChWidget* wid, ChWindow* win, int x, int y, int button) {
    (void)x; (void)y;
    SheetRibbonBtn* btn = (SheetRibbonBtn*)wid;
    bool needsRepaint = false;

    if (button == 1) {
        if (!wid->clicked) {
            wid->clicked = true;
            needsRepaint = true;
        }
        _KePrint("[XESheets] Ribbon click: %s\n", btn->label ? btn->label : "btn");
        if (wid->ChPaintHandler) {
            wid->ChPaintHandler(wid, win);
            ChWindowUpdate(win, wid->x, wid->y, wid->w, wid->h, 0, 1);
        }
        if (btn->action) {
            btn->action();
        }
        return;
    } else if (button == 0) {
        if (wid->clicked) {
            wid->clicked = false;
            needsRepaint = true;
        }
    }

    if (wid->hover != btn->prevHover) {
        btn->prevHover = wid->hover;
        needsRepaint = true;
    }

    if (needsRepaint && wid->ChPaintHandler) {
        wid->ChPaintHandler(wid, win);
        ChWindowUpdate(win, wid->x, wid->y, wid->w, wid->h, 0, 1);
    }
}

static SheetRibbonBtn* CreateRibbonBtn(ChWindow* win, int x, int y, int w, int h, const char* label, void (*act)(void)) {
    SheetRibbonBtn* b = (SheetRibbonBtn*)malloc(sizeof(SheetRibbonBtn));
    if (!b) return NULL;
    memset(b, 0, sizeof(SheetRibbonBtn));
    b->base.x = x;
    b->base.y = y;
    b->base.w = w;
    b->base.h = h;
    b->base.visible = true;
    b->base.ChPaintHandler = RibbonBtnPaint;
    b->base.ChMouseEvent = RibbonBtnMouse;
    b->label = label;
    b->action = act;
    ChWindowAddWidget(win, (ChWidget*)b);
    return b;
}

/* -------------------------------------------------------------
 * Ribbon Bar Background Widget
 * ------------------------------------------------------------- */

static void RibbonBarPaint(ChWidget* wid, ChWindow* win) {
    ChDrawRect(win->canv, wid->x, wid->y, wid->w, wid->h, COLOR_RIBBON_BG);
    ChDrawHorizontalLine(win->canv, wid->x, wid->y + wid->h - 1, wid->w, COLOR_RIBBON_BORDER);
}

/* -------------------------------------------------------------
 * Formula Bar Widget
 * ------------------------------------------------------------- */

static void FormulaBarPaint(ChWidget* wid, ChWindow* win) {
    ChDrawRect(win->canv, wid->x, wid->y, wid->w, wid->h, COLOR_RIBBON_BG);
    ChDrawHorizontalLine(win->canv, wid->x, wid->y + wid->h - 1, wid->w, COLOR_FORMULA_BORDER);

    /* Coordinate Box e.g. [ A1 ] */
    int coordW = 54;
    ChDrawRect(win->canv, wid->x + 6, wid->y + 2, coordW, wid->h - 4, 0xFFFFFFFF);
    ChDrawRectUnfilled(win->canv, wid->x + 6, wid->y + 2, coordW, wid->h - 4, COLOR_FORMULA_BORDER);

    char coordStr[8];
    SheetCoordToString(g_SheetUI.grid->activeCol, g_SheetUI.grid->activeRow, coordStr, sizeof(coordStr));
    ChFontSetSize(g_SheetUI.font, 13);
    int64_t cW = ChFontGetWidth(g_SheetUI.font, coordStr);
    ChFontDrawText(win->canv, g_SheetUI.font, coordStr, wid->x + 6 + (coordW - (int)cW) / 2, wid->y + 17, 13, COLOR_FORMULA_FX);

    /* fx indicator */
    int fxX = wid->x + 6 + coordW + 6;
    ChFontSetSize(g_SheetUI.font, 14);
    ChFontDrawText(win->canv, g_SheetUI.font, (char*)"fx", fxX, wid->y + 17, 14, 0xFF70757A);

    /* Formula input text box */
    int inputX = fxX + 22;
    int inputW = wid->w - (inputX - wid->x) - 8;
    ChDrawRect(win->canv, inputX, wid->y + 2, inputW, wid->h - 4, COLOR_FORMULA_BG);
    uint32_t boxBorder = g_SheetUI.formulaFocused ? COLOR_FORMULA_FX : COLOR_FORMULA_BORDER;
    ChDrawRectUnfilled(win->canv, inputX, wid->y + 2, inputW, wid->h - 4, boxBorder);

    /* Formula text */
    ChFontSetSize(g_SheetUI.font, 13);
    ChRect clip = {inputX + 4, wid->y + 2, inputW - 8, wid->h - 4};
    ChFontDrawTextClipped(win->canv, g_SheetUI.font, g_SheetUI.formulaText, inputX + 6, wid->y + 17, 0xFF202124, &clip);

    /* Cursor if focused */
    if (g_SheetUI.formulaFocused) {
        int curPos = g_SheetUI.formulaCursor;
        if (curPos < 0) curPos = 0;
        int len = (int)strlen(g_SheetUI.formulaText);
        if (curPos > len) curPos = len;

        char sub[SHEET_CELL_TEXT_LEN];
        memcpy(sub, g_SheetUI.formulaText, curPos);
        sub[curPos] = '\0';

        ChFontSetSize(g_SheetUI.font, 13);
        int prefixW = (curPos > 0) ? (int)ChFontGetWidth(g_SheetUI.font, sub) : 0;
        int curX = inputX + 6 + prefixW;
        if (curX < inputX + inputW - 4) {
            ChDrawVerticalLine(win->canv, curX, wid->y + 5, wid->h - 10, COLOR_FORMULA_FX);
            ChDrawVerticalLine(win->canv, curX + 1, wid->y + 5, wid->h - 10, COLOR_FORMULA_FX);
        }
    }
}

static void FormulaBarMouseEvent(ChWidget* wid, ChWindow* win, int x, int y, int button) {
    (void)y;
    if (button == 1) {
        if (!win || !win->info) return;

        g_SheetUI.formulaFocused = true;
        if (g_SheetUI.tabRenaming) {
            SheetModelRenameTab(&g_SheetUI.workbook, g_SheetUI.renamingTabIdx, g_SheetUI.renameBuffer);
            g_SheetUI.tabRenaming = false;
        }
        if (g_SheetUI.grid && g_SheetUI.grid->isEditing) {
            SheetGridCommitEdit(g_SheetUI.grid, win);
        }

        int winRelX = x - win->info->x;
        int coordW = 54;
        int fxX = wid->x + 6 + coordW + 6;
        int inputX = fxX + 22;
        int textStartX = inputX + 6;

        int len = (int)strlen(g_SheetUI.formulaText);
        if (winRelX <= textStartX) {
            g_SheetUI.formulaCursor = 0;
        } else {
            int clickOffset = winRelX - textStartX;
            ChFontSetSize(g_SheetUI.font, 13);

            char prefix[SHEET_CELL_TEXT_LEN];
            int prevW = 0;
            g_SheetUI.formulaCursor = len;

            for (int i = 1; i <= len; i++) {
                memcpy(prefix, g_SheetUI.formulaText, i);
                prefix[i] = '\0';
                int curW = (int)ChFontGetWidth(g_SheetUI.font, prefix);
                if (clickOffset < curW) {
                    int mid = prevW + (curW - prevW) / 2;
                    if (clickOffset < mid) {
                        g_SheetUI.formulaCursor = i - 1;
                    } else {
                        g_SheetUI.formulaCursor = i;
                    }
                    break;
                }
                prevW = curW;
            }
        }

        if (wid->ChPaintHandler) wid->ChPaintHandler(wid, win);
        ChWindowUpdate(win, wid->x, wid->y, wid->w, wid->h, 0, 1);
    }
}

/* -------------------------------------------------------------
 * Bottom Tab Strip Widget
 * ------------------------------------------------------------- */

static void TabStripPaint(ChWidget* wid, ChWindow* win) {
    ChDrawRect(win->canv, wid->x, wid->y, wid->w, wid->h, COLOR_TAB_BAR_BG);
    ChDrawHorizontalLine(win->canv, wid->x, wid->y, wid->w, COLOR_FORMULA_BORDER);

    SheetWorkbook* wb = &g_SheetUI.workbook;
    int curX = wid->x + 8;
    int tabW = 80;
    int tabH = wid->h - 4;

    for (int i = 0; i < wb->tabCount; i++) {
        bool isActive = (i == wb->activeTab);
        bool isRenaming = (g_SheetUI.tabRenaming && i == g_SheetUI.renamingTabIdx);
        uint32_t bg = isRenaming ? 0xFFFFFFFF : (isActive ? COLOR_TAB_ACTIVE_BG : COLOR_TAB_INACTIVE_BG);
        ChDrawRect(win->canv, curX, wid->y + 2, tabW, tabH, bg);
        ChDrawRectUnfilled(win->canv, curX, wid->y + 2, tabW, tabH, isRenaming ? COLOR_FORMULA_FX : COLOR_FORMULA_BORDER);

        if (isActive && !isRenaming) {
            ChDrawHorizontalLine(win->canv, curX, wid->y + 2, tabW, COLOR_FORMULA_FX);
            ChDrawHorizontalLine(win->canv, curX, wid->y + 3, tabW, COLOR_FORMULA_FX);
        }

        ChFontSetSize(g_SheetUI.font, 12);
        if (isRenaming) {
            int tx = curX + 6;
            ChRect clip = {curX + 2, wid->y + 2, tabW - 4, tabH};
            ChFontDrawTextClipped(win->canv, g_SheetUI.font, g_SheetUI.renameBuffer, tx, wid->y + 16, 0xFF202124, &clip);

            char sub[SHEET_TAB_NAME_LEN];
            int cpos = g_SheetUI.renameCursor;
            if (cpos < 0) cpos = 0;
            if (cpos > (int)strlen(g_SheetUI.renameBuffer)) cpos = (int)strlen(g_SheetUI.renameBuffer);
            memcpy(sub, g_SheetUI.renameBuffer, cpos);
            sub[cpos] = '\0';
            int64_t curOffset = ChFontGetWidth(g_SheetUI.font, sub);
            int cx = tx + (int)curOffset;
            if (cx < curX + tabW - 4) {
                ChDrawVerticalLine(win->canv, cx, wid->y + 5, tabH - 6, COLOR_FORMULA_FX);
                ChDrawVerticalLine(win->canv, cx + 1, wid->y + 5, tabH - 6, COLOR_FORMULA_FX);
            }
        } else {
            int64_t tw = ChFontGetWidth(g_SheetUI.font, wb->tabs[i].name);
            int tx = curX + (tabW - (int)tw) / 2;
            uint32_t textColor = isActive ? COLOR_FORMULA_FX : 0xFF5F6368;
            ChFontDrawText(win->canv, g_SheetUI.font, wb->tabs[i].name, tx, wid->y + 16, 12, textColor);
        }

        curX += tabW + 4;
    }

    /* [+] Add sheet button */
    if (wb->tabCount < SHEET_MAX_TABS) {
        int addW = 24;
        ChDrawRect(win->canv, curX, wid->y + 2, addW, tabH, COLOR_TAB_INACTIVE_BG);
        ChDrawRectUnfilled(win->canv, curX, wid->y + 2, addW, tabH, COLOR_FORMULA_BORDER);
        ChFontSetSize(g_SheetUI.font, 14);
        ChFontDrawText(win->canv, g_SheetUI.font, (char*)"+", curX + 7, wid->y + 17, 14, COLOR_FORMULA_FX);
    }
}

static void TabStripMouseEvent(ChWidget* wid, ChWindow* win, int x, int y, int button) {
    (void)y;
    if (button != 1) return;
    if (!win || !win->info) return;

    int localX = x - win->info->x - wid->x;
    SheetWorkbook* wb = &g_SheetUI.workbook;
    int curX = 8;
    int tabW = 80;
    size_t now = _KeGetSystemTimerTick();

    for (int i = 0; i < wb->tabCount; i++) {
        if (localX >= curX && localX < curX + tabW) {
            if (g_SheetUI.tabRenaming) {
                if (g_SheetUI.renamingTabIdx == i) {
                    return;
                }
                SheetModelRenameTab(wb, g_SheetUI.renamingTabIdx, g_SheetUI.renameBuffer);
                g_SheetUI.tabRenaming = false;
            }

            /* Double-click check: same tab clicked within 500 ms */
            if (g_SheetUI.lastClickedTab == i && (now - g_SheetUI.lastTabClickTick < 500)) {
                _KePrint("[XESheets] Double click on tab %d -> Rename mode\n", i);
                g_SheetUI.tabRenaming = true;
                g_SheetUI.renamingTabIdx = i;
                strncpy(g_SheetUI.renameBuffer, wb->tabs[i].name, sizeof(g_SheetUI.renameBuffer) - 1);
                g_SheetUI.renameBuffer[sizeof(g_SheetUI.renameBuffer) - 1] = '\0';
                g_SheetUI.renameCursor = (int)strlen(g_SheetUI.renameBuffer);
                g_SheetUI.lastTabClickTick = 0;
                g_SheetUI.lastClickedTab = -1;
                SheetUISetStatus(&g_SheetUI, "Rename Sheet: Enter to commit, Esc to cancel");
                SheetUIRefresh(&g_SheetUI);
                return;
            }

            /* Single click on tab: switch tab */
            g_SheetUI.lastTabClickTick = now;
            g_SheetUI.lastClickedTab = i;
            _KePrint("[XESheets] Switch tab to %d\n", i);
            SheetModelSwitchTab(wb, i);
            SheetUISyncSelection(&g_SheetUI);
            SheetUIRefresh(&g_SheetUI);
            return;
        }
        curX += tabW + 4;
    }

    /* Check [+] click */
    if (wb->tabCount < SHEET_MAX_TABS && localX >= curX && localX < curX + 24) {
        if (g_SheetUI.tabRenaming) {
            SheetModelRenameTab(wb, g_SheetUI.renamingTabIdx, g_SheetUI.renameBuffer);
            g_SheetUI.tabRenaming = false;
        }
        _KePrint("[XESheets] Add tab\n");
        SheetModelAddTab(wb, NULL);
        SheetUISyncSelection(&g_SheetUI);
        SheetUIRefresh(&g_SheetUI);
        return;
    }

    /* Click outside tabs */
    if (g_SheetUI.tabRenaming) {
        SheetModelRenameTab(wb, g_SheetUI.renamingTabIdx, g_SheetUI.renameBuffer);
        g_SheetUI.tabRenaming = false;
        SheetUISyncSelection(&g_SheetUI);
        SheetUIRefresh(&g_SheetUI);
    }
}

/* -------------------------------------------------------------
 * Bottom Status Bar Widget
 * ------------------------------------------------------------- */

static void StatusBarPaint(ChWidget* wid, ChWindow* win) {
    ChDrawRect(win->canv, wid->x, wid->y, wid->w, wid->h, COLOR_STATUS_BG);

    /* Left message */
    ChFontSetSize(g_SheetUI.font, 12);
    ChFontDrawText(win->canv, g_SheetUI.font, g_SheetUI.statusMsg, wid->x + 10, wid->y + 16, 12, COLOR_STATUS_TEXT);

    /* Right live statistics (SUM, AVG, COUNT) */
    char statsBuf[96];
    if (g_SheetUI.activeStats.numCount > 0) {
        char sumStr[24], avgStr[24];
        SheetFormatNumber(g_SheetUI.activeStats.sum, sumStr, sizeof(sumStr));
        SheetFormatNumber(g_SheetUI.activeStats.avg, avgStr, sizeof(avgStr));
        snprintf(statsBuf, sizeof(statsBuf), "SUM: %s   AVG: %s   COUNT: %d",
                 sumStr, avgStr, g_SheetUI.activeStats.count);
    } else if (g_SheetUI.activeStats.count > 0) {
        snprintf(statsBuf, sizeof(statsBuf), "COUNT: %d", g_SheetUI.activeStats.count);
    } else {
        snprintf(statsBuf, sizeof(statsBuf), "Ready");
    }

    int64_t sW = ChFontGetWidth(g_SheetUI.font, statsBuf);
    int sX = wid->x + wid->w - (int)sW - 12;
    if (sX > wid->x + 150) {
        ChFontDrawText(win->canv, g_SheetUI.font, statsBuf, sX, wid->y + 16, 12, COLOR_STATUS_TEXT);
    }
}

/* -------------------------------------------------------------
 * UI Synchronization & Lifecycle
 * ------------------------------------------------------------- */

static void GridSelectionCallback(SheetGridWidget* grid, ChWindow* win) {
    (void)grid;
    (void)win;
    g_SheetUI.formulaFocused = false;
    bool hadRename = g_SheetUI.tabRenaming;
    if (g_SheetUI.tabRenaming) {
        SheetModelRenameTab(&g_SheetUI.workbook, g_SheetUI.renamingTabIdx, g_SheetUI.renameBuffer);
        g_SheetUI.tabRenaming = false;
    }
    SheetUISyncSelection(&g_SheetUI);
    if (g_SheetUI.formulaBar.ChPaintHandler) {
        g_SheetUI.formulaBar.ChPaintHandler(&g_SheetUI.formulaBar, g_SheetUI.window);
        ChWindowUpdate(g_SheetUI.window, g_SheetUI.formulaBar.x, g_SheetUI.formulaBar.y,
                       g_SheetUI.formulaBar.w, g_SheetUI.formulaBar.h, 0, 1);
    }
    if (hadRename && g_SheetUI.tabStrip.ChPaintHandler) {
        g_SheetUI.tabStrip.ChPaintHandler(&g_SheetUI.tabStrip, g_SheetUI.window);
        ChWindowUpdate(g_SheetUI.window, g_SheetUI.tabStrip.x, g_SheetUI.tabStrip.y,
                       g_SheetUI.tabStrip.w, g_SheetUI.tabStrip.h, 0, 1);
    }
    if (g_SheetUI.statusBar.ChPaintHandler) {
        g_SheetUI.statusBar.ChPaintHandler(&g_SheetUI.statusBar, g_SheetUI.window);
        ChWindowUpdate(g_SheetUI.window, g_SheetUI.statusBar.x, g_SheetUI.statusBar.y,
                       g_SheetUI.statusBar.w, g_SheetUI.statusBar.h, 0, 1);
    }

    /* Update Bold ribbon button to reflect active cell's bold state only if state changed */
    SheetCell* actCell = SheetModelGetActiveCell(&g_SheetUI.workbook, g_SheetUI.grid->activeCol, g_SheetUI.grid->activeRow);
    bool isBold = (actCell && (actCell->format & CELL_FMT_BOLD));
    for (int i = 0; i < g_SheetUI.buttonCount; i++) {
        if (g_SheetUI.buttons[i] && g_SheetUI.buttons[i]->label && strcmp(g_SheetUI.buttons[i]->label, "Bold") == 0) {
            if (g_SheetUI.buttons[i]->base.clicked != isBold) {
                g_SheetUI.buttons[i]->base.clicked = isBold;
                if (g_SheetUI.buttons[i]->base.ChPaintHandler) {
                    g_SheetUI.buttons[i]->base.ChPaintHandler((ChWidget*)g_SheetUI.buttons[i], g_SheetUI.window);
                    ChWindowUpdate(g_SheetUI.window, g_SheetUI.buttons[i]->base.x, g_SheetUI.buttons[i]->base.y,
                                   g_SheetUI.buttons[i]->base.w, g_SheetUI.buttons[i]->base.h, 0, 1);
                }
            }
            break;
        }
    }
}


void SheetUISyncSelection(SheetUI* ui) {
    if (!ui || !ui->grid) return;
    SheetCell* cell = SheetModelGetActiveCell(&ui->workbook, ui->grid->activeCol, ui->grid->activeRow);
    if (cell) {
        strncpy(ui->formulaText, cell->rawText, sizeof(ui->formulaText) - 1);
        ui->formulaText[sizeof(ui->formulaText) - 1] = '\0';
    } else {
        ui->formulaText[0] = '\0';
    }
    ui->formulaCursor = (int)strlen(ui->formulaText);

    /* Compute stats for current range */
    SheetTab* tab = SheetModelGetActiveTab(&ui->workbook);
    if (tab) {
        SheetComputeStats(tab, &ui->grid->selRange, &ui->activeStats);
    }
}

void SheetUISetStatus(SheetUI* ui, const char* msg) {
    if (!ui || !msg) return;
    strncpy(ui->statusMsg, msg, sizeof(ui->statusMsg) - 1);
    ui->statusMsg[sizeof(ui->statusMsg) - 1] = '\0';
}

bool SheetUIHandleKey(SheetUI* ui, int keycode, char ascii) {
    if (!ui) return false;

    if (ui->tabRenaming) {
        int len = (int)strlen(ui->renameBuffer);
        if (ui->renameCursor < 0) ui->renameCursor = 0;
        if (ui->renameCursor > len) ui->renameCursor = len;

        if (keycode == SHEET_KEY_ENTER_STD || ascii == '\n' || ascii == '\r') {
            SheetModelRenameTab(&ui->workbook, ui->renamingTabIdx, ui->renameBuffer);
            ui->tabRenaming = false;
            SheetUISetStatus(ui, "Ready");
            SheetUISyncSelection(ui);
            SheetUIRefresh(ui);
            return true;
        } else if (keycode == KEY_ESCAPE || keycode == SHEET_KEY_ESC_STD) {
            ui->tabRenaming = false;
            SheetUISetStatus(ui, "Ready");
            SheetUISyncSelection(ui);
            SheetUIRefresh(ui);
            return true;
        } else if (keycode == SHEET_KEY_LEFT_EXT || keycode == KEY_LEFT) {
            if (ui->renameCursor > 0) {
                ui->renameCursor--;
                if (ui->tabStrip.ChPaintHandler) ui->tabStrip.ChPaintHandler(&ui->tabStrip, ui->window);
                ChWindowUpdate(ui->window, ui->tabStrip.x, ui->tabStrip.y, ui->tabStrip.w, ui->tabStrip.h, 0, 1);
            }
            return true;
        } else if (keycode == SHEET_KEY_RIGHT_EXT || keycode == KEY_RIGHT) {
            if (ui->renameCursor < len) {
                ui->renameCursor++;
                if (ui->tabStrip.ChPaintHandler) ui->tabStrip.ChPaintHandler(&ui->tabStrip, ui->window);
                ChWindowUpdate(ui->window, ui->tabStrip.x, ui->tabStrip.y, ui->tabStrip.w, ui->tabStrip.h, 0, 1);
            }
            return true;
        } else if (keycode == SHEET_KEY_HOME_EXT || keycode == KEY_HOME) {
            if (ui->renameCursor != 0) {
                ui->renameCursor = 0;
                if (ui->tabStrip.ChPaintHandler) ui->tabStrip.ChPaintHandler(&ui->tabStrip, ui->window);
                ChWindowUpdate(ui->window, ui->tabStrip.x, ui->tabStrip.y, ui->tabStrip.w, ui->tabStrip.h, 0, 1);
            }
            return true;
        } else if (keycode == SHEET_KEY_END_EXT || keycode == KEY_END) {
            if (ui->renameCursor != len) {
                ui->renameCursor = len;
                if (ui->tabStrip.ChPaintHandler) ui->tabStrip.ChPaintHandler(&ui->tabStrip, ui->window);
                ChWindowUpdate(ui->window, ui->tabStrip.x, ui->tabStrip.y, ui->tabStrip.w, ui->tabStrip.h, 0, 1);
            }
            return true;
        } else if (keycode == SHEET_KEY_DEL_EXT || keycode == KEY_DELETE) {
            if (ui->renameCursor < len) {
                memmove(&ui->renameBuffer[ui->renameCursor],
                        &ui->renameBuffer[ui->renameCursor + 1],
                        len - ui->renameCursor);
                if (ui->tabStrip.ChPaintHandler) ui->tabStrip.ChPaintHandler(&ui->tabStrip, ui->window);
                ChWindowUpdate(ui->window, ui->tabStrip.x, ui->tabStrip.y, ui->tabStrip.w, ui->tabStrip.h, 0, 1);
            }
            return true;
        } else if (keycode == SHEET_KEY_BS_STD || ascii == '\b') {
            if (ui->renameCursor > 0) {
                memmove(&ui->renameBuffer[ui->renameCursor - 1],
                        &ui->renameBuffer[ui->renameCursor],
                        len - ui->renameCursor + 1);
                ui->renameCursor--;
                if (ui->tabStrip.ChPaintHandler) ui->tabStrip.ChPaintHandler(&ui->tabStrip, ui->window);
                ChWindowUpdate(ui->window, ui->tabStrip.x, ui->tabStrip.y, ui->tabStrip.w, ui->tabStrip.h, 0, 1);
            }
            return true;
        } else if (ascii >= 32 && ascii <= 126) {
            if (len < (int)sizeof(ui->renameBuffer) - 1) {
                memmove(&ui->renameBuffer[ui->renameCursor + 1],
                        &ui->renameBuffer[ui->renameCursor],
                        len - ui->renameCursor + 1);
                ui->renameBuffer[ui->renameCursor] = ascii;
                ui->renameCursor++;
                if (ui->tabStrip.ChPaintHandler) ui->tabStrip.ChPaintHandler(&ui->tabStrip, ui->window);
                ChWindowUpdate(ui->window, ui->tabStrip.x, ui->tabStrip.y, ui->tabStrip.w, ui->tabStrip.h, 0, 1);
            }
            return true;
        }
        return true;
    }

    if (ui->formulaFocused) {
        int len = (int)strlen(ui->formulaText);
        if (ui->formulaCursor < 0) ui->formulaCursor = 0;
        if (ui->formulaCursor > len) ui->formulaCursor = len;

        if (keycode == SHEET_KEY_ENTER_STD || ascii == '\n' || ascii == '\r') {
            ui->formulaFocused = false;
            SheetModelSetCellText(&ui->workbook, ui->grid->activeCol, ui->grid->activeRow, ui->formulaText);
            SheetUISyncSelection(ui);
            SheetUIRefresh(ui);
            return true;
        } else if (keycode == KEY_ESCAPE || keycode == SHEET_KEY_ESC_STD) {
            ui->formulaFocused = false;
            SheetUISyncSelection(ui);
            if (ui->formulaBar.ChPaintHandler) ui->formulaBar.ChPaintHandler(&ui->formulaBar, ui->window);
            ChWindowUpdate(ui->window, ui->formulaBar.x, ui->formulaBar.y, ui->formulaBar.w, ui->formulaBar.h, 0, 1);
            return true;
        } else if (keycode == SHEET_KEY_LEFT_EXT || keycode == KEY_LEFT) {
            if (ui->formulaCursor > 0) {
                ui->formulaCursor--;
                if (ui->formulaBar.ChPaintHandler) ui->formulaBar.ChPaintHandler(&ui->formulaBar, ui->window);
                ChWindowUpdate(ui->window, ui->formulaBar.x, ui->formulaBar.y, ui->formulaBar.w, ui->formulaBar.h, 0, 1);
            }
            return true;
        } else if (keycode == SHEET_KEY_RIGHT_EXT || keycode == KEY_RIGHT) {
            if (ui->formulaCursor < len) {
                ui->formulaCursor++;
                if (ui->formulaBar.ChPaintHandler) ui->formulaBar.ChPaintHandler(&ui->formulaBar, ui->window);
                ChWindowUpdate(ui->window, ui->formulaBar.x, ui->formulaBar.y, ui->formulaBar.w, ui->formulaBar.h, 0, 1);
            }
            return true;
        } else if (keycode == SHEET_KEY_HOME_EXT || keycode == KEY_HOME) {
            if (ui->formulaCursor != 0) {
                ui->formulaCursor = 0;
                if (ui->formulaBar.ChPaintHandler) ui->formulaBar.ChPaintHandler(&ui->formulaBar, ui->window);
                ChWindowUpdate(ui->window, ui->formulaBar.x, ui->formulaBar.y, ui->formulaBar.w, ui->formulaBar.h, 0, 1);
            }
            return true;
        } else if (keycode == SHEET_KEY_END_EXT || keycode == KEY_END) {
            if (ui->formulaCursor != len) {
                ui->formulaCursor = len;
                if (ui->formulaBar.ChPaintHandler) ui->formulaBar.ChPaintHandler(&ui->formulaBar, ui->window);
                ChWindowUpdate(ui->window, ui->formulaBar.x, ui->formulaBar.y, ui->formulaBar.w, ui->formulaBar.h, 0, 1);
            }
            return true;
        } else if (keycode == SHEET_KEY_DEL_EXT || keycode == KEY_DELETE) {
            if (ui->formulaCursor < len) {
                memmove(&ui->formulaText[ui->formulaCursor],
                        &ui->formulaText[ui->formulaCursor + 1],
                        len - ui->formulaCursor);
                if (ui->formulaBar.ChPaintHandler) ui->formulaBar.ChPaintHandler(&ui->formulaBar, ui->window);
                ChWindowUpdate(ui->window, ui->formulaBar.x, ui->formulaBar.y, ui->formulaBar.w, ui->formulaBar.h, 0, 1);
            }
            return true;
        } else if (keycode == SHEET_KEY_BS_STD || ascii == '\b') {
            if (ui->formulaCursor > 0) {
                memmove(&ui->formulaText[ui->formulaCursor - 1],
                        &ui->formulaText[ui->formulaCursor],
                        len - ui->formulaCursor + 1);
                ui->formulaCursor--;
                if (ui->formulaBar.ChPaintHandler) ui->formulaBar.ChPaintHandler(&ui->formulaBar, ui->window);
                ChWindowUpdate(ui->window, ui->formulaBar.x, ui->formulaBar.y, ui->formulaBar.w, ui->formulaBar.h, 0, 1);
            }
            return true;
        } else if (ascii >= 32 && ascii <= 126) {
            if (len < (int)sizeof(ui->formulaText) - 1) {
                memmove(&ui->formulaText[ui->formulaCursor + 1],
                        &ui->formulaText[ui->formulaCursor],
                        len - ui->formulaCursor + 1);
                ui->formulaText[ui->formulaCursor] = ascii;
                ui->formulaCursor++;
                if (ui->formulaBar.ChPaintHandler) ui->formulaBar.ChPaintHandler(&ui->formulaBar, ui->window);
                ChWindowUpdate(ui->window, ui->formulaBar.x, ui->formulaBar.y, ui->formulaBar.w, ui->formulaBar.h, 0, 1);
            }
            return true;
        }
        return true;
    }

    return SheetGridHandleKey(ui->grid, ui->window, keycode, ascii);
}

void SheetUIInit(SheetUI* ui, ChWindow* win, ChFont* font) {
    if (!ui || !win || !font) return;
    memset(ui, 0, sizeof(SheetUI));

    ui->window = win;
    ui->font = font;
    SheetModelInit(&ui->workbook);
    strncpy(ui->statusMsg, "Ready", sizeof(ui->statusMsg) - 1);

    int winW = win->info->width;
    int winH = win->info->height;

    /* Row 1: Action Ribbon Bar: Y = 26, H = 32 */
    int ribbonY = 26;
    int ribbonH = 32;

    ui->ribbonBar.x = 0;
    ui->ribbonBar.y = ribbonY;
    ui->ribbonBar.w = winW;
    ui->ribbonBar.h = ribbonH;
    ui->ribbonBar.visible = true;
    ui->ribbonBar.ChPaintHandler = RibbonBarPaint;
    ChWindowAddWidget(win, &ui->ribbonBar);

    /* Flat modern Ribbon Buttons inside Ribbon Bar at Y = 29, H = 26 */
    int btnY = ribbonY + 3;
    int btnH = 26;
    int curX = 8;

    ui->buttonCount = 0;
    ui->buttons[ui->buttonCount++] = CreateRibbonBtn(win, curX, btnY, 46, btnH, "New", ActionNew);
    curX += 50;

    ui->buttons[ui->buttonCount++] = CreateRibbonBtn(win, curX, btnY, 48, btnH, "Open", ActionOpen);
    curX += 52;

    ui->buttons[ui->buttonCount++] = CreateRibbonBtn(win, curX, btnY, 48, btnH, "Save", ActionSave);
    curX += 56;

    ui->buttons[ui->buttonCount++] = CreateRibbonBtn(win, curX, btnY, 54, btnH, "+ Sum", ActionSum);
    curX += 58;

    ui->buttons[ui->buttonCount++] = CreateRibbonBtn(win, curX, btnY, 46, btnH, "Avg", ActionAvg);
    curX += 52;

    ui->buttons[ui->buttonCount++] = CreateRibbonBtn(win, curX, btnY, 64, btnH, "Sort A-Z", ActionSortAsc);
    curX += 68;

    ui->buttons[ui->buttonCount++] = CreateRibbonBtn(win, curX, btnY, 64, btnH, "Sort Z-A", ActionSortDesc);
    curX += 70;

    ui->buttons[ui->buttonCount++] = CreateRibbonBtn(win, curX, btnY, 44, btnH, "Bold", ActionBold);
    curX += 48;

    ui->buttons[ui->buttonCount++] = CreateRibbonBtn(win, curX, btnY, 46, btnH, "Align", ActionAlign);
    curX += 50;

    ui->buttons[ui->buttonCount++] = CreateRibbonBtn(win, curX, btnY, 50, btnH, "Clear", ActionClear);

    /* Row 2: Formula Bar: Y = 58, H = 28 */
    int formulaY = ribbonY + ribbonH;
    int formulaH = 28;

    ui->formulaBar.x = 0;
    ui->formulaBar.y = formulaY;
    ui->formulaBar.w = winW;
    ui->formulaBar.h = formulaH;
    ui->formulaBar.visible = true;
    ui->formulaBar.ChPaintHandler = FormulaBarPaint;
    ui->formulaBar.ChMouseEvent = FormulaBarMouseEvent;
    ChWindowAddWidget(win, &ui->formulaBar);

    /* Row 4 & 5: Bottom Bars */
    int statusBarH = 24;
    int tabStripH = 26;

    /* Row 3: Grid Area: Y = 86, H = winH - 86 - tabStripH - statusBarH */
    int gridY = formulaY + formulaH;
    int gridH = winH - gridY - tabStripH - statusBarH;

    ui->grid = SheetGridCreate(0, gridY, winW, gridH, &ui->workbook, font);
    ui->grid->OnSelectionChanged = GridSelectionCallback;
    ChWindowAddWidget(win, (ChWidget*)ui->grid);

    /* Row 4: Bottom Tab Strip: Y = winH - tabStripH - statusBarH, H = 26 */
    ui->tabStrip.x = 0;
    ui->tabStrip.y = winH - tabStripH - statusBarH;
    ui->tabStrip.w = winW;
    ui->tabStrip.h = tabStripH;
    ui->tabStrip.visible = true;
    ui->tabStrip.ChPaintHandler = TabStripPaint;
    ui->tabStrip.ChMouseEvent = TabStripMouseEvent;
    ChWindowAddWidget(win, &ui->tabStrip);

    /* Row 5: Bottom Status Bar: Y = winH - statusBarH, H = 24 */
    ui->statusBar.x = 0;
    ui->statusBar.y = winH - statusBarH;
    ui->statusBar.w = winW;
    ui->statusBar.h = statusBarH;
    ui->statusBar.visible = true;
    ui->statusBar.ChPaintHandler = StatusBarPaint;
    ChWindowAddWidget(win, &ui->statusBar);

    /* Initialize clean empty workbook state */
    ui->workbook.isDirty = false;
    SheetUISetStatus(ui, "Ready");
    SheetUISyncSelection(ui);
}
