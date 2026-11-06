/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2026, Manas Kamal Choudhury
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

/*
 * livetile.h -- public, header-only client API for the XELnch launcher.
 *
 * Any app can include this (Libs/Chitralekha is already on every app's
 * include path) to push live-tile content to its own launcher tile, and
 * Namdapha uses it to ask the launcher to show/hide itself with the
 * Metro animation instead of toggling the window behind its back.
 *
 * Transport: one PostEvent per update, posted to the launcher's thread.
 * The launcher registers itself under process token XELNCH_PROCESS_TOKEN,
 * so lookup is a single syscall -- no name service, no polling.
 *
 * IMPORTANT: never put pointers in the event (pValue / charValue ...).
 * They point into the sender's address space and are garbage inside the
 * launcher. All text travels inline in charValue3.
 *
 * PostEvent layout for XELNCH_MSG_LIVE_UPDATE:
 *   dword   = FNV-1a hash of the app path exactly as written in
 *             /lncher.cnf [App] (e.g. "/audplr.exe")   -> XelnchHash()
 *   dword2  = layout (XELNCH_TILE_*)
 *   dword3  = badge count (0 = no badge)
 *   dword4  = progress 0..1000 (XELNCH_NO_PROGRESS = hidden)
 *   charValue3 = "line1\0line2\0"   (<= 99 bytes total, truncated)
 */

#ifndef __XELNCH_LIVETILE_H__
#define __XELNCH_LIVETILE_H__

#include <stdint.h>
#include <string.h>
#include <sys/_keipcpostbox.h>
#include <sys/_kefile.h>
#include <sys/_keproc.h>

/* kernel token slot the launcher registers in. Kernel slots are
 * 0..AURORA_MAX_PROC_TOKEN-1 (50); 0..4 are used by system daemons. */
#define XELNCH_PROCESS_TOKEN PROCESS_TOKEN_LAUNCHER

/* PostEvent.type values handled by XELnch (type is uint8_t; these do not
 * collide with TIMER_MESSAGE_CODE (8) or DEODHAI_* (50..61, 150..174)). */
#define XELNCH_MSG_TOGGLE	   200 /* show if hidden, hide if shown (animated) */
#define XELNCH_MSG_LIVE_UPDATE 201 /* set live content of a tile             */
#define XELNCH_MSG_LIVE_CLEAR  202 /* drop live content of a tile            */
#define XELNCH_MSG_HIDE		   203 /* hide (animated) if currently shown     */

/* live tile layouts */
#define XELNCH_TILE_TEXT	   0 /* line1 + line2                          */
#define XELNCH_TILE_BADGE	   1 /* only the badge count                   */
#define XELNCH_TILE_PROGRESS   2 /* line1 + line2 + progress bar           */
#define XELNCH_TILE_NOWPLAYING 3 /* "NOW PLAYING" caption + lines + bar    */

#define XELNCH_NO_PROGRESS 0xFFFF

/* FNV-1a 32 bit, O(len). Launcher and clients must hash the same bytes. */
static inline uint32_t XelnchHash(const char* s) {
	uint32_t h = 2166136261u;
	while (*s) {
		h ^= (uint8_t)*s++;
		h *= 16777619u;
	}
	return h;
}

/* returns the launcher thread id or -1 when the launcher is not running */
static inline int XelnchGetThread() {
	int tid = _KeProcessTokenGetThreadID(XELNCH_PROCESS_TOKEN);
	return tid > 0 ? tid : -1;
}

static inline int XelnchPost(int postboxfd, PostEvent* e) {
	int tid = XelnchGetThread();
	if (tid < 0)
		return -1;
	e->to_id = (uint16_t)tid;
	_KeFileIoControl(postboxfd, POSTBOX_PUT_EVENT, e);
	return 0;
}

/*
 * @brief XelnchLiveTileUpdate -- push live content to this app's tile
 * @param postboxfd -- ChitralekhaApp::postboxfd of the caller
 * @param appPath -- path exactly as in /lncher.cnf, e.g. "/audplr.exe"
 * @param layout -- XELNCH_TILE_*
 * @param line1, line2 -- may be NULL
 * @param badge -- 0 hides the badge
 * @param progress -- 0..1000 or XELNCH_NO_PROGRESS
 * Rate limit yourself: one update per second is plenty for a tile.
 */
static inline int XelnchLiveTileUpdate(int postboxfd,
									   const char* appPath,
									   uint8_t layout,
									   const char* line1,
									   const char* line2,
									   uint16_t badge,
									   uint16_t progress) {
	PostEvent e;
	memset(&e, 0, sizeof(PostEvent));
	e.type = XELNCH_MSG_LIVE_UPDATE;
	e.dword = XelnchHash(appPath);
	e.dword2 = layout;
	e.dword3 = badge;
	e.dword4 = progress;
	size_t off = 0;
	const size_t cap = sizeof(e.charValue3) - 1;
	if (line1) {
		size_t n = strlen(line1);
		if (n > cap - 1)
			n = cap - 1;
		memcpy(e.charValue3, (void*)line1, n);
		off = n;
	}
	e.charValue3[off++] = '\0';
	if (line2 && off < cap) {
		size_t n = strlen(line2);
		if (n > cap - off)
			n = cap - off;
		memcpy(e.charValue3 + off, (void*)line2, n);
	}
	return XelnchPost(postboxfd, &e);
}

static inline int XelnchLiveTileClear(int postboxfd, const char* appPath) {
	PostEvent e;
	memset(&e, 0, sizeof(PostEvent));
	e.type = XELNCH_MSG_LIVE_CLEAR;
	e.dword = XelnchHash(appPath);
	return XelnchPost(postboxfd, &e);
}

/* used by Namdapha's Go button; returns -1 if no launcher registered */
static inline int XelnchToggleLauncher(int postboxfd) {
	PostEvent e;
	memset(&e, 0, sizeof(PostEvent));
	e.type = XELNCH_MSG_TOGGLE;
	return XelnchPost(postboxfd, &e);
}

#endif
