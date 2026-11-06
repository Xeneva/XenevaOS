/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2023, Manas Kamal Choudhury
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

#include "dirty.h"
#include "xr_present.h"
#include <stdlib.h>
#include <string.h>
#include <_xeneva.h>
#include <sys/_kefile.h>
#include <sys/iocodes.h>
#include <gpusrv.h>

static uint32_t _dirty_count = 0;
Rect dirtyRect[100];
XEFileIOControl ioctl;

static bool rects_touch_or_overlap(const Rect* a, const Rect* b) {
	int64_t a_right = (int64_t)a->x + a->w;
	int64_t a_bottom = (int64_t)a->y + a->h;
	int64_t b_right = (int64_t)b->x + b->w;
	int64_t b_bottom = (int64_t)b->y + b->h;
	return (int64_t)a->x <= b_right && (int64_t)b->x <= a_right &&
		   (int64_t)a->y <= b_bottom && (int64_t)b->y <= a_bottom;
}

static Rect rect_union(const Rect* a, const Rect* b) {
	int64_t left = a->x < b->x ? a->x : b->x;
	int64_t top = a->y < b->y ? a->y : b->y;
	int64_t a_right = (int64_t)a->x + a->w;
	int64_t b_right = (int64_t)b->x + b->w;
	int64_t a_bottom = (int64_t)a->y + a->h;
	int64_t b_bottom = (int64_t)b->y + b->h;
	int64_t right = a_right > b_right ? a_right : b_right;
	int64_t bottom = a_bottom > b_bottom ? a_bottom : b_bottom;
	Rect result;
	result.x = (int)left;
	result.y = (int)top;
	result.w = (int)(right - left);
	result.h = (int)(bottom - top);
	return result;
}

extern int _get_gpu_fd();
extern int _get_gpu_display_id();
extern bool _is_gpu_enabled();
extern bool _is_gpu_userspace();

void InitialiseDirtyClipList() {
	for (int i = 0; i < 100; i++) {
		dirtyRect[i].x = 0;
		dirtyRect[i].y = 0;
		dirtyRect[i].w = 0;
		dirtyRect[i].h = 0;
	}
	_dirty_count = 0;
	memset(&ioctl, 0, sizeof(XEFileIOControl));
}
/*
 * AddDirtyClip -- add a dirty clip rectangle
 * @param x -- x position
 * @param y -- y position
 * @param w -- width of the rect
 * @param h -- height of the rect
 */
void AddDirtyClip(int x, int y, int w, int h) {
	if (w <= 0 || h <= 0)
		return;

	Rect incoming = {x, y, w, h};
	/* Merge transitively so overlapping damage is copied only once. Removing a
	 * merged entry by swapping in the tail keeps insertion bounded and avoids
	 * shifting the fixed-size array. */
	for (uint32_t i = 0; i < _dirty_count;) {
		if (!rects_touch_or_overlap(&incoming, &dirtyRect[i])) {
			i++;
			continue;
		}
		incoming = rect_union(&incoming, &dirtyRect[i]);
		dirtyRect[i] = dirtyRect[--_dirty_count];
		i = 0;
	}

	if (_dirty_count < 100) {
		dirtyRect[_dirty_count++] = incoming;
		return;
	}

	/* Never discard pending damage on overflow. Collapse it to one conservative
	 * bounding rectangle instead; clipping happens against the canvas below. */
	for (uint32_t i = 0; i < _dirty_count; i++)
		incoming = rect_union(&incoming, &dirtyRect[i]);
	dirtyRect[0] = incoming;
	_dirty_count = 1;
	//_KePrint("Dirty clip updated %d %d \r\n", dirtyRect[_dirty_count].x,
	//	dirtyRect[_dirty_count].y);
	//_KePrint("DC -> %d \r\n", _dirty_count);
}

/*
 * DirtyScreenUpdate -- update the screen with respect
 * to dirty rect boundary
 * @param canvas -- pointer to canvas
 */
/* DirtyBoundingBox -- merge a rect list into one bounding box. Shared by the
 * GPU flush below and the kernel present request the software path sends. --axiss */
static void DirtyBoundingBox(const Rect* rects, uint32_t count,
			     int64_t* left, int64_t* top, int64_t* right, int64_t* bottom) {
	*left = rects[0].x;
	*top = rects[0].y;
	*right = *left + rects[0].w;
	*bottom = *top + rects[0].h;
	for (uint32_t i = 1; i < count; i++) {
		int64_t r = (int64_t)rects[i].x + rects[i].w;
		int64_t b = (int64_t)rects[i].y + rects[i].h;
		if (rects[i].x < *left)
			*left = rects[i].x;
		if (rects[i].y < *top)
			*top = rects[i].y;
		if (r > *right)
			*right = r;
		if (b > *bottom)
			*bottom = b;
	}
}

/* DirtyFlushBounding -- one synchronous transfer+flush for the whole frame.
 * Each 0x202 is a full virtqueue round-trip, so N rects meant N round-trips
 * and the guest watched its own screen assemble piece by piece. Merging to
 * one bounding box trades (sometimes more) bytes for exactly one round-trip
 * per frame -- on TCG the latency win dominates. --axiss */
static void DirtyFlushBounding(int display_id, const Rect* rects, uint32_t count) {
	int64_t left, top, right, bottom;
	DirtyBoundingBox(rects, count, &left, &top, &right, &bottom);
	if (_is_gpu_userspace()) {
		GpuSrvDamage damage;
		damage.x = (uint32_t)left;
		damage.y = (uint32_t)top;
		damage.width = (uint32_t)(right - left);
		damage.height = (uint32_t)(bottom - top);
		(void)display_id;
		_KeFileIoControl(_get_gpu_fd(), GPUSRV_IOC_DAMAGE, &damage);
	} else {
		ioctl.uint_1 = display_id;
		ioctl.ushort_1 = (uint16_t)left;
		ioctl.ushort_2 = (uint16_t)top;
		ioctl.ulong_1 = (uint64_t)(right - left);
		ioctl.ulong_2 = (uint64_t)(bottom - top);
		_KeFileIoControl(_get_gpu_fd(), 0x202, &ioctl);
	}
}

void DirtyScreenUpdate(ChCanvas* canvas) {
	int display_id = _get_gpu_display_id();
	bool gpu_enabled = _is_gpu_enabled();

	/* Clip everything to the canvas first, compacting valid rects down so
	 * the present paths below only ever see drawable damage. */
	uint32_t valid = 0;
	for (uint32_t i = 0; i < _dirty_count; i++) {
		int64_t left = dirtyRect[i].x;
		int64_t top = dirtyRect[i].y;
		int64_t right = left + dirtyRect[i].w;
		int64_t bottom = top + dirtyRect[i].h;
		if (left < 0)
			left = 0;
		if (top < 0)
			top = 0;
		if (right > (int64_t)canvas->canvasWidth)
			right = canvas->canvasWidth;
		if (bottom > (int64_t)canvas->canvasHeight)
			bottom = canvas->canvasHeight;
		if (right <= left || bottom <= top)
			continue;
		dirtyRect[valid].x = (int)left;
		dirtyRect[valid].y = (int)top;
		dirtyRect[valid].w = (int)(right - left);
		dirtyRect[valid].h = (int)(bottom - top);
		valid++;
	}
	_dirty_count = 0;
	if (valid == 0)
		return;

	if (XrPresentEnabled()) {
		/* xrEndFrame copied the full-resolution flat canvas into the scanout;
		 * its damage rectangles remain in source/output coordinates. --axiss */
		if (gpu_enabled)
			DirtyFlushBounding(display_id, dirtyRect, valid);
		return;
	}

	if (!gpu_enabled) {
		/* Plain RAM copies are cheap; keep per-rect updates and the single
		 * commit barrier at the presentation boundary. */
		for (uint32_t i = 0; i < valid; i++)
			ChCanvasScreenUpdate(
				canvas, dirtyRect[i].x, dirtyRect[i].y, dirtyRect[i].w, dirtyRect[i].h);
		ChCanvasScreenCommit();
		/* No /dev/virtiogpu to flush through here, but the console may
		 * still be mirrored onto a scanout by the kernel (the vendored
		 * virtio-gpu bridge). Ask for this frame's bounding box; with no
		 * mirror registered the request is a no-op. --axiss */
		int64_t left, top, right, bottom;
		DirtyBoundingBox(dirtyRect, valid, &left, &top, &right, &bottom);
		memset(&ioctl, 0, sizeof(ioctl));
		ioctl.syscall_magic = AURORA_SYSCALL_MAGIC;
		ioctl.ushort_1 = (uint16_t)left;
		ioctl.ushort_2 = (uint16_t)top;
		ioctl.ulong_1 = (uint64_t)(right - left);
		ioctl.ulong_2 = (uint64_t)(bottom - top);
		_KeFileIoControl(canvas->graphics_fd, SCREEN_UPDATE, &ioctl);
		return;
	}

	if (_is_gpu_userspace() && !XrPresentEnabled()) {
		/* /dev/graph is the kernel-owned staging surface in the user-space
		 * path. Copy through its real GOP pitch before publishing damage; the
		 * server then copies that stable surface into its DMA backing. */
		for (uint32_t i = 0; i < valid; i++)
			ChCanvasScreenUpdate(
				canvas, dirtyRect[i].x, dirtyRect[i].y, dirtyRect[i].w, dirtyRect[i].h);
		ChCanvasScreenCommit();
	}

	DirtyFlushBounding(display_id, dirtyRect, valid);
}

/*
 * GetDirtyRectCount -- returns the number of dirty rect
 */
uint32_t GetDirtyRectCount() {
	return _dirty_count;
}

bool GetDirtyRect(uint32_t index, Rect* rect) {
	if (!rect || index >= _dirty_count)
		return false;
	*rect = dirtyRect[index];
	return true;
}
