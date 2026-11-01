/* Minimal TCP throughput probe.
 *
 *   bench <host> <port> <bytes>      receive (client)
 *   bench -s <host> <port> <bytes>   send (client)
 *   bench -l <port> <bytes>          send (server)
 *   bench -r <port> <bytes>          receive (server)
 *
 * The receive paths use a 64 KiB buffer, matching Process/netsystest's host
 * speed test, so the guest-side syscall pattern is comparable.
 */
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define CHUNK (64 * 1024)

static double now_s(void) {
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static int listen_on(int port) {
	int fd;
	int one = 1;
	struct sockaddr_in a;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(INADDR_ANY);
	a.sin_port = htons((uint16_t)port);
	if (bind(fd, (struct sockaddr*)&a, sizeof(a)) != 0 ||
	    listen(fd, 4) != 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static int connect_to(const char* host, int port) {
	int fd;
	struct sockaddr_in a;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = htons((uint16_t)port);
	if (inet_pton(AF_INET, host, &a.sin_addr) != 1) {
		close(fd);
		return -1;
	}
	if (connect(fd, (struct sockaddr*)&a, sizeof(a)) != 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static uint64_t do_send(int fd, uint64_t total) {
	char* buf = malloc(CHUNK);
	uint64_t sent = 0;
	double t0, dt;

	if (!buf)
		return 0;
	memset(buf, 0x5a, CHUNK);
	t0 = now_s();
	while (sent < total) {
		size_t want = total - sent > CHUNK ? CHUNK : (size_t)(total - sent);
		ssize_t n = send(fd, buf, want, 0);
		if (n <= 0) {
			if (n < 0 && errno == EINTR)
				continue;
			break;
		}
		sent += (uint64_t)n;
	}
	dt = now_s() - t0;
	printf("sent %llu bytes in %.6f s = %.1f KiB/s (%.1f Mbit/s)\n",
	       (unsigned long long)sent, dt,
	       dt > 0 ? (double)sent / 1024.0 / dt : 0.0,
	       dt > 0 ? (double)sent * 8.0 / dt / 1e6 : 0.0);
	free(buf);
	return sent;
}

static uint64_t do_recv(int fd, uint64_t total) {
	char* buf = malloc(CHUNK);
	uint64_t got = 0;
	double t0, dt;

	if (!buf)
		return 0;
	t0 = now_s();
	while (got < total) {
		ssize_t n = recv(fd, buf, CHUNK, 0);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (n == 0)
			break;
		got += (uint64_t)n;
	}
	dt = now_s() - t0;
	printf("got %llu bytes in %.6f s = %.1f KiB/s (%.1f Mbit/s)\n",
	       (unsigned long long)got, dt,
	       dt > 0 ? (double)got / 1024.0 / dt : 0.0,
	       dt > 0 ? (double)got * 8.0 / dt / 1e6 : 0.0);
	free(buf);
	return got;
}

int main(int argc, char** argv) {
	int port;
	uint64_t total;
	const char* host = NULL;
	int send = 0;
	int server = 0;

	if (argc >= 2 && (strcmp(argv[1], "-l") == 0 ||
			  strcmp(argv[1], "-r") == 0)) {
		if (argc < 4) {
			fprintf(stderr, "usage: %s %s <port> <bytes>\n",
				argv[0], argv[1]);
			return 2;
		}
		server = 1;
		send = strcmp(argv[1], "-l") == 0;
		port = atoi(argv[2]);
		total = strtoull(argv[3], NULL, 0);
	} else if (argc >= 2 && strcmp(argv[1], "-s") == 0) {
		if (argc < 5) {
			fprintf(stderr, "usage: %s -s <host> <port> <bytes>\n",
				argv[0]);
			return 2;
		}
		host = argv[2];
		port = atoi(argv[3]);
		total = strtoull(argv[4], NULL, 0);
		send = 1;
	} else {
		if (argc < 4) {
			fprintf(stderr, "usage: %s <host> <port> <bytes>\n",
				argv[0]);
			return 2;
		}
		host = argv[1];
		port = atoi(argv[2]);
		total = strtoull(argv[3], NULL, 0);
	}

	if (server) {
		int lfd = listen_on(port);
		int cfd;
		uint64_t done;

		if (lfd < 0) {
			perror("listen");
			return 1;
		}
		cfd = accept(lfd, NULL, NULL);
		if (cfd < 0) {
			perror("accept");
			return 1;
		}
		done = send ? do_send(cfd, total) : do_recv(cfd, total);
		close(cfd);
		close(lfd);
		return done == total ? 0 : 1;
	}

	{
		int fd = connect_to(host, port);
		uint64_t done;

		if (fd < 0) {
			perror("connect");
			return 1;
		}
		done = send ? do_send(fd, total) : do_recv(fd, total);
		close(fd);
		return done == total ? 0 : 1;
	}
}
