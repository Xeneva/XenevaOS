/**
 * doomgeneric platform port for XenevaOS
 * Implements the doomgeneric API using XenevaOS + Chitralekha APIs.
 *
 * Notes:
 * - The framebuffer (DG_ScreenBuffer) is owned by doomgeneric.c
 *   (allocated in doomgeneric_Create). DG_Init only falls back to
 *   allocating it if it is still NULL (e.g. direct DG_Init use).
 * - Timing uses _KeGetCurrentMS (monotonic milliseconds).
 * - Input polls the Chitralekha app postbox for DEODHAI_REPLY_KEY_EVENT
 *   and maps Xeneva keycodes to Doom keycodes (arrows/fire/use/esc/enter
 *   handled explicitly, the rest via ChitralekhaKeyToASCII).
 */

#include "doomgeneric.h"
// NOTE: doomkeys.h intentionally NOT included here -- its KEY_* macros
// (KEY_ESCAPE etc.) collide with Chitralekha keycode.h's enum. Doom key
// codes used below (from doomkeys.h): up 0xad, down 0xaf, left 0xac,
// right 0xae, esc 27, enter 13, fire 0xa3, use 0xa2, rshift 0x80+0x36.
#define DOOM_KEY_UPARROW	0xad
#define DOOM_KEY_DOWNARROW	0xaf
#define DOOM_KEY_LEFTARROW	0xac
#define DOOM_KEY_RIGHTARROW 0xae
#define DOOM_KEY_ESCAPE		27
#define DOOM_KEY_ENTER		13
#define DOOM_KEY_FIRE		0xa3
#define DOOM_KEY_USE		0xa2
#define DOOM_KEY_RSHIFT		(0x80 + 0x36)
#include <_xeneva.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <chitralekha.h>
#include <widgets/base.h>
#include <widgets/window.h>
#include <keycode.h>
#include <sys/_keproc.h>
#include <sys/_kefile.h>
#include <sys/_ketime.h>
#include <sys/iocodes.h>

// Window / canvas / app state
static ChitralekhaApp* g_app = NULL;
static ChWindow* s_window = NULL;
static ChCanvas* s_canvas = NULL;
static int s_initialized = 0;

extern "C" {
void I_Quit(void);
}
// ChWindowPaintTitlebar has C++ linkage in libChitralekha (no extern "C"
// on its definition), so it must be declared without extern "C" here to
// mangle identically.
void ChWindowPaintTitlebar(ChWindow* win);

// Chrome geometry mirrors ChCreateWindow (titlebar 26px tall, 20px
// buttons at top-right: close, maximise, minimise from the right edge).
// Hit-tested directly instead of via ChWindowHandleMouse: the lib's
// control-struct dispatch faults on maximise clicks, and Doom only needs
// these three actions anyway.
#define DOOM_CHROME_H	   26
#define DOOM_CHROME_BTN_Y0 2
#define DOOM_CHROME_BTN_Y1 24

// Returns true if the click was consumed by chrome.
static int DG_HandleChromeClick(int x, int y) {
	if (!s_window || !s_window->info)
		return 0;
	int lx = x - s_window->info->x;
	int ly = y - s_window->info->y;
	int ww = s_window->info->width;
	if (ly < 0 || ly >= DOOM_CHROME_H)
		return 0;
	if (lx >= ww - 25 && lx < ww - 5) {
		I_Quit(); // close button; noreturn: exit funcs -> D_Endoom -> exit()
		return 1;
	}
	if (lx >= ww - 65 && lx < ww - 5 && ly >= DOOM_CHROME_BTN_Y0 && ly < DOOM_CHROME_BTN_Y1) {
		if (lx < ww - 45) {
			ChWindowHide(s_window); // minimise button
			return 1;
		}
		// Maximise button: toggles the shared zoom flag for the
		// compositor (stretched-fill zoom honours it); the legacy
		// lib dispatch for this button is never invoked.
		s_window->info->zoomed = !s_window->info->zoomed;
		_KePrint("[doom] MARK zoom=%d\n", (int)s_window->info->zoomed);
		return 1;
	}
	return 0;
}

// Map an input code to a Doom keycode. Returns 0 if unmapped.
// The virtio keyboard driver emits AT Set-1 scancodes: plain makes,
// make|0x80 on release, and 0xE000|make for extended keys (release sets
// 0x80 in the low byte). The Xeneva KEY_ enum cases below are kept for
// robustness but the AT codes are what actually arrive.
static int XeKeyToDoom(int code) {
	switch (code) {
	// AT extended (0xE0-prefixed) keys.
	case 0xE048:
		return DOOM_KEY_UPARROW;
	case 0xE050:
		return DOOM_KEY_DOWNARROW;
	case 0xE04B:
		return DOOM_KEY_LEFTARROW;
	case 0xE04D:
		return DOOM_KEY_RIGHTARROW;
	case 0xE01D:
		return DOOM_KEY_FIRE; // RCtrl
	case 0xE038:
		return DOOM_KEY_USE; // RAlt
	// AT plain scancodes.
	case 0x01:
		return DOOM_KEY_ESCAPE; // Esc
	case 0x0E:
		return KEY_BACKSPACE; // Backspace
	case 0x0F:
		return KEY_TAB; // Tab
	case 0x1C:
		return DOOM_KEY_ENTER; // Enter
	case 0x1D:
		return DOOM_KEY_FIRE; // LCtrl
	case 0x2A:				  // LShift
	case 0x36:
		return DOOM_KEY_RSHIFT; // RShift
	case 0x38:
		return DOOM_KEY_USE; // LAlt
	case KEY_UP:
		return DOOM_KEY_UPARROW;
	case KEY_DOWN:
		return DOOM_KEY_DOWNARROW;
	case KEY_LEFT:
		return DOOM_KEY_LEFTARROW;
	case KEY_RIGHT:
		return DOOM_KEY_RIGHTARROW;
	case KEY_ESCAPE:
		return DOOM_KEY_ESCAPE; // 27
	case KEY_RETURN:
		return DOOM_KEY_ENTER; // 13
	case KEY_LCTRL:
	case KEY_RCTRL:
		return DOOM_KEY_FIRE;
	case KEY_LALT:
	case KEY_RALT:
		return DOOM_KEY_USE;
	case KEY_LSHIFT:
	case KEY_RSHIFT:
		return DOOM_KEY_RSHIFT;
	default:
		break;
	}
	// F1-F10: AT 0x3B-0x44, Doom KEY_Fn = 0x80 + scancode.
	if (code >= 0x3B && code <= 0x44)
		return 0x80 + code;
	// Printable keys: translate scancode -> ASCII.
	{
		char c = ChitralekhaKeyToASCII(code);
		if (c)
			return (unsigned char)c;
	}
	// Raw ASCII passthrough (e.g. space).
	if (code > 0 && code < 128)
		return code;
	return 0;
}

// DG_Init -- Initialize the platform
void DG_Init() {
	// Framebuffer is normally allocated by doomgeneric_Create before
	// calling us; keep a fallback for direct DG_Init callers.
	if (!DG_ScreenBuffer) {
		DG_ScreenBuffer = (pixel_t*)malloc(DOOMGENERIC_RESX * DOOMGENERIC_RESY * 4);
		if (!DG_ScreenBuffer) {
			printf("[doomgeneric] Failed to allocate screen buffer\n");
			return;
		}
		memset(DG_ScreenBuffer, 0, DOOMGENERIC_RESX * DOOMGENERIC_RESY * 4);
	}

	if (!g_app) {
		g_app = ChitralekhaStartApp(0, NULL);
	}

	s_window = ChCreateWindow(g_app,
							  WINDOW_FLAG_MOVABLE,
							  "Doom II: Hell on Earth",
							  60,
							  40,
							  DOOMGENERIC_RESX,
							  DOOMGENERIC_RESY);

	if (s_window) {
		// Reuse the canvas ChCreateWindow already created and buffer-allocated
		// (640x400; ChAllocateBuffer runs inside ChCreateWindow). Creating a
		// second canvas with ChCreateCanvas leaves ->buffer = NULL, and the
		// Chitralekha paint path (icon/mask alpha-blit) dereferences it with
		// no null guard (only ChDrawRect guards), crashing in a NEON row-blit
		// with x0 = NULL + row*640*4 (FAR_EL1=0x5ecc/0x68cc). -- gdb captured
		s_canvas = s_window->canv;
		// NOTE: do NOT reassign s_window->buffer. ChCreateWindow sets it to
		// app->fb (the shared compositor backbuffer, filled in by the
		// blocking ChRequestWindow). ChWindowUpdate copies canv->buffer
		// (local) -> win->buffer (shared) and flags dirty; pointing
		// win->buffer at the canvas makes lfb == canvaddr, skipping the
		// copy so pixels never reach the compositor (invisible window).
		// Diagnostic for the maximise-click crash repro: dump chrome
		// control health at birth (a garbage mouse handler here means the
		// heap was already corrupt at creation). All three MouseEvent
		// pointers must be identical (same handler); print full 64-bit
		// values as hi:lo since only %x is trusted here.
		_KePrint("[doom] MARK controls: n=%d\n",
				 s_window->GlobalControls ? s_window->GlobalControls->pointer : -1);
		if (s_window->GlobalControls) {
			for (int ci = 0; ci < s_window->GlobalControls->pointer; ci++) {
				ChWinGlobalControl* gc =
					(ChWinGlobalControl*)list_get_at(s_window->GlobalControls, ci);
				if (!gc) {
					_KePrint("[doom] MARK control %d: NULL\n", ci);
					continue;
				}
				unsigned long long mh = (unsigned long long)(uintptr_t)gc->ChGlobalMouseEvent;
				unsigned long long ah = (unsigned long long)(uintptr_t)gc->ChGlobalActionEvent;
				_KePrint("[doom] MARK control %d: rect=%d,%d,%d,%d mouse=%x:%x action=%x:%x\n",
						 ci,
						 gc->x,
						 gc->y,
						 gc->w,
						 gc->h,
						 (unsigned)(mh >> 32),
						 (unsigned)mh,
						 (unsigned)(ah >> 32),
						 (unsigned)ah);
			}
		}
		_KePrint("[doom] MARK before ChWindowPaint\n");
		ChWindowBroadcastIcon(g_app, "/icons/doom.bmp");
		ChWindowPaint(s_window);
		_KePrint("[doom] MARK after ChWindowPaint\n");
	}

	s_initialized = 1;
	printf("[doomgeneric] Init complete (%dx%d)\n", DOOMGENERIC_RESX, DOOMGENERIC_RESY);
}

// DG_DrawFrame -- Draw one frame of Doom
void DG_DrawFrame() {
	if (!s_initialized || !DG_ScreenBuffer || !s_canvas || !s_canvas->buffer)
		return;

	// One-time post-init control dump (first presented frame): if these
	// differ from the birth MARK, init (zone/WAD/textures) corrupted them;
	// if they match, corruption happens later (tick loop or click path).
	{
		static int postinitDumped = 0;
		if (!postinitDumped && s_window && s_window->GlobalControls) {
			postinitDumped = 1;
			for (int ci = 0; ci < s_window->GlobalControls->pointer; ci++) {
				ChWinGlobalControl* gc =
					(ChWinGlobalControl*)list_get_at(s_window->GlobalControls, ci);
				if (!gc) {
					_KePrint("[doom] MARK postinit %d: NULL\n", ci);
					continue;
				}
				unsigned long long mh = (unsigned long long)(uintptr_t)gc->ChGlobalMouseEvent;
				_KePrint("[doom] MARK postinit %d: mouse=%x:%x\n",
						 ci,
						 (unsigned)(mh >> 32),
						 (unsigned)mh);
			}
		}
	}

	// Heap-integrity watch: the maximise-click crash showed a control
	// MouseEvent pointer mutated at runtime. Snapshot at first frame;
	// report the first mutation (tick count + old -> new) exactly once.
	{
		static int watchArmed = 0;
		static unsigned long long watchMouse[8];
		static int watchReported = 0;
		if (s_window && s_window->GlobalControls && s_window->GlobalControls->pointer <= 8) {
			if (!watchArmed) {
				watchArmed = 1;
				for (int ci = 0; ci < s_window->GlobalControls->pointer; ci++) {
					ChWinGlobalControl* gc =
						(ChWinGlobalControl*)list_get_at(s_window->GlobalControls, ci);
					watchMouse[ci] = gc ? (unsigned long long)(uintptr_t)gc->ChGlobalMouseEvent : 0;
				}
			} else if (!watchReported) {
				for (int ci = 0; ci < s_window->GlobalControls->pointer; ci++) {
					ChWinGlobalControl* gc =
						(ChWinGlobalControl*)list_get_at(s_window->GlobalControls, ci);
					unsigned long long cur =
						gc ? (unsigned long long)(uintptr_t)gc->ChGlobalMouseEvent : 0;
					if (cur != watchMouse[ci]) {
						watchReported = 1;
						_KePrint("[doom] MARK CORRUPT ctl=%d old=%x:%x new=%x:%x\n",
								 ci,
								 (unsigned)(watchMouse[ci] >> 32),
								 (unsigned)watchMouse[ci],
								 (unsigned)(cur >> 32),
								 (unsigned)cur);
						break;
					}
				}
			}
		}
	}

	DG_StatBegin(DG_STAT_MEMCPY);
	memcpy(
		s_canvas->buffer, DG_ScreenBuffer, (size_t)DOOMGENERIC_RESX * (size_t)DOOMGENERIC_RESY * 4);
	DG_StatEnd(DG_STAT_MEMCPY);

	DG_StatBegin(DG_STAT_UPDATE);
	// The canvas is the whole window surface: repaint the titlebar chrome
	// (rows 0-25) that the game blit just overwrote, then push once.
	ChWindowPaintTitlebar(s_window);

	ChWindowUpdate(s_window, 0, 0, DOOMGENERIC_RESX, DOOMGENERIC_RESY, 1, 0);
	DG_StatEnd(DG_STAT_UPDATE);
}

// DG_SleepMs -- Sleep in milliseconds
void DG_SleepMs(uint32_t ms) {
	_KeProcessSleep(ms);
}

// DG_GetTicksMs -- monotonic milliseconds since boot
uint32_t DG_GetTicksMs() {
	return (uint32_t)_KeGetCurrentMS();
}

// DG_GetKey -- Poll one key event. Returns 1 + fills pressed/key on event.
// Mouse/focus events are routed to the window (so the titlebar buttons
// work) instead of being dropped; the queue is drained until a key
// arrives or it runs dry.
int DG_GetKey(int* pressed, unsigned char* key) {
	PostEvent e;

	if (!pressed || !key)
		return 0;
	*pressed = 0;
	*key = 0;

	if (!g_app)
		return 0;

	for (;;) {
		memset(&e, 0, sizeof(PostEvent));
		int err = _KeFileIoControl(g_app->postboxfd, POSTBOX_GET_EVENT, &e);
		if (err == POSTBOX_NO_EVENT)
			return 0;

		if (e.type == DEODHAI_REPLY_MOUSE_EVENT) {
			if (e.dword3)
				_KePrint("[doom] MARK click x=%d y=%d\n", e.dword, e.dword2);
			// Direct chrome handling (never via ChWindowHandleMouse: its
			// control-struct dispatch is fragile, and Doom only needs
			// close/minimise/maximise-toggle anyway).
			if (s_window && e.dword3)
				DG_HandleChromeClick(e.dword, e.dword2);
			continue;
		}

		if (e.type == DEODHAI_REPLY_FOCUS_CHANGED) {
			if (s_window)
				ChWindowHandleFocus(s_window, e.dword, e.dword2);
			continue;
		}

		if (e.type != DEODHAI_REPLY_KEY_EVENT)
			continue;

		int raw = (int)e.dword;
		int ispress;
		int code;

		// Decode AT press/release: release sets 0x80 (low byte for
		// 0xE0-prefixed extended keys). Normalize to the make code.
		if ((raw & 0xFF00) == 0xE000) {
			code = raw & ~0x80;
			ispress = !(raw & 0x80);
		} else {
			code = raw & 0x7F;
			ispress = !(raw & 0x80);
		}

		// ScrollLock (AT make 0x46): dump frame stats live, swallow the key.
		// (F11/F12 never arrive: the virtio-kbd driver drops Linux codes
		// >= 0x49 missing from its ext_key_map, which includes 0x57/0x58.)
		if (code == 0x46 && ispress) {
			DG_StatReport();
			continue;
		}

		int doomkey = XeKeyToDoom(code);
		if (!doomkey)
			continue;

		*pressed = ispress;
		*key = (unsigned char)(doomkey & 0xFF);
		return 1;
	}
}

// Frame-time stats: per-stage ms totals over the run. Silent until
// DG_StatReport (F12 at runtime, plus quit/error exits). Means resolve
// finely via total/count; min/max stay 1ms-coarse (MS clock).
static uint64_t s_statTotal[DG_STAT_N];
static uint64_t s_statCount[DG_STAT_N];
static uint64_t s_statMin[DG_STAT_N];
static uint64_t s_statMax[DG_STAT_N];
static uint64_t s_statStart[DG_STAT_N];
static int s_statInit = 0;

void DG_StatBegin(int id) {
	if ((unsigned)id >= DG_STAT_N)
		return;
	if (!s_statInit) {
		s_statInit = 1;
		for (int i = 0; i < DG_STAT_N; i++) {
			s_statTotal[i] = 0;
			s_statCount[i] = 0;
			s_statMin[i] = (uint64_t)-1;
			s_statMax[i] = 0;
			s_statStart[i] = 0;
		}
	}
	s_statStart[id] = DG_GetTicksMs();
}

void DG_StatEnd(int id) {
	uint64_t now;
	uint64_t d;
	if ((unsigned)id >= DG_STAT_N || !s_statInit)
		return;
	now = DG_GetTicksMs();
	d = (now >= s_statStart[id]) ? (now - s_statStart[id]) : 0;
	s_statTotal[id] += d;
	s_statCount[id] += 1;
	if (d < s_statMin[id])
		s_statMin[id] = d;
	if (d > s_statMax[id])
		s_statMax[id] = d;
}

void DG_StatReport(void) {
	static const char* names[DG_STAT_N] = {"tick", "scale", "memcpy", "update"};
	uint64_t i;
	if (!s_statInit) {
		printf("[doomstat] no samples yet\n");
		return;
	}
	printf("[doomstat] stage : total_ms samples avg_ms min max\n");
	for (i = 0; i < DG_STAT_N; i++) {
		uint64_t avg = s_statCount[i] ? (s_statTotal[i] / s_statCount[i]) : 0;
		uint64_t mn = (s_statMin[i] == (uint64_t)-1) ? 0 : s_statMin[i];
		printf("[doomstat] %s : %d %d %d %d\n",
			   names[i],
			   (int)s_statTotal[i],
			   (int)s_statCount[i],
			   (int)avg,
			   (int)mn);
		printf("[doomstat]   max=%d\n", (int)s_statMax[i]);
	}
}

// DG_CloseWindow -- tear down the window (called on quit/error exits so
// no ghost window lingers after the process dies).
void DG_CloseWindow(void) {
	DG_StatReport();
	if (s_window) {
		ChWindowCloseWindow(s_window);
		s_window = NULL;
		s_canvas = NULL;
	}
	s_initialized = 0;
}

// DG_SetWindowTitle -- Set the window title
void DG_SetWindowTitle(const char* title) {
	if (title) {
		printf("[doomgeneric] Title: %s\n", title);
	}
}
