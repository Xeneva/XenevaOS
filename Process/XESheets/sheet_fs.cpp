/**
 * @file sheet_fs.cpp
 * @brief RFC 4180 CSV file persistence for XESheets
 */

#include "sheet_fs.h"
#include "sheet_model.h"
#include "sheet_eval.h"
#include <sys/_kefile.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static inline int IsAlnum(int c) {
    return isalpha(c) || isdigit(c);
}

static void EnsureSheetDir(void) {
    static bool created = false;
    if (!created) {
        _KeCreateDir((char*)"/sheet");
        created = true;
    }
}

static void MakeSafeCSVPath(const char* name, char* outPath, size_t outSize) {
    if (!name || name[0] == '\0') {
        snprintf(outPath, outSize, "/sheet/SHEET1.CSV");
        return;
    }

    if (name[0] == '/') {
        strncpy(outPath, name, outSize - 1);
        outPath[outSize - 1] = '\0';
        return;
    }

    char base[9];
    size_t j = 0;
    for (size_t i = 0; name[i] != '\0' && name[i] != '.' && j < 8; i++) {
        char c = name[i];
        if (IsAlnum((unsigned char)c)) {
            base[j++] = (char)toupper((unsigned char)c);
        }
    }
    if (j == 0) {
        strcpy(base, "SHEET1");
        j = 6;
    }
    base[j] = '\0';
    snprintf(outPath, outSize, "/sheet/%s.CSV", base);
}

static inline void CsvAppendChar(char* buf, size_t* pos, size_t cap, char c) {
    if (*pos < cap - 1) {
        buf[(*pos)++] = c;
    }
}

static inline void CsvAppendStr(char* buf, size_t* pos, size_t cap, const char* str, size_t len) {
    if (*pos + len < cap - 1) {
        memcpy(buf + *pos, (void*)str, len);
        *pos += len;
    }
}

bool SheetFSExportCSV(SheetWorkbook* wb, const char* filepath) {
    if (!wb) return false;
    SheetTab* tab = SheetModelGetActiveTab(wb);
    if (!tab) return false;

    EnsureSheetDir();
    char safePath[SHEET_PATH_MAX];
    MakeSafeCSVPath(filepath ? filepath : wb->filepath, safePath, sizeof(safePath));

    /* Find bounding box of non-empty data to avoid exporting empty cells */
    int maxR = -1;
    int maxC = -1;
    for (int r = 0; r < SHEET_MAX_ROWS; r++) {
        for (int c = 0; c < SHEET_MAX_COLS; c++) {
            if (tab->cells[r][c].rawText[0] != '\0') {
                if (r > maxR) maxR = r;
                if (c > maxC) maxC = c;
            }
        }
    }

    if (maxR == -1) {
        /* Empty sheet: create at least 1 empty row/col */
        maxR = 0;
        maxC = 0;
    }

    /* Allocate contiguous CSV buffer (256 KB) */
    size_t cap = 262144;
    char* csvBuf = (char*)malloc(cap);
    if (!csvBuf) return false;
    size_t totalLen = 0;

    /* Write CSV lines into contiguous memory buffer */
    for (int r = 0; r <= maxR; r++) {
        for (int c = 0; c <= maxC; c++) {
            const char* val = tab->cells[r][c].rawText;

            /* Check if quotes are required */
            bool needsQuotes = false;
            for (int k = 0; val[k] != '\0'; k++) {
                if (val[k] == ',' || val[k] == '"' || val[k] == '\n' || val[k] == '\r') {
                    needsQuotes = true;
                    break;
                }
            }

            if (needsQuotes) {
                CsvAppendChar(csvBuf, &totalLen, cap, '"');
                for (int k = 0; val[k] != '\0'; k++) {
                    if (val[k] == '"') {
                        CsvAppendChar(csvBuf, &totalLen, cap, '"');
                    }
                    CsvAppendChar(csvBuf, &totalLen, cap, val[k]);
                }
                CsvAppendChar(csvBuf, &totalLen, cap, '"');
            } else {
                CsvAppendStr(csvBuf, &totalLen, cap, val, strlen(val));
            }

            if (c < maxC) {
                CsvAppendChar(csvBuf, &totalLen, cap, ',');
            }
        }

        CsvAppendChar(csvBuf, &totalLen, cap, '\r');
        CsvAppendChar(csvBuf, &totalLen, cap, '\n');
    }

    /* Remove previous file entry to clean cluster chain and reset file size on disk */
    _KeRemoveFile((char*)safePath);

    int fd = _KeOpenFile(safePath, FILE_OPEN_WRITE | FILE_OPEN_CREAT);
    if (fd < 0) {
        free(csvBuf);
        _KePrint("XESheets: Failed to open %s for write\n", safePath);
        return false;
    }

    if (totalLen > 0) {
        _KeWriteFile(fd, csvBuf, totalLen);
    }
    _KeCloseFile(fd);
    free(csvBuf);

    strncpy(wb->filepath, safePath, sizeof(wb->filepath) - 1);
    wb->filepath[sizeof(wb->filepath) - 1] = '\0';
    wb->isDirty = false;
    _KePrint("XESheets: Successfully saved CSV to %s (%d bytes)\n", safePath, (int)totalLen);
    return true;
}

bool SheetFSImportCSV(SheetWorkbook* wb, const char* filepath) {
    if (!wb || !filepath) return false;
    SheetTab* tab = SheetModelGetActiveTab(wb);
    if (!tab) return false;

    char safePath[SHEET_PATH_MAX];
    MakeSafeCSVPath(filepath, safePath, sizeof(safePath));

    int fd = _KeOpenFile(safePath, FILE_OPEN_READ_ONLY);
    if (fd < 0) {
        _KePrint("XESheets: Failed to open %s for read\n", safePath);
        return false;
    }

    char* buffer = (char*)malloc(262144);
    if (!buffer) {
        _KeCloseFile(fd);
        return false;
    }

    int bytesRead = _KeReadFile(fd, buffer, 262143);
    _KeCloseFile(fd);

    if (bytesRead < 0) {
        free(buffer);
        return false;
    }
    buffer[bytesRead] = '\0';

    /* Clear current active sheet */
    SheetRange allRange = {0, 0, SHEET_MAX_COLS - 1, SHEET_MAX_ROWS - 1};
    SheetModelClearRange(wb, &allRange);

    if (bytesRead == 0) {
        free(buffer);
        wb->isDirty = false;
        return true;
    }

    /* RFC 4180 state-machine parser */
    int row = 0;
    int col = 0;
    int i = 0;
    char fieldBuf[SHEET_CELL_TEXT_LEN];
    int fLen = 0;

    enum {
        STATE_START_FIELD,
        STATE_IN_UNQUOTED,
        STATE_IN_QUOTED,
        STATE_QUOTE_IN_QUOTED
    } state = STATE_START_FIELD;

    while (i < bytesRead && row < SHEET_MAX_ROWS) {
        char ch = buffer[i++];
        if (ch == '\0') {
            break; /* Reached end of valid text */
        }

        switch (state) {
        case STATE_START_FIELD:
            if (ch == '"') {
                state = STATE_IN_QUOTED;
            } else if (ch == ',') {
                if (col < SHEET_MAX_COLS) {
                    fieldBuf[fLen] = '\0';
                    strncpy(tab->cells[row][col].rawText, fieldBuf, SHEET_CELL_TEXT_LEN - 1);
                    tab->cells[row][col].rawText[SHEET_CELL_TEXT_LEN - 1] = '\0';
                }
                col++;
                fLen = 0;
            } else if (ch == '\r') {
                if (i < bytesRead && buffer[i] == '\n') i++;
                if (col < SHEET_MAX_COLS) {
                    fieldBuf[fLen] = '\0';
                    strncpy(tab->cells[row][col].rawText, fieldBuf, SHEET_CELL_TEXT_LEN - 1);
                    tab->cells[row][col].rawText[SHEET_CELL_TEXT_LEN - 1] = '\0';
                }
                row++;
                col = 0;
                fLen = 0;
            } else if (ch == '\n') {
                if (col < SHEET_MAX_COLS) {
                    fieldBuf[fLen] = '\0';
                    strncpy(tab->cells[row][col].rawText, fieldBuf, SHEET_CELL_TEXT_LEN - 1);
                    tab->cells[row][col].rawText[SHEET_CELL_TEXT_LEN - 1] = '\0';
                }
                row++;
                col = 0;
                fLen = 0;
            } else {
                if (fLen < SHEET_CELL_TEXT_LEN - 1) fieldBuf[fLen++] = ch;
                state = STATE_IN_UNQUOTED;
            }
            break;

        case STATE_IN_UNQUOTED:
            if (ch == ',') {
                if (col < SHEET_MAX_COLS) {
                    fieldBuf[fLen] = '\0';
                    strncpy(tab->cells[row][col].rawText, fieldBuf, SHEET_CELL_TEXT_LEN - 1);
                    tab->cells[row][col].rawText[SHEET_CELL_TEXT_LEN - 1] = '\0';
                }
                col++;
                fLen = 0;
                state = STATE_START_FIELD;
            } else if (ch == '\r') {
                if (i < bytesRead && buffer[i] == '\n') i++;
                if (col < SHEET_MAX_COLS) {
                    fieldBuf[fLen] = '\0';
                    strncpy(tab->cells[row][col].rawText, fieldBuf, SHEET_CELL_TEXT_LEN - 1);
                    tab->cells[row][col].rawText[SHEET_CELL_TEXT_LEN - 1] = '\0';
                }
                row++;
                col = 0;
                fLen = 0;
                state = STATE_START_FIELD;
            } else if (ch == '\n') {
                if (col < SHEET_MAX_COLS) {
                    fieldBuf[fLen] = '\0';
                    strncpy(tab->cells[row][col].rawText, fieldBuf, SHEET_CELL_TEXT_LEN - 1);
                    tab->cells[row][col].rawText[SHEET_CELL_TEXT_LEN - 1] = '\0';
                }
                row++;
                col = 0;
                fLen = 0;
                state = STATE_START_FIELD;
            } else {
                if (fLen < SHEET_CELL_TEXT_LEN - 1) fieldBuf[fLen++] = ch;
            }
            break;

        case STATE_IN_QUOTED:
            if (ch == '"') {
                state = STATE_QUOTE_IN_QUOTED;
            } else {
                if (fLen < SHEET_CELL_TEXT_LEN - 1) fieldBuf[fLen++] = ch;
            }
            break;

        case STATE_QUOTE_IN_QUOTED:
            if (ch == '"') {
                /* Escaped double quote */
                if (fLen < SHEET_CELL_TEXT_LEN - 1) fieldBuf[fLen++] = '"';
                state = STATE_IN_QUOTED;
            } else if (ch == ',') {
                if (col < SHEET_MAX_COLS) {
                    fieldBuf[fLen] = '\0';
                    strncpy(tab->cells[row][col].rawText, fieldBuf, SHEET_CELL_TEXT_LEN - 1);
                    tab->cells[row][col].rawText[SHEET_CELL_TEXT_LEN - 1] = '\0';
                }
                col++;
                fLen = 0;
                state = STATE_START_FIELD;
            } else if (ch == '\r') {
                if (i < bytesRead && buffer[i] == '\n') i++;
                if (col < SHEET_MAX_COLS) {
                    fieldBuf[fLen] = '\0';
                    strncpy(tab->cells[row][col].rawText, fieldBuf, SHEET_CELL_TEXT_LEN - 1);
                    tab->cells[row][col].rawText[SHEET_CELL_TEXT_LEN - 1] = '\0';
                }
                row++;
                col = 0;
                fLen = 0;
                state = STATE_START_FIELD;
            } else if (ch == '\n') {
                if (col < SHEET_MAX_COLS) {
                    fieldBuf[fLen] = '\0';
                    strncpy(tab->cells[row][col].rawText, fieldBuf, SHEET_CELL_TEXT_LEN - 1);
                    tab->cells[row][col].rawText[SHEET_CELL_TEXT_LEN - 1] = '\0';
                }
                row++;
                col = 0;
                fLen = 0;
                state = STATE_START_FIELD;
            } else {
                if (fLen < SHEET_CELL_TEXT_LEN - 1) fieldBuf[fLen++] = ch;
                state = STATE_IN_UNQUOTED;
            }
            break;
        }
    }

    /* Save trailing field if any */
    if (fLen > 0 && row < SHEET_MAX_ROWS && col < SHEET_MAX_COLS) {
        fieldBuf[fLen] = '\0';
        strncpy(tab->cells[row][col].rawText, fieldBuf, SHEET_CELL_TEXT_LEN - 1);
        tab->cells[row][col].rawText[SHEET_CELL_TEXT_LEN - 1] = '\0';
    }

    free(buffer);
    strncpy(wb->filepath, safePath, sizeof(wb->filepath) - 1);
    wb->filepath[sizeof(wb->filepath) - 1] = '\0';
    wb->isDirty = false;

    SheetEvaluateAll(tab);
    _KePrint("XESheets: Successfully loaded CSV from %s\n", safePath);
    return true;
}
