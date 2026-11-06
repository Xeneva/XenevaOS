#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "window.h"

struct Mapping {
	void* address;
	size_t length;
	uint16_t key;
};
static Mapping mappings[16];
static size_t liveBytes;

static void* allocate(size_t length, uint16_t key) {
	for (Mapping& mapping : mappings) {
		if (mapping.address)
			continue;
		mapping.address = malloc(length);
		assert(mapping.address);
		memset(mapping.address, 0xAA, length);
		mapping.length = length;
		mapping.key = key;
		liveBytes += length;
		return mapping.address;
	}
	abort();
}

static void release(Mapping& mapping) {
	liveBytes -= mapping.length;
	free(mapping.address);
	mapping = {};
}

extern "C" int _KeCreateSharedMem(uint16_t key, size_t length, uint8_t) {
	allocate(length, key);
	return key;
}

extern "C" void* _KeObtainSharedMem(uint16_t id, void*, int) {
	for (const Mapping& mapping : mappings)
		if (mapping.address && mapping.key == id)
			return mapping.address;
	abort();
}

extern "C" void _KeUnmapSharedMem(uint16_t key) {
	for (Mapping& mapping : mappings) {
		if (mapping.address && mapping.key == key) {
			release(mapping);
			return;
		}
	}
	abort();
}

extern "C" void* _KeMemMap(void*, size_t length, int, int, int, uint64_t) {
	return allocate(length, 0);
}

extern "C" void _KeMemUnmap(void* address, size_t length) {
	for (Mapping& mapping : mappings) {
		if (mapping.address == address && !mapping.key) {
			assert(mapping.length == length);
			release(mapping);
			return;
		}
	}
	abort();
}

int main() {
	WinSharedInfo info = {};
	Window win = {};
	win.sharedInfo = (uint32_t*)&info;
	win.ownerId = 10;
	win.flags = WINDOW_FLAG_GLASS;
	win.originalW = info.width = 380;
	win.originalH = info.height = 400;
	size_t bytes = 380 * 400 * sizeof(uint32_t);
	win.backBuffer = (uint32_t*)CreateNewBackBuffer(10, bytes, &win.backBufferKey);
	win.glassBlur = (uint32_t*)allocate(bytes, 0);
	win.glassTmp = (uint32_t*)allocate(bytes, 0);

	const int widths[] = {438, 500, 380};
	for (int width : widths) {
		uint16_t oldKey = win.backBufferKey;
		assert(ResizeWindowBackBuffer(&win, width, 400));
		assert(win.backBufferKey != oldKey);
		assert(win.originalW == width && info.width == width);
		assert(info.height == 400 && !win.glassBlurValid);
		assert(info.updateEntireWindow && !info.dirty && info.rect_count == 0);
		bytes = (size_t)width * 400 * sizeof(uint32_t);
		assert(liveBytes == bytes * 3);
		for (size_t i = 0; i < bytes / sizeof(uint32_t); i++)
			assert(win.backBuffer[i] == 0);
	}
	ReleaseWindowEffects(&win);
	assert(!win.glassTmp && !win.glassBlur && liveBytes == bytes);
	ReleaseWindowEffects(&win);
	_KeUnmapSharedMem(win.backBufferKey);
	assert(liveBytes == 0);
	puts("PASS: real resize buffer replacement, keys, zeroing, effect sizes and reclamation");
}
