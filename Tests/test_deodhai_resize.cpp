#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "resize.h"

static uint64_t nowMs = 100;
static PostEvent sent[16];
static unsigned sentCount;
static unsigned diagnostics;
static unsigned reallocations;
static bool allocationFails;

extern "C" uint64_t _KeGetCurrentMS() {
	return nowMs;
}

extern "C" int _KeFileIoControl(int fd, int code, void* arg) {
	assert(fd == 42);
	/* A nested event drain would swallow unrelated window/close requests. */
	assert(code == POSTBOX_PUT_EVENT);
	assert(sentCount < sizeof(sent) / sizeof(sent[0]));
	sent[sentCount++] = *(PostEvent*)arg;
	return 1;
}

extern "C" void _KePrint(const char*, ...) {
	diagnostics++;
}

bool ResizeWindowBackBuffer(Window* win, int width, int height) {
	reallocations++;
	if (allocationFails)
		return false;
	WinSharedInfo* info = (WinSharedInfo*)win->sharedInfo;
	info->width = win->originalW = width;
	info->height = win->originalH = height;
	win->backBufferKey++;
	return true;
}

static Window window(WinSharedInfo* info, uint16_t owner, uint32_t handle) {
	Window win = {};
	*info = {};
	info->x = 20;
	info->y = 30;
	info->width = win.originalW = 380;
	info->height = win.originalH = 400;
	win.sharedInfo = (uint32_t*)info;
	win.ownerId = owner;
	win.handle = handle;
	win.backBufferKey = 600;
	win.title = (char*)"test window";
	return win;
}

static PostEvent acknowledgement(const Window& win) {
	PostEvent event = {};
	event.type = DEODHAI_MESSAGE_BUFFER_DESTROYED;
	event.from_id = win.ownerId;
	event.dword = win.backBufferKey;
	event.dword4 = win.handle;
	event.dword5 = HANDLE_TYPE_NORMAL_WINDOW;
	return event;
}

int main() {
	WinSharedInfo info, peerInfo;
	Window win = window(&info, 10, 100);
	Window peer = window(&peerInfo, 11, 101);
	win.flags = WINDOW_FLAG_NON_RESIZABLE;
	assert(!DeodhaiBeginResize(&win, 42, 20, 30, 500, 400));
	win.flags = 0;
	assert(!DeodhaiBeginResize(&win, 42, 20, 30, 0, 400));
	assert(!DeodhaiBeginResize(&win, 42, 20, 30, INT_MAX, INT_MAX));
	assert(!DeodhaiBeginResize(&win, 42, 20, 30, 380, 400));
	assert(sentCount == 0);

	assert(DeodhaiBeginResize(&win, 42, 40, 50, 500, 450));
	assert(sentCount == 1);
	assert(sent[0].type == DEODHAI_REPLY_DESTROY_BUFFER);
	assert(sent[0].to_id == 10 && sent[0].dword4 == 100);
	assert(info.width == 380 && info.height == 400);
	assert(info.x == 20 && info.y == 30 && win.backBufferKey == 600);
	assert(!DeodhaiBeginResize(&win, 42, 0, 0, 600, 600));
	assert(DeodhaiBeginResize(&peer, 42, 20, 30, 420, 410));

	PostEvent event = acknowledgement(win);
	event.from_id = peer.ownerId;
	assert(!DeodhaiHandleResizeReply(&win, 42, &event));
	event = acknowledgement(win);
	event.dword4 = peer.handle;
	assert(!DeodhaiHandleResizeReply(&win, 42, &event));
	event.type = DEODHAI_MESSAGE_CLOSE_WINDOW;
	PostEvent saved = event;
	assert(!DeodhaiHandleResizeReply(&win, 42, &event));
	assert(memcmp(&event, &saved, sizeof(event)) == 0);
	assert(reallocations == 0);

	/* Hundreds of frames can proceed while a client is silent. */
	for (nowMs = 101; nowMs < 1100; nowMs++) {
		DeodhaiPollResize(&win);
		assert(diagnostics == 0 && reallocations == 0 && sentCount == 2);
	}
	DeodhaiPollResize(&win);
	assert(diagnostics == 1 && (win.flags & WINDOW_FLAG_NON_RESIZABLE));
	assert(info.width == 380 && info.height == 400 && win.backBufferKey == 600);
	DeodhaiPollResize(&win);
	assert(diagnostics == 1);
	assert(!DeodhaiBeginResize(&win, 42, 0, 0, 600, 600));

	/* A different client can complete, followed by a safe late reply. */
	event = acknowledgement(peer);
	assert(DeodhaiHandleResizeReply(&peer, 42, &event));
	assert(peerInfo.width == 420 && peerInfo.height == 410);
	assert(!peer.resizePending && win.resizePending);
	event = acknowledgement(win);
	assert(DeodhaiHandleResizeReply(&win, 42, &event));
	assert(info.width == 500 && info.height == 450 && info.x == 40 && info.y == 50);
	assert(win.backBufferKey == 601 && !win.resizePending);
	assert(sent[3].type == DEODHAI_REPLY_REINIT_BUFFER);
	assert(sent[3].dword == 601 && sent[3].to_id == 10 && sent[3].dword4 == 100);
	assert(!DeodhaiHandleResizeReply(&win, 42, &event));
	assert(reallocations == 2);
	PostEvent previousReply = acknowledgement(peer);
	previousReply.dword--;

	/* Failed replacement must reattach the retained old mapping and stride. */
	allocationFails = true;
	assert(DeodhaiBeginResize(&peer, 42, 0, 0, 600, 600));
	assert(!DeodhaiHandleResizeReply(&peer, 42, &previousReply));
	event = acknowledgement(peer);
	assert(DeodhaiHandleResizeReply(&peer, 42, &event));
	assert(peerInfo.width == 420 && peerInfo.height == 410 && peerInfo.x == 20);
	assert(sent[5].dword == peer.backBufferKey && !peer.resizePending);
	puts("PASS: nonblocking resize, reply isolation, timeout, late reply, allocation recovery");
}
