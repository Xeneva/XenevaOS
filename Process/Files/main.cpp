/**
* BSD 2-Clause License
*
* Copyright (c) 2024-2026, Manas Kamal Choudhury
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

/**
 * Files -- the XenevaOS file manager
 *
 * Widget tree. Every rectangle below is computed by a Chitralekha
 * BorderLayout; nothing in this file hard-codes a widget position:
 *
 *   rootLayout                     client area (window minus the titlebar)
 *   |- TOP     ChMenubar
 *   `- CENTER  mainLayout
 *              |- TOP     FilesToolbar   sidebar toggle | back fwd | up | breadcrumb | refresh | search
 *              |- BOTTOM  FilesStatusBar item counts / selection / sort order
 *              `- CENTER  bodyLayout     (1px hgap = hairline divider)
 *                         |- LEFT    ChSidebar  Favourites + Locations (partitions)
 *                         `- CENTER  ListView   Name | Date Modified | Size | Kind
 *
 * The layouts are pure geometry managers. They are NOT added to the window's
 * widget list; the leaf widgets are. That keeps the window's own hover/leave
 * dispatch (ChWindowHandleMouse) working for every leaf, which matters for the
 * Sidebar: it only clears its hover row when it receives the "leave" event.
 *
 * Memory policy
 *  - one directory cache (FilesEntry, 44 bytes/entry) reused across folders,
 *    grown by doubling and trimmed when a much smaller folder is opened
 *  - one int block holding the sort order, the filtered view and the merge
 *    sort scratch area (no allocation per sort / per keystroke)
 *  - ListView item storage is reserved up front and trimmed the same way
 *  - paths and history are fixed buffers: navigation never touches the heap
 *  - everything the app allocates is released in FilesOnClose / ChDestroy
 *
 * Complexity
 *  - sort: stable bottom-up merge sort, O(n log n) worst case, O(n) when the
 *    order is already sorted, no recursion (the XEClib qsort is a Lomuto
 *    quicksort: O(n^2) and n-deep recursion on sorted input)
 *  - type-to-select: O(log n) binary search while sorted by name
 *  - relayout on resize: O(1); repaint only touches dirty regions
 */

#include <stdint.h>
#include <_xeneva.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/_keproc.h>
#include <sys/_kefile.h>
#include <sys/iocodes.h>
#include <sys/mman.h>
#include <chitralekha.h>
#include <keycode.h>
#include <widgets/base.h>
#include <widgets/window.h>
#include <widgets/menubar.h>
#include <widgets/menu.h>
#include <widgets/msgbox.h>
#include <widgets/listview.h>
#include <widgets/sidebar.h>
#include <widgets/icon.h>
#include <widgets/layout/border_layout.h>

/* ------------------------------------------------------------------------ */
/*  Geometry                                                                */
/* ------------------------------------------------------------------------ */

#define FILES_WIN_X 80
#define FILES_WIN_Y 60
#define FILES_WIN_W 900
#define FILES_WIN_H 560

#define FILES_TITLEBAR_H		 26
#define FILES_MENUBAR_H			 26
#define FILES_TOOLBAR_H			 44
#define FILES_STATUS_H			 24
#define FILES_SIDEBAR_W			 200
#define FILES_SIDEBAR_W_COMPACT	 150
#define FILES_SIDEBAR_BREAKPOINT 680 /* window width below which the sidebar narrows */
#define FILES_SIDEBAR_AUTOHIDE	 440 /* ...and below which it folds away */
#define FILES_SEARCH_BREAKPOINT	 560 /* window width below which the search box hides */
#define FILES_ROW_H				 26
#define FILES_SCROLLBAR_W		 16
#define FILES_ICON_SIZE			 20

#define FILES_COL_NAME_MIN 180
#define FILES_COL_DATE_W   168
#define FILES_COL_SIZE_W   88
#define FILES_COL_KIND_W   128

/* ------------------------------------------------------------------------ */
/*  Limits                                                                  */
/* ------------------------------------------------------------------------ */

#define FILES_PATH_MAX			  256
#define FILES_NAME_MAX			  32 /* == sizeof(XEDirectoryEntry::filename) */
#define FILES_HISTORY_MAX		  32
#define FILES_MAX_ENTRIES		  4096
#define FILES_MAX_LOCATIONS		  16
#define FILES_MAX_CRUMBS		  24
#define FILES_SEARCH_MAX		  40
#define FILES_TYPEAHEAD_MAX		  24
#define FILES_TYPEAHEAD_GAP_TICKS 900

/* ------------------------------------------------------------------------ */
/*  Palette -- dark, built around the ListView's fixed colours              */
/* ------------------------------------------------------------------------ */

#define COL_DIVIDER		   0xFF151517
#define COL_TOOLBAR_BG	   0xFF2B2B2E
#define COL_TOOLBAR_EDGE   0xFF19191B
#define COL_BTN_HOVER	   0xFF3B3B40
#define COL_BTN_PRESSED	   0xFF4A4A51
#define COL_GLYPH		   0xFFD6D6DB
#define COL_GLYPH_DISABLED 0xFF5C5C63
#define COL_FIELD_BG	   0xFF1C1C1F
#define COL_FIELD_BORDER   0xFF404046
#define COL_FOCUS		   0xFF4C8BF5
#define COL_TEXT		   0xFFE6E6EA
#define COL_TEXT_DIM	   0xFF9A9AA2
#define COL_TEXT_FAINT	   0xFF6E6E76
#define COL_CRUMB_HOVER	   0xFF333338
#define COL_WARNING		   0xFFF0B455
#define COL_SIDEBAR_BG	   0xFF242427
#define COL_SIDEBAR_HOVER  0xFF323237
#define COL_SIDEBAR_SELECT 0xFF3A6EA5 /* == ListView selection */
#define COL_SIDEBAR_HEADER 0xFF85858D
#define COL_STATUS_BG	   0xFF2B2B2E
#define COL_LV_BG		   0xFF1E1E1E /* ListView LV_COLOR_BG */
#define COL_LV_HEADER	   0xFF2D2D2D /* ListView LV_COLOR_HEADER_BG */

/* ------------------------------------------------------------------------ */
/*  XT set-1 scancodes (matched on scancode, like Terminal does, because     */
/*  ChitralekhaKeyToASCII returns a char and truncates the arrow keycodes)   */
/* ------------------------------------------------------------------------ */

#define SC_RELEASE	 0x80
#define SC_MASK		 0x7F
#define SC_EXT_MASK	 0xFF00
#define SC_EXT_FLAG	 0xE000
#define SC_ESC		 0x01
#define SC_BACKSPACE 0x0E
#define SC_TAB		 0x0F
#define SC_ENTER	 0x1C
#define SC_CTRL		 0x1D
#define SC_LSHIFT	 0x2A
#define SC_RSHIFT	 0x36
#define SC_ALT		 0x38
#define SC_CAPSLOCK	 0x3A
#define SC_F5		 0x3F
#define SC_HOME		 0x47
#define SC_UP		 0x48
#define SC_PGUP		 0x49
#define SC_LEFT		 0x4B
#define SC_RIGHT	 0x4D
#define SC_END		 0x4F
#define SC_DOWN		 0x50
#define SC_PGDN		 0x51

/* An alignment value that is none of the five BorderLayout regions: the
 * layout simply skips such an item. Used to take the sidebar out of the
 * layout without destroying it (BorderLayout has no "remove"). It must stay
 * inside the enum's value range (0..7 for enumerators 0..4) -- storing a
 * larger value in a C++ enum without a fixed underlying type is UB. */
#define FILES_ALIGN_DETACHED ((ChBorderAlignment)7)

/* ------------------------------------------------------------------------ */
/*  Types                                                                   */
/* ------------------------------------------------------------------------ */

typedef void (*FilesPaintFn)(ChWidget* wid, ChWindow* win);
typedef void (*FilesMouseFn)(ChWidget* wid, ChWindow* win, int x, int y, int button);
typedef int (*FilesOpenFn)(const char* fullpath);

enum FilesKind {
	KIND_FOLDER = 0,
	KIND_APP,
	KIND_LIBRARY,
	KIND_IMAGE,
	KIND_AUDIO,
	KIND_VIDEO,
	KIND_TEXT,
	KIND_FONT,
	KIND_DOCUMENT,
	KIND_COUNT
};

enum FilesIconId {
	ICON_FOLDER = 0,
	ICON_DOCUMENT,
	ICON_APP,
	ICON_IMAGE,
	ICON_LIBRARY,
	ICON_MUSIC,
	ICON_DRIVE,
	ICON_COUNT
};

enum FilesFocus { FOCUS_LIST = 0, FOCUS_PATH, FOCUS_SEARCH };

enum FilesSortKey { SORT_NAME = 0, SORT_DATE, SORT_SIZE, SORT_KIND, SORT_COUNT };

enum FilesToolbarElement {
	TB_NONE = -1,
	TB_SIDEBAR = 0,
	TB_BACK,
	TB_FORWARD,
	TB_UP,
	TB_REFRESH,
	TB_PATH,
	TB_SEARCH,
	TB_SEARCH_CLEAR,
	TB_COUNT
};

enum FilesGlyphDir { DIR_LEFT = 0, DIR_RIGHT, DIR_UP, DIR_DOWN };

typedef struct _files_ext_info_ {
	const char* ext;
	uint8_t kind;
	FilesOpenFn open;
} FilesExtInfo;

/* one cached directory entry: 44 bytes (the old FileEntryItem was 177) */
typedef struct _files_entry_ {
	char name[FILES_NAME_MAX];
	uint32_t size;
	uint32_t stamp; /* (FAT date << 16) | FAT time -- orders chronologically */
	uint8_t kind;
	uint8_t isDir;
	uint8_t ext;
	uint8_t reserved;
} FilesEntry;

typedef struct _files_location_ {
	char label[40];
	char path[40];
	uint8_t mounted;
	uint8_t volume; /* a disk / partition (Locations), not a Favourite */
	int8_t section; /* sidebar coordinates of this location's row */
	int8_t item;
} FilesLocation;

typedef struct _files_crumb_ {
	int16_t x0, x1; /* window-relative x range */
	int16_t end;	/* length of the path prefix it stands for, -1 = "..." */
} FilesCrumb;

typedef struct _files_toolbar_ {
	ChWidget base;
	ChRect r[TB_COUNT];
	int hot;
	int pressed;
	int hotCrumb;
	bool prevDown;
	int crumbCount;
	FilesCrumb crumbs[FILES_MAX_CRUMBS];
} FilesToolbar;

typedef struct _files_statusbar_ {
	ChWidget base;
} FilesStatusBar;

typedef struct _files_app_ {
	ChitralekhaApp* app;
	ChWindow* win;

	ChMenubar* menubar;
	FilesToolbar* toolbar;
	FilesStatusBar* status;
	ChSidebar* sidebar;
	ListView* list;

	ChBorderLayout* rootLayout;
	ChBorderLayout* mainLayout;
	ChBorderLayout* bodyLayout;

	/* original handlers of the library widgets we wrap */
	FilesPaintFn sidebarPaint;
	FilesMouseFn sidebarMouse;
	FilesPaintFn listPaint;
	FilesMouseFn listMouse;

	char path[FILES_PATH_MAX];

	/* browser style history kept in a ring: O(1) push, back, forward */
	char history[FILES_HISTORY_MAX][FILES_PATH_MAX];
	int histStart;
	int histCount;
	int histPos;

	/* directory cache */
	FilesEntry* entries;
	int entryCount;
	int entryCap;
	int dirCount;

	/* one block: order[cap] | view[cap] | scratch[cap] */
	int* order;
	int* view;
	int* scratch;
	int indexCap;
	int viewCount;
	int viewDirs;

	int sortKey;
	bool sortDesc;

	char search[FILES_SEARCH_MAX];
	int searchLen;
	char edit[FILES_PATH_MAX];
	int editLen;
	int focus;

	char typeBuf[FILES_TYPEAHEAD_MAX];
	int typeLen;
	size_t typeTick;

	char notice[96];

	FilesLocation loc[FILES_MAX_LOCATIONS];
	int locCount;
	int activeLoc;

	ChIcon* icons[ICON_COUNT];

	bool sidebarVisible;
	bool ready;	   /* first full paint done -- partial repaints allowed */
	bool resizing; /* back buffer unmapped by the compositor: no painting */
	bool closing;
	bool listDown;
} FilesApp;

static FilesApp g;

/* ------------------------------------------------------------------------ */
/*  Forward declarations                                                    */
/* ------------------------------------------------------------------------ */

static bool FilesNavigate(const char* target, bool remember, const char* selectName);
static void FilesRepaintList(void);
static void FilesRepaintStatus(void);
static void FilesRepaintSidebar(void);
static void FilesRepaintToolbar(void);
static void FilesRepaintToolbarElement(int id);
static void FilesSetFocus(int focus);
static void FilesSelectRow(int row, bool reveal, bool repaint);
static void FilesRelayout(void);
static void FilesToolbarLayout(FilesToolbar* tb);

/* ------------------------------------------------------------------------ */
/*  Small string helpers                                                    */
/* ------------------------------------------------------------------------ */

static inline int FilesLower(int c) {
	return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

static void FilesStrCopy(char* dst, const char* src, size_t cap) {
	if (!dst || cap == 0)
		return;
	size_t i = 0;
	if (src) {
		for (; i + 1 < cap && src[i]; i++)
			dst[i] = src[i];
	}
	dst[i] = '\0';
}

static int FilesStrCaseCmp(const char* a, const char* b) {
	while (*a && FilesLower((unsigned char)*a) == FilesLower((unsigned char)*b)) {
		a++;
		b++;
	}
	return FilesLower((unsigned char)*a) - FilesLower((unsigned char)*b);
}

/* compares the first n characters of name against prefix; a name that ends
 * early sorts first. Monotonic with FilesStrCaseCmp, so binary search works */
static int FilesPrefixCmp(const char* name, const char* prefix, int n) {
	for (int i = 0; i < n; i++) {
		int a = FilesLower((unsigned char)name[i]);
		int b = FilesLower((unsigned char)prefix[i]);
		if (a != b)
			return a - b;
		if (!a)
			return 0;
	}
	return 0;
}

static bool FilesContainsCase(const char* hay, const char* needle, int n) {
	if (n <= 0)
		return true;
	for (; *hay; hay++) {
		int i = 0;
		while (i < n && hay[i] &&
			   FilesLower((unsigned char)hay[i]) == FilesLower((unsigned char)needle[i]))
			i++;
		if (i == n)
			return true;
	}
	return false;
}

/* is `path` equal to, or inside, `prefix`? (case-insensitive: FAT) */
static bool FilesPathHasPrefix(const char* path, const char* prefix) {
	if (prefix[0] == '/' && prefix[1] == '\0')
		return path[0] == '/';
	size_t n = strlen(prefix);
	if (FilesPrefixCmp(path, prefix, (int)n) != 0 || strlen(path) < n)
		return false;
	return path[n] == '\0' || path[n] == '/';
}

/*
 * FilesPathResolve -- resolve `sub` against `base` into a clean absolute
 * path ("/a/b"). Collapses "//", "." and "..". O(len), no allocation.
 */
static bool FilesPathResolve(const char* base, const char* sub, char* out, size_t cap) {
	char tmp[FILES_PATH_MAX * 2];
	if (!out || cap < 2)
		return false;
	if (!base || !base[0])
		base = "/";
	if (sub && sub[0] == '/')
		FilesStrCopy(tmp, sub, sizeof(tmp));
	else
		snprintf(tmp, sizeof(tmp), "%s/%s", base, sub ? sub : "");

	size_t w = 0;
	out[w++] = '/';
	const char* p = tmp;
	while (*p) {
		while (*p == '/')
			p++;
		if (!*p)
			break;
		const char* seg = p;
		while (*p && *p != '/')
			p++;
		size_t len = (size_t)(p - seg);
		if (len == 1 && seg[0] == '.')
			continue;
		if (len == 2 && seg[0] == '.' && seg[1] == '.') {
			if (w > 1) {
				size_t k = w - 1;
				while (k > 0 && out[k] != '/')
					k--;
				w = (k == 0) ? 1 : k;
			}
			continue;
		}
		if (w > 1) {
			if (w + 1 >= cap)
				return false;
			out[w++] = '/';
		}
		if (w + len >= cap)
			return false;
		memmove(out + w, seg, len);
		w += len;
	}
	out[w] = '\0';
	return true;
}

/* if `current` lies inside `ancestor`, copy the first component below it
 * (used to re-select the folder you came from after going up / back) */
static bool FilesChildBelow(const char* ancestor, const char* current, char* out, size_t cap) {
	if (!FilesPathHasPrefix(current, ancestor))
		return false;
	size_t n = (ancestor[0] == '/' && ancestor[1] == '\0') ? 0 : strlen(ancestor);
	if (current[n] != '/' || current[n + 1] == '\0')
		return false;
	const char* s = current + n + 1;
	size_t i = 0;
	while (s[i] && s[i] != '/' && i + 1 < cap) {
		out[i] = s[i];
		i++;
	}
	out[i] = '\0';
	return i > 0;
}

static void FilesFormatSize(uint32_t bytes, char* out, size_t cap) {
	static const char* const units[] = {"KB", "MB", "GB"};
	if (bytes < 1024) {
		snprintf(out, cap, "%d %s", (int)bytes, bytes == 1 ? "byte" : "bytes");
		return;
	}
	/* round once, in the final unit (truncating per step showed 4 GB as 3.9) */
	uint64_t b10 = (uint64_t)bytes * 10;
	uint64_t div = 1024;
	int u = 0;
	uint64_t tenths = (b10 + div / 2) / div;
	while (tenths >= 10240 && u < 2) {
		div *= 1024;
		u++;
		tenths = (b10 + div / 2) / div;
	}
	if (tenths < 100)
		snprintf(out, cap, "%d.%d %s", (int)(tenths / 10), (int)(tenths % 10), units[u]);
	else
		snprintf(out, cap, "%d %s", (int)((tenths + 5) / 10), units[u]);
}

static void FilesFormatDate(uint32_t stamp, char* out, size_t cap) {
	static const char* const months[] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
	uint16_t date = (uint16_t)(stamp >> 16);
	uint16_t time = (uint16_t)(stamp & 0xFFFF);
	int month = (date >> 5) & 0x0F;
	int day = date & 0x1F;
	if (date == 0 || month < 1 || month > 12 || day == 0) {
		FilesStrCopy(out, "--", cap);
		return;
	}
	snprintf(out,
			 cap,
			 "%d %s %d, %02d:%02d",
			 day,
			 months[month - 1],
			 ((date >> 9) & 0x7F) + 1980,
			 (time >> 11) & 0x1F,
			 (time >> 5) & 0x3F);
}

/* ------------------------------------------------------------------------ */
/*  Opening files                                                           */
/* ------------------------------------------------------------------------ */

static int FilesOpenExe(const char* filename) {
	char fullpath[FILES_PATH_MAX];
	FilesStrCopy(fullpath, filename, sizeof(fullpath));
	char* dot = strrchr(fullpath, '.');
	if (dot) {
		for (char* p = dot; *p; p++)
			*p = (char)FilesLower((unsigned char)*p);
	}
	const char* slash = strrchr(fullpath, '/');
	const char* procName = slash ? slash + 1 : fullpath;
	int pid = _KeCreateProcess(0, (char*)procName);
	if (pid > 0)
		_KeProcessLoadExec(pid, fullpath, 0, NULL);
	return pid > 0;
}

static int FilesOpenAudio(const char* filename) {
	char* argv[1] = {(char*)filename};
	int pid = _KeCreateProcess(0, (char*)"accent");
	if (pid > 0)
		_KeProcessLoadExec(pid, (char*)"/audplr.exe", 1, argv);
	return pid > 0;
}

static int FilesOpenImage(const char* filename) {
	char* argv[1] = {(char*)filename};
	int pid = _KeCreateProcess(0, (char*)"glimpse");
	if (pid > 0)
		_KeProcessLoadExec(pid, (char*)"/glimpse.exe", 1, argv);
	return pid > 0;
}

/* one table drives the kind label, the icon and the opener */
static const FilesExtInfo g_extTable[] = {
	{"exe", KIND_APP, FilesOpenExe},
	{"dll", KIND_LIBRARY, NULL},
	{"bmp", KIND_IMAGE, FilesOpenImage},
	{"jpg", KIND_IMAGE, FilesOpenImage},
	{"jpeg", KIND_IMAGE, FilesOpenImage},
	{"png", KIND_IMAGE, FilesOpenImage},
	{"svg", KIND_IMAGE, NULL},
	{"wav", KIND_AUDIO, FilesOpenAudio},
	{"mp3", KIND_AUDIO, FilesOpenAudio},
	{"mp4", KIND_VIDEO, NULL},
	{"txt", KIND_TEXT, NULL},
	{"cnf", KIND_TEXT, NULL},
	{"conf", KIND_TEXT, NULL},
	{"md", KIND_TEXT, NULL},
	{"ttf", KIND_FONT, NULL},
};

#define FILES_EXT_COUNT (sizeof(g_extTable) / sizeof(g_extTable[0]))
#define FILES_EXT_NONE	0xFF

static const char* const g_kindName[KIND_COUNT] = {
	"Folder", "Application", "Library", "Image", "Audio", "Movie", "Text", "Font", "Document"};

static const uint8_t g_kindIcon[KIND_COUNT] = {ICON_FOLDER,
											   ICON_APP,
											   ICON_LIBRARY,
											   ICON_IMAGE,
											   ICON_MUSIC,
											   ICON_DOCUMENT,
											   ICON_DOCUMENT,
											   ICON_DOCUMENT,
											   ICON_DOCUMENT};

static uint8_t FilesLookupExt(const char* name) {
	const char* dot = strrchr(name, '.');
	if (!dot || dot == name || !dot[1])
		return FILES_EXT_NONE;
	for (size_t i = 0; i < FILES_EXT_COUNT; i++) {
		if (FilesStrCaseCmp(dot + 1, g_extTable[i].ext) == 0)
			return (uint8_t)i;
	}
	return FILES_EXT_NONE;
}

/* ------------------------------------------------------------------------ */
/*  Drawing helpers (all bounds-safe: ChDrawRect takes unsigned args, so a  */
/*  negative width would wrap and paint the whole row)                      */
/* ------------------------------------------------------------------------ */

static void FilesFill(ChCanvas* c, int x, int y, int w, int h, uint32_t col) {
	if (!c || !c->buffer)
		return;
	if (x < 0) {
		w += x;
		x = 0;
	}
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (w <= 0 || h <= 0)
		return;
	ChDrawRect(c, (unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h, col);
}

static int FilesISqrt(int v) {
	int r = 0;
	while ((r + 1) * (r + 1) <= v)
		r++;
	return r;
}

/* anti-alias-free rounded rectangle, one span per row: O(h * r) */
static void FilesFillRound(ChCanvas* c, int x, int y, int w, int h, int r, uint32_t col) {
	if (w <= 0 || h <= 0)
		return;
	if (r > h / 2)
		r = h / 2;
	if (r > w / 2)
		r = w / 2;
	for (int row = 0; row < h; row++) {
		int inset = 0;
		int k = -1;
		if (row < r)
			k = row;
		else if (row >= h - r)
			k = h - 1 - row;
		if (k >= 0) {
			int d = 2 * r - 2 * k - 1;
			inset = r - (FilesISqrt(4 * r * r - d * d) + 1) / 2;
		}
		FilesFill(c, x + inset, y + row, w - 2 * inset, 1, col);
	}
}

/* 2px-stroke chevron centred on (cx, cy) */
static void FilesChevron(ChCanvas* c, int cx, int cy, int s, int dir, uint32_t col) {
	for (int i = 0; i <= s; i++) {
		switch (dir) {
		case DIR_LEFT:
			FilesFill(c, cx - s / 2 + i, cy - i, 2, 1, col);
			FilesFill(c, cx - s / 2 + i, cy + i, 2, 1, col);
			break;
		case DIR_RIGHT:
			FilesFill(c, cx + s / 2 - i - 1, cy - i, 2, 1, col);
			FilesFill(c, cx + s / 2 - i - 1, cy + i, 2, 1, col);
			break;
		case DIR_UP:
			FilesFill(c, cx - i, cy - s / 2 + i, 1, 2, col);
			FilesFill(c, cx + i, cy - s / 2 + i, 1, 2, col);
			break;
		case DIR_DOWN:
			FilesFill(c, cx - i, cy + s / 2 - i - 1, 1, 2, col);
			FilesFill(c, cx + i, cy + s / 2 - i - 1, 1, 2, col);
			break;
		}
	}
}

static void FilesRing(ChCanvas* c, int cx, int cy, int r, uint32_t col) {
	if (cx - r < 0 || cy - r < 0)
		return;
	ChDrawCircleUnfilled(c, cx, cy, r, col);
	ChDrawCircleUnfilled(c, cx, cy, r - 1, col);
}

static int FilesBaseline(int y, int h, int size) {
	return y + (h + (size * 7) / 10) / 2;
}

static int FilesTextWidth(const char* s, int size) {
	ChFontSetSize(g.app->baseFont, size);
	return (int)ChFontGetWidth(g.app->baseFont, (char*)s);
}

static void FilesText(const char* s, int x, int baseline, int size, uint32_t col, ChRect* clip) {
	if (!s || !*s || clip->w <= 0 || clip->h <= 0)
		return;
	if (x < clip->x)
		x = clip->x;
	ChFontSetSize(g.app->baseFont, size);
	ChFontDrawTextClipped(g.win->canv, g.app->baseFont, (char*)s, x, baseline, col, clip);
}

/*
 * FilesElide -- fit `src` into maxW pixels, cutting the tail and adding
 * "..." when needed. Binary search over the cut point: O(log len) width
 * measurements instead of one per character.
 */
static void FilesElide(const char* src, int size, int maxW, char* out, size_t cap) {
	FilesStrCopy(out, src, cap);
	if (maxW <= 0) {
		out[0] = '\0';
		return;
	}
	if (FilesTextWidth(out, size) <= maxW)
		return;
	int len = (int)strlen(out);
	int lo = 0, hi = len;
	char probe[FILES_PATH_MAX];
	while (lo < hi) {
		int mid = (lo + hi + 1) / 2;
		if ((size_t)mid + 4 > sizeof(probe)) {
			hi = mid - 1;
			continue;
		}
		memmove(probe, src, (size_t)mid);
		memmove(probe + mid, "...", 4);
		if (FilesTextWidth(probe, size) <= maxW)
			lo = mid;
		else
			hi = mid - 1;
	}
	if ((size_t)lo + 4 > cap)
		lo = (int)cap - 4;
	memmove(out, src, (size_t)lo);
	memmove(out + lo, "...", 4);
}

/* first index whose suffix fits in maxW -- used by the text fields so the
 * caret end of a long string stays visible */
static int FilesTailStart(const char* s, int size, int maxW) {
	int len = (int)strlen(s);
	if (FilesTextWidth(s, size) <= maxW)
		return 0;
	int lo = 0, hi = len;
	while (lo < hi) {
		int mid = (lo + hi) / 2;
		if (FilesTextWidth(s + mid, size) <= maxW)
			hi = mid;
		else
			lo = mid + 1;
	}
	return lo;
}

/* ------------------------------------------------------------------------ */
/*  Icons                                                                   */
/* ------------------------------------------------------------------------ */

static void FilesIconFree(ChIcon* ico) {
	if (!ico)
		return;
	if (ico->image.image && ico->image.filesz > 0)
		_KeMemUnmap(ico->image.image, (size_t)ico->image.filesz);
	if (ico->image.fd >= 0)
		_KeCloseFile(ico->image.fd);
	free(ico);
}

static ChIcon* FilesIconLoad(const char* path) {
	ChIcon* ico = ChCreateIcon();
	if (!ico)
		return NULL;
	ChIconOpen(ico, (char*)path);
	if (ico->image.fd == -1 || !ico->image.image) {
		/* ChDrawIcon only checks fd; never hand it an icon without pixels */
		FilesIconFree(ico);
		return NULL;
	}
	ChIconRead(ico);
	if (!ico->image.data || ico->image.width <= 0 || ico->image.height <= 0 ||
		ico->image.width > 64 || ico->image.height > 64) {
		FilesIconFree(ico);
		return NULL;
	}
	return ico;
}

/*
 * FilesIconShrink -- nearest-neighbour downscale in place (no allocation).
 * Safe in place because every source pixel index is >= its destination
 * index (dw <= sw, dh <= sh), so nothing is read after being overwritten.
 * Byte copies because BMP pixel data is not guaranteed 4-byte aligned.
 */
static void FilesIconShrink(ChIcon* ico, int dw, int dh) {
	if (!ico || !ico->image.data)
		return;
	int sw = ico->image.width, sh = ico->image.height;
	if (dw <= 0 || dh <= 0 || dw > sw || dh > sh || (dw == sw && dh == sh))
		return;
	uint8_t* px = (uint8_t*)ico->image.data;
	for (int y = 0; y < dh; y++) {
		int sy = (y * sh) / dh;
		for (int x = 0; x < dw; x++) {
			int sx = (x * sw) / dw;
			memmove(px + ((size_t)y * dw + x) * 4, px + ((size_t)sy * sw + sx) * 4, 4);
		}
	}
	ico->image.width = dw;
	ico->image.height = dh;
}

static void FilesLoadIcons(void) {
	static const char* const paths[ICON_COUNT] = {"/icons/dir.bmp",
												  "/icons/doc.bmp",
												  "/icons/exe.bmp",
												  "/icons/img.bmp",
												  "/icons/dll.bmp",
												  "/icons/music.bmp",
												  "/icons/drive.bmp"};
	for (int i = 0; i < ICON_COUNT; i++)
		g.icons[i] = FilesIconLoad(paths[i]);

	/* drive.bmp is 35x30: taller than a sidebar row and wider than the 20px
	 * folder icons, which would push its label out of line. Bring it to the
	 * same 20px column, keeping the aspect ratio. */
	ChIcon* drive = g.icons[ICON_DRIVE];
	if (drive && drive->image.width > FILES_ICON_SIZE) {
		int h = (drive->image.height * FILES_ICON_SIZE) / drive->image.width;
		FilesIconShrink(drive, FILES_ICON_SIZE, h > 0 ? h : 1);
	}
	/* a missing icon file just leaves that slot NULL: rows render without it */
}

static ChIcon* FilesKindIcon(uint8_t kind) {
	ChIcon* ico = g.icons[g_kindIcon[kind]];
	return ico ? ico : g.icons[ICON_DOCUMENT];
}

/* ------------------------------------------------------------------------ */
/*  Directory cache, sorting and filtering                                  */
/* ------------------------------------------------------------------------ */

static bool FilesReserveIndices(int need) {
	if (need <= g.indexCap)
		return true;
	int cap = g.entryCap > need ? g.entryCap : need;
	int* block = (int*)malloc(sizeof(int) * 3 * (size_t)cap);
	if (!block)
		return false;
	free(g.order);
	g.order = block;
	g.view = block + cap;
	g.scratch = block + 2 * cap;
	g.indexCap = cap;
	return true;
}

static bool FilesReserveEntries(int need) {
	if (need <= g.entryCap)
		return true;
	if (need > FILES_MAX_ENTRIES)
		return false;
	int cap = g.entryCap ? g.entryCap : 64;
	while (cap < need)
		cap <<= 1;
	if (cap > FILES_MAX_ENTRIES)
		cap = FILES_MAX_ENTRIES;
	FilesEntry* e = (FilesEntry*)realloc(g.entries, sizeof(FilesEntry) * (size_t)cap);
	if (!e)
		return false;
	g.entries = e;
	g.entryCap = cap;
	return true;
}

/* give memory back after leaving a big folder (capacity >= 4x what we use) */
static void FilesTrimBuffers(void) {
	if (g.entryCap <= 256 || g.entryCount * 4 > g.entryCap)
		return;
	int cap = 64;
	while (cap < g.entryCount * 2)
		cap <<= 1;
	FilesEntry* e = (FilesEntry*)realloc(g.entries, sizeof(FilesEntry) * (size_t)cap);
	if (e) {
		g.entries = e;
		g.entryCap = cap;
	}
	if (g.indexCap > cap) {
		int* block = (int*)malloc(sizeof(int) * 3 * (size_t)cap);
		if (block) {
			free(g.order);
			g.order = block;
			g.view = block + cap;
			g.scratch = block + 2 * cap;
			g.indexCap = cap;
		}
	}
}

/*
 * FilesLoadDirectory -- read `dirpath` into the cache. The cache is only
 * touched once the directory opened successfully, so a failed navigation
 * leaves the current listing intact.
 */
static bool FilesLoadDirectory(const char* dirpath) {
	int dirfd = _KeOpenDir((char*)dirpath);
	if (dirfd < 0)
		return false;

	g.entryCount = 0;
	g.dirCount = 0;

	XEDirectoryEntry dirent;
	memset(&dirent, 0, sizeof(XEDirectoryEntry));

	/* same termination rules as before -- they encode the VFS readdir quirks */
	int prevIndex = -1;
	int guard = 0;
	while (guard++ < FILES_MAX_ENTRIES) {
		if (dirent.index == -1)
			break;
		prevIndex = dirent.index;
		dirent.flags = 0;
		dirent.size = 0;
		dirent.date = 0;
		dirent.time = 0;
		memset(dirent.filename, 0, sizeof(dirent.filename));

		int code = _KeReadDir(dirfd, &dirent);
		if (dirent.index == -1)
			break;
		if (code == -1 && dirent.index == prevIndex)
			break;
		if (code == -1)
			continue;
		if (dirent.filename[0] == '\0')
			continue;
		if (strcmp(dirent.filename, ".") == 0 || strcmp(dirent.filename, "..") == 0)
			continue;

		bool isDir = (dirent.flags & FILE_DIRECTORY) != 0;
		if (!isDir && !(dirent.flags & FILE_GENERAL))
			continue;
		if (!FilesReserveEntries(g.entryCount + 1))
			break;

		FilesEntry* e = &g.entries[g.entryCount++];
		memmove(e->name, dirent.filename, FILES_NAME_MAX - 1);
		e->name[FILES_NAME_MAX - 1] = '\0';
		e->size = isDir ? 0 : (uint32_t)dirent.size;
		e->stamp = ((uint32_t)(dirent.date & 0xFFFF) << 16) | (uint32_t)(dirent.time & 0xFFFF);
		e->isDir = isDir ? 1 : 0;
		e->reserved = 0;
		if (isDir) {
			e->ext = FILES_EXT_NONE;
			e->kind = KIND_FOLDER;
			g.dirCount++;
		} else {
			e->ext = FilesLookupExt(e->name);
			e->kind = (e->ext == FILES_EXT_NONE) ? KIND_DOCUMENT : g_extTable[e->ext].kind;
		}
	}
	_KeCloseFile(dirfd);

	FilesTrimBuffers();
	if (!FilesReserveIndices(g.entryCount)) {
		/* out of memory: show what fits rather than nothing */
		g.entryCount = g.indexCap;
		snprintf(g.notice, sizeof(g.notice), "Low memory: listing truncated");
	}
	return true;
}

static int FilesCompare(int a, int b) {
	const FilesEntry* x = &g.entries[a];
	const FilesEntry* y = &g.entries[b];
	if (x->isDir != y->isDir)
		return x->isDir ? -1 : 1; /* folders first, in either direction */
	int r = 0;
	switch (g.sortKey) {
	case SORT_DATE:
		r = (x->stamp < y->stamp) ? -1 : (x->stamp > y->stamp);
		break;
	case SORT_SIZE:
		r = (x->size < y->size) ? -1 : (x->size > y->size);
		break;
	case SORT_KIND:
		r = FilesStrCaseCmp(g_kindName[x->kind], g_kindName[y->kind]);
		break;
	default:
		break;
	}
	if (r == 0)
		r = FilesStrCaseCmp(x->name, y->name);
	return g.sortDesc ? -r : r;
}

/*
 * FilesSortOrder -- stable bottom-up merge sort of g.order.
 * Runs of 16 are insertion sorted, then merged pairwise into the scratch
 * area. Adjacent runs that are already in order are copied, not merged,
 * so re-sorting an ordered listing costs O(n).
 */
static void FilesSortOrder(void) {
	int n = g.entryCount;
	for (int i = 0; i < n; i++)
		g.order[i] = i;
	if (n < 2)
		return;

	const int RUN = 16;
	int* a = g.order;
	int* t = g.scratch;

	for (int lo = 0; lo < n; lo += RUN) {
		int hi = (lo + RUN < n) ? lo + RUN : n;
		for (int i = lo + 1; i < hi; i++) {
			int v = a[i];
			int j = i - 1;
			while (j >= lo && FilesCompare(a[j], v) > 0) {
				a[j + 1] = a[j];
				j--;
			}
			a[j + 1] = v;
		}
	}

	for (int width = RUN; width < n; width <<= 1) {
		for (int lo = 0; lo < n; lo += 2 * width) {
			int mid = (lo + width < n) ? lo + width : n;
			int hi = (lo + 2 * width < n) ? lo + 2 * width : n;
			if (mid >= hi || FilesCompare(a[mid - 1], a[mid]) <= 0) {
				memmove(t + lo, a + lo, sizeof(int) * (size_t)(hi - lo));
				continue;
			}
			int i = lo, j = mid, k = lo;
			while (i < mid && j < hi)
				t[k++] = (FilesCompare(a[j], a[i]) < 0) ? a[j++] : a[i++];
			while (i < mid)
				t[k++] = a[i++];
			while (j < hi)
				t[k++] = a[j++];
		}
		int* swap = a;
		a = t;
		t = swap;
	}
	if (a != g.order)
		memmove(g.order, a, sizeof(int) * (size_t)n);
}

/* filtered view over the sorted order; folders stay in [0, viewDirs) */
static void FilesBuildView(void) {
	g.viewCount = 0;
	g.viewDirs = 0;
	for (int i = 0; i < g.entryCount; i++) {
		int idx = g.order[i];
		if (g.searchLen && !FilesContainsCase(g.entries[idx].name, g.search, g.searchLen))
			continue;
		g.view[g.viewCount++] = idx;
		if (g.entries[idx].isDir)
			g.viewDirs++;
	}
}

/* ------------------------------------------------------------------------ */
/*  ListView glue                                                           */
/* ------------------------------------------------------------------------ */

static FilesEntry* FilesEntryAtRow(int row) {
	ListView* lv = g.list;
	if (!lv || row < 0 || row >= lv->itemCount)
		return NULL;
	intptr_t idx = (intptr_t)lv->item[row].data;
	if (idx < 0 || idx >= g.entryCount)
		return NULL;
	return &g.entries[idx];
}

static FilesEntry* FilesSelectedEntry(void) {
	return g.list ? FilesEntryAtRow(g.list->selectedIndex) : NULL;
}

static void FilesSelectedName(char* out, size_t cap) {
	FilesEntry* e = FilesSelectedEntry();
	FilesStrCopy(out, e ? e->name : "", cap);
}

/*
 * FilesListReserve -- size the ListView item buffer for `need` rows before
 * filling it. Pre-growing avoids one realloc per doubling inside
 * ListViewAddItem; shrinking returns memory after a big folder (a ListItem is
 * ~540 bytes). Must run right after ListViewClear. Returns the row budget.
 */
static int FilesListReserve(int need) {
	ListView* lv = g.list;
	int cap = lv->itemCapacity;
	int want = cap;
	if (need > cap) {
		while (want < need)
			want <<= 1;
	} else if (cap > 128 && need * 4 < cap) {
		want = 32;
		while (want < need * 2)
			want <<= 1;
	}
	if (want != cap) {
		ListItem* items = (ListItem*)realloc(lv->item, sizeof(ListItem) * (size_t)want);
		if (items) {
			lv->item = items;
			lv->itemCapacity = want;
		}
	}
	/* never let ListViewAddItem grow on its own: it does not check realloc */
	return need < lv->itemCapacity ? need : lv->itemCapacity;
}

/* stretch the Name column so the four columns always fill the view */
static void FilesListFitColumns(void) {
	ListView* lv = g.list;
	if (lv->columnCount < 4)
		return;
	int others = 0;
	for (int c = 1; c < lv->columnCount; c++)
		others += lv->columns[c].width;
	int nameW = (lv->base.w - FILES_SCROLLBAR_W) - others;
	if (nameW < FILES_COL_NAME_MIN)
		nameW = FILES_COL_NAME_MIN;
	lv->columns[0].width = nameW;
}

/*
 * FilesListSyncGeometry -- ListView has no resize call: move its embedded
 * scrollbars and recompute the viewport after the layout changed its rect.
 * Mirrors the library's (static) lv_recalc_content_metric.
 */
static void FilesListSyncGeometry(void) {
	ListView* lv = g.list;
	int x = lv->base.x, y = lv->base.y, w = lv->base.w, h = lv->base.h;
	int sb = FILES_SCROLLBAR_W;

	int vh = h - lv->headerHeight - sb;
	int vw = w - sb;
	if (vh < 0)
		vh = 0;
	if (vw < 0)
		vw = 0;

	lv->vscroll.base.x = x + w - sb;
	lv->vscroll.base.y = y + lv->headerHeight;
	lv->vscroll.base.w = sb;
	lv->vscroll.base.h = vh;
	lv->hscroll.base.x = x;
	lv->hscroll.base.y = y + h - sb;
	lv->hscroll.base.w = vw;
	lv->hscroll.base.h = sb;

	FilesListFitColumns();

	lv->content_height = lv->itemCount * lv->itemHeight;
	lv->content_width = 0;
	for (int c = 0; c < lv->columnCount; c++)
		lv->content_width += lv->columns[c].width;
	lv->viewport_height = vh;
	lv->viewport_width = vw;

	ChScrollBarSetRange(&lv->vscroll, 0, lv->content_height, vh);
	ChScrollBarSetRange(&lv->hscroll, 0, lv->content_width, vw);

	int maxY = lv->content_height - vh;
	int maxX = lv->content_width - vw;
	if (lv->scroll_y > (maxY > 0 ? maxY : 0))
		lv->scroll_y = maxY > 0 ? maxY : 0;
	if (lv->scroll_x > (maxX > 0 ? maxX : 0))
		lv->scroll_x = maxX > 0 ? maxX : 0;
	ChScrollBarSetValue(&lv->vscroll, lv->scroll_y);
	ChScrollBarSetValue(&lv->hscroll, lv->scroll_x);

	lv->prev_scroll_y = -1;
	lv->prev_scroll_x = -1;
	lv->force_full_redraw = true;
}

static void FilesPopulateList(const char* selectName) {
	ListView* lv = g.list;
	ListViewClear(lv);
	lv->hoverIndex = -1;

	int budget = FilesListReserve(g.viewCount);
	char date[32], size[24];
	int selectRow = -1;
	for (int i = 0; i < budget; i++) {
		FilesEntry* e = &g.entries[g.view[i]];
		FilesFormatDate(e->stamp, date, sizeof(date));
		if (e->isDir)
			FilesStrCopy(size, "--", sizeof(size));
		else
			FilesFormatSize(e->size, size, sizeof(size));
		const char* cells[4] = {e->name, date, size, g_kindName[e->kind]};
		ListViewAddItem(lv, cells, 4, (void*)(intptr_t)g.view[i]);
		ChIcon* ico = FilesKindIcon(e->kind);
		if (ico)
			ListViewSetItemIcon(lv, lv->itemCount - 1, ico, ico->image.width, ico->image.height);
		if (selectName && selectRow < 0 && FilesStrCaseCmp(e->name, selectName) == 0)
			selectRow = i;
	}
	if (budget < g.viewCount)
		snprintf(
			g.notice, sizeof(g.notice), "Low memory: showing %d of %d items", budget, g.viewCount);

	FilesListSyncGeometry();
	if (selectRow >= 0)
		FilesSelectRow(selectRow, true, false);
}

/* re-sort, re-filter and refill, keeping the selection by name */
static void FilesRefreshView(const char* selectName) {
	FilesSortOrder();
	FilesBuildView();
	FilesPopulateList(selectName);
}

/*
 * FilesSelectRow -- programmatic selection; scrolls the row into view
 */
static void FilesSelectRow(int row, bool reveal, bool repaint) {
	ListView* lv = g.list;
	if (lv->itemCount == 0) {
		lv->selectedIndex = -1;
		return;
	}
	if (row < 0)
		row = 0;
	if (row >= lv->itemCount)
		row = lv->itemCount - 1;
	lv->selectedIndex = row;

	if (reveal && lv->viewport_height > 0) {
		int top = row * lv->itemHeight;
		int bottom = top + lv->itemHeight;
		if (top < lv->scroll_y)
			lv->scroll_y = top;
		else if (bottom > lv->scroll_y + lv->viewport_height)
			lv->scroll_y = bottom - lv->viewport_height;
		if (lv->scroll_y < 0)
			lv->scroll_y = 0;
		ChScrollBarSetValue(&lv->vscroll, lv->scroll_y);
	}
	lv->force_full_redraw = true;
	if (repaint) {
		FilesRepaintList();
		FilesRepaintStatus();
	}
}

/* ------------------------------------------------------------------------ */
/*  Sidebar                                                                 */
/* ------------------------------------------------------------------------ */

static ChSidebarItem* FilesLocationItem(int i) {
	FilesLocation* l = &g.loc[i];
	if (l->section < 0 || l->section >= g.sidebar->sectionCount)
		return NULL;
	ChSidebarSection* s = &g.sidebar->sections[l->section];
	if (l->item < 0 || l->item >= s->itemCount)
		return NULL;
	return &s->items[l->item];
}

/* location containing the current path; longest prefix wins (/music beats /).
 * O(locations * path length) */
static int FilesBestLocation(bool volumesOnly) {
	int best = -1, bestLen = -1;
	for (int i = 0; i < g.locCount; i++) {
		FilesLocation* l = &g.loc[i];
		if (!l->mounted || (volumesOnly && !l->volume) || !FilesPathHasPrefix(g.path, l->path))
			continue;
		int len = (int)strlen(l->path);
		if (len > bestLen) {
			best = i;
			bestLen = len;
		}
	}
	return best;
}

/* FilesSyncSidebar -- highlight the location that contains the current path */
static void FilesSyncSidebar(bool repaint) {
	int best = FilesBestLocation(false);
	for (int i = 0; i < g.locCount; i++) {
		ChSidebarItem* item = FilesLocationItem(i);
		if (item)
			item->selected = (i == best);
	}
	if (best != g.activeLoc) {
		g.activeLoc = best;
		if (repaint)
			FilesRepaintSidebar();
	}
}

static void FilesSidebarSelect(ChSidebarItem* item, ChWindow* win) {
	(void)win;
	FilesLocation* l = (FilesLocation*)item->data;
	if (!l)
		return;
	if (!l->mounted) {
		/* restore the highlight first: the message box is not modal and the
		 * sidebar repaints the old and the clicked row right after this */
		FilesSyncSidebar(false);
		ChMessageBox* mb = ChCreateMessageBox(g.win,
											  (char*)"Drive Not Mounted",
											  (char*)"This disk or partition is not mounted.",
											  MSGBOX_TYPE_ONLYCLOSE,
											  MSGBOX_ICON_INFORMATION);
		ChMessageBoxShow(mb);
		return;
	}
	if (g.focus != FOCUS_LIST)
		FilesSetFocus(FOCUS_LIST);
	if (!FilesNavigate(l->path, true, NULL))
		FilesSyncSidebar(false);
}

static FilesLocation* FilesAddLocation(const char* label, const char* path, bool mounted) {
	if (g.locCount >= FILES_MAX_LOCATIONS)
		return NULL;
	FilesLocation* l = &g.loc[g.locCount++];
	memset(l, 0, sizeof(*l));
	FilesStrCopy(l->label, label, sizeof(l->label));
	FilesStrCopy(l->path, path, sizeof(l->path));
	l->mounted = mounted ? 1 : 0;
	l->section = -1;
	l->item = -1;
	/* two partitions of one disk share its name: tell them apart */
	for (int i = 0; i < g.locCount - 1; i++) {
		if (FilesStrCaseCmp(g.loc[i].label, l->label) == 0 && path[0]) {
			char tmp[40];
			snprintf(tmp, sizeof(tmp), "%s (%s)", label, path);
			FilesStrCopy(l->label, tmp, sizeof(l->label));
			break;
		}
	}
	return l;
}

static void FilesAddSidebarItem(ChSidebarSection* sec, int sectionIdx, FilesLocation* l, int icon) {
	if (!l)
		return;
	ChIcon* ico = g.icons[icon] ? g.icons[icon] : g.icons[ICON_FOLDER];
	int w = ico ? ico->image.width : 0;
	int h = ico ? ico->image.height : 0;
	ChSidebarAddItem(sec, l->label, ico, w, h, l);
	l->section = (int8_t)sectionIdx;
	l->item = (int8_t)(sec->itemCount - 1);
}

static void FilesAddVolume(ChSidebarSection* sec, int sectionIdx, FilesLocation* l) {
	if (!l)
		return;
	l->volume = 1;
	FilesAddSidebarItem(sec, sectionIdx, l, ICON_DRIVE);
}

static void FilesEnumeratePartitions(ChSidebarSection* sec, int sectionIdx) {
	FilesAddVolume(sec, sectionIdx, FilesAddLocation("System", "/", true));

	XEVDiskInfo disk;
	XEVDiskPartitionInfo part;
	for (int i = 0; i < XE_MAX_STORAGE_DEVICE; i++) {
		memset(&disk, 0, sizeof(disk));
		if (_KeGetStorageDiskInfo((uint8_t)i, &disk) == -1)
			break;
		disk.diskname[sizeof(disk.diskname) - 1] = '\0';

		for (int j = 0; j < disk.num_partition; j++) {
			memset(&part, 0, sizeof(part));
			if (_KeGetStoragePartitionInfo((uint8_t)i, (uint8_t)j, &part) == -1)
				break;
			part.mountedName[sizeof(part.mountedName) - 1] = '\0';
			if (strcmp(part.mountedName, "/") == 0 || part.mountedName[0] == '\0')
				continue; /* the root volume is "System" */
			FilesAddVolume(
				sec, sectionIdx, FilesAddLocation(disk.diskname, part.mountedName, true));
		}

		if (disk.num_partition == 0) {
			memset(&part, 0, sizeof(part));
			int ret = _KeGetStoragePartitionInfo((uint8_t)i, 0, &part);
			part.mountedName[sizeof(part.mountedName) - 1] = '\0';
			if (ret != -1 && part.mountedName[0] != '\0') {
				if (strcmp(part.mountedName, "/") != 0)
					FilesAddVolume(
						sec, sectionIdx, FilesAddLocation(disk.diskname, part.mountedName, true));
			} else {
				FilesAddVolume(sec, sectionIdx, FilesAddLocation(disk.diskname, "", false));
			}
		}
	}
}

static void FilesSidebarPaint(ChWidget* wid, ChWindow* win) {
	if (!wid->visible || wid->w <= 0 || wid->h <= 0)
		return;
	ChFontSetSize(win->app->baseFont, 13);
	g.sidebarPaint(wid, win);
}

static void FilesSidebarMouse(ChWidget* wid, ChWindow* win, int x, int y, int button) {
	if (g.resizing || !wid->visible)
		return;
	if (button == 1 && g.focus != FOCUS_LIST)
		FilesSetFocus(FOCUS_LIST);
	g.sidebarMouse(wid, win, x, y, button);
}

static void FilesSidebarDestroy(ChWidget* wid, ChWindow* win) {
	(void)win;
	ChSidebar* sb = (ChSidebar*)wid;
	for (int s = 0; s < sb->sectionCount; s++)
		free(sb->sections[s].items);
	free(sb->sections);
	free(sb);
}

static ChSidebar* FilesCreateSidebar(void) {
	static const struct {
		const char* label;
		const char* path;
		int icon;
	} favourites[] = {
		{"Music", "/music", ICON_MUSIC},
		{"Pictures", "/image", ICON_IMAGE},
		{"Icons", "/icons", ICON_FOLDER},
		{"App Data", "/appdata", ICON_FOLDER},
		{"Config", "/etc", ICON_FOLDER},
	};

	ChSidebar* sb = ChSidebarCreate(0, 0, FILES_SIDEBAR_W, FILES_WIN_H);
	if (!sb)
		return NULL;
	g.sidebar = sb;
	sb->bgColor = COL_SIDEBAR_BG;
	sb->hoverColor = COL_SIDEBAR_HOVER;
	sb->selectedColor = COL_SIDEBAR_SELECT;
	sb->textColor = COL_TEXT;
	sb->selectedTextColor = 0xFFFFFFFF;
	sb->headerTextColor = COL_SIDEBAR_HEADER;

	/* Favourites: only folders that really exist on this system */
	ChSidebarSection* fav = NULL;
	for (size_t i = 0; i < sizeof(favourites) / sizeof(favourites[0]); i++) {
		int fd = _KeOpenDir((char*)favourites[i].path);
		if (fd < 0)
			continue;
		_KeCloseFile(fd);
		if (!fav)
			fav = ChSidebarAddSection(sb, "Favourites");
		FilesAddSidebarItem(fav,
							sb->sectionCount - 1,
							FilesAddLocation(favourites[i].label, favourites[i].path, true),
							favourites[i].icon);
	}

	ChSidebarSection* places = ChSidebarAddSection(sb, "Locations");
	FilesEnumeratePartitions(places, sb->sectionCount - 1);

	/* item pointers are stable only once all items exist (realloc'd arrays) */
	for (int s = 0; s < sb->sectionCount; s++) {
		for (int i = 0; i < sb->sections[s].itemCount; i++)
			sb->sections[s].items[i].OnSelect = FilesSidebarSelect;
	}

	g.sidebarPaint = sb->base.ChPaintHandler;
	g.sidebarMouse = sb->base.ChMouseEvent;
	sb->base.ChPaintHandler = FilesSidebarPaint;
	sb->base.ChMouseEvent = FilesSidebarMouse;
	sb->base.ChDestroy = FilesSidebarDestroy;
	return sb;
}

/* ------------------------------------------------------------------------ */
/*  History and navigation                                                  */
/* ------------------------------------------------------------------------ */

static char* FilesHistoryAt(int i) {
	return g.history[(g.histStart + i) % FILES_HISTORY_MAX];
}

static void FilesHistoryPush(const char* p) {
	if (g.histCount > 0 && g.histPos >= 0 && FilesStrCaseCmp(FilesHistoryAt(g.histPos), p) == 0)
		return;
	g.histCount = g.histPos + 1; /* a new visit drops the forward entries */
	if (g.histCount == FILES_HISTORY_MAX) {
		g.histStart = (g.histStart + 1) % FILES_HISTORY_MAX;
		g.histCount--;
	}
	FilesStrCopy(FilesHistoryAt(g.histCount), p, FILES_PATH_MAX);
	g.histPos = g.histCount;
	g.histCount++;
}

static bool FilesCanGoBack(void) {
	return g.histPos > 0;
}

static bool FilesCanGoForward(void) {
	return g.histPos >= 0 && g.histPos < g.histCount - 1;
}

/*
 * FilesNavigate -- open `target` (absolute, or relative to the current
 * folder). The listing and the current path only change on success.
 */
static bool FilesNavigate(const char* target, bool remember, const char* selectName) {
	char resolved[FILES_PATH_MAX];
	char select[FILES_NAME_MAX];
	FilesStrCopy(select, selectName, sizeof(select));

	if (!FilesPathResolve(g.path, target, resolved, sizeof(resolved))) {
		FilesStrCopy(g.notice, "That path is too long", sizeof(g.notice));
		FilesRepaintStatus();
		return false;
	}

	/* remember the selection when simply reloading the same folder */
	if (!select[0] && FilesStrCaseCmp(resolved, g.path) == 0)
		FilesSelectedName(select, sizeof(select));

	g.notice[0] = '\0';
	if (!FilesLoadDirectory(resolved)) {
		snprintf(g.notice, sizeof(g.notice), "Can't open \"%s\"", resolved);
		FilesRepaintStatus();
		return false;
	}

	bool sameFolder = FilesStrCaseCmp(resolved, g.path) == 0;
	FilesStrCopy(g.path, resolved, sizeof(g.path));
	if (remember)
		FilesHistoryPush(g.path);
	if (!sameFolder) {
		g.search[0] = '\0';
		g.searchLen = 0;
		g.list->scroll_y = 0;
		g.list->scroll_x = 0;
	}
	g.typeLen = 0;

	FilesRefreshView(select[0] ? select : NULL);
	FilesSyncSidebar(true);
	FilesRepaintToolbar();
	FilesRepaintList();
	FilesRepaintStatus();
	return true;
}

static void FilesGoHistory(int delta) {
	int pos = g.histPos + delta;
	if (pos < 0 || pos >= g.histCount)
		return;
	char target[FILES_PATH_MAX];
	char child[FILES_NAME_MAX];
	FilesStrCopy(target, FilesHistoryAt(pos), sizeof(target));
	bool haveChild = FilesChildBelow(target, g.path, child, sizeof(child));
	int oldPos = g.histPos;
	g.histPos = pos; /* before navigating, so the toolbar paints the new state once */
	if (!FilesNavigate(target, false, haveChild ? child : NULL)) {
		g.histPos = oldPos;
		FilesRepaintToolbar();
	}
}

static void FilesGoBack(void) {
	FilesGoHistory(-1);
}

static void FilesGoForward(void) {
	FilesGoHistory(1);
}

static void FilesGoUp(void) {
	if (g.path[0] == '/' && g.path[1] == '\0')
		return;
	char parent[FILES_PATH_MAX];
	char child[FILES_NAME_MAX];
	if (!FilesPathResolve(g.path, "..", parent, sizeof(parent)))
		return;
	bool haveChild = FilesChildBelow(parent, g.path, child, sizeof(child));
	FilesNavigate(parent, true, haveChild ? child : NULL);
}

static void FilesRefresh(void) {
	char here[FILES_PATH_MAX];
	FilesStrCopy(here, g.path, sizeof(here));
	FilesNavigate(here, false, NULL);
}

static void FilesOpenRow(int row) {
	FilesEntry* e = FilesEntryAtRow(row);
	if (!e)
		return;
	char name[FILES_NAME_MAX];
	FilesStrCopy(name, e->name, sizeof(name)); /* the cache is reused by navigation */

	if (e->isDir) {
		FilesNavigate(name, true, NULL);
		return;
	}

	char full[FILES_PATH_MAX];
	if (!FilesPathResolve(g.path, name, full, sizeof(full)))
		return;
	FilesOpenFn open = (e->ext != FILES_EXT_NONE) ? g_extTable[e->ext].open : NULL;
	if (open && open(full))
		g.notice[0] = '\0';
	else
		snprintf(g.notice, sizeof(g.notice), "No application can open \"%s\"", name);
	FilesRepaintStatus();
}

static void FilesOnListAction(ListView* lv, int index, ListItem* item) {
	(void)lv;
	(void)item;
	FilesOpenRow(index);
}

/* ------------------------------------------------------------------------ */
/*  Search and type-to-select                                               */
/* ------------------------------------------------------------------------ */

static void FilesApplySearch(void) {
	char keep[FILES_NAME_MAX];
	FilesSelectedName(keep, sizeof(keep));
	g.list->scroll_y = 0;
	FilesBuildView(); /* order is already sorted: filtering is a single pass */
	FilesPopulateList(keep[0] ? keep : NULL);
	FilesRepaintToolbarElement(TB_SEARCH);
	FilesRepaintList();
	FilesRepaintStatus();
}

static void FilesClearSearch(void) {
	if (!g.searchLen)
		return;
	g.search[0] = '\0';
	g.searchLen = 0;
	FilesApplySearch();
}

/* first row in [lo, hi) whose name starts with prefix; the range must be
 * sorted by name ascending. O(log n) */
static int FilesLowerBound(int lo, int hi, const char* p, int n) {
	int a = lo, b = hi;
	while (a < b) {
		int mid = a + (b - a) / 2;
		if (FilesPrefixCmp(g.entries[g.view[mid]].name, p, n) < 0)
			a = mid + 1;
		else
			b = mid;
	}
	if (a < hi && FilesPrefixCmp(g.entries[g.view[a]].name, p, n) == 0)
		return a;
	return -1;
}

static int FilesFindPrefix(const char* p, int n) {
	if (g.sortKey == SORT_NAME && !g.sortDesc) {
		int r = FilesLowerBound(0, g.viewDirs, p, n);
		return r >= 0 ? r : FilesLowerBound(g.viewDirs, g.viewCount, p, n);
	}
	for (int i = 0; i < g.viewCount; i++) {
		if (FilesPrefixCmp(g.entries[g.view[i]].name, p, n) == 0)
			return i;
	}
	return -1;
}

static void FilesTypeAhead(char c) {
	size_t now = _KeGetSystemTimerTick();
	if (g.typeLen && now - g.typeTick > FILES_TYPEAHEAD_GAP_TICKS)
		g.typeLen = 0;
	g.typeTick = now;
	if (g.typeLen < FILES_TYPEAHEAD_MAX - 1) {
		g.typeBuf[g.typeLen++] = c;
		g.typeBuf[g.typeLen] = '\0';
	}
	int row = FilesFindPrefix(g.typeBuf, g.typeLen);
	if (row >= 0 && row < g.list->itemCount)
		FilesSelectRow(row, true, true);
}

/* ------------------------------------------------------------------------ */
/*  Sort, sidebar toggle, info                                              */
/* ------------------------------------------------------------------------ */

static void FilesSetSort(int key) {
	if (g.sortKey == key) {
		g.sortDesc = !g.sortDesc;
	} else {
		g.sortKey = key;
		/* newest / biggest first is the useful default for those columns */
		g.sortDesc = (key == SORT_DATE || key == SORT_SIZE);
	}
	char keep[FILES_NAME_MAX];
	FilesSelectedName(keep, sizeof(keep));
	FilesRefreshView(keep[0] ? keep : NULL);
	FilesRepaintList();
	FilesRepaintStatus();
}

static void FilesLayoutSetAlign(ChBorderLayout* l, ChWidget* w, ChBorderAlignment align) {
	for (unsigned i = 0; i < l->items->pointer; i++) {
		ChBorderItem* it = (ChBorderItem*)list_get_at(l->items, i);
		if (it && it->widget == w) {
			it->align = align;
			return;
		}
	}
}

/*
 * FilesApplySidebarVisibility -- the sidebar is shown when the user wants it
 * (Ctrl+B) AND the window is wide enough; a narrow window folds it away
 * without forgetting the preference.
 */
static void FilesApplySidebarVisibility(int windowW) {
	ChSidebar* sb = g.sidebar;
	bool show = g.sidebarVisible && windowW >= FILES_SIDEBAR_AUTOHIDE;
	if (show != sb->base.visible) {
		sb->base.visible = show;
		sb->base.hover = false;
		sb->hoverSectionIdx = -1;
		sb->hoverItemIdx = -1;
		FilesLayoutSetAlign(
			g.bodyLayout, (ChWidget*)sb, show ? CH_BORDER_ALIGN_LEFT : FILES_ALIGN_DETACHED);
	}
	if (show) {
		int sw = (windowW < FILES_SIDEBAR_BREAKPOINT) ? FILES_SIDEBAR_W_COMPACT : FILES_SIDEBAR_W;
		ChBorderLayoutSetPreferred(g.bodyLayout, (ChWidget*)sb, sw, 0);
	}
}

static void FilesToggleSidebar(void) {
	g.sidebarVisible = !g.sidebarVisible;
	FilesRelayout();
	if (g.ready && !g.resizing)
		ChWindowPaint(g.win);
}

static void FilesShowInfo(void) {
	FilesEntry* e = FilesSelectedEntry();
	char title[FILES_NAME_MAX + 8];
	char msg[200];
	char size[24], date[32];
	if (!e) {
		FilesStrCopy(title, "Info", sizeof(title));
		snprintf(msg,
				 sizeof(msg),
				 "%s  -  %d folders, %d files",
				 g.path,
				 g.dirCount,
				 g.entryCount - g.dirCount);
	} else {
		FilesStrCopy(title, e->name, sizeof(title));
		FilesFormatDate(e->stamp, date, sizeof(date));
		if (e->isDir)
			FilesStrCopy(size, "--", sizeof(size));
		else
			FilesFormatSize(e->size, size, sizeof(size));
		snprintf(msg,
				 sizeof(msg),
				 "%s  -  %s  -  Modified %s  -  in %s",
				 g_kindName[e->kind],
				 size,
				 date,
				 g.path);
	}
	ChMessageBox* mb =
		ChCreateMessageBox(g.win, title, msg, MSGBOX_TYPE_ONLYCLOSE, MSGBOX_ICON_INFORMATION);
	ChMessageBoxShow(mb);
}

/* ------------------------------------------------------------------------ */
/*  Toolbar                                                                 */
/* ------------------------------------------------------------------------ */

static void FilesRectSet(ChRect* r, int x, int y, int w, int h) {
	r->x = x;
	r->y = y;
	r->w = w > 0 ? w : 0;
	r->h = h > 0 ? h : 0;
}

static bool FilesRectHit(const ChRect* r, int x, int y) {
	return r->w > 0 && r->h > 0 && x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

/* O(1): positions every control from the toolbar's current rect */
static void FilesToolbarLayout(FilesToolbar* tb) {
	int x = tb->base.x, y = tb->base.y, w = tb->base.w, h = tb->base.h;
	int bw = 30, bh = 28;
	int by = y + (h - bh) / 2;

	for (int i = 0; i < TB_COUNT; i++)
		FilesRectSet(&tb->r[i], 0, 0, 0, 0);
	if (w <= 0 || h <= 0)
		return;

	int cx = x + 10;
	FilesRectSet(&tb->r[TB_SIDEBAR], cx, by, bw, bh);
	cx += bw + 10;
	FilesRectSet(&tb->r[TB_BACK], cx, by, bw, bh);
	cx += bw + 2;
	FilesRectSet(&tb->r[TB_FORWARD], cx, by, bw, bh);
	cx += bw + 6;
	FilesRectSet(&tb->r[TB_UP], cx, by, bw, bh);
	cx += bw + 10;

	int right = x + w - 10;
	if (g.win->info->width >= FILES_SEARCH_BREAKPOINT) {
		int sw = w / 4;
		if (sw < 150)
			sw = 150;
		if (sw > 240)
			sw = 240;
		FilesRectSet(&tb->r[TB_SEARCH], right - sw, by, sw, bh);
		FilesRectSet(&tb->r[TB_SEARCH_CLEAR], right - 26, by + 4, 20, bh - 8);
		right -= sw + 8;
	}
	if (right - bw >= cx) {
		FilesRectSet(&tb->r[TB_REFRESH], right - bw, by, bw, bh);
		right -= bw + 8;
	}
	if (right - cx >= 60)
		FilesRectSet(&tb->r[TB_PATH], cx, by, right - cx, bh);
}

static bool FilesToolbarEnabled(int id) {
	switch (id) {
	case TB_BACK:
		return FilesCanGoBack();
	case TB_FORWARD:
		return FilesCanGoForward();
	case TB_UP:
		return !(g.path[0] == '/' && g.path[1] == '\0');
	default:
		return true;
	}
}

static void FilesToolbarPaintButton(FilesToolbar* tb, int id) {
	ChCanvas* c = g.win->canv;
	ChRect* r = &tb->r[id];
	if (r->w <= 0)
		return;
	FilesFill(c, r->x, r->y, r->w, r->h, COL_TOOLBAR_BG);

	bool enabled = FilesToolbarEnabled(id);
	if (enabled && tb->hot == id)
		FilesFillRound(
			c, r->x, r->y, r->w, r->h, 7, tb->pressed == id ? COL_BTN_PRESSED : COL_BTN_HOVER);

	uint32_t fg = enabled ? COL_GLYPH : COL_GLYPH_DISABLED;
	int cx = r->x + r->w / 2;
	int cy = r->y + r->h / 2;
	switch (id) {
	case TB_SIDEBAR: {
		/* window with a sidebar pane: outline + divider + tinted pane */
		int gx = cx - 9, gy = cy - 7;
		ChDrawRectUnfilled(c, gx, gy, 18, 14, fg);
		ChDrawRectUnfilled(c, gx + 1, gy + 1, 16, 12, fg);
		FilesFill(c, gx + 6, gy + 1, 2, 12, fg);
		if (g.sidebar && g.sidebar->base.visible)
			FilesFill(c, gx + 2, gy + 2, 4, 10, fg);
		break;
	}
	case TB_BACK:
		FilesChevron(c, cx, cy, 5, DIR_LEFT, fg);
		break;
	case TB_FORWARD:
		FilesChevron(c, cx, cy, 5, DIR_RIGHT, fg);
		break;
	case TB_UP:
		FilesChevron(c, cx, cy - 1, 5, DIR_UP, fg);
		FilesFill(c, cx - 5, cy + 5, 11, 2, fg);
		break;
	case TB_REFRESH: {
		/* open ring with an arrow head at the gap */
		FilesRing(c, cx, cy, 7, fg);
		uint32_t bg = (enabled && tb->hot == id)
						  ? (tb->pressed == id ? COL_BTN_PRESSED : COL_BTN_HOVER)
						  : COL_TOOLBAR_BG;
		FilesFill(c, cx + 1, cy - 8, 8, 6, bg);
		FilesFill(c, cx + 1, cy - 7, 5, 2, fg);
		FilesFill(c, cx + 4, cy - 7, 2, 5, fg);
		break;
	}
	default:
		break;
	}
}

/*
 * Breadcrumb: "<location> > dir > dir". When it does not fit, the first
 * crumb and as many trailing crumbs as possible are kept, joined by "...".
 */
static void FilesToolbarPaintCrumbs(FilesToolbar* tb, ChRect* clip) {
	typedef struct {
		const char* s;
		int len;
		int end;
		int w;
	} Crumb;
	Crumb cr[FILES_MAX_CRUMBS];
	int n = 0;
	const int font = 13, pad = 6, sep = 14;
	char buf[FILES_PATH_MAX];

	/* the trail starts at the volume, not at a Favourite, so every parent
	 * folder stays one click away */
	int rootEnd = 1;
	const char* rootLabel = "/";
	int vol = FilesBestLocation(true);
	if (vol >= 0) {
		rootLabel = g.loc[vol].label;
		rootEnd = (int)strlen(g.loc[vol].path);
		if (rootEnd < 1)
			rootEnd = 1;
	}
	cr[n].s = rootLabel;
	cr[n].len = (int)strlen(rootLabel);
	cr[n].end = rootEnd;
	n++;

	int i = rootEnd;
	int plen = (int)strlen(g.path);
	while (i < plen && n < FILES_MAX_CRUMBS) {
		while (i < plen && g.path[i] == '/')
			i++;
		int s = i;
		while (i < plen && g.path[i] != '/')
			i++;
		if (i > s) {
			cr[n].s = g.path + s;
			cr[n].len = i - s;
			cr[n].end = i;
			n++;
		}
	}

	for (int k = 0; k < n; k++) {
		int len = cr[k].len < (int)sizeof(buf) - 1 ? cr[k].len : (int)sizeof(buf) - 1;
		memmove(buf, cr[k].s, (size_t)len);
		buf[len] = '\0';
		cr[k].w = FilesTextWidth(buf, font) + pad * 2;
	}

	/* choose what to show: all, or first + "..." + tail */
	int ellW = FilesTextWidth("...", font) + pad * 2;
	int total = 0;
	for (int k = 0; k < n; k++)
		total += cr[k].w + (k ? sep : 0);
	int firstTail = 1;
	bool ellipsis = false;
	if (total > clip->w && n > 1) {
		ellipsis = true;
		int used = cr[0].w + sep + ellW;
		firstTail = n;
		while (firstTail > 1 && used + sep + cr[firstTail - 1].w <= clip->w) {
			used += sep + cr[firstTail - 1].w;
			firstTail--;
		}
		if (firstTail == n)
			firstTail = n - 1; /* always keep the current folder */
	}

	ChCanvas* c = g.win->canv;
	int x = clip->x;
	int baseline = FilesBaseline(clip->y, clip->h, font);
	tb->crumbCount = 0;

	for (int k = 0; k < n; k++) {
		if (ellipsis && k > 0 && k < firstTail) {
			if (k == 1) {
				FilesText("...", x + pad, baseline, font, COL_TEXT_FAINT, clip);
				if (tb->crumbCount < FILES_MAX_CRUMBS) {
					FilesCrumb* fc = &tb->crumbs[tb->crumbCount++];
					fc->x0 = (int16_t)x;
					fc->x1 = (int16_t)(x + ellW);
					fc->end = -1;
				}
				x += ellW;
				FilesChevron(c, x + sep / 2, clip->y + clip->h / 2, 3, DIR_RIGHT, COL_TEXT_FAINT);
				x += sep;
			}
			continue;
		}
		bool last = (k == n - 1);
		int w = cr[k].w;
		if (x + w > clip->x + clip->w)
			w = clip->x + clip->w - x;
		if (w <= 0)
			break;

		int slot = tb->crumbCount;
		if (!last && slot == tb->hotCrumb)
			FilesFillRound(c, x, clip->y + 2, w, clip->h - 4, 5, COL_CRUMB_HOVER);

		int len = cr[k].len < (int)sizeof(buf) - 1 ? cr[k].len : (int)sizeof(buf) - 1;
		memmove(buf, cr[k].s, (size_t)len);
		buf[len] = '\0';
		char shown[FILES_PATH_MAX];
		FilesElide(buf, font, w - pad * 2, shown, sizeof(shown));
		FilesText(shown, x + pad, baseline, font, last ? COL_TEXT : COL_TEXT_DIM, clip);

		if (slot < FILES_MAX_CRUMBS) {
			FilesCrumb* fc = &tb->crumbs[tb->crumbCount++];
			fc->x0 = (int16_t)x;
			fc->x1 = (int16_t)(x + w);
			fc->end = (int16_t)cr[k].end;
		}
		x += w;
		if (!last) {
			FilesChevron(c, x + sep / 2, clip->y + clip->h / 2, 3, DIR_RIGHT, COL_TEXT_FAINT);
			x += sep;
		}
	}
}

static void FilesPaintField(const ChRect* r, int radius, bool focused) {
	ChCanvas* c = g.win->canv;
	FilesFill(c, r->x, r->y, r->w, r->h, COL_TOOLBAR_BG);
	if (focused) {
		FilesFillRound(c, r->x, r->y, r->w, r->h, radius, COL_FOCUS);
		FilesFillRound(c, r->x + 2, r->y + 2, r->w - 4, r->h - 4, radius - 2, COL_FIELD_BG);
	} else {
		FilesFillRound(c, r->x, r->y, r->w, r->h, radius, COL_FIELD_BORDER);
		FilesFillRound(c, r->x + 1, r->y + 1, r->w - 2, r->h - 2, radius - 1, COL_FIELD_BG);
	}
}

/* single line edit text with the caret at the end, tail kept in view */
static void FilesPaintEditText(const char* text, ChRect* clip, int font) {
	int start = FilesTailStart(text, font, clip->w - 4);
	int baseline = FilesBaseline(clip->y, clip->h, font);
	FilesText(text + start, clip->x, baseline, font, COL_TEXT, clip);
	int caretX = clip->x + (text[start] ? FilesTextWidth(text + start, font) : 0) + 1;
	if (caretX > clip->x + clip->w - 2)
		caretX = clip->x + clip->w - 2;
	FilesFill(g.win->canv, caretX, clip->y + (clip->h - 16) / 2, 2, 16, COL_FOCUS);
}

static void FilesToolbarPaintPath(FilesToolbar* tb) {
	ChRect* r = &tb->r[TB_PATH];
	tb->crumbCount = 0;
	if (r->w <= 0)
		return;
	bool focused = (g.focus == FOCUS_PATH);
	FilesPaintField(r, 7, focused);
	ChRect clip;
	clip.x = r->x + 6;
	clip.y = r->y + 2;
	clip.w = r->w - 12;
	clip.h = r->h - 4;
	if (focused)
		FilesPaintEditText(g.edit, &clip, 13);
	else
		FilesToolbarPaintCrumbs(tb, &clip);
}

static void FilesToolbarPaintSearch(FilesToolbar* tb) {
	ChRect* r = &tb->r[TB_SEARCH];
	if (r->w <= 0)
		return;
	ChCanvas* c = g.win->canv;
	bool focused = (g.focus == FOCUS_SEARCH);
	FilesPaintField(r, r->h / 2, focused);

	/* magnifier */
	int mx = r->x + 16, my = r->y + r->h / 2 - 1;
	FilesRing(c, mx, my, 5, COL_TEXT_DIM);
	for (int i = 0; i < 4; i++)
		FilesFill(c, mx + 3 + i, my + 3 + i, 2, 2, COL_TEXT_DIM);

	ChRect clip;
	clip.x = r->x + 28;
	clip.y = r->y + 2;
	clip.w = r->w - 28 - (g.searchLen ? 28 : 12);
	clip.h = r->h - 4;
	if (focused)
		FilesPaintEditText(g.search, &clip, 13);
	else if (g.searchLen)
		FilesText(g.search, clip.x, FilesBaseline(clip.y, clip.h, 13), 13, COL_TEXT, &clip);
	else
		FilesText("Search", clip.x, FilesBaseline(clip.y, clip.h, 13), 13, COL_TEXT_FAINT, &clip);

	if (g.searchLen) {
		ChRect* x = &tb->r[TB_SEARCH_CLEAR];
		int cx = x->x + x->w / 2, cy = x->y + x->h / 2;
		uint32_t ring = (tb->hot == TB_SEARCH_CLEAR) ? COL_TEXT_DIM : COL_TEXT_FAINT;
		FilesFillRound(c, cx - 7, cy - 7, 14, 14, 7, ring);
		for (int i = -3; i <= 3; i++) {
			FilesFill(c, cx + i, cy + i, 1, 1, COL_FIELD_BG);
			FilesFill(c, cx + i, cy - i, 1, 1, COL_FIELD_BG);
			FilesFill(c, cx + i + 1, cy + i, 1, 1, COL_FIELD_BG);
			FilesFill(c, cx + i + 1, cy - i, 1, 1, COL_FIELD_BG);
		}
	}
}

static void FilesToolbarPaintElement(FilesToolbar* tb, int id) {
	switch (id) {
	case TB_PATH:
		FilesToolbarPaintPath(tb);
		break;
	case TB_SEARCH:
	case TB_SEARCH_CLEAR:
		FilesToolbarPaintSearch(tb);
		break;
	case TB_NONE:
		break;
	default:
		FilesToolbarPaintButton(tb, id);
		break;
	}
}

static void FilesToolbarPaint(ChWidget* wid, ChWindow* win) {
	FilesToolbar* tb = (FilesToolbar*)wid;
	if (wid->w <= 0 || wid->h <= 0)
		return;
	FilesToolbarLayout(tb);
	FilesFill(win->canv, wid->x, wid->y, wid->w, wid->h, COL_TOOLBAR_BG);
	FilesFill(win->canv, wid->x, wid->y + wid->h - 1, wid->w, 1, COL_TOOLBAR_EDGE);
	for (int id = 0; id < TB_COUNT; id++) {
		if (id != TB_SEARCH_CLEAR)
			FilesToolbarPaintElement(tb, id);
	}
}

static int FilesToolbarHit(FilesToolbar* tb, int x, int y) {
	if (g.searchLen && FilesRectHit(&tb->r[TB_SEARCH_CLEAR], x, y))
		return TB_SEARCH_CLEAR;
	for (int id = 0; id < TB_COUNT; id++) {
		if (id != TB_SEARCH_CLEAR && FilesRectHit(&tb->r[id], x, y))
			return id;
	}
	return TB_NONE;
}

static int FilesCrumbAt(FilesToolbar* tb, int x) {
	for (int i = 0; i < tb->crumbCount; i++) {
		if (x >= tb->crumbs[i].x0 && x < tb->crumbs[i].x1)
			return tb->crumbs[i].end > 0 ? i : -1;
	}
	return -1;
}

static void FilesCrumbClicked(FilesToolbar* tb, int crumb) {
	int end = tb->crumbs[crumb].end;
	int plen = (int)strlen(g.path);
	if (end <= 0 || end >= plen)
		return; /* the current folder */
	char target[FILES_PATH_MAX];
	char child[FILES_NAME_MAX];
	memmove(target, g.path, (size_t)end);
	target[end] = '\0';
	bool haveChild = FilesChildBelow(target, g.path, child, sizeof(child));
	FilesNavigate(target, true, haveChild ? child : NULL);
}

static void FilesToolbarMouse(ChWidget* wid, ChWindow* win, int mx, int my, int button) {
	FilesToolbar* tb = (FilesToolbar*)wid;
	if (g.resizing || button == DEODHAI_MOUSE_MSG_SCROLL_UP ||
		button == DEODHAI_MOUSE_MSG_SCROLL_DOWN)
		return;

	int x = mx - win->info->x;
	int y = my - win->info->y;
	bool down = (button != 0);
	bool pressEdge = down && !tb->prevDown;
	tb->prevDown = down;

	int hit = wid->hover ? FilesToolbarHit(tb, x, y) : TB_NONE;
	int crumb = (hit == TB_PATH && g.focus != FOCUS_PATH) ? FilesCrumbAt(tb, x) : -1;
	int pressed = down ? (pressEdge ? hit : tb->pressed) : TB_NONE;

	int oldHot = tb->hot, oldPressed = tb->pressed, oldCrumb = tb->hotCrumb;
	tb->hot = hit;
	tb->hotCrumb = crumb;
	tb->pressed = pressed;

	if (oldHot != hit || oldPressed != pressed) {
		FilesRepaintToolbarElement(oldHot);
		if (hit != oldHot)
			FilesRepaintToolbarElement(hit);
	} else if (hit == TB_PATH && oldCrumb != crumb) {
		FilesRepaintToolbarElement(TB_PATH);
	}

	if (!pressEdge)
		return;

	switch (hit) {
	case TB_SIDEBAR:
		FilesToggleSidebar();
		break;
	case TB_BACK:
		FilesGoBack();
		break;
	case TB_FORWARD:
		FilesGoForward();
		break;
	case TB_UP:
		FilesGoUp();
		break;
	case TB_REFRESH:
		FilesRefresh();
		break;
	case TB_PATH:
		if (crumb >= 0 && g.focus != FOCUS_PATH)
			FilesCrumbClicked(tb, crumb);
		else
			FilesSetFocus(FOCUS_PATH);
		break;
	case TB_SEARCH:
		FilesSetFocus(FOCUS_SEARCH);
		break;
	case TB_SEARCH_CLEAR:
		FilesClearSearch();
		break;
	default:
		if (g.focus != FOCUS_LIST)
			FilesSetFocus(FOCUS_LIST);
		break;
	}
}

static void FilesFreeWidget(ChWidget* wid, ChWindow* win) {
	(void)win;
	free(wid);
}

static FilesToolbar* FilesCreateToolbar(void) {
	FilesToolbar* tb = (FilesToolbar*)malloc(sizeof(FilesToolbar));
	if (!tb)
		return NULL;
	memset(tb, 0, sizeof(FilesToolbar));
	tb->base.w = FILES_WIN_W;
	tb->base.h = FILES_TOOLBAR_H; /* BorderLayout takes this as the preferred height */
	tb->base.ChPaintHandler = FilesToolbarPaint;
	tb->base.ChMouseEvent = FilesToolbarMouse;
	tb->base.ChDestroy = FilesFreeWidget;
	tb->hot = TB_NONE;
	tb->pressed = TB_NONE;
	tb->hotCrumb = -1;
	return tb;
}

/* ------------------------------------------------------------------------ */
/*  Status bar                                                              */
/* ------------------------------------------------------------------------ */

static void FilesSortLabel(char* out, size_t cap) {
	static const char* const names[SORT_COUNT] = {"Name", "Date", "Size", "Kind"};
	static const char* const asc[SORT_COUNT] = {"A-Z", "oldest first", "smallest first", "A-Z"};
	static const char* const desc[SORT_COUNT] = {"Z-A", "newest first", "largest first", "Z-A"};
	snprintf(out,
			 cap,
			 "Sorted by %s, %s",
			 names[g.sortKey],
			 g.sortDesc ? desc[g.sortKey] : asc[g.sortKey]);
}

static void FilesStatusPaint(ChWidget* wid, ChWindow* win) {
	if (wid->w <= 0 || wid->h <= 0)
		return;
	ChCanvas* c = win->canv;
	FilesFill(c, wid->x, wid->y, wid->w, wid->h, COL_STATUS_BG);
	FilesFill(c, wid->x, wid->y, wid->w, 1, COL_TOOLBAR_EDGE);

	const int font = 12;
	char left[160], right[96];
	uint32_t leftCol = COL_TEXT_DIM;
	int files = g.entryCount - g.dirCount;

	if (g.notice[0]) {
		FilesStrCopy(left, g.notice, sizeof(left));
		leftCol = COL_WARNING;
	} else if (g.searchLen) {
		snprintf(left,
				 sizeof(left),
				 "%d of %d %s match \"%s\"",
				 g.viewCount,
				 g.entryCount,
				 g.entryCount == 1 ? "item" : "items",
				 g.search);
	} else if (g.entryCount == 0) {
		FilesStrCopy(left, "Empty folder", sizeof(left));
	} else {
		snprintf(left,
				 sizeof(left),
				 "%d %s  -  %d %s, %d %s",
				 g.entryCount,
				 g.entryCount == 1 ? "item" : "items",
				 g.dirCount,
				 g.dirCount == 1 ? "folder" : "folders",
				 files,
				 files == 1 ? "file" : "files");
	}

	FilesEntry* sel = FilesSelectedEntry();
	if (sel) {
		char size[24];
		if (sel->isDir)
			FilesStrCopy(size, "Folder", sizeof(size));
		else
			FilesFormatSize(sel->size, size, sizeof(size));
		snprintf(right, sizeof(right), "%s  -  %s", sel->name, size);
	} else {
		FilesSortLabel(right, sizeof(right));
	}

	/* both sides get their natural width when it fits; otherwise the
	 * right side is capped at half and the left side takes the rest */
	const int gap = 24;
	int avail = wid->w - 24;
	int leftW = FilesTextWidth(left, font);
	int rightW = FilesTextWidth(right, font);
	int leftMax = leftW, rightMax = rightW;
	if (leftW + gap + rightW > avail) {
		rightMax = (avail - gap) / 2;
		if (rightMax > rightW)
			rightMax = rightW;
		leftMax = avail - gap - rightMax;
		if (leftMax > leftW) {
			leftMax = leftW;
			rightMax = avail - gap - leftMax;
		}
	}
	char lt[160], rt[96];
	FilesElide(left, font, leftMax, lt, sizeof(lt));
	FilesElide(right, font, rightMax, rt, sizeof(rt));

	ChRect clip;
	clip.y = wid->y + 1;
	clip.h = wid->h - 1;
	int baseline = FilesBaseline(clip.y, clip.h, font);

	clip.x = wid->x + 12;
	clip.w = leftMax;
	FilesText(lt, clip.x, baseline, font, leftCol, &clip);

	int rw = rt[0] ? FilesTextWidth(rt, font) : 0;
	clip.x = wid->x + wid->w - 12 - rw;
	clip.w = rw;
	FilesText(rt, clip.x, baseline, font, COL_TEXT_DIM, &clip);
}

static FilesStatusBar* FilesCreateStatusBar(void) {
	FilesStatusBar* sb = (FilesStatusBar*)malloc(sizeof(FilesStatusBar));
	if (!sb)
		return NULL;
	memset(sb, 0, sizeof(FilesStatusBar));
	sb->base.w = FILES_WIN_W;
	sb->base.h = FILES_STATUS_H; /* preferred height for the BOTTOM region */
	sb->base.ChPaintHandler = FilesStatusPaint;
	sb->base.ChDestroy = FilesFreeWidget;
	return sb;
}

/* ------------------------------------------------------------------------ */
/*  ListView wrappers                                                       */
/* ------------------------------------------------------------------------ */

static bool FilesListTooSmall(ListView* lv) {
	return lv->base.w < FILES_SCROLLBAR_W * 4 ||
		   lv->base.h < lv->headerHeight + FILES_SCROLLBAR_W + lv->itemHeight;
}

static void FilesListPaint(ChWidget* wid, ChWindow* win) {
	ListView* lv = (ListView*)wid;
	if (wid->w <= 0 || wid->h <= 0)
		return;
	if (FilesListTooSmall(lv)) {
		FilesFill(win->canv, wid->x, wid->y, wid->w, wid->h, COL_LV_BG);
		return;
	}
	/* the ListView never paints the two squares beside its scrollbars */
	int sb = FILES_SCROLLBAR_W;
	FilesFill(win->canv, wid->x + wid->w - sb, wid->y, sb, lv->headerHeight, COL_LV_HEADER);
	FilesFill(win->canv, wid->x + wid->w - sb, wid->y + wid->h - sb, sb, sb, COL_LV_HEADER);

	g.listPaint(wid, win);

	if (lv->itemCount == 0) {
		char msg[96];
		if (g.searchLen)
			snprintf(msg, sizeof(msg), "No items match \"%s\"", g.search);
		else
			FilesStrCopy(msg, "This folder is empty", sizeof(msg));
		ChRect clip;
		clip.x = wid->x;
		clip.y = wid->y + lv->headerHeight;
		clip.w = lv->viewport_width;
		clip.h = lv->viewport_height;
		int tw = FilesTextWidth(msg, 14);
		int tx = clip.x + (clip.w - tw) / 2;
		FilesText(msg, tx, FilesBaseline(clip.y, clip.h / 2, 14) + 10, 14, COL_TEXT_FAINT, &clip);
	}
}

static void FilesListMouse(ChWidget* wid, ChWindow* win, int x, int y, int button) {
	ListView* lv = (ListView*)wid;
	if (g.resizing || FilesListTooSmall(lv))
		return;
	bool scroll =
		(button == DEODHAI_MOUSE_MSG_SCROLL_UP || button == DEODHAI_MOUSE_MSG_SCROLL_DOWN);
	bool down = !scroll && button != 0;
	if (down && !g.listDown) {
		g.typeLen = 0;
		if (g.focus != FOCUS_LIST)
			FilesSetFocus(FOCUS_LIST);
	}
	if (!scroll)
		g.listDown = down;

	int before = lv->selectedIndex;
	g.listMouse(wid, win, x, y, button); /* may open a folder via onItemAction */
	if (lv->selectedIndex != before)
		FilesRepaintStatus();
}

static void FilesListDestroy(ChWidget* wid, ChWindow* win) {
	(void)win;
	ListView* lv = (ListView*)wid;
	free(lv->item);
	free(lv);
}

static ListView* FilesCreateList(void) {
	ListView* lv = ChListViewCreate(0, 0, FILES_WIN_W, FILES_WIN_H, FILES_ROW_H);
	if (!lv)
		return NULL;
	ListViewAddColumn(lv, "Name", FILES_COL_NAME_MIN, 120);
	ListViewAddColumn(lv, "Date Modified", FILES_COL_DATE_W, 110);
	ListViewAddColumn(lv, "Size", FILES_COL_SIZE_W, 60);
	ListViewAddColumn(lv, "Kind", FILES_COL_KIND_W, 70);
	ListViewSetActionHandler(lv, FilesOnListAction);
	g.listPaint = lv->base.ChPaintHandler;
	g.listMouse = lv->base.ChMouseEvent;
	lv->base.ChPaintHandler = FilesListPaint;
	lv->base.ChMouseEvent = FilesListMouse;
	lv->base.ChDestroy = FilesListDestroy;
	return lv;
}

/* ------------------------------------------------------------------------ */
/*  Repaint helpers                                                         */
/* ------------------------------------------------------------------------ */

static bool FilesCanPaint(void) {
	return g.ready && !g.resizing && !g.closing && g.win && g.win->canv && g.win->canv->buffer;
}

static void FilesRepaintWidget(ChWidget* w) {
	if (!FilesCanPaint() || !w || !w->visible || w->w <= 0 || w->h <= 0 || !w->ChPaintHandler)
		return;
	w->ChPaintHandler(w, g.win);
	ChWindowUpdate(g.win, w->x, w->y, w->w, w->h, 0, 1);
}

static void FilesRepaintList(void) {
	FilesRepaintWidget((ChWidget*)g.list);
}

static void FilesRepaintStatus(void) {
	FilesRepaintWidget((ChWidget*)g.status);
}

static void FilesRepaintSidebar(void) {
	FilesRepaintWidget((ChWidget*)g.sidebar);
}

static void FilesRepaintToolbar(void) {
	FilesRepaintWidget((ChWidget*)g.toolbar);
}

static void FilesRepaintToolbarElement(int id) {
	if (!FilesCanPaint() || id == TB_NONE)
		return;
	FilesToolbar* tb = g.toolbar;
	if (id == TB_SEARCH_CLEAR)
		id = TB_SEARCH;
	ChRect* r = &tb->r[id];
	if (r->w <= 0 || r->h <= 0)
		return;
	FilesToolbarPaintElement(tb, id);
	ChWindowUpdate(g.win, r->x, r->y, r->w, r->h, 0, 1);
}

/* ------------------------------------------------------------------------ */
/*  Focus and keyboard                                                      */
/* ------------------------------------------------------------------------ */

static void FilesSetFocus(int focus) {
	if (focus == g.focus)
		return;
	if (focus == FOCUS_SEARCH && g.toolbar->r[TB_SEARCH].w <= 0)
		return; /* window too narrow for the search box */
	if (focus == FOCUS_PATH && g.toolbar->r[TB_PATH].w <= 0)
		return;
	int old = g.focus;
	g.focus = focus;
	g.typeLen = 0;
	if (focus == FOCUS_PATH) {
		FilesStrCopy(g.edit, g.path, sizeof(g.edit));
		g.editLen = (int)strlen(g.edit);
	}
	if (old == FOCUS_PATH || focus == FOCUS_PATH)
		FilesRepaintToolbarElement(TB_PATH);
	if (old == FOCUS_SEARCH || focus == FOCUS_SEARCH)
		FilesRepaintToolbarElement(TB_SEARCH);
}

static void FilesCommitPathEdit(void) {
	char target[FILES_PATH_MAX];
	FilesStrCopy(target, g.edit, sizeof(target));
	FilesSetFocus(FOCUS_LIST);
	if (target[0])
		FilesNavigate(target, true, NULL);
}

static void FilesHandleTextKey(int code, char c) {
	int sc = code & SC_MASK;
	bool path = (g.focus == FOCUS_PATH);
	char* buf = path ? g.edit : g.search;
	int* len = path ? &g.editLen : &g.searchLen;
	int cap = path ? (int)sizeof(g.edit) : (int)sizeof(g.search);
	bool changed = false;

	switch (sc) {
	case SC_ESC:
		if (!path && *len)
			FilesClearSearch();
		else
			FilesSetFocus(FOCUS_LIST);
		return;
	case SC_ENTER:
		if (path) {
			FilesCommitPathEdit();
		} else {
			FilesSetFocus(FOCUS_LIST);
			if (g.list->itemCount)
				FilesSelectRow(0, true, true);
		}
		return;
	case SC_TAB:
		FilesSetFocus(FOCUS_LIST);
		return;
	case SC_BACKSPACE:
		if (*len > 0) {
			buf[--(*len)] = '\0';
			changed = true;
		}
		break;
	default:
		/* only trust arrows when the driver tags them as extended, so the
		 * numeric keypad keeps typing digits */
		if ((code & SC_EXT_MASK) == SC_EXT_FLAG) {
			if (!path && sc == SC_DOWN) {
				FilesSetFocus(FOCUS_LIST);
				if (g.list->itemCount)
					FilesSelectRow(0, true, true);
			}
			return;
		}
		if (c >= 0x20 && c < 0x7F && *len < cap - 1) {
			buf[(*len)++] = c;
			buf[*len] = '\0';
			changed = true;
		}
		break;
	}

	if (!changed)
		return;
	if (path)
		FilesRepaintToolbarElement(TB_PATH);
	else
		FilesApplySearch();
}

/* Ctrl shortcuts -- work from the list and from the text fields */
static bool FilesHandleCommand(int sc, int lc) {
	switch (sc) {
	case SC_UP:
		FilesGoUp();
		return true;
	case SC_DOWN:
		FilesOpenRow(g.list->selectedIndex);
		return true;
	default:
		break;
	}
	switch (lc) {
	case 'l':
		FilesSetFocus(FOCUS_PATH);
		return true;
	case 'f':
		FilesSetFocus(FOCUS_SEARCH);
		return true;
	case 'r':
		FilesRefresh();
		return true;
	case 'b':
		FilesToggleSidebar();
		return true;
	case 'i':
		FilesShowInfo();
		return true;
	case 'o':
		FilesOpenRow(g.list->selectedIndex);
		return true;
	case '[':
		FilesGoBack();
		return true;
	case ']':
		FilesGoForward();
		return true;
	case '1':
	case '2':
	case '3':
	case '4':
		FilesSetSort(lc - '1');
		return true;
	case 'q':
	case 'w':
		ChWindowCloseWindow(g.win); /* does not return */
		return true;
	default:
		return false;
	}
}

static void FilesHandleListKey(int code, char c, bool alt) {
	int sc = code & SC_MASK;
	ListView* lv = g.list;
	int rows = lv->itemCount;
	int sel = lv->selectedIndex;
	int page = lv->itemHeight > 0 ? lv->viewport_height / lv->itemHeight - 1 : 1;
	if (page < 1)
		page = 1;

	if (alt) {
		switch (sc) {
		case SC_LEFT:
			FilesGoBack();
			break;
		case SC_RIGHT:
			FilesGoForward();
			break;
		case SC_UP:
			FilesGoUp();
			break;
		case SC_DOWN:
			FilesOpenRow(sel);
			break;
		default:
			break;
		}
		return;
	}

	switch (sc) {
	case SC_UP:
		if (rows)
			FilesSelectRow(sel < 0 ? rows - 1 : sel - 1, true, true);
		return;
	case SC_DOWN:
		if (rows)
			FilesSelectRow(sel < 0 ? 0 : sel + 1, true, true);
		return;
	case SC_HOME:
		if (rows)
			FilesSelectRow(0, true, true);
		return;
	case SC_END:
		if (rows)
			FilesSelectRow(rows - 1, true, true);
		return;
	case SC_PGUP:
		if (rows)
			FilesSelectRow(sel < 0 ? 0 : sel - page, true, true);
		return;
	case SC_PGDN:
		if (rows)
			FilesSelectRow(sel < 0 ? 0 : sel + page, true, true);
		return;
	case SC_ENTER:
		FilesOpenRow(sel);
		return;
	case SC_BACKSPACE:
		FilesGoUp();
		return;
	case SC_F5:
		FilesRefresh();
		return;
	case SC_TAB:
		FilesSetFocus(FOCUS_SEARCH);
		return;
	case SC_ESC:
		if (g.searchLen) {
			FilesClearSearch();
		} else if (sel >= 0) {
			lv->selectedIndex = -1;
			lv->force_full_redraw = true;
			FilesRepaintList();
			FilesRepaintStatus();
		}
		return;
	case SC_LEFT:
	case SC_RIGHT:
		return;
	default:
		break;
	}

	if (c > 0x20 && c < 0x7F)
		FilesTypeAhead(c);
}

static void FilesHandleKey(int code) {
	ChitralekhaProcessKey(code);
	if (g.resizing || (code & SC_RELEASE))
		return;
	int sc = code & SC_MASK;
	if (sc == SC_CTRL || sc == SC_LSHIFT || sc == SC_RSHIFT || sc == SC_ALT || sc == SC_CAPSLOCK)
		return;

	bool ctrl = ChitralekhaKeyGetCTRL();
	bool alt = ChitralekhaKeyGetAlt();
	char c = ChitralekhaKeyToASCII(code);

	if (ctrl) {
		FilesHandleCommand(sc, FilesLower((unsigned char)c));
		return;
	}
	if (g.focus != FOCUS_LIST) {
		FilesHandleTextKey(code, c);
		return;
	}
	FilesHandleListKey(code, c, alt);
}

/* ------------------------------------------------------------------------ */
/*  Layout and resize                                                       */
/* ------------------------------------------------------------------------ */

static void FilesBuildLayouts(void) {
	int W = g.win->info->width, H = g.win->info->height;
	g.rootLayout = ChCreateBorderLayout(1, FILES_TITLEBAR_H, W - 2, H - FILES_TITLEBAR_H - 1, 0, 0);
	g.mainLayout = ChCreateBorderLayout(0, 0, 0, 0, 0, 0);
	g.bodyLayout = ChCreateBorderLayout(0, 0, 0, 0, 1, 0); /* 1px gap = divider */

	ChBorderLayoutAdd(g.bodyLayout, (ChWidget*)g.sidebar, CH_BORDER_ALIGN_LEFT);
	ChBorderLayoutAdd(g.bodyLayout, (ChWidget*)g.list, CH_BORDER_ALIGN_CENTER);

	ChBorderLayoutAdd(g.mainLayout, (ChWidget*)g.toolbar, CH_BORDER_ALIGN_TOP);
	ChBorderLayoutAdd(g.mainLayout, (ChWidget*)g.status, CH_BORDER_ALIGN_BOTTOM);
	ChBorderLayoutAdd(g.mainLayout, (ChWidget*)g.bodyLayout, CH_BORDER_ALIGN_CENTER);

	ChBorderLayoutAdd(g.rootLayout, (ChWidget*)g.menubar, CH_BORDER_ALIGN_TOP);
	ChBorderLayoutAdd(g.rootLayout, (ChWidget*)g.mainLayout, CH_BORDER_ALIGN_CENTER);

	ChBorderLayoutSetPreferred(g.rootLayout, (ChWidget*)g.menubar, 0, FILES_MENUBAR_H);
	ChBorderLayoutSetPreferred(g.mainLayout, (ChWidget*)g.toolbar, 0, FILES_TOOLBAR_H);
	ChBorderLayoutSetPreferred(g.mainLayout, (ChWidget*)g.status, 0, FILES_STATUS_H);
}

/* frees the layout tree only -- child widgets belong to the window */
static void FilesLayoutFree(ChBorderLayout* l) {
	if (!l)
		return;
	while (l->items->pointer) {
		ChBorderItem* it = (ChBorderItem*)list_remove(l->items, 0);
		if (!it)
			break;
		if (it->widget && it->widget->type == CH_WIDEGT_BORDERLAYOUT)
			FilesLayoutFree((ChBorderLayout*)it->widget);
		free(it);
	}
	free(l->items);
	free(l);
}

/* O(1): everything is derived from the window size */
static void FilesRelayout(void) {
	ChWindow* win = g.win;
	int W = win->info->width;
	int H = win->info->height;

	/* ChCreateWindow places close / maximize / minimize once; keep them
	 * glued to the right edge (created in that order, 20px apart) */
	for (unsigned i = 0; i < win->GlobalControls->pointer; i++) {
		ChWinGlobalControl* ctl = (ChWinGlobalControl*)list_get_at(win->GlobalControls, i);
		if (ctl)
			ctl->x = W - 25 - 20 * (int)i;
	}

	FilesApplySidebarVisibility(W);

	int cw = W - 2;
	int ch = H - FILES_TITLEBAR_H - 1;
	ChBorderLayoutResize(g.rootLayout, 1, FILES_TITLEBAR_H, cw > 0 ? cw : 0, ch > 0 ? ch : 0);

	FilesListSyncGeometry();
	FilesToolbarLayout(g.toolbar);
}

/* compositor finished resizing: map the new back buffer and lay out again */
static void FilesHandleReinitBuffer(int backbufkey) {
	ChWindow* win = g.win;
	int id = _KeCreateSharedMem((uint16_t)backbufkey, 0, 0);
	void* buffer = _KeObtainSharedMem(id, 0, 0);
	win->app->backbufkey = backbufkey;
	win->app->fb = buffer;
	win->buffer = (uint32_t*)buffer;
	win->info->windowReady = 1;

	_KeProcessSleep(50);
	ChDeAllocateBuffer(win->canv);
	win->canv->canvasWidth = win->info->width;
	win->canv->canvasHeight = win->info->height;
	if (!ChAllocateBuffer(win->canv)) {
		win->canv->buffer = NULL; /* every painter checks for this */
		win->canv->bufferSz = 0;
		return;
	}
	g.resizing = false;
	FilesRelayout();
	ChWindowPaint(win);
}

static void FilesHandleMouseLeave(void) {
	FilesToolbar* tb = g.toolbar;
	int hot = tb->hot;
	tb->hot = TB_NONE;
	tb->pressed = TB_NONE;
	tb->hotCrumb = -1;
	tb->prevDown = false;
	FilesRepaintToolbarElement(hot);
	tb->base.hover = false;

	ListView* lv = g.list;
	lv->base.hover = false;
	if (lv->hoverIndex != -1) {
		lv->hoverIndex = -1;
		lv->force_full_redraw = true;
		FilesRepaintList();
	}
	g.listDown = false;

	ChSidebar* sb = g.sidebar;
	sb->base.hover = false;
	if (sb->hoverItemIdx != -1 || sb->hoverSectionIdx != -1) {
		sb->hoverItemIdx = -1;
		sb->hoverSectionIdx = -1;
		FilesRepaintSidebar();
	}
}

/* ------------------------------------------------------------------------ */
/*  Menus                                                                   */
/* ------------------------------------------------------------------------ */

static void MenuOpen(ChWidget* w, ChWindow* win) {
	(void)w;
	(void)win;
	FilesOpenRow(g.list->selectedIndex);
}
static void MenuInfo(ChWidget* w, ChWindow* win) {
	(void)w;
	(void)win;
	FilesShowInfo();
}
static void MenuRefresh(ChWidget* w, ChWindow* win) {
	(void)w;
	(void)win;
	FilesRefresh();
}
static void MenuClose(ChWidget* w, ChWindow* win) {
	(void)w;
	(void)win;
	ChWindowCloseWindow(g.win);
}
static void MenuBack(ChWidget* w, ChWindow* win) {
	(void)w;
	(void)win;
	FilesGoBack();
}
static void MenuForward(ChWidget* w, ChWindow* win) {
	(void)w;
	(void)win;
	FilesGoForward();
}
static void MenuUp(ChWidget* w, ChWindow* win) {
	(void)w;
	(void)win;
	FilesGoUp();
}
static void MenuGoTo(ChWidget* w, ChWindow* win) {
	(void)w;
	(void)win;
	FilesSetFocus(FOCUS_PATH);
}
static void MenuSearch(ChWidget* w, ChWindow* win) {
	(void)w;
	(void)win;
	FilesSetFocus(FOCUS_SEARCH);
}
static void MenuSortName(ChWidget* w, ChWindow* win) {
	(void)w;
	(void)win;
	FilesSetSort(SORT_NAME);
}
static void MenuSortDate(ChWidget* w, ChWindow* win) {
	(void)w;
	(void)win;
	FilesSetSort(SORT_DATE);
}
static void MenuSortSize(ChWidget* w, ChWindow* win) {
	(void)w;
	(void)win;
	FilesSetSort(SORT_SIZE);
}
static void MenuSortKind(ChWidget* w, ChWindow* win) {
	(void)w;
	(void)win;
	FilesSetSort(SORT_KIND);
}
static void MenuSidebar(ChWidget* w, ChWindow* win) {
	(void)w;
	(void)win;
	FilesToggleSidebar();
}
static void MenuAbout(ChWidget* w, ChWindow* win) {
	(void)w;
	(void)win;
	ChMessageBox* mb = ChCreateMessageBox(g.win,
										  (char*)"About Files",
										  (char*)"Files v2.0 -- file manager for XenevaOS",
										  MSGBOX_TYPE_ONLYCLOSE,
										  MSGBOX_ICON_SUCCESS);
	ChMessageBoxShow(mb);
}

typedef struct {
	const char* title;
	void (*action)(ChWidget* w, ChWindow* win);
} FilesMenuItem;

static void FilesAddMenu(const char* title, const FilesMenuItem* items, int count) {
	ChMenuButton* button = ChCreateMenubutton(g.menubar, (char*)title);
	ChMenubarAddButton(g.menubar, button);
	ChPopupMenu* pm = ChCreatePopupMenu(g.win, 0);
	for (int i = 0; i < count; i++) {
		ChMenuItem* mi = ChCreateMenuItem((char*)items[i].title, pm);
		mi->wid.ChActionHandler = items[i].action;
	}
	ChMenuButtonAddMenu(button, pm);
}

static void FilesBuildMenus(void) {
	static const FilesMenuItem fileMenu[] = {
		{"Open  (Enter)", MenuOpen},
		{"Get Info  (Ctrl+I)", MenuInfo},
		{"Refresh  (F5)", MenuRefresh},
		{"Close  (Ctrl+Q)", MenuClose},
	};
	static const FilesMenuItem goMenu[] = {
		{"Back  (Alt+Left)", MenuBack},
		{"Forward  (Alt+Right)", MenuForward},
		{"Enclosing Folder  (Alt+Up)", MenuUp},
		{"Go to Location...  (Ctrl+L)", MenuGoTo},
		{"Search  (Ctrl+F)", MenuSearch},
	};
	static const FilesMenuItem viewMenu[] = {
		{"Sort by Name  (Ctrl+1)", MenuSortName},
		{"Sort by Date  (Ctrl+2)", MenuSortDate},
		{"Sort by Size  (Ctrl+3)", MenuSortSize},
		{"Sort by Kind  (Ctrl+4)", MenuSortKind},
		{"Show/Hide Sidebar  (Ctrl+B)", MenuSidebar},
	};
	static const FilesMenuItem helpMenu[] = {
		{"About Files", MenuAbout},
	};

	g.menubar = ChCreateMenubar(g.win);
	g.menubar->wid.h = FILES_MENUBAR_H;
	FilesAddMenu("File", fileMenu, (int)(sizeof(fileMenu) / sizeof(fileMenu[0])));
	FilesAddMenu("Go", goMenu, (int)(sizeof(goMenu) / sizeof(goMenu[0])));
	FilesAddMenu("View", viewMenu, (int)(sizeof(viewMenu) / sizeof(viewMenu[0])));
	FilesAddMenu("Help", helpMenu, (int)(sizeof(helpMenu) / sizeof(helpMenu[0])));
}

/* ------------------------------------------------------------------------ */
/*  Cleanup                                                                 */
/* ------------------------------------------------------------------------ */

/*
 * FilesOnClose -- runs first inside ChWindowCloseWindow. Releases what the
 * window does not own. The leaf widgets are freed afterwards by
 * ChFreeWindowResources through their ChDestroy handlers (menubar: library;
 * toolbar, status bar, sidebar, list view: ours, above).
 */
static void FilesOnClose(ChWindow* win) {
	(void)win;
	if (g.closing)
		return;
	g.closing = true;

	free(g.entries);
	g.entries = NULL;
	g.entryCount = g.entryCap = 0;

	free(g.order); /* one block: order + view + scratch */
	g.order = g.view = g.scratch = NULL;
	g.indexCap = 0;

	FilesLayoutFree(g.rootLayout); /* recursively frees main + body */
	g.rootLayout = g.mainLayout = g.bodyLayout = NULL;

	for (int i = 0; i < ICON_COUNT; i++) {
		FilesIconFree(g.icons[i]);
		g.icons[i] = NULL;
	}
}

/* ------------------------------------------------------------------------ */
/*  Legacy entry points used by extmngr.cpp (kept so it still links)        */
/* ------------------------------------------------------------------------ */

bool VerifyDocExtension(char* filename, char* extension) {
	size_t fl = strlen(filename), el = strlen(extension);
	return fl >= el && strcmp(filename + fl - el, extension) == 0;
}

char* FileManagerGetCurrentPath() {
	return g.path;
}

/* ------------------------------------------------------------------------ */
/*  Message loop                                                            */
/* ------------------------------------------------------------------------ */

static void FilesHandleMessage(PostEvent* e) {
	switch (e->type) {
	case DEODHAI_REPLY_MOUSE_EVENT: {
		if (g.resizing)
			break;
		int handle = e->dword4;
		if (e->dword5 == WINDOW_HANDLE_TYPE_NORMAL) {
			ChWindow* w = ChGetWindowByHandle(g.win, handle);
			if (w)
				ChWindowHandleMouse(w, e->dword, e->dword2, e->dword3);
		} else if (e->dword5 == WINDOW_HANDLE_TYPE_POPUP) {
			ChWindow* pw = ChGetPopupWindowByHandle(g.win, handle);
			if (pw)
				ChPopupWindowHandleMouse(pw, e->dword, e->dword2, e->dword3);
		}
		break;
	}
	case DEODHAI_REPLY_TOUCH_EVENT: {
		if (g.resizing)
			break;
		if (e->dword5 == WINDOW_HANDLE_TYPE_NORMAL) {
			ChWindow* w = ChGetWindowByHandle(g.win, e->dword4);
			if (w)
				ChWindowHandleTouch(w, e->dword, e->dword2, e->dword3);
		}
		break;
	}
	case DEODHAI_REPLY_KEY_EVENT:
		FilesHandleKey(e->dword);
		break;
	case DEODHAI_REPLY_FOCUS_CHANGED: {
		int handle = e->dword2;
		ChWindow* w = ChGetWindowByHandle(g.win, handle);
		if (!w)
			break;
		if (g.resizing && w == g.win)
			w->focused = e->dword ? true : false; /* repainted after the resize */
		else
			ChWindowHandleFocus(w, e->dword ? true : false, handle);
		break;
	}
	case DEODHAI_REPLY_MOUSE_LEAVE:
		if (!g.resizing)
			FilesHandleMouseLeave();
		break;
	case DEODHAI_REPLY_DESTROY_BUFFER:
		/* the compositor waits ~1s for this ack before it marks the window
		 * non-resizable; from here until REINIT nothing may paint */
		g.resizing = true;
		ChWindowHandleDestroyBuffer(g.win);
		break;
	case DEODHAI_REPLY_REINIT_BUFFER:
		FilesHandleReinitBuffer(e->dword);
		break;
	default:
		break;
	}
	memset(e, 0, sizeof(PostEvent));
}

/*
 * main -- main entry
 */
int main(int argc, char* argv[]) {
	memset(&g, 0, sizeof(g));
	g.histPos = -1;
	g.activeLoc = -1;
	g.focus = FOCUS_LIST;
	g.sortKey = SORT_NAME;
	g.sidebarVisible = true;
	FilesStrCopy(g.path, "/", sizeof(g.path));

	g.app = ChitralekhaStartApp(argc, argv);
	g.win = ChCreateWindow(g.app,
						   WINDOW_FLAG_MOVABLE,
						   (char*)"Files",
						   FILES_WIN_X,
						   FILES_WIN_Y,
						   FILES_WIN_W,
						   FILES_WIN_H);
	g.win->color = COL_DIVIDER;
	g.win->ChCloseWin = FilesOnClose;

	FilesLoadIcons();
	FilesBuildMenus();
	g.toolbar = FilesCreateToolbar();
	g.status = FilesCreateStatusBar();
	g.sidebar = FilesCreateSidebar();
	g.list = FilesCreateList();
	if (!g.toolbar || !g.status || !g.sidebar || !g.list || !g.menubar) {
		_KePrint("Files: out of memory while creating the UI \r\n");
		ChWindowCloseWindow(g.win);
	}

	/* window list order == paint order */
	ChWindowAddWidget(g.win, (ChWidget*)g.menubar);
	ChWindowAddWidget(g.win, (ChWidget*)g.toolbar);
	ChWindowAddWidget(g.win, (ChWidget*)g.sidebar);
	ChWindowAddWidget(g.win, (ChWidget*)g.list);
	ChWindowAddWidget(g.win, (ChWidget*)g.status);

	FilesBuildLayouts();
	FilesRelayout();

	/* "files /some/dir" opens that folder. Dynamically linked builds get
	 * argv[0] = the executable (xeldr prepends it) while static images get
	 * the arguments as passed, so take the first argument that is a folder. */
	const char* start = "/";
	for (int i = 0; argv && i < argc; i++) {
		if (!argv[i] || argv[i][0] != '/')
			continue;
		uint8_t ext = FilesLookupExt(argv[i]);
		if (ext != FILES_EXT_NONE && g_extTable[ext].kind == KIND_APP)
			continue; /* the executable itself, not a folder */
		int fd = _KeOpenDir(argv[i]);
		if (fd >= 0) {
			_KeCloseFile(fd);
			start = argv[i];
			break;
		}
	}
	if (!FilesNavigate(start, true, NULL) && start[1] != '\0')
		FilesNavigate("/", true, NULL);

	g.ready = true;
	ChWindowPaint(g.win);
	ChWindowBroadcastIcon(g.app, (char*)"/icons/file.bmp");

	PostEvent e;
	memset(&e, 0, sizeof(PostEvent));

	/* message boxes longjmp back here when they close */
	setjmp(g.win->jump);
	while (1) {
		int err = _KeFileIoControl(g.app->postboxfd, POSTBOX_GET_EVENT, &e);
		FilesHandleMessage(&e);
		if (err == POSTBOX_NO_EVENT)
			_KePauseThread();
	}
}