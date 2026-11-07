/*
 * Process/ipcsrv/main.cpp -- test service for the generic kernel IPC (/dev/ipc).
 *
 * Creates two endpoints and serves them:
 *   "echo"  (anyone may call)      request text -> upper-cased text
 *                                  "whoami"     -> the caller's pid/uid/gid as the kernel saw them
 *                                  "slow"       -> answers after 1.5 s (caller times out first)
 *                                  "die"        -> exits without answering (caller must see IPC_E_DEAD)
 *   "priv"  (daemons only)         request text -> "ok"
 * Runs as a MISC_DAEMONS process. Results go to the serial log via _KePrint.
 */
#include <_xeneva.h>
#include <stdio.h>
#include <string.h>
#include <sys/_kefile.h>
#include <sys/_keproc.h>
#include <auipc.h>

static int g_fd = -1;

static int make_endpoint(const char* name, unsigned acl) {
	IpcCreate c;
	memset(&c, 0, sizeof c);
	strcpy(c.name, name);
	c.acl = acl;
	c.handle = -1;
	int rc = _KeFileIoControl(g_fd, IPC_IOC_CREATE, &c);
	_KePrint("[ipcsrv] create '%s' -> %d\r\n", name, rc);
	return rc == IPC_OK ? c.handle : -1;
}

static void answer(int h, int call_id, const void* data, unsigned len, int st) {
	IpcReply r;
	memset(&r, 0, sizeof r);
	r.handle = h;
	r.call_id = call_id;
	r.resp = data;
	r.len = len;
	r.server_status = st;
	int rc = _KeFileIoControl(g_fd, IPC_IOC_REPLY, &r);
	if (rc != IPC_OK)
		_KePrint("[ipcsrv] reply failed %d\r\n", rc);
}

int main(int argc, char* argv[]) {
	(void)argc; (void)argv;
	g_fd = _KeOpenFile((char*)"/dev/ipc", FILE_OPEN_READ_ONLY);
	if (g_fd < 0) {
		_KePrint("[ipcsrv] cannot open /dev/ipc\r\n");
		return 1;
	}
	int echo = make_endpoint("echo", IPC_ACL_ANY);
	int priv = make_endpoint("priv", IPC_ACL_DAEMONS);
	if (echo < 0 || priv < 0)
		return 1;
	/* a second server must not be able to take a name that is in use */
	_KePrint("[ipcsrv] duplicate create -> %d (want %d)\r\n", make_endpoint("echo", IPC_ACL_ANY), -1);

	static char buf[IPC_MAX_MSG + 1];
	static char out[IPC_MAX_MSG + 64];
	for (;;) {
		for (int ep = 0; ep < 2; ep++) {
			int h = ep ? priv : echo;
			IpcWait w = {h, ep ? 1u : 150u, 0, 0};
			_KeFileIoControl(g_fd, IPC_IOC_WAIT, &w);
			for (;;) {
				IpcRecv r;
				memset(&r, 0, sizeof r);
				r.handle = h;
				r.buf = buf;
				r.cap = IPC_MAX_MSG;
				_KeFileIoControl(g_fd, IPC_IOC_RECV, &r);
				if (r.status != IPC_OK)
					break;
				buf[r.len] = 0;
				if (ep) {
					answer(h, r.call_id, "ok", 2, 0);
				} else if (!strcmp(buf, "whoami")) {
					int n = sprintf(out, "pid=%d uid=%d gid=%d", r.sender_pid, r.sender_uid, r.sender_gid);
					answer(h, r.call_id, out, (unsigned)n, 7);
				} else if (!strcmp(buf, "slow")) {
					_KeProcessSleep(1500);
					answer(h, r.call_id, "late", 4, 0);
				} else if (!strcmp(buf, "die")) {
					_KePrint("[ipcsrv] dying on purpose with a call in service\r\n");
					*(volatile int*)0 = 1;
				} else {
					unsigned i;
					for (i = 0; i < r.len; i++)
						out[i] = (buf[i] >= 'a' && buf[i] <= 'z') ? (char)(buf[i] - 32) : buf[i];
					answer(h, r.call_id, out, r.len, 0);
				}
			}
		}
	}
	return 0;
}
