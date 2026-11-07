/* Static musl guest. Libc calls go through musl. The syscall list matches
 * the rust hello witness and uses svc with the Linux number in x8. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum {
	ENOSYS = -38,
	ENOTTY = -25,
	EEXIST = -17,
	EAGAIN = -11,
	ECHILD = -10,
	ENETUNREACH = -114
};

enum {
	CLONE_VM = 0x100,
	CLONE_FS = 0x200,
	CLONE_FILES = 0x400,
	CLONE_SIGHAND = 0x800,
	CLONE_THREAD = 0x10000,
	CLONE_SYSVSEM = 0x40000,
	CLONE_SETTLS = 0x80000,
	CLONE_PARENT_SETTID = 0x100000,
	CLONE_CHILD_CLEARTID = 0x200000,
	CLONE_CHILD_SETTID = 0x1000000
};

static unsigned pass_n;
static unsigned total_n;
static volatile uint32_t ran;
static uint32_t ptid;
static volatile uint32_t ctid;
static uint64_t tls[2];
static unsigned char child_stack[16384] __attribute__((aligned(16)));

struct iovec_k {
	uint64_t base;
	uint64_t len;
};

struct msghdr_k {
	uint64_t name;
	uint32_t namelen;
	uint32_t pad;
	uint64_t iov;
	uint64_t iovlen;
	uint64_t control;
	uint64_t controllen;
	int32_t flags;
};

struct sockaddr_in_k {
	uint16_t family;
	uint16_t port;
	uint32_t addr;
	unsigned char zero[8];
};

struct pollfd_k {
	int32_t fd;
	int16_t events;
	int16_t revents;
};

static long syscall6(long nr, long a0, long a1, long a2, long a3, long a4, long a5) {
	register long x8 __asm__("x8") = nr;
	register long x0 __asm__("x0") = a0;
	register long x1 __asm__("x1") = a1;
	register long x2 __asm__("x2") = a2;
	register long x3 __asm__("x3") = a3;
	register long x4 __asm__("x4") = a4;
	register long x5 __asm__("x5") = a5;
	__asm__ volatile("svc #0"
					 : "+r"(x0)
					 : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x8)
					 : "memory");
	return x0;
}

static void write_all(const char *s, size_t n) {
	while (n) {
		ssize_t w = write(1, s, n);
		if (w <= 0)
			return;
		s += w;
		n -= (size_t)w;
	}
}

static void write_str(const char *s) {
	write_all(s, strlen(s));
}

static void put_u64(uint64_t v) {
	char buf[20];
	int i = 20;
	if (v == 0) {
		write_str("0");
		return;
	}
	while (v > 0) {
		buf[--i] = (char)('0' + (v % 10));
		v /= 10;
	}
	write_all(buf + i, (size_t)(20 - i));
}

static void put_i64(int64_t v) {
	if (v < 0) {
		write_str("-");
		put_u64((uint64_t)(-v));
	} else {
		put_u64((uint64_t)v);
	}
}

static void report(const char *name, int ok, long rc) {
	total_n++;
	if (ok)
		pass_n++;
	write_str(name);
	if (ok) {
		write_str(" ok\n");
	} else {
		write_str(" fail ");
		put_i64(rc);
		write_str("\n");
	}
}

static void ok_ge0(const char *name, long rc) {
	report(name, rc >= 0, rc);
}

static void ok_eq(const char *name, long rc, long expect) {
	report(name, rc == expect, rc);
}

static void child_entry(void) __attribute__((noreturn));
static void child_entry(void) {
	ran = 1;
	syscall6(98, (long)&ran, 1, 1, 0, 0, 0);
	syscall6(93, 0, 0, 0, 0, 0, 0);
	for (;;) {
	}
}

static long spawn(void) {
	uint64_t top = ((uintptr_t)child_stack + sizeof child_stack) & ~(uint64_t)15;
	long flags = CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD |
				 CLONE_SYSVSEM | CLONE_SETTLS | CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID |
				 CLONE_CHILD_SETTID;
	register long x0 __asm__("x0") = flags;
	register long x1 __asm__("x1") = (long)top;
	register long x2 __asm__("x2") = (long)&ptid;
	register long x3 __asm__("x3") = (long)tls;
	register long x4 __asm__("x4") = (long)&ctid;
	register long x8 __asm__("x8") = 220;
	__asm__ volatile("svc #0\n\t"
					 "cbz x0, 1f\n\t"
					 "b 2f\n\t"
					 "1:\n\t"
					 "blr %[fn]\n\t"
					 "b 1b\n\t"
					 "2:\n\t"
					 : "+r"(x0)
					 : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x8), [fn] "r"(child_entry)
					 : "memory", "x5", "x6", "x7", "x30");
	return x0;
}

static struct sockaddr_in_k addr_loopback(void) {
	struct sockaddr_in_k sa;
	memset(&sa, 0, sizeof sa);
	sa.family = 2;
	sa.addr = 0x0100007f;
	return sa;
}

static void libc_smoke(void) {
	const char src[] = "xeneva musl clib";
	size_t n = strlen(src);
	char *buf = (char *)malloc(n + 1);
	time_t now;
	FILE *out;

	if (!buf) {
		write_str("libc fail\n");
		return;
	}
	memcpy(buf, src, n + 1);
	now = time(NULL);
	write_str(buf);
	write_str(" ");
	put_u64(n);
	write_str(" ");
	put_i64((int64_t)now);
	write_str("\n");
	free(buf);

	out = fopen("/clib.ok", "w");
	if (!out) {
		write_str("fopen fail\n");
		return;
	}
	fputs("ok\n", out);
	fclose(out);
}

static void run(void) {
	unsigned char buf[325];
	unsigned char small[64];
	unsigned char statb[256];
	long ts[2];
	long cwd;
	long efd;
	long fd;
	long mk;
	const char *file = "/c.dat";
	const char *dir = "/cdir";
	const char *exe = "/proc/self/exe";

	write_str("xeneva musl syscalls\n");

	cwd = syscall6(17, (long)buf, 64, 0, 0, 0, 0);
	report("getcwd", cwd == 2 && buf[0] == '/', cwd);

	efd = syscall6(19, 0, 0, 0, 0, 0, 0);
	ok_ge0("eventfd2", efd);
	if (efd >= 0) {
		uint64_t one = 1;
		uint64_t got = 0;
		long n = syscall6(64, efd, (long)&one, 8, 0, 0, 0);
		long r = syscall6(63, efd, (long)&got, 8, 0, 0, 0);
		report("eventfd2 io", n == 8 && r == 8 && got == 1, r);
	}

	fd = syscall6(56, -100, (long)file, 2 | 64, 0644, 0, 0);
	ok_ge0("openat", fd);
	mk = syscall6(34, -100, (long)dir, 0755, 0, 0, 0);
	report("mkdirat", mk == 0 || mk == EEXIST, mk);
	ok_eq("faccessat", syscall6(48, -100, (long)file, 4, 0, 0, 0), 0);

	if (fd >= 0) {
		const char msg[] = "hi\n";
		struct iovec_k iov;
		struct iovec_k iov_r;
		long n = syscall6(64, fd, (long)msg, 3, 0, 0, 0);
		long ep;
		report("write", n == 3, n);
		ok_eq("lseek", syscall6(62, fd, 0, 0, 0, 0, 0), 0);
		n = syscall6(63, fd, (long)small, 3, 0, 0, 0);
		report("read", n == 3 && small[0] == 'h', n);

		iov.base = (uint64_t)(uintptr_t)msg;
		iov.len = 3;
		n = syscall6(66, fd, (long)&iov, 1, 0, 0, 0);
		report("writev", n == 3, n);
		syscall6(62, fd, 0, 0, 0, 0, 0);
		iov_r.base = (uint64_t)(uintptr_t)small;
		iov_r.len = 3;
		n = syscall6(65, fd, (long)&iov_r, 1, 0, 0, 0);
		report("readv", n == 3, n);

		ok_eq("fstat", syscall6(80, fd, (long)statb, 0, 0, 0, 0), 0);
		ok_eq("newfstatat", syscall6(79, -100, (long)file, (long)statb, 0, 0, 0), 0);
		ok_eq("statx", syscall6(291, -100, (long)file, 0, 0, (long)statb, 0), 0);

		ep = syscall6(20, 0, 0, 0, 0, 0, 0);
		ok_ge0("epoll_create1", ep);
		if (ep >= 0) {
			unsigned char ev[12];
			unsigned char out[12];
			long ctl;
			long nw;
			memset(ev, 0, sizeof ev);
			ev[0] = 5;
			ctl = syscall6(21, ep, 1, fd, (long)ev, 0, 0);
			ok_eq("epoll_ctl", ctl, 0);
			nw = syscall6(22, ep, (long)out, 1, 0, 0, 0);
			report("epoll_pwait", nw >= 1, nw);
			syscall6(57, ep, 0, 0, 0, 0, 0);
		}

		{
			long d1 = syscall6(23, fd, 0, 0, 0, 0, 0);
			long d2 = syscall6(24, fd, 0, 0, 0, 0, 0);
			long d3 = syscall6(25, fd, 0, 0, 0, 0, 0);
			ok_ge0("dup", d1);
			ok_ge0("dup3", d2);
			ok_ge0("fcntl", d3);
			if (d1 >= 0)
				syscall6(57, d1, 0, 0, 0, 0, 0);
			if (d2 >= 0)
				syscall6(57, d2, 0, 0, 0, 0, 0);
			if (d3 >= 0)
				syscall6(57, d3, 0, 0, 0, 0, 0);
		}
		ok_eq("close", syscall6(57, fd, 0, 0, 0, 0, 0), 0);
	}

	ok_eq("ioctl", syscall6(29, 1, 0x5401, 0, 0, 0, 0), ENOTTY);

	{
		int pipes[2] = {-1, -1};
		long pp = syscall6(59, (long)pipes, 0, 0, 0, 0, 0);
		report("pipe2", pp == 0 && pipes[0] >= 0 && pipes[1] >= 0, pp);
		if (pipes[0] >= 0)
			syscall6(57, pipes[0], 0, 0, 0, 0, 0);
		if (pipes[1] >= 0)
			syscall6(57, pipes[1], 0, 0, 0, 0, 0);
	}

	{
		long dfd = syscall6(56, -100, (long)dir, 0, 0, 0, 0);
		if (dfd >= 0) {
			ok_eq("getdents64", syscall6(61, dfd, (long)buf, 64, 0, 0, 0), 0);
			syscall6(57, dfd, 0, 0, 0, 0, 0);
		} else {
			report("getdents64", 0, dfd);
		}
	}

	{
		struct pollfd_k pfd;
		pfd.fd = 1;
		pfd.events = 4;
		pfd.revents = 0;
		ok_ge0("ppoll", syscall6(73, (long)&pfd, 1, 0, 0, 0, 0));
	}

	{
		long rl = syscall6(78, -100, (long)exe, (long)small, 64, 0, 0);
		report("readlinkat", rl == 10, rl);
	}

	{
		long tid = syscall6(96, (long)&ctid, 0, 0, 0, 0, 0);
		ok_ge0("set_tid_address", tid);
		report("gettid", syscall6(178, 0, 0, 0, 0, 0, 0) == tid, tid);
	}

	{
		uint32_t word = 1;
		ok_ge0("futex", syscall6(98, (long)&word, 1, 1, 0, 0, 0));
		word = 2;
		ok_eq("futex wait", syscall6(98, (long)&word, 0, 1, 0, 0, 0), EAGAIN);
	}

	ok_eq("set_robust_list", syscall6(99, 0, 0, 0, 0, 0, 0), 0);
	ok_eq("get_robust_list", syscall6(100, 0, 0, 0, 0, 0, 0), 0);

	ts[0] = 0;
	ts[1] = 0;
	ok_eq("nanosleep", syscall6(101, (long)ts, 0, 0, 0, 0, 0), 0);
	ok_eq("clock_nanosleep", syscall6(115, 0, 0, (long)ts, 0, 0, 0), 0);
	ok_eq("clock_gettime", syscall6(113, 0, (long)ts, 0, 0, 0, 0), 0);
	ok_eq("clock_getres", syscall6(114, 0, (long)ts, 0, 0, 0, 0), 0);
	ok_eq("gettimeofday", syscall6(169, (long)ts, 0, 0, 0, 0, 0), 0);

	ok_eq("sched_setscheduler", syscall6(119, 0, 0, 0, 0, 0, 0), 0);
	ok_eq("sched_getscheduler", syscall6(120, 0, 0, 0, 0, 0, 0), 0);
	ok_eq("sched_setaffinity", syscall6(122, 0, 8, (long)buf, 0, 0, 0), 0);
	{
		long aff = syscall6(123, 0, 8, (long)buf, 0, 0, 0);
		report("sched_getaffinity", aff >= 8, aff);
	}
	ok_eq("sched_yield", syscall6(124, 0, 0, 0, 0, 0, 0), 0);
	ok_eq("tkill", syscall6(130, 0, 0, 0, 0, 0, 0), 0);
	ok_eq("sigaltstack", syscall6(132, 0, 0, 0, 0, 0, 0), 0);
	ok_eq("rt_sigaction", syscall6(134, 0, 0, 0, 0, 0, 0), 0);
	ok_eq("rt_sigprocmask", syscall6(135, 0, 0, 0, 0, 0, 0), 0);
	ok_eq("rt_sigreturn", syscall6(139, 0, 0, 0, 0, 0, 0), 0);

	{
		long un = syscall6(160, (long)buf, 0, 0, 0, 0, 0);
		report("uname", un == 0 && buf[0] == 'L' && buf[260] == 'a', un);
	}

	ok_eq("getrlimit", syscall6(163, 0, (long)statb, 0, 0, 0, 0), 0);
	ok_eq("prlimit64", syscall6(261, 0, 0, 0, (long)statb, 0, 0), 0);

	{
		const char name[] = "clib";
		long gn;
		ok_eq("prctl", syscall6(167, 15, (long)name, 0, 0, 0, 0), 0);
		memset(small, 0, 16);
		gn = syscall6(167, 16, (long)small, 0, 0, 0, 0);
		report("prctl get", gn == 0 && small[0] == 'c', gn);
	}

	ok_ge0("getpid", syscall6(172, 0, 0, 0, 0, 0, 0));
	ok_ge0("getppid", syscall6(173, 0, 0, 0, 0, 0, 0));
	ok_eq("getuid", syscall6(174, 0, 0, 0, 0, 0, 0), 0);
	ok_eq("geteuid", syscall6(175, 0, 0, 0, 0, 0, 0), 0);
	ok_eq("getgid", syscall6(176, 0, 0, 0, 0, 0, 0), 0);
	ok_eq("getegid", syscall6(177, 0, 0, 0, 0, 0, 0), 0);
	ok_eq("sysinfo", syscall6(179, (long)buf, 0, 0, 0, 0, 0), 0);

	{
		unsigned char rnd[16];
		ok_eq("getrandom", syscall6(278, (long)rnd, 16, 0, 0, 0, 0), 16);
	}

	{
		long brk0 = syscall6(214, 0, 0, 0, 0, 0, 0);
		long brk1 = syscall6(214, brk0 + 0x1000, 0, 0, 0, 0, 0);
		report("brk", brk0 > 0 && brk1 >= brk0, brk1);
	}

	{
		long map = syscall6(222, 0, 4096, 3, 0x22, -1, 0);
		report("mmap", map > 0, map);
		if (map > 0) {
			*(volatile unsigned char *)map = 0x5a;
			ok_eq("mprotect", syscall6(226, map, 4096, 3, 0, 0, 0), 0);
			ok_eq("madvise", syscall6(233, map, 4096, 0, 0, 0, 0), 0);
			ok_eq("munmap", syscall6(215, map, 4096, 0, 0, 0, 0), 0);
		}
	}
	{
		uint64_t fixed = 0x200000000000ull;
		long mf = syscall6(222, (long)fixed, 4096, 3, 0x32, -1, 0);
		report("mmap fixed", mf == (long)fixed, mf);
		if (mf == (long)fixed) {
			*(volatile unsigned char *)fixed = 1;
			syscall6(215, (long)fixed, 4096, 0, 0, 0, 0);
		}
	}
	ok_eq("mremap", syscall6(216, 0, 0, 0, 0, 0, 0), ENOSYS);
	ok_eq("membarrier", syscall6(283, 0, 0, 0, 0, 0, 0), 0);

	{
		long tcp = syscall6(198, 2, 1, 0, 0, 0, 0);
		ok_ge0("socket", tcp);
		if (tcp >= 0) {
			struct sockaddr_in_k sa = addr_loopback();
			ok_eq("bind", syscall6(200, tcp, (long)&sa, 16, 0, 0, 0), 0);
			ok_eq("listen", syscall6(201, tcp, 1, 0, 0, 0, 0), 0);
			ok_eq("shutdown", syscall6(210, tcp, 2, 0, 0, 0, 0), 0);
			syscall6(57, tcp, 0, 0, 0, 0, 0);
		}
	}

	{
		long udp = syscall6(198, 2, 2, 0, 0, 0, 0);
		ok_ge0("socket dgram", udp);
		if (udp >= 0) {
			struct sockaddr_in_k sa = addr_loopback();
			struct iovec_k iov;
			struct msghdr_k msg;
			const char byte[] = "z";
			uint32_t opt = 1;
			long bd;
			long sent;
			long sm;
			sa.port = 0x0900;
			bd = syscall6(200, udp, (long)&sa, 16, 0, 0, 0);
			report("bind dgram", bd == 0 || bd == -1, bd);
			ok_eq("connect", syscall6(203, udp, (long)&sa, 16, 0, 0, 0), 0);
			{
				long acc = syscall6(202, udp, 0, 0, 0, 0, 0);
				long acc4 = syscall6(242, udp, 0, 0, 0, 0, 0);
				report("accept", acc != ENOSYS, acc);
				report("accept4", acc4 != ENOSYS, acc4);
			}
			sent = syscall6(206, udp, (long)byte, 1, 0, (long)&sa, 16);
			report("sendto", sent >= 0 || sent == ENETUNREACH, sent);
			ok_ge0("recvfrom", syscall6(207, udp, (long)small, 8, 0, 0, 0));
			iov.base = (uint64_t)(uintptr_t)byte;
			iov.len = 1;
			memset(&msg, 0, sizeof msg);
			msg.name = (uint64_t)(uintptr_t)&sa;
			msg.namelen = 16;
			msg.iov = (uint64_t)(uintptr_t)&iov;
			msg.iovlen = 1;
			sm = syscall6(211, udp, (long)&msg, 0, 0, 0, 0);
			report("sendmsg", sm >= 0 || sm == ENETUNREACH, sm);
			ok_ge0("recvmsg", syscall6(212, udp, (long)&msg, 0, 0, 0, 0));
			ok_eq("setsockopt", syscall6(208, udp, 1, 2, (long)&opt, 4, 0), 0);
			syscall6(57, udp, 0, 0, 0, 0, 0);
		}
	}

	ok_eq("wait4", syscall6(260, -1, 0, 0, 0, 0, 0), ECHILD);
	ok_eq("rseq", syscall6(293, 0, 0, 0, 0, 0, 0), ENOSYS);
	ok_eq("clone3", syscall6(435, 0, 0, 0, 0, 0, 0), ENOSYS);

	ran = 0;
	ctid = 0;
	{
		long child = spawn();
		uint32_t stored = ctid;
		int spins = 0;
		report("clone", child > 0 && stored == (uint32_t)child, child);
		while (ctid != 0 && spins < 3) {
			ts[0] = 0;
			ts[1] = 200000000;
			syscall6(98, (long)&ctid, 0, (long)stored, (long)ts, 0, 0);
			syscall6(124, 0, 0, 0, 0, 0, 0);
			spins++;
		}
		report("exit", ran == 1 && ctid == 0, (long)ran);
	}

	write_str("servo musl ");
	put_u64(pass_n);
	write_str("/");
	put_u64(total_n);
	write_str("\n");
	syscall6(94, 0, 0, 0, 0, 0, 0);
}

int main(void) {
	libc_smoke();
	run();
	return 0;
}
