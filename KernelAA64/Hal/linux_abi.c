/**
 * Linux syscall numbers from x8, for PROCESS_TYPE_LINUX guests.
 * Each call lands on a kernel path that already exists. clone/futex are
 * the pthread piece: they share the process address space and block on
 * the scheduler lists that PE threads already use.
 */

#include <stdint.h>
#include <string.h>
#include <_null.h>
#include <process.h>
#include <loader.h>
#include <clean.h>
#include <Serv/sysserv.h>
#include <Hal/AA64/sched.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Hal/AA64/aa64cpu.h>
#include <Mm/mmap.h>
#include <Mm/kmalloc.h>
#include <Mm/pmmngr.h>
#include <Mm/vmmngr.h>
#include <Fs/vfs.h>
#include <Fs/pipe.h>
#include <Net/socket.h>
#include <Cap/capability.h>
#include <Drivers/uart.h>
#include <timer.h>
#include <Sync/futex.h>

#define LNX_ENOSYS 38
#define LNX_EAGAIN 11
#define LNX_ENOMEM 12
#define LNX_EFAULT 14
#define LNX_EINVAL 22
#define LNX_ENOTTY 25
#define LNX_EBADF 9
#define LNX_ENOENT 2
#define LNX_EIO 5
#define LNX_ETIMEDOUT 110
#define LNX_EMFILE 24
#define LNX_EEXIST 17
#define LNX_ECHILD 10

#define LNX_O_DIRECTORY 040000
#define LNX_AT_EMPTY_PATH 0x1000
#define LNX_F_DUPFD 0
#define LNX_F_DUPFD_CLOEXEC 1030
#define LNX_STATX_BASIC (1u | 2u | 0x200u)

#define LNX_CLONE_VM 0x00000100
#define LNX_CLONE_SETTLS 0x00080000
#define LNX_CLONE_PARENT_SETTID 0x00100000
#define LNX_CLONE_CHILD_CLEARTID 0x00200000
#define LNX_CLONE_CHILD_SETTID 0x01000000

#define LNX_PROT_READ 1
#define LNX_MAP_FIXED 0x10
#define LNX_MAP_ANON 0x20

#define LNX_PR_SET_NAME 15
#define LNX_PR_GET_NAME 16

extern int hwrng_read_bytes(void* buf, unsigned int max);

static uint64_t enosys_seen[8];
static int pipe_seq;

static AuProcess* linux_current(void) {
	AA64Thread* thr = AuGetCurrentThread();
	if (!thr)
		return NULL;
	AuProcess* proc = (AuProcess*)thr->procSlot;
	if (!proc)
		proc = AuProcessFindThread(thr);
	if (!proc)
		proc = AuProcessFindSubThread(thr);
	return proc;
}

static int64_t linux_enosys(uint64_t nr) {
	uint64_t bit = (uint64_t)1 << (nr & 63);
	uint64_t word = nr >> 6;
	if (word < 8 && (enosys_seen[word] & bit) == 0) {
		enosys_seen[word] |= bit;
		UARTDebugOut("[linux]: unimplemented syscall %d\r\n", (int)nr);
	}
	return -LNX_ENOSYS;
}

static int copy_user_str(char* dst, size_t cap, const char* src) {
	if (!src || !dst || cap == 0)
		return -LNX_EFAULT;
	for (size_t i = 0; i < cap - 1; i++) {
		char c = src[i];
		dst[i] = c;
		if (!c)
			return 0;
	}
	dst[cap - 1] = 0;
	return -LNX_EINVAL;
}

static int linux_open_mode(int flags) {
	/* O_DIRECTORY is not a kernel open mode. Drop it so it cannot fail the open. */
	flags &= ~LNX_O_DIRECTORY;
	int mode = FILE_OPEN_READ_ONLY;
	int acc = flags & 3;
	if (acc == 1 || acc == 2)
		mode |= FILE_OPEN_WRITE;
	if (flags & 0100)
		mode |= FILE_OPEN_CREAT | FILE_OPEN_WRITE;
	return mode;
}

static int linux_domain(int domain) {
	if (domain == 2)
		return AF_INET;
	if (domain == 10)
		return AF_INET6;
	return -1;
}

static void linux_fix_sockaddr(sockaddr* dst, const sockaddr* src, socklen_t len) {
	memset(dst, 0, sizeof(sockaddr));
	if (!src || len < 2)
		return;
	memcpy(dst, src, len > sizeof(sockaddr) ? sizeof(sockaddr) : len);
	if (dst->sa_family == 2)
		dst->sa_family = AF_INET;
	else if (dst->sa_family == 10)
		dst->sa_family = AF_INET6;
}

typedef struct _lnx_eventfd_ {
	uint64_t counter;
} lnx_eventfd;

static int linux_is_eventfd(AuVFSNode* node) {
	return node && strcmp(node->filename, "eventfd") == 0;
}

static int64_t linux_eventfd_write(AuVFSNode* node, const void* buf, size_t len) {
	lnx_eventfd* ev = node ? (lnx_eventfd*)node->device : NULL;
	if (!ev || !buf || len != 8)
		return -LNX_EINVAL;
	uint64_t add = 0;
	memcpy(&add, buf, 8);
	ev->counter += add;
	return 8;
}

static int64_t linux_eventfd_read(AuVFSNode* node, void* buf, size_t len) {
	lnx_eventfd* ev = node ? (lnx_eventfd*)node->device : NULL;
	if (!ev || !buf || len != 8)
		return -LNX_EINVAL;
	if (ev->counter == 0)
		return 0;
	memcpy(buf, &ev->counter, 8);
	ev->counter = 0;
	return 8;
}

static int64_t linux_write_fd(int fd, const void* buf, size_t len) {
	AuProcess* proc = linux_current();
	if (!proc || fd < 0 || fd >= FILE_DESC_PER_PROCESS)
		return -LNX_EBADF;
	if (!buf && len)
		return -LNX_EFAULT;
	if (fd == 1 || fd == 2) {
		char tmp[128];
		size_t off = 0;
		const char* src = (const char*)buf;
		while (off < len) {
			size_t n = len - off;
			if (n > sizeof(tmp) - 1)
				n = sizeof(tmp) - 1;
			memcpy(tmp, src + off, n);
			tmp[n] = 0;
			UARTDebugOut("%s", tmp);
			off += n;
		}
		if (!proc->fds[fd])
			return (int64_t)len;
	}
	if (linux_is_eventfd(proc->fds[fd]))
		return linux_eventfd_write(proc->fds[fd], buf, len);
	size_t n = WriteFile(fd, (void*)buf, len);
	if (n == 0 && len)
		return -LNX_EIO;
	return (int64_t)n;
}

static int64_t linux_read_fd(int fd, void* buf, size_t len) {
	if (fd < 0 || fd >= FILE_DESC_PER_PROCESS)
		return -LNX_EBADF;
	AuProcess* proc = linux_current();
	AuVFSNode* node = proc ? proc->fds[fd] : NULL;
	if (linux_is_eventfd(node))
		return linux_eventfd_read(node, buf, len);
	if (!buf)
		return -LNX_EBADF;
	size_t n = ReadFile(fd, buf, len);
	return (int64_t)n;
}

static int64_t linux_iov(int fd, void* iov, int count, int write) {
	if (!iov || count < 0)
		return -LNX_EINVAL;
	typedef struct {
		void* base;
		uint64_t len;
	} lnx_iov;
	lnx_iov* v = (lnx_iov*)iov;
	int64_t total = 0;
	for (int i = 0; i < count; i++) {
		int64_t n = write ? linux_write_fd(fd, v[i].base, (size_t)v[i].len)
						  : linux_read_fd(fd, v[i].base, (size_t)v[i].len);
		if (n < 0)
			return total ? total : n;
		total += n;
		if ((uint64_t)n < v[i].len)
			break;
	}
	return total;
}

static void linux_fill_stat(void* buf, AuVFSNode* node) {
	uint8_t raw[128];
	memset(raw, 0, sizeof(raw));
	uint32_t mode = node && (node->flags & FS_FLAG_DIRECTORY) ? 0040755 : 0100644;
	if (node && (node->flags & (FS_FLAG_TTY | FS_FLAG_DEVICE)))
		mode = 0020666;
	memcpy(raw + 16, &mode, 4);
	uint32_t nlink = 1;
	memcpy(raw + 20, &nlink, 4);
	int64_t size = node ? (int64_t)node->size : 0;
	memcpy(raw + 48, &size, 8);
	int32_t blksize = 4096;
	memcpy(raw + 56, &blksize, 4);
	memcpy(buf, raw, sizeof(raw));
}

static int64_t linux_futex(uint32_t* uaddr, int op, uint32_t val, void* timeout,
						   uint32_t* uaddr2, uint32_t val3) {
	AuProcess* proc = linux_current();
	AA64Thread* thr = AuGetCurrentThread();
	if (!proc || !thr || !uaddr || ((uint64_t)uaddr & 3))
		return -LNX_EINVAL;
	int cmd = op & AU_FUTEX_CMD_MASK;

	if (cmd == AU_FUTEX_WAKE || cmd == AU_FUTEX_WAKE_BITSET) {
		uint32_t bits = cmd == AU_FUTEX_WAKE ? 0xffffffffu : val3;
		if (cmd == AU_FUTEX_WAKE_BITSET && bits == 0)
			return -LNX_EINVAL;
		return AuFutexWake(proc, uaddr, (int)val, bits);
	}

	if (cmd == AU_FUTEX_REQUEUE || cmd == AU_FUTEX_CMP_REQUEUE) {
		int nrequeue = (int)(int64_t)timeout;
		return AuFutexRequeue(proc, uaddr, (int)val, nrequeue, uaddr2,
							  val3, cmd == AU_FUTEX_CMP_REQUEUE);
	}

	if (cmd != AU_FUTEX_WAIT && cmd != AU_FUTEX_WAIT_BITSET)
		return -LNX_ENOSYS;

	uint32_t bitset = cmd == AU_FUTEX_WAIT ? 0xffffffffu : val3;
	if (cmd == AU_FUTEX_WAIT_BITSET && bitset == 0)
		return -LNX_EINVAL;

	uint64_t ms = 0;
	int timed = 0;
	if (timeout) {
		int64_t sec = ((int64_t*)timeout)[0];
		int64_t nsec = ((int64_t*)timeout)[1];
		if (sec < 0 || nsec < 0 || nsec >= 1000000000LL)
			return -LNX_EINVAL;
		if (sec == 0 && nsec == 0)
			return -LNX_ETIMEDOUT;
		ms = (uint64_t)sec * 1000ULL;
		ms += ((uint64_t)nsec + 999999ULL) / 1000000ULL;
		if (ms == 0)
			ms = 1;
		timed = 1;
	}

	int r = AuFutexWait(proc, thr, uaddr, val, bitset, timed, ms);
	if (r == -AU_FUTEX_ETIMEDOUT)
		return -LNX_ETIMEDOUT;
	return r;
}

static void linux_write_tid(uint64_t ptr, uint32_t tid) {
	if (!ptr)
		return;
	*(uint32_t*)ptr = tid;
}

static int64_t linux_clone(uint64_t flags, uint64_t newsp, uint64_t parent_tid, uint64_t tls,
						   uint64_t child_tid) {
	AuProcess* proc = linux_current();
	AA64Thread* parent = AuGetCurrentThread();
	AA64Registers* regs = AA64GetCurrentRegCtx();
	if (!proc || !parent || !regs)
		return -LNX_EFAULT;
	if ((flags & LNX_CLONE_VM) == 0 || newsp == 0)
		return -LNX_ENOSYS;
	if (proc->num_thread >= MAX_THREADS_PER_PROCESS)
		return -LNX_EAGAIN;

	uint64_t top = AuCreateKernelStack(proc->cr3);
	top &= ~(uint64_t)15;
	AA64Registers* frame = (AA64Registers*)(top - sizeof(AA64Registers));
	memcpy(frame, regs, sizeof(AA64Registers));
	frame->x0 = 0;
	frame->EL0SP = (int64_t)newsp;

	/* AuCreateSubKthread publishes the child on the ready list. Keep it
	 * locked until its resume state, process membership and TIDs are
	 * initialized so another core cannot run a partially published child. */
	uint64_t sched_daif = AuSchedLock();
	AA64Thread* child = AuCreateSubKthread(AuProcessEntSubThread, (uint64_t)frame, proc->cr3, "lthr");
	child->threadType = THREAD_LEVEL_USER | THREAD_LEVEL_SUBTHREAD;
	child->procSlot = proc;
	child->justStored = true;
	child->elr_el1 = read_elr_el1();
	child->spsr_el1 = read_spsr_el1();
	child->sp = (uint64_t)frame;
	child->linux_tls = (flags & LNX_CLONE_SETTLS) ? tls : aa64_read_tpidr_el0();
	if (flags & LNX_CLONE_CHILD_CLEARTID)
		child->clear_child_tid = child_tid;
	proc->threads[proc->num_thread++] = child;

	uint32_t tid = (uint32_t)child->thread_id;
	if (flags & LNX_CLONE_PARENT_SETTID)
		linux_write_tid(parent_tid, tid);
	if (flags & LNX_CLONE_CHILD_SETTID)
		linux_write_tid(child_tid, tid);
	AuSchedUnlock(sched_daif);
	return (int64_t)tid;
}

static int64_t linux_exit(int group) {
	AA64Thread* thr = AuGetCurrentThread();
	AuProcess* proc = linux_current();
	AA64Registers* regs = AA64GetCurrentRegCtx();
	if (!thr || !proc)
		return -LNX_EFAULT;
	if (thr->clear_child_tid) {
		uint32_t* word = (uint32_t*)thr->clear_child_tid;
		*word = 0;
		AuFutexWake(proc, word, 1, 0xffffffffu);
		thr->clear_child_tid = 0;
	}
	if (regs)
		regs->x0 = 0;
	if (group || thr == proc->main_thread) {
		ProcessExit();
		return 0;
	}
	AuExitSubThread(proc, thr, (int)thr->thread_id);
	if (regs)
		AuScheduleThread(regs);
	return 0;
}

static int64_t linux_mmap(uint64_t addr, size_t len, int prot, int flags, int fd, uint64_t off) {
	(void)prot;
	if (!len)
		return -LNX_EINVAL;
	/* MAP_FIXED at the brk (0x3000000000) is above PROCESS_MMAP_ADDRESS.
	 * CreateMemMapping would advance mmap_next into the heap, and the next
	 * anonymous map (thread stack) would land on musl's malloc metadata. */
	if ((flags & LNX_MAP_FIXED) && (flags & LNX_MAP_ANON) && addr) {
		uint64_t start = addr & ~(uint64_t)0xFFF;
		size_t span = (size_t)PAGE_ALIGN((addr - start) + len);
		for (size_t done = 0; done < span; done += PAGE_SIZE) {
			uint64_t phys = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
			if (!phys)
				return -LNX_ENOMEM;
			memset((void*)P2V(phys), 0, PAGE_SIZE);
			if (!AuMapPage(phys, start + done, PTE_NORMAL_MEM | PTE_AP_RW_USER))
				AuPmmngrReleasePage(phys);
		}
		return (int64_t)addr;
	}
	int mapfd = (flags & LNX_MAP_ANON) ? -1 : fd;
	void* p = CreateMemMapping(NULL, len, 0, 0, mapfd, off);
	if (!p)
		return -LNX_ENOMEM;
	return (int64_t)p;
}

static int64_t linux_clock(void* ts) {
	if (!ts)
		return -LNX_EFAULT;
	int64_t sec = 0;
	int64_t nsec = 0;
	AuGetWalltime(&sec, &nsec);
	((int64_t*)ts)[0] = sec;
	((int64_t*)ts)[1] = nsec;
	return 0;
}

static int64_t linux_sleep_ms(uint64_t ms) {
	AA64Registers* regs = AA64GetCurrentRegCtx();
	if (regs)
		regs->x0 = 0;
	if (ms == 0)
		return 0;
	ProcessSleep(ms);
	return 0;
}

typedef struct _lnx_epoll_item_ {
	int fd;
	uint32_t events;
	uint64_t data;
} lnx_epoll_item;

typedef struct _lnx_epoll_ {
	int count;
	lnx_epoll_item items[32];
} lnx_epoll;

static int fd_ready(AuProcess* proc, int fd, uint32_t events) {
	if (!proc || fd < 0 || fd >= FILE_DESC_PER_PROCESS || !proc->fds[fd])
		return 0;
	AuVFSNode* node = proc->fds[fd];
	int in = 1;
	int out = 1;
	if (node->flags & FS_FLAG_PIPE) {
		AuPipe* pipe = (AuPipe*)node->device;
		in = pipe && AuPipeUnread(pipe) > 0;
	} else if (node->flags & FS_FLAG_SOCKET) {
		in = 0;
	}
	int re = 0;
	if ((events & 1) && in)
		re |= 1;
	if ((events & 4) && out)
		re |= 4;
	return re;
}

static int64_t linux_epoll_create(void) {
	AuProcess* proc = linux_current();
	if (!proc)
		return -LNX_EFAULT;
	int fd = AuProcessGetFileDesc(proc);
	if (fd < 0)
		return -LNX_EMFILE;
	AuVFSNode* node = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	lnx_epoll* ep = (lnx_epoll*)kmalloc(sizeof(lnx_epoll));
	memset(node, 0, sizeof(AuVFSNode));
	memset(ep, 0, sizeof(lnx_epoll));
	strcpy(node->filename, "epoll");
	node->device = ep;
	proc->fds[fd] = node;
	return fd;
}

static lnx_epoll* epoll_of(AuProcess* proc, int epfd) {
	if (!proc || epfd < 0 || epfd >= FILE_DESC_PER_PROCESS || !proc->fds[epfd])
		return NULL;
	AuVFSNode* node = proc->fds[epfd];
	if (strcmp(node->filename, "epoll") != 0)
		return NULL;
	return (lnx_epoll*)node->device;
}

static int64_t linux_epoll_ctl(int epfd, int op, int fd, void* event) {
	AuProcess* proc = linux_current();
	lnx_epoll* ep = epoll_of(proc, epfd);
	if (!ep || !event)
		return -LNX_EINVAL;
	uint32_t events = *(uint32_t*)event;
	uint64_t data = *(uint64_t*)((uint8_t*)event + 4);
	if (op == 1) {
		if (ep->count >= 32)
			return -LNX_ENOMEM;
		ep->items[ep->count].fd = fd;
		ep->items[ep->count].events = events;
		ep->items[ep->count].data = data;
		ep->count++;
		return 0;
	}
	for (int i = 0; i < ep->count; i++) {
		if (ep->items[i].fd != fd)
			continue;
		if (op == 2) {
			ep->items[i] = ep->items[ep->count - 1];
			ep->count--;
			return 0;
		}
		if (op == 3) {
			ep->items[i].events = events;
			ep->items[i].data = data;
			return 0;
		}
	}
	return -LNX_ENOENT;
}

static int64_t linux_epoll_wait(int epfd, void* events, int maxevents, int timeout) {
	AuProcess* proc = linux_current();
	lnx_epoll* ep = epoll_of(proc, epfd);
	if (!ep || !events || maxevents <= 0)
		return -LNX_EINVAL;
	for (int pass = 0; pass < 2; pass++) {
		int n = 0;
		for (int i = 0; i < ep->count && n < maxevents; i++) {
			int re = fd_ready(proc, ep->items[i].fd, ep->items[i].events);
			if (!re)
				continue;
			uint8_t* dst = (uint8_t*)events + (size_t)n * 12;
			memcpy(dst, &re, 4);
			memcpy(dst + 4, &ep->items[i].data, 8);
			n++;
		}
		if (n || timeout == 0)
			return n;
		if (pass == 0 && timeout > 0)
			linux_sleep_ms((uint64_t)timeout);
		else
			break;
	}
	return 0;
}

static int64_t linux_ppoll(void* fds, int nfds, void* ts) {
	AuProcess* proc = linux_current();
	if (!proc || !fds || nfds < 0)
		return -LNX_EINVAL;
	typedef struct {
		int fd;
		short events;
		short revents;
	} lnx_pollfd;
	lnx_pollfd* p = (lnx_pollfd*)fds;
	uint64_t ms = 0;
	if (ts) {
		int64_t sec = ((int64_t*)ts)[0];
		int64_t nsec = ((int64_t*)ts)[1];
		if (sec > 0 || nsec > 0)
			ms = (uint64_t)sec * 1000 + (uint64_t)nsec / 1000000;
	}
	for (int pass = 0; pass < 2; pass++) {
		int ready = 0;
		for (int i = 0; i < nfds; i++) {
			int re = fd_ready(proc, p[i].fd, (uint32_t)p[i].events);
			p[i].revents = (short)re;
			if (re)
				ready++;
		}
		if (ready || ms == 0)
			return ready;
		if (pass == 0)
			linux_sleep_ms(ms ? ms : 1);
	}
	return 0;
}

static int64_t linux_socket_call(int domain, int type, int protocol) {
	int kern = linux_domain(domain);
	int kind = type & 0xff;
	int fd = AuCreateSocket(kern, kind, protocol);
	if (fd < 0)
		return -LNX_EINVAL;
	return fd;
}

static int64_t linux_dup_fd(AuProcess* proc, int oldfd) {
	if (!proc)
		return -LNX_EFAULT;
	int neu = AuProcessGetFileDesc(proc);
	if (neu < 0)
		return -LNX_EMFILE;
	if (BordoisilaCapDup(proc, oldfd, neu) != CAP_OK)
		return -LNX_EBADF;
	return neu;
}

static int64_t linux_fcntl(AuProcess* proc, int fd, int cmd) {
	if (cmd == LNX_F_DUPFD || cmd == LNX_F_DUPFD_CLOEXEC)
		return linux_dup_fd(proc, fd);
	return 0;
}

static int64_t linux_brk(AuProcess* proc, uint64_t req) {
	if (!proc)
		return -LNX_EFAULT;
	if (!proc->proc_mem_heap)
		proc->proc_mem_heap = PROCESS_BREAK_ADDRESS;
	uint64_t cur = proc->proc_mem_heap;
	if (req == 0 || req <= cur)
		return (int64_t)cur;
	size_t diff = PAGE_ALIGN((size_t)(req - cur));
	uint64_t got = GetProcessHeapMem(diff);
	if (got == 0 || got == (uint64_t)-1) {
		proc->proc_mem_heap = cur;
		return (int64_t)cur;
	}
	proc->proc_mem_heap = req;
	return (int64_t)req;
}

/* x86 CreateDir returns 0, or -1 when the fs or create fails. AA64 has no CreateDir. */
static int64_t linux_mkdir(const char* userpath) {
	char path[128];
	int rc = copy_user_str(path, sizeof(path), userpath);
	if (rc < 0)
		return rc;
	if (AuVFSOpen(path))
		return -LNX_EEXIST;
	AuVFSNode* fsys = AuVFSFind(path);
	if (!fsys)
		return -LNX_ENOENT;
	AuVFSNode* dir = AuVFSCreateDir(fsys, path);
	if (!dir)
		return -LNX_ENOENT;
	kfree(dir);
	return 0;
}

static int64_t linux_readlink(const char* userpath, char* buf, size_t cap) {
	char path[128];
	int rc = copy_user_str(path, sizeof(path), userpath);
	if (rc < 0)
		return rc;
	if (strcmp(path, "/proc/self/exe") != 0)
		return -LNX_ENOENT;
	if (!buf || cap == 0)
		return -LNX_EINVAL;
	const char* target = "/servo.elf";
	size_t n = 10;
	size_t copy = n < cap ? n : cap;
	memcpy(buf, target, copy);
	if (cap > n)
		buf[n] = 0;
	return (int64_t)copy;
}

/* musl: statx(dirfd, path, flags, mask, statxbuf) so the buffer is x4, not x2. */
static int64_t linux_statx(uint64_t dirfd, const char* userpath, uint64_t flags, void* buf) {
	if (!buf)
		return -LNX_EFAULT;
	char path[128];
	int rc = copy_user_str(path, sizeof(path), userpath);
	if (rc < 0)
		return rc;
	AuVFSNode* node = NULL;
	if (path[0] == 0) {
		if ((flags & LNX_AT_EMPTY_PATH) == 0)
			return -LNX_ENOENT;
		AuProcess* proc = linux_current();
		int fd = (int)dirfd;
		if (!proc || fd < 0 || fd >= FILE_DESC_PER_PROCESS || !proc->fds[fd])
			return -LNX_EBADF;
		node = proc->fds[fd];
	} else {
		node = AuVFSOpen(path);
		if (!node)
			return -LNX_ENOENT;
	}
	uint8_t raw[256];
	memset(raw, 0, sizeof(raw));
	uint32_t mask = LNX_STATX_BASIC;
	uint32_t blk = 4096;
	uint16_t mode = (node && (node->flags & FS_FLAG_DIRECTORY)) ? 0040644 : 0100644;
	uint64_t sz = node ? (uint64_t)node->size : 0;
	memcpy(raw + 0, &mask, 4);
	memcpy(raw + 4, &blk, 4);
	memcpy(raw + 28, &mode, 2);
	memcpy(raw + 40, &sz, 8);
	memcpy(buf, raw, sizeof(raw));
	return 0;
}

static int64_t linux_eventfd(uint64_t init) {
	AuProcess* proc = linux_current();
	if (!proc)
		return -LNX_EFAULT;
	int fd = AuProcessGetFileDesc(proc);
	if (fd < 0)
		return -LNX_EMFILE;
	AuVFSNode* node = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	lnx_eventfd* ev = (lnx_eventfd*)kmalloc(sizeof(lnx_eventfd));
	if (!node || !ev) {
		if (node)
			kfree(node);
		if (ev)
			kfree(ev);
		return -LNX_ENOMEM;
	}
	memset(node, 0, sizeof(AuVFSNode));
	strcpy(node->filename, "eventfd");
	node->device = ev;
	ev->counter = init;
	proc->fds[fd] = node;
	return fd;
}

int64_t AuLinuxSyscall(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
					   uint64_t a5) {
	AuProcess* proc = linux_current();
	AA64Thread* thr = AuGetCurrentThread();
	switch ((int)nr) {
	case 17: {
		char* buf = (char*)a0;
		size_t cap = (size_t)a1;
		if (!buf || cap < 2)
			return -LNX_EINVAL;
		buf[0] = '/';
		buf[1] = 0;
		return 2;
	}
	case 19:
		return linux_eventfd(a0);
	case 20:
		return linux_epoll_create();
	case 21:
		return linux_epoll_ctl((int)a0, (int)a1, (int)a2, (void*)a3);
	case 22:
		return linux_epoll_wait((int)a0, (void*)a1, (int)a2, (int)a3);
	case 23:
	case 24:
		return linux_dup_fd(proc, (int)a0);
	case 25:
		return linux_fcntl(proc, (int)a0, (int)a1);
	case 29:
		return -LNX_ENOTTY;
	case 34:
		return linux_mkdir((const char*)a1);
	case 48:
	case 56: {
		char path[128];
		int rc = copy_user_str(path, sizeof(path), (const char*)(nr == 56 ? a1 : a1));
		if (rc < 0)
			return rc;
		if (nr == 48) {
			AuVFSNode* node = AuVFSOpen(path);
			return node ? 0 : -LNX_ENOENT;
		}
		int fd = OpenFile(path, linux_open_mode((int)a2));
		return fd < 0 ? -LNX_ENOENT : fd;
	}
	case 57:
		return CloseFile((int)a0) < 0 ? -LNX_EBADF : 0;
	case 59: {
		if (!proc)
			return -LNX_EFAULT;
		char pname[16];
		pname[0] = 'p';
		pname[1] = '0' + (pipe_seq % 10);
		pname[2] = 0;
		pipe_seq++;
		int created = AuCreatePipe(pname, 4096);
		if (created < 0)
			return -LNX_ENOMEM;
		int neu = AuProcessGetFileDesc(proc);
		if (neu < 0 || BordoisilaCapDup(proc, created, neu) != CAP_OK)
			return -LNX_EMFILE;
		int* out = (int*)a0;
		out[0] = created;
		out[1] = neu;
		return 0;
	}
	case 61:
		return 0;
	case 62: {
		int64_t off = (int64_t)a1;
		int whence = (int)a2;
		AuVFSNode* node = NULL;
		if (proc && (int)a0 >= 0 && (int)a0 < FILE_DESC_PER_PROCESS)
			node = proc->fds[(int)a0];
		if (!node)
			return -LNX_EBADF;
		if (whence == 1)
			off += (int64_t)node->pos;
		else if (whence == 2)
			off += (int64_t)node->size;
		else if (whence != 0)
			return -LNX_EINVAL;
		if (off < 0)
			return -LNX_EINVAL;
		if (FileSetOffset((int)a0, (size_t)off) < 0)
			return -LNX_EINVAL;
		return off;
	}
	case 63:
		return linux_read_fd((int)a0, (void*)a1, (size_t)a2);
	case 64:
		return linux_write_fd((int)a0, (const void*)a1, (size_t)a2);
	case 65:
		return linux_iov((int)a0, (void*)a1, (int)a2, 0);
	case 66:
		return linux_iov((int)a0, (void*)a1, (int)a2, 1);
	case 73:
		return linux_ppoll((void*)a0, (int)a1, (void*)a2);
	case 78:
		return linux_readlink((const char*)a1, (char*)a2, (size_t)a3);
	case 79:
	case 80: {
		AuVFSNode* node = NULL;
		if (nr == 80) {
			if (!proc || (int)a0 < 0 || (int)a0 >= FILE_DESC_PER_PROCESS || !proc->fds[(int)a0])
				return -LNX_EBADF;
			node = proc->fds[(int)a0];
			linux_fill_stat((void*)a1, node);
			return 0;
		}
		char path[128];
		if (copy_user_str(path, sizeof(path), (const char*)a1) < 0)
			return -LNX_EFAULT;
		node = AuVFSOpen(path);
		if (!node)
			return -LNX_ENOENT;
		linux_fill_stat((void*)a2, node);
		return 0;
	}
	case 93:
		return linux_exit(0);
	case 94:
		return linux_exit(1);
	case 96:
		if (!thr)
			return -LNX_EFAULT;
		thr->clear_child_tid = a0;
		return (int64_t)thr->thread_id;
	case 98:
		return linux_futex((uint32_t*)a0,
						   (int)a1,
						   (uint32_t)a2,
						   (void*)a3,
						   (uint32_t*)a4,
						   (uint32_t)a5);
	case 99:
	case 100:
	case 132:
	case 134:
	case 135:
	case 160:
		if (nr == 160) {
			char* u = (char*)a0;
			if (!u)
				return -LNX_EFAULT;
			memset(u, 0, 65 * 5);
			strcpy(u, "Linux");
			strcpy(u + 65, "xeneva");
			strcpy(u + 130, "0.0.0");
			strcpy(u + 195, "xeneva");
			strcpy(u + 260, "aarch64");
			return 0;
		}
		return 0;
	case 101:
	case 115: {
		/* nanosleep(req, rem) uses x0. clock_nanosleep(clk, flags, req, rem) uses x2. */
		uint64_t ts_arg = nr == 115 ? a2 : a0;
		if (!ts_arg)
			return 0;
		int64_t sec = ((int64_t*)ts_arg)[0];
		int64_t nsec = ((int64_t*)ts_arg)[1];
		uint64_t ms = (uint64_t)sec * 1000 + (uint64_t)nsec / 1000000;
		return linux_sleep_ms(ms);
	}
	case 114:
		if (a1)
			memset((void*)a1, 0, 16);
		return 0;
	case 113:
	case 169:
		if (nr == 169) {
			int64_t sec = 0;
			int64_t nsec = 0;
			if (!a0)
				return -LNX_EFAULT;
			AuGetWalltime(&sec, &nsec);
			((int64_t*)a0)[0] = sec;
			((int64_t*)a0)[1] = nsec / 1000;
			return 0;
		}
		return linux_clock((void*)a1);
	case 119:
	case 120:
	case 122:
	case 130:
	case 139:
		return 0;
	case 163:
		if (a1) {
			uint64_t inf = ~(uint64_t)0;
			memcpy((void*)a1, &inf, 8);
			memcpy((uint8_t*)a1 + 8, &inf, 8);
		}
		return 0;
	case 123: {
		/* sched_getaffinity(pid, cpusetsize, mask). pid is ignored. */
		if (!a2 || a1 < 8)
			return -LNX_EINVAL;
		memset((void*)a2, 0, (size_t)a1);
		*(uint64_t*)a2 = 1;
		return (int64_t)a1;
	}
	case 124: {
		AA64Registers* regs = AA64GetCurrentRegCtx();
		if (regs)
			regs->x0 = 0;
		AuScheduleThread(regs);
		return 0;
	}
	case 167:
		if (!thr)
			return -LNX_EFAULT;
		if ((int)a0 == LNX_PR_SET_NAME && a1) {
			memset(thr->name, 0, sizeof(thr->name));
			for (int i = 0; i < 7; i++) {
				char c = ((char*)a1)[i];
				thr->name[i] = c;
				if (!c)
					break;
			}
			return 0;
		}
		if ((int)a0 == LNX_PR_GET_NAME && a1) {
			memset((void*)a1, 0, 16);
			memcpy((void*)a1, thr->name, 8);
			return 0;
		}
		return 0;
	case 172:
	case 173:
		return proc ? proc->proc_id : 1;
	case 174:
	case 175:
	case 176:
	case 177:
		return 0;
	case 178:
		return thr ? (int64_t)thr->thread_id : -LNX_EFAULT;
	case 179:
		if (a0)
			memset((void*)a0, 0, 104);
		return 0;
	case 198:
		return linux_socket_call((int)a0, (int)a1, (int)a2);
	case 200:
	case 203: {
		sockaddr local;
		linux_fix_sockaddr(&local, (const sockaddr*)a1, (socklen_t)a2);
		int rc = nr == 200 ? NetBind((int)a0, &local, (socklen_t)a2)
						   : NetConnect((int)a0, &local, (socklen_t)a2);
		return rc < 0 ? rc : 0;
	}
	case 201:
		return NetListen((int)a0, (int)a1) < 0 ? -LNX_EINVAL : 0;
	case 202:
	case 242:
		return NetAccept((int)a0, (sockaddr*)a1, (socklen_t*)a2);
	case 206:
	case 207:
	case 211:
	case 212: {
		iovec one;
		msghdr msg;
		memset(&msg, 0, sizeof(msg));
		if (nr == 211 || nr == 212)
			return (nr == 211 ? NetSend : NetReceive)((int)a0, (msghdr*)a1, (int)a2);
		one.iov_base = (void*)a1;
		one.iov_len = (size_t)a2;
		msg.msg_iov = &one;
		msg.msg_iovlen = 1;
		if (nr == 206) {
			sockaddr local;
			linux_fix_sockaddr(&local, (const sockaddr*)a4, (socklen_t)a5);
			msg.msg_name = a4 ? &local : NULL;
			msg.msg_namelen = (socklen_t)a5;
			return NetSend((int)a0, &msg, (int)a3);
		}
		msg.msg_name = (void*)a4;
		msg.msg_namelen = (socklen_t)a5;
		return NetReceive((int)a0, &msg, (int)a3);
	}
	case 208: {
		/* musl and Rust pass Linux SOL_SOCKET (1). The kernel constant is 0. */
		int level = (int)a1;
		if (level == 1)
			level = 0;
		return AuSocketSetOpt((int)a0, level, (int)a2, (const void*)a3, (socklen_t)a4) < 0
				   ? -LNX_EINVAL
				   : 0;
	}
	case 210:
		return 0;
	case 214:
		return linux_brk(proc, a0);
	case 215:
		UnmapMemMapping((void*)a0, (size_t)a1);
		return 0;
	case 216:
		return linux_enosys(nr);
	case 220:
		return linux_clone(a0, a1, a2, a3, a4);
	case 222:
		return linux_mmap(a0, (size_t)a1, (int)a2, (int)a3, (int)a4, a5);
	case 226:
	case 233:
	case 283:
		return 0;
	case 260:
		return -LNX_ECHILD;
	case 261:
		if (a3) {
			uint64_t inf = ~(uint64_t)0;
			memcpy((void*)a3, &inf, 8);
			memcpy((uint8_t*)a3 + 8, &inf, 8);
		}
		return 0;
	case 278: {
		int n = hwrng_read_bytes((void*)a0, (unsigned int)a1);
		if (n > 0)
			return n;
		uint8_t* dst = (uint8_t*)a0;
		uint64_t tick = AuGetCurrentUS();
		for (size_t i = 0; i < (size_t)a1; i++) {
			tick = tick * 6364136223846793005ULL + 1;
			dst[i] = (uint8_t)(tick >> 33);
		}
		return (int64_t)a1;
	}
	case 291:
		return linux_statx(a0, (const char*)a1, a2, (void*)a4);
	case 293:
	case 435:
		return linux_enosys(nr);
	default:
		return linux_enosys(nr);
	}
}
