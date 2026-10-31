/*
 * Process/fssrv/main.cpp -- a tmpfs served to the kernel's /srv mount.
 *
 * Creates the IPC endpoint "fs" (daemons only: applications never talk to it
 * directly, the kernel forwards their open/read/write with the caller's uid) and
 * keeps a flat set of files in memory. Permissions are decided here, from the uid
 * the kernel stamped on the request: anyone may read, only the owner (or uid 0)
 * may write. Runs as a MISC_DAEMONS process.
 *
 * Test hook: opening the name "crash.me" kills the server mid-request.
 */
#include <_xeneva.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/_kefile.h>
#include <sys/_keproc.h>
#include <auipc.h>
#include <fssrv.h>

#define MAX_FILES    64
#define MAX_FILE_SZ  (256 * 1024)

struct File { bool used; char name[FSSRV_NAME_MAX + 1]; int owner; uint8_t* data; uint32_t size, cap; };

static File g_files[MAX_FILES];
static int g_fd = -1, g_ep = -1;
static unsigned g_requests;

static File* lookup(const char* name) {
	for (int i = 0; i < MAX_FILES; i++)
		if (g_files[i].used && !strcmp(g_files[i].name, name))
			return &g_files[i];
	return 0;
}

static File* create(const char* name, int owner) {
	for (int i = 0; i < MAX_FILES; i++) {
		if (!g_files[i].used) {
			memset(&g_files[i], 0, sizeof(File));
			g_files[i].used = true;
			strcpy(g_files[i].name, name);
			g_files[i].owner = owner;
			return &g_files[i];
		}
	}
	return 0;
}

static bool may_write(File* f, int uid) {
	return uid == 0 || uid == f->owner;
}

static int grow(File* f, uint32_t need) {
	if (need > MAX_FILE_SZ)
		return -1;
	if (need <= f->cap)
		return 0;
	uint32_t cap = f->cap ? f->cap : 4096;
	while (cap < need)
		cap *= 2;
	uint8_t* p = (uint8_t*)malloc(cap);
	if (!p)
		return -1;
	memset(p, 0, cap);
	if (f->data) {
		memcpy(p, f->data, f->size);
		free(f->data);
	}
	f->data = p;
	f->cap = cap;
	return 0;
}

static void answer(int call_id, FsResp* resp, const void* data, unsigned dlen) {
	static uint8_t out[sizeof(FsResp) + FSSRV_MAX_DATA];
	resp->len = dlen;
	memcpy(out, resp, sizeof(FsResp));
	if (dlen)
		memcpy(out + sizeof(FsResp), (void*)data, dlen);
	IpcReply r;
	memset(&r, 0, sizeof r);
	r.handle = g_ep;
	r.call_id = call_id;
	r.resp = out;
	r.len = sizeof(FsResp) + dlen;
	_KeFileIoControl(g_fd, IPC_IOC_REPLY, &r);
}

int main(int argc, char* argv[]) {
	(void)argc; (void)argv;
	g_fd = _KeOpenFile((char*)"/dev/ipc", FILE_OPEN_READ_ONLY);
	if (g_fd < 0) {
		_KePrint("[fssrv] cannot open /dev/ipc\r\n");
		return 1;
	}
	IpcCreate c;
	memset(&c, 0, sizeof c);
	strcpy(c.name, FSSRV_ENDPOINT);
	c.acl = IPC_ACL_DAEMONS;
	c.handle = -1;
	if (_KeFileIoControl(g_fd, IPC_IOC_CREATE, &c) != IPC_OK) {
		_KePrint("[fssrv] cannot create the '%s' endpoint\r\n", FSSRV_ENDPOINT);
		return 1;
	}
	g_ep = c.handle;
	_KePrint("[fssrv] serving /srv\r\n");

	static uint8_t buf[IPC_MAX_MSG];
	for (;;) {
		IpcWait w = {g_ep, 200, 0, 0};
		_KeFileIoControl(g_fd, IPC_IOC_WAIT, &w);
		for (;;) {
			IpcRecv r;
			memset(&r, 0, sizeof r);
			r.handle = g_ep;
			r.buf = buf;
			r.cap = sizeof buf;
			_KeFileIoControl(g_fd, IPC_IOC_RECV, &r);
			if (r.status != IPC_OK)
				break;
			g_requests++;
			FsResp resp;
			memset(&resp, 0, sizeof resp);
			if (r.len < sizeof(FsReq)) {
				resp.status = FSSRV_E_INVAL;
				answer(r.call_id, &resp, 0, 0);
				continue;
			}
			FsReq* req = (FsReq*)buf;
			req->name[FSSRV_NAME_MAX] = 0;
			int uid = r.sender_uid;
			File* f = lookup(req->name);
			if (req->op == FSSRV_OP_OPEN) {
				if (!strcmp(req->name, "crash.me")) {
					_KePrint("[fssrv] test hook: dying in the middle of a request\r\n");
					*(volatile int*)0 = 1;
				}
				if (!f && (req->flags & FSSRV_F_CREATE)) {
					f = create(req->name, uid);
					if (f)
						_KePrint("[fssrv] uid %d created '%s'\r\n", uid, req->name);
				}
				if (!f) {
					resp.status = FSSRV_E_NOENT;
				} else if ((req->flags & FSSRV_F_WRITE) && !may_write(f, uid)) {
					resp.status = FSSRV_E_PERM;
				} else {
					resp.status = FSSRV_OK;
					resp.size = f->size;
					resp.uid = (uint32_t)f->owner;
				}
				answer(r.call_id, &resp, 0, 0);
			} else if (req->op == FSSRV_OP_READ) {
				if (!f) {
					resp.status = FSSRV_E_NOENT;
					answer(r.call_id, &resp, 0, 0);
				} else {
					uint32_t n = 0;
					if (req->offset < f->size) {
						n = f->size - req->offset;
						if (n > req->len) n = req->len;
						if (n > FSSRV_MAX_DATA) n = FSSRV_MAX_DATA;
					}
					resp.status = FSSRV_OK;
					resp.size = f->size;
					resp.uid = (uint32_t)f->owner;
					answer(r.call_id, &resp, n ? f->data + req->offset : 0, n);
				}
			} else if (req->op == FSSRV_OP_READDIR) {
				int want = (int)req->offset, seen = 0;
				File* hit = 0;
				for (int i = 0; i < MAX_FILES; i++) {
					if (!g_files[i].used) continue;
					if (seen++ == want) { hit = &g_files[i]; break; }
				}
				if (!hit) {
					resp.status = FSSRV_E_NOENT;
					answer(r.call_id, &resp, 0, 0);
				} else {
					FsDirEnt de;
					memset(&de, 0, sizeof de);
					strcpy(de.name, hit->name);
					de.size = hit->size;
					de.uid = (uint32_t)hit->owner;
					resp.status = FSSRV_OK;
					answer(r.call_id, &resp, &de, sizeof de);
				}
			} else if (req->op == FSSRV_OP_REMOVE) {
				if (!f) {
					resp.status = FSSRV_E_NOENT;
				} else if (!may_write(f, uid)) {
					_KePrint("[fssrv] uid %d denied removing '%s' (owner %d)\r\n", uid, f->name, f->owner);
					resp.status = FSSRV_E_PERM;
				} else {
					if (f->data) free(f->data);
					memset(f, 0, sizeof(File));
					resp.status = FSSRV_OK;
				}
				answer(r.call_id, &resp, 0, 0);
			} else if (req->op == FSSRV_OP_WRITE) {
				uint32_t dl = r.len - (uint32_t)sizeof(FsReq);
				if (!f) {
					resp.status = FSSRV_E_NOENT;
				} else if (!may_write(f, uid)) {
					_KePrint("[fssrv] uid %d denied write to '%s' (owner %d)\r\n", uid, f->name, f->owner);
					resp.status = FSSRV_E_PERM;
				} else if (dl > req->len || req->offset > MAX_FILE_SZ || grow(f, req->offset + dl) != 0) {
					resp.status = FSSRV_E_NOSPC;
				} else {
					memcpy(f->data + req->offset, buf + sizeof(FsReq), dl);
					if (req->offset + dl > f->size)
						f->size = req->offset + dl;
					resp.status = FSSRV_OK;
					resp.size = f->size;
					resp.uid = (uint32_t)f->owner;
				}
				answer(r.call_id, &resp, 0, 0);
			} else {
				resp.status = FSSRV_E_INVAL;
				answer(r.call_id, &resp, 0, 0);
			}
		}
	}
	return 0;
}
