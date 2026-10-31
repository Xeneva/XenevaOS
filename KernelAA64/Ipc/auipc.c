/**
 * auipc.c -- generic kernel IPC (see BaseHdr/auipc.h).
 *
 * All state lives in two small fixed tables guarded by masking interrupts: the
 * kernel is single-CPU here and no operation here blocks while holding them.
 * A sleeping operation checks its condition and goes to sleep with interrupts
 * masked, so a wake-up cannot slip in between (same rule as /dev/hwdev IRQ_WAIT).
 */
#include <Ipc/auipc.h>
#include <Fs/Dev/devhw.h>
#include <Fs/Dev/devfs.h>
#include <Fs/vfs.h>
#include <Cred/group.h>
#include <Mm/kmalloc.h>
#include <Drivers/uart.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Hal/AA64/sched.h>
#include <Serv/sysserv.h>
#include <auipc.h>
#include <string.h>
#include <_null.h>

#define MAX_EP        16
#define MAX_CALLS     32
#define EP_QUEUE      8

enum { CS_FREE = 0, CS_QUEUED, CS_SERVING, CS_DONE, CS_FAILED, CS_ABANDONED };

typedef struct {
	bool used;
	uint32_t gen;
	char name[IPC_NAME_MAX];
	AuProcess* owner;
	int owner_pid;
	uint32_t acl;
	uint64_t waiter_tid;           /* server thread sleeping in WAIT, 0 if none */
	int queue[EP_QUEUE];           /* call slots, FIFO */
	int qcount;
} Endpoint;

typedef struct {
	int state;
	uint32_t gen;
	int ep;                        /* endpoint slot */
	int ep_gen;
	int client_pid, client_uid, client_gid;
	uint64_t client_tid;
	uint8_t* req;
	uint32_t req_len;
	uint8_t* resp;
	uint32_t resp_len;
	int32_t server_status;
	int32_t fail;                  /* IPC_E_* when state == CS_FAILED */
} Call;

static Endpoint g_ep[MAX_EP];
static Call g_call[MAX_CALLS];
static uint32_t g_gen_counter = 1;

static bool ipc_user_ptr(const void* p, size_t len) {
	uint64_t a = (uint64_t)p;
	return a && a + len > a && a + len < 0x0000800000000000ULL;
}

static int32_t make_id(uint32_t gen, int slot) { return (int32_t)((gen << 8) | (uint32_t)slot); }

static Endpoint* ep_from_handle(int32_t h) {
	int slot = h & 0xFF;
	if (slot >= MAX_EP || !g_ep[slot].used || (uint32_t)(h >> 8) != g_ep[slot].gen)
		return NULL;
	return &g_ep[slot];
}

static Call* call_from_id(int32_t id) {
	int slot = id & 0xFF;
	if (slot >= MAX_CALLS || g_call[slot].state == CS_FREE || (uint32_t)(id >> 8) != g_call[slot].gen)
		return NULL;
	return &g_call[slot];
}

static void call_free(Call* c) {
	if (c->req) kfree(c->req);
	if (c->resp) kfree(c->resp);
	memset(c, 0, sizeof(*c));          /* state = CS_FREE */
}

static void wake_tid(uint64_t tid) {
	if (!tid)
		return;
	AA64Thread* t = AuThreadFindByIDSleepList(tid);
	if (t)
		AuThreadMakeReady(t);
}

/* fail a call from the server side (endpoint gone): the client is woken */
static void call_fail(Call* c, int32_t why) {
	if (c->state == CS_ABANDONED) {
		call_free(c);
		return;
	}
	c->state = CS_FAILED;
	c->fail = why;
	wake_tid(c->client_tid);
}

void AuIpcProcessGone(AuProcess* proc) {
	if (!proc)
		return;
	uint64_t daif = read_daif();
	mask_irqs();
	for (int i = 0; i < MAX_EP; i++) {
		Endpoint* e = &g_ep[i];
		if (!e->used || e->owner != proc)
			continue;
		for (int c = 0; c < MAX_CALLS; c++)
			if (g_call[c].state != CS_FREE && g_call[c].ep == i && g_call[c].ep_gen == (int)e->gen &&
				(g_call[c].state == CS_QUEUED || g_call[c].state == CS_SERVING))
				call_fail(&g_call[c], IPC_E_DEAD);
		UARTDebugOut("[ipc]: endpoint '%s' removed (owner pid %d gone)\r\n", e->name, e->owner_pid);
		memset(e, 0, sizeof(*e));
		e->gen = g_gen_counter++;
	}
	/* calls this process had in flight: nobody is going to fetch them */
	for (int c = 0; c < MAX_CALLS; c++) {
		Call* cl = &g_call[c];
		if (cl->state == CS_FREE || cl->client_pid != proc->proc_id)
			continue;
		if (cl->state == CS_QUEUED || cl->state == CS_SERVING)
			cl->state = CS_ABANDONED;      /* freed when the server answers / skips it */
		else
			call_free(cl);
	}
	restore_daif(daif);
}

/* sleep the current thread for up to ms; interrupts must be masked by the caller,
 * who restores nothing: the syscall exit path trusts the live state */
static void ipc_sleep(uint32_t ms) {
	AA64Thread* thr = AuGetCurrentThread();
	AuSleepThread(thr, ms);
	AuScheduleThread(AA64GetCurrentRegCtx());     /* resumes after the svc */
}

static uint32_t clamp_ms(uint32_t ms) {
	if (ms < 1) return 1;
	if (ms > 5000) return 5000;
	return ms;
}

/* A server that has died stays a zombie until its parent reaps it, which can be
 * after a supervisor has already started its replacement. Tear its endpoints
 * down as soon as anyone looks, so the name is free and callers fail at once. */
static void ipc_prune_dead(void) {
	for (int i = 0; i < MAX_EP; i++) {
		if (g_ep[i].used && g_ep[i].owner && (g_ep[i].owner->state & PROCESS_STATE_DIED))
			AuIpcProcessGone(g_ep[i].owner);
	}
}

static int do_create(AuProcess* proc, IpcCreate* r) {
	if (!ipc_user_ptr(r, sizeof(*r)))
		return IPC_E_INVAL;
	if (!AuHwdevCallerAllowed(proc))
		return IPC_E_PERM;
	r->name[IPC_NAME_MAX - 1] = 0;
	if (!r->name[0] || r->acl > IPC_ACL_DAEMONS)
		return IPC_E_INVAL;
	ipc_prune_dead();
	uint64_t daif = read_daif();
	mask_irqs();
	int free_slot = -1, rc = IPC_OK;
	for (int i = 0; i < MAX_EP; i++) {
		if (g_ep[i].used && !strcmp(g_ep[i].name, r->name)) {
			rc = IPC_E_BUSY;
			break;
		}
		if (!g_ep[i].used && free_slot < 0)
			free_slot = i;
	}
	if (rc == IPC_OK && free_slot < 0)
		rc = IPC_E_BUSY;
	if (rc == IPC_OK) {
		Endpoint* e = &g_ep[free_slot];
		uint32_t gen = e->gen ? e->gen : g_gen_counter++;
		memset(e, 0, sizeof(*e));
		e->gen = gen;
		e->used = true;
		strcpy(e->name, r->name);
		e->owner = proc;
		e->owner_pid = proc->proc_id;
		e->acl = r->acl;
		r->handle = make_id(e->gen, free_slot);
		UARTDebugOut("[ipc]: endpoint '%s' created by pid %d (acl %d)\r\n", e->name, proc->proc_id, (int)e->acl);
	}
	restore_daif(daif);
	return rc;
}

static bool acl_admits(Endpoint* e, AuProcess* proc) {
	if (e->acl == IPC_ACL_DAEMONS)
		return AuHwdevCallerAllowed(proc);
	return true;
}

static int do_connect(AuProcess* proc, IpcConnect* r) {
	if (!ipc_user_ptr(r, sizeof(*r)))
		return IPC_E_INVAL;
	r->name[IPC_NAME_MAX - 1] = 0;
	ipc_prune_dead();
	int rc = IPC_E_NOENT;
	uint64_t daif = read_daif();
	mask_irqs();
	for (int i = 0; i < MAX_EP; i++) {
		if (g_ep[i].used && !strcmp(g_ep[i].name, r->name)) {
			if (!acl_admits(&g_ep[i], proc)) {
				rc = IPC_E_PERM;
			} else {
				r->handle = make_id(g_ep[i].gen, i);
				rc = IPC_OK;
			}
			break;
		}
	}
	restore_daif(daif);
	return rc;
}

static int do_destroy(AuProcess* proc, IpcHandle* r) {
	if (!ipc_user_ptr(r, sizeof(*r)))
		return IPC_E_INVAL;
	int32_t h = r->handle;
	uint64_t daif = read_daif();
	mask_irqs();
	Endpoint* e = ep_from_handle(h);
	int rc = IPC_OK;
	if (!e || e->owner != proc) {
		rc = e ? IPC_E_PERM : IPC_E_NOENT;
		restore_daif(daif);
		return rc;
	}
	restore_daif(daif);
	AuIpcProcessGone(proc);        /* tears down every endpoint this process owns */
	return IPC_OK;
}

static int do_call(AuProcess* proc, IpcCall* r) {
	if (!ipc_user_ptr(r, sizeof(*r)))
		return IPC_E_INVAL;
	r->status = IPC_E_INVAL;
	r->call_id = -1;
	if (r->req_len > IPC_MAX_MSG || (r->req_len && !ipc_user_ptr(r->req, r->req_len)))
		return IPC_E_INVAL;
	uint8_t* copy = (uint8_t*)kmalloc(r->req_len ? r->req_len : 1);
	if (!copy)
		return IPC_E_BUSY;
	if (r->req_len)
		memcpy(copy, r->req, r->req_len);
	AA64Thread* thr = AuGetCurrentThread();
	uint32_t ms = clamp_ms(r->timeout_ms);
	ipc_prune_dead();

	uint64_t daif = read_daif();
	mask_irqs();
	Endpoint* e = ep_from_handle(r->handle);
	if (!e || e->owner == proc) {
		r->status = e ? IPC_E_INVAL : IPC_E_NOENT;     /* a server cannot call itself */
		goto fail;
	}
	if (!acl_admits(e, proc)) {
		r->status = IPC_E_PERM;
		goto fail;
	}
	if (e->qcount >= EP_QUEUE) {
		r->status = IPC_E_BUSY;
		goto fail;
	}
	int slot = -1;
	for (int i = 0; i < MAX_CALLS; i++)
		if (g_call[i].state == CS_FREE) { slot = i; break; }
	if (slot < 0) {
		r->status = IPC_E_BUSY;
		goto fail;
	}
	Call* c = &g_call[slot];
	uint32_t gen = c->gen + 1;
	memset(c, 0, sizeof(*c));
	c->gen = gen;
	c->state = CS_QUEUED;
	c->ep = (int)(e - g_ep);
	c->ep_gen = (int)e->gen;
	c->client_pid = proc->proc_id;
	c->client_uid = proc->creds.uid;
	c->client_gid = proc->creds.gid;
	c->client_tid = thr ? thr->thread_id : 0;
	c->req = copy;
	c->req_len = r->req_len;
	e->queue[e->qcount++] = slot;
	r->call_id = make_id(c->gen, slot);
	r->status = IPC_OK;                 /* "the reply is in" is decided by FETCH */
	uint64_t waiter = e->waiter_tid;
	e->waiter_tid = 0;
	if (waiter)
		wake_tid(waiter);
	if (thr)
		ipc_sleep(ms);                  /* woken by REPLY / server death, or the timeout */
	return IPC_OK;
fail:
	restore_daif(daif);
	kfree(copy);
	return IPC_OK;                      /* result is in r->status */
}

static int do_fetch(AuProcess* proc, IpcFetch* r) {
	if (!ipc_user_ptr(r, sizeof(*r)))
		return IPC_E_INVAL;
	r->resp_len = 0;
	r->server_status = 0;
	if (r->resp_cap && !ipc_user_ptr(r->resp, r->resp_cap))
		return IPC_E_INVAL;
	uint64_t daif = read_daif();
	mask_irqs();
	Call* c = call_from_id(r->call_id);
	if (!c || c->client_pid != proc->proc_id) {
		r->status = IPC_E_NOENT;
		restore_daif(daif);
		return IPC_OK;
	}
	if (c->state == CS_DONE) {
		uint32_t n = c->resp_len < r->resp_cap ? c->resp_len : r->resp_cap;
		if (n)
			memcpy(r->resp, c->resp, n);
		r->resp_len = c->resp_len;
		r->server_status = c->server_status;
		r->status = c->resp_len > r->resp_cap ? IPC_E_TOOBIG : IPC_OK;
		call_free(c);
	} else if (c->state == CS_FAILED) {
		r->status = c->fail;
		call_free(c);
	} else {
		/* no reply yet: give up on it; the server's answer will be dropped */
		c->state = CS_ABANDONED;
		r->status = IPC_E_TIMEOUT;
	}
	restore_daif(daif);
	return IPC_OK;
}

static int do_wait(AuProcess* proc, IpcWait* r) {
	if (!ipc_user_ptr(r, sizeof(*r)))
		return IPC_E_INVAL;
	uint32_t ms = clamp_ms(r->timeout_ms);
	AA64Thread* thr = AuGetCurrentThread();
	uint64_t daif = read_daif();
	mask_irqs();
	Endpoint* e = ep_from_handle(r->handle);
	if (!e || e->owner != proc) {
		r->status = e ? IPC_E_PERM : IPC_E_NOENT;
		restore_daif(daif);
		return IPC_OK;
	}
	r->status = IPC_OK;
	if (e->qcount > 0 || !thr) {
		restore_daif(daif);
		return IPC_OK;
	}
	e->waiter_tid = thr->thread_id;
	ipc_sleep(ms);
	return IPC_OK;
}

static int do_recv(AuProcess* proc, IpcRecv* r) {
	if (!ipc_user_ptr(r, sizeof(*r)))
		return IPC_E_INVAL;
	r->call_id = -1;
	r->len = 0;
	if (r->cap && !ipc_user_ptr(r->buf, r->cap))
		return IPC_E_INVAL;
	uint64_t daif = read_daif();
	mask_irqs();
	Endpoint* e = ep_from_handle(r->handle);
	if (!e || e->owner != proc) {
		r->status = e ? IPC_E_PERM : IPC_E_NOENT;
		restore_daif(daif);
		return IPC_OK;
	}
	r->status = IPC_E_EMPTY;
	while (e->qcount > 0) {
		int slot = e->queue[0];
		for (int i = 1; i < e->qcount; i++)
			e->queue[i - 1] = e->queue[i];
		e->qcount--;
		Call* c = &g_call[slot];
		if (c->state == CS_ABANDONED) {            /* the caller gave up while it queued */
			call_free(c);
			continue;
		}
		if (c->req_len > r->cap) {
			call_fail(c, IPC_E_TOOBIG);            /* tell the caller; keep the server sane */
			r->status = IPC_E_TOOBIG;
			break;
		}
		if (c->req_len)
			memcpy(r->buf, c->req, c->req_len);
		r->len = c->req_len;
		r->call_id = make_id(c->gen, slot);
		r->sender_pid = c->client_pid;
		r->sender_uid = c->client_uid;
		r->sender_gid = c->client_gid;
		c->state = CS_SERVING;
		r->status = IPC_OK;
		break;
	}
	restore_daif(daif);
	return IPC_OK;
}

static int do_reply(AuProcess* proc, IpcReply* r) {
	if (!ipc_user_ptr(r, sizeof(*r)))
		return IPC_E_INVAL;
	if (r->len > IPC_MAX_MSG || (r->len && !ipc_user_ptr(r->resp, r->len)))
		return IPC_E_INVAL;
	uint8_t* copy = (uint8_t*)kmalloc(r->len ? r->len : 1);
	if (!copy)
		return IPC_E_BUSY;
	if (r->len)
		memcpy(copy, r->resp, r->len);
	uint64_t daif = read_daif();
	mask_irqs();
	Endpoint* e = ep_from_handle(r->handle);
	Call* c = call_from_id(r->call_id);
	int rc = IPC_OK;
	if (!e || e->owner != proc) {
		rc = e ? IPC_E_PERM : IPC_E_NOENT;
	} else if (!c || c->ep != (int)(e - g_ep) || c->ep_gen != (int)e->gen ||
			   (c->state != CS_SERVING && c->state != CS_ABANDONED)) {
		rc = IPC_E_NOENT;
	} else if (c->state == CS_ABANDONED) {
		call_free(c);                              /* nobody is waiting any more */
	} else {
		c->resp = copy;
		c->resp_len = r->len;
		c->server_status = r->server_status;
		c->state = CS_DONE;
		wake_tid(c->client_tid);
		copy = NULL;
	}
	restore_daif(daif);
	if (copy)
		kfree(copy);
	return rc;
}

static int ipc_ioctl(AuVFSNode* file, int code, void* arg) {
	(void)file;
	AuProcess* proc = AuHwdevCurrentProcess();
	if (!proc)
		return IPC_E_PERM;
	switch (code) {
	case IPC_IOC_CREATE:  return do_create(proc, (IpcCreate*)arg);
	case IPC_IOC_DESTROY: return do_destroy(proc, (IpcHandle*)arg);
	case IPC_IOC_CONNECT: return do_connect(proc, (IpcConnect*)arg);
	case IPC_IOC_CALL:    return do_call(proc, (IpcCall*)arg);
	case IPC_IOC_FETCH:   return do_fetch(proc, (IpcFetch*)arg);
	case IPC_IOC_WAIT:    return do_wait(proc, (IpcWait*)arg);
	case IPC_IOC_RECV:    return do_recv(proc, (IpcRecv*)arg);
	case IPC_IOC_REPLY:   return do_reply(proc, (IpcReply*)arg);
	}
	return IPC_E_INVAL;
}

void AuIpcInitialize(void) {
	AuVFSNode* fsys = AuVFSFind("/dev");
	if (!fsys)
		return;
	AuVFSNode* node = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(node, 0, sizeof(AuVFSNode));
	strcpy(node->filename, "ipc");
	node->flags = FS_FLAG_DEVICE;
	node->uid = 0;
	node->gid = AuCredGetGroupID(AURORA_GID_MISC_WORLD);   /* every application; create is daemons-only */
	node->device = fsys;
	node->iocontrol = ipc_ioctl;
	AuDevFSAddFile(fsys, "/", node);
	UARTDebugOut("[ipc]: /dev/ipc registered\r\n");
}

/* ---- kernel-side client API: lets the kernel itself be a caller (see Fs/srvfs.c) ----
 * A kernel client cannot block inside a nested call (a sleeping syscall resumes
 * after the svc, it does not return up the C stack), so these are split into a
 * non-blocking begin / poll / abandon; the caller decides how to wait. */
int AuIpcKCallBegin(const char* name, int pid, int uid, int gid, const void* req, uint32_t len, int32_t* call_id) {
	if (len > IPC_MAX_MSG)
		return IPC_E_INVAL;
	uint8_t* copy = (uint8_t*)kmalloc(len ? len : 1);
	if (!copy)
		return IPC_E_BUSY;
	if (len)
		memcpy(copy, req, len);
	AA64Thread* thr = AuGetCurrentThread();
	ipc_prune_dead();
	uint64_t daif = read_daif();
	mask_irqs();
	int rc = IPC_E_NOENT;
	for (int i = 0; i < MAX_EP; i++) {
		Endpoint* e = &g_ep[i];
		if (!e->used || strcmp(e->name, name))
			continue;
		rc = IPC_E_BUSY;
		if (e->qcount >= EP_QUEUE)
			break;
		int slot = -1;
		for (int k = 0; k < MAX_CALLS; k++)
			if (g_call[k].state == CS_FREE) { slot = k; break; }
		if (slot < 0)
			break;
		Call* c = &g_call[slot];
		uint32_t gen = c->gen + 1;
		memset(c, 0, sizeof(*c));
		c->gen = gen;
		c->state = CS_QUEUED;
		c->ep = i;
		c->ep_gen = (int)e->gen;
		c->client_pid = pid;
		c->client_uid = uid;
		c->client_gid = gid;
		c->client_tid = thr ? thr->thread_id : 0;
		c->req = copy;
		c->req_len = len;
		copy = NULL;
		e->queue[e->qcount++] = slot;
		*call_id = make_id(c->gen, slot);
		uint64_t waiter = e->waiter_tid;
		e->waiter_tid = 0;
		if (waiter)
			wake_tid(waiter);
		rc = IPC_OK;
		break;
	}
	restore_daif(daif);
	if (copy)
		kfree(copy);
	return rc;
}

/* IPC_OK: answered (out/outlen/server_status filled, call freed); IPC_E_TIMEOUT: not yet;
 * anything else: the call failed and is freed */
int AuIpcKCallPoll(int32_t call_id, void* out, uint32_t cap, uint32_t* outlen, int32_t* server_status) {
	uint64_t daif = read_daif();
	mask_irqs();
	Call* c = call_from_id(call_id);
	int rc;
	if (!c) {
		rc = IPC_E_NOENT;
	} else if (c->state == CS_DONE) {
		uint32_t n = c->resp_len < cap ? c->resp_len : cap;
		if (n)
			memcpy(out, c->resp, n);
		*outlen = c->resp_len;
		*server_status = c->server_status;
		rc = c->resp_len > cap ? IPC_E_TOOBIG : IPC_OK;
		call_free(c);
	} else if (c->state == CS_FAILED) {
		rc = c->fail;
		call_free(c);
	} else {
		rc = IPC_E_TIMEOUT;
	}
	restore_daif(daif);
	return rc;
}

void AuIpcKCallAbandon(int32_t call_id) {
	uint64_t daif = read_daif();
	mask_irqs();
	Call* c = call_from_id(call_id);
	if (c) {
		if (c->state == CS_QUEUED || c->state == CS_SERVING)
			c->state = CS_ABANDONED;
		else
			call_free(c);
	}
	restore_daif(daif);
}
