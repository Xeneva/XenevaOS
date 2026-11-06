/*
 * Process/netsystest/main.cpp -- native socket syscall conformance suite.
 *
 * Exercises the six network rows this branch is about:
 *   SYS_CREATE_SOCKET(46), SYS_NET_CONNECT(47), SYS_NET_SEND(48),
 *   SYS_NET_RECEIVE(49), SYS_SOCKET_SET_OPT(50), SYS_NET_ACCEPT(52)
 * plus the supporting getsockopt(180)/accept4(188) ABI and poll(83)
 * readiness/timeout behaviour.
 *
 * Every assertion is timed with _KeGetCurrentUS() and printed as
 *   [netsystest] PASS <name> <us> us
 * so a semantic fix that turns into a busy-wait, a lost wake-up, or a
 * 30-second timeout guard is visible instead of hiding behind a plain PASS.
 *
 * The suite has three tiers:
 *   1. loopback conformance -- hermetic socket ABI/errno/state coverage;
 *   2. real network -- DNS, TCP and HTTP/1.1 against example.com, so the
 *      resolver, default route, ARP/ND and external TCP path are exercised
 *      instead of only 127.0.0.1;
 *   3. performance -- explicit wall-clock budgets for sustained TCP,
 *      sendmmsg/recvmmsg UDP, connect churn, poll fast-path and DNS cache
 *      hits. These are end-to-end budgets, not microbenchmarks: they fail
 *      when an optimisation regresses into a timeout or an O(n) syscall-per-
 *      packet path. The 1 MiB TCP result also prints KiB/s; its buffer setup
 *      and integrity check are outside the timed region.
 *
 * An optional fourth tier measures the real virtio-net receive path:
 *   netsock --speed-host 10.0.2.2 --speed-port 9000 --speed-mib 8
 * connects to a host TCP server, reads exactly that many MiB and prints the
 * rate. It is skipped unless --speed-host is supplied.
 *
 * Real-network tests are skipped when the guest has no non-loopback IPv4
 * address (for example a --no-network image). Pass --no-real to skip them
 * explicitly on an offline host.
 *
 * The binary is an ordinary network-group application: no driver privileges.
 */
#include <stdint.h>
#include <_xeneva.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <arpa/inet.h>
#include <time.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <sys/netdb.h>
#include <sys/_ketime.h>
#include <sys/_kefile.h>
#include <sys/_keproc.h>
#include <sys/socket.h>
#include <https.h>

static int g_pass;
static int g_fail;
static int g_skip;
static int g_real_enabled = 1;
static uint64_t g_suite_start;
static const char* g_speed_host;
static int g_speed_port;
static int g_speed_mib;
static int g_speed_reps;

static uint64_t now_us(void) {
	return _KeGetCurrentUS();
}

static void rec(const char* name, int ok, long detail, uint64_t us) {
	if (ok)
		g_pass++;
	else
		g_fail++;
	_KePrint("[netsystest] %s %s %d us\r\n",
			 ok ? "PASS" : "FAIL", name, (int)us);
	if (!ok)
		_KePrint("[netsystest] detail=%d\r\n", (int)detail);
}

/* A skip is an environment limitation, not a pass: it is counted and
 * printed separately so a --no-network image cannot look fully green. */
static void skip(const char* name, const char* reason) {
	g_skip++;
	_KePrint("[netsystest] SKIP %s (%s)\r\n", name, reason);
}

static void rcv4(sockaddr_in* a, const char* ip, int port) {
	memset(a, 0, sizeof(*a));
	a->sin_family = AF_INET;
	a->sin_port = htons((unsigned short)port);
	a->sin_addr.s_addr = inet_addr(ip);
}

static void rcv4_6(sockaddr_in6* a, const char* ip, int port) {
	memset(a, 0, sizeof(*a));
	a->sin6_family = AF_INET6;
	a->sin6_port = htons((unsigned short)port);
	inet_pton(AF_INET6, ip, &a->sin6_addr);
}

/* One-iovec sendmsg/recvmsg helpers used by the socketpair and shutdown
 * tests. They keep the timing of every kernel transition visible without
 * repeating the msghdr boilerplate at each call site. */
static int send_buf(int fd, const void* buf, size_t len, int flags) {
	iovec iov;
	msghdr m;

	iov.iov_base = (void*)buf;
	iov.iov_len = len;
	memset(&m, 0, sizeof(m));
	m.msg_iov = &iov;
	m.msg_iovlen = 1;
	return (int)sendmsg(fd, &m, flags);
}

static int recv_buf(int fd, void* buf, size_t len, int flags) {
	iovec iov;
	msghdr m;

	iov.iov_base = buf;
	iov.iov_len = len;
	memset(&m, 0, sizeof(m));
	m.msg_iov = &iov;
	m.msg_iovlen = 1;
	return (int)recvmsg(fd, &m, flags);
}

/* --- socket/option ABI -------------------------------------------------- */
static void test_options(void) {
	uint64_t t0;
	int rc;
	int tcp, udp;
	socklen_t len;

	t0 = now_us();
	tcp = socket(AF_INET, SOCK_STREAM, 0);
	rec("opt.socket.tcp", tcp >= 0, tcp, now_us() - t0);

	t0 = now_us();
	udp = socket(AF_INET, SOCK_DGRAM, 0);
	rec("opt.socket.udp", udp >= 0, udp, now_us() - t0);

	t0 = now_us();
	rc = socket(999, SOCK_STREAM, 0);
	rec("opt.socket.bad-family", rc == -EAFNOSUPPORT, rc, now_us() - t0);

	t0 = now_us();
	rc = socket(AF_INET, 99, 0);
	rec("opt.socket.bad-type", rc == -EINVAL, rc, now_us() - t0);

	{
		int v = -1;
		len = sizeof(v);
		t0 = now_us();
		rc = getsockopt(tcp, SOL_SOCKET, SO_TYPE, &v, &len);
		rec("opt.getsockopt.type.tcp",
			rc == 0 && v == SOCK_STREAM && len == sizeof(int), v, now_us() - t0);
	}

	{
		int v = -1;
		len = sizeof(v);
		t0 = now_us();
		rc = getsockopt(udp, SOL_SOCKET, SO_TYPE, &v, &len);
		rec("opt.getsockopt.type.udp",
			rc == 0 && v == SOCK_DGRAM, v, now_us() - t0);
	}

	{
		int v = 32768;
		t0 = now_us();
		rc = setsockopt(tcp, SOL_SOCKET, SO_RCVBUF, &v, sizeof(v));
		rec("opt.setsockopt.rcvbuf", rc == 0, rc, now_us() - t0);

		v = 0;
		len = sizeof(v);
		t0 = now_us();
		rc = getsockopt(tcp, SOL_SOCKET, SO_RCVBUF, &v, &len);
		rec("opt.getsockopt.rcvbuf", rc == 0 && v == 32768, v, now_us() - t0);
	}

	{
		int v = 16384;
		t0 = now_us();
		rc = setsockopt(tcp, SOL_SOCKET, SO_SNDBUF, &v, sizeof(v));
		rec("opt.setsockopt.sndbuf", rc == 0, rc, now_us() - t0);

		v = 0;
		len = sizeof(v);
		t0 = now_us();
		rc = getsockopt(tcp, SOL_SOCKET, SO_SNDBUF, &v, &len);
		rec("opt.getsockopt.sndbuf", rc == 0 && v == 16384, v, now_us() - t0);
	}

	{
		int v = 1024;
		t0 = now_us();
		rc = setsockopt(tcp, SOL_SOCKET, SO_RCVBUF, &v, sizeof(v));
		rec("opt.setsockopt.rcvbuf.too-small", rc == -EINVAL, rc,
			now_us() - t0);
	}

	{
		int v = 1;
		t0 = now_us();
		rc = setsockopt(tcp, IPPROTOCOL_TCP, TCP_NODELAY, &v, sizeof(v));
		rec("opt.setsockopt.nodelay", rc == 0, rc, now_us() - t0);

		v = 0;
		len = sizeof(v);
		t0 = now_us();
		rc = getsockopt(tcp, IPPROTOCOL_TCP, TCP_NODELAY, &v, &len);
		rec("opt.getsockopt.nodelay", rc == 0 && v == 1, v, now_us() - t0);
	}

	{
		int v = 1;
		t0 = now_us();
		rc = setsockopt(tcp, SOL_SOCKET, SO_NONBLOCK, &v, sizeof(v));
		rec("opt.setsockopt.nonblock", rc == 0, rc, now_us() - t0);

		v = 0;
		len = sizeof(v);
		t0 = now_us();
		rc = getsockopt(tcp, SOL_SOCKET, SO_NONBLOCK, &v, &len);
		rec("opt.getsockopt.nonblock", rc == 0 && v == 1, v, now_us() - t0);
		v = 0;
		setsockopt(tcp, SOL_SOCKET, SO_NONBLOCK, &v, sizeof(v));
	}

	{
		int v = 0;
		len = sizeof(v);
		t0 = now_us();
		rc = getsockopt(tcp, SOL_SOCKET, SO_ACCEPTCONN, &v, &len);
		rec("opt.getsockopt.acceptconn.plain", rc == 0 && v == 0, v,
			now_us() - t0);
	}

	{
		int v = 0;
		t0 = now_us();
		rc = setsockopt(tcp, SOL_SOCKET, SO_ERROR, &v, sizeof(v));
		rec("opt.setsockopt.so-error.readonly", rc == -ENOPROTOOPT, rc,
			now_us() - t0);
	}

	{
		int v = 0;
		len = 2;
		t0 = now_us();
		rc = getsockopt(tcp, SOL_SOCKET, SO_TYPE, &v, &len);
		rec("opt.getsockopt.small-buffer",
			rc == -EINVAL && len == sizeof(int), rc, now_us() - t0);
	}

	{
		int v = 1;
		t0 = now_us();
		rc = setsockopt(udp, IPPROTOCOL_TCP, TCP_NODELAY, &v, sizeof(v));
		rec("opt.setsockopt.tcp-on-udp", rc == -ENOPROTOOPT, rc,
			now_us() - t0);
	}

	{
		int v = 1;
		len = sizeof(v);
		t0 = now_us();
		rc = getsockopt(9999, SOL_SOCKET, SO_TYPE, &v, &len);
		rec("opt.getsockopt.bad-fd", rc == -EBADF, rc, now_us() - t0);

		t0 = now_us();
		rc = setsockopt(9999, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(v));
		rec("opt.setsockopt.bad-fd", rc == -EBADF, rc, now_us() - t0);
	}

	_KeCloseFile(udp);
	_KeCloseFile(tcp);
}

/* --- TCP: blocking connect/accept, iovec, EOF, 32 KiB transfer ---------- */
static void test_tcp_blocking(void) {
	uint64_t t0;
	int rc;
	int lfd, cfd, afd, dupfd;
	sockaddr_in la, ca, peer;
	socklen_t len;

	t0 = now_us();
	lfd = socket(AF_INET, SOCK_STREAM, 0);
	rec("tcp.socket.listener", lfd >= 0, lfd, now_us() - t0);

	rcv4(&la, "127.0.0.1", 39001);
	t0 = now_us();
	rc = bind(lfd, (struct sockaddr*)&la, sizeof(la));
	rec("tcp.bind", rc == 0, rc, now_us() - t0);

	t0 = now_us();
	rc = listen(lfd, 8);
	rec("tcp.listen", rc == 0, rc, now_us() - t0);

	t0 = now_us();
	rc = connect(lfd, (sockaddr_*)&la, sizeof(la));
	rec("tcp.connect.listener-einval", rc == -EINVAL, rc, now_us() - t0);

	{
		int v = 0;
		len = sizeof(v);
		t0 = now_us();
		rc = getsockopt(lfd, SOL_SOCKET, SO_ACCEPTCONN, &v, &len);
		rec("tcp.acceptconn.listener", rc == 0 && v == 1, v,
			now_us() - t0);
	}

	dupfd = socket(AF_INET, SOCK_STREAM, 0);
	t0 = now_us();
	rc = bind(dupfd, (struct sockaddr*)&la, sizeof(la));
	rec("tcp.bind.addr-in-use", rc == -EADDRINUSE, rc, now_us() - t0);
	_KeCloseFile(dupfd);

	cfd = socket(AF_INET, SOCK_STREAM, 0);
	rcv4(&ca, "127.0.0.1", 39001);
	t0 = now_us();
	rc = connect(cfd, (sockaddr_*)&ca, sizeof(ca));
	{
		uint64_t us = now_us() - t0;
		rec("tcp.connect.blocking", rc == 0 && us <= 200000, rc, us);
	}

	t0 = now_us();
	rc = connect(cfd, (sockaddr_*)&ca, sizeof(ca));
	rec("tcp.connect.already-eisconn", rc == -EISCONN, rc, now_us() - t0);

	{
		int err = -1;
		len = sizeof(err);
		t0 = now_us();
		rc = getsockopt(cfd, SOL_SOCKET, SO_ERROR, &err, &len);
		rec("tcp.connect.so-error", rc == 0 && err == 0, err,
			now_us() - t0);
	}

	{
		struct pollfd p;
		p.fd = lfd;
		p.events = POLLIN;
		p.revents = 0;
		t0 = now_us();
		rc = poll(&p, 1, 0);
		rec("tcp.poll.listener.pending",
			rc == 1 && (p.revents & POLLIN), p.revents, now_us() - t0);
	}

	memset(&peer, 0, sizeof(peer));
	len = sizeof(peer);
	t0 = now_us();
	afd = accept4(lfd, (struct sockaddr*)&peer, &len, SOCK_NONBLOCK);
	rec("tcp.accept4.nonblock",
		afd >= 0 && peer.sin_family == AF_INET && ntohs(peer.sin_port) != 0,
		afd, now_us() - t0);

	{
		char buf[4];
		t0 = now_us();
		rc = (int)recvfrom(afd, buf, sizeof(buf), 0, NULL, NULL);
		rec("tcp.accept4.child-nonblock", rc == -EAGAIN, rc,
			now_us() - t0);
	}

	{
		int v = -1;
		len = sizeof(v);
		t0 = now_us();
		rc = getsockopt(afd, SOL_SOCKET, SO_ACCEPTCONN, &v, &len);
		rec("tcp.acceptconn.child", rc == 0 && v == 0, v, now_us() - t0);
	}

	{
		char hi[] = "hello ";
		char lo[] = "world";
		iovec iov[2];
		msghdr m;

		iov[0].iov_base = hi;
		iov[0].iov_len = 6;
		iov[1].iov_base = lo;
		iov[1].iov_len = 5;
		memset(&m, 0, sizeof(m));
		m.msg_iov = iov;
		m.msg_iovlen = 2;
		t0 = now_us();
		rc = (int)sendmsg(cfd, &m, 0);
		rec("tcp.sendmsg.iovec", rc == 11, rc, now_us() - t0);
	}

	{
		char r1[5];
		char r2[6];
		iovec iov[2];
		msghdr m;

		memset(r1, 0, sizeof(r1));
		memset(r2, 0, sizeof(r2));
		iov[0].iov_base = r1;
		iov[0].iov_len = sizeof(r1);
		iov[1].iov_base = r2;
		iov[1].iov_len = sizeof(r2);
		memset(&m, 0, sizeof(m));
		m.msg_iov = iov;
		m.msg_iovlen = 2;
		t0 = now_us();
		rc = (int)recvmsg(afd, &m, MSG_WAITALL);
		rec("tcp.recvmsg.iovec-waitall",
			rc == 11 && memcmp(r1, "hello", 5) == 0 &&
				memcmp(r2, " world", 6) == 0,
			rc, now_us() - t0);
	}

	{
		char one[4];
		iovec iov;
		msghdr m;

		iov.iov_base = one;
		iov.iov_len = sizeof(one);
		memset(&m, 0, sizeof(m));
		m.msg_iov = &iov;
		m.msg_iovlen = 1;
		t0 = now_us();
		rc = (int)recvmsg(afd, &m, MSG_DONTWAIT);
		rec("tcp.recvmsg.eagain", rc == -EAGAIN, rc, now_us() - t0);
	}

	{
		char peek[] = "peek";
		char got[4];
		iovec iov;
		msghdr m;

		iov.iov_base = peek;
		iov.iov_len = 4;
		memset(&m, 0, sizeof(m));
		m.msg_iov = &iov;
		m.msg_iovlen = 1;
		t0 = now_us();
		rc = (int)sendmsg(cfd, &m, 0);
		rec("tcp.sendmsg.peek-setup", rc == 4, rc, now_us() - t0);

		memset(got, 0, sizeof(got));
		iov.iov_base = got;
		iov.iov_len = sizeof(got);
		memset(&m, 0, sizeof(m));
		m.msg_iov = &iov;
		m.msg_iovlen = 1;
		t0 = now_us();
		rc = (int)recvmsg(afd, &m, MSG_PEEK);
		rec("tcp.recvmsg.peek", rc == 4 && memcmp(got, "peek", 4) == 0,
			rc, now_us() - t0);

		memset(got, 0, sizeof(got));
		t0 = now_us();
		rc = (int)recvmsg(afd, &m, 0);
		rec("tcp.recvmsg.peek-consumed",
			rc == 4 && memcmp(got, "peek", 4) == 0, rc, now_us() - t0);
	}

	{
		const size_t total = 32768;
		char* out = (char*)malloc(total);
		char* in = (char*)malloc(total);
		iovec iov;
		msghdr m;

		if (!out || !in) {
			rec("tcp.transfer.32k.alloc", 0, 0, 0);
		} else {
			for (size_t i = 0; i < total; i++)
				out[i] = (char)(i * 31 + 7);
			memset(in, 0, total);
			iov.iov_base = out;
			iov.iov_len = total;
			memset(&m, 0, sizeof(m));
			m.msg_iov = &iov;
			m.msg_iovlen = 1;
			t0 = now_us();
			rc = (int)sendmsg(cfd, &m, 0);
			{
				uint64_t us = now_us() - t0;
				rec("tcp.send.32k", rc == (int)total && us <= 500000, rc, us);
			}

			iov.iov_base = in;
			iov.iov_len = total;
			memset(&m, 0, sizeof(m));
			m.msg_iov = &iov;
			m.msg_iovlen = 1;
			t0 = now_us();
			rc = (int)recvmsg(afd, &m, MSG_WAITALL);
			{
				uint64_t us = now_us() - t0;
				rec("tcp.recv.32k",
					rc == (int)total && memcmp(out, in, total) == 0 &&
						us <= 500000,
					rc, us);
			}
			free(in);
			free(out);
		}
	}

	{
		char eof[4];
		iovec iov;
		msghdr m;

		t0 = now_us();
		_KeCloseFile(cfd);
		cfd = -1;
		iov.iov_base = eof;
		iov.iov_len = sizeof(eof);
		memset(&m, 0, sizeof(m));
		m.msg_iov = &iov;
		m.msg_iovlen = 1;
		rc = (int)recvmsg(afd, &m, 0);
		rec("tcp.recv.eof-after-peer-close", rc == 0, rc, now_us() - t0);
	}

	if (afd >= 0)
		_KeCloseFile(afd);
	if (cfd >= 0)
		_KeCloseFile(cfd);
	_KeCloseFile(lfd);
}

/* --- TCP: non-blocking connect, refused connect, SO_ERROR --------------- */
static void test_tcp_nonblock(void) {
	uint64_t t0;
	int rc;
	int lfd, cfd, afd;
	sockaddr_in la, ca;
	socklen_t len;

	lfd = socket(AF_INET, SOCK_STREAM, 0);
	rcv4(&la, "127.0.0.1", 39002);
	rc = bind(lfd, (struct sockaddr*)&la, sizeof(la));
	rc = listen(lfd, 8);
	rec("tcp.nonblock.listener", rc == 0, rc, 0);

	cfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	rcv4(&ca, "127.0.0.1", 39002);
	t0 = now_us();
	rc = connect(cfd, (sockaddr_*)&ca, sizeof(ca));
	rec("tcp.connect.nonblock",
		rc == 0 || rc == -EINPROGRESS, rc, now_us() - t0);

	{
		struct pollfd p;
		p.fd = cfd;
		p.events = POLLOUT;
		p.revents = 0;
		t0 = now_us();
		rc = poll(&p, 1, 1000);
		rec("tcp.connect.nonblock.poll-writable",
			rc == 1 && (p.revents & POLLOUT), p.revents, now_us() - t0);
	}

	{
		int err = -1;
		len = sizeof(err);
		t0 = now_us();
		rc = getsockopt(cfd, SOL_SOCKET, SO_ERROR, &err, &len);
		rec("tcp.connect.nonblock.so-error", rc == 0 && err == 0, err,
			now_us() - t0);
	}

	len = sizeof(ca);
	t0 = now_us();
	afd = accept(lfd, (struct sockaddr*)&ca, &len);
	rec("tcp.accept.after-nonblock", afd >= 0, afd, now_us() - t0);

	if (afd >= 0)
		_KeCloseFile(afd);
	_KeCloseFile(cfd);
	_KeCloseFile(lfd);

	/* Refused connect: no listener on 39099; loopback RST is synchronous. */
	cfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	rcv4(&ca, "127.0.0.1", 39099);
	t0 = now_us();
	rc = connect(cfd, (sockaddr_*)&ca, sizeof(ca));
	rec("tcp.connect.refused.progress", rc == -EINPROGRESS, rc,
		now_us() - t0);

	{
		int err = -1;
		len = sizeof(err);
		t0 = now_us();
		rc = getsockopt(cfd, SOL_SOCKET, SO_ERROR, &err, &len);
		rec("tcp.connect.refused.so-error",
			rc == 0 && err == ECONNREFUSED, err, now_us() - t0);

		err = -1;
		len = sizeof(err);
		t0 = now_us();
		rc = getsockopt(cfd, SOL_SOCKET, SO_ERROR, &err, &len);
		rec("tcp.connect.refused.so-error-cleared",
			rc == 0 && err == 0, err, now_us() - t0);
	}

	_KeCloseFile(cfd);
}

/* --- UDP: bind/sendto/connect/sendmsg/recvfrom/poll --------------------- */
static void test_udp(void) {
	uint64_t t0;
	int rc;
	int rfd, sfd, nfd;
	sockaddr_in ra, src;
	socklen_t len;

	rfd = socket(AF_INET, SOCK_DGRAM, 0);
	rcv4(&ra, "127.0.0.1", 39003);
	t0 = now_us();
	rc = bind(rfd, (struct sockaddr*)&ra, sizeof(ra));
	rec("udp.bind.receiver", rc == 0, rc, now_us() - t0);

	sfd = socket(AF_INET, SOCK_DGRAM, 0);
	t0 = now_us();
	rc = (int)sendto(sfd, "udp-one", 7, 0,
		(struct sockaddr*)&ra, sizeof(ra));
	rec("udp.sendto", rc == 7, rc, now_us() - t0);

	{
		struct pollfd p;
		p.fd = rfd;
		p.events = POLLIN;
		p.revents = 0;
		t0 = now_us();
		rc = poll(&p, 1, 1000);
		rec("udp.poll.readable", rc == 1 && (p.revents & POLLIN),
			p.revents, now_us() - t0);
	}

	{
		char buf[32];
		memset(buf, 0, sizeof(buf));
		memset(&src, 0, sizeof(src));
		len = sizeof(src);
		t0 = now_us();
		rc = (int)recvfrom(rfd, buf, sizeof(buf), 0,
			(struct sockaddr*)&src, &len);
		rec("udp.recvfrom",
			rc == 7 && memcmp(buf, "udp-one", 7) == 0 &&
				src.sin_family == AF_INET && ntohs(src.sin_port) != 0 &&
				len == sizeof(sockaddr_in),
			rc, now_us() - t0);
	}

	t0 = now_us();
	rc = connect(sfd, (sockaddr_*)&ra, sizeof(ra));
	rec("udp.connect", rc == 0, rc, now_us() - t0);

	{
		char text[] = "udp-two";
		iovec iov;
		msghdr m;

		iov.iov_base = text;
		iov.iov_len = 7;
		memset(&m, 0, sizeof(m));
		m.msg_iov = &iov;
		m.msg_iovlen = 1;
		t0 = now_us();
		rc = (int)sendmsg(sfd, &m, 0);
		rec("udp.sendmsg.connected", rc == 7, rc, now_us() - t0);
	}

	{
		char buf[32];
		memset(buf, 0, sizeof(buf));
		len = 0;
		t0 = now_us();
		rc = (int)recvfrom(rfd, buf, sizeof(buf), 0, NULL, NULL);
		rec("udp.recv.connected-send",
			rc == 7 && memcmp(buf, "udp-two", 7) == 0, rc,
			now_us() - t0);
	}

	{
		int v = -1;
		len = sizeof(v);
		rc = getsockopt(rfd, SOL_SOCKET, SO_TYPE, &v, &len);
		rec("udp.getsockopt.type", rc == 0 && v == SOCK_DGRAM, v, 0);
	}

	nfd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
	{
		char buf[8];
		iovec iov;
		msghdr m;

		iov.iov_base = buf;
		iov.iov_len = sizeof(buf);
		memset(&m, 0, sizeof(m));
		m.msg_iov = &iov;
		m.msg_iovlen = 1;
		t0 = now_us();
		rc = (int)recvmsg(nfd, &m, 0);
		rec("udp.recvmsg.eagain", rc == -EAGAIN, rc, now_us() - t0);
	}

	{
		struct pollfd p;
		p.fd = nfd;
		p.events = POLLIN;
		p.revents = 0;
		t0 = now_us();
		rc = poll(&p, 1, 0);
		rec("udp.poll.empty-nonblock", rc == 0, p.revents, now_us() - t0);
	}

	_KeCloseFile(nfd);
	_KeCloseFile(sfd);
	_KeCloseFile(rfd);
}

/* --- AF_UNIX: stream and datagram -------------------------------------- */
static void test_unix(void) {
	uint64_t t0;
	int rc;
	int sfd = -1, cfd = -1, afd = -1;
	int dfd = -1, xfd = -1;
	sockaddr_un sa, ca, da, xa, src;
	socklen_t len;

	t0 = now_us();
	sfd = socket(AF_UNIX, SOCK_STREAM, 0);
	rec("unix.stream.socket", sfd >= 0, sfd, now_us() - t0);

	memset(&sa, 0, sizeof(sa));
	sa.sun_family = AF_UNIX;
	strcpy(sa.sun_path, "/tmp/netsystest.stream");
	t0 = now_us();
	rc = bind(sfd, (struct sockaddr*)&sa, sizeof(sa));
	rec("unix.stream.bind", rc == 0, rc, now_us() - t0);

	t0 = now_us();
	rc = listen(sfd, 4);
	rec("unix.stream.listen", rc == 0, rc, now_us() - t0);

	{
		int v = -1;
		len = sizeof(v);
		rc = getsockopt(sfd, SOL_SOCKET, SO_ACCEPTCONN, &v, &len);
		rec("unix.stream.acceptconn", rc == 0 && v == 1, v, 0);
	}

	{
		int dupfd = socket(AF_UNIX, SOCK_STREAM, 0);
		t0 = now_us();
		rc = bind(dupfd, (struct sockaddr*)&sa, sizeof(sa));
		rec("unix.stream.bind.addr-in-use", rc == -EADDRINUSE, rc,
			now_us() - t0);
		_KeCloseFile(dupfd);
	}

	cfd = socket(AF_UNIX, SOCK_STREAM, 0);
	memset(&ca, 0, sizeof(ca));
	ca.sun_family = AF_UNIX;
	strcpy(ca.sun_path, "/tmp/netsystest.stream");
	t0 = now_us();
	rc = connect(cfd, (sockaddr_*)&ca, sizeof(ca));
	rec("unix.stream.connect", rc == 0, rc, now_us() - t0);

	{
		struct pollfd p;
		p.fd = sfd;
		p.events = POLLIN;
		p.revents = 0;
		t0 = now_us();
		rc = poll(&p, 1, 0);
		rec("unix.stream.poll.pending",
			rc == 1 && (p.revents & POLLIN), p.revents, now_us() - t0);
	}

	memset(&ca, 0, sizeof(ca));
	len = sizeof(ca);
	t0 = now_us();
	afd = accept(sfd, (struct sockaddr*)&ca, &len);
	rec("unix.stream.accept", afd >= 0, afd, now_us() - t0);

	{
		char ping[] = "unix-ping";
		char got[16];
		iovec iov;
		msghdr m;

		iov.iov_base = ping;
		iov.iov_len = 9;
		memset(&m, 0, sizeof(m));
		m.msg_iov = &iov;
		m.msg_iovlen = 1;
		t0 = now_us();
		rc = (int)sendmsg(cfd, &m, 0);
		rec("unix.stream.send", rc == 9, rc, now_us() - t0);

		memset(got, 0, sizeof(got));
		iov.iov_base = got;
		iov.iov_len = sizeof(got);
		memset(&m, 0, sizeof(m));
		m.msg_iov = &iov;
		m.msg_iovlen = 1;
		t0 = now_us();
		rc = (int)recvmsg(afd, &m, MSG_WAITALL);
		rec("unix.stream.recv",
			rc == 9 && memcmp(got, "unix-ping", 9) == 0, rc,
			now_us() - t0);
	}

	{
		char pong[] = "unix-pong";
		char got[16];
		iovec iov;
		msghdr m;

		iov.iov_base = pong;
		iov.iov_len = 9;
		memset(&m, 0, sizeof(m));
		m.msg_iov = &iov;
		m.msg_iovlen = 1;
		t0 = now_us();
		rc = (int)sendmsg(afd, &m, 0);
		rec("unix.stream.send.reverse", rc == 9, rc, now_us() - t0);

		memset(got, 0, sizeof(got));
		iov.iov_base = got;
		iov.iov_len = sizeof(got);
		t0 = now_us();
		rc = (int)recvmsg(cfd, &m, 0);
		rec("unix.stream.recv.reverse",
			rc == 9 && memcmp(got, "unix-pong", 9) == 0, rc,
			now_us() - t0);
	}

	{
		char one[4];
		iovec iov;
		msghdr m;

		iov.iov_base = one;
		iov.iov_len = sizeof(one);
		memset(&m, 0, sizeof(m));
		m.msg_iov = &iov;
		m.msg_iovlen = 1;
		t0 = now_us();
		_KeCloseFile(cfd);
		cfd = -1;
		rc = (int)recvmsg(afd, &m, 0);
		rec("unix.stream.recv.eof", rc == 0, rc, now_us() - t0);
	}

	{
		int miss = socket(AF_UNIX, SOCK_STREAM, 0);
		sockaddr_un ma;
		memset(&ma, 0, sizeof(ma));
		ma.sun_family = AF_UNIX;
		strcpy(ma.sun_path, "/tmp/netsystest.missing");
		t0 = now_us();
		rc = connect(miss, (sockaddr_*)&ma, sizeof(ma));
		rec("unix.stream.connect.missing", rc == -ECONNREFUSED, rc,
			now_us() - t0);
		_KeCloseFile(miss);
	}

	{
		rc = socket(AF_UNIX, SOCK_STREAM, IPPROTOCOL_TCP);
		rec("unix.socket.bad-protocol", rc == -EINVAL, rc, 0);
	}

	/* Datagrams. */
	dfd = socket(AF_UNIX, SOCK_DGRAM, 0);
	memset(&da, 0, sizeof(da));
	da.sun_family = AF_UNIX;
	strcpy(da.sun_path, "/tmp/netsystest.dgram");
	t0 = now_us();
	rc = bind(dfd, (struct sockaddr*)&da, sizeof(da));
	rec("unix.dgram.bind.receiver", rc == 0, rc, now_us() - t0);

	xfd = socket(AF_UNIX, SOCK_DGRAM, 0);
	memset(&xa, 0, sizeof(xa));
	xa.sun_family = AF_UNIX;
	strcpy(xa.sun_path, "/tmp/netsystest.sender");
	t0 = now_us();
	rc = bind(xfd, (struct sockaddr*)&xa, sizeof(xa));
	rec("unix.dgram.bind.sender", rc == 0, rc, now_us() - t0);

	memset(&src, 0, sizeof(src));
	src.sun_family = AF_UNIX;
	strcpy(src.sun_path, "/tmp/netsystest.absent");
	t0 = now_us();
	rc = (int)sendto(xfd, "no-such", 7, 0,
		(struct sockaddr*)&src, sizeof(src));
	rec("unix.dgram.send.missing", rc == -ENOENT, rc, now_us() - t0);

	t0 = now_us();
	rc = (int)sendto(xfd, "udp-unix", 8, 0,
		(struct sockaddr*)&da, sizeof(da));
	rec("unix.dgram.sendto", rc == 8, rc, now_us() - t0);

	{
		struct pollfd p;
		p.fd = dfd;
		p.events = POLLIN;
		p.revents = 0;
		t0 = now_us();
		rc = poll(&p, 1, 0);
		rec("unix.dgram.poll.readable",
			rc == 1 && (p.revents & POLLIN), p.revents, now_us() - t0);
	}

	{
		char buf[32];
		memset(buf, 0, sizeof(buf));
		memset(&src, 0, sizeof(src));
		len = sizeof(src);
		t0 = now_us();
		rc = (int)recvfrom(dfd, buf, sizeof(buf), 0,
			(struct sockaddr*)&src, &len);
		rec("unix.dgram.recvfrom",
			rc == 8 && memcmp(buf, "udp-unix", 8) == 0 &&
				src.sun_family == AF_UNIX &&
				strcmp(src.sun_path, "/tmp/netsystest.sender") == 0 &&
				len == sizeof(sockaddr_un),
			rc, now_us() - t0);
	}

	t0 = now_us();
	rc = connect(xfd, (sockaddr_*)&da, sizeof(da));
	rec("unix.dgram.connect", rc == 0, rc, now_us() - t0);

	{
		char d2[] = "unix-dg2";
		iovec iov;
		msghdr m;

		iov.iov_base = d2;
		iov.iov_len = 8;
		memset(&m, 0, sizeof(m));
		m.msg_iov = &iov;
		m.msg_iovlen = 1;
		t0 = now_us();
		rc = (int)sendmsg(xfd, &m, 0);
		rec("unix.dgram.sendmsg.connected", rc == 8, rc, now_us() - t0);
	}

	{
		char buf[32];
		memset(buf, 0, sizeof(buf));
		t0 = now_us();
		rc = (int)recvfrom(dfd, buf, sizeof(buf), 0, NULL, NULL);
		rec("unix.dgram.recv.connected",
			rc == 8 && memcmp(buf, "unix-dg2", 8) == 0, rc,
			now_us() - t0);
	}

	/* A connected sender must notice that the receiver went away instead
	 * of queueing into a socket that is being freed. */
	_KeCloseFile(dfd);
	dfd = -1;
	{
		char d3[] = "gone";
		iovec iov;
		msghdr m;

		iov.iov_base = d3;
		iov.iov_len = 4;
		memset(&m, 0, sizeof(m));
		m.msg_iov = &iov;
		m.msg_iovlen = 1;
		t0 = now_us();
		rc = (int)sendmsg(xfd, &m, 0);
		rec("unix.dgram.peer-closed", rc == -ECONNREFUSED, rc,
			now_us() - t0);
	}

	if (xfd >= 0)
		_KeCloseFile(xfd);
	if (dfd >= 0)
		_KeCloseFile(dfd);
	if (afd >= 0)
		_KeCloseFile(afd);
	if (cfd >= 0)
		_KeCloseFile(cfd);
	if (sfd >= 0)
		_KeCloseFile(sfd);
}

/* --- socketpair: linked AF_UNIX peers ---------------------------------- */
static void test_socketpair(void) {
	uint64_t t0;
	int rc;
	int sv[2];
	int dv[2];
	char buf[32];

	sv[0] = sv[1] = -1;
	t0 = now_us();
	rc = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
	rec("pair.stream.create",
		rc == 0 && sv[0] >= 0 && sv[1] >= 0 && sv[0] != sv[1], rc,
		now_us() - t0);

	memset(buf, 0, sizeof(buf));
	t0 = now_us();
	rc = send_buf(sv[0], "pair-a", 6, 0);
	rec("pair.stream.send", rc == 6, rc, now_us() - t0);

	t0 = now_us();
	rc = recv_buf(sv[1], buf, sizeof(buf), 0);
	rec("pair.stream.recv",
		rc == 6 && memcmp(buf, "pair-a", 6) == 0, rc, now_us() - t0);

	memset(buf, 0, sizeof(buf));
	t0 = now_us();
	rc = send_buf(sv[1], "pair-b", 6, 0);
	rec("pair.stream.send.reverse", rc == 6, rc, now_us() - t0);

	t0 = now_us();
	rc = recv_buf(sv[0], buf, sizeof(buf), 0);
	rec("pair.stream.recv.reverse",
		rc == 6 && memcmp(buf, "pair-b", 6) == 0, rc, now_us() - t0);

	_KeCloseFile(sv[1]);
	sv[1] = -1;
	t0 = now_us();
	rc = recv_buf(sv[0], buf, sizeof(buf), 0);
	rec("pair.stream.eof", rc == 0, rc, now_us() - t0);
	_KeCloseFile(sv[0]);
	sv[0] = -1;

	dv[0] = dv[1] = -1;
	t0 = now_us();
	rc = socketpair(AF_UNIX, SOCK_DGRAM, 0, dv);
	rec("pair.dgram.create",
		rc == 0 && dv[0] >= 0 && dv[1] >= 0 && dv[0] != dv[1], rc,
		now_us() - t0);

	memset(buf, 0, sizeof(buf));
	t0 = now_us();
	rc = send_buf(dv[0], "dg", 2, 0);
	rec("pair.dgram.send", rc == 2, rc, now_us() - t0);

	t0 = now_us();
	rc = recv_buf(dv[1], buf, sizeof(buf), 0);
	rec("pair.dgram.recv", rc == 2 && memcmp(buf, "dg", 2) == 0, rc,
		now_us() - t0);
	_KeCloseFile(dv[0]);
	_KeCloseFile(dv[1]);

	t0 = now_us();
	rc = socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sv);
	rec("pair.nonblock.create", rc == 0, rc, now_us() - t0);

	t0 = now_us();
	rc = recv_buf(sv[0], buf, sizeof(buf), 0);
	rec("pair.nonblock.recv-eagain", rc == -EAGAIN, rc, now_us() - t0);

	t0 = now_us();
	rc = send_buf(sv[1], "nb", 2, 0);
	rec("pair.nonblock.send", rc == 2, rc, now_us() - t0);

	t0 = now_us();
	rc = recv_buf(sv[0], buf, sizeof(buf), 0);
	rec("pair.nonblock.recv",
		rc == 2 && memcmp(buf, "nb", 2) == 0, rc, now_us() - t0);
	_KeCloseFile(sv[0]);
	_KeCloseFile(sv[1]);

	t0 = now_us();
	rc = socketpair(AF_INET, SOCK_STREAM, 0, sv);
	rec("pair.domain.eafnosupport", rc == -EAFNOSUPPORT, rc,
		now_us() - t0);

	t0 = now_us();
	rc = socketpair(AF_UNIX, SOCK_RAW, 0, sv);
	rec("pair.type.eopnotsupp", rc == -EOPNOTSUPP, rc, now_us() - t0);

	t0 = now_us();
	rc = socketpair(AF_UNIX, SOCK_STREAM, IPPROTOCOL_TCP, sv);
	rec("pair.protocol.einval", rc == -EINVAL, rc, now_us() - t0);

	t0 = now_us();
	rc = socketpair(AF_UNIX, SOCK_STREAM, 0, (int*)0);
	rec("pair.sv.efault", rc == -EFAULT, rc, now_us() - t0);
}

/* --- shutdown: half-close on UNIX, TCP and UDP -------------------------- */
static void test_shutdown(void) {
	uint64_t t0;
	int rc;
	int a, b;
	char buf[32];

	/* UNIX stream: SHUT_WR gives the peer EOF but keeps its write side. */
	a = b = -1;
	{
		int sv[2];

		sv[0] = sv[1] = -1;
		rc = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
		a = sv[0];
		b = sv[1];
	}
	t0 = now_us();
	rc = shutdown(a, SHUT_WR);
	rec("shutdown.stream.wr", rc == 0, rc, now_us() - t0);

	t0 = now_us();
	rc = send_buf(a, "x", 1, 0);
	rec("shutdown.stream.send-epipe", rc == -EPIPE, rc, now_us() - t0);

	t0 = now_us();
	rc = recv_buf(b, buf, sizeof(buf), 0);
	rec("shutdown.stream.peer-eof", rc == 0, rc, now_us() - t0);

	memset(buf, 0, sizeof(buf));
	t0 = now_us();
	rc = send_buf(b, "back", 4, 0);
	rec("shutdown.stream.reverse-send", rc == 4, rc, now_us() - t0);

	t0 = now_us();
	rc = recv_buf(a, buf, sizeof(buf), 0);
	rec("shutdown.stream.reverse-recv",
		rc == 4 && memcmp(buf, "back", 4) == 0, rc, now_us() - t0);

	t0 = now_us();
	rc = shutdown(a, SHUT_WR);
	rec("shutdown.stream.repeat", rc == 0, rc, now_us() - t0);

	t0 = now_us();
	rc = shutdown(a, 7);
	rec("shutdown.how-einval", rc == -EINVAL, rc, now_us() - t0);
	_KeCloseFile(a);
	_KeCloseFile(b);

	/* UNIX stream: SHUT_RD is an immediate EOF even with queued data. */
	{
		int sv[2];

		sv[0] = sv[1] = -1;
		socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
		a = sv[0];
		b = sv[1];
	}
	t0 = now_us();
	rc = send_buf(b, "queued", 6, 0);
	rec("shutdown.stream.rd-setup", rc == 6, rc, now_us() - t0);

	t0 = now_us();
	rc = shutdown(a, SHUT_RD);
	rec("shutdown.stream.rd", rc == 0, rc, now_us() - t0);

	t0 = now_us();
	rc = recv_buf(a, buf, sizeof(buf), 0);
	rec("shutdown.stream.rd-eof", rc == 0, rc, now_us() - t0);

	t0 = now_us();
	rc = send_buf(b, "more", 4, 0);
	rec("shutdown.stream.rd-peer-send", rc == 4, rc, now_us() - t0);
	_KeCloseFile(a);
	_KeCloseFile(b);

	/* UNIX datagrams: flags are local to the connected pair. */
	{
		int sv[2];

		sv[0] = sv[1] = -1;
		socketpair(AF_UNIX, SOCK_DGRAM, 0, sv);
		a = sv[0];
		b = sv[1];
	}
	t0 = now_us();
	rc = shutdown(a, SHUT_WR);
	rec("shutdown.dgram.wr", rc == 0, rc, now_us() - t0);

	t0 = now_us();
	rc = send_buf(a, "x", 1, 0);
	rec("shutdown.dgram.send-epipe", rc == -EPIPE, rc, now_us() - t0);

	t0 = now_us();
	rc = shutdown(b, SHUT_RD);
	rec("shutdown.dgram.rd", rc == 0, rc, now_us() - t0);

	t0 = now_us();
	rc = recv_buf(b, buf, sizeof(buf), 0);
	rec("shutdown.dgram.recv-eof", rc == 0, rc, now_us() - t0);
	_KeCloseFile(a);
	_KeCloseFile(b);

	/* Unconnected sockets and invalid descriptors. */
	{
		int s = socket(AF_UNIX, SOCK_STREAM, 0);
		t0 = now_us();
		rc = shutdown(s, SHUT_WR);
		rec("shutdown.stream.not-connected", rc == -ENOTCONN, rc,
			now_us() - t0);
		_KeCloseFile(s);
	}

	t0 = now_us();
	rc = shutdown(9999, SHUT_WR);
	rec("shutdown.bad-fd", rc == -EBADF, rc, now_us() - t0);

	/* TCP: FIN carries the pending data, then the peer can reply. */
	{
		int lfd, cfd, afd;
		sockaddr_in la, ca;

		lfd = socket(AF_INET, SOCK_STREAM, 0);
		rcv4(&la, "127.0.0.1", 39011);
		bind(lfd, (struct sockaddr*)&la, sizeof(la));
		listen(lfd, 4);

		cfd = socket(AF_INET, SOCK_STREAM, 0);
		rcv4(&ca, "127.0.0.1", 39011);
		connect(cfd, (sockaddr_*)&ca, sizeof(ca));
		afd = accept(lfd, NULL, NULL);

		memset(buf, 0, sizeof(buf));
		t0 = now_us();
		rc = send_buf(cfd, "last", 4, 0);
		rec("shutdown.tcp.send-before", rc == 4, rc, now_us() - t0);

		t0 = now_us();
		rc = shutdown(cfd, SHUT_WR);
		rec("shutdown.tcp.wr", rc == 0, rc, now_us() - t0);

		t0 = now_us();
		rc = send_buf(cfd, "no", 2, 0);
		rec("shutdown.tcp.send-epipe", rc == -EPIPE, rc, now_us() - t0);

		memset(buf, 0, sizeof(buf));
		t0 = now_us();
		rc = recv_buf(afd, buf, sizeof(buf), 0);
		rec("shutdown.tcp.recv-data",
			rc == 4 && memcmp(buf, "last", 4) == 0, rc, now_us() - t0);

		t0 = now_us();
		rc = recv_buf(afd, buf, sizeof(buf), 0);
		rec("shutdown.tcp.recv-eof", rc == 0, rc, now_us() - t0);

		memset(buf, 0, sizeof(buf));
		t0 = now_us();
		rc = send_buf(afd, "reply", 5, 0);
		rec("shutdown.tcp.reverse-send", rc == 5, rc, now_us() - t0);

		t0 = now_us();
		rc = recv_buf(cfd, buf, sizeof(buf), 0);
		rec("shutdown.tcp.reverse-recv",
			rc == 5 && memcmp(buf, "reply", 5) == 0, rc,
			now_us() - t0);

		_KeCloseFile(afd);
		_KeCloseFile(cfd);
		_KeCloseFile(lfd);
	}

	/* UDP: connected datagrams accept half-close flags, unconnected
	 * sockets report ENOTCONN. */
	{
		int u1, u2;
		sockaddr_in a1, a2;

		u1 = socket(AF_INET, SOCK_DGRAM, 0);
		t0 = now_us();
		rc = shutdown(u1, SHUT_WR);
		rec("shutdown.udp.not-connected", rc == -ENOTCONN, rc,
			now_us() - t0);

		u2 = socket(AF_INET, SOCK_DGRAM, 0);
		rcv4(&a1, "127.0.0.1", 39012);
		rcv4(&a2, "127.0.0.1", 39013);
		bind(u1, (struct sockaddr*)&a1, sizeof(a1));
		bind(u2, (struct sockaddr*)&a2, sizeof(a2));

		t0 = now_us();
		rc = connect(u1, (sockaddr_*)&a2, sizeof(a2));
		rec("shutdown.udp.connect", rc == 0, rc, now_us() - t0);

		t0 = now_us();
		rc = shutdown(u1, SHUT_WR);
		rec("shutdown.udp.wr", rc == 0, rc, now_us() - t0);

		t0 = now_us();
		rc = send_buf(u1, "x", 1, 0);
		rec("shutdown.udp.send-epipe", rc == -EPIPE, rc, now_us() - t0);

		connect(u2, (sockaddr_*)&a1, sizeof(a1));
		t0 = now_us();
		rc = shutdown(u2, SHUT_RD);
		rec("shutdown.udp.rd", rc == 0, rc, now_us() - t0);

		t0 = now_us();
		rc = recv_buf(u2, buf, sizeof(buf), 0);
		rec("shutdown.udp.recv-eof", rc == 0, rc, now_us() - t0);

		_KeCloseFile(u1);
		_KeCloseFile(u2);
	}
}

/* --- poll readiness and timeout accuracy -------------------------------- */
/* --- address inspection: getsockname/getpeername ----------------------- */
static void test_names(void) {
	uint64_t t0;
	int rc;
	int lfd = -1, cfd = -1, afd = -1;
	int ufd = -1, sv[2] = { -1, -1 };
	sockaddr_in la, peer, local;
	sockaddr_un un, unpeer;
	socklen_t len;
	uint16_t cport = 0;

	lfd = socket(AF_INET, SOCK_STREAM, 0);
	rcv4(&la, "127.0.0.1", 39014);
	t0 = now_us();
	rc = bind(lfd, (struct sockaddr*)&la, sizeof(la));
	rec("name.tcp.bind", rc == 0, rc, now_us() - t0);

	t0 = now_us();
	rc = listen(lfd, 4);
	rec("name.tcp.listen", rc == 0, rc, now_us() - t0);

	{
		memset(&local, 0, sizeof(local));
		len = sizeof(local);
		t0 = now_us();
		rc = getsockname(lfd, (sockaddr_*)&local, &len);
		rec("name.tcp.listen.getsockname",
			rc == 0 && local.sin_family == AF_INET &&
				ntohs(local.sin_port) == 39014 &&
				local.sin_addr.s_addr == inet_addr("127.0.0.1") &&
				len == sizeof(sockaddr_in),
			rc, now_us() - t0);
	}

	cfd = socket(AF_INET, SOCK_STREAM, 0);
	rcv4(&la, "127.0.0.1", 39014);
	t0 = now_us();
	rc = connect(cfd, (sockaddr_*)&la, sizeof(la));
	rec("name.tcp.connect", rc == 0, rc, now_us() - t0);

	{
		memset(&peer, 0, sizeof(peer));
		len = sizeof(peer);
		t0 = now_us();
		rc = getpeername(cfd, (sockaddr_*)&peer, &len);
		rec("name.tcp.peer",
			rc == 0 && peer.sin_family == AF_INET &&
				ntohs(peer.sin_port) == 39014 &&
				peer.sin_addr.s_addr == inet_addr("127.0.0.1") &&
				len == sizeof(sockaddr_in),
			rc, now_us() - t0);

		memset(&local, 0, sizeof(local));
		len = sizeof(local);
		t0 = now_us();
		rc = getsockname(cfd, (sockaddr_*)&local, &len);
		rec("name.tcp.local",
			rc == 0 && local.sin_family == AF_INET &&
				ntohs(local.sin_port) != 0 &&
				len == sizeof(sockaddr_in),
			rc, now_us() - t0);
		cport = (uint16_t)ntohs(local.sin_port);
	}

	{
		int bfd = socket(AF_INET, SOCK_STREAM, 0);
		sockaddr_in ba, bl;

		rcv4(&ba, "127.0.0.2", 39017);
		t0 = now_us();
		rc = bind(bfd, (struct sockaddr*)&ba, sizeof(ba));
		rec("name.tcp.bind.nonprimary", rc == 0, rc, now_us() - t0);

		memset(&bl, 0, sizeof(bl));
		len = sizeof(bl);
		t0 = now_us();
		rc = getsockname(bfd, (sockaddr_*)&bl, &len);
		rec("name.tcp.nonprimary.local",
			rc == 0 && bl.sin_family == AF_INET &&
				bl.sin_addr.s_addr == inet_addr("127.0.0.2") &&
				ntohs(bl.sin_port) == 39017 &&
				len == sizeof(sockaddr_in),
			rc, now_us() - t0);
		_KeCloseFile(bfd);
	}

	{
		int v6fd = socket(AF_INET6, SOCK_STREAM, 0);
		sockaddr_in6 a6;
		char small6[16];

		rcv4_6(&a6, "::1", 39018);
		t0 = now_us();
		rc = bind(v6fd, (struct sockaddr*)&a6, sizeof(a6));
		rec("name.tcp.v6.bind", rc == 0, rc, now_us() - t0);
		t0 = now_us();
		rc = listen(v6fd, 2);
		rec("name.tcp.v6.listen", rc == 0, rc, now_us() - t0);

		memset(small6, 0, sizeof(small6));
		len = sizeof(small6);
		t0 = now_us();
		rc = getsockname(v6fd, (sockaddr_*)small6, &len);
		rec("name.tcp.v6.truncate",
			rc == 0 && len == sizeof(sockaddr_in6) &&
				*(unsigned short*)small6 == AF_INET6,
			rc, now_us() - t0);
		_KeCloseFile(v6fd);
	}

	{
		char small[4];
		len = sizeof(small);
		memset(small, 0, sizeof(small));
		t0 = now_us();
		rc = getsockname(cfd, (sockaddr_*)small, &len);
		rec("name.tcp.truncate",
			rc == 0 && len == sizeof(sockaddr_in) &&
				*(unsigned short*)small == AF_INET,
			rc, now_us() - t0);
	}

	memset(&peer, 0, sizeof(peer));
	len = sizeof(peer);
	t0 = now_us();
	afd = accept(lfd, (struct sockaddr*)&peer, &len);
	rec("name.tcp.accept", afd >= 0, afd, now_us() - t0);

	{
		memset(&local, 0, sizeof(local));
		len = sizeof(local);
		t0 = now_us();
		rc = getsockname(afd, (sockaddr_*)&local, &len);
		rec("name.tcp.accepted.local",
			rc == 0 && ntohs(local.sin_port) == 39014, rc,
			now_us() - t0);
	}

	{
		memset(&peer, 0, sizeof(peer));
		len = sizeof(peer);
		t0 = now_us();
		rc = getpeername(afd, (sockaddr_*)&peer, &len);
		rec("name.tcp.accepted.peer",
			rc == 0 && peer.sin_family == AF_INET &&
				cport != 0 && ntohs(peer.sin_port) == cport &&
				peer.sin_addr.s_addr == inet_addr("127.0.0.1") &&
				len == sizeof(sockaddr_in),
			rc, now_us() - t0);
	}

	{
		int nfd = socket(AF_INET, SOCK_STREAM, 0);
		memset(&peer, 0, sizeof(peer));
		len = sizeof(peer);
		t0 = now_us();
		rc = getpeername(nfd, (sockaddr_*)&peer, &len);
		rec("name.tcp.unconnected-econn",
			rc == -ENOTCONN, rc, now_us() - t0);
		_KeCloseFile(nfd);
	}

	t0 = now_us();
	rc = getsockname(9999, (sockaddr_*)&peer, &len);
	rec("name.bad-fd", rc == -EBADF, rc, now_us() - t0);

	{
		t0 = now_us();
		rc = getsockname(cfd, (sockaddr_*)&peer, NULL);
		rec("name.null-len", rc == -EFAULT, rc, now_us() - t0);
	}

	/* UDP: bound local name and connected peer name. */
	{
		int urfd = socket(AF_INET, SOCK_DGRAM, 0);
		int usfd = socket(AF_INET, SOCK_DGRAM, 0);
		sockaddr_in ra;

		rcv4(&ra, "127.0.0.1", 39015);
		t0 = now_us();
		rc = bind(urfd, (struct sockaddr*)&ra, sizeof(ra));
		rec("name.udp.bind", rc == 0, rc, now_us() - t0);

		memset(&local, 0, sizeof(local));
		len = sizeof(local);
		t0 = now_us();
		rc = getsockname(urfd, (sockaddr_*)&local, &len);
		rec("name.udp.getsockname",
			rc == 0 && local.sin_family == AF_INET &&
				ntohs(local.sin_port) == 39015 &&
				len == sizeof(sockaddr_in),
			rc, now_us() - t0);

		memset(&peer, 0, sizeof(peer));
		len = sizeof(peer);
		t0 = now_us();
		rc = getpeername(urfd, (sockaddr_*)&peer, &len);
		rec("name.udp.unconnected-econn",
			rc == -ENOTCONN, rc, now_us() - t0);

		t0 = now_us();
		rc = connect(usfd, (sockaddr_*)&ra, sizeof(ra));
		rec("name.udp.connect", rc == 0, rc, now_us() - t0);

		memset(&peer, 0, sizeof(peer));
		len = sizeof(peer);
		t0 = now_us();
		rc = getpeername(usfd, (sockaddr_*)&peer, &len);
		rec("name.udp.peer",
			rc == 0 && peer.sin_family == AF_INET &&
				ntohs(peer.sin_port) == 39015 &&
				peer.sin_addr.s_addr == inet_addr("127.0.0.1") &&
				len == sizeof(sockaddr_in),
			rc, now_us() - t0);

		{
			struct {
				sockaddr_in in;
				char pad[112];
			} padded;
			int pfd = socket(AF_INET, SOCK_DGRAM, 0);

			memset(&padded, 0, sizeof(padded));
			rcv4(&padded.in, "127.0.0.1", 39020);
			t0 = now_us();
			rc = bind(pfd, (struct sockaddr*)&padded, sizeof(padded));
			rec("name.udp.bind.padded", rc == 0, rc, now_us() - t0);

			memset(&local, 0, sizeof(local));
			len = sizeof(local);
			t0 = now_us();
			rc = getsockname(pfd, (sockaddr_*)&local, &len);
			rec("name.udp.padded.len",
				rc == 0 && local.sin_family == AF_INET &&
					ntohs(local.sin_port) == 39020 &&
					len == sizeof(sockaddr_in),
				rc, now_us() - t0);
			_KeCloseFile(pfd);
		}

		_KeCloseFile(usfd);
		_KeCloseFile(urfd);
	}

	/* AF_UNIX: bound path and unnamed socketpair peer. */
	ufd = socket(AF_UNIX, SOCK_STREAM, 0);
	memset(&un, 0, sizeof(un));
	un.sun_family = AF_UNIX;
	strcpy(un.sun_path, "/tmp/netsystest.names");
	t0 = now_us();
	rc = bind(ufd, (struct sockaddr*)&un, sizeof(un));
	rec("name.unix.bind", rc == 0, rc, now_us() - t0);

	{
		memset(&un, 0, sizeof(un));
		len = sizeof(un);
		t0 = now_us();
		rc = getsockname(ufd, (sockaddr_*)&un, &len);
		rec("name.unix.getsockname",
			rc == 0 && un.sun_family == AF_UNIX &&
				strcmp(un.sun_path, "/tmp/netsystest.names") == 0 &&
				len == sizeof(sockaddr_un),
			rc, now_us() - t0);
	}

	{
		char small[4];
		len = sizeof(small);
		memset(small, 0, sizeof(small));
		t0 = now_us();
		rc = getsockname(ufd, (sockaddr_*)small, &len);
		rec("name.unix.truncate",
			rc == 0 && len == sizeof(sockaddr_un) &&
				*(unsigned short*)small == AF_UNIX,
			rc, now_us() - t0);
	}

	{
		memset(&unpeer, 0, sizeof(unpeer));
		len = sizeof(unpeer);
		t0 = now_us();
		rc = getpeername(ufd, (sockaddr_*)&unpeer, &len);
		rec("name.unix.unconnected-econn",
			rc == -ENOTCONN, rc, now_us() - t0);
	}

	t0 = now_us();
	rc = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
	rec("name.unix.pair.create", rc == 0, rc, now_us() - t0);
	if (rc == 0) {
		memset(&unpeer, 0, sizeof(unpeer));
		len = sizeof(unpeer);
		t0 = now_us();
		rc = getpeername(sv[0], (sockaddr_*)&unpeer, &len);
		rec("name.unix.pair.peer",
			rc == 0 && unpeer.sun_family == AF_UNIX &&
				unpeer.sun_path[0] == '\0' &&
				len == sizeof(sockaddr_un),
			rc, now_us() - t0);
		_KeCloseFile(sv[0]);
		_KeCloseFile(sv[1]);
	}

	/* A bound client must be reported by accept()/getpeername(), not the
	 * accepted child's own (empty) path. */
	{
		sockaddr_un lsn, cb, ap;
		int cli, acc;

		memset(&lsn, 0, sizeof(lsn));
		lsn.sun_family = AF_UNIX;
		strcpy(lsn.sun_path, "/tmp/netsystest.names");
		t0 = now_us();
		rc = listen(ufd, 2);
		rec("name.unix.listen", rc == 0, rc, now_us() - t0);

		cli = socket(AF_UNIX, SOCK_STREAM, 0);
		memset(&cb, 0, sizeof(cb));
		cb.sun_family = AF_UNIX;
		strcpy(cb.sun_path, "/tmp/netsystest.names.client");
		t0 = now_us();
		rc = bind(cli, (struct sockaddr*)&cb, sizeof(cb));
		rec("name.unix.client.bind", rc == 0, rc, now_us() - t0);

		t0 = now_us();
		rc = connect(cli, (sockaddr_*)&lsn, sizeof(lsn));
		rec("name.unix.client.connect", rc == 0, rc, now_us() - t0);

		memset(&ap, 0xFF, sizeof(ap));
		len = sizeof(ap);
		t0 = now_us();
		acc = accept(ufd, (struct sockaddr*)&ap, &len);
		rec("name.unix.accept.peer",
			acc >= 0 && len == sizeof(sockaddr_un) &&
				ap.sun_family == AF_UNIX &&
				strcmp(ap.sun_path,
					"/tmp/netsystest.names.client") == 0,
			acc, now_us() - t0);

		if (acc >= 0) {
			memset(&ap, 0, sizeof(ap));
			len = sizeof(ap);
			t0 = now_us();
			rc = getpeername(acc, (sockaddr_*)&ap, &len);
			rec("name.unix.accepted.peer",
				rc == 0 && len == sizeof(sockaddr_un) &&
					strcmp(ap.sun_path,
						"/tmp/netsystest.names.client") == 0,
				rc, now_us() - t0);
			_KeCloseFile(acc);
		}
		_KeCloseFile(cli);
	}

	_KeCloseFile(ufd);
	if (afd >= 0)
		_KeCloseFile(afd);
	_KeCloseFile(cfd);
	_KeCloseFile(lfd);
}

/* --- sendmmsg/recvmmsg and batch datagram transfer --------------------- */
static void test_mmsg(void) {
	static const char* payloads[3] = { "m0", "m1", "m2" };
	uint64_t t0;
	uint64_t recv_us = 0;
	int rc;
	int rfd, sfd;
	sockaddr_in ra, src;
	socklen_t len;
	mmsghdr mv[3];
	mmsghdr rv[3];
	iovec siov[3];
	iovec riov[3];
	char rbuf[3][8];
	int seen = 0;
	int i;

	rfd = socket(AF_INET, SOCK_DGRAM, 0);
	rcv4(&ra, "127.0.0.1", 39016);
	t0 = now_us();
	rc = bind(rfd, (struct sockaddr*)&ra, sizeof(ra));
	rec("mmsg.recv.bind", rc == 0, rc, now_us() - t0);

	sfd = socket(AF_INET, SOCK_DGRAM, 0);
	memset(mv, 0, sizeof(mv));
	memset(siov, 0, sizeof(siov));
	for (i = 0; i < 3; i++) {
		siov[i].iov_base = (void*)payloads[i];
		siov[i].iov_len = 2;
		mv[i].msg_hdr.msg_name = &ra;
		mv[i].msg_hdr.msg_namelen = sizeof(ra);
		mv[i].msg_hdr.msg_iov = &siov[i];
		mv[i].msg_hdr.msg_iovlen = 1;
	}

	t0 = now_us();
	rc = (int)sendmmsg(sfd, mv, 3, 0);
	rec("mmsg.send.batch",
		rc == 3 && mv[0].msg_len == 2 && mv[1].msg_len == 2 &&
			mv[2].msg_len == 2,
		rc, now_us() - t0);

	memset(rv, 0, sizeof(rv));
	memset(riov, 0, sizeof(riov));
	memset(rbuf, 0, sizeof(rbuf));
	memset(&src, 0, sizeof(src));
	for (i = 0; i < 3; i++) {
		riov[i].iov_base = rbuf[i];
		riov[i].iov_len = sizeof(rbuf[i]);
		rv[i].msg_hdr.msg_name = &src;
		rv[i].msg_hdr.msg_namelen = sizeof(src);
		rv[i].msg_hdr.msg_iov = &riov[i];
		rv[i].msg_hdr.msg_iovlen = 1;
	}

	{
		struct timespec ts;

		ts.tv_sec = 0;
		ts.tv_nsec = 200000000;
		t0 = now_us();
		rc = (int)recvmmsg(rfd, rv, 3, 0, &ts);
		recv_us = now_us() - t0;
	}
	rec("mmsg.recv.batch", rc == 3, rc, recv_us);
	for (i = 0; i < 3 && rc == 3; i++) {
		if (rv[i].msg_len != 2)
			continue;
		if (memcmp(rbuf[i], "m0", 2) == 0)
			seen |= 1;
		else if (memcmp(rbuf[i], "m1", 2) == 0)
			seen |= 2;
		else if (memcmp(rbuf[i], "m2", 2) == 0)
			seen |= 4;
	}
	rec("mmsg.recv.payloads", seen == 7, seen, recv_us);

	{
		struct timespec ts;

		ts.tv_sec = 0;
		ts.tv_nsec = 60000000;
		t0 = now_us();
		rc = (int)recvmmsg(rfd, rv, 1, 0, &ts);
		rec("mmsg.recv.timeout",
			rc == -ETIMEDOUT && now_us() - t0 >= 40000,
			rc, now_us() - t0);
	}

	{
		struct timespec ts;

		ts.tv_sec = 1;
		ts.tv_nsec = 0;
		rc = (int)sendmmsg(sfd, mv, 2, 0);
		memset(rbuf, 0, sizeof(rbuf));
		t0 = now_us();
		rc = (int)recvmmsg(rfd, rv, 3, MSG_WAITFORONE, &ts);
		rec("mmsg.recv.waitforone",
			rc == 2 && now_us() - t0 < 500000,
			rc, now_us() - t0);
	}

	{
		rc = (int)sendmmsg(sfd, mv, 3, 0);
		memset(rbuf, 0, sizeof(rbuf));
		t0 = now_us();
		rc = (int)recvmmsg(rfd, rv, 3, 0, NULL);
		rec("mmsg.recv.blocking",
			rc == 3 && now_us() - t0 < 1000000,
			rc, now_us() - t0);
	}

	{
		t0 = now_us();
		rc = (int)recvmmsg(rfd, rv, 1, MSG_DONTWAIT, NULL);
		rec("mmsg.recv.dontwait",
			rc == -EAGAIN && now_us() - t0 < 100000,
			rc, now_us() - t0);
	}

	{
		char small[8];
		sockaddr_in shortaddr;
		socklen_t slen = 8;
		int sret;

		memset(&shortaddr, 0xFF, sizeof(shortaddr));
		sret = (int)sendto(sfd, "short", 5, 0, (struct sockaddr*)&ra,
			sizeof(ra));
		t0 = now_us();
		rc = (int)recvfrom(rfd, small, sizeof(small), 0,
			(struct sockaddr*)&shortaddr, &slen);
		rec("mmsg.recvfrom.short-addr",
			sret == 5 && rc == 5 && slen == sizeof(sockaddr_in) &&
				shortaddr.sin_family == AF_INET &&
				ntohs(shortaddr.sin_port) != 0,
			rc, now_us() - t0);
	}

	{
		struct timespec ts = { 0, 0 };

		t0 = now_us();
		rc = (int)sendmmsg(sfd, mv, 0, 0);
		rec("mmsg.send.vlen0", rc == -EINVAL, rc, now_us() - t0);

		rc = (int)recvmmsg(rfd, rv, 0, 0, &ts);
		rec("mmsg.recv.vlen0", rc == -EINVAL, rc, 0);

		t0 = now_us();
		rc = (int)sendmmsg(sfd, (mmsghdr*)1, 1, 0);
		rec("mmsg.send.badvec", rc == -EFAULT, rc, now_us() - t0);

		rc = (int)recvmmsg(rfd, (mmsghdr*)1, 1, 0, &ts);
		rec("mmsg.recv.badvec", rc == -EFAULT, rc, 0);

		{
			struct timespec bad = { -1, 0 };

			t0 = now_us();
			rc = (int)recvmmsg(rfd, rv, 1, 0, &bad);
			rec("mmsg.recv.neg-timeout", rc == -EINVAL, rc,
				now_us() - t0);

			bad.tv_sec = 0;
			bad.tv_nsec = 1000000000;
			t0 = now_us();
			rc = (int)recvmmsg(rfd, rv, 1, 0, &bad);
			rec("mmsg.recv.bad-nsec", rc == -EINVAL, rc,
				now_us() - t0);
		}
	}

	{
		char buf[4];
		len = sizeof(src);
		t0 = now_us();
		rc = (int)sendto(sfd, NULL, 4, 0, (struct sockaddr*)&ra, sizeof(ra));
		rec("mmsg.sendto.null-buf", rc == -EFAULT, rc, now_us() - t0);

		t0 = now_us();
		rc = (int)sendto(sfd, "x", 1, 0, (struct sockaddr*)1, sizeof(ra));
		rec("mmsg.sendto.bad-addr", rc == -EFAULT, rc, now_us() - t0);

		t0 = now_us();
		rc = (int)recvfrom(rfd, NULL, 4, 0, NULL, NULL);
		rec("mmsg.recvfrom.null-buf", rc == -EFAULT, rc, now_us() - t0);

		t0 = now_us();
		rc = (int)recvfrom(rfd, buf, sizeof(buf), 0, (struct sockaddr*)1,
			&len);
		rec("mmsg.recvfrom.bad-addr", rc == -EFAULT, rc, now_us() - t0);
	}

	_KeCloseFile(sfd);
	_KeCloseFile(rfd);
}

/* --- interface enumeration --------------------------------------------- */
static void test_ifaddrs(void) {
	uint64_t t0;
	uint64_t list_us = 0;
	int rc;
	unsigned int idx;
	struct ifaddrs* list = NULL;
	struct ifaddrs* ifa;
	int found4 = 0, found6 = 0, flags_ok = 0;

	t0 = now_us();
	idx = if_nametoindex("lo");
	rec("ifaddrs.lo.index", idx > 0, (int)idx, now_us() - t0);

	/* A name that fits IFNAMSIZ but matches no adapter is ENXIO.  The old
	 * "netsystest-missing" spelling was longer than AU_IFNAME_MAX, so the
	 * kernel correctly returned EINVAL and the test blamed the wrong path. */
	t0 = now_us();
	errno = 0;
	idx = if_nametoindex("nosuchif");
	rec("ifaddrs.missing.index", idx == 0 && errno == ENXIO,
		errno, now_us() - t0);

	/* An over-long name never reaches the hash lookup; it is EINVAL. */
	t0 = now_us();
	errno = 0;
	idx = if_nametoindex("this-name-is-too-long-for-if");
	rec("ifaddrs.longname.index", idx == 0 && errno == EINVAL,
		errno, now_us() - t0);

	t0 = now_us();
	rc = getifaddrs(&list);
	list_us = now_us() - t0;
	rec("ifaddrs.list", rc == 0 && list != NULL, rc, list_us);

	for (ifa = list; ifa; ifa = ifa->ifa_next) {
		if (strcmp(ifa->ifa_name, "lo") != 0)
			continue;
		if ((ifa->ifa_flags & IFF_LOOPBACK) &&
			(ifa->ifa_flags & IFF_UP))
			flags_ok = 1;
		if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
			sockaddr_in* in = (sockaddr_in*)ifa->ifa_addr;
			if (in->sin_addr.s_addr == inet_addr("127.0.0.1"))
				found4 = 1;
		}
		if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET6) {
			sockaddr_in6* in6 = (sockaddr_in6*)ifa->ifa_addr;
			int all_zero = 1;
			int j;

			for (j = 0; j < 15; j++) {
				if (in6->sin6_addr.s6_addr[j])
					all_zero = 0;
			}
			if (all_zero && in6->sin6_addr.s6_addr[15] == 1)
				found6 = 1;
		}
	}
	rec("ifaddrs.lo.ipv4", found4, found4, list_us);
	rec("ifaddrs.lo.ipv6", found6, found6, list_us);
	rec("ifaddrs.lo.flags", flags_ok, flags_ok, list_us);
	freeifaddrs(list);

	t0 = now_us();
	rc = getifaddrs(NULL);
	rec("ifaddrs.null", rc == -1 && errno == EFAULT, errno,
		now_us() - t0);
}

/* --- real network: DNS, public TCP and HTTP ----------------------------- */

static int ipv4_is_public(uint32_t addr) {
	uint32_t host = ntohl(addr);
	uint8_t a = (uint8_t)(host >> 24);
	uint8_t b = (uint8_t)(host >> 16);

	if (host == 0 || a == 0 || a == 10 || a == 127)
		return 0;
	if (a == 172 && b >= 16 && b <= 31)
		return 0;
	if (a == 192 && b == 168)
		return 0;
	if (a == 169 && b == 254)
		return 0;
	return 1;
}

static int real_network_ready(void) {
	struct ifaddrs* list = NULL;
	struct ifaddrs* ifa;
	int ready = 0;

	if (getifaddrs(&list) != 0)
		return 0;
	for (ifa = list; ifa; ifa = ifa->ifa_next) {
		if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
			continue;
		if (ifa->ifa_flags & IFF_LOOPBACK)
			continue;
		if (!(ifa->ifa_flags & IFF_UP))
			continue;
		ready = 1;
		break;
	}
	freeifaddrs(list);
	return ready;
}

static int wait_fd(int fd, short events, uint64_t deadline) {
	struct pollfd p;
	uint64_t now = now_us();
	int wait_ms;

	if (now >= deadline)
		return 0;
	wait_ms = (int)((deadline - now) / 1000);
	if (wait_ms <= 0)
		wait_ms = 1;
	p.fd = fd;
	p.events = events;
	p.revents = 0;
	return poll(&p, 1, wait_ms) == 1 && (p.revents & events);
}

static int send_all_deadline(int fd, const char* buf, size_t len,
	uint64_t deadline) {
	size_t off = 0;

	while (off < len) {
		int n = send_buf(fd, buf + off, len - off, 0);

		if (n > 0) {
			off += (size_t)n;
			continue;
		}
		if (n == -EAGAIN && wait_fd(fd, POLLOUT, deadline))
			continue;
		return -1;
	}
	return 0;
}

static int recv_until_marker(int fd, char* buf, size_t cap,
	uint64_t deadline, const char* marker) {
	size_t total = 0;

	buf[0] = 0;
	while (total + 1 < cap) {
		int n = recv_buf(fd, buf + total, cap - 1 - total, 0);

		if (n > 0) {
			total += (size_t)n;
			buf[total] = 0;
			if (marker && strstr(buf, marker))
				break;
			continue;
		}
		if (n == 0)
			break;
		if (n == -EAGAIN && wait_fd(fd, POLLIN, deadline))
			continue;
		break;
	}
	return (int)total;
}

static void test_real_network(void) {
	uint64_t t0;
	uint64_t dns_us = 0;
	uint64_t cache_us = 0;
	uint64_t deadline;
	addrinfo hints;
	addrinfo* res = NULL;
	addrinfo* rp;
	sockaddr_in target;
	uint32_t target_addr = 0;
	int have_target = 0;
	int rc;

	if (!g_real_enabled) {
		skip("real.network", "disabled by --no-real");
		return;
	}
	if (!real_network_ready()) {
		skip("real.network", "no non-loopback IPv4");
		return;
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	t0 = now_us();
	rc = getaddrinfo("example.com", "80", &hints, &res);
	dns_us = now_us() - t0;
	rec("real.dns.example.com",
		rc == 0 && res != NULL && res->ai_family == AF_INET &&
			dns_us <= 5000000,
		rc, dns_us);

	if (rc == 0 && res) {
		for (rp = res; rp; rp = rp->ai_next) {
			if (rp->ai_family == AF_INET && rp->ai_addr) {
				target = *(sockaddr_in*)rp->ai_addr;
				target_addr = target.sin_addr.s_addr;
				have_target = 1;
				break;
			}
		}
	}
	if (have_target) {
		char ip[32];
		const char* ipstr =
			inet_ntop(AF_INET, &target.sin_addr, ip, sizeof(ip));

		_KePrint("[netsystest] info real.dns.example.com=%s\r\n",
			ipstr ? ipstr : "?");
	}
	rec("real.dns.public-address",
		have_target && ipv4_is_public(target_addr), (int)target_addr,
		dns_us);
	if (res) {
		freeaddrinfo(res);
		res = NULL;
	}

	/* A second lookup must come from the per-process DNS cache.  This is a
	 * real optimisation check: a cache miss would pay another UDP round trip
	 * and would not finish inside the 50 ms budget. */
	if (!have_target) {
		skip("real.dns.cache-hit", "no resolved address");
	} else {
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		t0 = now_us();
		rc = getaddrinfo("example.com", "80", &hints, &res);
		cache_us = now_us() - t0;
		{
			int same = 0;

			for (rp = res; rp; rp = rp->ai_next) {
				if (rp->ai_family == AF_INET && rp->ai_addr &&
					((sockaddr_in*)rp->ai_addr)->sin_addr.s_addr ==
						target_addr) {
					same = 1;
					break;
				}
			}
			rec("real.dns.cache-hit",
				rc == 0 && res != NULL && same && cache_us <= 50000,
				rc, cache_us);
		}
		if (res) {
			freeaddrinfo(res);
			res = NULL;
		}
	}

	if (!have_target) {
		rec("real.tcp.connect", 0, 0, 0);
		rec("real.http.status", 0, 0, 0);
		rec("real.http.body", 0, 0, 0);
		return;
	}

	/* Reverse DNS through the PTR path.  A resolver may legitimately
	 * refuse PTR queries, so a negative answer is a skip rather than a
	 * failure; a malformed reply or a crash would still be caught. */
	{
		sockaddr_in ptr;
		char host[NI_MAXHOST];

		memset(&ptr, 0, sizeof(ptr));
		ptr.sin_family = AF_INET;
		ptr.sin_port = htons(53);
		inet_pton(AF_INET, "1.1.1.1", &ptr.sin_addr);
		t0 = now_us();
		rc = getnameinfo((struct sockaddr*)&ptr, sizeof(ptr), host,
						 sizeof(host), NULL, 0, NI_NAMEREQD);
		if (rc == 0)
			rec("real.dns.ptr", host[0] != '\0', rc, now_us() - t0);
		else
			skip("real.dns.ptr", gai_strerror(rc));
	}

	/* Numeric connect to the address DNS returned: no name lookup remains
	 * on this path, so a failure isolates route/ARP/TCP from the resolver. */
	{
		int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
		int err = -1;
		socklen_t errlen = sizeof(err);
		int connected = 0;

		if (fd < 0) {
			rec("real.tcp.connect", 0, fd, 0);
			rec("real.http.status", 0, 0, 0);
			rec("real.http.body", 0, 0, 0);
			return;
		}

		t0 = now_us();
		rc = connect(fd, (sockaddr_*)&target, sizeof(target));
		if (rc == 0) {
			connected = 1;
		} else if (rc == -EINPROGRESS) {
			struct pollfd p;

			p.fd = fd;
			p.events = POLLOUT;
			p.revents = 0;
			if (poll(&p, 1, 5000) == 1) {
				getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen);
				connected = (err == 0);
			}
		}
		rec("real.tcp.connect", connected, err, now_us() - t0);

		if (!connected) {
			rec("real.http.status", 0, err, 0);
			rec("real.http.body", 0, 0, 0);
			_KeCloseFile(fd);
			return;
		}

		{
			char req[160];
			char buf[16384];
			int reqlen;
			int total;
			int status = -1;
			int sent_ok;

			reqlen = snprintf(req, sizeof(req),
				"GET / HTTP/1.1\r\n"
				"Host: example.com\r\n"
				"User-Agent: netsystest/1.0\r\n"
				"Accept: */*\r\n"
				"Connection: close\r\n\r\n");
			deadline = now_us() + 5000000;
			sent_ok = reqlen > 0 &&
				send_all_deadline(fd, req, (size_t)reqlen, deadline) == 0;
			t0 = now_us();
			total = sent_ok ?
				recv_until_marker(fd, buf, sizeof(buf), deadline,
					"Example Domain") : 0;
			if (total > 7 && memcmp(buf, "HTTP/1.", 7) == 0)
				status = atoi(buf + 9);
			rec("real.http.status", sent_ok && status == 200, status,
				now_us() - t0);
			rec("real.http.body",
				sent_ok && total > 0 && strstr(buf, "Example Domain") != NULL,
				total, now_us() - t0);
		}

		_KeCloseFile(fd);
	}
}

/* --- HTTPS: trust store, TLS session cache, content decoding ------------ */

/* The image ships the Mozilla bundle at this path. A missing file is a
 * deployment failure, not a reason to fall back silently in the test. */
static int ca_file_ok(void) {
	int fd;
	char buf[2048];
	size_t got;

	fd = _KeOpenFile((char*)"/etc/ssl/certs/cacert.pem", FILE_OPEN_READ_ONLY);
	if (fd < 0)
		return 0;
	got = _KeReadFile(fd, buf, sizeof(buf) - 1);
	_KeCloseFile(fd);
	if (got == 0 || got >= sizeof(buf))
		return 0;
	buf[got] = 0;
	return strstr(buf, "BEGIN CERTIFICATE") != NULL;
}

static void test_https(void) {
	xe_http_response r;
	const char* src;
	int full0 = 0, resumed0 = 0;
	int full1 = 0, resumed1 = 0;
	uint64_t t0;
	int rc;

	if (!g_real_enabled) {
		skip("https.network", "disabled by --no-real");
		return;
	}
	if (!real_network_ready()) {
		skip("https.network", "no non-loopback IPv4");
		return;
	}

	rec("https.ca.file", ca_file_ok(), 0, 0);

	memset(&r, 0, sizeof(r));
	t0 = now_us();
	rc = xe_http_get("https://example.com/", "GET", "netsystest/1.0", &r, 512 * 1024);
	rec("https.tls.example.com", rc == 0 && r.status == 200 && r.tls == 1, rc,
		now_us() - t0);
	rec("https.body.example.com",
		rc == 0 && r.body && strstr(r.body, "Example Domain") != NULL,
		(int)r.body_len, 0);
	src = xe_http_ca_source();
	rec("https.ca.source",
		src && strncmp(src, "file:", 5) == 0, 0, 0);
	xe_http_response_free(&r);

	xe_http_tls_stats(&full0, &resumed0);
	memset(&r, 0, sizeof(r));
	rc = xe_http_get("https://example.com/", "GET", "netsystest/1.0", &r, 512 * 1024);
	xe_http_tls_stats(&full1, &resumed1);
	rec("https.tls.resume", rc == 0 && resumed1 > resumed0,
		resumed1 - resumed0, 0);
	xe_http_response_free(&r);

	/* httpbin forces one encoding per endpoint. The echoed JSON proves the
	 * body was decoded before it reached the caller. */
	{
		struct {
			const char* url;
			const char* needle;
			const char* name;
		} cases[] = {
			{ "https://httpbin.org/gzip", "\"gzipped\": true", "https.gzip" },
			{ "https://httpbin.org/deflate", "\"deflated\": true", "https.deflate" },
			{ "https://httpbin.org/brotli", "\"brotli\": true", "https.brotli" }
		};
		unsigned i;

		for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
			memset(&r, 0, sizeof(r));
			t0 = now_us();
			rc = xe_http_get(cases[i].url, "GET", "netsystest/1.0", &r, 256 * 1024);
			if (rc == -1 && !r.body &&
				strcmp(r.error, "Content decoding failed") != 0 &&
				strcmp(r.error, "Body read failed") != 0) {
				/* The endpoint, not the decoder, is unreachable. */
				skip(cases[i].name, r.error);
			} else {
				rec(cases[i].name,
					rc == 0 && r.status == 200 && r.body &&
						strstr(r.body, cases[i].needle) != NULL,
					rc, now_us() - t0);
			}
			xe_http_response_free(&r);
		}
	}
}

/* --- performance and optimisation budgets ------------------------------- */

/* Sustained loopback TCP: 32 x 32 KiB = 1 MiB.  The 64 KiB receive buffer
 * means the sender must not be allowed to run a whole MiB ahead; alternating
 * send/receive keeps the window open and measures the steady-state path.
 * Buffer setup and the final integrity check are outside the timed region so
 * the result is the send/recv path, not memcpy/memset/memcmp bandwidth. */
static void test_perf_tcp_1m(void) {
	const size_t chunk = 32768;
	const int rounds = 32;
	const size_t total = chunk * (size_t)rounds;
	uint64_t t0;
	uint64_t us;
	int lfd = -1;
	int cfd = -1;
	int afd = -1;
	sockaddr_in la;
	sockaddr_in ca;
	sockaddr_in peer;
	socklen_t len;
	char* out = NULL;
	char* in = NULL;
	int ok = 1;
	int i;

	lfd = socket(AF_INET, SOCK_STREAM, 0);
	rcv4(&la, "127.0.0.1", 39020);
	if (lfd < 0 || bind(lfd, (struct sockaddr*)&la, sizeof(la)) != 0 ||
		listen(lfd, 4) != 0) {
		rec("perf.tcp.loopback.1m", 0, lfd, 0);
		if (lfd >= 0)
			_KeCloseFile(lfd);
		return;
	}

	cfd = socket(AF_INET, SOCK_STREAM, 0);
	rcv4(&ca, "127.0.0.1", 39020);
	if (cfd < 0 || connect(cfd, (sockaddr_*)&ca, sizeof(ca)) != 0) {
		rec("perf.tcp.loopback.1m", 0, cfd, 0);
		if (cfd >= 0)
			_KeCloseFile(cfd);
		_KeCloseFile(lfd);
		return;
	}

	memset(&peer, 0, sizeof(peer));
	len = sizeof(peer);
	afd = accept(lfd, (struct sockaddr*)&peer, &len);
	if (afd < 0) {
		rec("perf.tcp.loopback.1m", 0, afd, 0);
		_KeCloseFile(cfd);
		_KeCloseFile(lfd);
		return;
	}

	out = (char*)malloc(total);
	in = (char*)malloc(total);
	if (!out || !in) {
		rec("perf.tcp.loopback.1m", 0, 0, 0);
		goto done;
	}

	/* Fill and clear once, before the clock starts.  The old loop rebuilt
	 * the pattern and memset/memcmp'd every 32 KiB chunk, adding ~3 MiB of
	 * buffer work to a 1 MiB transfer measurement. */
	for (i = 0; i < (int)total; i++)
		out[i] = (char)((i * 13 + 7) & 0xFF);
	memset(in, 0, total);

	t0 = now_us();
	for (i = 0; i < rounds && ok; i++) {
		if (send_all_deadline(cfd, out + (size_t)i * chunk, chunk,
			now_us() + 5000000) != 0) {
			ok = 0;
			break;
		}
		if (recv_buf(afd, in + (size_t)i * chunk, chunk, MSG_WAITALL) !=
			(int)chunk) {
			ok = 0;
			break;
		}
	}
	us = now_us() - t0;
	/* Verify after timing so the comparison does not inflate the result. */
	if (ok && memcmp(out, in, total) != 0)
		ok = 0;
	{
		int kib_s = us ? (int)((total / 1024) * 1000000ULL / us) : 0;

		_KePrint("[netsystest] info perf.tcp.loopback.1m=%d KiB/s\r\n",
			kib_s);
	}
	rec("perf.tcp.loopback.1m", ok && us <= 100000, (int)us, us);

done:
	free(in);
	free(out);
	_KeCloseFile(afd);
	_KeCloseFile(cfd);
	_KeCloseFile(lfd);
}

/* Connection churn: 100 full connect/accept/close cycles.  The kernel prints
 * one UART line per TCP socket, so the budget leaves room for that debug
 * noise while still failing if a single connect waits a scheduler tick. */
static void test_perf_connect(void) {
	uint64_t t0;
	uint64_t us;
	int lfd;
	int i;
	int ok = 1;
	sockaddr_in la;
	sockaddr_in ca;
	sockaddr_in peer;
	socklen_t len;

	lfd = socket(AF_INET, SOCK_STREAM, 0);
	rcv4(&la, "127.0.0.1", 39021);
	if (lfd < 0 || bind(lfd, (struct sockaddr*)&la, sizeof(la)) != 0 ||
		listen(lfd, 32) != 0) {
		rec("perf.tcp.connect.100", 0, lfd, 0);
		if (lfd >= 0)
			_KeCloseFile(lfd);
		return;
	}

	t0 = now_us();
	for (i = 0; i < 100; i++) {
		int cfd = socket(AF_INET, SOCK_STREAM, 0);
		int afd;

		rcv4(&ca, "127.0.0.1", 39021);
		if (cfd < 0 || connect(cfd, (sockaddr_*)&ca, sizeof(ca)) != 0) {
			ok = 0;
			if (cfd >= 0)
				_KeCloseFile(cfd);
			break;
		}
		memset(&peer, 0, sizeof(peer));
		len = sizeof(peer);
		afd = accept(lfd, (struct sockaddr*)&peer, &len);
		if (afd < 0) {
			ok = 0;
			_KeCloseFile(cfd);
			break;
		}
		_KeCloseFile(afd);
		_KeCloseFile(cfd);
	}
	us = now_us() - t0;
	rec("perf.tcp.connect.100", ok && i == 100 && us <= 150000, i, us);
	_KeCloseFile(lfd);
}

#define PERF_UDP_MSGS 64
#define PERF_UDP_ROUNDS 8
#define PERF_UDP_SIZE 128

/* Batched datagram path: 8 rounds x 64 datagrams = 512 messages.  sendmmsg
 * and recvmmsg amortise the syscall boundary, but NetSendMmsg currently
 * loops per message, so this is a correctness + syscall-overhead test, not a
 * zero-copy batch path.  A one-syscall-per-packet regression still shows up
 * in the wall-clock number, just less dramatically than true batching. */
static void test_perf_udp_mmsg(void) {
	static mmsghdr mv[PERF_UDP_MSGS];
	static iovec siov[PERF_UDP_MSGS];
	static char sbuf[PERF_UDP_MSGS][PERF_UDP_SIZE];
	uint64_t t0;
	uint64_t us;
	int rfd;
	int sfd;
	int round;
	int i;
	int ok = 1;
	sockaddr_in ra;

	rfd = socket(AF_INET, SOCK_DGRAM, 0);
	rcv4(&ra, "127.0.0.1", 39022);
	if (rfd < 0 || bind(rfd, (struct sockaddr*)&ra, sizeof(ra)) != 0) {
		rec("perf.udp.mmsg.512", 0, rfd, 0);
		if (rfd >= 0)
			_KeCloseFile(rfd);
		return;
	}

	sfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sfd < 0 || connect(sfd, (sockaddr_*)&ra, sizeof(ra)) != 0) {
		rec("perf.udp.mmsg.512", 0, sfd, 0);
		if (sfd >= 0)
			_KeCloseFile(sfd);
		_KeCloseFile(rfd);
		return;
	}

	memset(mv, 0, sizeof(mv));
	memset(siov, 0, sizeof(siov));
	for (i = 0; i < PERF_UDP_MSGS; i++) {
		siov[i].iov_base = sbuf[i];
		siov[i].iov_len = PERF_UDP_SIZE;
		mv[i].msg_hdr.msg_iov = &siov[i];
		mv[i].msg_hdr.msg_iovlen = 1;
	}

	t0 = now_us();
	for (round = 0; round < PERF_UDP_ROUNDS && ok; round++) {
		int got = 0;
		uint64_t seen = 0;
		uint32_t base = (uint32_t)(round * PERF_UDP_MSGS);

		for (i = 0; i < PERF_UDP_MSGS; i++) {
			uint32_t seq = base + (uint32_t)i;

			memcpy(sbuf[i], &seq, sizeof(seq));
			memset(sbuf[i] + sizeof(seq), (int)(seq & 0xFF),
				PERF_UDP_SIZE - sizeof(seq));
		}
		if (sendmmsg(sfd, mv, PERF_UDP_MSGS, 0) != PERF_UDP_MSGS) {
			ok = 0;
			break;
		}
		while (got < PERF_UDP_MSGS) {
			static mmsghdr rv[PERF_UDP_MSGS];
			static iovec riov[PERF_UDP_MSGS];
			static char rbuf[PERF_UDP_MSGS][PERF_UDP_SIZE];
			struct timespec ts;
			int n;
			int k;

			memset(rv, 0, sizeof(rv));
			for (i = 0; i < PERF_UDP_MSGS; i++) {
				riov[i].iov_base = rbuf[i];
				riov[i].iov_len = PERF_UDP_SIZE;
				rv[i].msg_hdr.msg_iov = &riov[i];
				rv[i].msg_hdr.msg_iovlen = 1;
			}
			ts.tv_sec = 1;
			ts.tv_nsec = 0;
			n = (int)recvmmsg(rfd, rv, PERF_UDP_MSGS, 0, &ts);
			if (n <= 0) {
				ok = 0;
				break;
			}
			for (k = 0; k < n; k++) {
				uint32_t seq;
				uint32_t slot;

				if (rv[k].msg_len != PERF_UDP_SIZE) {
					ok = 0;
					continue;
				}
				memcpy(&seq, rbuf[k], sizeof(seq));
				if (seq < base || seq >= base + PERF_UDP_MSGS) {
					ok = 0;
					got++;
					continue;
				}
				slot = seq - base;
				if (seen & (1ULL << slot))
					ok = 0;
				seen |= 1ULL << slot;
				got++;
			}
		}
		if (seen != ~0ULL)
			ok = 0;
	}
	us = now_us() - t0;
	rec("perf.udp.mmsg.512", ok && us <= 100000, (int)us, us);

	_KeCloseFile(sfd);
	_KeCloseFile(rfd);
}

/* A ready descriptor must wake poll immediately, not after a timer tick. */
static void test_perf_poll(void) {
	int sv[2];
	int rc;
	uint64_t t0;
	uint64_t us;
	struct pollfd p;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
		rec("perf.poll.ready", 0, -1, 0);
		return;
	}
	send_buf(sv[0], "x", 1, 0);
	p.fd = sv[1];
	p.events = POLLIN;
	p.revents = 0;
	t0 = now_us();
	rc = poll(&p, 1, 1000);
	us = now_us() - t0;
	rec("perf.poll.ready",
		rc == 1 && (p.revents & POLLIN) && us <= 50000, rc, us);
	_KeCloseFile(sv[0]);
	_KeCloseFile(sv[1]);
}

/* Optional host-to-guest TCP throughput test. The host server sends exactly
 * --speed-mib MiB and closes; the guest reads and times the receive path.
 * This exercises virtio-net, the QEMU slirp path and the TCP receive path,
 * not the loopback memory copy measured by perf.tcp.loopback.1m. */
typedef struct {
	size_t got;
	uint64_t connect_us;
	uint64_t poll_us;
	uint64_t recv_us;
	uint64_t total_us;
	int poll_calls;
	int recv_calls;
	int ok;
} SpeedRun;

/* One host->guest transfer: connect, then poll+recv until the server's
 * fixed byte count has arrived. Returns 0 when the run completed. */
static int speed_run_once(SpeedRun* r, const char* host, int port,
	size_t expect, char* buf, size_t chunk) {
	uint64_t t0;
	int fd;
	sockaddr_in a;
	struct pollfd p;

	memset(r, 0, sizeof(*r));
	fd = socket(AF_INET, SOCK_STREAM, 0);
	rcv4(&a, host, port);
	t0 = now_us();
	if (fd < 0 || connect(fd, (sockaddr_*)&a, sizeof(a)) != 0) {
		r->connect_us = now_us() - t0;
		if (fd >= 0)
			_KeCloseFile(fd);
		return -1;
	}
	r->connect_us = now_us() - t0;
	r->ok = 1;

	t0 = now_us();
	while (r->got < expect) {
		int n;
		uint64_t op;
		uint64_t orc;
		int pr;

		p.fd = fd;
		p.events = POLLIN;
		p.revents = 0;
		op = now_us();
		pr = poll(&p, 1, 10000);
		r->poll_us += now_us() - op;
		r->poll_calls++;
		if (pr <= 0) {
			r->ok = 0;
			break;
		}
		orc = now_us();
		n = recv_buf(fd, buf, chunk, 0);
		r->recv_us += now_us() - orc;
		r->recv_calls++;
		if (n < 0) {
			if (errno == EINTR)
				continue;
			r->ok = 0;
			break;
		}
		if (n == 0)
			break;
		r->got += (size_t)n;
	}
	r->total_us = now_us() - t0;
	if (r->got == expect && r->ok)
		r->ok = 1;
	else
		r->ok = 0;
	_KeCloseFile(fd);
	return r->ok ? 0 : -1;
}

/* Optional host-to-guest TCP throughput test. The host server sends exactly
 * --speed-mib MiB and closes; the guest reads and times the receive path.
 * --speed-reps repeats the transfer in the same boot so a median can be
 * taken without mixing in boot-to-boot TCG variance. */
static void test_perf_host_speed(void) {
	const size_t chunk = 65536;
	size_t expect;
	int reps;
	int i;
	int pass = 1;
	int kib[16];
	int connect_us[16];
	int nkib = 0;
	char* buf = NULL;
	SpeedRun run;

	if (!g_speed_host)
		return;
	expect = (size_t)(g_speed_mib > 0 ? g_speed_mib : 8) * 1024 * 1024;
	reps = g_speed_reps > 0 ? g_speed_reps : 1;
	if (reps > 16)
		reps = 16;

	buf = (char*)malloc(chunk);
	if (!buf) {
		rec("perf.tcp.host.speed", 0, 0, 0);
		return;
	}

	for (i = 0; i < reps; i++) {
		if (speed_run_once(&run, g_speed_host,
				g_speed_port > 0 ? g_speed_port : 9000, expect, buf,
				chunk) != 0) {
			_KePrint("[netsystest] info perf.tcp.host.rep.fail=%d\r\n", i);
			pass = 0;
			break;
		}
		kib[nkib] = run.total_us ?
			(int)((run.got / 1024) * 1000000ULL / run.total_us) : 0;
		connect_us[nkib] = (int)run.connect_us;
		_KePrint("[netsystest] info perf.tcp.host.rep.kibs=%d\r\n", kib[nkib]);
		_KePrint("[netsystest] info perf.tcp.host.rep.total_us=%d\r\n",
			(int)run.total_us);
		_KePrint("[netsystest] info perf.tcp.host.rep.poll_us=%d\r\n",
			(int)run.poll_us);
		_KePrint("[netsystest] info perf.tcp.host.rep.recv_us=%d\r\n",
			(int)run.recv_us);
		_KePrint("[netsystest] info perf.tcp.host.rep.connect_us=%d\r\n",
			connect_us[nkib]);
		nkib++;
	}

	if (nkib > 0) {
		int j;
		int med;

		for (i = 1; i < nkib; i++) {
			int v = kib[i];
			int c = connect_us[i];

			for (j = i - 1; j >= 0 && kib[j] > v; j--) {
				kib[j + 1] = kib[j];
				connect_us[j + 1] = connect_us[j];
			}
			kib[j + 1] = v;
			connect_us[j + 1] = c;
		}
		med = kib[nkib / 2];
		_KePrint("[netsystest] info perf.tcp.host.reps=%d\r\n", nkib);
		_KePrint("[netsystest] info perf.tcp.host.kibs.median=%d\r\n", med);
		_KePrint("[netsystest] info perf.tcp.host.kibs.min=%d\r\n", kib[0]);
		_KePrint("[netsystest] info perf.tcp.host.kibs.max=%d\r\n",
			kib[nkib - 1]);
		_KePrint("[netsystest] info perf.tcp.host.connect_us.median=%d\r\n",
			connect_us[nkib / 2]);
		_KePrint("[netsystest] info perf.tcp.host.bytes=%d\r\n",
			(int)expect);
		rec("perf.tcp.host.speed", pass, med,
			(uint64_t)(med ? (expect / 1024) * 1000000ULL / (uint64_t)med : 0));
	} else {
		rec("perf.tcp.host.speed", 0, 0, 0);
	}
	free(buf);
}

static void test_poll(void) {
	uint64_t t0;
	int rc;
	int fd, lfd, cfd, afd;
	sockaddr_in a, ca;
	socklen_t len;
	struct pollfd p;

	fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
	rcv4(&a, "127.0.0.1", 39006);
	rc = bind(fd, (struct sockaddr*)&a, sizeof(a));
	rec("poll.udp.bind", rc == 0, rc, 0);

	p.fd = fd;
	p.events = POLLIN;
	p.revents = 0;
	t0 = now_us();
	rc = poll(&p, 1, 150);
	{
		uint64_t us = now_us() - t0;
		rec("poll.timeout.150ms",
			rc == 0 && us >= 140000 && us <= 800000, rc, us);
	}

	p.revents = 0;
	t0 = now_us();
	rc = poll(&p, 1, 0);
	{
		uint64_t us = now_us() - t0;
		rec("poll.timeout.zero", rc == 0 && us < 50000, rc, us);
	}

	p.fd = 9999;
	p.events = POLLIN;
	p.revents = 0;
	t0 = now_us();
	rc = poll(&p, 1, 0);
	rec("poll.bad-fd.revents",
		rc == 1 && (p.revents & POLLNVAL), p.revents, now_us() - t0);

	_KeCloseFile(fd);

	/* Writable + readable socket readiness. */
	lfd = socket(AF_INET, SOCK_STREAM, 0);
	rcv4(&a, "127.0.0.1", 39007);
	rc = bind(lfd, (struct sockaddr*)&a, sizeof(a));
	rc = listen(lfd, 4);

	cfd = socket(AF_INET, SOCK_STREAM, 0);
	rcv4(&ca, "127.0.0.1", 39007);
	rc = connect(cfd, (sockaddr_*)&ca, sizeof(ca));
	len = sizeof(ca);
	afd = accept(lfd, (struct sockaddr*)&ca, &len);

	p.fd = cfd;
	p.events = POLLOUT;
	p.revents = 0;
	t0 = now_us();
	rc = poll(&p, 1, 0);
	rec("poll.tcp.writable", rc == 1 && (p.revents & POLLOUT),
		p.revents, now_us() - t0);

	{
		char text[] = "poll-data";
		iovec iov;
		msghdr m;

		iov.iov_base = text;
		iov.iov_len = 9;
		memset(&m, 0, sizeof(m));
		m.msg_iov = &iov;
		m.msg_iovlen = 1;
		rc = (int)sendmsg(cfd, &m, 0);
	}

	p.fd = afd;
	p.events = POLLIN;
	p.revents = 0;
	t0 = now_us();
	rc = poll(&p, 1, 0);
	rec("poll.tcp.readable", rc == 1 && (p.revents & POLLIN),
		p.revents, now_us() - t0);

	if (afd >= 0)
		_KeCloseFile(afd);
	_KeCloseFile(cfd);
	_KeCloseFile(lfd);
}

/* --- IPv6 loopback TCP -------------------------------------------------- */
static void test_ipv6_tcp(void) {
	uint64_t t0;
	int rc;
	int lfd = -1, cfd = -1, afd = -1;
	sockaddr_in6 a, ca;
	socklen_t len;

	lfd = socket(AF_INET6, SOCK_STREAM, 0);
	rec("v6.tcp.socket", lfd >= 0, lfd, 0);

	rcv4_6(&a, "::1", 39004);
	t0 = now_us();
	rc = bind(lfd, (struct sockaddr*)&a, sizeof(a));
	rec("v6.tcp.bind", rc == 0, rc, now_us() - t0);

	rc = listen(lfd, 4);
	rec("v6.tcp.listen", rc == 0, rc, 0);

	cfd = socket(AF_INET6, SOCK_STREAM, 0);
	t0 = now_us();
	rc = connect(cfd, (sockaddr_*)&a, sizeof(a));
	{
		uint64_t us = now_us() - t0;
		rec("v6.tcp.connect", rc == 0 && us <= 200000, rc, us);
	}

	memset(&ca, 0, sizeof(ca));
	len = sizeof(ca);
	t0 = now_us();
	afd = accept(lfd, (struct sockaddr*)&ca, &len);
	rec("v6.tcp.accept",
		afd >= 0 && ca.sin6_family == AF_INET6, afd, now_us() - t0);

	{
		char text[] = "v6-tcp";
		char got[8];
		iovec iov;
		msghdr m;

		iov.iov_base = text;
		iov.iov_len = 6;
		memset(&m, 0, sizeof(m));
		m.msg_iov = &iov;
		m.msg_iovlen = 1;
		t0 = now_us();
		rc = (int)sendmsg(cfd, &m, 0);
		rec("v6.tcp.send", rc == 6, rc, now_us() - t0);

		memset(got, 0, sizeof(got));
		iov.iov_base = got;
		iov.iov_len = 6;
		t0 = now_us();
		rc = (int)recvmsg(afd, &m, MSG_WAITALL);
		rec("v6.tcp.recv", rc == 6 && memcmp(got, "v6-tcp", 6) == 0, rc,
			now_us() - t0);
	}

	if (afd >= 0)
		_KeCloseFile(afd);
	if (cfd >= 0)
		_KeCloseFile(cfd);
	if (lfd >= 0)
		_KeCloseFile(lfd);
}

/* --- error paths -------------------------------------------------------- */
static void test_errors(void) {
	uint64_t t0;
	int rc;
	int tcp = socket(AF_INET, SOCK_STREAM, 0);
	int udp = socket(AF_INET, SOCK_DGRAM, 0);
	sockaddr_in a;
	msghdr m;

	rcv4(&a, "127.0.0.1", 39008);

	t0 = now_us();
	rc = connect(-1, NULL, 0);
	rec("err.connect.bad-fd", rc == -EBADF, rc, now_us() - t0);

	t0 = now_us();
	rc = bind(-1, NULL, 0);
	rec("err.bind.bad-fd", rc == -EBADF, rc, now_us() - t0);

	t0 = now_us();
	rc = listen(-1, 1);
	rec("err.listen.bad-fd", rc == -EBADF, rc, now_us() - t0);

	t0 = now_us();
	rc = accept(-1, NULL, NULL);
	rec("err.accept.bad-fd", rc == -EBADF, rc, now_us() - t0);

	{
		iovec iov;
		char b[4];
		iov.iov_base = b;
		iov.iov_len = sizeof(b);
		memset(&m, 0, sizeof(m));
		m.msg_iov = &iov;
		m.msg_iovlen = 1;

		t0 = now_us();
		rc = (int)sendmsg(-1, &m, 0);
		rec("err.sendmsg.bad-fd", rc == -EBADF, rc, now_us() - t0);

		t0 = now_us();
		rc = (int)recvmsg(-1, &m, 0);
		rec("err.recvmsg.bad-fd", rc == -EBADF, rc, now_us() - t0);

		t0 = now_us();
		rc = (int)sendmsg(tcp, NULL, 0);
		rec("err.sendmsg.null-msg", rc == -EFAULT, rc, now_us() - t0);

		t0 = now_us();
		rc = (int)recvmsg(tcp, NULL, 0);
		rec("err.recvmsg.null-msg", rc == -EFAULT, rc, now_us() - t0);
	}

	{
		memset(&m, 0, sizeof(m));
		m.msg_iovlen = 2000;
		t0 = now_us();
		rc = (int)sendmsg(tcp, &m, 0);
		rec("err.sendmsg.iov-too-many", rc == -EINVAL, rc, now_us() - t0);
	}

	t0 = now_us();
	rc = connect(tcp, NULL, 0);
	rec("err.connect.null-addr", rc == -EINVAL, rc, now_us() - t0);

	t0 = now_us();
	rc = connect(tcp, (sockaddr_*)&a, 4);
	rec("err.connect.short-addr", rc == -EINVAL, rc, now_us() - t0);

	t0 = now_us();
	rc = bind(tcp, (struct sockaddr*)&a, sizeof(a));
	rec("err.bind.unbound-tcp-ok", rc == 0, rc, now_us() - t0);

	t0 = now_us();
	rc = bind(tcp, (struct sockaddr*)&a, sizeof(a));
	rec("err.bind.twice", rc == -EINVAL, rc, now_us() - t0);

	t0 = now_us();
	rc = connect(tcp, (sockaddr_*)&a, sizeof(a));
	rec("err.connect.not-listening", rc < 0 && rc != 0, rc, now_us() - t0);

	t0 = now_us();
	rc = accept(tcp, NULL, NULL);
	rec("err.accept.not-listener", rc == -EINVAL, rc, now_us() - t0);

	{
		char text[] = "x";
		iovec iov;
		iov.iov_base = text;
		iov.iov_len = 1;
		memset(&m, 0, sizeof(m));
		m.msg_iov = &iov;
		m.msg_iovlen = 1;
		t0 = now_us();
		rc = (int)sendmsg(tcp, &m, 0);
		rec("err.send.unconnected", rc == -EPIPE, rc, now_us() - t0);
	}

	{
		memset(&m, 0, sizeof(m));
		t0 = now_us();
		rc = (int)sendmsg(udp, &m, 0);
		rec("err.udp.send.no-destination", rc == -EDESTADDRREQ, rc,
			now_us() - t0);
	}

	_KeCloseFile(udp);
	_KeCloseFile(tcp);
}

/* --- getsockopt(SO_ERROR) consumption and IPv6 text parsing ------------- */
static void test_so_error(void) {
	uint64_t t0;
	int fd, rc, err;
	socklen_t len;
	sockaddr_in a;
	struct pollfd p;

	fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	rcv4(&a, "127.0.0.1", 39010);
	t0 = now_us();
	rc = connect(fd, (sockaddr_*)&a, sizeof(a));
	rec("err.so-error.connect-started",
		rc == -EINPROGRESS || rc == -ECONNREFUSED, rc, now_us() - t0);

	p.fd = fd;
	p.events = POLLOUT;
	p.revents = 0;
	t0 = now_us();
	rc = poll(&p, 1, 1000);
	rec("err.so-error.poll", rc == 1 && (p.revents & (POLLERR | POLLHUP)),
		p.revents, now_us() - t0);

	/* A short buffer must not consume the pending error. */
	len = 1;
	err = 0;
	t0 = now_us();
	rc = getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
	rec("opt.getsockopt.so-error.short",
		rc == -EINVAL && len == sizeof(int), rc, now_us() - t0);

	len = sizeof(err);
	err = 0;
	t0 = now_us();
	rc = getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
	rec("opt.getsockopt.so-error.pending",
		rc == 0 && err == ECONNREFUSED, err, now_us() - t0);

	len = sizeof(err);
	err = -1;
	t0 = now_us();
	rc = getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
	rec("opt.getsockopt.so-error.cleared", rc == 0 && err == 0, err,
		now_us() - t0);

	len = sizeof(err);
	t0 = now_us();
	rc = getsockopt(fd, SOL_SOCKET, SO_ERROR, NULL, &len);
	rec("opt.getsockopt.null-optval", rc == -EFAULT, rc, now_us() - t0);

	_KeCloseFile(fd);
}

static void test_inet_pton6(void) {
	static const struct {
		const char* text;
		uint8_t want[16];
	} ok[] = {
		{ "::", { 0 } },
		{ "::1", { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1 } },
		{ "1::2", { 0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,2 } },
		{ "2001:db8::1",
			{ 0x20,0x01,0x0d,0xb8,0,0,0,0,0,0,0,0,0,0,0,1 } },
		{ "fe80::1:2:3:4",
			{ 0xfe,0x80,0,0,0,0,0,0,0,1,0,2,0,3,0,4 } },
		{ "1:2:3:4:5:6:7:8",
			{ 0,1,0,2,0,3,0,4,0,5,0,6,0,7,0,8 } },
	};
	static const char* bad[] = {
		"", ":", ":::", "12345::1", "1::2::3",
		"1:2:3:4:5:6:7:8:9", "1:2:3:4:5:6:7", "gggg::1",
		":1:2:3:4:5:6:7",
	};
	uint8_t got[16];
	uint64_t t0;
	int rc;

	for (unsigned i = 0; i < sizeof(ok) / sizeof(ok[0]); i++) {
		char name[64];

		strcpy(name, "inet.pton6.ok.");
		strcat(name, ok[i].text);
		memset(got, 0xFF, sizeof(got));
		t0 = now_us();
		rc = inet_pton(AF_INET6, ok[i].text, got);
		rec(name, rc == 1 && memcmp(got, ok[i].want, 16) == 0,
			rc, now_us() - t0);
	}

	for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		char name[64];

		strcpy(name, "inet.pton6.bad.");
		strcat(name, bad[i]);
		memset(got, 0xFF, sizeof(got));
		t0 = now_us();
		rc = inet_pton(AF_INET6, bad[i], got);
		rec(name, rc == 0, rc, now_us() - t0);
	}
}

/* --- network libraries: inet_*, resolver, services, getnameinfo --------- */
static void test_netlibs(void) {
	uint64_t t0;
	int rc;

	/* Byte order: verify the wire bytes, not only a round trip. */
	{
		uint16_t s = htons(0x1234);
		uint32_t l = htonl(0x12345678);
		uint8_t* sb = (uint8_t*)&s;
		uint8_t* lb = (uint8_t*)&l;

		rec("netlib.htons.bytes", sb[0] == 0x12 && sb[1] == 0x34, s, 0);
		rec("netlib.htonl.bytes",
			lb[0] == 0x12 && lb[1] == 0x34 && lb[2] == 0x56 && lb[3] == 0x78,
			(long)l, 0);
		rec("netlib.ntohs.roundtrip", ntohs(htons(0xBEEF)) == 0xBEEF, 0, 0);
		rec("netlib.ntohl.roundtrip",
			ntohl(htonl(0xDEADBEEF)) == 0xDEADBEEF, 0, 0);
	}

	/* inet_addr/inet_ntoa: valid, zero, and malformed input. */
	{
		uint32_t a = inet_addr("192.0.2.1");
		uint8_t want[4] = { 192, 0, 2, 1 };
		struct in_addr ia;
		const char* s;

		rec("netlib.inet_addr.valid", memcmp(&a, want, 4) == 0, (long)a, 0);
		rec("netlib.inet_addr.zero", inet_addr("0.0.0.0") == 0, 0, 0);
		rec("netlib.inet_addr.broadcast",
			inet_addr("255.255.255.255") == INADDR_NONE, 0, 0);
		rec("netlib.inet_addr.bad.range",
			inet_addr("256.1.1.1") == INADDR_NONE, 0, 0);
		rec("netlib.inet_addr.bad.short",
			inet_addr("1.2.3") == INADDR_NONE, 0, 0);
		rec("netlib.inet_addr.bad.long",
			inet_addr("1.2.3.4.5") == INADDR_NONE, 0, 0);
		rec("netlib.inet_addr.bad.empty",
			inet_addr("") == INADDR_NONE, 0, 0);
		rec("netlib.inet_addr.bad.text",
			inet_addr("1.2.3.x") == INADDR_NONE, 0, 0);

		ia.s_addr = inet_addr("198.51.100.7");
		t0 = now_us();
		s = inet_ntoa(ia);
		rec("netlib.inet_ntoa",
			s && strcmp(s, "198.51.100.7") == 0, s ? 1 : 0,
			now_us() - t0);
	}

	/* inet_pton/inet_ntop IPv4 and error reporting. */
	{
		uint8_t bin[16];
		char text[64];
		struct in_addr a;
		const char* s;
		int n;

		memset(bin, 0xFF, sizeof(bin));
		t0 = now_us();
		n = inet_pton(AF_INET, "203.0.113.9", bin);
		rec("netlib.pton4",
			n == 1 && bin[0] == 203 && bin[1] == 0 && bin[2] == 113 &&
				bin[3] == 9,
			n, now_us() - t0);
		rec("netlib.pton4.bad",
			inet_pton(AF_INET, "203.0.113", bin) == 0, 0, 0);
		rec("netlib.pton4.range",
			inet_pton(AF_INET, "203.0.113.256", bin) == 0, 0, 0);

		memset(&a, 0, sizeof(a));
		memcpy(&a, bin, 4);
		t0 = now_us();
		s = inet_ntop(AF_INET, &a, text, sizeof(text));
		rec("netlib.ntop4",
			s && strcmp(text, "203.0.113.9") == 0, s ? 1 : 0,
			now_us() - t0);

		errno = 0;
		s = inet_ntop(AF_INET, &a, text, 8);
		rec("netlib.ntop4.enospc", s == NULL && errno == ENOSPC, errno, 0);

		errno = 0;
		s = inet_ntop(999, &a, text, sizeof(text));
		rec("netlib.ntop.bad-family",
			s == NULL && errno == EAFNOSUPPORT, errno, 0);

		errno = 0;
		n = inet_pton(999, "127.0.0.1", bin);
		rec("netlib.pton.bad-family",
			n == -1 && errno == EAFNOSUPPORT, errno, 0);
	}

	/* IPv4-mapped and dotted-quad IPv6 forms. */
	{
		static const uint8_t mapped[16] = {
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 192, 0, 2, 1
		};
		uint8_t bin[16];
		char text[64];
		const char* s;
		int n;

		memset(bin, 0xFF, sizeof(bin));
		n = inet_pton(AF_INET6, "::ffff:192.0.2.1", bin);
		rec("netlib.pton6.mapped",
			n == 1 && memcmp(bin, mapped, 16) == 0, n, 0);

		memset(bin, 0, sizeof(bin));
		n = inet_pton(AF_INET6, "1:2:3:4:5:6:192.0.2.1", bin);
		rec("netlib.pton6.dotted-full",
			n == 1 && bin[0] == 0 && bin[1] == 1 && bin[2] == 0 &&
				bin[3] == 2 && bin[12] == 192 && bin[15] == 1,
			n, 0);

		n = inet_pton(AF_INET6, "::192.0.2.1", bin);
		rec("netlib.pton6.dotted-short",
			n == 1 && bin[10] == 0 && bin[11] == 0 && bin[12] == 192 &&
				bin[13] == 0 && bin[14] == 2 && bin[15] == 1,
			n, 0);

		rec("netlib.pton6.dotted-bad",
			inet_pton(AF_INET6, "::ffff:192.0.2.256", bin) == 0 &&
				inet_pton(AF_INET6, "::ffff:192.0.2", bin) == 0 &&
				inet_pton(AF_INET6, "1.2.3.4", bin) == 0,
			0, 0);

		memset(bin, 0, sizeof(bin));
		bin[10] = 0xFF;
		bin[11] = 0xFF;
		bin[12] = 192;
		bin[14] = 2;
		bin[15] = 1;
		t0 = now_us();
		s = inet_ntop(AF_INET6, bin, text, sizeof(text));
		rec("netlib.ntop6.mapped",
			s && strcmp(text, "::ffff:192.0.2.1") == 0, s ? 1 : 0,
			now_us() - t0);

		memset(bin, 0, sizeof(bin));
		bin[0] = 0x20;
		bin[1] = 0x01;
		bin[2] = 0x0d;
		bin[3] = 0xb8;
		bin[15] = 1;
		s = inet_ntop(AF_INET6, bin, text, sizeof(text));
		rec("netlib.ntop6",
			s && strcmp(text, "2001:db8::1") == 0, 0, 0);

		errno = 0;
		s = inet_ntop(AF_INET6, bin, text, 8);
		rec("netlib.ntop6.enospc", s == NULL && errno == ENOSPC, errno, 0);
	}

	/* /hosts forward lookups, aliases, numeric nodes and misses. */
	{
		hostent* he;
		uint8_t want4[4];
		uint8_t want6[16];

		inet_pton(AF_INET, "127.0.0.53", want4);
		he = gethostbyname("netlib-test.xeneva");
		rec("netlib.gethostbyname.hosts",
			he && he->h_addrtype == AF_INET && he->h_length == 4 &&
				he->h_addr_list && he->h_addr_list[0] &&
				memcmp(he->h_addr_list[0], want4, 4) == 0,
			he ? he->h_addrtype : -1, 0);

		he = gethostbyname("netlib-alias");
		rec("netlib.gethostbyname.alias",
			he && he->h_addrtype == AF_INET && he->h_addr_list[0] &&
				memcmp(he->h_addr_list[0], want4, 4) == 0,
			he ? he->h_addrtype : -1, 0);

		he = gethostbyname("localhost");
		rec("netlib.gethostbyname.localhost",
			he && he->h_addrtype == AF_INET && he->h_addr_list[0] &&
				((uint8_t*)he->h_addr_list[0])[0] == 127 &&
				((uint8_t*)he->h_addr_list[0])[3] == 1,
			he ? he->h_addrtype : -1, 0);

		inet_pton(AF_INET6, "2001:db8::53", want6);
		he = gethostbyname("netlib6.xeneva.test");
		rec("netlib.gethostbyname.v6",
			he && he->h_addrtype == AF_INET6 && he->h_length == 16 &&
				he->h_addr_list[0] &&
				memcmp(he->h_addr_list[0], want6, 16) == 0,
			he ? he->h_addrtype : -1, 0);

		he = gethostbyname("no-such-host.localhost");
		rec("netlib.gethostbyname.miss",
			he == NULL && h_errno == HOST_NOT_FOUND, h_errno, 0);

		he = gethostbyname("192.0.2.1");
		rec("netlib.gethostbyname.numeric",
			he && he->h_addrtype == AF_INET && he->h_addr_list[0] &&
				((uint8_t*)he->h_addr_list[0])[0] == 192 &&
				((uint8_t*)he->h_addr_list[0])[3] == 1,
			he ? he->h_addrtype : -1, 0);
	}

	/* Reverse lookups from the static table and /hosts. */
	{
		hostent* he;
		struct in_addr a4;
		struct in6_addr a6;

		inet_pton(AF_INET, "127.0.0.1", &a4);
		he = gethostbyaddr(&a4, 4, AF_INET);
		rec("netlib.gethostbyaddr.loopback",
			he && strcmp(he->h_name, "localhost") == 0 &&
				he->h_addrtype == AF_INET && he->h_length == 4,
			0, 0);

		inet_pton(AF_INET, "127.0.0.53", &a4);
		he = gethostbyaddr(&a4, 4, AF_INET);
		rec("netlib.gethostbyaddr.hosts",
			he && strcmp(he->h_name, "netlib-test.xeneva") == 0, 0, 0);

		inet_pton(AF_INET, "192.0.2.55", &a4);
		he = gethostbyaddr(&a4, 4, AF_INET);
		rec("netlib.gethostbyaddr.docs",
			he && strcmp(he->h_name, "docs.xeneva.test") == 0, 0, 0);

		inet_pton(AF_INET6, "2001:db8::53", &a6);
		he = gethostbyaddr(&a6, 16, AF_INET6);
		rec("netlib.gethostbyaddr.v6",
			he && strcmp(he->h_name, "netlib6.xeneva.test") == 0 &&
				he->h_addrtype == AF_INET6 && he->h_length == 16,
			0, 0);

		he = gethostbyaddr(&a4, 4, 999);
		rec("netlib.gethostbyaddr.bad-family",
			he == NULL && h_errno == NO_RECOVERY, h_errno, 0);
	}

	/* Services database. */
	{
		servent* se;

		se = getservbyname("http", "tcp");
		rec("netlib.getservbyname.http",
			se && se->s_port == htons(80) &&
				strcmp(se->s_proto, "tcp") == 0,
			se ? se->s_port : -1, 0);

		se = getservbyname("domain", "udp");
		rec("netlib.getservbyname.domain",
			se && se->s_port == htons(53) &&
				strcmp(se->s_proto, "udp") == 0,
			se ? se->s_port : -1, 0);

		se = getservbyport(htons(443), "tcp");
		rec("netlib.getservbyport.https",
			se && strcmp(se->s_name, "https") == 0, 0, 0);

		se = getservbyport(htons(53), "udp");
		rec("netlib.getservbyport.domain",
			se && strcmp(se->s_name, "domain") == 0, 0, 0);

		rec("netlib.getserv.miss",
			getservbyname("nosuchservice", "tcp") == NULL &&
				getservbyport(htons(443), "udp") == NULL &&
				getservbyport(htons(65000), NULL) == NULL,
			0, 0);
	}

	/* getnameinfo: names, numeric flags, services, overflow. */
	{
		sockaddr_in in;
		sockaddr_in6 in6;
		char host[NI_MAXHOST];
		char serv[NI_MAXSERV];

		memset(&in, 0, sizeof(in));
		in.sin_family = AF_INET;
		in.sin_port = htons(80);
		inet_pton(AF_INET, "127.0.0.53", &in.sin_addr);

		t0 = now_us();
		rc = getnameinfo((struct sockaddr*)&in, sizeof(in), host,
						 sizeof(host), serv, sizeof(serv), 0);
		rec("netlib.getnameinfo.hosts",
			rc == 0 && strcmp(host, "netlib-test.xeneva") == 0 &&
				strcmp(serv, "http") == 0,
			rc, now_us() - t0);

		rc = getnameinfo((struct sockaddr*)&in, sizeof(in), host,
						 sizeof(host), serv, sizeof(serv),
						 NI_NUMERICHOST | NI_NUMERICSERV);
		rec("netlib.getnameinfo.numeric",
			rc == 0 && strcmp(host, "127.0.0.53") == 0 &&
				strcmp(serv, "80") == 0,
			rc, 0);

		rc = getnameinfo((struct sockaddr*)&in, sizeof(in), host,
						 sizeof(host), NULL, 0, NI_NOFQDN);
		rec("netlib.getnameinfo.nofqdn",
			rc == 0 && strcmp(host, "netlib-test") == 0, rc, 0);

		in.sin_port = htons(53);
		rc = getnameinfo((struct sockaddr*)&in, sizeof(in), host,
						 sizeof(host), serv, sizeof(serv),
						 NI_NUMERICHOST | NI_DGRAM);
		rec("netlib.getnameinfo.dgram",
			rc == 0 && strcmp(serv, "domain") == 0, rc, 0);

		in.sin_port = htons(65000);
		rc = getnameinfo((struct sockaddr*)&in, sizeof(in), host,
						 sizeof(host), serv, sizeof(serv), NI_NUMERICHOST);
		rec("netlib.getnameinfo.unknown-service",
			rc == 0 && strcmp(serv, "65000") == 0, rc, 0);

		in.sin_port = htons(80);
		rc = getnameinfo((struct sockaddr*)&in, sizeof(in), host, 4,
						 serv, sizeof(serv), 0);
		rec("netlib.getnameinfo.host-overflow", rc == EAI_OVERFLOW, rc, 0);

		rc = getnameinfo((struct sockaddr*)&in, sizeof(in), host,
						 sizeof(host), serv, 2, 0);
		rec("netlib.getnameinfo.serv-overflow", rc == EAI_OVERFLOW, rc, 0);

		inet_pton(AF_INET, "127.0.0.99", &in.sin_addr);
		rc = getnameinfo((struct sockaddr*)&in, sizeof(in), host,
						 sizeof(host), NULL, 0, NI_NAMEREQD);
		rec("netlib.getnameinfo.namereqd", rc == EAI_NONAME, rc, 0);

		rc = getnameinfo((struct sockaddr*)&in, sizeof(in), host,
						 sizeof(host), NULL, 0,
						 NI_NUMERICHOST | NI_NAMEREQD);
		rec("netlib.getnameinfo.numeric-namereqd",
			rc == 0 && strcmp(host, "127.0.0.99") == 0, rc, 0);

		rc = getnameinfo((struct sockaddr*)&in, 4, host, sizeof(host),
						 NULL, 0, 0);
		rec("netlib.getnameinfo.short-sockaddr", rc == EAI_FAMILY, rc, 0);

		memset(&in6, 0, sizeof(in6));
		in6.sin6_family = AF_INET6;
		in6.sin6_port = htons(443);
		inet_pton(AF_INET6, "2001:db8::1", &in6.sin6_addr);
		rc = getnameinfo((struct sockaddr*)&in6, sizeof(in6), host,
						 sizeof(host), serv, sizeof(serv),
						 NI_NUMERICHOST | NI_NUMERICSERV);
		rec("netlib.getnameinfo.v6",
			rc == 0 && strcmp(host, "2001:db8::1") == 0 &&
				strcmp(serv, "443") == 0,
			rc, 0);

		rc = getnameinfo((struct sockaddr*)&in6, sizeof(in6), host,
						 sizeof(host), serv, sizeof(serv), NI_DGRAM);
		rec("netlib.getnameinfo.v6-dgram",
			rc == 0 && strcmp(serv, "443") == 0, rc, 0);
	}

	/* getaddrinfo flags, service names, canonical names, NULL node. */
	{
		addrinfo hints;
		addrinfo* res = NULL;

		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		t0 = now_us();
		rc = getaddrinfo("netlib-test.xeneva", "http", &hints, &res);
		rec("netlib.getaddrinfo.hosts",
			rc == 0 && res && res->ai_family == AF_INET &&
				res->ai_socktype == SOCK_STREAM &&
				res->ai_protocol == IPPROTO_TCP &&
				((sockaddr_in*)res->ai_addr)->sin_port == htons(80) &&
				((sockaddr_in*)res->ai_addr)->sin_addr.s_addr ==
					inet_addr("127.0.0.53"),
			rc, now_us() - t0);
		if (res) {
			freeaddrinfo(res);
			res = NULL;
		}

		hints.ai_flags = AI_CANONNAME;
		rc = getaddrinfo("netlib-test.xeneva", "https", &hints, &res);
		rec("netlib.getaddrinfo.canonname",
			rc == 0 && res && res->ai_canonname &&
				strcmp(res->ai_canonname, "netlib-test.xeneva") == 0 &&
				((sockaddr_in*)res->ai_addr)->sin_port == htons(443),
			rc, 0);
		if (res) {
			freeaddrinfo(res);
			res = NULL;
		}

		hints.ai_flags = AI_NUMERICSERV;
		rc = getaddrinfo("netlib-test.xeneva", "http", &hints, &res);
		rec("netlib.getaddrinfo.numericserv",
			rc == EAI_SERVICE && res == NULL, rc, 0);

		hints.ai_flags = AI_NUMERICHOST;
		rc = getaddrinfo("netlib-test.xeneva", "80", &hints, &res);
		rec("netlib.getaddrinfo.numerichost",
			rc == EAI_NONAME && res == NULL, rc, 0);

		hints.ai_flags = AI_NUMERICHOST;
		rc = getaddrinfo("127.0.0.53", "80", &hints, &res);
		rec("netlib.getaddrinfo.numerichost.ok",
			rc == 0 && res && res->ai_family == AF_INET &&
				((sockaddr_in*)res->ai_addr)->sin_port == htons(80),
			rc, 0);
		if (res) {
			freeaddrinfo(res);
			res = NULL;
		}

		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_PASSIVE;
		rc = getaddrinfo(NULL, "http", &hints, &res);
		rec("netlib.getaddrinfo.passive",
			rc == 0 && res && res->ai_family == AF_INET &&
				((sockaddr_in*)res->ai_addr)->sin_addr.s_addr == 0 &&
				((sockaddr_in*)res->ai_addr)->sin_port == htons(80),
			rc, 0);
		if (res) {
			freeaddrinfo(res);
			res = NULL;
		}

		hints.ai_flags = 0;
		rc = getaddrinfo(NULL, "80", &hints, &res);
		rec("netlib.getaddrinfo.loopback-default",
			rc == 0 && res && res->ai_family == AF_INET &&
				((sockaddr_in*)res->ai_addr)->sin_addr.s_addr ==
					inet_addr("127.0.0.1"),
			rc, 0);
		if (res) {
			freeaddrinfo(res);
			res = NULL;
		}

		hints.ai_flags = 0x1000;
		rc = getaddrinfo("127.0.0.1", "80", &hints, &res);
		rec("netlib.getaddrinfo.badflags", rc == EAI_BADFLAGS, rc, 0);

		hints.ai_flags = 0;
		hints.ai_family = 999;
		rc = getaddrinfo("127.0.0.1", "80", &hints, &res);
		rec("netlib.getaddrinfo.badfamily", rc == EAI_FAMILY, rc, 0);

		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET6;
		rc = getaddrinfo("netlib6.xeneva.test", "80", &hints, &res);
		rec("netlib.getaddrinfo.v6",
			rc == 0 && res && res->ai_family == AF_INET6 &&
				((sockaddr_in6*)res->ai_addr)->sin6_port == htons(80),
			rc, 0);
		if (res) {
			freeaddrinfo(res);
			res = NULL;
		}

		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		rc = getaddrinfo("no-such-host.localhost", "80", &hints, &res);
		rec("netlib.getaddrinfo.miss", rc == EAI_NONAME && res == NULL, rc, 0);

		rc = getaddrinfo("127.0.0.1", "99999", &hints, &res);
		rec("netlib.getaddrinfo.badservice", rc == EAI_SERVICE, rc, 0);

		hints.ai_socktype = 999;
		rc = getaddrinfo("127.0.0.1", "80", &hints, &res);
		rec("netlib.getaddrinfo.badsocktype",
			rc == EAI_SOCKTYPE && res == NULL, rc, 0);

		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_DGRAM;
		rc = getaddrinfo("127.0.0.1", "domain", &hints, &res);
		rec("netlib.getaddrinfo.udp",
			rc == 0 && res && res->ai_socktype == SOCK_DGRAM &&
				res->ai_protocol == IPPROTO_UDP &&
				((sockaddr_in*)res->ai_addr)->sin_port == htons(53),
			rc, 0);
		if (res) {
			freeaddrinfo(res);
			res = NULL;
		}

		rec("netlib.gai_strerror",
			gai_strerror(0) && gai_strerror(EAI_NONAME) &&
				gai_strerror(EAI_OVERFLOW) && gai_strerror(EAI_SOCKTYPE),
			0, 0);
	}
}


int main(int argc, char** argv) {
	int poweroff = 0;

	for (int i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--poweroff") == 0)
			poweroff = 1;
		else if (strcmp(argv[i], "--no-real") == 0)
			g_real_enabled = 0;
		else if (!strcmp(argv[i], "--speed-host") && i + 1 < argc)
			g_speed_host = argv[++i];
		else if (!strcmp(argv[i], "--speed-port") && i + 1 < argc)
			g_speed_port = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--speed-mib") && i + 1 < argc)
			g_speed_mib = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--speed-reps") && i + 1 < argc)
			g_speed_reps = atoi(argv[++i]);
	}

	g_suite_start = now_us();
	_KePrint("[netsystest] native socket suite start\r\n");

	test_options();
	test_tcp_blocking();
	test_tcp_nonblock();
	test_udp();
	test_unix();
	test_socketpair();
	test_shutdown();
	test_names();
	test_mmsg();
	test_ifaddrs();
	test_poll();
	test_ipv6_tcp();
	test_errors();
	test_so_error();
	test_inet_pton6();
	test_netlibs();
	test_real_network();
	test_https();
	test_perf_tcp_1m();
	test_perf_connect();
	test_perf_udp_mmsg();
	test_perf_poll();
	test_perf_host_speed();

	_KePrint("[netsystest] RESULT pass=%d fail=%d skip=%d total=%d us\r\n",
			 g_pass, g_fail, g_skip,
			 (int)(now_us() - g_suite_start));
	if (poweroff) {
		_KeProcessSleep(250);
		_KePowerDown();
	}
	return g_fail ? 1 : 0;
}
