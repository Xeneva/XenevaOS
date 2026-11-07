/**
 * srvfs.c -- /srv, a file system served by a user-space process.
 *
 * The kernel keeps no file data and no directory: every open/read/write on a
 * /srv path becomes a request to the IPC endpoint "fs" (BaseHdr/fssrv.h), stamped
 * with the caller's uid. The server (Process/fssrv) owns the namespace.
 *
 * The hard part is waiting. A syscall that sleeps does not return up the C
 * stack: the thread resumes *after* the svc instruction. So a proxy operation
 * that has to wait for the server does this instead:
 *
 *   1. first execution: queue the request, remember it, rewind ELR_EL1 by one
 *      instruction and sleep -- the thread resumes by re-executing the svc;
 *   2. re-execution: the syscall runs again from its entry. Each proxy operation
 *      is matched, in order, against this thread's history for the syscall;
 *      a finished one returns its saved answer without asking the server again,
 *      a pending one polls (and sleeps again if still unanswered).
 *
 * Positions are restored on replay so side effects of an earlier pass are not
 * applied twice. The history is dropped when the syscall finally returns (the
 * dispatcher calls AuSrvfsSyscallExit). A request the server does not answer in
 * SRV_TIMEOUT_MS fails with an error, so a dead or stuck server never hangs a
 * client. Reads and writes move at most one page per operation.
 */
#include <Fs/srvfs.h>
#include <Fs/vfs.h>
#include <Ipc/auipc.h>
#include <Fs/Dev/devhw.h>
#include <Cred/group.h>
#include <Mm/kmalloc.h>
#include <Drivers/uart.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Hal/AA64/sched.h>
#include <Serv/sysserv.h>
#include <process.h>
#include <auipc.h>
#include <fssrv.h>
#include <string.h>
#include <_null.h>

#define SRV_TIMEOUT_MS   3000
#define MAX_HIST         8
#define MAX_OPS          16
#define HIST_STALE_US    (15ULL * 1000 * 1000)

extern uint64_t AuGetCurrentUS();

enum { OP_FREE = 0, OP_WAIT, OP_DONE };

typedef struct {
	int state;
	uint64_t sig;
	int32_t call_id;
	int32_t rc;                      /* IPC-level result */
	uint32_t pos_before;
	uint64_t deadline_us;
	FsResp resp;
	uint8_t* data;                   /* READ payload, kept until the syscall returns */
	uint32_t data_len;
} Op;

typedef struct {
	uint64_t tid;                    /* 0 = unused */
	uint64_t born_us;
	int n;                           /* operations recorded in this syscall */
	int pass;                        /* operations seen in the current execution */
	Op ops[MAX_OPS];
} Hist;

static Hist g_hist[MAX_HIST];
int g_srvfs_active;
static int g_open_mode;
static AuVFSNode* g_fs;

void AuSrvfsNoteOpenMode(int mode) {
	g_open_mode = mode;
}

static Hist* hist_find(uint64_t tid) {
	for (int i = 0; i < MAX_HIST; i++)
		if (g_hist[i].tid == tid)
			return &g_hist[i];
	return NULL;
}

static void hist_drop(Hist* h) {
	for (int i = 0; i < h->n; i++) {
		if (h->ops[i].state == OP_WAIT)
			AuIpcKCallAbandon(h->ops[i].call_id);
		if (h->ops[i].data)
			kfree(h->ops[i].data);
	}
	memset(h, 0, sizeof(*h));
	g_srvfs_active--;
}

static Hist* hist_get(uint64_t tid) {
	Hist* h = hist_find(tid);
	if (h)
		return h;
	uint64_t now = AuGetCurrentUS();
	for (int i = 0; i < MAX_HIST; i++) {
		if (g_hist[i].tid && now - g_hist[i].born_us > HIST_STALE_US)
			hist_drop(&g_hist[i]);       /* its thread died mid-operation */
	}
	for (int i = 0; i < MAX_HIST; i++) {
		if (!g_hist[i].tid) {
			memset(&g_hist[i], 0, sizeof(g_hist[i]));
			g_hist[i].tid = tid;
			g_hist[i].born_us = now;
			g_srvfs_active++;
			return &g_hist[i];
		}
	}
	return NULL;
}

void AuSrvfsSyscallEnterSlow(AA64Thread* thr) {
	Hist* h = hist_find(thr->thread_id);
	if (h)
		h->pass = 0;
}

void AuSrvfsSyscallExitSlow(AA64Thread* thr) {
	Hist* h = hist_find(thr->thread_id);
	if (h)
		hist_drop(h);
}

/* sleep and come back by re-executing the svc; does not return */
static void sleep_and_restart(AA64Thread* thr, uint32_t ms) {
	uint64_t elr;
	mask_irqs();
	__asm__ volatile("mrs %0, elr_el1" : "=r"(elr));
	elr -= 4;
	__asm__ volatile("msr elr_el1, %0" : : "r"(elr));
	AuSleepThread(thr, ms ? ms : 1);
	AuScheduleThread(AA64GetCurrentRegCtx());
}

/* The offset is deliberately not part of the signature: a replay starts with the
 * file position an earlier pass already advanced, and restoring it is srv_op's job. */
static uint64_t sig_of(uint32_t op, uint32_t flags, uint32_t len, const char* name) {
	uint64_t h = 1469598103934665603ULL ^ op;
	h = (h * 1099511628211ULL) ^ flags;
	h = (h * 1099511628211ULL) ^ len;
	for (; *name; name++)
		h = (h * 1099511628211ULL) ^ (uint8_t)*name;
	return h;
}

/* Runs one request/answer exchange. Returns IPC_OK with *resp filled and any READ
 * payload copied to `out`; or an IPC_E_* (no server, timeout, server died).
 * `pos` is restored to its value at the first execution before a replay. */
static int srv_op(uint32_t* pos, const FsReq* req, const void* wdata, uint32_t wlen, void* out,
				  uint32_t outcap, FsResp* resp) {
	AA64Thread* thr = AuGetCurrentThread();
	AuProcess* proc = AuHwdevCurrentProcess();
	if (!thr || !proc)
		return IPC_E_PERM;
	uint64_t sig = sig_of(req->op, req->flags, req->len, req->name);
	Hist* h = hist_get(thr->thread_id);
	if (!h)
		return IPC_E_BUSY;
	int idx = h->pass++;
	Op* op = NULL;
	if (idx < h->n) {
		op = &h->ops[idx];
		if (op->sig != sig) {
			/* the syscall is not repeating what we recorded: start over */
			for (int i = idx; i < h->n; i++) {
				if (h->ops[i].state == OP_WAIT)
					AuIpcKCallAbandon(h->ops[i].call_id);
				if (h->ops[i].data)
					kfree(h->ops[i].data);
				memset(&h->ops[i], 0, sizeof(Op));
			}
			h->n = idx;
			op = NULL;
		}
	}
	if (!op) {
		if (h->n >= MAX_OPS)
			return IPC_E_BUSY;
		op = &h->ops[h->n++];
		memset(op, 0, sizeof(*op));
		op->sig = sig;
		op->pos_before = *pos;
		uint32_t total = (uint32_t)sizeof(FsReq) + wlen;
		uint8_t* msg = (uint8_t*)kmalloc(total);
		if (!msg) {
			op->state = OP_DONE;
			op->rc = IPC_E_BUSY;
		} else {
			memcpy(msg, req, sizeof(FsReq));
			if (wlen)
				memcpy(msg + sizeof(FsReq), wdata, wlen);
			int rc = AuIpcKCallBegin(FSSRV_ENDPOINT, proc->proc_id, proc->creds.uid, proc->creds.gid, msg, total,
									 &op->call_id);
			kfree(msg);
			if (rc != IPC_OK) {
				op->state = OP_DONE;
				op->rc = rc;                       /* no server: fail at once */
			} else {
				op->state = OP_WAIT;
				op->deadline_us = AuGetCurrentUS() + (uint64_t)SRV_TIMEOUT_MS * 1000;
				sleep_and_restart(thr, SRV_TIMEOUT_MS);   /* does not return */
			}
		}
	} else {
		*pos = op->pos_before;                    /* replay: undo the earlier pass's side effects */
	}

	if (op->state == OP_WAIT) {
		uint8_t* buf = (uint8_t*)kmalloc(sizeof(FsResp) + FSSRV_MAX_DATA);
		uint32_t n = 0;
		int32_t sstatus = 0;
		int rc = buf ? AuIpcKCallPoll(op->call_id, buf, sizeof(FsResp) + FSSRV_MAX_DATA, &n, &sstatus) : IPC_E_BUSY;
		if (rc == IPC_E_TIMEOUT) {
			kfree(buf);
			uint64_t now = AuGetCurrentUS();
			if (now >= op->deadline_us) {
				AuIpcKCallAbandon(op->call_id);
				op->state = OP_DONE;
				op->rc = IPC_E_TIMEOUT;
			} else {
				sleep_and_restart(thr, (uint32_t)((op->deadline_us - now) / 1000) + 1);
			}
		} else if (rc == IPC_OK && n >= sizeof(FsResp)) {
			memcpy(&op->resp, buf, sizeof(FsResp));
			uint32_t dl = n - (uint32_t)sizeof(FsResp);
			if (dl > op->resp.len)
				dl = op->resp.len;
			if (dl) {
				op->data = (uint8_t*)kmalloc(dl);
				if (op->data) {
					memcpy(op->data, buf + sizeof(FsResp), dl);
					op->data_len = dl;
				}
			}
			kfree(buf);
			op->state = OP_DONE;
			op->rc = IPC_OK;
		} else {
			if (buf) kfree(buf);
			op->state = OP_DONE;
			op->rc = rc == IPC_OK ? IPC_E_INVAL : rc;
		}
	}

	if (op->rc == IPC_OK) {
		*resp = op->resp;
		if (out && op->data_len) {
			uint32_t n = op->data_len < outcap ? op->data_len : outcap;
			memcpy(out, op->data, n);
			resp->len = n;
		} else {
			resp->len = 0;
		}
	}
	return op->rc;
}

/* ---- the file system node ---- */
static AuVFSNode* srv_open(AuVFSNode* node, char* path) {
	if (!path)
		return node;                      /* reference-count bump by OpenFile */
	if (*path == '/')
		path++;
	size_t n = strlen(path);
	if (!n || n > FSSRV_NAME_MAX || strchr(path, '/'))
		return NULL;                      /* flat namespace; no directory listing */
	FsReq req;
	memset(&req, 0, sizeof(req));
	req.op = FSSRV_OP_OPEN;
	if (g_open_mode & (FILE_OPEN_CREAT | FILE_OPEN_WRITE))
		req.flags = FSSRV_F_CREATE | FSSRV_F_WRITE;
	strcpy(req.name, path);
	FsResp resp;
	uint32_t dummy = 0;
	int rc = srv_op(&dummy, &req, NULL, 0, NULL, 0, &resp);
	if (rc != IPC_OK || resp.status != FSSRV_OK)
		return NULL;
	AuVFSNode* f = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	if (!f)
		return NULL;
	memset(f, 0, sizeof(*f));
	strcpy(f->filename, path);
	f->flags = FS_FLAG_GENERAL;
	f->size = resp.size;
	f->uid = resp.uid;
	f->gid = AuCredGetGroupID(AURORA_GID_MISC_WORLD);   /* every application; the server decides who may write */
	f->device = node;
	f->open = srv_open;
	f->read = node->read;
	f->write = node->write;
	return f;
}

static size_t srv_read(AuVFSNode* fs, AuVFSNode* file, uint64_t* buffer, uint32_t length) {
	(void)fs;
	if (length > FSSRV_MAX_DATA)
		length = FSSRV_MAX_DATA;
	FsReq req;
	memset(&req, 0, sizeof(req));
	req.op = FSSRV_OP_READ;
	req.offset = file->pos;
	req.len = length;
	strcpy(req.name, file->filename);
	FsResp resp;
	int rc = srv_op(&file->pos, &req, NULL, 0, buffer, length, &resp);
	if (rc != IPC_OK || resp.status != FSSRV_OK)
		return 0;
	file->pos += resp.len;
	if (resp.len == 0)
		file->eof = 1;
	return resp.len;
}

static size_t srv_write(AuVFSNode* fs, AuVFSNode* file, uint64_t* buffer, uint32_t length) {
	(void)fs;
	if (length > FSSRV_MAX_DATA)
		length = FSSRV_MAX_DATA;
	FsReq req;
	memset(&req, 0, sizeof(req));
	req.op = FSSRV_OP_WRITE;
	req.offset = file->pos;
	req.len = length;
	strcpy(req.name, file->filename);
	FsResp resp;
	int rc = srv_op(&file->pos, &req, buffer, length, NULL, 0, &resp);
	if (rc != IPC_OK || resp.status != FSSRV_OK)
		return 0;
	file->pos += length;
	if (file->pos > file->size)
		file->size = file->pos;
	return length;
}

/* /srv has one directory, its root. */
static AuVFSNode* srv_opendir(AuVFSNode* fs, char* dirname) {
	const char* p = dirname;
	if (*p == '/')
		p++;
	if (strncmp(p, "srv", 3) != 0)
		return NULL;
	p += 3;
	if (*p == '/')
		p++;
	if (*p)
		return NULL;                      /* no subdirectories */
	AuVFSNode* d = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	if (!d)
		return NULL;
	memset(d, 0, sizeof(*d));
	strcpy(d->filename, "srv");
	d->flags = FS_FLAG_DIRECTORY;
	d->device = fs;
	return d;
}

/* dirent->index is the entry to fetch; same protocol as the FAT driver: 0 = an
 * entry was filled in, -1 with index == -1 = the end */
static int srv_readdir(AuVFSNode* fs, AuVFSNode* dir, AuDirectoryEntry* dirent) {
	(void)fs; (void)dir;
	if (dirent->index < 0)
		return -1;
	FsReq req;
	memset(&req, 0, sizeof(req));
	req.op = FSSRV_OP_READDIR;
	req.offset = (uint32_t)dirent->index;
	FsResp resp;
	FsDirEnt ent;
	uint32_t dummy = 0;
	int rc = srv_op(&dummy, &req, NULL, 0, &ent, sizeof(ent), &resp);
	if (rc != IPC_OK || resp.status != FSSRV_OK || resp.len < sizeof(ent)) {
		dirent->index = -1;
		return -1;
	}
	ent.name[FSSRV_NAME_MAX] = 0;
	memset(dirent->filename, 0, sizeof(dirent->filename));
	strcpy(dirent->filename, ent.name);
	dirent->size = (int)ent.size;
	dirent->flags = FS_FLAG_GENERAL;
	dirent->index += 1;
	return 0;
}

/* RemoveFile(2) for /srv paths; the server checks ownership */
int AuSrvfsRemove(const char* path) {
	if (!path)
		return -1;
	const char* p = path;
	if (*p == '/')
		p++;
	if (strncmp(p, "srv/", 4) != 0)
		return -1;
	p += 4;
	size_t n = strlen(p);
	if (!n || n > FSSRV_NAME_MAX || strchr((char*)p, '/'))
		return -1;
	FsReq req;
	memset(&req, 0, sizeof(req));
	req.op = FSSRV_OP_REMOVE;
	strcpy(req.name, p);
	FsResp resp;
	uint32_t dummy = 0;
	int rc = srv_op(&dummy, &req, NULL, 0, NULL, 0, &resp);
	return (rc == IPC_OK && resp.status == FSSRV_OK) ? 0 : -1;
}

bool AuSrvfsOwns(AuVFSNode* fsys) {
	return fsys && fsys == g_fs;
}

void AuSrvfsInitialize(void) {
	AuVFSNode* fs = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(fs, 0, sizeof(*fs));
	strcpy(fs->filename, "srv");
	fs->flags = FS_FLAG_FILE_SYSTEM;
	fs->open = srv_open;
	fs->read = srv_read;
	fs->write = srv_write;
	fs->opendir = srv_opendir;
	fs->read_dir = srv_readdir;
	g_fs = fs;
	AuVFSAddFileSystem(fs);
	UARTDebugOut("[srvfs]: /srv mounted (served by endpoint '%s')\r\n", FSSRV_ENDPOINT);
}
