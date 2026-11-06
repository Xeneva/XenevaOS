/**
 * @file sheet_types.h
 * @brief Core types, constants, and limits for XESheets spreadsheet application
 */

#ifndef __SHEET_TYPES_H__
#define __SHEET_TYPES_H__

#include <stdint.h>
#include <stdbool.h>

#define SHEET_MAX_COLS        26   /* Columns A to Z */
#define SHEET_MAX_ROWS        100  /* Rows 1 to 100 */
#define SHEET_MAX_TABS        8    /* Up to 8 multi-sheet tabs */
#define SHEET_TAB_NAME_LEN    32
#define SHEET_CELL_TEXT_LEN   64
#define SHEET_DISP_TEXT_LEN   32
#define SHEET_PATH_MAX        128

/* Default Grid Layout */
#define SHEET_DEFAULT_COL_W   72
#define SHEET_DEFAULT_ROW_H   22
#define SHEET_ROW_HEADER_W    42
#define SHEET_COL_HEADER_H    22

/* Cell Types */
enum CellType {
    CELL_TYPE_EMPTY = 0,
    CELL_TYPE_TEXT,
    CELL_TYPE_NUMBER,
    CELL_TYPE_FORMULA
};

/* Cell Errors */
enum CellError {
    CELL_ERR_NONE = 0,
    CELL_ERR_DIV0,   /* #DIV/0! */
    CELL_ERR_VALUE,  /* #VALUE! */
    CELL_ERR_NAME,   /* #NAME? */
    CELL_ERR_CYCLE,  /* #CYCLE! */
    CELL_ERR_REF     /* #REF! */
};

/* Formatting Flags */
#define CELL_FMT_BOLD         (1 << 0)
#define CELL_FMT_ALIGN_LEFT   (0)
#define CELL_FMT_ALIGN_CENTER (1 << 1)
#define CELL_FMT_ALIGN_RIGHT  (1 << 2)

/* Single Spreadsheet Cell */
typedef struct _SheetCell {
    char rawText[SHEET_CELL_TEXT_LEN];
    char displayText[SHEET_DISP_TEXT_LEN];
    double computedValue;
    uint8_t type;
    uint8_t error;
    uint16_t format;
    uint32_t bgColor;
    uint32_t textColor;
} SheetCell;

/* Single Sheet Tab */
typedef struct _SheetTab {
    char name[SHEET_TAB_NAME_LEN];
    SheetCell cells[SHEET_MAX_ROWS][SHEET_MAX_COLS];
    uint8_t evalVisiting[SHEET_MAX_ROWS][SHEET_MAX_COLS];
    int16_t colWidths[SHEET_MAX_COLS];
    int16_t rowHeights[SHEET_MAX_ROWS];
} SheetTab;

/* Multi-Tab Workbook */
typedef struct _SheetWorkbook {
    SheetTab tabs[SHEET_MAX_TABS];
    int tabCount;
    int activeTab;
    char filepath[SHEET_PATH_MAX];
    bool isDirty;
} SheetWorkbook;

/* Cell Selection / Coordinate Pair */
typedef struct _SheetCoord {
    int col; /* 0 .. SHEET_MAX_COLS - 1 */
    int row; /* 0 .. SHEET_MAX_ROWS - 1 */
} SheetCoord;

/* Range Selection */
typedef struct _SheetRange {
    int startCol;
    int startRow;
    int endCol;
    int endRow;
} SheetRange;

/* Calculation Summary Statistics */
typedef struct _SheetStats {
    double sum;
    double avg;
    double min;
    double max;
    int count;
    int numCount;
} SheetStats;

#endif /* __SHEET_TYPES_H__ */
