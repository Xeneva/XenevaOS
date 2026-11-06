/**
 * Host microbench for the TCP RX ring copy.
 *
 * Links KernelAA64/circbuf.c. The RX write/read path matches
 * KernelAA64/Net/tcp.c (TCPWriteRx / AuTCPReceive) as selected
 * by -DTCP_USES_BULK.
 *
 * Built by Tests/bench_compare.sh.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include "circbuf.h"

#ifndef TCP_RX_BUF_SZ
#define TCP_RX_BUF_SZ 16384
#endif

#ifndef TCP_MSS
#define TCP_MSS 1460
#endif

#define RECV_SZ    4096
#define ITERATIONS 50000
#define WARMUP     200
#define NOINLINE   __attribute__((noinline))

void* kmalloc(unsigned int n) {
	return malloc(n ? n : 1);
}

void kfree(void* p) {
	free(p);
}

/* Same copy as KernelAA64/Net/tcp.c TCPWriteRx / AuTCPReceive. */
static NOINLINE int tcp_write_rx(CircBuffer* buf, const uint8_t* data, size_t len) {
	if (!buf || !data || !len)
		return 0;
#ifdef TCP_USES_BULK
	return (int)AuCircBufWrite(buf, data, len);
#else
	{
		size_t i;
		for (i = 0; i < len; i++) {
			if (AuCircBufPut(buf, data[i]) != 0)
				return (int)i;
		}
		return (int)len;
	}
#endif
}

static NOINLINE int tcp_receive(CircBuffer* buf, uint8_t* dest, size_t want) {
	if (!buf)
		return -1;
	if (!dest || want == 0)
		return 0;
	if (CircBufEmpty(buf))
		return -1;
#ifdef TCP_USES_BULK
	return (int)AuCircBufRead(buf, dest, want);
#else
	{
		size_t got = 0;
		while (got < want && !CircBufEmpty(buf)) {
			if (AuCircBufGet(buf, dest + got) != 0)
				break;
			got++;
		}
		return (int)got;
	}
#endif
}

static uint8_t src[TCP_RX_BUF_SZ];
static uint8_t dst[TCP_RX_BUF_SZ];
static volatile uint64_t sink;

static double now_ns(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static CircBuffer* make_rx(uint8_t* mem) {
	memset(mem, 0, TCP_RX_BUF_SZ);
	return AuCircBufInitialise(mem, TCP_RX_BUF_SZ);
}

static int fail(const char* msg) {
	fprintf(stderr, "FAIL: %s\n", msg);
	return 1;
}

static int check_roundtrip(size_t prefill, size_t n) {
	uint8_t* mem = malloc(TCP_RX_BUF_SZ);
	CircBuffer* buf;
	int rc = 0;

	if (!mem)
		return fail("malloc");
	buf = make_rx(mem);
	memset(dst, 0x5A, n);

	if (prefill) {
		if (tcp_write_rx(buf, src, prefill) != (int)prefill)
			rc = fail("prefill write");
		else if (tcp_receive(buf, dst, prefill) != (int)prefill)
			rc = fail("prefill drain");
	}
	if (!rc && tcp_write_rx(buf, src, n) != (int)n)
		rc = fail("write short");
	if (!rc && tcp_receive(buf, dst, n) != (int)n)
		rc = fail("read short");
	if (!rc && memcmp(src, dst, n) != 0)
		rc = fail("data mismatch");
	if (!rc && !CircBufEmpty(buf))
		rc = fail("ring not empty");

	AuCircBufFree(buf);
	free(mem);
	return rc;
}

static int run_correctness(void) {
	size_t sizes[] = { 1, 64, 256, 1460, 4096, TCP_RX_BUF_SZ };
	size_t s;

	for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
		if (check_roundtrip(0, sizes[s])) {
			fprintf(stderr, "  linear %zu\n", sizes[s]);
			return 1;
		}
		if (sizes[s] < TCP_RX_BUF_SZ &&
		    check_roundtrip(TCP_RX_BUF_SZ - 17, sizes[s])) {
			fprintf(stderr, "  wrap %zu\n", sizes[s]);
			return 1;
		}
	}
	printf("correctness: ok\n");
	return 0;
}

static double time_aligned(size_t n, int iters) {
	uint8_t* mem = malloc(TCP_RX_BUF_SZ);
	CircBuffer* buf = make_rx(mem);
	int i;
	double t0, ns;

	for (i = 0; i < WARMUP; i++) {
		tcp_write_rx(buf, src, n);
		tcp_receive(buf, dst, n);
		sink += dst[0] + dst[n - 1];
	}
	AuCircBufFree(buf);
	buf = make_rx(mem);

	t0 = now_ns();
	for (i = 0; i < iters; i++) {
		tcp_write_rx(buf, src, n);
		tcp_receive(buf, dst, n);
		sink += dst[0] + dst[n - 1];
	}
	ns = (now_ns() - t0) / iters;
	AuCircBufFree(buf);
	free(mem);
	return ns;
}

static double time_wrap(size_t n, int iters) {
	uint8_t* mem = malloc(TCP_RX_BUF_SZ);
	CircBuffer* buf = make_rx(mem);
	size_t park = TCP_RX_BUF_SZ - (n / 2);
	int i;
	double t0, ns;

	if (park == 0 || n / 2 == 0)
		park = TCP_RX_BUF_SZ - 1;

	for (i = 0; i < WARMUP; i++) {
		buf->head = park;
		buf->tail = park;
		buf->full = false;
		tcp_write_rx(buf, src, n);
		tcp_receive(buf, dst, n);
		sink += dst[0] + dst[n - 1];
	}
	t0 = now_ns();
	for (i = 0; i < iters; i++) {
		buf->head = park;
		buf->tail = park;
		buf->full = false;
		tcp_write_rx(buf, src, n);
		tcp_receive(buf, dst, n);
		sink += dst[0] + dst[n - 1];
	}
	ns = (now_ns() - t0) / iters;
	AuCircBufFree(buf);
	free(mem);
	return ns;
}

/* NIC posts MSS segments; user recv()s 4 KiB. Ring stays occupied and wraps. */
static double time_tcp_like(int iters) {
	uint8_t* mem = malloc(TCP_RX_BUF_SZ);
	CircBuffer* buf = make_rx(mem);
	int i;
	double t0, ns;

	for (i = 0; i < WARMUP; i++) {
		if (CircBufFull(buf) || (TCP_RX_BUF_SZ - AuCircBufSize(buf)) < TCP_MSS)
			tcp_receive(buf, dst, RECV_SZ);
		tcp_write_rx(buf, src, TCP_MSS);
		if (AuCircBufSize(buf) >= RECV_SZ)
			tcp_receive(buf, dst, RECV_SZ);
		sink += dst[0];
	}
	AuCircBufFree(buf);
	buf = make_rx(mem);

	t0 = now_ns();
	for (i = 0; i < iters; i++) {
		int w, r = 0;
		if (CircBufFull(buf) || (TCP_RX_BUF_SZ - AuCircBufSize(buf)) < TCP_MSS)
			tcp_receive(buf, dst, RECV_SZ);
		w = tcp_write_rx(buf, src, TCP_MSS);
		if (AuCircBufSize(buf) >= RECV_SZ)
			r = tcp_receive(buf, dst, RECV_SZ);
		sink += dst[0] + (unsigned)w + (unsigned)r;
	}
	while (!CircBufEmpty(buf))
		tcp_receive(buf, dst, RECV_SZ);
	ns = (now_ns() - t0) / iters;
	AuCircBufFree(buf);
	free(mem);
	return ns;
}

int main(void) {
	int sizes[] = { 64, 256, 512, 1024, 1460 };
	int s;
	double tcp_ns;

	for (s = 0; s < TCP_RX_BUF_SZ; s++)
		src[s] = (uint8_t)(s * 131u + 17u);

#ifdef TCP_USES_BULK
	printf("tcp_rx: AuCircBufWrite/Read (bulk)\n");
#else
	printf("tcp_rx: AuCircBufPut/Get (byte)\n");
#endif
	printf("ring=%d  mss=%d  recv=%d  iters=%d\n\n",
	       TCP_RX_BUF_SZ, TCP_MSS, RECV_SZ, ITERATIONS);

	if (run_correctness())
		return 1;

	printf("\n%-22s  %s\n", "Case", "ns/op");
	printf("----------------------  ----------\n");
	for (s = 0; s < (int)(sizeof(sizes) / sizeof(sizes[0])); s++) {
		char label[32];
		snprintf(label, sizeof(label), "aligned %dB", sizes[s]);
		printf("%-22s  %10.1f\n", label, time_aligned((size_t)sizes[s], ITERATIONS));
	}
	printf("\n");
	for (s = 0; s < (int)(sizeof(sizes) / sizeof(sizes[0])); s++) {
		char label[32];
		snprintf(label, sizeof(label), "wrap %dB", sizes[s]);
		printf("%-22s  %10.1f\n", label, time_wrap((size_t)sizes[s], ITERATIONS));
	}
	tcp_ns = time_tcp_like(ITERATIONS);
	printf("\n%-22s  %10.1f\n", "tcp-like 1460/4096", tcp_ns);
	printf("\nRESULT tcp_like_ns=%.1f\n", tcp_ns);
	(void)sink;
	return 0;
}
