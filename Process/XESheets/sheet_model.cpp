/**
 * @file sheet_model.cpp
 * @brief Data model, multi-sheet workbook management, and cell operations for XESheets
 */

#include "sheet_model.h"
#include "sheet_eval.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void InitSingleTab(SheetTab* tab, const char* name) {
    if (!tab) return;
    memset(tab, 0, sizeof(SheetTab));
    if (name && name[0]) {
        strncpy(tab->name, name, sizeof(tab->name) - 1);
    } else {
        snprintf(tab->name, sizeof(tab->name), "Sheet1");
    }

    for (int c = 0; c < SHEET_MAX_COLS; c++) {
        tab->colWidths[c] = SHEET_DEFAULT_COL_W;
    }
    for (int r = 0; r < SHEET_MAX_ROWS; r++) {
        tab->rowHeights[r] = SHEET_DEFAULT_ROW_H;
        for (int c = 0; c < SHEET_MAX_COLS; c++) {
            tab->cells[r][c].bgColor = 0xFFFFFFFF; /* White */
            tab->cells[r][c].textColor = 0xFF000000; /* Black */
            tab->cells[r][c].format = CELL_FMT_ALIGN_LEFT;
        }
    }
}

void SheetModelInit(SheetWorkbook* wb) {
    if (!wb) return;
    memset(wb, 0, sizeof(SheetWorkbook));
    wb->tabCount = 1;
    wb->activeTab = 0;
    wb->isDirty = false;
    wb->filepath[0] = '\0';

    InitSingleTab(&wb->tabs[0], "Sheet1");
}

SheetTab* SheetModelGetActiveTab(SheetWorkbook* wb) {
    if (!wb || wb->tabCount <= 0 || wb->activeTab < 0 || wb->activeTab >= wb->tabCount) {
        return NULL;
    }
    return &wb->tabs[wb->activeTab];
}

int SheetModelAddTab(SheetWorkbook* wb, const char* name) {
    if (!wb || wb->tabCount >= SHEET_MAX_TABS) return -1;

    char defaultName[SHEET_TAB_NAME_LEN];
    if (!name || name[0] == '\0') {
        snprintf(defaultName, sizeof(defaultName), "Sheet%d", wb->tabCount + 1);
        name = defaultName;
    }

    int newIdx = wb->tabCount;
    InitSingleTab(&wb->tabs[newIdx], name);
    wb->tabCount++;
    wb->activeTab = newIdx;
    wb->isDirty = true;
    return newIdx;
}

bool SheetModelSwitchTab(SheetWorkbook* wb, int index) {
    if (!wb || index < 0 || index >= wb->tabCount) return false;
    wb->activeTab = index;
    return true;
}

bool SheetModelRenameTab(SheetWorkbook* wb, int index, const char* newName) {
    if (!wb || index < 0 || index >= wb->tabCount || !newName || !newName[0]) return false;
    strncpy(wb->tabs[index].name, newName, SHEET_TAB_NAME_LEN - 1);
    wb->tabs[index].name[SHEET_TAB_NAME_LEN - 1] = '\0';
    wb->isDirty = true;
    return true;
}

bool SheetModelDeleteTab(SheetWorkbook* wb, int index) {
    if (!wb || wb->tabCount <= 1 || index < 0 || index >= wb->tabCount) return false;

    for (int i = index; i < wb->tabCount - 1; i++) {
        wb->tabs[i] = wb->tabs[i + 1];
    }
    wb->tabCount--;

    if (wb->activeTab >= wb->tabCount) {
        wb->activeTab = wb->tabCount - 1;
    }
    wb->isDirty = true;
    return true;
}

SheetCell* SheetModelGetActiveCell(SheetWorkbook* wb, int col, int row) {
    SheetTab* tab = SheetModelGetActiveTab(wb);
    if (!tab || col < 0 || col >= SHEET_MAX_COLS || row < 0 || row >= SHEET_MAX_ROWS) {
        return NULL;
    }
    return &tab->cells[row][col];
}

void SheetModelSetCellText(SheetWorkbook* wb, int col, int row, const char* text) {
    SheetTab* tab = SheetModelGetActiveTab(wb);
    if (!tab || col < 0 || col >= SHEET_MAX_COLS || row < 0 || row >= SHEET_MAX_ROWS) return;

    SheetCell* cell = &tab->cells[row][col];
    if (text) {
        strncpy(cell->rawText, text, SHEET_CELL_TEXT_LEN - 1);
        cell->rawText[SHEET_CELL_TEXT_LEN - 1] = '\0';
    } else {
        cell->rawText[0] = '\0';
    }

    wb->isDirty = true;
    SheetEvaluateAll(tab);
}

void SheetModelToggleFormat(SheetWorkbook* wb, int col, int row, uint16_t fmtFlag) {
    SheetTab* tab = SheetModelGetActiveTab(wb);
    if (!tab || col < 0 || col >= SHEET_MAX_COLS || row < 0 || row >= SHEET_MAX_ROWS) return;

    SheetCell* cell = &tab->cells[row][col];
    cell->format ^= fmtFlag;
    wb->isDirty = true;
}

void SheetModelSetRangeFormat(SheetWorkbook* wb, const SheetRange* range, uint16_t fmtFlag, bool enable) {
    SheetTab* tab = SheetModelGetActiveTab(wb);
    if (!tab || !range) return;

    for (int r = range->startRow; r <= range->endRow; r++) {
        for (int c = range->startCol; c <= range->endCol; c++) {
            if (enable) {
                tab->cells[r][c].format |= fmtFlag;
            } else {
                tab->cells[r][c].format &= ~fmtFlag;
            }
        }
    }
    wb->isDirty = true;
}

void SheetModelClearRange(SheetWorkbook* wb, const SheetRange* range) {
    SheetTab* tab = SheetModelGetActiveTab(wb);
    if (!tab || !range) return;

    for (int r = range->startRow; r <= range->endRow; r++) {
        for (int c = range->startCol; c <= range->endCol; c++) {
            SheetCell* cell = &tab->cells[r][c];
            cell->rawText[0] = '\0';
            cell->displayText[0] = '\0';
            cell->computedValue = 0.0;
            cell->type = CELL_TYPE_EMPTY;
            cell->error = CELL_ERR_NONE;
            cell->format = CELL_FMT_ALIGN_LEFT;
            cell->bgColor = 0xFFFFFFFF;
            cell->textColor = 0xFF000000;
        }
    }
    wb->isDirty = true;
    SheetEvaluateAll(tab);
}

/* Stable sort row range by keyCol */
void SheetModelSortRange(SheetWorkbook* wb, const SheetRange* range, int keyCol, bool ascending) {
    SheetTab* tab = SheetModelGetActiveTab(wb);
    if (!tab || !range) return;

    int rStart = range->startRow;
    int rEnd = range->endRow;
    int cStart = range->startCol;
    int cEnd = range->endCol;

    /* If single cell or row selected, auto-detect data table boundaries */
    if (rStart >= rEnd) {
        int maxR = 0;
        int maxC = 0;
        for (int r = 0; r < SHEET_MAX_ROWS; r++) {
            for (int c = 0; c < SHEET_MAX_COLS; c++) {
                if (tab->cells[r][c].type != CELL_TYPE_EMPTY && tab->cells[r][c].rawText[0] != '\0') {
                    if (r > maxR) maxR = r;
                    if (c > maxC) maxC = c;
                }
            }
        }
        if (maxR < 1) return;

        cStart = 0;
        cEnd = maxC;
        rStart = 1; /* Preserve header row 0 */
        rEnd = maxR;

        /* If the last row is a summary / total row (e.g. contains "Total"), exclude it from sorting */
        char firstCellText[SHEET_DISP_TEXT_LEN];
        strncpy(firstCellText, tab->cells[rEnd][0].displayText, sizeof(firstCellText) - 1);
        firstCellText[sizeof(firstCellText) - 1] = '\0';
        for (int k = 0; firstCellText[k]; k++) {
            if (firstCellText[k] >= 'A' && firstCellText[k] <= 'Z') {
                firstCellText[k] = (char)(firstCellText[k] + 32);
            }
        }
        if (strstr(firstCellText, "total") != NULL && rEnd > rStart) {
            rEnd--;
        }
    } else {
        /* When range is explicitly selected across 1 column, sort all table columns together */
        if (cStart == cEnd) {
            int maxC = 0;
            for (int r = rStart; r <= rEnd; r++) {
                for (int c = 0; c < SHEET_MAX_COLS; c++) {
                    if (tab->cells[r][c].type != CELL_TYPE_EMPTY && tab->cells[r][c].rawText[0] != '\0') {
                        if (c > maxC) maxC = c;
                    }
                }
            }
            cStart = 0;
            cEnd = maxC;
        }
    }

    if (rStart >= rEnd) return;
    if (keyCol < cStart) keyCol = cStart;
    if (keyCol > cEnd) keyCol = cEnd;

    /* Insertion sort for stable in-place row reordering across cStart..cEnd */
    for (int i = rStart + 1; i <= rEnd; i++) {
        SheetCell tempRow[SHEET_MAX_COLS];
        for (int c = cStart; c <= cEnd; c++) {
            tempRow[c] = tab->cells[i][c];
        }

        int j = i - 1;
        while (j >= rStart) {
            SheetCell* cellA = &tab->cells[j][keyCol];
            SheetCell* cellB = &tempRow[keyCol];

            int cmp = 0;
            bool bothNums = (cellA->type == CELL_TYPE_NUMBER || (cellA->type == CELL_TYPE_FORMULA && cellA->error == CELL_ERR_NONE)) &&
                            (cellB->type == CELL_TYPE_NUMBER || (cellB->type == CELL_TYPE_FORMULA && cellB->error == CELL_ERR_NONE));

            if (bothNums) {
                if (cellA->computedValue < cellB->computedValue) cmp = -1;
                else if (cellA->computedValue > cellB->computedValue) cmp = 1;
                else cmp = 0;
            } else {
                cmp = strcmp(cellA->displayText, cellB->displayText);
            }

            bool swap = ascending ? (cmp > 0) : (cmp < 0);
            if (!swap) break;

            for (int c = cStart; c <= cEnd; c++) {
                tab->cells[j + 1][c] = tab->cells[j][c];
            }
            j--;
        }

        for (int c = cStart; c <= cEnd; c++) {
            tab->cells[j + 1][c] = tempRow[c];
        }
    }

    wb->isDirty = true;
    SheetEvaluateAll(tab);
}
