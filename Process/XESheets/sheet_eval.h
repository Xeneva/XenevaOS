/**
 * @file sheet_eval.h
 * @brief High-performance recursive-descent formula parser and evaluator for XESheets
 */

#ifndef __SHEET_EVAL_H__
#define __SHEET_EVAL_H__

#include "sheet_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Convert column index (0..25) and row index (0..99) to A1 string */
void SheetCoordToString(int col, int row, char* buf, int maxLen);

/* Parse cell reference token (e.g. "A1", "C25") into col and row indices.
 * Returns length of parsed token, or 0 if invalid. */
int SheetParseCellRef(const char* str, int* outCol, int* outRow);

/* Parse range token (e.g. "A1:B10") into start and end coords.
 * Returns length of parsed token, or 0 if invalid. */
int SheetParseRange(const char* str, SheetRange* outRange);

/* Evaluate a single cell's formula or value in the given sheet tab */
void SheetEvaluateCell(SheetTab* tab, int row, int col);

/* Evaluate all cells in the sheet tab with dependency resolution & cycle detection */
void SheetEvaluateAll(SheetTab* tab);

/* Format numeric value into human-readable spreadsheet string */
void SheetFormatNumber(double val, char* buf, int maxLen);

/* Compute range statistics (SUM, AVG, MIN, MAX, COUNT) for selected cells */
void SheetComputeStats(SheetTab* tab, const SheetRange* range, SheetStats* outStats);

#ifdef __cplusplus
}
#endif

#endif /* __SHEET_EVAL_H__ */
