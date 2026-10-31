#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/_ketty.h>
#include <sys/_kefile.h>
#include <sys/iocodes.h>

#define STAR_COUNT 12

static const char starGlyphs[] = {'.', '+', '*', '+'};

typedef struct {
	uint8_t x, y;
	uint8_t phase;
	uint8_t speed;
} Star;

static Star stars[STAR_COUNT];

void StarsInit(int cols, int rows) {
	for (int i = 0; i < STAR_COUNT; i++) {
		stars[i].x = (i * 7 + 3) % (cols - 1);
		stars[i].y = (i * 5 + 1) % (rows - 1);
		stars[i].phase = i % 4;
		stars[i].speed = (i % 2) + 1;
	}
}

void StarsDraw(int tick) {
	printf("\033[?25l\033[H");

	for (int i = 0; i < STAR_COUNT; i++) {
		Star* s = &stars[i];
		uint8_t p = (s->phase + tick * s->speed) % 4;

		printf("\033[%d;%dH%c", s->y + 1, s->x + 1, starGlyphs[p]);
	}

	fflush(stdout);
}

void StarsErase() {
	for (int i = 0; i < STAR_COUNT; i++)
		printf("\033[%d;%dH ", stars[i].y + 1, stars[i].x + 1);
	printf("\033[?25h");
	fflush(stdout);
}

void StarsRun(int cols, int rows, int frames) {
	StarsInit(cols, rows);
	printf("\033[2J");

	for (int tick = 0; tick < frames; tick++) {
		StarsDraw(tick);
		struct timespec ts;
		ts.tv_sec = 0;
		ts.tv_nsec = 150000000L;
		nanosleep(&ts, NULL);
	}

	StarsErase();
	printf("\033[2J]033[H");
	fflush(stdout);
}

int main(int argc, char* argv[]) {
	WinSize sz;
	_KeFileIoControl(0, TIOCGWINSZ, &sz);
	_KePrint("started twinkling col : %d, row : %d \r\n", sz.ws_col, sz.ws_row);
	StarsRun(sz.ws_col, sz.ws_row, 100);
}