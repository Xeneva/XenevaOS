/*
 * Process/ipctest/main.cpp -- client half of the generic IPC test. An ordinary
 * application: it holds no daemons group, so it must be able to call "echo" and
 * must be refused everything privileged. Every line is "[ipctest] PASS|FAIL ..."
 * on the serial log, then a SUMMARY.
 */
#include <_xeneva.h>
#include <stdio.h>
#include <string.h>
#include <sys/_kefile.h>
#include <sys/_keproc.h>
#include <auipc.h>

static int g_fd = -1, g_ok, g_bad;

static void check(const char* what, long got, long want) {
	if (got == want) { g_ok++; _KePrint("[ipctest] PASS %s (%d)\r\n", what, (int)got); }
	else { g_bad++; _KePrint("[ipctest] FAIL %s got=%d want=%d\r\n", what, (int)got, (int)want); }
}

static int connect_to(const char* name, int* handle) {
	IpcConnect c;
	memset(&c, 0, sizeof c);
	strcpy(c.name, name);
	c.handle = -1;
	int rc = _KeFileIoControl(g_fd, IPC_IOC_CONNECT, &c);
	*handle = c.handle;
	return rc;
}

/* one full call: returns the status the caller sees; reply text in out */
static int call(int h, const void* req, unsigned len, unsigned timeout_ms, char* out, unsigned cap,
				unsigned* outlen, int* server_status) {
	IpcCall c;
	memset(&c, 0, sizeof c);
	c.handle = h;
	c.req = req;
	c.req_len = len;
	c.timeout_ms = timeout_ms;
	_KeFileIoControl(g_fd, IPC_IOC_CALL, &c);
	if (c.status != IPC_OK)
		return c.status;
	IpcFetch f;
	memset(&f, 0, sizeof f);
	f.call_id = c.call_id;
	f.resp = out;
	f.resp_cap = cap;
	_KeFileIoControl(g_fd, IPC_IOC_FETCH, &f);
	if (outlen) *outlen = f.resp_len;
	if (server_status) *server_status = f.server_status;
	return f.status;
}

int main(int argc, char* argv[]) {
	(void)argc; (void)argv;
	_KePrint("[ipctest] start\r\n");
	g_fd = _KeOpenFile((char*)"/dev/ipc", FILE_OPEN_READ_ONLY);
	if (g_fd < 0) { _KePrint("[ipctest] FAIL open /dev/ipc\r\n"); return 1; }

	int echo = -1, priv = -1, none = -1;
	int rc = -1;
	for (int i = 0; i < 100 && rc != IPC_OK; i++) {          /* the server may still be starting */
		rc = connect_to("echo", &echo);
		if (rc != IPC_OK) _KeProcessSleep(100);
	}
	check("connect to echo", rc, IPC_OK);
	check("connect to a missing endpoint", connect_to("nope", &none), IPC_E_NOENT);
	check("daemons-only endpoint refuses an ordinary app", connect_to("priv", &priv), IPC_E_PERM);

	IpcCreate cr;
	memset(&cr, 0, sizeof cr);
	strcpy(cr.name, "mine");
	check("an ordinary app cannot create an endpoint", _KeFileIoControl(g_fd, IPC_IOC_CREATE, &cr), IPC_E_PERM);

	char out[256];
	unsigned n = 0;
	int ss = -1;
	check("call echo", call(echo, "hello ipc", 9, 1000, out, sizeof out, &n, &ss), IPC_OK);
	out[n < 255 ? n : 255] = 0;
	check("reply is upper-cased", !strcmp(out, "HELLO IPC"), 1);
	check("reply length", (long)n, 9);

	check("call whoami", call(echo, "whoami", 6, 1000, out, sizeof out, &n, &ss), IPC_OK);
	out[n < 255 ? n : 255] = 0;
	_KePrint("[ipctest] server saw the caller as: %s\r\n", out);
	check("server status travels back", ss, 7);
	check("kernel-supplied identity is the caller's, not the server's", strstr(out, "uid=0") == 0, 1);

	char big[IPC_MAX_MSG + 8];
	memset(big, 'a', sizeof big);
	check("oversize request refused", call(echo, big, IPC_MAX_MSG + 1, 1000, out, sizeof out, &n, &ss), IPC_E_INVAL);
	check("maximum-size request accepted", call(echo, big, IPC_MAX_MSG, 1000, out, sizeof out, &n, &ss), IPC_E_TOOBIG);
	check("  ... and the reply length is reported", (long)n, IPC_MAX_MSG);

	check("bogus handle", call(0x7ffff, "x", 1, 100, out, sizeof out, &n, &ss), IPC_E_NOENT);

	check("slow server: caller times out", call(echo, "slow", 4, 300, out, sizeof out, &n, &ss), IPC_E_TIMEOUT);
	_KeProcessSleep(2000);                                   /* the late answer must be dropped silently */
	check("service still healthy after a timeout", call(echo, "again", 5, 1000, out, sizeof out, &n, &ss), IPC_OK);

	for (int i = 0; i < 20; i++) {                           /* many calls, no leak of call slots */
		if (call(echo, "burst", 5, 1000, out, sizeof out, &n, &ss) != IPC_OK) { check("burst of 20 calls", i, 20); break; }
		if (i == 19) check("burst of 20 calls", 20, 20);
	}

	check("server dies mid-call: caller is told", call(echo, "die", 3, 3000, out, sizeof out, &n, &ss), IPC_E_DEAD);
	check("calls to a dead server fail cleanly", call(echo, "x", 1, 200, out, sizeof out, &n, &ss), IPC_E_NOENT);

	_KePrint("[ipctest] SUMMARY %d ok, %d failed\r\n", g_ok, g_bad);
	return g_bad;
}
