/**
 * Host unit test for the XELnch launcher label readability contract.
 *
 * Links the real Process/XELnch/button.cpp against stub Chitralekha and
 * kernel functions, drives LaunchButtonPaint, and asserts:
 *   - exactly one background fill in LAUNCHER_BACKGROUND_COLOR,
 *   - no scrim rect behind the label (ChDrawRectClipped must never fire),
 *   - exactly two text draws: opaque-black shadow at (+1,+1), then the
 *     white face, both centered on the button,
 *   - hover paints the gradient exactly once and keeps the same text order.
 *
 * Built and run by Tests/test_xelnch_label.sh.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "button.h"
#include "launcher.h"
#include "appgrid.h"
#include <chitralekha.h>
#include <color.h>
#include <draw.h>
#include <font.h>
#include <widgets/base.h>
#include <widgets/window.h>

/* Stub font metrics: fixed advance width so centering is predictable. */
#define STUB_FONT_WIDTH 60
#define STUB_FONT_SIZE	11

#define LABEL_SHADOW_COLOR 0xFF000000u
#define LABEL_FACE_COLOR   0xFFFFFFFFu

static struct {
	int rectCount;
	unsigned rectX, rectY, rectW, rectH;
	uint32_t rectCol;
	int clippedRectCount;
	int gradientCount;
	uint32_t gradC1, gradC2;
	int fontSize;
	int textCount;
	struct {
		int x, y;
		uint32_t color;
		const char* text;
	} text[8];
} g;

static void resetCalls(void) {
	memset(&g, 0, sizeof(g));
}

/* C-linkage stubs matching the XE_LIB declarations used by button.cpp. */
extern "C" {

void ChDrawRect(
	ChCanvas* canvas, unsigned x, unsigned y, unsigned w, unsigned h, uint32_t col) {
	(void)canvas;
	g.rectCount++;
	g.rectX = x;
	g.rectY = y;
	g.rectW = w;
	g.rectH = h;
	g.rectCol = col;
}

void ChDrawRectClipped(ChCanvas* canv,
					   unsigned x,
					   unsigned y,
					   unsigned w,
					   unsigned h,
					   ChRect* clip,
					   uint32_t col) {
	(void)canv;
	(void)x;
	(void)y;
	(void)w;
	(void)h;
	(void)clip;
	(void)col;
	g.clippedRectCount++;
}

void ChColorDrawHorizontalGradient(
	ChCanvas* canv, int x, int y, int w, int h, uint32_t c1, uint32_t c2) {
	(void)canv;
	(void)x;
	(void)y;
	(void)w;
	(void)h;
	g.gradientCount++;
	g.gradC1 = c1;
	g.gradC2 = c2;
}

void ChDrawPixel(ChCanvas* canvas, int x, int y, uint32_t color) {
	(void)canvas;
	(void)x;
	(void)y;
	(void)color;
}

void ChFontSetSize(ChFont* font, int size) {
	(void)font;
	g.fontSize = size;
}

int64_t ChFontGetWidth(ChFont* font, char* string) {
	(void)font;
	(void)string;
	return STUB_FONT_WIDTH;
}

int64_t ChFontGetHeight(ChFont* font, char* string) {
	(void)font;
	(void)string;
	return 14;
}

int ChFontDrawTextClipped(ChCanvas* canv,
						  ChFont* font,
						  char* string,
						  int penx,
						  int peny,
						  uint32_t color,
						  ChRect* limit) {
	(void)canv;
	(void)font;
	(void)limit;
	if (g.textCount < 8) {
		g.text[g.textCount].x = penx;
		g.text[g.textCount].y = peny;
		g.text[g.textCount].color = color;
		g.text[g.textCount].text = string;
	}
	g.textCount++;
	return 0;
}

void ChWindowUpdate(
	ChWindow* win, int x, int y, int w, int h, bool entire, bool dirty) {
	(void)win;
	(void)x;
	(void)y;
	(void)w;
	(void)h;
	(void)entire;
	(void)dirty;
}

void ChWindowHide(ChWindow* win) {
	(void)win;
}

void _KePrint(const char* text, ...) {
	(void)text;
}

int _KeProcessSleep(uint64_t ms) {
	(void)ms;
	return 0;
}

int _KeCreateProcess(int parent_id, char* name) {
	(void)parent_id;
	(void)name;
	return -1;
}

int _KeProcessLoadExec(int proc_id, char* filename, int argc, char** argv) {
	(void)proc_id;
	(void)filename;
	(void)argc;
	(void)argv;
	return -1;
}

int _KeOpenFile(char* pathname, int mode) {
	(void)pathname;
	(void)mode;
	return -1;
}

size_t _KeReadFile(int fd, void* buffer, size_t length) {
	(void)fd;
	(void)buffer;
	(void)length;
	return 0;
}

int _KeFileStat(int fd, void* buf) {
	(void)fd;
	(void)buf;
	return -1;
}

void* _KeMemMap(
	void* address, size_t length, int protect, int flags, int filedesc, uint64_t offset) {
	(void)address;
	(void)length;
	(void)protect;
	(void)flags;
	(void)filedesc;
	(void)offset;
	return NULL;
}

} /* extern "C" */

/* launcher.h has no extern "C" guard, so these keep C++ linkage. */
static AppGrid g_grid;

AppGrid* XELauncherGetAppGrid(void) {
	return &g_grid;
}

void LaunchButtonPaint(LaunchButton* lb, ChWindow* win);

static int g_fail = 0;

#define CHECK(cond, msg)                                                                          \
	do {                                                                                          \
		if (!(cond)) {                                                                            \
			printf("FAIL: %s\n", msg);                                                            \
			g_fail++;                                                                             \
		} else {                                                                                  \
			printf("ok: %s\n", msg);                                                              \
		}                                                                                         \
	} while (0)

static ChCanvas g_canvas;
static ChitralekhaApp g_app;
static ChWindow g_win;

static LaunchButton* makeTestButton(void) {
	LaunchButton* lb = CreateLaunchButton(50, 40, 100, 100, (char*)"Files", (char*)"/file.exe");
	lb->hover = false;
	lb->clicked = false;
	lb->buttonIcon = 0;
	return lb;
}

static void checkLabelDraws(const char* ctx, int expectX, int expectY) {
	char msg[160];
	snprintf(msg, sizeof(msg), "%s: two text draws (shadow, face)", ctx);
	CHECK(g.textCount == 2, msg);
	if (g.textCount != 2)
		return;
	snprintf(msg, sizeof(msg), "%s: shadow is opaque black at (+1,+1)", ctx);
	CHECK(g.text[0].color == LABEL_SHADOW_COLOR && g.text[0].x == expectX + 1 &&
			  g.text[0].y == expectY + 1,
		  msg);
	snprintf(msg, sizeof(msg), "%s: face is white at centered origin", ctx);
	CHECK(g.text[1].color == LABEL_FACE_COLOR && g.text[1].x == expectX &&
			  g.text[1].y == expectY,
		  msg);
	snprintf(msg, sizeof(msg), "%s: both draws carry the button title", ctx);
	CHECK(g.text[0].text && g.text[1].text && strcmp(g.text[0].text, "Files") == 0 &&
			  strcmp(g.text[1].text, "Files") == 0,
		  msg);
}

int main(void) {
	static uint32_t pixels[400 * 300];

	memset(&g_grid, 0, sizeof(g_grid));
	g_grid.x = 0;
	g_grid.y = 0;
	g_grid.w = 400;
	g_grid.h = 300;

	memset(&g_canvas, 0, sizeof(g_canvas));
	g_canvas.canvasWidth = 400;
	g_canvas.canvasHeight = 300;
	g_canvas.buffer = pixels;
	g_canvas.bufferSz = sizeof(pixels);

	memset(&g_app, 0, sizeof(g_app));
	g_app.baseFont = (ChFont*)0x1; /* stubs ignore the pointer */

	memset(&g_win, 0, sizeof(g_win));
	g_win.canv = &g_canvas;
	g_win.app = &g_app;

	/* Button at (50,40) size 100x100, stub width 60:
	 * origin = (50 + 50 - 30, 40 + 100 - 5) = (70, 135). */
	const int expectX = 70;
	const int expectY = 135;

	/* Plain paint. */
	resetCalls();
	LaunchButton* lb = makeTestButton();
	LaunchButtonPaint(lb, &g_win);
	CHECK(g.fontSize == STUB_FONT_SIZE, "plain: font size 11");
	CHECK(g.rectCount == 1, "plain: exactly one background fill");
	CHECK(g.rectCol == LAUNCHER_BACKGROUND_COLOR, "plain: fill is launcher background");
	CHECK(g.rectX == 50 && g.rectY == 40 && g.rectW == 100 && g.rectH == 100,
		  "plain: fill covers the button rect");
	CHECK(g.clippedRectCount == 0, "plain: no scrim rect behind the label");
	CHECK(g.gradientCount == 0, "plain: no hover gradient");
	checkLabelDraws("plain", expectX, expectY);

	/* Hover paint: gradient once, same label contract. */
	resetCalls();
	lb->hover = true;
	LaunchButtonPaint(lb, &g_win);
	CHECK(g.gradientCount == 1, "hover: gradient painted once");
	CHECK(g.rectCount == 1, "hover: background fill still exactly once");
	CHECK(g.clippedRectCount == 0, "hover: no scrim rect behind the label");
	checkLabelDraws("hover", expectX, expectY);

	free(lb->title);
	free(lb->appname);
	free(lb);

	if (g_fail) {
		printf("%d check(s) FAILED\n", g_fail);
		return 1;
	}
	printf("all label checks passed\n");
	return 0;
}
