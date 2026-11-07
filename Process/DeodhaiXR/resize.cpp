#include "resize.h"
#include <string.h>
#include <sys/_kefile.h>
#include <sys/_ketime.h>

static const uint64_t RESIZE_TIMEOUT_MS = 1000;

bool DeodhaiBeginResize(Window* win, int postbox, int x, int y, int width, int height) {
	if (!win || win->resizePending || (win->flags & WINDOW_FLAG_NON_RESIZABLE) || width <= 0 ||
		height <= 0 || (uint64_t)width * height > UINT32_MAX / 4)
		return false;
	WinSharedInfo* info = (WinSharedInfo*)win->sharedInfo;
	if (width == info->width && height == info->height)
		return false;

	/* Keep the old stride and mappings valid until this particular client
	 * acknowledges destruction. The normal frame loop owns all event reads. */
	win->resizePending = true;
	win->resizeDeadline = _KeGetCurrentMS() + RESIZE_TIMEOUT_MS;
	win->resizeX = x;
	win->resizeY = y;
	win->resizeW = width;
	win->resizeH = height;
	PostEvent event;
	memset(&event, 0, sizeof(event));
	event.type = DEODHAI_REPLY_DESTROY_BUFFER;
	event.to_id = win->ownerId;
	event.from_id = POSTBOX_ROOT_ID;
	event.dword = win->backBufferKey;
	event.dword4 = win->handle;
	event.dword5 = (win->flags & WINDOW_FLAG_POPUP) ? HANDLE_TYPE_POPUP_WINDOW
														 : HANDLE_TYPE_NORMAL_WINDOW;
	_KeFileIoControl(postbox, POSTBOX_PUT_EVENT, &event);
	return true;
}

bool DeodhaiHandleResizeReply(Window* win, int postbox, const PostEvent* event) {
	if (!win || !win->resizePending || event->type != DEODHAI_MESSAGE_BUFFER_DESTROYED ||
		event->from_id != win->ownerId || event->dword4 != win->handle ||
		event->dword != win->backBufferKey)
		return false;

	WinSharedInfo* info = (WinSharedInfo*)win->sharedInfo;
	if (ResizeWindowBackBuffer(win, win->resizeW, win->resizeH)) {
		info->x = win->resizeX;
		info->y = win->resizeY;
	}
	/* If allocation failed, send the retained old buffer back to the client. */
	PostEvent reply;
	memset(&reply, 0, sizeof(reply));
	reply.type = DEODHAI_REPLY_REINIT_BUFFER;
	reply.dword = win->backBufferKey;
	reply.dword4 = win->handle;
	reply.dword5 = event->dword5;
	reply.to_id = win->ownerId;
	reply.from_id = POSTBOX_ROOT_ID;
	win->resizePending = false;
	win->resizeDeadline = 0;
	_KeFileIoControl(postbox, POSTBOX_PUT_EVENT, &reply);
	return true;
}

void DeodhaiPollResize(Window* win) {
	if (!win->resizePending || !win->resizeDeadline || _KeGetCurrentMS() < win->resizeDeadline)
		return;
	_KePrint("[deodhaiXR]: resize client did not reply: %s\r\n", win->title);
	win->flags |= WINDOW_FLAG_NON_RESIZABLE;
	win->resizeDeadline = 0;
	/* Keep the request so a late reply can safely reattach the client. There
	 * is no retry, no nested event drain, and no wait in the compositor. */
}
