#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "compose.h"

static const int WIDTH = 96;
static const int HEIGHT = 96;
static uint32_t pixels[WIDTH * HEIGHT];
static uint32_t wallpaper[WIDTH * HEIGHT];
static uint32_t blurred[WIDTH * HEIGHT];
static uint32_t overlayPixels[88 * 20];
static int damageCount;

uint32_t* DeoGetBackSurface() {
	return wallpaper;
}

uint32_t* DeoGetScreenBlur() {
	return blurred;
}
void AddDirtyClip(int x, int y, int width, int height) {
	assert(x >= 0 && y >= 0 && x + width <= WIDTH && y + height <= HEIGHT);
	damageCount++;
}

static void verify(const ChCanvas& canvas, const Window& overlay) {
	const WinSharedInfo* info = (const WinSharedInfo*)overlay.sharedInfo;
	for (int y = 0; y < HEIGHT; y++) {
		for (int x = 0; x < WIDTH; x++) {
			uint32_t expected = wallpaper[y * WIDTH + x];
			if (x >= info->x && x < info->x + info->width && y >= info->y &&
				y < info->y + info->height) {
				uint32_t source = overlay.backBuffer[(y - info->y) * info->width + x - info->x];
				if (overlay.flags & WINDOW_FLAG_GLASS) {
					uint32_t blur = blurred[y * WIDTH + x];
					_blend_scanline_glass_neon(&expected, &source, &blur, 1);
				} else {
					expected = ChColorAlphaBlend2(expected, source);
				}
			}
			assert(canvas.buffer[y * WIDTH + x] == expected);
		}
	}
}

int main() {
	for (int i = 0; i < WIDTH * HEIGHT; i++) {
		wallpaper[i] = 0xFF102030u + i;
		blurred[i] = 0xFF304050u + i;
	}
	for (int i = 0; i < 88 * 20; i++)
		overlayPixels[i] = 0xFF8090A0u + i; /* Unique opaque clock and dock pixels. */
	ChCanvas canvas = {};
	canvas.canvasWidth = canvas.screenWidth = WIDTH;
	canvas.canvasHeight = canvas.screenHeight = HEIGHT;
	canvas.buffer = pixels;
	WinSharedInfo info = {}, movingInfo = {};
	info.x = 4;
	info.y = 70;
	info.width = 88;
	info.height = 20;
	Window overlay = {}, moving = {};
	overlay.flags = WINDOW_FLAG_ALWAYS_ON_TOP;
	overlay.sharedInfo = (uint32_t*)&info;
	overlay.backBuffer = overlayPixels;
	moving.sharedInfo = (uint32_t*)&movingInfo;
	movingInfo.y = 58;
	movingInfo.width = 30;
	movingInfo.height = 25;

	/* Moving across the overlay must redraw both the old and new overlap,
	 * including the clock even when the new window location misses it. */
	for (int x = 0; x <= 100; x += 5) {
		memcpy(pixels, wallpaper, sizeof(pixels));
		movingInfo.x = x;
		info.rect_count = 1;
		info.rect[0] = {70, 2, 12, 10}; /* Simulate a concurrent client clock update. */
		info.dirty = true;
		Rect pending = info.rect[0];
		_compose_always_on_top_entire(&canvas, &overlay, true, true, &info, &moving);
		verify(canvas, overlay);
		assert(info.rect_count == 1 && info.dirty);
		assert(memcmp(&info.rect[0], &pending, sizeof(pending)) == 0);
	}

	/* Glass/alpha content must be stable across repeated drag redraws. */
	overlay.flags |= WINDOW_FLAG_GLASS;
	for (int i = 0; i < 88 * 20; i++)
		if (i % 3)
			overlayPixels[i] = (overlayPixels[i] & 0x00FFFFFFu) | 0x60000000u;
	memcpy(pixels, wallpaper, sizeof(pixels));
	for (int i = 0; i < 10; i++) {
		_compose_always_on_top_entire(&canvas, &overlay, true, true, &info, &moving);
		verify(canvas, overlay);
	}
	assert(damageCount == 31);
	puts("PASS: entire dock/clock survives moving overlaps; "
		 "client damage stays intact; glass redraw is stable");
}
