/**
 * @file sheet_model.h
 * @brief Data model, multi-sheet workbook management, and cell operations for XESheets
 */

#ifndef __SHEET_MODEL_H__
#define __SHEET_MODEL_H__

#include "sheet_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize an existing workbook structure */
void SheetModelInit(SheetWorkbook* wb);

/* Retrieve currently active tab */
SheetTab* SheetModelGetActiveTab(SheetWorkbook* wb);

/* Add a new sheet tab to the workbook. Returns tab index, or -1 if at max capacity */
int SheetModelAddTab(SheetWorkbook* wb, const char* name);

/* Switch active tab to target index. Returns true on success */
bool SheetModelSwitchTab(SheetWorkbook* wb, int index);

/* Rename sheet tab. Returns true on success */
bool SheetModelRenameTab(SheetWorkbook* wb, int index, const char* newName);

/* Delete sheet tab. Always keeps at least 1 tab. Returns true on success */
bool SheetModelDeleteTab(SheetWorkbook* wb, int index);

/* Get cell pointer for active sheet */
SheetCell* SheetModelGetActiveCell(SheetWorkbook* wb, int col, int row);

/* Set raw cell text for active sheet, triggering formula evaluation */
void SheetModelSetCellText(SheetWorkbook* wb, int col, int row, const char* text);

/* Apply or toggle format flag on a specific cell or range */
void SheetModelToggleFormat(SheetWorkbook* wb, int col, int row, uint16_t fmtFlag);
void SheetModelSetRangeFormat(SheetWorkbook* wb, const SheetRange* range, uint16_t fmtFlag, bool enable);

/* Clear content and formatting in range */
void SheetModelClearRange(SheetWorkbook* wb, const SheetRange* range);

/* Sort rows in range according to specified column */
void SheetModelSortRange(SheetWorkbook* wb, const SheetRange* range, int keyCol, bool ascending);

#ifdef __cplusplus
}
#endif

#endif /* __SHEET_MODEL_H__ */
