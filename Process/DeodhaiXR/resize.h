#ifndef __DEODHAI_RESIZE_H__
#define __DEODHAI_RESIZE_H__

#include "window.h"
#include <sys/_keipcpostbox.h>

bool DeodhaiBeginResize(Window* win, int postbox, int x, int y, int width, int height);
bool DeodhaiHandleResizeReply(Window* win, int postbox, const PostEvent* event);
void DeodhaiPollResize(Window* win);

#endif
