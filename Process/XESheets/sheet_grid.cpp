/**
 * @file sheet_grid.cpp
 * @brief Virtualized interactive spreadsheet grid widget for XESheets
 */

#include "sheet_grid.h"
#include "sheet_model.h"
#include "sheet_eval.h"
#include <keycode.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define COLOR_GRID_HEADER_BG    0xFFF0F3F4
#define COLOR_GRID_HEADER_BORDER 0xFFD2D6D9
#define COLOR_GRID_HEADER_TEXT  0xFF444746
#define COLOR_GRID_ACTIVE_ACCENT 0xFF107C41
#define COLOR_GRID_SEL_FILL     0x20107C41
#define COLOR_GRID_LINE         0xFFE0E3E5
#define COLOR_GRID_CELL_BG      0xFFFFFFFF
#define COLOR_GRID_TEXT         0xFF1F1F1F
#define COLOR_GRID_NUM_TEXT     0xFF104E27
#define COLOR_GRID_ERR_TEXT     0xFFD93025

static int GetColX(SheetGridWidget* grid, SheetTab* tab, int targetCol) {
    int curX = grid->base.x + SHEET_ROW_HEADER_W;
    for (int c = grid->scrollCol; c < targetCol && c < SHEET_MAX_COLS; c++) {
        curX += tab->colWidths[c];
    }
    return curX;
}

static int GetRowY(SheetGridWidget* grid, SheetTab* tab, int targetRow) {
    int curY = grid->base.y + SHEET_COL_HEADER_H;
    for (int r = grid->scrollRow; r < targetRow && r < SHEET_MAX_ROWS; r++) {
        curY += tab->rowHeights[r];
    }
    return curY;
}

void SheetGridScrollToActive(SheetGridWidget* grid) {
    if (!grid) return;
    SheetTab* tab = SheetModelGetActiveTab(grid->workbook);
    if (!tab) return;

    if (grid->activeCol < grid->scrollCol) {
        grid->scrollCol = grid->activeCol;
    }
    if (grid->activeRow < grid->scrollRow) {
        grid->scrollRow = grid->activeRow;
    }

    int gridVisW = grid->base.w - SHEET_ROW_HEADER_W;
    while (grid->scrollCol < grid->activeCol) {
        int x = GetColX(grid, tab, grid->activeCol) - (grid->base.x + SHEET_ROW_HEADER_W);
        if (x + tab->colWidths[grid->activeCol] <= gridVisW) {
            break;
        }
        grid->scrollCol++;
    }

    int gridVisH = grid->base.h - SHEET_COL_HEADER_H;
    while (grid->scrollRow < grid->activeRow) {
        int y = GetRowY(grid, tab, grid->activeRow) - (grid->base.y + SHEET_COL_HEADER_H);
        if (y + tab->rowHeights[grid->activeRow] <= gridVisH) {
            break;
        }
        grid->scrollRow++;
    }
}

void SheetGridCommitEdit(SheetGridWidget* grid, ChWindow* win) {
    if (!grid || !grid->isEditing) return;

    grid->isEditing = false;
    SheetModelSetCellText(grid->workbook, grid->activeCol, grid->activeRow, grid->editBuffer);
    grid->anchorCol = grid->activeCol;
    grid->anchorRow = grid->activeRow;

    if (grid->OnCellEdited) {
        grid->OnCellEdited(grid, win, grid->activeCol, grid->activeRow, grid->editBuffer);
    }
    if (grid->OnSelectionChanged) {
        grid->OnSelectionChanged(grid, win);
    }
}

void SheetGridCancelEdit(SheetGridWidget* grid, ChWindow* win) {
    if (!grid || !grid->isEditing) return;

    grid->isEditing = false;
    if (grid->OnSelectionChanged) {
        grid->OnSelectionChanged(grid, win);
    }
}

void SheetGridStartEdit(SheetGridWidget* grid, ChWindow* win, const char* initialText) {
    if (!grid) return;
    (void)win;

    grid->isEditing = true;
    grid->anchorCol = grid->activeCol;
    grid->anchorRow = grid->activeRow;
    if (initialText) {
        strncpy(grid->editBuffer, initialText, sizeof(grid->editBuffer) - 1);
        grid->editBuffer[sizeof(grid->editBuffer) - 1] = '\0';
    } else {
        SheetCell* cell = SheetModelGetActiveCell(grid->workbook, grid->activeCol, grid->activeRow);
        if (cell) {
            strncpy(grid->editBuffer, cell->rawText, sizeof(grid->editBuffer) - 1);
            grid->editBuffer[sizeof(grid->editBuffer) - 1] = '\0';
        } else {
            grid->editBuffer[0] = '\0';
        }
    }
    grid->editCursor = (int)strlen(grid->editBuffer);
}

/* Paint Handler */
static void SheetGridPaint(ChWidget* wid, ChWindow* win) {
    SheetGridWidget* grid = (SheetGridWidget*)wid;
    SheetTab* tab = SheetModelGetActiveTab(grid->workbook);
    if (!tab) return;

    /* Base background fill */
    ChDrawRect(win->canv, wid->x, wid->y, wid->w, wid->h, COLOR_GRID_CELL_BG);

    /* Corner Header (top-left) */
    ChDrawRect(win->canv, wid->x, wid->y, SHEET_ROW_HEADER_W, SHEET_COL_HEADER_H, COLOR_GRID_HEADER_BG);
    ChDrawRectUnfilled(win->canv, wid->x, wid->y, SHEET_ROW_HEADER_W, SHEET_COL_HEADER_H, COLOR_GRID_HEADER_BORDER);

    /* Column Headers (A, B, C...) */
    int curX = wid->x + SHEET_ROW_HEADER_W;
    for (int c = grid->scrollCol; c < SHEET_MAX_COLS; c++) {
        int colW = tab->colWidths[c];
        if (curX >= wid->x + wid->w) break;

        bool isColActive = (c == grid->activeCol || (c >= grid->selRange.startCol && c <= grid->selRange.endCol));
        uint32_t headerBg = isColActive ? 0xFFE2E7E9 : COLOR_GRID_HEADER_BG;
        ChDrawRect(win->canv, curX, wid->y, colW, SHEET_COL_HEADER_H, headerBg);
        ChDrawRectUnfilled(win->canv, curX, wid->y, colW, SHEET_COL_HEADER_H, COLOR_GRID_HEADER_BORDER);

        if (isColActive) {
            ChDrawHorizontalLine(win->canv, curX, wid->y + SHEET_COL_HEADER_H - 2, colW, COLOR_GRID_ACTIVE_ACCENT);
            ChDrawHorizontalLine(win->canv, curX, wid->y + SHEET_COL_HEADER_H - 1, colW, COLOR_GRID_ACTIVE_ACCENT);
        }

        /* Column letter */
        char colLabel[4];
        colLabel[0] = (char)('A' + c);
        colLabel[1] = '\0';

        int64_t textW = ChFontGetWidth(grid->font, colLabel);
        int textX = curX + (colW - (int)textW) / 2;
        int textY = wid->y + 15;
        ChFontDrawText(win->canv, grid->font, colLabel, textX, textY, 13, isColActive ? COLOR_GRID_ACTIVE_ACCENT : COLOR_GRID_HEADER_TEXT);

        curX += colW;
    }

    /* Row Headers (1, 2, 3...) - Set font size 12 once */
    ChFontSetSize(grid->font, 12);
    int curY = wid->y + SHEET_COL_HEADER_H;
    int maxY = wid->y + wid->h;
    int maxX = wid->x + wid->w;

    for (int r = grid->scrollRow; r < SHEET_MAX_ROWS; r++) {
        int rowH = tab->rowHeights[r];
        if (curY >= maxY) break;
        int drawH = (curY + rowH > maxY) ? (maxY - curY) : rowH;
        if (drawH <= 0) break;

        bool isRowActive = (r == grid->activeRow || (r >= grid->selRange.startRow && r <= grid->selRange.endRow));
        uint32_t headerBg = isRowActive ? 0xFFE2E7E9 : COLOR_GRID_HEADER_BG;
        ChDrawRect(win->canv, wid->x, curY, SHEET_ROW_HEADER_W, drawH, headerBg);
        ChDrawRectUnfilled(win->canv, wid->x, curY, SHEET_ROW_HEADER_W, drawH, COLOR_GRID_HEADER_BORDER);

        if (isRowActive) {
            ChDrawVerticalLine(win->canv, wid->x + SHEET_ROW_HEADER_W - 2, curY, drawH, COLOR_GRID_ACTIVE_ACCENT);
            ChDrawVerticalLine(win->canv, wid->x + SHEET_ROW_HEADER_W - 1, curY, drawH, COLOR_GRID_ACTIVE_ACCENT);
        }

        /* Row number */
        if (drawH >= 12) {
            char rowLabel[8];
            snprintf(rowLabel, sizeof(rowLabel), "%d", r + 1);

            int64_t textW = ChFontGetWidth(grid->font, rowLabel);
            int textX = wid->x + (SHEET_ROW_HEADER_W - (int)textW) / 2;
            int textY = curY + 15;
            ChRect rclip = {wid->x, curY, SHEET_ROW_HEADER_W, drawH};
            ChFontDrawTextClipped(win->canv, grid->font, rowLabel, textX, textY, isRowActive ? COLOR_GRID_ACTIVE_ACCENT : COLOR_GRID_HEADER_TEXT, &rclip);
        }

        curY += rowH;
    }

    /* Grid Cells - Set font size 13 once for butter-smooth rendering */
    ChFontSetSize(grid->font, 13);
    curY = wid->y + SHEET_COL_HEADER_H;
    for (int r = grid->scrollRow; r < SHEET_MAX_ROWS; r++) {
        int rowH = tab->rowHeights[r];
        if (curY >= maxY) break;
        int drawH = (curY + rowH > maxY) ? (maxY - curY) : rowH;
        if (drawH <= 0) break;

        curX = wid->x + SHEET_ROW_HEADER_W;
        for (int c = grid->scrollCol; c < SHEET_MAX_COLS; c++) {
            int colW = tab->colWidths[c];
            if (curX >= maxX) break;
            int drawW = (curX + colW > maxX) ? (maxX - curX) : colW;
            if (drawW <= 0) break;

            SheetCell* cell = &tab->cells[r][c];

            /* Check range selection */
            bool inRange = (c >= grid->selRange.startCol && c <= grid->selRange.endCol &&
                            r >= grid->selRange.startRow && r <= grid->selRange.endRow);

            /* Fill background */
            if (cell->bgColor != 0xFFFFFFFF) {
                ChDrawRect(win->canv, curX, curY, drawW, drawH, cell->bgColor);
            } else if (inRange && (c != grid->activeCol || r != grid->activeRow)) {
                ChDrawRect(win->canv, curX, curY, drawW, drawH, 0xFFE8F0FE); /* Google Sheets soft blue selection */
            }

            /* Grid border line */
            ChDrawRectUnfilled(win->canv, curX, curY, drawW, drawH, COLOR_GRID_LINE);

            /* Cell text */
            if (grid->isEditing && r == grid->activeRow && c == grid->activeCol) {
                /* In-cell editor preview */
                if (drawW > 2 && drawH > 2) {
                    ChDrawRect(win->canv, curX + 1, curY + 1, drawW - 2, drawH - 2, COLOR_GRID_CELL_BG);
                }
                ChRect clip = {curX + 4, curY + 2, drawW - 8, drawH - 4};
                ChFontDrawTextClipped(win->canv, grid->font, grid->editBuffer, curX + 4, curY + 15, COLOR_GRID_TEXT, &clip);

                /* Cursor */
                int64_t curTextW = ChFontGetWidth(grid->font, grid->editBuffer);
                int curLineX = curX + 4 + (int)curTextW;
                if (curLineX < curX + drawW - 4 && drawH > 6) {
                    ChDrawVerticalLine(win->canv, curLineX, curY + 3, drawH - 6, COLOR_GRID_ACTIVE_ACCENT);
                }
            } else if (cell->displayText[0] != '\0') {
                int64_t textW = ChFontGetWidth(grid->font, cell->displayText);
                int textX = curX + 4;

                if (cell->format & CELL_FMT_ALIGN_CENTER) {
                    textX = curX + (colW - (int)textW) / 2;
                } else if ((cell->format & CELL_FMT_ALIGN_RIGHT) || (cell->type == CELL_TYPE_NUMBER)) {
                    textX = curX + colW - 6 - (int)textW;
                    if (textX < curX + 4) textX = curX + 4;
                }

                uint32_t textColor = cell->textColor;
                if (cell->error != CELL_ERR_NONE) {
                    textColor = COLOR_GRID_ERR_TEXT;
                } else if (cell->type == CELL_TYPE_NUMBER) {
                    textColor = COLOR_GRID_NUM_TEXT;
                }

                ChRect clip = {curX + 2, curY + 2, drawW - 4, drawH - 4};
                ChFontDrawTextClipped(win->canv, grid->font, cell->displayText, textX, curY + 15, textColor, &clip);
                /* Algorithmic bold overstrike: draw again offset by 1px */
                if (cell->format & CELL_FMT_BOLD) {
                    ChFontDrawTextClipped(win->canv, grid->font, cell->displayText, textX + 1, curY + 15, textColor, &clip);
                }
            }

            curX += colW;
        }
        curY += rowH;
    }

    /* Draw Active Cell Cursor (Excel emerald green 2px outline) */
    if (grid->activeCol >= grid->scrollCol && grid->activeRow >= grid->scrollRow) {
        int actX = GetColX(grid, tab, grid->activeCol);
        int actY = GetRowY(grid, tab, grid->activeRow);
        int actW = tab->colWidths[grid->activeCol];
        int actH = tab->rowHeights[grid->activeRow];

        if (actX < maxX && actY < maxY) {
            int drawW = (actX + actW > maxX) ? (maxX - actX) : actW;
            int drawH = (actY + actH > maxY) ? (maxY - actY) : actH;
            if (drawW > 2 && drawH > 2) {
                ChDrawRectUnfilled(win->canv, actX, actY, drawW, drawH, COLOR_GRID_ACTIVE_ACCENT);
                ChDrawRectUnfilled(win->canv, actX + 1, actY + 1, drawW - 2, drawH - 2, COLOR_GRID_ACTIVE_ACCENT);

                /* Fill handle square at bottom right */
                int handleX = actX + drawW - 4;
                int handleY = actY + drawH - 4;
                if (handleX + 5 <= maxX && handleY + 5 <= maxY) {
                    ChDrawRect(win->canv, handleX, handleY, 5, 5, COLOR_GRID_ACTIVE_ACCENT);
                }
            }
        }
    }

    /* Clean bottom border line separating grid from tab strip */
    ChDrawHorizontalLine(win->canv, wid->x, maxY - 1, wid->w, COLOR_GRID_HEADER_BORDER);
}

void SheetGridRepaint(SheetGridWidget* grid, ChWindow* win) {
    if (!grid || !win) return;
    if (grid->base.ChPaintHandler) {
        grid->base.ChPaintHandler((ChWidget*)grid, win);
    }
    SheetWindowUpdate(win, grid->base.x, grid->base.y, grid->base.w, grid->base.h, false);
}

void SheetGridRepaintActiveCell(SheetGridWidget* grid, ChWindow* win) {
    if (!grid || !win) return;
    SheetTab* tab = SheetModelGetActiveTab(grid->workbook);
    if (!tab) return;

    if (grid->activeCol < grid->scrollCol || grid->activeRow < grid->scrollRow) return;

    int actX = GetColX(grid, tab, grid->activeCol);
    int actY = GetRowY(grid, tab, grid->activeRow);
    int actW = tab->colWidths[grid->activeCol];
    int actH = tab->rowHeights[grid->activeRow];

    int maxX = grid->base.x + grid->base.w;
    int maxY = grid->base.y + grid->base.h;

    if (actX >= maxX || actY >= maxY) return;
    int drawW = (actX + actW > maxX) ? (maxX - actX) : actW;
    int drawH = (actY + actH > maxY) ? (maxY - actY) : actH;
    if (drawW <= 2 || drawH <= 2) return;

    /* Cell background */
    ChDrawRect(win->canv, actX + 1, actY + 1, drawW - 2, drawH - 2, COLOR_GRID_CELL_BG);

    /* Text */
    ChFontSetSize(grid->font, 13);
    ChRect clip = {actX + 4, actY + 2, drawW - 8, drawH - 4};
    ChFontDrawTextClipped(win->canv, grid->font, grid->editBuffer, actX + 4, actY + 15, COLOR_GRID_TEXT, &clip);

    /* Cursor */
    int64_t curTextW = ChFontGetWidth(grid->font, grid->editBuffer);
    int curLineX = actX + 4 + (int)curTextW;
    if (curLineX < actX + drawW - 4 && drawH > 6) {
        ChDrawVerticalLine(win->canv, curLineX, actY + 3, drawH - 6, COLOR_GRID_ACTIVE_ACCENT);
    }

    /* Emerald green active cell outline */
    ChDrawRectUnfilled(win->canv, actX, actY, drawW, drawH, COLOR_GRID_ACTIVE_ACCENT);
    ChDrawRectUnfilled(win->canv, actX + 1, actY + 1, drawW - 2, drawH - 2, COLOR_GRID_ACTIVE_ACCENT);

    /* Fill handle square */
    int handleX = actX + drawW - 4;
    int handleY = actY + drawH - 4;
    if (handleX + 5 <= maxX && handleY + 5 <= maxY) {
        ChDrawRect(win->canv, handleX, handleY, 5, 5, COLOR_GRID_ACTIVE_ACCENT);
    }

    /* Fast localized update for active cell */
    SheetWindowUpdate(win, actX, actY, drawW, drawH, false);
}


/* Mouse Event Handler */
static void SheetGridMouseEvent(ChWidget* wid, ChWindow* win, int x, int y, int button) {
    SheetGridWidget* grid = (SheetGridWidget*)wid;
    SheetTab* tab = SheetModelGetActiveTab(grid->workbook);
    if (!tab) return;

    /* Handle scroll wheel */
    if (button == DEODHAI_MOUSE_MSG_SCROLL_UP) {
        if (grid->scrollRow > 0) {
            grid->scrollRow -= 2;
            if (grid->scrollRow < 0) grid->scrollRow = 0;
            SheetGridRepaint(grid, win);
        }
        return;
    } else if (button == DEODHAI_MOUSE_MSG_SCROLL_DOWN) {
        if (grid->scrollRow < SHEET_MAX_ROWS - 5) {
            grid->scrollRow += 2;
            SheetGridRepaint(grid, win);
        }
        return;
    }

    if (button != 1) return; /* Left click only */

    if (!win || !win->info) return;

    int localX = x - win->info->x - wid->x;
    int localY = y - win->info->y - wid->y;

    /* Click on row/column headers */
    if (localX < SHEET_ROW_HEADER_W || localY < SHEET_COL_HEADER_H) {
        return;
    }

    /* Locate clicked col */
    int clickedCol = -1;
    int curX = SHEET_ROW_HEADER_W;
    for (int c = grid->scrollCol; c < SHEET_MAX_COLS; c++) {
        int w = tab->colWidths[c];
        if (localX >= curX && localX < curX + w) {
            clickedCol = c;
            break;
        }
        curX += w;
    }

    /* Locate clicked row */
    int clickedRow = -1;
    int curY = SHEET_COL_HEADER_H;
    for (int r = grid->scrollRow; r < SHEET_MAX_ROWS; r++) {
        int h = tab->rowHeights[r];
        if (localY >= curY && localY < curY + h) {
            clickedRow = r;
            break;
        }
        curY += h;
    }

    if (clickedCol >= 0 && clickedRow >= 0) {
        _KePrint("[XESheets] Grid click: col=%d row=%d (localX=%d localY=%d)\n", clickedCol, clickedRow, localX, localY);
        if (clickedCol == grid->activeCol && clickedRow == grid->activeRow) {
            /* Clicked on already active cell -> enter edit mode */
            if (!grid->isEditing) {
                SheetGridStartEdit(grid, win, NULL);
            }
        } else {
            /* Switched active cell */
            if (grid->isEditing) {
                SheetGridCommitEdit(grid, win);
            }
            grid->activeCol = clickedCol;
            grid->activeRow = clickedRow;
            grid->anchorCol = clickedCol;
            grid->anchorRow = clickedRow;
            grid->selRange.startCol = clickedCol;
            grid->selRange.endCol = clickedCol;
            grid->selRange.startRow = clickedRow;
            grid->selRange.endRow = clickedRow;

            if (grid->OnSelectionChanged) {
                grid->OnSelectionChanged(grid, win);
            }
        }

        SheetGridRepaint(grid, win);
    }
}

/* Key Event Handler */
bool SheetGridHandleKey(SheetGridWidget* grid, ChWindow* win, int keycode, char ascii) {
    if (!grid) return false;
    SheetTab* tab = SheetModelGetActiveTab(grid->workbook);
    if (!tab) return false;

    bool shift = ChitralekhaKeyGetShift();

    if (grid->isEditing) {
        /* Handle keys while in in-cell edit mode */
        if (keycode == SHEET_KEY_ENTER_STD || ascii == '\n' || ascii == '\r') {
            SheetGridCommitEdit(grid, win);
            if (!shift && grid->activeRow < SHEET_MAX_ROWS - 1) grid->activeRow++;
            else if (shift && grid->activeRow > 0) grid->activeRow--;
            grid->selRange.startCol = grid->activeCol;
            grid->selRange.endCol = grid->activeCol;
            grid->selRange.startRow = grid->activeRow;
            grid->selRange.endRow = grid->activeRow;
            SheetGridScrollToActive(grid);
            if (grid->OnSelectionChanged) grid->OnSelectionChanged(grid, win);
            SheetGridRepaint(grid, win);
            return true;
        } else if (keycode == SHEET_KEY_TAB_STD || keycode == KEY_TAB || ascii == '\t') {
            SheetGridCommitEdit(grid, win);
            if (!shift && grid->activeCol < SHEET_MAX_COLS - 1) grid->activeCol++;
            else if (shift && grid->activeCol > 0) grid->activeCol--;
            grid->selRange.startCol = grid->activeCol;
            grid->selRange.endCol = grid->activeCol;
            grid->selRange.startRow = grid->activeRow;
            grid->selRange.endRow = grid->activeRow;
            SheetGridScrollToActive(grid);
            if (grid->OnSelectionChanged) grid->OnSelectionChanged(grid, win);
            SheetGridRepaint(grid, win);
            return true;
        } else if (keycode == SHEET_KEY_ESC_STD || keycode == KEY_ESCAPE) {
            SheetGridCancelEdit(grid, win);
            SheetGridRepaint(grid, win);
            return true;
        } else if (keycode == SHEET_KEY_BS_STD || ascii == '\b') {
            if (grid->editCursor > 0) {
                grid->editCursor--;
                grid->editBuffer[grid->editCursor] = '\0';
                SheetGridRepaintActiveCell(grid, win);
            }
            return true;
        } else if (keycode == SHEET_KEY_LEFT_EXT || keycode == KEY_LEFT) {
            if (grid->editCursor > 0) {
                grid->editCursor--;
                SheetGridRepaintActiveCell(grid, win);
            }
            return true;
        } else if (keycode == SHEET_KEY_RIGHT_EXT || keycode == KEY_RIGHT) {
            if (grid->editCursor < (int)strlen(grid->editBuffer)) {
                grid->editCursor++;
                SheetGridRepaintActiveCell(grid, win);
            }
            return true;
        } else if (ascii >= 32 && ascii <= 126) {
            if (grid->editCursor < (int)sizeof(grid->editBuffer) - 1) {
                grid->editBuffer[grid->editCursor++] = ascii;
                grid->editBuffer[grid->editCursor] = '\0';
                SheetGridRepaintActiveCell(grid, win);
            }
            return true;
        }
    } else {
        /* Navigation mode */
        bool keyHandled = false;

        if (keycode == SHEET_KEY_UP_EXT || keycode == KEY_UP) {
            if (grid->activeRow > 0) {
                grid->activeRow--;
                keyHandled = true;
            }
        } else if (keycode == SHEET_KEY_DOWN_EXT || keycode == KEY_DOWN) {
            if (grid->activeRow < SHEET_MAX_ROWS - 1) {
                grid->activeRow++;
                keyHandled = true;
            }
        } else if (keycode == SHEET_KEY_LEFT_EXT || keycode == KEY_LEFT) {
            if (grid->activeCol > 0) {
                grid->activeCol--;
                keyHandled = true;
            }
        } else if (keycode == SHEET_KEY_RIGHT_EXT || keycode == KEY_RIGHT) {
            if (grid->activeCol < SHEET_MAX_COLS - 1) {
                grid->activeCol++;
                keyHandled = true;
            }
        } else if (keycode == SHEET_KEY_HOME_EXT || keycode == KEY_HOME) {
            grid->activeCol = 0;
            keyHandled = true;
        } else if (keycode == SHEET_KEY_END_EXT || keycode == KEY_END) {
            grid->activeCol = SHEET_MAX_COLS - 1;
            keyHandled = true;
        } else if (keycode == SHEET_KEY_PGUP_EXT || keycode == KEY_PAGEUP) {
            grid->activeRow = (grid->activeRow > 10) ? grid->activeRow - 10 : 0;
            keyHandled = true;
        } else if (keycode == SHEET_KEY_PGDN_EXT || keycode == KEY_PAGEDOWN) {
            grid->activeRow = (grid->activeRow + 10 < SHEET_MAX_ROWS) ? grid->activeRow + 10 : SHEET_MAX_ROWS - 1;
            keyHandled = true;
        } else if (keycode == SHEET_KEY_ENTER_STD || ascii == '\n' || ascii == '\r') {
            if (!shift && grid->activeRow < SHEET_MAX_ROWS - 1) grid->activeRow++;
            else if (shift && grid->activeRow > 0) grid->activeRow--;
            keyHandled = true;
        } else if (keycode == SHEET_KEY_TAB_STD || keycode == KEY_TAB || ascii == '\t') {
            if (!shift && grid->activeCol < SHEET_MAX_COLS - 1) grid->activeCol++;
            else if (shift && grid->activeCol > 0) grid->activeCol--;
            keyHandled = true;
        } else if (keycode == SHEET_KEY_BS_STD || ascii == '\b' ||
                   keycode == SHEET_KEY_DEL_EXT || keycode == KEY_DELETE) {
            /* Clear active cell */
            SheetRange singleRange = {grid->activeCol, grid->activeRow, grid->activeCol, grid->activeRow};
            SheetModelClearRange(grid->workbook, &singleRange);
            if (grid->OnSelectionChanged) grid->OnSelectionChanged(grid, win);
            SheetGridRepaint(grid, win);
            return true;
        } else if (keycode == 0x3C || keycode == KEY_F2) {
            /* F2: Edit existing cell */
            SheetGridStartEdit(grid, win, NULL);
            SheetGridRepaintActiveCell(grid, win);
            return true;
        } else if (ascii >= 32 && ascii <= 126) {
            /* Start editing with typed character */
            char initStr[2] = {ascii, '\0'};
            SheetGridStartEdit(grid, win, initStr);
            SheetGridRepaintActiveCell(grid, win);
            return true;
        }

        if (keyHandled) {
            if (shift) {
                /* Range selection with Shift held */
                grid->selRange.startCol = (grid->anchorCol <= grid->activeCol) ? grid->anchorCol : grid->activeCol;
                grid->selRange.endCol   = (grid->anchorCol <= grid->activeCol) ? grid->activeCol : grid->anchorCol;
                grid->selRange.startRow = (grid->anchorRow <= grid->activeRow) ? grid->anchorRow : grid->activeRow;
                grid->selRange.endRow   = (grid->anchorRow <= grid->activeRow) ? grid->activeRow : grid->anchorRow;
            } else {
                /* Single cell movement: reset anchor to activeCol/activeRow */
                grid->anchorCol = grid->activeCol;
                grid->anchorRow = grid->activeRow;
                grid->selRange.startCol = grid->activeCol;
                grid->selRange.endCol = grid->activeCol;
                grid->selRange.startRow = grid->activeRow;
                grid->selRange.endRow = grid->activeRow;
            }
            SheetGridScrollToActive(grid);
            if (grid->OnSelectionChanged) grid->OnSelectionChanged(grid, win);
            SheetGridRepaint(grid, win);
            return true;
        }
    }

    return false;
}

SheetGridWidget* SheetGridCreate(int x, int y, int w, int h, SheetWorkbook* wb, ChFont* font) {
    SheetGridWidget* grid = (SheetGridWidget*)malloc(sizeof(SheetGridWidget));
    if (!grid) return NULL;
    memset(grid, 0, sizeof(SheetGridWidget));

    grid->base.x = x;
    grid->base.y = y;
    grid->base.w = w;
    grid->base.h = h;
    grid->base.visible = true;
    grid->base.ChPaintHandler = SheetGridPaint;
    grid->base.ChMouseEvent = SheetGridMouseEvent;

    grid->workbook = wb;
    grid->font = font;
    grid->activeCol = 0;
    grid->activeRow = 0;
    grid->anchorCol = 0;
    grid->anchorRow = 0;
    grid->selRange.startCol = 0;
    grid->selRange.endCol = 0;
    grid->selRange.startRow = 0;
    grid->selRange.endRow = 0;
    grid->scrollCol = 0;
    grid->scrollRow = 0;
    grid->isEditing = false;
    grid->editBuffer[0] = '\0';
    grid->editCursor = 0;

    return grid;
}
