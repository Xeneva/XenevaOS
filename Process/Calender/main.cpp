/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2026, Manas Kamal Choudhury
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

/*
 * Calender -- month view with events, in the knot window style.
 *
 *   +- knot --- calendar ---------------------------------------------+
 *   | november 2026            < > today | wednesday                 |
 *   | sun mon tue wed thu fri sat         | 4 november 2026           |
 *   |  1   2   3  [4]  5   6   7          | 07:30  Bihu practice      |
 *   |  .               .                  | 18:30  team call          |
 *   | ...                                 |                           |
 *   |                                     | [+ add event]   remove    |
 *   | arrows move  .  pgup/pgdn month ... |                           |
 *   +-----------------------------------------------------------------+
 *
 * Events live in /CALEVT/YYYYMM.EVT (8.3 names, the FAT driver has no
 * long file names), one file per month, see Libs/Chitralekha/calevt.h.
 * Only the viewed month is in memory (one CalMonth, 4 KB).
 *
 * Repaints are minimal: a hover or selection change redraws only the
 * previous and the new cell (or row); the grid is redrawn only when the
 * month changes.
 */

#include <stdint.h>
#include <_xeneva.h>
#include <stdio.h>
#include <sys/_keproc.h>
#include <sys/_kefile.h>
#include <sys/iocodes.h>
#include <sys/_ketime.h>
#include <chitralekha.h>
#include <widgets/base.h>
#include <widgets/button.h>
#include <widgets/window.h>
#include <widgets/gmstyle.h>
#include <keycode.h>
#include <font.h>
#include <calevt.h>
#include <livetile.h>
#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------ layout */
#define WIN_W 640
#define WIN_H 420

#define GRID_X	 18
#define GRID_Y	 96 /* weekday row */
#define CELL_W	 50
#define CELL_H	 42
#define CELLS_Y	 (GRID_Y + 22)
#define GRID_W	 (CELL_W * 7)
#define GRID_H	 (22 + CELL_H * 6)

#define PANEL_X 392
#define PANEL_W 230
#define LIST_Y	108
#define ROW_H	34
#define LIST_ROWS 6
#define LIST_H	(ROW_H * LIST_ROWS)

#define HEAD_SIZE 20 /* Roboto Light, never resized */
#define HEAD_W	  (GRID_W - 100) /* month title area, left of the < > today buttons */

#define TITLE_FIELD_MAX CALEVT_TITLE_MAX

/* scan codes (virtio keyboard, set 1); extended keys arrive as 0xE0xx */
#define SC_ESC		 0x01
#define SC_BACKSPACE 0x0E
#define SC_TAB		 0x0F
#define SC_ENTER	 0x1C
#define SC_CAPS		 0x3A
#define SC_HOME		 0x47
#define SC_UP		 0x48
#define SC_PGUP		 0x49
#define SC_LEFT		 0x4B
#define SC_RIGHT	 0x4D
#define SC_DOWN		 0x50
#define SC_PGDN		 0x51
#define SC_DEL		 0x53

ChitralekhaApp* app;
ChWindow* mainWin;
static ChFont* headFont;

static CalMonth month; /* the month on screen */
static CalNow today;
static int viewYear, viewMonth;
static int selDay = 1;
static int firstCol; /* weekday of the 1st */
static int hoverCell = -1;
static int hoverRow = -1;
static int selEvent = -1; /* index into month.ev */
static int listTop;		  /* first visible event of the day (scroll) */

/* editor */
static bool editing;
static int editField; /* 0 time, 1 title */
static char editTime[5]; /* up to 4 digits */
static int editTimeLen;
static char editTitle[TITLE_FIELD_MAX];
static int editTitleLen;
static const char* editError;
static char editSuggest[6]; /* shown as the placeholder, used if no time typed */

static ChButton *btnPrev, *btnNext, *btnToday, *btnAdd, *btnRemove, *btnSave, *btnCancel;
static ChWidget gridWidget, listWidget, headWidget;

static const char* _months[13] = { "", "january", "february", "march", "april", "may", "june",
								   "july", "august", "september", "october", "november", "december" };
static const char* _days[7] = { "sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday" };
static const char* _dshort[7] = { "sun", "mon", "tue", "wed", "thu", "fri", "sat" };

/* ------------------------------------------------------------ helpers */

static void present(int x, int y, int w, int h) {
	ChWindowUpdate(mainWin, x, y, w, h, false, true);
}

static void text(const char* s, int x, int base, uint32_t col) {
	ChGmText(mainWin->canv, app->baseFont, s, x, base, col);
}

static void head(const char* s, int x, int base, uint32_t col) {
	if (!headFont) {
		text(s, x, base, col);
		return;
	}
	ChFontDrawText(mainWin->canv, headFont, (char*)s, x, base, HEAD_SIZE, col);
}

static int headWidth(const char* s) {
	if (!headFont)
		return ChGmTextWidth(app->baseFont, s);
	return (int)ChFontGetWidth(headFont, (char*)s);
}

static void itoa2(int v, char* out) {
	if (v >= 10) {
		out[0] = (char)('0' + v / 10);
		out[1] = (char)('0' + v % 10);
		out[2] = 0;
	} else {
		out[0] = (char)('0' + v);
		out[1] = 0;
	}
}

static void showButton(ChButton* b, bool show) {
	b->base.visible = show;
	if (!show) {
		b->base.hover = false;
		b->base.clicked = false;
		ChGmFill(mainWin->canv, b->base.x, b->base.y, b->base.w, b->base.h, mainWin->color);
	} else if (b->base.ChPaintHandler) {
		b->base.ChPaintHandler((ChWidget*)b, mainWin);
	}
	present(b->base.x, b->base.y, b->base.w, b->base.h);
}

/* ------------------------------------------------------------ live tile */

/*
 * tell XELnch what is next (its calendar card and LuitFlow): the first
 * event from now on, this month or the next, named the same way the
 * launcher names it. Runs after each change only.
 */
static void PublishNext() {
	static CalMonth scratch; /* 4 KB, reused; the viewed month stays intact */
	CalNow n;
	CalEvtNow(&n);
	CalEvent ev;
	int ahead = 0;
	if (CalEvtFindNext(&scratch, &n, &ev, &ahead)) {
		char when[24];
		CalEvtWhen(when, ahead, n.weekday + ahead, ev.day, ev.hour, ev.minute);
		/* the badge counts what is still ahead today */
		int first, cnt = CalEvtDayRange(&scratch, n.day, &first);
		int left = 0;
		if (ahead == 0) {
			int i = CalEvtLowerBound(&scratch, CalEvtKey(n.day, n.hour, n.minute));
			left = first + cnt - i;
		}
		XelnchLiveTileUpdate(app->postboxfd, "/calendr.exe", XELNCH_TILE_TEXT, when, ev.title, left,
							 XELNCH_NO_PROGRESS);
	} else {
		XelnchLiveTileClear(app->postboxfd, "/calendr.exe");
	}
}

/* ------------------------------------------------------------ grid */

static void cellRect(int i, int* x, int* y) {
	*x = GRID_X + (i % 7) * CELL_W;
	*y = CELLS_Y + (i / 7) * CELL_H;
}

static int dayOfCell(int i) {
	int d = i - firstCol + 1;
	return (d >= 1 && d <= CalEvtDaysIn(viewYear, viewMonth)) ? d : 0;
}

static int cellOfDay(int d) {
	return d + firstCol - 1;
}

static bool isToday(int d) {
	return d && today.year == viewYear && today.month == viewMonth && today.day == d;
}

static void PaintCell(int i, bool show) {
	if (i < 0 || i >= 42)
		return;
	ChCanvas* c = mainWin->canv;
	int x, y;
	cellRect(i, &x, &y);
	int d = dayOfCell(i);
	bool hov = (i == hoverCell) && d;
	ChGmFill(c, x, y, CELL_W, CELL_H, hov ? CH_GM_LINEN : mainWin->color);
	if (d) {
		char num[3];
		itoa2(d, num);
		bool tdy = isToday(d);
		bool sel = (d == selDay);
		uint32_t col = CH_GM_INK;
		if (tdy) {
			ChGmFill(c, x + 3, y + 3, CELL_W - 6, CELL_H - 6, CH_GM_RED);
			col = CH_GM_WHITE;
		} else if (sel) {
			ChGmFrame(c, x + 3, y + 3, CELL_W - 6, CELL_H - 6, CH_GM_INK);
		}
		if (!tdy && (i % 7) == 0)
			col = CH_GM_RED_DEEP; /* sundays */
		int tw = ChGmTextWidth(app->baseFont, num);
		text(num, x + 12, y + 20, col);
		if (sel)
			ChGmThread(c, x + 12, y + 24, tw, tdy ? CH_GM_WHITE : CH_GM_RED,
					   tdy ? 0xFFF0A0AC : CH_GM_RED_THREAD);
		/* up to three event stitches: O(log n) per cell */
		if (month.dayMask & (1u << d)) {
			int first, n = CalEvtDayRange(&month, d, &first);
			if (n > 3)
				n = 3;
			for (int k = 0; k < n; k++)
				ChGmLozenge(c, x + 14 + k * 8, y + CELL_H - 10, 2, tdy ? CH_GM_WHITE : CH_GM_MUGA_DEEP);
		}
	}
	if (show)
		present(x, y, CELL_W, CELL_H);
}

static void GridPaint(ChWidget* w, ChWindow* win) {
	(void)w;
	ChCanvas* c = win->canv;
	ChGmFill(c, GRID_X, GRID_Y, GRID_W, 22, win->color);
	for (int i = 0; i < 7; i++)
		text(_dshort[i], GRID_X + i * CELL_W + 12, GRID_Y + 14, i == 0 ? CH_GM_RED_DEEP : CH_GM_INK_SOFT);
	ChGmFill(c, GRID_X, GRID_Y + 20, GRID_W, 1, CH_GM_HAIRLINE);
	for (int i = 0; i < 42; i++)
		PaintCell(i, false);
}

static void HeadPaint(ChWidget* w, ChWindow* win) {
	(void)w;
	ChCanvas* c = win->canv;
	/* month title */
	ChGmFill(c, GRID_X, 34, HEAD_W, 52, win->color);
	head(_months[viewMonth], GRID_X, 72, CH_GM_INK);
	char yr[8];
	snprintf(yr, sizeof(yr), "%d", viewYear);
	text(yr, GRID_X + headWidth(_months[viewMonth]) + 10, 72, CH_GM_INK_SOFT);
	/* lozenge chain under it, the one subtle woven touch */
	for (int x = GRID_X + 4; x < GRID_X + GRID_W; x += 14)
		ChGmLozenge(c, x, 84, 1, CH_GM_RED_THREAD);
	/* footer help */
	ChGmFill(c, GRID_X, WIN_H - 30, GRID_W, 20, win->color);
	text("arrows move   pgup/pgdn month   n new   del remove", GRID_X, WIN_H - 16, CH_GM_INK_SOFT);
}

static void RepaintGrid() {
	GridPaint(&gridWidget, mainWin);
	present(GRID_X, GRID_Y, GRID_W, GRID_H);
}

static void RepaintHead() {
	HeadPaint(&headWidget, mainWin);
	present(GRID_X, 34, HEAD_W, 52);
}

/* ------------------------------------------------------------ panel */

static void PaintListRow(int r, bool show);

static void PanelHeader() {
	ChCanvas* c = mainWin->canv;
	ChGmFill(c, PANEL_X, 34, PANEL_W, LIST_Y - 34, mainWin->color);
	int wd = CalEvtWeekday(viewYear, viewMonth, selDay);
	head(_days[wd], PANEL_X, 72, CH_GM_INK);
	char sub[32];
	snprintf(sub, sizeof(sub), "%d %s %d", selDay, _months[viewMonth], viewYear);
	text(sub, PANEL_X + 1, 92, isToday(selDay) ? CH_GM_RED : CH_GM_INK_SOFT);
}

static int dayFirst(int* count) {
	int first;
	*count = CalEvtDayRange(&month, selDay, &first);
	return first;
}

static void PaintListRow(int r, bool show) {
	ChCanvas* c = mainWin->canv;
	int y = LIST_Y + r * ROW_H;
	int n, first = dayFirst(&n);
	int idx = listTop + r;
	bool has = idx < n;
	int ev = first + idx;
	bool sel = has && ev == selEvent;
	bool hov = has && r == hoverRow;
	ChGmFill(c, PANEL_X - 8, y, PANEL_W + 8, ROW_H, sel ? CH_GM_LINEN : (hov ? 0xFFF8F2EA : mainWin->color));
	if (has) {
		CalEvent* e = &month.ev[ev];
		if (sel)
			ChGmFill(c, PANEL_X - 8, y + 4, 3, ROW_H - 8, CH_GM_RED);
		char t[6];
		CalEvtFormatTime(t, e->hour, e->minute);
		text(t, PANEL_X, y + 22, CH_GM_RED);
		ChRect clip = { PANEL_X + 46, y, PANEL_W - 50, ROW_H };
		ChGmTextClipped(c, app->baseFont, e->title, PANEL_X + 46, y + 22, CH_GM_INK, &clip);
		if (idx + 1 < n && r + 1 < LIST_ROWS)
			ChGmFill(c, PANEL_X + 46, y + ROW_H - 1, PANEL_W - 46, 1, CH_GM_HAIRLINE);
	} else if (r == 0 && n == 0) {
		text("nothing planned", PANEL_X, y + 22, CH_GM_INK_SOFT);
	}
	if (r == LIST_ROWS - 1 && listTop + LIST_ROWS < n)
		text("more below", PANEL_X + PANEL_W - 64, y + 30, CH_GM_INK_SOFT);
	if (show)
		present(PANEL_X - 8, y, PANEL_W + 8, ROW_H);
}

static void field(int x, int y, int w, const char* s, const char* placeholder, bool focus) {
	ChCanvas* c = mainWin->canv;
	ChGmFill(c, x, y, w, 28, CH_GM_PAPER);
	ChGmFrame(c, x, y, w, 28, focus ? CH_GM_INK : CH_GM_EDGE);
	bool empty = !s[0];
	ChRect clip = { x + 4, y, w - 8, 28 };
	ChGmTextClipped(c, app->baseFont, empty ? placeholder : s, x + 8, y + 19,
					empty ? CH_GM_SAND : CH_GM_INK, &clip);
	if (focus) {
		int cx = x + 8 + (empty ? 0 : ChGmTextWidth(app->baseFont, s)) + 1;
		if (cx < x + w - 4)
			ChGmFill(c, cx, y + 6, 1, 16, CH_GM_RED);
		ChGmFill(c, x, y + 26, w, 2, CH_GM_RED);
	}
}

static void timeDisplay(char* out) {
	/* "1830" -> "18:30", "7" -> "7" */
	int j = 0;
	for (int i = 0; i < editTimeLen; i++) {
		if (i == 2)
			out[j++] = ':';
		out[j++] = editTime[i];
	}
	out[j] = 0;
}

static void PaintEditor() {
	ChCanvas* c = mainWin->canv;
	ChGmFill(c, PANEL_X - 8, LIST_Y, PANEL_W + 8, LIST_H, mainWin->color);
	text("time", PANEL_X, LIST_Y + 14, CH_GM_INK_SOFT);
	char t[8];
	timeDisplay(t);
	field(PANEL_X, LIST_Y + 20, 80, t, editSuggest, editField == 0);
	text("what", PANEL_X, LIST_Y + 70, CH_GM_INK_SOFT);
	field(PANEL_X, LIST_Y + 76, PANEL_W - 8, editTitle, "a name for it", editField == 1);
	if (editError)
		text(editError, PANEL_X, LIST_Y + 128, CH_GM_RED);
	text("tab switches   enter saves   esc cancels", PANEL_X, LIST_Y + 152, CH_GM_INK_SOFT);
	present(PANEL_X - 8, LIST_Y, PANEL_W + 8, LIST_H);
}

static void ListPaint(ChWidget* w, ChWindow* win) {
	(void)w;
	(void)win;
	PanelHeader();
	if (editing) {
		PaintEditor();
		return;
	}
	for (int r = 0; r < LIST_ROWS; r++)
		PaintListRow(r, false);
}

static void RepaintPanel() {
	ListPaint(&listWidget, mainWin);
	present(PANEL_X - 8, 34, PANEL_W + 8, LIST_Y - 34 + LIST_H);
	if (!editing)
		showButton(btnRemove, selEvent >= 0);
}

/* ------------------------------------------------------------ state */

static void LoadMonth(int y, int m) {
	if (month.dirty)
		CalEvtSave(&month);
	viewYear = y;
	viewMonth = m;
	CalEvtLoad(&month, y, m);
	firstCol = CalEvtWeekday(y, m, 1);
}

static void SelectDay(int d) {
	int dim = CalEvtDaysIn(viewYear, viewMonth);
	if (d < 1 || d > dim) {
		/* step into the neighbouring month */
		int y = viewYear, m = viewMonth;
		if (d < 1) {
			if (--m < 1) {
				m = 12;
				y--;
			}
			d += CalEvtDaysIn(y, m);
		} else {
			d -= dim;
			if (++m > 12) {
				m = 1;
				y++;
			}
		}
		LoadMonth(y, m);
		selDay = d;
		selEvent = -1;
		listTop = 0;
		hoverCell = -1;
		RepaintHead();
		RepaintGrid();
		RepaintPanel();
		return;
	}
	if (d == selDay)
		return;
	int old = selDay;
	selDay = d;
	selEvent = -1;
	listTop = 0;
	/* only the two cells that changed */
	PaintCell(cellOfDay(old), true);
	PaintCell(cellOfDay(d), true);
	RepaintPanel();
}

static void ShiftMonth(int delta) {
	int y = viewYear, m = viewMonth + delta;
	while (m < 1) {
		m += 12;
		y--;
	}
	while (m > 12) {
		m -= 12;
		y++;
	}
	LoadMonth(y, m);
	int dim = CalEvtDaysIn(y, m);
	if (selDay > dim)
		selDay = dim;
	selEvent = -1;
	listTop = 0;
	hoverCell = -1;
	RepaintHead();
	RepaintGrid();
	RepaintPanel();
}

static void GoToday() {
	CalEvtNow(&today);
	if (today.year != viewYear || today.month != viewMonth) {
		LoadMonth(today.year, today.month);
		selDay = today.day;
		selEvent = -1;
		listTop = 0;
		RepaintHead();
		RepaintGrid();
		RepaintPanel();
	} else {
		SelectDay(today.day);
	}
}

static void OpenEditor() {
	if (editing)
		return;
	editing = true;
	editField = 0;
	editTimeLen = 0;
	editTitleLen = 0;
	editTitle[0] = 0;
	editError = NULL;
	/* suggest the next full hour today, 09:00 on other days */
	int h = 9;
	if (isToday(selDay))
		h = today.hour + 1 < 24 ? today.hour + 1 : 23;
	CalEvtFormatTime(editSuggest, h, 0);
	showButton(btnAdd, false);
	showButton(btnRemove, false);
	showButton(btnSave, true);
	showButton(btnCancel, true);
	RepaintPanel();
}

static void CloseEditor() {
	editing = false;
	showButton(btnSave, false);
	showButton(btnCancel, false);
	showButton(btnAdd, true);
	RepaintPanel();
}

static void SaveEditor() {
	if (editTimeLen == 0) { /* take the suggested time */
		editTime[0] = editSuggest[0];
		editTime[1] = editSuggest[1];
		editTime[2] = editSuggest[3];
		editTime[3] = editSuggest[4];
		editTimeLen = 4;
	}
	if (editTimeLen < 3) {
		editError = "time like 07:30";
		PaintEditor();
		return;
	}
	int hh, mm;
	if (editTimeLen == 3) { /* "730" */
		hh = editTime[0] - '0';
		mm = (editTime[1] - '0') * 10 + (editTime[2] - '0');
	} else {
		hh = (editTime[0] - '0') * 10 + (editTime[1] - '0');
		mm = (editTime[2] - '0') * 10 + (editTime[3] - '0');
	}
	if (hh > 23 || mm > 59) {
		editError = "time like 07:30";
		PaintEditor();
		return;
	}
	if (editTitleLen == 0) {
		editError = "give it a name";
		editField = 1;
		PaintEditor();
		return;
	}
	CalEvent e;
	memset(&e, 0, sizeof(e));
	e.day = (uint8_t)selDay;
	e.hour = (uint8_t)hh;
	e.minute = (uint8_t)mm;
	memcpy(e.title, editTitle, editTitleLen);
	int at = CalEvtInsert(&month, &e);
	if (at < 0) {
		editError = "this month is full";
		PaintEditor();
		return;
	}
	CalEvtSave(&month);
	selEvent = at;
	int n, first = dayFirst(&n);
	if (at - first >= LIST_ROWS)
		listTop = at - first - LIST_ROWS + 1;
	PaintCell(cellOfDay(selDay), true);
	CloseEditor();
	PublishNext();
}

static void RemoveSelected() {
	if (selEvent < 0 || editing)
		return;
	CalEvtRemove(&month, selEvent);
	CalEvtSave(&month);
	selEvent = -1;
	int n;
	dayFirst(&n);
	if (listTop > 0 && listTop + LIST_ROWS > n)
		listTop = n > LIST_ROWS ? n - LIST_ROWS : 0;
	PaintCell(cellOfDay(selDay), true);
	RepaintPanel();
	PublishNext();
}

/* ------------------------------------------------------------ widgets */

static int localX(int sx) {
	return sx - mainWin->info->x;
}

static int localY(int sy) {
	return sy - mainWin->info->y;
}

static void GridMouse(ChWidget* w, ChWindow* win, int x, int y, int button) {
	(void)w;
	(void)win;
	int lx = localX(x) - GRID_X, ly = localY(y) - CELLS_Y;
	int cell = -1;
	if (lx >= 0 && ly >= 0 && lx < GRID_W && ly < CELL_H * 6)
		cell = (ly / CELL_H) * 7 + lx / CELL_W; /* O(1) */
	if (cell >= 0 && !dayOfCell(cell))
		cell = -1;
	if (cell != hoverCell) {
		int old = hoverCell;
		hoverCell = cell;
		PaintCell(old, true);
		PaintCell(cell, true);
	}
	if (button && cell >= 0 && !editing)
		SelectDay(dayOfCell(cell));
}

static void ListMouse(ChWidget* w, ChWindow* win, int x, int y, int button) {
	(void)w;
	(void)win;
	if (editing)
		return;
	if (button == DEODHAI_MOUSE_MSG_SCROLL_UP || button == DEODHAI_MOUSE_MSG_SCROLL_DOWN) {
		int n;
		dayFirst(&n);
		int top = listTop + (button == DEODHAI_MOUSE_MSG_SCROLL_DOWN ? 1 : -1);
		if (top > n - LIST_ROWS)
			top = n - LIST_ROWS;
		if (top < 0)
			top = 0;
		if (top != listTop) {
			listTop = top;
			hoverRow = -1;
			RepaintPanel();
		}
		return;
	}
	int lx = localX(x), ly = localY(y) - LIST_Y;
	int row = -1;
	if (lx >= PANEL_X - 8 && lx < PANEL_X + PANEL_W && ly >= 0 && ly < LIST_H)
		row = ly / ROW_H;
	int n, first = dayFirst(&n);
	if (row >= 0 && listTop + row >= n)
		row = -1;
	if (row != hoverRow) {
		int old = hoverRow;
		hoverRow = row;
		if (old >= 0)
			PaintListRow(old, true);
		if (row >= 0)
			PaintListRow(row, true);
	}
	if (button && row >= 0) {
		int ev = first + listTop + row;
		if (ev != selEvent) {
			int oldRow = selEvent - first - listTop;
			selEvent = ev;
			if (oldRow >= 0 && oldRow < LIST_ROWS)
				PaintListRow(oldRow, true);
			PaintListRow(row, true);
			showButton(btnRemove, true);
		}
	}
}

static void initWidget(ChWidget* w, int x, int y, int ww, int hh,
					   void (*paint)(ChWidget*, ChWindow*),
					   void (*mouse)(ChWidget*, ChWindow*, int, int, int)) {
	memset(w, 0, sizeof(ChWidget));
	w->x = x;
	w->y = y;
	w->w = ww;
	w->h = hh;
	w->ChPaintHandler = paint;
	w->ChMouseEvent = mouse;
	ChWindowAddWidget(mainWin, w);
}

static void OnPrev(ChWidget* w, ChWindow* win) {
	(void)w;
	(void)win;
	if (!editing)
		ShiftMonth(-1);
}
static void OnNext(ChWidget* w, ChWindow* win) {
	(void)w;
	(void)win;
	if (!editing)
		ShiftMonth(1);
}
static void OnToday(ChWidget* w, ChWindow* win) {
	(void)w;
	(void)win;
	if (!editing)
		GoToday();
}
static void OnAdd(ChWidget* w, ChWindow* win) {
	(void)w;
	(void)win;
	OpenEditor();
}
static void OnRemove(ChWidget* w, ChWindow* win) {
	(void)w;
	(void)win;
	RemoveSelected();
}
static void OnSave(ChWidget* w, ChWindow* win) {
	(void)w;
	(void)win;
	if (editing)
		SaveEditor();
}
static void OnCancel(ChWidget* w, ChWindow* win) {
	(void)w;
	(void)win;
	if (editing)
		CloseEditor();
}

/* button coordinates are relative to the client area (the title bar
 * padding is added by ChCreateButton) */
static ChButton* mkButton(int x, int y, int w, int h, const char* label, uint16_t flags,
						  void (*act)(ChWidget*, ChWindow*)) {
	ChButton* b = ChCreateButtonEx(x - CHITRALEKHA_WINDOW_DEFAULT_PAD_X, y - CHITRALEKHA_WINDOW_DEFAULT_PAD_Y,
								   w, h, (char*)label, (uint16_t)(CH_BUTTON_FLAT | flags));
	b->base.ChActionHandler = act;
	ChWindowAddWidget(mainWin, (ChWidget*)b);
	return b;
}

/* ------------------------------------------------------------ keys */

static void EditorKey(int sc, char ch) {
	if (sc == SC_ESC) {
		CloseEditor();
		return;
	}
	if (sc == SC_ENTER) {
		SaveEditor();
		return;
	}
	if (sc == SC_TAB) {
		editField ^= 1;
		PaintEditor();
		return;
	}
	if (sc == SC_BACKSPACE) {
		if (editField == 0 && editTimeLen > 0)
			editTimeLen--;
		else if (editField == 1 && editTitleLen > 0)
			editTitle[--editTitleLen] = 0;
		editError = NULL;
		PaintEditor();
		return;
	}
	if (editField == 0) {
		if (ch >= '0' && ch <= '9' && editTimeLen < 4) {
			editTime[editTimeLen++] = ch;
			editError = NULL;
			/* four digits typed: move on to the name */
			if (editTimeLen == 4)
				editField = 1;
			PaintEditor();
		}
		return;
	}
	if (ch >= 32 && ch < 127 && editTitleLen < TITLE_FIELD_MAX - 1) {
		editTitle[editTitleLen++] = ch;
		editTitle[editTitleLen] = 0;
		editError = NULL;
		PaintEditor();
	}
}

static void HandleKey(int code) {
	ChitralekhaProcessKey(code); /* shift / caps state */
	bool ext = (code & 0xFF00) == 0xE000;
	if (code & 0x80)
		return; /* releases */
	int sc = code & 0x7F;
	if (ext) {
		if (editing)
			return;
		switch (sc) {
		case SC_LEFT:
			SelectDay(selDay - 1);
			break;
		case SC_RIGHT:
			SelectDay(selDay + 1);
			break;
		case SC_UP:
			SelectDay(selDay - 7);
			break;
		case SC_DOWN:
			SelectDay(selDay + 7);
			break;
		case SC_PGUP:
			ShiftMonth(-1);
			break;
		case SC_PGDN:
			ShiftMonth(1);
			break;
		case SC_HOME:
			GoToday();
			break;
		case SC_DEL:
			RemoveSelected();
			break;
		}
		return;
	}
	char ch = ChitralekhaKeyToASCII(code);
	if (editing) {
		EditorKey(sc, ch);
		return;
	}
	if (sc == SC_ENTER || ch == 'n' || ch == 'N')
		OpenEditor();
	else if (ch == 't' || ch == 'T')
		GoToday();
}

/* ------------------------------------------------------------ loop */

static void WindowHandleMessage(PostEvent* e) {
	switch (e->type) {
	case DEODHAI_REPLY_MOUSE_EVENT:
		ChWindowHandleMouse(mainWin, e->dword, e->dword2, e->dword3);
		break;
	case DEODHAI_REPLY_MOUSE_LEAVE:
		ChWindowHandleMouseLeave(mainWin);
		break;
	case DEODHAI_REPLY_KEY_EVENT:
		HandleKey((int)e->dword);
		break;
	case DEODHAI_REPLY_FOCUS_CHANGED: {
		ChWindow* w = ChGetWindowByHandle(mainWin, e->dword2);
		if (w)
			ChWindowHandleFocus(w, e->dword, e->dword2);
		break;
	}
	}
	memset(e, 0, sizeof(PostEvent));
}

int main(int argc, char* argv[]) {
	app = ChitralekhaStartApp(argc, argv);
	mainWin = ChCreateWindow(app, WINDOW_FLAG_MOVABLE | WINDOW_FLAG_STYLE_KNOT, (char*)"calendar", 100, 50,
							 WIN_W, WIN_H);
	mainWin->color = CH_GM_PAPER;

	/* one extra face at one fixed size: no atlas re-bakes later */
	headFont = ChInitialiseFont((char*)ROBOTO_LIGHT);
	if (headFont)
		ChFontSetSize(headFont, HEAD_SIZE);

	CalEvtNow(&today);
	LoadMonth(today.year, today.month);
	selDay = today.day;

	initWidget(&headWidget, GRID_X, 34, HEAD_W, 52, HeadPaint, NULL);
	initWidget(&gridWidget, GRID_X, GRID_Y, GRID_W, GRID_H, GridPaint, GridMouse);
	initWidget(&listWidget, PANEL_X - 8, LIST_Y, PANEL_W + 8, LIST_H, ListPaint, ListMouse);

	btnPrev = mkButton(GRID_X + GRID_W - 96, 54, 22, 24, "<", CH_BUTTON_FLAT_QUIET, OnPrev);
	btnNext = mkButton(GRID_X + GRID_W - 70, 54, 22, 24, ">", CH_BUTTON_FLAT_QUIET, OnNext);
	btnToday = mkButton(GRID_X + GRID_W - 40, 54, 40, 24, "today", CH_BUTTON_FLAT_QUIET, OnToday);
	btnAdd = mkButton(PANEL_X, LIST_Y + LIST_H + 16, 120, 30, "add event", CH_BUTTON_FLAT_LOZENGE, OnAdd);
	btnRemove = mkButton(PANEL_X + 140, LIST_Y + LIST_H + 19, 60, 24, "remove", CH_BUTTON_FLAT_QUIET, OnRemove);
	btnSave = mkButton(PANEL_X, LIST_Y + LIST_H + 16, 120, 30, "save", 0, OnSave);
	btnCancel = mkButton(PANEL_X + 140, LIST_Y + LIST_H + 19, 60, 24, "cancel", CH_BUTTON_FLAT_QUIET, OnCancel);
	btnRemove->base.visible = false;
	btnSave->base.visible = false;
	btnCancel->base.visible = false;

	ChWindowPaint(mainWin);
	/* the divider between month and day */
	ChGmFill(mainWin->canv, PANEL_X - 18, 40, 1, WIN_H - 60, CH_GM_HAIRLINE);
	present(PANEL_X - 18, 40, 1, WIN_H - 60);

	ChWindowBroadcastIcon(app, (char*)"/icons/calndr.bmp");
	PublishNext();

	PostEvent e;
	memset(&e, 0, sizeof(PostEvent));
	while (1) {
		int err = _KeFileIoControl(app->postboxfd, POSTBOX_GET_EVENT, &e);
		WindowHandleMessage(&e);
		if (err == POSTBOX_NO_EVENT)
			_KePauseThread();
	}
}
