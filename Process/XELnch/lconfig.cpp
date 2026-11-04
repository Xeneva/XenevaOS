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
 * lconfig.cpp -- /lncher.cnf parser.
 *
 * Entry format (one per app, fields in any order, separated by '|'):
 *
 *   {[Title]Accent Player|
 *   [Icon]/icons/medial.bmp|
 *   [Param]Null|
 *   [App]/audplr.exe|
 *   [Size]M|          optional: S 1x1, M 2x2 (default), W 2x1, L 4x2
 *   [Pin]1|           optional: 1 pinned (default), 0 only in "all apps"
 *   [Color]red}       optional: red, deep, muga, mugadeep, ink, ivory
 *
 * The old files (no Size/Pin/Color, last entry closed with '>') parse
 * unchanged. Single pass, O(file size); the file buffer is freed after
 * parsing and the app table is allocated at its exact size.
 */

#include "launcher.h"
#include <livetile.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/_kefile.h>

static bool _key_is(const char* k, int kl, const char* name) {
	int n = (int)strlen(name);
	if (n != kl)
		return false;
	for (int i = 0; i < n; i++) {
		char a = k[i], b = name[i];
		if (a >= 'A' && a <= 'Z')
			a += 32;
		if (b >= 'A' && b <= 'Z')
			b += 32;
		if (a != b)
			return false;
	}
	return true;
}

static void _copy_trim(char* dst, int dstSize, const char* s, int len) {
	while (len > 0 && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')) {
		s++;
		len--;
	}
	while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' ||
					   s[len - 1] == '\n'))
		len--;
	if (len > dstSize - 1)
		len = dstSize - 1;
	XlMemcpy(dst, s, len);
	dst[len] = '\0';
}

static int _parse_size(const char* v, int def) {
	switch (v[0]) {
	case 'S':
	case 's':
		return XL_TILE_S;
	case 'M':
	case 'm':
		return XL_TILE_M;
	case 'W':
	case 'w':
		return XL_TILE_W;
	case 'L':
	case 'l':
		return XL_TILE_L;
	}
	return def;
}

static int _parse_color(const char* v, int def) {
	static const char* names[XL_COL_COUNT] = { "red", "deep", "muga", "mugadeep", "ink", "ivory" };
	for (int i = 0; i < XL_COL_COUNT; i++)
		if (_key_is(v, (int)strlen(v), names[i]))
			return i;
	return def;
}

/* one {...} block -> app. s points after '{', e at the closing char */
static bool _parse_entry(const char* s, const char* e, XlApp* a, int ordinal) {
	memset(a, 0, sizeof(XlApp));
	a->size = XL_TILE_M;
	a->defPinned = 1;
	/* default colours cycle through the woven palette */
	static const uint8_t cycle[] = { XL_COL_RED,  XL_COL_MUGA,	XL_COL_INK,
									 XL_COL_RED_DEEP, XL_COL_IVORY, XL_COL_MUGA_DEEP };
	a->color = cycle[ordinal % (int)sizeof(cycle)];
	strcpy(a->param, "Null");
	const char* p = s;
	while (p < e) {
		const char* lb = (const char*)memchr(p, '[', e - p);
		if (!lb)
			break;
		const char* rb = (const char*)memchr(lb, ']', e - lb);
		if (!rb)
			break;
		const char* val = rb + 1;
		const char* end = (const char*)memchr(val, '|', e - val);
		if (!end)
			end = e;
		const char* key = lb + 1;
		int kl = (int)(rb - key);
		int vl = (int)(end - val);
		char tmp[XL_PATH_MAX];
		if (_key_is(key, kl, "title"))
			_copy_trim(a->title, XL_TITLE_MAX, val, vl);
		else if (_key_is(key, kl, "icon"))
			_copy_trim(a->icon, XL_PATH_MAX, val, vl);
		else if (_key_is(key, kl, "param"))
			_copy_trim(a->param, XL_PARAM_MAX, val, vl);
		else if (_key_is(key, kl, "app"))
			_copy_trim(a->path, XL_PATH_MAX, val, vl);
		else if (_key_is(key, kl, "size")) {
			_copy_trim(tmp, sizeof(tmp), val, vl);
			a->size = (uint8_t)_parse_size(tmp, a->size);
		} else if (_key_is(key, kl, "pin")) {
			_copy_trim(tmp, sizeof(tmp), val, vl);
			a->defPinned = (tmp[0] == '0' || tmp[0] == 'n' || tmp[0] == 'N') ? 0 : 1;
		} else if (_key_is(key, kl, "color")) {
			_copy_trim(tmp, sizeof(tmp), val, vl);
			a->color = (uint8_t)_parse_color(tmp, a->color);
		}
		p = end + 1;
	}
	if (!a->title[0] || !a->path[0])
		return false;
	a->titleHash = XelnchHash(a->title);
	a->pathHash = XelnchHash(a->path);
	a->col = a->row = -1;
	a->live.progress = XELNCH_NO_PROGRESS;
	return true;
}

/*
 * XlConfigLoad -- read the launcher config, fill xlnch.apps.
 * Returns the number of apps loaded.
 */
int XlConfigLoad(const char* path) {
	xlnch.apps = NULL;
	xlnch.appCount = 0;
	int fd = _KeOpenFile((char*)path, FILE_OPEN_READ_ONLY);
	if (fd == -1) {
		_KePrint("[XELnch]: failed to open %s \r\n", path);
		return 0;
	}
	XEFileStatus st;
	memset(&st, 0, sizeof(st));
	_KeFileStat(fd, &st);
	size_t sz = st.size;
	if (sz == 0 || sz > (256u << 10)) {
		_KeCloseFile(fd);
		return 0;
	}
	char* buf = (char*)malloc(sz + 1);
	if (!buf) {
		_KeCloseFile(fd);
		return 0;
	}
	_KeReadFile(fd, buf, sz);
	_KeCloseFile(fd);
	buf[sz] = '\0';

	int blocks = 0;
	for (size_t i = 0; i < sz; i++)
		if (buf[i] == '{')
			blocks++;
	if (blocks > XL_MAX_APPS)
		blocks = XL_MAX_APPS;
	if (blocks == 0) {
		free(buf);
		return 0;
	}
	xlnch.apps = (XlApp*)malloc(sizeof(XlApp) * blocks);
	if (!xlnch.apps) {
		free(buf);
		return 0;
	}

	char* p = buf;
	char* end = buf + sz;
	while (p < end && xlnch.appCount < blocks) {
		char* open = (char*)memchr(p, '{', end - p);
		if (!open)
			break;
		char* close = open + 1;
		while (close < end && *close != '}' && *close != '>' && *close != '{')
			close++;
		if (_parse_entry(open + 1, close, &xlnch.apps[xlnch.appCount], xlnch.appCount))
			xlnch.appCount++;
		p = close < end ? close + 1 : end;
	}
	free(buf);
	_KePrint("[XELnch]: %d apps from %s \r\n", xlnch.appCount, path);
	return xlnch.appCount;
}
