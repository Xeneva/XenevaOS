/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2023, Manas Kamal Choudhury
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice, this
*    list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following disclaimer in the documentation
*    and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
**/

#include <_xeneva.h>
#include <sys/_keproc.h>
#include <sys/_kefile.h>
#include <sys/iocodes.h>
#include <ctype.h>
#include <chitralekha.h>
#include <widgets/window.h>
#include <keycode.h>
#include "term.h"
#include "arrayfont.h"
#include <sys/mman.h>
#include "esccode.h"
#include <sys/_ketty.h>
#include <sys/_ketime.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>

ChWindow* win;
ChitralekhaApp* app;
ChFont* consolas;
int master_fd;
int slave_fd;
Terminal term;
bool dirty = false;
bool _escape_seq = false;
bool _seq_csi = false;
bool _seq_osc = false;
bool _cursor_blink = 0;
bool _update_terminal_ = false;
bool _first_time = false;
uint32_t backColor;
uint32_t fgColor;
char* escBuf;
char oscBuf[64];
int oscLen;
int shell_id;

/* Glassmorphic palette: the alpha byte makes the compositor's
 * WINDOW_FLAG_GLASS blend show the blurred desktop backdrop through the
 * surface. _SOLID keeps the block cursor legible. --axiss */
#define TERMINAL_BLACK		 0xCC373434
#define TERMINAL_BLACK_SOLID 0xFF373434
#define TERMINAL_GLASS_ALPHA 0xC8

static inline int _terminal_cell_to_pixelX(Terminal* t, int col) {
	return t->originX + col * t->cellW;
}

static inline int _terminal_cell_to_pixelY(Terminal* t, int row) {
	return t->originY + row * t->cellH;
}

static void _terminal_mouse_to_cell(Terminal* t, int mouseX, int mouseY, int* cellX, int* cellY) {
	int relX = mouseX - t->originX;
	int relY = mouseY - t->originY;

	*cellX = relX / t->cellW;
	*cellY = relY / t->cellH;

	if (*cellX < 0)
		*cellX = 0;
	if (*cellX >= t->cols)
		*cellX = t->cols - 1;
	if (*cellY < 0)
		*cellY = 0;
	if (*cellY >= t->rows)
		*cellY = t->rows - 1;
}

void TerminalDrawCell(Terminal* t, int col, int row);

static void _terminal_redraw_cursor(Terminal* t) {
	if (!t->blink_visible)
		return;
	if (t->scrolling)
		return;
	TermCell* c = &t->cells[t->cursorY][t->cursorX];
	int px = _terminal_cell_to_pixelX(t, t->cursorX);
	int py = _terminal_cell_to_pixelY(t, t->cursorY);

	uint32_t cursor_col = 0xFFAEAEAE; //c->bg;

	uint32_t* pixels = win->canv->buffer;
	for (int y = 0; y < t->cellH; y++) {
		uint32_t* row = pixels + (py + y) * win->canv->canvasWidth + px;
		for (int x = 0; x < t->cellW; x++)
			row[x] = cursor_col;
	}

	if (c->c && c->c != ' ') {
		char buf[2] = {c->c, '\0'};
		ChRect clip;
		clip.x = px;
		clip.y = py;
		clip.w = t->cellW;
		clip.h = t->cellH;
		ChFontDrawTextClipped(win->canv, consolas, buf, px, py + t->baseine, TERMINAL_BLACK_SOLID, &clip);
	}
	// draw the character here
	ChWindowUpdate(win, px, py, t->cellW, t->cellH, 0, 1);
}

static void _terminal_erase_cursor(Terminal* t) {
	if (t->scrolling)
		return;
	TermCell* cell = &term.cells[t->cursorY][t->cursorX];
	cell->flags |= (1 << 1);
	if (!cell->bg)
		cell->bg = TERMINAL_BLACK;
	TerminalDrawCell(t, t->cursorX, t->cursorY);
	cell->flags &= ~(1 << 1);
	int px = _terminal_cell_to_pixelX(t, t->cursorX);
	int py = _terminal_cell_to_pixelY(t, t->cursorY);
	ChWindowUpdate(win, px, py, t->cellW, t->cellH, 0, 1);
}

/* TerminalBlinkTick -- synchronous block-cursor toggle, driven from
 * TerminalThread's idle loop. The old SIGALRM handler painted
 * asynchronously in the middle of streaming output: blocks got stamped at
 * stale cursor positions that later erases (which always target the
 * *current* position) never cleaned up, smearing blocks across the text.
 * Painting only from the reader thread makes every block's lifetime
 * explicit: parked before a burst, redrawn after, toggled only while idle.
 * --axiss */
static void TerminalBlinkTick(Terminal* t) {
	if (t->cursor_hide || t->scrolling)
		return;
	if (t->blink_visible) {
		_terminal_erase_cursor(t);
		t->blink_visible = false;
	} else {
		t->blink_visible = true;
		_terminal_redraw_cursor(t);
	}
}
/*
 * TerminalDrawArrayFont -- draw bitmap fonts using defined array
 * @param canv -- Pointer to canvas 
 * @param x -- X coordinate of the font
 * @param y -- Y coordinate of the font
 * @param c -- character to draw
 * @param color -- color to use for drawing
 */
void TerminalDrawArrayFont(
	ChCanvas* canv, unsigned x, unsigned y, unsigned char c, uint32_t color) {
	uint8_t shiftline;
	for (int i = 0; i < 12; i++) {
		shiftline = font_array[i * 128 + c];
		for (int j = 0; j < 8; j++) {
			if (shiftline & 0x80)
				canv->buffer[(i + y) * canv->canvasWidth + (j + x)] = color;
			shiftline <<= 1;
		}
	}
}

void TerminalPutChar(Terminal* t, char ch, uint32_t bg, uint32_t fg) {
	TermCell* c = &t->cells[t->cursorY][t->cursorX];
	c->c = ch;
	c->fg = fg;
	c->bg = bg;
	c->flags |= 0x1;
}

void TerminalSetCellData(Terminal* t, int row, int col, char ch, uint32_t bg, uint32_t fg) {
	TermCell* c = &t->cells[row][col];
	c->c = ch;
	c->fg = fg;
	c->bg = bg;
	c->flags |= 0x1;
}

/*
 * TerminalDrawCell -- draw a particular cell
 * @param x -- x position of the cell
 * @param y -- y position of the cell
 * @param dirty -- dirty specifies was this a single cell update?
 */
void TerminalDrawCell(Terminal* t, int col, int row) {
	int y_offset = 26;
	TermCell* cell = &t->cells[row][col];

	int px = _terminal_cell_to_pixelX(t, col);
	int py = _terminal_cell_to_pixelY(t, row);

	/* A selected cell paints with fg and bg exchanged, and paints even
	 * when empty so the highlight reaches the end of the line. The cell
	 * itself is left alone: output can arrive under a live selection and
	 * the real colours have to survive that. */
	bool selected = (cell->flags & TERMINAL_CELL_SELECTED) != 0;
	uint32_t bg = selected ? cell->fg : cell->bg;
	uint32_t fg = selected ? cell->bg : cell->fg;

	if (cell->c || cell->flags & (1 << 1) || selected) {
		ChDrawRect(win->canv, px, py, t->cellW, t->cellH, bg);
		if (cell->c) {
			char buf[2] = {cell->c, '\0'};
			ChRect clip;
			clip.x = px;
			clip.y = py;
			clip.w = t->cellW;
			clip.h = t->cellH;

			ChFontDrawTextClipped(win->canv, consolas, buf, px, py + t->baseine, fg, &clip);
		}
	}
}

void TerminalFlush(Terminal* t) {
	int minX = INT_MAX, minY = INT_MAX, maxX = 0, maxY = 0;
	bool any_dirty = false;

	for (int r = 0; r < t->rows; r++) {
		for (int c = 0; c < t->cols; c++) {
			TermCell* cell = &t->cells[r][c];
			if (!(cell->flags & 0x1))
				continue;
			TerminalDrawCell(t, c, r);
			cell->flags &= ~0x1;
			cell->flags &= ~(1 << 1);

			int px = _terminal_cell_to_pixelX(t, c);
			int py = _terminal_cell_to_pixelY(t, r);
			if (px < minX)
				minX = px;
			if (py < minY)
				minY = py;
			if (px + t->cellW > maxX)
				maxX = px + t->cellW;
			if (py + t->cellH > maxY)
				maxY = py + t->cellH;
			any_dirty = true;
		}
	}

	if (any_dirty) {
		ChWindowUpdate(win, minX, minY, maxX - minX, maxY - minY, 0, 1);
	}
}

/*
 * TerminalDrawAllCells -- update all the cells to canvas
 */
void TerminalDrawAllCells() {}

/* TerminalDrawCursor -- draws the cursor */
void TerminalDrawCursor() {}

/* -----------------------------------------------------------
 * Mouse selection.
 *
 * The anchor is lastCellXClicked/YClicked -- the cell the drag
 * started on -- and selEndX/Y is wherever the pointer has got to.
 * Highlighting is a flag on each cell plus a repaint, never a
 * colour swap inside the cell, so text arriving underneath a live
 * selection keeps its real colours. selPaintX0..Y1 remembers the
 * rectangle that is currently lit, so shrinking a selection only
 * touches the rows it is leaving; that matters because this runs
 * on every mouse motion, and walking the whole grid per event is
 * what makes a drag stutter.
 * --------------------------------------------------------- */

/* TerminalSelectionUnpaint -- take the highlight off the rectangle that was
 * painted last and forget that rectangle. Leaves selActive alone. */
static void TerminalSelectionUnpaint(Terminal* t) {
	if (t->selPaintY0 < 0)
		return; /* nothing was lit, and the rectangle is already empty */
	for (int y = t->selPaintY0; y <= t->selPaintY1 && y < t->rows; y++)
		for (int x = t->selPaintX0; x <= t->selPaintX1 && x < t->cols; x++) {
			TermCell* cell = &t->cells[y][x];
			if (cell->flags & TERMINAL_CELL_SELECTED) {
				cell->flags &= ~TERMINAL_CELL_SELECTED;
				cell->flags |= 0x1;
			}
		}
	t->selPaintX0 = t->selPaintY0 = t->selPaintX1 = t->selPaintY1 = -1;
}

/* TerminalSelectionClear -- unpaint and switch selection off */
static void TerminalSelectionClear(Terminal* t) {
	TerminalSelectionUnpaint(t);
	t->selActive = false;
}

/* TerminalSelectionCorners -- order the anchor and the dragged corner into an
 * inclusive top-left and bottom-right, clamped to the grid. False when the
 * result is empty, so callers can bail before touching cells. */
static bool TerminalSelectionCorners(Terminal* t, int* x0, int* y0, int* x1, int* y1) {
	*x0 = t->lastCellXClicked;
	*x1 = t->selEndX;
	if (*x0 > *x1) {
		int s = *x0;
		*x0 = *x1;
		*x1 = s;
	}
	*y0 = t->lastCellYClicked;
	*y1 = t->selEndY;
	if (*y0 > *y1) {
		int s = *y0;
		*y0 = *y1;
		*y1 = s;
	}
	if (*x0 < 0)
		*x0 = 0;
	if (*x1 >= t->cols)
		*x1 = t->cols - 1;
	if (*y0 < 0)
		*y0 = 0;
	if (*y1 >= t->rows)
		*y1 = t->rows - 1;
	return *y1 >= *y0 && *x1 >= *x0;
}

/* TerminalSelectionRefresh -- light the range for the current selection */
static void TerminalSelectionRefresh(Terminal* t) {
	if (!t->selActive)
		return;
	int x0, y0, x1, y1;
	if (!TerminalSelectionCorners(t, &x0, &y0, &x1, &y1))
		return;

	TerminalSelectionUnpaint(t);
	for (int y = y0; y <= y1; y++) {
		int sx = (y == y0) ? x0 : 0;
		int ex = (y == y1) ? x1 : t->cols - 1;
		for (int x = sx; x <= ex; x++) {
			TermCell* cell = &t->cells[y][x];
			if (!(cell->flags & TERMINAL_CELL_SELECTED)) {
				cell->flags |= TERMINAL_CELL_SELECTED | 0x1;
			}
		}
	}
	/* the bounding rectangle, not the ragged shape: unpaint walks this
	 * and only clears cells that actually carry the flag */
	t->selPaintX0 = 0;
	t->selPaintY0 = y0;
	t->selPaintX1 = t->cols - 1;
	t->selPaintY1 = y1;
	TerminalFlush(t);
}

/* TerminalScroll -- scrolls the current terminal 
 * one line up
 */
void TerminalScroll(Terminal* t, int lines) {
	/* the cells slide under the highlight when the screen scrolls, so the
	 * range stops meaning anything -- drop it rather than leave lit cells
	 * marking text that is no longer there */
	TerminalSelectionClear(t);
	t->scrolling = true;
	int regionRows = t->scrollBot - t->scrollTop + 1;
	if (lines > regionRows)
		lines = regionRows;

	//	_terminal_erase_cursor(t);

	for (int r = t->scrollTop; r <= t->scrollBot - lines; r++)
		memcpy(t->cells[r], t->cells[r + lines], t->cols * sizeof(TermCell));

	for (int r = t->scrollBot - lines + 1; r <= t->scrollBot; r++) {
		for (int c = 0; c < t->cols; c++) {
			t->cells[r][c].c = ' ';
			t->cells[r][c].fg = t->defaultFg;
			t->cells[r][c].bg = t->defaultBG;
			t->cells[r][c].flags &= ~0x1;
		}
	}

	uint32_t* pixels = win->canv->buffer;
	int canvasW = (int)win->canv->canvasWidth;
	int canvasH = (int)win->canv->canvasHeight;

	int regionPx = _terminal_cell_to_pixelX(t, 0);
	int regionPy = _terminal_cell_to_pixelY(t, t->scrollTop);
	int regionW = t->cols * t->cellW;
	int regionH = regionRows * t->cellH;

	if (regionPx < 0)
		regionPx = 0;
	if (regionPy < 0)
		regionPy = 0;
	if (regionPx >= canvasW || regionPy >= canvasH)
		regionW = 0;
	if (regionPx + regionW > canvasW)
		regionW = canvasW - regionPx;
	if (regionPy + regionH > canvasH)
		regionH = canvasH - regionPy;
	if (regionW < 0)
		regionW = 0;
	if (regionH < 0)
		regionH = 0;

	int shiftRows = regionRows - lines;
	int shiftH = shiftRows * t->cellH;
	int srcOff = lines * t->cellH;

	for (int y = 0; y < shiftH && regionW > 0; y++) {
		int dy = regionPy + y;
		int sy = dy + srcOff;
		uint32_t* dst;
		uint32_t* src;
		if (dy < 0 || sy < 0 || dy >= canvasH || sy >= canvasH)
			continue;
		dst = pixels + dy * canvasW + regionPx;
		src = pixels + sy * canvasW + regionPx;
		memmove(dst, src, regionW * sizeof(uint32_t));
	}

	int exposedRow = t->scrollBot - lines + 1;
	int exposedPy = _terminal_cell_to_pixelY(t, exposedRow);
	int exposedH = lines * t->cellH;

	for (int y = 0; y < exposedH && regionW > 0; y++) {
		int py = exposedPy + y;
		uint32_t* row;
		int x;
		if (py < 0 || py >= canvasH)
			continue;
		row = pixels + py * canvasW + regionPx;
		for (x = 0; x < regionW; x++)
			row[x] = t->defaultBG;
	}

	for (int r = exposedRow; r <= t->scrollBot; r++)
		for (int c = 0; c < t->cols; c++)
			TerminalDrawCell(t, c, r);

	ChWindowUpdate(win, regionPx, regionPy, regionW, regionH, 0, 1);
	t->cursorY = t->scrollBot;
	t->scrolling = false;
}

/* TerminalClearScreen -- clears entire screen area of
 * terminal
 */
void TerminalClearScreen(Terminal* t) {
	/* the highlight flags live on the cells this is about to overwrite */
	TerminalSelectionClear(t);
	t->lastCursorX = t->cursorX;
	t->lastCursorY = t->cursorY;
	_terminal_erase_cursor(t);
	for (int r = 0; r < t->rows; r++) {
		for (int c = 0; c < t->cols; c++) {
			t->cells[r][c].c = ' ';
			t->cells[r][c].fg = t->defaultFg;
			t->cells[r][c].bg = t->defaultBG;
			t->cells[r][c].flags |= 0x1;
		}
	}
	t->cursorX = 0;
	t->cursorY = 0;

	int drawableH = t->rows * t->cellH;
	ChDrawRect(win->canv, 0, t->originY, t->cols * t->cellW, drawableH, t->defaultBG);
	ChWindowUpdate(win, 0, t->originY, t->cols * t->cellW, drawableH, 0, 1);
}

//ESC[2K - clear entire line
void TerminalClearLine(Terminal* t) {
	int r = t->cursorY;
	for (int c = 0; c < t->cols; c++) {
		t->cells[r][c].c = ' ';
		t->cells[r][c].fg = t->defaultFg;
		t->cells[r][c].bg = t->defaultBG;
		t->cells[r][c].flags &= ~0x1;
	}

	int px = _terminal_cell_to_pixelX(t, 0);
	int py = _terminal_cell_to_pixelY(t, r);

	ChDrawRect(win->canv, px, py, t->cols * t->cellW, t->cellH, t->defaultBG);
	ChWindowUpdate(win, px, py, t->cols * t->cellW, t->cellH, 0, 1);
}

void TerminalClearLineToCursor(Terminal* t) {
	int r = t->cursorY;
	for (int c = 0; c <= t->cursorX; c++) {
		t->cells[r][c].c = ' ';
		t->cells[r][c].fg = t->defaultFg;
		t->cells[r][c].bg = t->defaultBG;
		t->cells[r][c].flags &= ~0x1;
	}
	int px = _terminal_cell_to_pixelX(t, 0);
	int py = _terminal_cell_to_pixelY(t, r);
	int w = (t->cursorX + 1) * t->cellW;
	ChDrawRect(win->canv, px, py, w, t->cellH, t->defaultBG);
	ChWindowUpdate(win, px, py, w, t->cellH, 0, 1);
}

void TerminalClearLineFromCursor(Terminal* t) {
	int r = t->cursorY;
	for (int c = t->cursorX; c < t->cols; c++) {
		t->cells[r][c].c = ' ';
		t->cells[r][c].fg = t->defaultFg;
		t->cells[r][c].bg = t->defaultBG;
		t->cells[r][c].flags &= ~0x1;
	}
	int px = _terminal_cell_to_pixelX(t, t->cursorX);
	int py = _terminal_cell_to_pixelY(t, r);
	int w = (t->cols - t->cursorX) * t->cellW;
	ChDrawRect(win->canv, px, py, w, t->cellH, t->defaultBG);
	ChWindowUpdate(win, px, py, w, t->cellH, 0, 1);
}

/*
 * TerminalPrintChar -- print a single character
 * @param char c -- character to print
 * @param fgcolor -- Foreground color
 * @param bgcolor -- Background color
 */
void TerminalPrintChar(Terminal* t, char c, uint32_t fgcolor, uint32_t bgcolor) {
	if (c == '\n') {
		fgColor = WHITE;
		backColor = TERMINAL_BLACK;
		t->lastCursorY = t->cursorY;
		t->lastCursorX = t->cursorX;
		/* No per-character erase here: the batch owner (TerminalThread)
		 * parks the cursor once before painting and redraws it once after.
		 * Erasing per char cost a ChWindowUpdate -- and in shared-buffer
		 * builds a full compositor round-trip -- for every single byte,
		 * throttling output to ~60 chars/sec. --axiss */
		t->cursorY++;
		t->cursorX = 0;
		if (t->cursorY >= t->scrollBot) {
			//t->cursorY = t->scrollBot;
			TerminalScroll(t, 1);
			//return;
		}
	} else if (c == '\r') {
		t->cursorX = 0;
		//return;
	} else if (c == '\b') {
		t->cursorX--;
		if (t->cursorX < 0) {
			t->cursorY--;
			t->cursorX = t->cols;
		}
		//TerminalSetCellData(cursor_x, cursor_y, 0, backColor, fgColor);
		if (t->cursorY < 0)
			t->cursorY = 0;
		TerminalPutChar(t, ' ', backColor, fgColor);
		//return;
	} else {
		TerminalPutChar(t, c, backColor, fgColor);
		t->lastCursorX = t->cursorX;
		t->lastCursorY = t->cursorY;
		/* See above: cursor parking lives with the batch, not the byte. */
		t->cursorX++;
		if (t->cursorX == t->cols) {
			t->cursorX = 0;
			t->cursorY++;
			if (t->cursorY >= t->scrollBot - 1)
				TerminalScroll(t, 1);
		}
	}
}

/* TerminalPrintString -- prints sequences of characters
 * @param string -- string to print
 * @param fgcolor -- foreground color
 * @param bgcolor -- background color
 */
void TerminalPrintString(Terminal* t, char* string, uint32_t fgcolor, uint32_t bgcolor) {
	while (*string) {
		TerminalPrintChar(t, *string, fgcolor, bgcolor);
		string++;
	}
}

void TerminalReplaceInput(Terminal* t, const char* newText) {
	int oldLen = t->intputLen;
	char slave_buf[2];
	slave_buf[0] = '\b';
	slave_buf[1] = '\0';
	int curX = t->cursorX;
	int curY = t->cursorY;
	t->lastCursorX = curX;
	t->lastCursorY = curY;
	for (int i = 0; i < oldLen; i++) {
		if (curX > 0) {
			curX--;
		} else if (curY > 0) {
			curY--;
			curX = t->cols - 1;
		}
		TermCell* c = &t->cells[curY][curX];
		c->c = ' ';
		c->fg = t->defaultFg;
		c->bg = t->defaultBG;
		c->flags |= 0x1;

		/** drain the last character passed to slave buffer */
		_KeWriteFile(master_fd, slave_buf, 1);
	}

	strncpy(t->inputBuffer, newText, 255);
	t->inputBuffer[255] = '\0';
	t->intputLen = strlen(t->inputBuffer);

	for (int i = 0; i < t->intputLen; i++) {
		//TerminalPrintChar(t, t->inputBuffer[i], t->defaultFg, t->defaultBG);
		_KeWriteFile(master_fd, &t->inputBuffer[i], 1);
	}

	TerminalFlush(t);
	//_update_terminal_ = 1;
}

void TerminalHistoryPush(Terminal* t, const char* cmd) {
	if (!cmd || cmd[0] == '\0')
		return;

	/*if (t->history.count > 0) {
		int last = (t->history.head - 1 + TERMINAL_HISTORY_MAX) % TERMINAL_HISTORY_MAX;
		if (strcmp(t->history.entries[last], cmd) == 0) {
			t->history.browse = -1;
			return;
		}
	}*/
	for (int i = 0; i < t->history.count; i++) {
		int idx = (t->history.head - 1 - i + TERMINAL_HISTORY_MAX * 2) % TERMINAL_HISTORY_MAX;
		if (strcmp(t->history.entries[idx], cmd) == 0) {
			for (int j = i; j > 0; j--) {
				int dst =
					(t->history.head - 1 - j + TERMINAL_HISTORY_MAX * 2) % TERMINAL_HISTORY_MAX;
				int src =
					(t->history.head - 1 - j + 1 + TERMINAL_HISTORY_MAX * 2) % TERMINAL_HISTORY_MAX;
				memcpy(t->history.entries[dst], t->history.entries[src], 256);
			}

			t->history.head = (t->history.head - 1 + TERMINAL_HISTORY_MAX) % TERMINAL_HISTORY_MAX;
			t->history.count--;
			break;
		}
	}

	strncpy(t->history.entries[t->history.head], cmd, 255);
	t->history.entries[t->history.head][255] = '\0';
	t->history.head = (t->history.head + 1) % TERMINAL_HISTORY_MAX;
	if (t->history.count < TERMINAL_HISTORY_MAX)
		t->history.count++;

	t->history.browse = -1;
}

void TerminalHistoryUp(Terminal* t) {
	if (t->history.count == 0)
		return;

	if (t->history.browse == -1) {
		strncpy(t->inputSaved, t->inputBuffer, 255);
		t->history.browse = 0;
	} else {
		if (t->history.browse < t->history.count - 1)
			t->history.browse++;
		else
			return;
	}

	int idx =
		(t->history.head - 1 - t->history.browse + TERMINAL_HISTORY_MAX * 2) % TERMINAL_HISTORY_MAX;
	_terminal_erase_cursor(t);
	TerminalReplaceInput(t, t->history.entries[idx]);
}

void TerminalHistoryDown(Terminal* t) {
	if (t->history.browse == -1)
		return;

	if (t->history.browse > 0) {
		t->history.browse--;
		int idx = (t->history.head - 1 - t->history.browse + TERMINAL_HISTORY_MAX * 2) %
				  TERMINAL_HISTORY_MAX;
		_terminal_erase_cursor(t);
		TerminalReplaceInput(t, t->history.entries[idx]);
	} else {
		t->history.browse = -1;
		_terminal_erase_cursor(t);
		TerminalReplaceInput(t, t->inputSaved);
	}
}
/* ProcessControlSequence -- the main emulation function 
 * of ANSI Terminal
 * @param ch -- Character to emulate
 */
void ProcessControlSequence(Terminal* term_, char ch) {
	/* Emulates graphics rendition */
	if (ch == CSI_SET_GRAPHICS_RENDITION) {
		for (int i = 0; i < 256; i++) {
			if (escBuf[i] == 'm') {
				escBuf[i] = 0;
				break;
			}
		}
		int colorCode = atoi(escBuf);
		switch (colorCode) {
		case CSI_SET_BG_BLACK:
			backColor = TERMINAL_BLACK; // BLACK;
			break;
		case CSI_SET_BG_BLUE:
			backColor = BLUE;
			break;
		case CSI_SET_BG_BROWN:
			backColor = BROWN;
			break;
		case CSI_SET_BG_CYAN:
			backColor = CYAN;
			break;
		case CSI_SET_BG_DEFAULT:
			backColor = TERMINAL_BLACK; // BLACK;
			break;
		case CSI_SET_BG_GREEN:
			backColor = GREEN;
			break;
		case CSI_SET_BG_MAGENTA:
			backColor = MAGENTA;
			break;
		case CSI_SET_BG_RED:
			backColor = RED;
			break;
		case CSI_SET_BG_WHITE:
			backColor = WHITE;
			break;
		case CSI_SET_FG_BLUE:
			fgColor = BLUE;
			break;
		case CSI_SET_FG_BROWN:
			fgColor = BROWN;
			break;
		case CSI_SET_FG_CYAN:
			fgColor = CYAN;
			break;
		case CSI_SET_FG_BLACK:
			fgColor = BLACK;
			break;
		case CSI_SET_FG_GREEN:
			fgColor = GREEN;
			break;
		case CSI_SET_FG_MAGENTA:
			fgColor = MAGENTA;
			break;
		case CSI_SET_FG_RED:
			fgColor = RED;
			break;
		case CSI_SET_FG_WHITE:
			fgColor = WHITE;
			break;
		case CSI_SET_FG_DEFAULT:
			fgColor = WHITE;
			break;
		}
		_escape_seq = false;
		_seq_csi = false;
		memset(escBuf, 0, 256);
		return;
	}

	/* emulates cursor attributes */
	if (ch == CSI_CURSOR_UP) {
		int index = 0;
		for (int i = 0; i < 256; i++) {
			if (escBuf[i] == CSI_CURSOR_UP) {
				escBuf[i] = 0;
				index = i;
				break;
			}
		}

		int count = atoi(escBuf);
		if (count == 0)
			count = 1;
		term_->cursorY -= count;
		if (term_->cursorY <= 0)
			term_->cursorY = 0;
		_escape_seq = false;
		_seq_csi = false;
		memset(escBuf, 0, 256);
		return;
	}
	if (ch == CSI_CURSOR_BACKWARD) {
		for (int i = 0; i < 256; i++) {
			if (escBuf[i] == CSI_CURSOR_BACKWARD) {
				escBuf[i] = 0;
				break;
			}
		}
		int count = atoi(escBuf);
		if (count == 0)
			count = 1;
		term_->cursorX -= count;
		if (term_->cursorX <= 0)
			term_->cursorX = 0;
		_escape_seq = false;
		_seq_csi = false;
		memset(escBuf, 0, 256);
		return;
	}
	if (ch == CSI_CURSOR_FORWARD) {
		for (int i = 0; i < 256; i++) {
			if (escBuf[i] == CSI_CURSOR_FORWARD) {
				escBuf[i] = 0;
				break;
			}
		}
		int count = atoi(escBuf);
		if (count == 0)
			count = 1;
		term_->cursorX += count;
		if (term_->cursorX == term_->cols - 1) {
			term_->cursorX = 0;
			term_->cursorY++;
		}
		_escape_seq = false;
		_seq_csi = false;
		memset(escBuf, 0, 256);
		return;
	}
	if (ch == CSI_CURSOR_DOWN) {
		for (int i = 0; i < 256; i++) {
			if (escBuf[i] == CSI_CURSOR_DOWN) {
				escBuf[i] = 0;
				break;
			}
		}
		int count = atoi(escBuf);
		if (count == 0)
			count = 1;
		term_->cursorY += count;
		if (term_->cursorY >= term_->rows)
			term_->cursorY--;
		_escape_seq = false;
		_seq_csi = false;
		memset(escBuf, 0, 256);
		return;
	}

	if (ch == CSI_CURSOR_HOME || ch == 'f') {
		for (int i = 0; i < 256; i++) {
			if (escBuf[i] == CSI_CURSOR_HOME || escBuf[i] == 'f') {
				escBuf[i] = 0;
				break;
			}
		}
		int row = 0, col = 0;
		char* semi = strchr(escBuf, ';');
		if (semi) {
			*semi = '\0';
			row = atoi(escBuf);
			col = atoi(semi + 1);
		} else {
			row = atoi(escBuf);
			col = 0;
		}

		if (row > 0)
			row--;
		if (col > 0)
			col--;

		if (row < 0)
			row = 0;
		if (row >= term.rows)
			row = term.rows - 1;
		if (col < 0)
			col = 0;
		if (col >= term.cols)
			col = term.cols - 1;

		term.cursorX = row;
		term.cursorY = col;

		_escape_seq = false;
		_seq_csi = false;
		memset(escBuf, 0, 256);
		return;
	}
	/* emulate erase text non line mode */
	if (ch == CSI_ERASE_TEXT_NONLINE) {
		for (int i = 0; i < 256; i++) {
			if (escBuf[i] == CSI_ERASE_TEXT_NONLINE) {
				escBuf[i] = 0;
				break;
			}
		}
		int value = atoi(escBuf);
		switch (value) {
		case 2:
			/* erase entire screen*/
			TerminalClearScreen(term_);
			break;
		case 1:
			//erase upward
			for (int y = 0; y < term_->cursorY; y++) {
				for (int x = 0; x < term_->cols; x++) {
					TerminalSetCellData(term_, y, x, ' ', TERMINAL_BLACK, WHITE);
				}
			}
			break;
		}
		if (value == 0) {
			/* erase downward */
			for (int y = term_->cursorY; y < term_->rows; y++) {
				for (int x = 0; x < term_->cols; x++) {
					TerminalSetCellData(term_, y, x, ' ', TERMINAL_BLACK, WHITE);
				}
			}
		}
		_escape_seq = false;
		_seq_csi = false;
		memset(escBuf, 0, 256);
		return;
	}

	if (ch == CSI_ERASE_TEXT_LINE) {
		for (int i = 0; i < 256; i++) {
			if (escBuf[i] == CSI_ERASE_TEXT_LINE) {
				escBuf[i] = 0;
				break;
			}
		}
		int n = atoi(escBuf);
		switch (n) {
		case 2:
			/* clear the current line*/
			TerminalClearLine(term_);
			break;
		case 1:
			/* clear the line from start to current cursor position*/
			TerminalClearLineToCursor(term_);
			break;
		case 0:
		default:
			/* clear the line from current cursor position */
			TerminalClearLineFromCursor(term_);
			break;
		}
		_escape_seq = false;
		_seq_csi = false;
		memset(escBuf, 0, 256);
		return;
	}

	if (ch == CSI_SET_MODE || ch == CSI_RESET_MODE) {
		for (int i = 0; i < 256; i++) {
			if (escBuf[i] == ch) {
				escBuf[i] = 0;
				break;
			}
		}

		int mode = 0;
		if (escBuf[0] == '?')
			mode = atoi(escBuf + 1);
		else
			mode = atoi(escBuf);

		switch (mode) {
		case 25:
			term_->blink_visible = (ch == CSI_SET_MODE) ? 1 : 0;
			term_->cursor_hide = !term_->blink_visible;
			//if (term_->cursor_hide == 0)
			//alarm(1);
			_KePrint("Cursor blink change requested %d\r\n", term_->cursor_hide);
			break;

		case 1049:
			break;
		case 1000:
			break;
		default:
			break;
		}
		_escape_seq = false;
		_seq_csi = false;
		memset(escBuf, 0, 256);
		return;
	}

	if (ch == CSI_SAVE_CURSOR) {
		term_->savedCursorX = term_->cursorX;
		term_->savedCursorY = term_->cursorY;
		_escape_seq = false;
		_seq_csi = false;
		memset(escBuf, 0, 256);
		return;
	}

	if (ch == CSI_RESTORE_CURSOR) {
		term_->cursorX = term_->savedCursorX;
		term_->cursorY = term_->savedCursorY;
		_escape_seq = false;
		_seq_csi = false;
		memset(escBuf, 0, 256);
		return;
	}
}

void ProcessOSCSequence(Terminal* t, const char* payload) {
	//OSC 133 - shell integration/ semantic prompt markers
	if (strncmp(payload, "133;", 4) == 0) {
		char marker = payload[4];
		switch (marker) {
		case 'A': {
			t->inputStartX = t->cursorX;
			t->inputStartY = t->cursorY;
			break;
		}

		case 'B': {
			t->inputStartX = t->cursorX;
			t->inputStartY = t->cursorY;
			t->intputLen = 0;
			t->inputBuffer[0] = '\0';
			break;
		}

		case 'C': {
			//input startX = -1;
			t->inputStartX = -1;
			break;
		}
		case 'D': {
			//command done, parse exit code from OSC payload,
			//e.g. 133;D;0-> exit code = atoi(payload+6)
			break;
		}
		}

		if (strncmp(payload, "0;", 2) == 0 || strncmp(payload, "2;", 2) == 0) {
			//Set window title (win, payload+2);
			return;
		}
	}
}

/*
 * TerminalProcessLine -- emulates terminals
 * @param ch -- character to process
 */
void TerminalProcessLine(Terminal* t, char ch) {
	if (_escape_seq) {
		if (ch == SEQUENCE_CSI) {
			_seq_csi = true;
			return;
		}

		if (_seq_csi) {
			char s[] = {ch, 0};
			strncat(escBuf, s, 2);
			ProcessControlSequence(t, ch);
			return;
		}

		if (ch == SEQUENCE_OSC) {
			_seq_osc = true;
			oscLen = 0;
			oscBuf[0] = '\0';
			return;
		}

		if (_seq_osc) {
			if (ch == '\007') {
				oscBuf[oscLen] = '\0';
				ProcessOSCSequence(t, oscBuf);
				_seq_osc = false;
				_escape_seq = false;
				oscLen = 0;
				return;
			}

			if (ch == '\\') {
				oscBuf[oscLen] = '\0';
				ProcessOSCSequence(t, oscBuf);
				_seq_osc = false;
				_escape_seq = false;
				oscLen = 0;
				return;
			}

			if (oscLen < 64) //increase it to 255 in future
				oscBuf[oscLen++] = ch;
			return;
		}
	} else {
		/* process default state */
		if (ch == ASCII_ESC_CHAR) {
			if (_seq_osc)
				return;
			_escape_seq = true;
			return;
		}
		if (ch == ASCII_ESC_OCTAL) {
			_escape_seq = true;
			return;
		}
		if (ch == ASCII_ESC_HEX) {
			_escape_seq = true;
			return;
		}
		if (ch == ASCII_ESC_DECIMAL) {
			_escape_seq = true;
			return;
		}

		TerminalPrintChar(t, ch, fgColor, backColor);
	}
}

void TerminalHandleMouseClick(Terminal* t, int mouseX, int mouseY, int button) {
	/* button carries the state that is held down, not an edge, so the
	 * press that begins a drag is the one arriving while nothing was held
	 * -- and the release has to be delivered for the next press to be
	 * recognised, which is why the caller no longer skips button==0 */
	bool pressed = (button != 0) && (t->lastButtonState == 0);
	t->lastButtonState = button;

	/** skip the titlebar **/
	if (mouseY <= 26)
		return;

	int cellX = 0;
	int cellY = 0;
	_terminal_mouse_to_cell(t, mouseX, mouseY, &cellX, &cellY);

	if (pressed) {
		/* a new drag starts a new selection rather than extending the
		 * old one, and lights the anchor cell straight away so a plain
		 * click still shows the one-cell highlight it always did */
		TerminalSelectionClear(t);
		t->lastCellXClicked = cellX;
		t->lastCellYClicked = cellY;
		t->selEndX = cellX;
		t->selEndY = cellY;
		t->selActive = true;
		TerminalSelectionRefresh(t);
		return;
	}

	/* release keeps the selection; motion with nothing held is a hover */
	if (button == 0 || !t->selActive)
		return;

	t->selEndX = cellX;
	t->selEndY = cellY;
	TerminalSelectionRefresh(t);
}

/* The clipboard device holds CLIPBOARD_MAX (4096) bytes; a longer write is
 * truncated by the device, so the copy builds to the same size and stops.
 * Static rather than a stack frame because this runs on the input thread of
 * a process whose stack also does font work. */
#define TERMINAL_CLIP_MAX 4096
static char _terminal_clip_out[TERMINAL_CLIP_MAX];

/* TerminalClipboardBuildSelection -- flatten the highlighted range into text,
 * one line per row, trailing spaces trimmed so what lands on the host reads
 * like text rather than a filled rectangle. Rows outside the first and last
 * are taken whole, which is what a drag across lines means. */
static int TerminalClipboardBuildSelection(Terminal* t) {
	int x0, y0, x1, y1;
	if (!TerminalSelectionCorners(t, &x0, &y0, &x1, &y1))
		return 0;

	int len = 0;
	for (int y = y0; y <= y1; y++) {
		int sx = (y == y0) ? x0 : 0;
		int ex = (y == y1) ? x1 : t->cols - 1;
		int start = len;
		for (int x = sx; x <= ex; x++) {
			if (len >= TERMINAL_CLIP_MAX - 2)
				break;
			char ch = t->cells[y][x].c;
			_terminal_clip_out[len++] = ch ? ch : ' ';
		}
		while (len > start && _terminal_clip_out[len - 1] == ' ')
			len--;
		if (len >= TERMINAL_CLIP_MAX - 2)
			break;
		if (y != y1)
			_terminal_clip_out[len++] = '\n';
	}
	return len;
}

/* TerminalClipboardCopy -- put something on the clipboard: the highlighted
 * range if there is one, otherwise the line being typed, which is the same
 * rule the tty shell's Ctrl+C uses. An empty line with nothing selected
 * leaves the clipboard alone rather than clearing it. */
static void TerminalClipboardCopy(Terminal* t) {
	int len = 0;

	if (t->selActive)
		len = TerminalClipboardBuildSelection(t);
	else if (t->intputLen > 0) {
		len = t->intputLen;
		memcpy(_terminal_clip_out, t->inputBuffer, len);
	}

	if (len <= 0) {
		_KePrint("[term]: copy skipped, nothing selected and input line empty\r\n");
		return;
	}
	int fd = _KeOpenFile((char*)"/dev/clipboard", FILE_OPEN_WRITE);
	if (fd < 0) {
		_KePrint("[term]: /dev/clipboard unavailable\r\n");
		return;
	}
	_KeWriteFile(fd, (void*)_terminal_clip_out, len);
	_KeCloseFile(fd);
	_KePrint("[term]: copied %d bytes\r\n", len);
}

/* TerminalClipboardPaste -- type the clipboard into the prompt through the
 * same path a keystroke takes, so the shell on the other end sees exactly
 * what a fast typist would have sent. Stops at the first line break: pasting
 * several shell commands at once would run all of them, which is a surprise
 * rather than a convenience. */
static void TerminalClipboardPaste(Terminal* t) {
    int fd = _KeOpenFile((char*)"/dev/clipboard", FILE_OPEN_READ_ONLY | FILE_OPEN_WRITE);
    if (fd < 0) {
        _KePrint("[term]: /dev/clipboard unavailable\r\n");
        return;
    }
    char buf[256];
    int got = _KeReadFile(fd, buf, sizeof(buf) - 1);
    if (got <= 0) {
        _KePrint("[term]: clipboard empty\r\n");
        _KeCloseFile(fd);
        return;
    }
    _KeWriteFile(fd, (void*)"\0", 1);
    _KeCloseFile(fd);
    int pasted = 0;
    for (int i = 0; i < got; i++) {
        char ch = buf[i];
        if (ch == '\n' || ch == '\r')
            break;
        if (ch < 32 || ch > 126)
            continue;
        if (t->intputLen >= 255)
            break;
        t->inputBuffer[t->intputLen++] = ch;
        t->inputBuffer[t->intputLen] = '\0';
        _KeWriteFile(master_fd, &ch, 1);
        pasted++;
    }
    _KePrint("[term]: pasted %d bytes\r\n", pasted);
}

/* Special keys must match on scancode: ChitralekhaGetKeyPress returns ASCII
 * values that collide with printable keys (KEY_KP_8=='8', and the arrow map
 * yields '3'/'.') , so rawkey matching hijacked '8', '3' and '.' while
 * typing addresses and commands. --axiss */
#define TERMINAL_SCANCODE_RELEASE 0x80
#define TERMINAL_SCANCODE_MASK	   0x7f
#define TERMINAL_SCANCODE_UP	   0x48
#define TERMINAL_SCANCODE_LEFT	   0x4b
#define TERMINAL_SCANCODE_RIGHT	   0x4d
/*
 * TerminalHandleMessage -- handle incoming 'Deodhai' messages
 * @param e -- Pointer to PostEvent memory location where 
 * incoming messages are stored
 */
void TerminalHandleMessage(PostEvent* e) {
	switch (e->type) {
	case DEODHAI_REPLY_MOUSE_EVENT:
		/* always delivered, button state included: TerminalHandleMouseClick
		 * needs the release edge to tell one drag from the next */
		TerminalHandleMouseClick(
			&term, e->dword - win->info->x, e->dword2 - win->info->y, e->dword3);
		ChWindowHandleMouse(win, e->dword, e->dword2, e->dword3);
		memset(e, 0, sizeof(PostEvent));
		break;
	case DEODHAI_REPLY_KEY_EVENT: {
		int code = e->dword;
		ChitralekhaProcessKey(code);
		char rawkey = ChitralekhaGetKeyPress(code);
		bool _pass_to_input = true;
		char c = ChitralekhaKeyToASCII(code);
		if (rawkey == KEY_RETURN) {
			TerminalHistoryPush(&term, term.inputBuffer);
			c = '\n';
			term.intputLen = 0;
			term.inputBuffer[0] = '\0';
			_pass_to_input = false;
		}

		if (rawkey == KEY_BACKSPACE) {
			if (term.intputLen > 0) {
				term.intputLen--;
				term.inputBuffer[term.intputLen] = '\0';
				c = '\b';
				_pass_to_input = false;
			}
		}

		/** check from extended key code map (scancode 0x48: Up / keypad 8) **/
		if (!(code & TERMINAL_SCANCODE_RELEASE) &&
			(code & TERMINAL_SCANCODE_MASK) == TERMINAL_SCANCODE_UP) {
			TerminalHistoryUp(&term);
			memset(e, 0, sizeof(PostEvent));
			return;
		}

		/** chec from extended key code map **/
		/**if (rawkey == KEY_KP_2) {
			TerminalHistoryDown(&term);
			memset(e, 0, sizeof(PostEvent));
			return;
		}**/

		if (!(code & TERMINAL_SCANCODE_RELEASE) &&
			(code & TERMINAL_SCANCODE_MASK) == TERMINAL_SCANCODE_LEFT) {
			if (term.cursorX > term.inputStartX) {
				_terminal_erase_cursor(&term);
				term.cursorX--;
				_terminal_redraw_cursor(&term);
			}
			memset(e, 0, sizeof(PostEvent));
		}

		if (!(code & TERMINAL_SCANCODE_RELEASE) &&
			(code & TERMINAL_SCANCODE_MASK) == TERMINAL_SCANCODE_RIGHT) {
			if (term.cursorX < term.inputStartX + term.intputLen) {
				_terminal_erase_cursor(&term);
				term.cursorX++;
				_terminal_redraw_cursor(&term);
			}
			memset(e, 0, sizeof(PostEvent));
		}
		/**
		 * CTRL + SHIFT + C/V -- the host<->VM clipboard, on the same
		 * /dev/clipboard node the tty shell uses, so a copy here reaches
		 * the host and a paste here reads whatever the host last copied.
		 * Shift has to be part of the binding: on its own Ctrl+C is
		 * SIGINT, which is what people already use below. Only Ctrl+X and
		 * Ctrl+A are taken by the compositor's own keybinds, so this
		 * arrives as a raw scancode. ChitralekhaKeyToASCII uppercases
		 * while shift is held, hence both letters are matched.
		 */
		if (ChitralekhaKeyGetCTRL() && ChitralekhaKeyGetShift()) {
			if (c == KEY_C || c == 'C') {
				TerminalClipboardCopy(&term);
				memset(e, 0, sizeof(PostEvent));
				return;
			}
			if (c == KEY_V || c == 'V') {
				TerminalClipboardPaste(&term);
				memset(e, 0, sizeof(PostEvent));
				return;
			}
		}

		/**
		 * handle CTRL + combined keys 
		 */
		if (ChitralekhaKeyGetCTRL()) {
			if (c == KEY_C) {
				_KePrint("Sending signal to id : %d \r\n", shell_id);
				if (shell_id > 0)
					kill(shell_id, SIGINT);
				c = '\n';
			}
			if (c == KEY_H) {
				ChWindowHide(win);
			}

			if (c == KEY_W) {
				TerminalHistoryUp(&term);
				memset(e, 0, sizeof(PostEvent));
				return;
			}

			if (c == KEY_S) {
				TerminalHistoryDown(&term);
				memset(e, 0, sizeof(PostEvent));
				return;
			}
		}

		if (_pass_to_input && c >= 0x20 && c < 0x7F && term.intputLen < 255) {
			term.inputBuffer[term.intputLen++] = c;
			term.inputBuffer[term.intputLen] = '\0';
		}
		_KeWriteFile(master_fd, &c, 1);
		/* else its a key release event */
		memset(e, 0, sizeof(PostEvent));
		break;
	}
	case DEODHAI_REPLY_FOCUS_CHANGED: {
		int focus_val = e->dword;
		int handle = e->dword2;
		ChWindowHandleFocus(win, focus_val, handle);
		memset(e, 0, sizeof(PostEvent));
		break;
	}
	case DEODHAI_REPLY_MOUSE_LEAVE: {
		memset(e, 0, sizeof(PostEvent));
		break;
	}
	}
}

/*
 * TerminalThread -- terminal thread to handle
 * asynchronous reads
 */
void TerminalThread() {
	char* buf = (char*)malloc(1024);
	memset(buf, 0, 1024);
	int bytes_read = 0;
	uint64_t last_blink_ms = _KeGetCurrentMS();
	while (1) {
		bytes_read = _KeReadFile(master_fd, buf, 1024);
		if (bytes_read >= 1024) {
			bytes_read = 1024;
		}

		if (bytes_read > 0 || _update_terminal_) {
			/* Park the block cursor for the whole burst: output below
			 * moves it, and repainting it per byte both throttled us to
			 * one compositor round-trip per character and stranded blocks
			 * at stale positions. --axiss */
			bool was_visible = term.blink_visible && !term.scrolling && !term.cursor_hide;
			if (was_visible) {
				_terminal_erase_cursor(&term);
				term.blink_visible = false;
			}
			for (int i = 0; i < bytes_read; i++) {
				TerminalProcessLine(&term, buf[i]);
			}
			TerminalFlush(&term);
			bytes_read = 0;
			_update_terminal_ = false;
			if (was_visible && !term.scrolling && !term.cursor_hide) {
				term.blink_visible = true;
				_terminal_redraw_cursor(&term);
			}
			last_blink_ms = _KeGetCurrentMS();
			/* No sleep on data: loop straight back to drain the rest of
			 * the burst. The compositor ack inside ChWindowUpdate already
			 * paces us to one present per frame. */
		} else {
			/* Idle: blink here instead of from a signal handler, and nap
			 * so an empty pty doesn't hot-spin. Master reads return 0
			 * when empty, so this branch is the only sleeper. --axiss */
			uint64_t now = _KeGetCurrentMS();
			if (now - last_blink_ms >= 500)
			{
				TerminalBlinkTick(&term);
				last_blink_ms = now;
			}
			_KeProcessSleep(60);
		}
	}
}

/*
* main -- terminal emulator
*/
int main(int argc, char* arv[]) {
	app = ChitralekhaStartApp(argc, arv);
	/* The fixed cell grid and reader thread do not handle buffer replacement. */
	win = ChCreateWindow(app,
						 WINDOW_FLAG_MOVABLE | WINDOW_FLAG_GLASS | WINDOW_FLAG_NON_RESIZABLE,
						 "Xeneva Terminal",
						 300,
						 100,
						 680,
						 450);
	if (!win || !win->info) {
		_KePrint("term: failed to create window \r\n");
		return 1;
	}
	win->info->alpha = false;
	win->info->alphaValue = 0.7;
	win->color = TERMINAL_BLACK;
	/* Chitralekha glass option: translucent chrome over the compositor's
	 * blurred desktop backdrop. Cells keep their own translucent fills. */
	ChWindowSetGlassMorphism(win, TERMINAL_GLASS_ALPHA);

	consolas = ChInitialiseFont(CONSOLAS);
	if (!consolas) {
		_KePrint("term: failed to load CONSOLAS \r\n");
		return 1;
	}
	ChFontSetSize(consolas, 12);

	int f_w = ChFontGetWidthChar(consolas, 'M');
#ifdef _USE_FREETYPE
	if (!consolas->face || !consolas->face->size) {
		_KePrint("term: freetype face not ready \r\n");
		return 1;
	}
	int f_h = consolas->face->size->metrics.height >> 6;
	term.baseine = consolas->face->size->metrics.ascender >> 6;
#else
	int f_h = ChFontGetHeightChar(consolas, 'A');
	term.baseine = f_h - 4;
#endif
	/* Cell needs a few px below the 'A'-height metric: descenders
	 * (g, j, p, q, y) sink ~4px past the baseline and the per-cell
	 * clip would shave them off. */
#ifdef _USE_FREETYPE
	term.cellH = f_h;
#else
	term.cellH = f_h + 4;
#endif

	if (f_w <= 0)
		f_w = 8;
	if (f_h <= 0)
		f_h = 12;
	term.cellW = f_w;

	int term_w = win->info->width;
	int term_h = win->info->height - 16; // -26 for titlebar height

	term.cols = term_w / term.cellW;
	term.rows = term_h / term.cellH;
	term.cursorX = 0;
	term.cursorY = 0;
	term.lastCursorX = term.cursorX;
	term.lastCursorY = term.cursorY;
	_cursor_blink = 0;
	escBuf = (char*)malloc(256);
	memset(escBuf, 0, 256);
	term.defaultBG = TERMINAL_BLACK; // 0xFF000000;// BLACK;
	term.defaultFg = WHITE;
	term.scrollTop = 0;
	term.scrollBot = term.rows - 1;
	term.originX = 1;
	term.originY = 26;
	term.lastCellXClicked = -1;
	term.lastCellYClicked = -1;
	/* no selection, and the painted rectangle is empty: an unpaint with a
	 * zeroed rect would walk row 0 looking for highlights that are not
	 * there, which is harmless but not free */
	term.selActive = false;
	term.lastButtonState = 0;
	term.selPaintX0 = term.selPaintY0 = term.selPaintX1 = term.selPaintY1 = -1;
	term.inputStartX = 0;
	term.inputStartY = 0;
	term.scrolling = 0;
	term.savedCursorX = 0;
	term.savedCursorY = 0;
	term.cursor_hide = 0;
	backColor = term.defaultBG;
	fgColor = term.defaultFg;

	_update_terminal_ = false;
	master_fd = slave_fd = 0;
	/* create the terminal */
	int success = 0;
	_KePrint("Creating TTY : %x - %x \r\n", &master_fd, &slave_fd);
	success = _KeCreateTTY(&master_fd, &slave_fd);
	WinSize sz;
	sz.ws_col = term.cols;
	sz.ws_row = term.rows;
	sz.ws_xpixel = term_w;
	sz.ws_ypixel = term_h;
	_KePrint("TTY Created \r\n");
	_KeFileIoControl(master_fd, TIOCSWINSZ, &sz);

	_KePrint("TIOCSWINSZ done \r\n");

	/*term_buffer = (TermCell*)malloc(ws_col * ws_row * sizeof(TermCell));
	memset(term_buffer, 0x0, static_cast<uint64_t>(ws_col) * ws_row * sizeof(TermCell));*/

	ChWindowPaint(win);

	int term_id = _KeGetProcessID();
	/* try loading the shell process */
	shell_id = _KeCreateProcess(0, "xesh");

	_KeSetFileToProcess(slave_fd, 0, shell_id);
	_KeSetFileToProcess(slave_fd, 1, shell_id);
	_KeSetFileToProcess(slave_fd, 2, shell_id);

	_KeSetFileToProcess(slave_fd, 0, term_id);
	_KeSetFileToProcess(slave_fd, 1, term_id);
	_KeSetFileToProcess(slave_fd, 2, term_id);

	_KeProcessLoadExec(shell_id, "/xesh.exe", 0, 0);

	int thread_idx = _KeCreateThread(TerminalThread, "asyncthr");

	ChWindowBroadcastIcon(app, "/icons/term.bmp");

	term.blink_visible = 1;
	/* Cursor blink is driven synchronously from TerminalThread's idle loop
	 * (see TerminalBlinkTick); no SIGALRM involved, so nothing to arm here. */

	PostEvent e;
	memset(&e, 0, sizeof(PostEvent));
	while (1) {
		int err = _KeFileIoControl(app->postboxfd, POSTBOX_GET_EVENT, &e);
		TerminalHandleMessage(&e);
		if (err == POSTBOX_NO_EVENT) {
			_KePauseThread();
		}
	}
}
