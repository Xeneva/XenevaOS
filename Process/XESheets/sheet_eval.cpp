/**
 * @file sheet_eval.cpp
 * @brief High-performance recursive-descent formula parser and evaluator for XESheets
 */

#include "sheet_eval.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>

static inline int IsAlnum(int c) {
    return isalpha(c) || isdigit(c);
}

/* Convert column and row to A1 string */
void SheetCoordToString(int col, int row, char* buf, int maxLen) {
    if (!buf || maxLen < 3) return;
    if (col < 0) col = 0;
    if (col >= SHEET_MAX_COLS) col = SHEET_MAX_COLS - 1;
    if (row < 0) row = 0;
    if (row >= SHEET_MAX_ROWS) row = SHEET_MAX_ROWS - 1;
    snprintf(buf, maxLen, "%c%d", (char)('A' + col), row + 1);
}

/* Parse cell reference token (e.g. "A1", "C25"). Returns parsed character length */
int SheetParseCellRef(const char* str, int* outCol, int* outRow) {
    if (!str || !isalpha((unsigned char)str[0])) return 0;
    char colChar = (char)toupper((unsigned char)str[0]);
    int col = colChar - 'A';
    if (col < 0 || col >= SHEET_MAX_COLS) return 0;

    int idx = 1;
    int rowNum = 0;
    while (isdigit((unsigned char)str[idx])) {
        rowNum = rowNum * 10 + (str[idx] - '0');
        idx++;
    }

    if (rowNum < 1 || rowNum > SHEET_MAX_ROWS) return 0;

    if (outCol) *outCol = col;
    if (outRow) *outRow = rowNum - 1;
    return idx;
}

/* Parse range token (e.g. "A1:B10"). Returns parsed character length */
int SheetParseRange(const char* str, SheetRange* outRange) {
    if (!str) return 0;
    int col1 = 0, row1 = 0;
    int len1 = SheetParseCellRef(str, &col1, &row1);
    if (len1 == 0) return 0;

    if (str[len1] != ':') return 0;

    int col2 = 0, row2 = 0;
    int len2 = SheetParseCellRef(str + len1 + 1, &col2, &row2);
    if (len2 == 0) return 0;

    if (outRange) {
        outRange->startCol = (col1 <= col2) ? col1 : col2;
        outRange->endCol   = (col1 <= col2) ? col2 : col1;
        outRange->startRow = (row1 <= row2) ? row1 : row2;
        outRange->endRow   = (row1 <= row2) ? row2 : row1;
    }

    return len1 + 1 + len2;
}

/* Format numeric value cleanly (e.g. 12 -> "12", 12.5 -> "12.5", 12.3333 -> "12.33") */
void SheetFormatNumber(double val, char* buf, int maxLen) {
    if (!buf || maxLen <= 0) return;
    if (isnan(val)) {
        snprintf(buf, maxLen, "#VALUE!");
        return;
    }
    if (isinf(val)) {
        snprintf(buf, maxLen, (val > 0) ? "#DIV/0!" : "-#DIV/0!");
        return;
    }

    /* Check if integer */
    double intPart = 0.0;
    if (fabs(modf(val, &intPart)) < 1e-9 && fabs(val) < 1e14) {
        snprintf(buf, maxLen, "%lld", (long long)val);
    } else {
        snprintf(buf, maxLen, "%.4g", val);
    }
}

/* -------------------------------------------------------------
 * Recursive-Descent Expression Parser
 * ------------------------------------------------------------- */

typedef struct {
    const char* src;
    int pos;
    SheetTab* tab;
    uint8_t err;
} EvalCtx;

static void SkipWhitespace(EvalCtx* ctx) {
    while (ctx->src[ctx->pos] == ' ' || ctx->src[ctx->pos] == '\t' ||
           ctx->src[ctx->pos] == '\r' || ctx->src[ctx->pos] == '\n') {
        ctx->pos++;
    }
}

/* Forward declarations */
static double ParseExpr(EvalCtx* ctx);

/* Parse number literal */
static double ParseNumber(EvalCtx* ctx) {
    SkipWhitespace(ctx);
    const char* start = ctx->src + ctx->pos;
    char* end = NULL;
    double val = strtod(start, &end);
    if (end == start) {
        ctx->err = CELL_ERR_VALUE;
        return 0.0;
    }
    ctx->pos += (int)(end - start);
    return val;
}

/* Evaluate cell reference and recursively compute if necessary */
static double ResolveCellVal(EvalCtx* ctx, int col, int row) {
    if (col < 0 || col >= SHEET_MAX_COLS || row < 0 || row >= SHEET_MAX_ROWS) {
        ctx->err = CELL_ERR_REF;
        return 0.0;
    }

    SheetCell* cell = &ctx->tab->cells[row][col];

    /* Cycle check */
    if (ctx->tab->evalVisiting[row][col] == 1) {
        ctx->err = CELL_ERR_CYCLE;
        return 0.0;
    }

    if (ctx->tab->evalVisiting[row][col] == 0) {
        SheetEvaluateCell(ctx->tab, row, col);
    }

    if (cell->error != CELL_ERR_NONE) {
        ctx->err = cell->error;
        return 0.0;
    }

    return cell->computedValue;
}

/* Parse built-in function or cell reference */
static double ParseIdentOrFunc(EvalCtx* ctx) {
    SkipWhitespace(ctx);
    int start = ctx->pos;
    while (IsAlnum((unsigned char)ctx->src[ctx->pos]) || ctx->src[ctx->pos] == '_') {
        ctx->pos++;
    }
    int len = ctx->pos - start;
    if (len == 0) {
        ctx->err = CELL_ERR_VALUE;
        return 0.0;
    }

    char name[32];
    if (len >= (int)sizeof(name)) len = sizeof(name) - 1;
    for (int i = 0; i < len; i++) {
        name[i] = (char)toupper((unsigned char)ctx->src[start + i]);
    }
    name[len] = '\0';

    SkipWhitespace(ctx);
    if (ctx->src[ctx->pos] == '(') {
        /* Function call */
        ctx->pos++; /* Consume '(' */

        /* Argument arrays */
        double vals[128];
        int valCount = 0;

        SkipWhitespace(ctx);
        if (ctx->src[ctx->pos] != ')') {
            while (1) {
                SkipWhitespace(ctx);

                /* Check for range token e.g. A1:B10 */
                SheetRange range;
                int rangeLen = SheetParseRange(ctx->src + ctx->pos, &range);
                if (rangeLen > 0) {
                    ctx->pos += rangeLen;
                    for (int r = range.startRow; r <= range.endRow; r++) {
                        for (int c = range.startCol; c <= range.endCol; c++) {
                            if (valCount < 128) {
                                vals[valCount++] = ResolveCellVal(ctx, c, r);
                            }
                        }
                    }
                } else {
                    double v = ParseExpr(ctx);
                    if (valCount < 128) {
                        vals[valCount++] = v;
                    }
                }

                SkipWhitespace(ctx);
                if (ctx->src[ctx->pos] == ',') {
                    ctx->pos++;
                } else {
                    break;
                }
            }
        }

        if (ctx->src[ctx->pos] == ')') {
            ctx->pos++;
        } else {
            ctx->err = CELL_ERR_VALUE;
            return 0.0;
        }

        /* Dispatch built-ins */
        if (strcmp(name, "SUM") == 0) {
            double total = 0.0;
            for (int i = 0; i < valCount; i++) total += vals[i];
            return total;
        } else if (strcmp(name, "AVERAGE") == 0 || strcmp(name, "AVG") == 0) {
            if (valCount == 0) {
                ctx->err = CELL_ERR_DIV0;
                return 0.0;
            }
            double total = 0.0;
            for (int i = 0; i < valCount; i++) total += vals[i];
            return total / (double)valCount;
        } else if (strcmp(name, "COUNT") == 0 || strcmp(name, "COUNTA") == 0) {
            return (double)valCount;
        } else if (strcmp(name, "MIN") == 0) {
            if (valCount == 0) return 0.0;
            double m = vals[0];
            for (int i = 1; i < valCount; i++) if (vals[i] < m) m = vals[i];
            return m;
        } else if (strcmp(name, "MAX") == 0) {
            if (valCount == 0) return 0.0;
            double m = vals[0];
            for (int i = 1; i < valCount; i++) if (vals[i] > m) m = vals[i];
            return m;
        } else if (strcmp(name, "PRODUCT") == 0) {
            if (valCount == 0) return 0.0;
            double p = 1.0;
            for (int i = 0; i < valCount; i++) p *= vals[i];
            return p;
        } else if (strcmp(name, "ABS") == 0) {
            if (valCount < 1) return 0.0;
            return fabs(vals[0]);
        } else if (strcmp(name, "SQRT") == 0) {
            if (valCount < 1 || vals[0] < 0.0) {
                ctx->err = CELL_ERR_VALUE;
                return 0.0;
            }
            return sqrt(vals[0]);
        } else if (strcmp(name, "ROUND") == 0) {
            if (valCount < 1) return 0.0;
            if (valCount == 1) return (double)roundf((float)vals[0]);
            double factor = pow(10.0, vals[1]);
            return (double)roundf((float)(vals[0] * factor)) / factor;
        } else if (strcmp(name, "IF") == 0) {
            if (valCount < 2) return 0.0;
            if (fabs(vals[0]) > 1e-9) {
                return vals[1];
            } else {
                return (valCount >= 3) ? vals[2] : 0.0;
            }
        } else {
            ctx->err = CELL_ERR_NAME;
            return 0.0;
        }
    }

    /* Otherwise, it's a cell reference */
    int col = 0, row = 0;
    int refLen = SheetParseCellRef(name, &col, &row);
    if (refLen == len) {
        return ResolveCellVal(ctx, col, row);
    }

    ctx->err = CELL_ERR_NAME;
    return 0.0;
}

/* Primary = Number | IdentOrFunc | '(' Expr ')' */
static double ParsePrimary(EvalCtx* ctx) {
    SkipWhitespace(ctx);
    char c = ctx->src[ctx->pos];

    if (c == '(') {
        ctx->pos++;
        double val = ParseExpr(ctx);
        SkipWhitespace(ctx);
        if (ctx->src[ctx->pos] == ')') {
            ctx->pos++;
        } else {
            ctx->err = CELL_ERR_VALUE;
        }
        return val;
    }

    if (isdigit((unsigned char)c) || c == '.') {
        return ParseNumber(ctx);
    }

    if (isalpha((unsigned char)c) || c == '_') {
        return ParseIdentOrFunc(ctx);
    }

    ctx->err = CELL_ERR_VALUE;
    return 0.0;
}

/* Unary = ('+' | '-')* Primary */
static double ParseUnary(EvalCtx* ctx) {
    SkipWhitespace(ctx);
    if (ctx->src[ctx->pos] == '+') {
        ctx->pos++;
        return ParseUnary(ctx);
    }
    if (ctx->src[ctx->pos] == '-') {
        ctx->pos++;
        return -ParseUnary(ctx);
    }
    return ParsePrimary(ctx);
}

/* Power = Unary ('^' Power)? (right-associative) */
static double ParsePower(EvalCtx* ctx) {
    double base = ParseUnary(ctx);
    SkipWhitespace(ctx);
    if (ctx->src[ctx->pos] == '^') {
        ctx->pos++;
        double exp = ParsePower(ctx);
        return pow(base, exp);
    }
    return base;
}

/* Term = Power (('*' | '/') Power)* */
static double ParseTerm(EvalCtx* ctx) {
    double left = ParsePower(ctx);
    while (1) {
        SkipWhitespace(ctx);
        char op = ctx->src[ctx->pos];
        if (op == '*' || op == '/') {
            ctx->pos++;
            double right = ParsePower(ctx);
            if (op == '*') {
                left *= right;
            } else {
                if (fabs(right) < 1e-12) {
                    ctx->err = CELL_ERR_DIV0;
                    return 0.0;
                }
                left /= right;
            }
        } else {
            break;
        }
    }
    return left;
}

/* Expr = Term (('+' | '-') Term)* */
static double ParseExpr(EvalCtx* ctx) {
    double left = ParseTerm(ctx);
    while (1) {
        SkipWhitespace(ctx);
        char op = ctx->src[ctx->pos];
        if (op == '+' || op == '-') {
            ctx->pos++;
            double right = ParseTerm(ctx);
            if (op == '+') left += right;
            else left -= right;
        } else {
            break;
        }
    }
    return left;
}

/* -------------------------------------------------------------
 * Public Evaluator Functions
 * ------------------------------------------------------------- */

void SheetEvaluateCell(SheetTab* tab, int row, int col) {
    if (!tab || row < 0 || row >= SHEET_MAX_ROWS || col < 0 || col >= SHEET_MAX_COLS) return;

    SheetCell* cell = &tab->cells[row][col];
    tab->evalVisiting[row][col] = 1; /* Mark visiting for cycle detection */

    /* If raw text is empty */
    if (cell->rawText[0] == '\0') {
        cell->type = CELL_TYPE_EMPTY;
        cell->error = CELL_ERR_NONE;
        cell->computedValue = 0.0;
        cell->displayText[0] = '\0';
        tab->evalVisiting[row][col] = 2;
        return;
    }

    /* If starts with '=' -> Formula */
    if (cell->rawText[0] == '=') {
        cell->type = CELL_TYPE_FORMULA;
        EvalCtx ctx;
        ctx.src = cell->rawText + 1;
        ctx.pos = 0;
        ctx.tab = tab;
        ctx.err = CELL_ERR_NONE;

        double res = ParseExpr(&ctx);
        cell->error = ctx.err;
        cell->computedValue = res;

        if (cell->error == CELL_ERR_DIV0) {
            snprintf(cell->displayText, sizeof(cell->displayText), "#DIV/0!");
        } else if (cell->error == CELL_ERR_CYCLE) {
            snprintf(cell->displayText, sizeof(cell->displayText), "#CYCLE!");
        } else if (cell->error == CELL_ERR_REF) {
            snprintf(cell->displayText, sizeof(cell->displayText), "#REF!");
        } else if (cell->error == CELL_ERR_NAME) {
            snprintf(cell->displayText, sizeof(cell->displayText), "#NAME?");
        } else if (cell->error != CELL_ERR_NONE) {
            snprintf(cell->displayText, sizeof(cell->displayText), "#VALUE!");
        } else {
            SheetFormatNumber(res, cell->displayText, sizeof(cell->displayText));
        }

        tab->evalVisiting[row][col] = 2;
        return;
    }

    /* Check if rawText is a pure number */
    char* endPtr = NULL;
    double numVal = strtod(cell->rawText, &endPtr);
    if (endPtr != cell->rawText && *endPtr == '\0') {
        cell->type = CELL_TYPE_NUMBER;
        cell->error = CELL_ERR_NONE;
        cell->computedValue = numVal;
        SheetFormatNumber(numVal, cell->displayText, sizeof(cell->displayText));
    } else {
        /* Plain text */
        cell->type = CELL_TYPE_TEXT;
        cell->error = CELL_ERR_NONE;
        cell->computedValue = 0.0;
        strncpy(cell->displayText, cell->rawText, sizeof(cell->displayText) - 1);
        cell->displayText[sizeof(cell->displayText) - 1] = '\0';
    }

    tab->evalVisiting[row][col] = 2;
}

void SheetEvaluateAll(SheetTab* tab) {
    if (!tab) return;

    /* Clear evaluation state matrix */
    memset(tab->evalVisiting, 0, sizeof(tab->evalVisiting));

    /* First pass: evaluate all non-empty cells */
    for (int r = 0; r < SHEET_MAX_ROWS; r++) {
        for (int c = 0; c < SHEET_MAX_COLS; c++) {
            if (tab->evalVisiting[r][c] == 0) {
                SheetEvaluateCell(tab, r, c);
            }
        }
    }
}

void SheetComputeStats(SheetTab* tab, const SheetRange* range, SheetStats* outStats) {
    if (!tab || !range || !outStats) return;

    outStats->sum = 0.0;
    outStats->avg = 0.0;
    outStats->min = 0.0;
    outStats->max = 0.0;
    outStats->count = 0;
    outStats->numCount = 0;

    bool firstNum = true;
    for (int r = range->startRow; r <= range->endRow; r++) {
        for (int c = range->startCol; c <= range->endCol; c++) {
            SheetCell* cell = &tab->cells[r][c];
            if (cell->rawText[0] != '\0') {
                outStats->count++;
                if (cell->type == CELL_TYPE_NUMBER || (cell->type == CELL_TYPE_FORMULA && cell->error == CELL_ERR_NONE)) {
                    double val = cell->computedValue;
                    outStats->sum += val;
                    outStats->numCount++;
                    if (firstNum) {
                        outStats->min = val;
                        outStats->max = val;
                        firstNum = false;
                    } else {
                        if (val < outStats->min) outStats->min = val;
                        if (val > outStats->max) outStats->max = val;
                    }
                }
            }
        }
    }

    if (outStats->numCount > 0) {
        outStats->avg = outStats->sum / (double)outStats->numCount;
    }
}
