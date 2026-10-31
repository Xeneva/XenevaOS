/*
 * Process/srvtest/main.cpp -- exercises the user-space file system mounted at
 * /srv through ordinary open/read/write/seek/close. Two instances run as
 * different users: "owner" (uid 1000) and "other" (uid 1001).
 *
 *   owner: create + write 10000 bytes (several server round trips in one write),
 *          read them back whole, in pieces, and at an offset; missing file,
 *          subdirectory path; then (after "other" finished) kills the server and
 *          checks that clients are not left hanging.
 *   other: can read the owner's file, cannot open it for writing, and a write
 *          through a read-only descriptor is refused by the server.
 */
#include <_xeneva.h>
#include <stdio.h>
#include <string.h>
#include <sys/_kefile.h>
#include <sys/_keproc.h>
#include <sys/_ketime.h>

static int g_ok, g_bad;
static const char* g_who = "owner";

static void check(const char* what, long got, long want) {
	if (got == want) { g_ok++; _KePrint("[srvtest/%s] PASS %s (%d)\r\n", g_who, what, (int)got); }
	else { g_bad++; _KePrint("[srvtest/%s] FAIL %s got=%d want=%d\r\n", g_who, what, (int)got, (int)want); }
}

static unsigned char pat(unsigned i) { return (unsigned char)((i * 7 + (i >> 8)) & 0xFF); }

static int open_retry(const char* path, int mode, int tries) {
	int fd = -1;
	for (int i = 0; i < tries && fd < 0; i++) {
		fd = _KeOpenFile((char*)path, mode);
		if (fd < 0) _KeProcessSleep(100);
	}
	return fd;
}


/* names found in /srv, as a bit set: 1 = hello.txt, 2 = b.txt */
static int list_srv(int* count) {
	int fd = _KeOpenDir((char*)"/srv");
	if (fd < 0) return -1;
	XEDirectoryEntry de;
	memset(&de, 0, sizeof de);
	int seen = 0, n = 0;
	for (int guard = 0; guard < 100 && de.index != -1; guard++) {
		if (_KeReadDir(fd, &de) == 0) {
			n++;
			if (!strcmp(de.filename, "hello.txt")) seen |= 1;
			if (!strcmp(de.filename, "b.txt")) seen |= 2;
		}
	}
	_KeCloseFile(fd);
	if (count) *count = n;
	return seen;
}

static void owner(void) {
	static unsigned char data[10000], back[10000];
	for (unsigned i = 0; i < sizeof data; i++) data[i] = pat(i);

	int fd = open_retry("/srv/hello.txt", FILE_OPEN_WRITE | FILE_OPEN_CREAT, 100);
	check("create and open for writing", fd >= 0, 1);
	if (fd < 0) return;
	check("write 10000 bytes (3 round trips in one syscall)", (long)_KeWriteFile(fd, data, sizeof data), 10000);
	_KeCloseFile(fd);

	fd = _KeOpenFile((char*)"/srv/hello.txt", FILE_OPEN_READ_ONLY);
	check("reopen read-only", fd >= 0, 1);
	if (fd >= 0) {
		size_t total = 0;
		for (int i = 0; i < 10 && total < sizeof back; i++) {      /* reads move at most a page */
			size_t n = _KeReadFile(fd, back + total, sizeof back - total);
			if (!n) break;
			total += n;
		}
		check("read back the whole file", (long)total, 10000);
		check("contents identical", memcmp(data, back, sizeof data) == 0, 1);
		check("read at EOF returns 0", (long)_KeReadFile(fd, back, 100), 0);
		_KeFileSetOffset(fd, 5000);
		unsigned char part[100];
		check("seek then read 100 bytes", (long)_KeReadFile(fd, part, 100), 100);
		check("seeked contents identical", memcmp(part, data + 5000, 100) == 0, 1);
		_KeCloseFile(fd);
	}

	check("open of a missing file fails", _KeOpenFile((char*)"/srv/missing", FILE_OPEN_READ_ONLY) < 0, 1);
	check("a path with a subdirectory fails (flat namespace)", _KeOpenFile((char*)"/srv/a/b", FILE_OPEN_READ_ONLY) < 0, 1);

	int b = _KeOpenFile((char*)"/srv/b.txt", FILE_OPEN_WRITE | FILE_OPEN_CREAT);
	if (b >= 0) { _KeWriteFile(b, (void*)"bee", 3); _KeCloseFile(b); }
	int cnt = 0;
	check("directory listing shows both files", list_srv(&cnt), 3);
	check("remove a file I own", _KeRemoveFile((char*)"/srv/b.txt"), 0);
	check("listing no longer shows it", list_srv(&cnt), 1);
	check("remove a missing file fails", _KeRemoveFile((char*)"/srv/b.txt") < 0, 1);
	check("it can no longer be opened", _KeOpenFile((char*)"/srv/b.txt", FILE_OPEN_READ_ONLY) < 0, 1);

	uint64_t t0 = _KeGetCurrentUS();
	for (int i = 0; i < 20; i++) {
		int f = _KeOpenFile((char*)"/srv/hello.txt", FILE_OPEN_READ_ONLY);
		unsigned char b[16];
		if (f < 0 || _KeReadFile(f, b, 16) != 16) { check("20 open+read cycles", i, 20); break; }
		_KeCloseFile(f);
		if (i == 19) check("20 open+read cycles", 20, 20);
	}
	_KePrint("[srvtest/owner] 20 open+read cycles took %d ms\r\n", (int)((_KeGetCurrentUS() - t0) / 1000));

	/* wait for the other user to finish its checks */
	int done = open_retry("/srv/other.done", FILE_OPEN_READ_ONLY, 200);
	check("the other user's checks completed", done >= 0, 1);
	if (done >= 0) _KeCloseFile(done);

	t0 = _KeGetCurrentUS();
	int dead = _KeOpenFile((char*)"/srv/crash.me", FILE_OPEN_WRITE | FILE_OPEN_CREAT);
	uint64_t ms = (_KeGetCurrentUS() - t0) / 1000;
	check("server dies mid-request: client gets an error", dead < 0, 1);
	check("... and is not left hanging (< 2 s)", ms < 2000, 1);
	/* under svcmgr (it reads /services.cnf) the server must come back by itself; its
	 * files were in memory and are gone, but the file system works again */
	int cnf = _KeOpenFile((char*)"/services.cnf", FILE_OPEN_READ_ONLY);
	if (cnf >= 0) {
		_KeCloseFile(cnf);
		check("the old file is gone with the old server", _KeOpenFile((char*)"/srv/hello.txt", FILE_OPEN_READ_ONLY) < 0, 1);
		int again = open_retry("/srv/again.txt", FILE_OPEN_WRITE | FILE_OPEN_CREAT, 200);
		check("supervisor restarted the server: /srv works again", again >= 0, 1);
		if (again >= 0) {
			check("write after the restart", (long)_KeWriteFile(again, (void*)"back", 4), 4);
			_KeCloseFile(again);
			int rd = _KeOpenFile((char*)"/srv/again.txt", FILE_OPEN_READ_ONLY);
			char b[8] = {0};
			check("read after the restart", rd >= 0 && _KeReadFile(rd, b, 4) == 4 && !memcmp(b, "back", 4), 1);
		}
		return;
	}
	t0 = _KeGetCurrentUS();
	check("later opens fail at once", _KeOpenFile((char*)"/srv/hello.txt", FILE_OPEN_READ_ONLY) < 0, 1);
	check("... without waiting (< 500 ms)", (_KeGetCurrentUS() - t0) / 1000 < 500, 1);
}

static void other(void) {
	g_who = "other";
	int fd = open_retry("/srv/hello.txt", FILE_OPEN_READ_ONLY, 100);
	check("another user can open for reading", fd >= 0, 1);
	if (fd >= 0) {
		unsigned char b[64];
		check("... and read the owner's data", (long)_KeReadFile(fd, b, 64), 64);
		bool same = true;
		for (unsigned i = 0; i < 64; i++) if (b[i] != pat(i)) same = false;
		check("... which is intact", same, 1);
		unsigned char junk[16];
		memset(junk, 0xEE, sizeof junk);
		_KeFileSetOffset(fd, 0);
		_KeWriteFile(fd, junk, sizeof junk);            /* read-only open grants the fd write rights; the server must refuse */
		_KeFileSetOffset(fd, 0);
		check("server refuses a write by a non-owner", (long)_KeReadFile(fd, b, 16), 16);
		check("... and the data is untouched", b[0] == pat(0) && b[1] == pat(1), 1);
		_KeCloseFile(fd);
	}
	check("a non-owner cannot open for writing", _KeOpenFile((char*)"/srv/hello.txt", FILE_OPEN_WRITE) < 0, 1);
	check("a non-owner cannot remove the owner's file", _KeRemoveFile((char*)"/srv/hello.txt") < 0, 1);
	int fd2 = _KeOpenFile((char*)"/srv/hello.txt", FILE_OPEN_READ_ONLY);
	check("... and it is still there", fd2 >= 0, 1);
	if (fd2 >= 0) _KeCloseFile(fd2);
	int mine = _KeOpenFile((char*)"/srv/other.done", FILE_OPEN_WRITE | FILE_OPEN_CREAT);
	check("a non-owner can create its own file", mine >= 0, 1);
	if (mine >= 0) { _KeWriteFile(mine, (void*)"x", 1); _KeCloseFile(mine); }
}

int main(int argc, char* argv[]) {
	bool is_other = false;
	for (int i = 0; i < argc; i++)
		if (argv[i] && strstr(argv[i], "other")) is_other = true;
	_KePrint("[srvtest] start (%s)\r\n", is_other ? "other" : "owner");
	if (is_other) other(); else owner();
	_KePrint("[srvtest/%s] SUMMARY %d ok, %d failed\r\n", g_who, g_ok, g_bad);
	return g_bad;
}
