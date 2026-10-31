/**
* BSD 2-Clause License
*
* Copyright (c) 2026, Manas Kamal Choudhury
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice, this
*    list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following disclaimer in the documentation
*    and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
**/

/*
 * HTTP/1.1 GET and HEAD over XEClib TCP, with TLS 1.2/1.3 when the URL
 * is https. Certificate chain and hostname are checked. ALPN is http/1.1.
 * xe_http_get_many runs several GETs at once on this same thread.
 */

#include "https.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/netdb.h>
#include <arpa/inet.h>
#include <sys/_kefile.h>
#include <sys/_keproc.h>
#include <sys/_ketime.h>

#include "mbedtls/build_info.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"

#include <zlib.h>
#include <brotli/decode.h>

extern "C" {
extern const unsigned char xe_cacert_pem[];
extern const unsigned int xe_cacert_pem_len;
}

/* XEClib exports receive(), not a C recv(). */
static int xe_recv(int fd, void* buf, size_t len) {
	iovec iov;
	msghdr hdr;
	iov.iov_base = buf;
	iov.iov_len = len;
	hdr.msg_name = NULL;
	hdr.msg_namelen = 0;
	hdr.msg_iov = &iov;
	hdr.msg_iovlen = 1;
	hdr.msg_control = NULL;
	hdr.msg_controllen = 0;
	hdr.msg_flags = 0;
	return receive(fd, &hdr, 0);
}

#define XE_CLOCK_MIN 1577836800 /* 2020-01-01; epoch means the clock was never set */
#define XE_IDLE_MAX 150         /* 100 ms sleeps: 15 s */
#define XE_HDR_RAW 8192
#define XE_MAX_REDIRECTS 5

struct xe_url {
	int https;
	char host[256];
	char path[3072];
	uint16_t port;
};

struct xe_tls_slot {
	mbedtls_ssl_context ssl;
	mbedtls_ssl_config conf;
};

struct xe_reader {
	int tls;
	int resumed;
	int fd;
	/* When set, a recv or send that would wait returns WANT_* so another
	 * socket on this thread can run. The blocking path leaves it clear. */
	int yield;
	int progressed;
	/* Set when this step actually read bytes. A write alone must not
	 * refresh the idle deadline: a socket that only retransmits would
	 * otherwise stay open forever and hold the rest of the batch. */
	int got_read;
	mbedtls_ssl_context* ssl;
	struct xe_tls_slot* slot;
	unsigned char buf[2048];
	int len;
	int pos;
	int eof;
	int err;
};

static mbedtls_entropy_context g_entropy;
static mbedtls_ctr_drbg_context g_drbg;
static mbedtls_x509_crt g_cacert;
static int g_rng_ready;
static int g_ca_ready;
static char g_referer[2048];
static char* g_req_body = NULL;
static size_t g_req_body_len = 0;
static char g_req_ctype[80];
static char g_last_url[3072];
static char g_ca_source[64] = "uninitialized";

#define XE_CA_MAX (512 * 1024)

#define XE_TLS_SESSION_MAX 8
#define XE_TLS_SESSION_TTL 3600

struct xe_tls_session {
	int used;
	char host[256];
	int64_t expires;
	mbedtls_ssl_session session;
};

static struct xe_tls_session g_sessions[XE_TLS_SESSION_MAX];
static int g_sessions_ready;
static int g_tls_full;
static int g_tls_resumed;

static void set_err(xe_http_response* out, const char* msg) {
	snprintf(out->error, sizeof(out->error), "%s", msg);
}

void xe_http_response_free(xe_http_response* out) {
	if (!out)
		return;
	free(out->body);
	out->body = NULL;
	out->body_len = 0;
}

static int ensure_rng(xe_http_response* out) {
	int ret;
	if (g_rng_ready)
		return 0;
	mbedtls_entropy_init(&g_entropy);
	mbedtls_ctr_drbg_init(&g_drbg);
	ret = mbedtls_ctr_drbg_seed(&g_drbg, mbedtls_entropy_func, &g_entropy,
								(const unsigned char*)"xeneva-https", 12);
	if (ret != 0) {
		mbedtls_ctr_drbg_free(&g_drbg);
		mbedtls_entropy_free(&g_entropy);
		set_err(out, "entropy unavailable");
		return -1;
	}
	g_rng_ready = 1;
	return 0;
}

static unsigned char* ca_read_file(const char* path, size_t* len) {
	int fd;
	unsigned char* buf;
	size_t got = 0;

	fd = _KeOpenFile((char*)path, FILE_OPEN_READ_ONLY);
	if (fd < 0)
		return NULL;
	buf = (unsigned char*)malloc(XE_CA_MAX);
	if (!buf) {
		_KeCloseFile(fd);
		return NULL;
	}
	while (got < XE_CA_MAX - 1) {
		size_t room = XE_CA_MAX - 1 - got;
		size_t n = _KeReadFile(fd, buf + got, room);
		/* A failed read comes back as a negative int in a size_t. */
		if (n == 0 || n > room)
			break;
		got += n;
	}
	_KeCloseFile(fd);
	if (got == 0 || got == XE_CA_MAX - 1) {
		free(buf);
		return NULL;
	}
	buf[got] = 0;
	*len = got + 1;
	return buf;
}

static int ensure_ca(xe_http_response* out) {
	static const char* const paths[] = {
		"/etc/ssl/certs/cacert.pem",
		"/cacert.pem",
		NULL
	};
	int ret;
	int i;

	if (g_ca_ready)
		return 0;
	mbedtls_x509_crt_init(&g_cacert);
	for (i = 0; paths[i]; i++) {
		size_t len = 0;
		unsigned char* pem = ca_read_file(paths[i], &len);
		if (!pem)
			continue;
		ret = mbedtls_x509_crt_parse(&g_cacert, pem, len);
		free(pem);
		if (ret >= 0 && g_cacert.raw.p != NULL) {
			snprintf(g_ca_source, sizeof(g_ca_source), "file:%s", paths[i]);
			g_ca_ready = 1;
			return 0;
		}
		mbedtls_x509_crt_free(&g_cacert);
		mbedtls_x509_crt_init(&g_cacert);
	}
	ret = mbedtls_x509_crt_parse(&g_cacert, xe_cacert_pem, xe_cacert_pem_len);
	if (ret < 0 || g_cacert.raw.p == NULL) {
		set_err(out, "trust anchors unusable");
		return -1;
	}
	snprintf(g_ca_source, sizeof(g_ca_source), "embedded");
	g_ca_ready = 1;
	return 0;
}

const char* xe_http_ca_source(void) {
	return g_ca_source;
}

static int parse_url(const char* url, struct xe_url* u) {
	const char* p = url;
	const char* slash;
	const char* colon;
	size_t hlen;
	memset(u, 0, sizeof(*u));
	u->port = 80;
	strcpy(u->path, "/");
	if (!p || !p[0])
		return -1;
	if (!strncmp(p, "https://", 8)) {
		u->https = 1;
		u->port = 443;
		p += 8;
	} else if (!strncmp(p, "http://", 7)) {
		p += 7;
	}
	if (p[0] == '[')
		return -2;
	slash = strchr(p, '/');
	colon = strchr(p, ':');
	if (colon && (!slash || colon < slash)) {
		const char* d = colon + 1;
		int prt = 0;
		hlen = (size_t)(colon - p);
		while (*d >= '0' && *d <= '9') {
			prt = prt * 10 + (*d - '0');
			d++;
		}
		if (prt <= 0 || prt > 65535)
			return -1;
		u->port = (uint16_t)prt;
		slash = (*d == '/') ? d : NULL;
	} else {
		hlen = slash ? (size_t)(slash - p) : strlen(p);
	}
	if (hlen == 0 || hlen >= sizeof(u->host))
		return -1;
	memcpy(u->host, (void*)p, hlen);
	u->host[hlen] = 0;
	if (slash && slash[0]) {
		if (strlen(slash) >= sizeof(u->path))
			return -1;
		strcpy(u->path, slash);
	}
	return 0;
}

static void format_abs(const struct xe_url* u, const char* path, char* dst, size_t n) {
	int deflt = (u->https && u->port == 443) || (!u->https && u->port == 80);
	if (deflt)
		snprintf(dst, n, "%s://%s%s", u->https ? "https" : "http", u->host, path);
	else
		snprintf(dst, n, "%s://%s:%u%s", u->https ? "https" : "http", u->host, u->port, path);
}

static int join_location(const struct xe_url* cur, const char* loc, char* dst, size_t n) {
	if (!strncmp(loc, "https://", 8) || !strncmp(loc, "http://", 7)) {
		snprintf(dst, n, "%s", loc);
		return 0;
	}
	if (loc[0] == '/' && loc[1] == '/') {
		snprintf(dst, n, "%s:%s", cur->https ? "https" : "http", loc);
		return 0;
	}
	if (loc[0] == '/') {
		format_abs(cur, loc, dst, n);
		return 0;
	}
	{
		char base[1024];
		char joined[1200];
		const char* slash = strrchr(cur->path, '/');
		size_t dir = slash ? (size_t)(slash - cur->path + 1) : 1;
		if (dir >= sizeof(base))
			return -1;
		memcpy(base, (void*)cur->path, dir);
		base[dir] = 0;
		snprintf(joined, sizeof(joined), "%s%s", base, loc);
		format_abs(cur, joined, dst, n);
		return 0;
	}
}

static int io_read(struct xe_reader* r, unsigned char* buf, size_t len) {
	int idle = 0;
	if (r->yield) {
		int n;
		int spins = 0;
		for (;;) {
			if (r->tls)
				n = mbedtls_ssl_read(r->ssl, buf, len);
			else
				n = xe_recv(r->fd, buf, len);
			if (n > 0) {
				r->progressed = 1;
				return n;
			}
			if (n == 0 || n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
				return 0;
			if (r->tls && n == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) {
				if (++spins < 4)
					continue;
				return n;
			}
			if (r->tls && (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE))
				return n;
			if (!r->tls && n < 0)
				return MBEDTLS_ERR_SSL_WANT_READ;
			return n;
		}
	}
	while (idle < XE_IDLE_MAX) {
		int n;
		if (r->tls)
			n = mbedtls_ssl_read(r->ssl, buf, len);
		else
			n = xe_recv(r->fd, buf, len);
		if (n > 0)
			return n;
		if (n == 0 || n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
			return 0;
		if (r->tls && n != MBEDTLS_ERR_SSL_WANT_READ && n != MBEDTLS_ERR_SSL_WANT_WRITE &&
			n != MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET && n != MBEDTLS_ERR_SSL_TIMEOUT)
			return n;
		_KeProcessSleep(100);
		idle++;
	}
	return MBEDTLS_ERR_SSL_TIMEOUT;
}

static int io_write_all(struct xe_reader* r, const unsigned char* buf, size_t len) {
	size_t off = 0;
	int idle = 0;
	while (off < len && idle < XE_IDLE_MAX) {
		int n;
		if (r->tls)
			n = mbedtls_ssl_write(r->ssl, buf + off, len - off);
		else
			n = (int)sendto(r->fd, buf + off, len - off, 0, NULL, 0);
		if (n > 0) {
			off += (size_t)n;
			idle = 0;
			continue;
		}
		if (r->tls && n != MBEDTLS_ERR_SSL_WANT_READ && n != MBEDTLS_ERR_SSL_WANT_WRITE)
			return -1;
		_KeProcessSleep(100);
		idle++;
	}
	return off == len ? 0 : -1;
}

static int tls_send(void* ctx, const unsigned char* buf, size_t len) {
	struct xe_reader* r = (struct xe_reader*)ctx;
	int idle = 0;
	if (r->yield) {
		int n = (int)sendto(r->fd, buf, len, 0, NULL, 0);
		if (n > 0) {
			r->progressed = 1;
			return n;
		}
		/* 0 means the send window took nothing. A hard error fails the handshake. */
		if (n == 0)
			return MBEDTLS_ERR_SSL_WANT_WRITE;
		return MBEDTLS_ERR_SSL_CONN_EOF;
	}
	while (idle < XE_IDLE_MAX) {
		int n = (int)sendto(r->fd, buf, len, 0, NULL, 0);
		if (n > 0)
			return n;
		/* 0 is "nothing queued" (send window or a neighbor lookup), not
		 * a peer close. Treating it as EOF aborted the first handshake
		 * of a boot before the SYN retry had a cached route. */
		_KeProcessSleep(100);
		idle++;
	}
	return MBEDTLS_ERR_SSL_TIMEOUT;
}

static int tls_recv(void* ctx, unsigned char* buf, size_t len) {
	struct xe_reader* r = (struct xe_reader*)ctx;
	int idle = 0;
	if (r->yield) {
		int n = xe_recv(r->fd, buf, len);
		if (n > 0) {
			r->progressed = 1;
			r->got_read = 1;
			return n;
		}
		if (n == 0)
			return 0;
		return MBEDTLS_ERR_SSL_WANT_READ;
	}
	while (idle < XE_IDLE_MAX) {
		int n = xe_recv(r->fd, buf, len);
		if (n > 0)
			return n;
		if (n == 0)
			return 0;
		_KeProcessSleep(100);
		idle++;
	}
	return MBEDTLS_ERR_SSL_TIMEOUT;
}

static int rd_fill(struct xe_reader* r) {
	int n;
	if (r->pos < r->len)
		return 0;
	if (r->eof)
		return 0;
	n = io_read(r, r->buf, sizeof(r->buf));
	if (n == 0) {
		r->eof = 1;
		return 0;
	}
	if (n < 0) {
		r->err = 1;
		return -1;
	}
	r->len = n;
	r->pos = 0;
	return 0;
}

static int rd_copy(struct xe_reader* r, unsigned char* dst, size_t n, size_t* got) {
	*got = 0;
	while (*got < n) {
		size_t have;
		size_t take;
		if (rd_fill(r) < 0)
			return -1;
		if (r->pos >= r->len)
			return 0;
		have = (size_t)(r->len - r->pos);
		take = n - *got;
		if (take > have)
			take = have;
		if (dst)
			memcpy(dst + *got, r->buf + r->pos, take);
		r->pos += (int)take;
		*got += take;
	}
	return 0;
}

static int rd_line(struct xe_reader* r, char* dst, size_t dstsz) {
	size_t i = 0;
	if (dstsz)
		dst[0] = 0;
	for (;;) {
		size_t one = 0;
		unsigned char c;
		if (rd_copy(r, &c, 1, &one) < 0)
			return -1;
		if (one == 0)
			return -1;
		if (c == '\n') {
			if (i < dstsz)
				dst[i] = 0;
			return 0;
		}
		if (c != '\r' && i + 1 < dstsz)
			dst[i++] = (char)c;
	}
}

static int hex_len(const char* s, size_t* out) {
	size_t v = 0;
	int any = 0;
	if (!s)
		return -1;
	while (*s == ' ' || *s == '\t')
		s++;
	while ((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'f') || (*s >= 'A' && *s <= 'F')) {
		int d = (*s >= '0' && *s <= '9') ? *s - '0' :
				(*s >= 'a' && *s <= 'f') ? *s - 'a' + 10 : *s - 'A' + 10;
		v = (v << 4) + (size_t)d;
		any = 1;
		s++;
	}
	if (!any)
		return -1;
	*out = v;
	return 0;
}

static const char* hdr_value(const char* hdr, const char* key) {
	size_t klen = strlen(key);
	const char* p = hdr;
	while (p && *p) {
		if (strncasecmp(p, key, klen) == 0 && p[klen] == ':') {
			p += klen + 1;
			while (*p == ' ' || *p == '\t')
				p++;
			return p;
		}
		p = strstr(p, "\r\n");
		if (!p)
			break;
		p += 2;
	}
	return NULL;
}

/* Connection: close, or "Keep-Alive, close". A bare substring would match
 * inside another token. */
static int hdr_has_token(const char* v, const char* tok) {
	size_t n;
	if (!v || !tok)
		return 0;
	n = strlen(tok);
	while (*v) {
		while (*v == ' ' || *v == '\t' || *v == ',')
			v++;
		if (!*v)
			break;
		if (strncasecmp(v, tok, n) == 0 &&
			(v[n] == 0 || v[n] == ',' || v[n] == ' ' || v[n] == '\t' || v[n] == '\r'))
			return 1;
		while (*v && *v != ',')
			v++;
	}
	return 0;
}

static void copy_token(const char* src, char* dst, size_t n) {
	size_t i = 0;
	while (src[i] && src[i] != '\r' && src[i] != '\n' && i + 1 < n) {
		dst[i] = src[i];
		i++;
	}
	dst[i] = 0;
}

/* Content-Encoding values this client can undo. Unknown encodings are left
 * alone rather than turning a readable response into a failure. */
enum {
	XE_ENC_NONE = 0,
	XE_ENC_GZIP,
	XE_ENC_DEFLATE,
	XE_ENC_BR
};

/* A compressed body is usually smaller than the decoded one. Keep enough
 * raw input for incompressible content and for the framing overhead. */
#define XE_DECODE_SLACK (128 * 1024)

static int http_content_encoding(const char* hdr) {
	const char* v = hdr_value(hdr, "Content-Encoding");
	if (!v)
		return XE_ENC_NONE;
	while (*v == ' ' || *v == '\t')
		v++;
	if (strncasecmp(v, "gzip", 4) == 0)
		return XE_ENC_GZIP;
	if (strncasecmp(v, "x-gzip", 6) == 0)
		return XE_ENC_GZIP;
	if (strncasecmp(v, "deflate", 7) == 0)
		return XE_ENC_DEFLATE;
	if (strncasecmp(v, "br", 2) == 0)
		return XE_ENC_BR;
	return XE_ENC_NONE;
}

static size_t decode_raw_cap(size_t max_body) {
	size_t cap = max_body ? max_body : 1;
	if (cap > (size_t)-1 - XE_DECODE_SLACK)
		return cap;
	return cap + XE_DECODE_SLACK;
}

/* Z_SOLO removes zlib's default allocator hooks; inflateInit2 rejects a
 * stream whose zalloc/zfree are NULL. */
static voidpf xe_zlib_alloc(voidpf opaque, uInt items, uInt size) {
	(void)opaque;
	return malloc((size_t)items * (size_t)size);
}

static void xe_zlib_free(voidpf opaque, voidpf address) {
	(void)opaque;
	free(address);
}

/* One zlib pass. wbits selects zlib, raw deflate, or gzip framing. */
static int inflate_try(const unsigned char* in, size_t inlen, int wbits,
					   unsigned char* dst, size_t cap, size_t* outlen) {
	z_stream zs;
	int ret;
	int done = 0;

	memset(&zs, 0, sizeof(zs));
	zs.zalloc = xe_zlib_alloc;
	zs.zfree = xe_zlib_free;
	zs.opaque = NULL;
	zs.next_in = (Bytef*)in;
	zs.avail_in = (uInt)inlen;
	zs.next_out = dst;
	zs.avail_out = (uInt)cap;
	ret = inflateInit2(&zs, wbits);
	if (ret != Z_OK) {
		_KePrint("[zlib] init wbits=%d ret=%d\n", wbits, ret);
		return -1;
	}
	for (;;) {
		ret = inflate(&zs, Z_NO_FLUSH);
		if (ret == Z_STREAM_END) {
			done = 1;
			break;
		}
		if (ret == Z_OK) {
			if (zs.avail_out == 0 || zs.avail_in == 0)
				break;
			continue;
		}
		if (ret == Z_BUF_ERROR)
			break;
		break;
	}
	*outlen = cap - zs.avail_out;
	inflateEnd(&zs);
	/* A truncated stream is still useful up to the cap; a hard format error
	 * is not. */
	if (!done && ret != Z_OK && ret != Z_BUF_ERROR) {
		_KePrint("[zlib] inflate wbits=%d ret=%d in=%d out=%d\n", wbits, ret,
				 (int)inlen, (int)*outlen);
		return -1;
	}
	return 0;
}

static int decode_gzip_deflate(const unsigned char* in, size_t inlen, int gzip,
							   unsigned char* dst, size_t cap, size_t* outlen) {
	if (gzip)
		return inflate_try(in, inlen, 16 + MAX_WBITS, dst, cap, outlen);
	if (inflate_try(in, inlen, MAX_WBITS, dst, cap, outlen) == 0)
		return 0;
	/* RFC 9110 says zlib framing, but some servers send raw deflate. */
	return inflate_try(in, inlen, -MAX_WBITS, dst, cap, outlen);
}

static int decode_brotli(const unsigned char* in, size_t inlen,
						 unsigned char* dst, size_t cap, size_t* outlen) {
	BrotliDecoderState* s = BrotliDecoderCreateInstance(NULL, NULL, NULL);
	const uint8_t* next_in = in;
	size_t avail_in = inlen;
	uint8_t* next_out = dst;
	size_t avail_out = cap;
	int ok = 0;

	if (!s)
		return -1;
	for (;;) {
		BrotliDecoderResult r =
			BrotliDecoderDecompressStream(s, &avail_in, &next_in, &avail_out, &next_out, NULL);
		if (r == BROTLI_DECODER_RESULT_SUCCESS) {
			ok = 1;
			break;
		}
		if (r == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT)
			break;
		if (r == BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT) {
			if (avail_in == 0)
				break;
			continue;
		}
		break;
	}
	*outlen = cap - avail_out;
	BrotliDecoderDestroyInstance(s);
	return ok || *outlen ? 0 : -1;
}

/* Decode in place, then apply the caller's body cap to the decoded bytes. */
static int decode_body(xe_http_response* out, size_t max_body, int encoding) {
	unsigned char* dst;
	size_t cap;
	size_t got = 0;
	int rc;

	if (!out || !out->body || !out->body_len || encoding == XE_ENC_NONE)
		return 0;
	cap = max_body ? max_body : 1;
	dst = (unsigned char*)malloc(cap + 1);
	if (!dst)
		return -1;
	if (encoding == XE_ENC_BR)
		rc = decode_brotli((const unsigned char*)out->body, out->body_len, dst, cap, &got);
	else
		rc = decode_gzip_deflate((const unsigned char*)out->body, out->body_len,
								 encoding == XE_ENC_GZIP, dst, cap, &got);
	if (rc < 0) {
		_KePrint("[http] decode enc=%d in=%d first=%d\n", encoding,
				 (int)out->body_len,
				 out->body_len > 0 ? (int)(unsigned char)out->body[0] : 0);
		free(dst);
		return -1;
	}
	dst[got] = 0;
	free(out->body);
	out->body = (char*)dst;
	out->body_len = got;
	return 0;
}

static void tls_error(int ret, uint32_t flags, xe_http_response* out) {
	if (flags & MBEDTLS_X509_BADCERT_CN_MISMATCH)
		set_err(out, "certificate name mismatch");
	else if (flags & MBEDTLS_X509_BADCERT_EXPIRED)
		set_err(out, "certificate expired");
	else if (flags & MBEDTLS_X509_BADCERT_FUTURE)
		set_err(out, "certificate not yet valid");
	else if (flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED)
		set_err(out, "certificate not trusted");
	else if (flags)
		set_err(out, "certificate rejected");
	else
		snprintf(out->error, sizeof(out->error), "TLS handshake failed (%d)", ret);
}

/* One handshake owns its ssl context. The old globals made a second
 * socket overwrite the first in the middle of TLS. */
static volatile int g_rng_lock;

static int tls_rng(void* ctx, unsigned char* buf, size_t len) {
	int rc;
	while (__atomic_exchange_n(&g_rng_lock, 1, __ATOMIC_ACQUIRE))
		;
	rc = mbedtls_ctr_drbg_random(ctx, buf, len);
	__atomic_store_n(&g_rng_lock, 0, __ATOMIC_RELEASE);
	return rc;
}

static volatile int g_dns_lock;

static void tls_free(struct xe_reader* r) {
	if (!r || !r->slot)
		return;
	mbedtls_ssl_free(&r->slot->ssl);
	mbedtls_ssl_config_free(&r->slot->conf);
	free(r->slot);
	r->slot = NULL;
	r->ssl = NULL;
	r->tls = 0;
}

static void tls_sessions_init(void) {
	int i;

	if (g_sessions_ready)
		return;
	for (i = 0; i < XE_TLS_SESSION_MAX; i++)
		mbedtls_ssl_session_init(&g_sessions[i].session);
	g_sessions_ready = 1;
}

static mbedtls_ssl_session* tls_session_get(const char* host) {
	int i;
	int64_t now;

	tls_sessions_init();
	now = (int64_t)time(NULL);
	for (i = 0; i < XE_TLS_SESSION_MAX; i++) {
		if (!g_sessions[i].used)
			continue;
		if (g_sessions[i].expires <= now) {
			mbedtls_ssl_session_free(&g_sessions[i].session);
			mbedtls_ssl_session_init(&g_sessions[i].session);
			g_sessions[i].used = 0;
			continue;
		}
		if (strcmp(g_sessions[i].host, host) == 0)
			return &g_sessions[i].session;
	}
	return NULL;
}

/* Export the current session into the cache. A TLS 1.3 session is only
 * resumable after its NewSessionTicket has been processed, so this is called
 * again once the body has been read. A failed export leaves the previous
 * entry intact instead of throwing away a usable TLS 1.2 session. */
static int tls_session_put(const char* host, mbedtls_ssl_context* ssl) {
	mbedtls_ssl_session fresh;
	int i;
	int slot = -1;
	int64_t now;

	if (!host || !ssl)
		return -1;
	tls_sessions_init();
	mbedtls_ssl_session_init(&fresh);
	if (mbedtls_ssl_get_session(ssl, &fresh) != 0) {
		mbedtls_ssl_session_free(&fresh);
		return -1;
	}
	now = (int64_t)time(NULL);
	for (i = 0; i < XE_TLS_SESSION_MAX; i++) {
		if (!g_sessions[i].used) {
			slot = i;
			break;
		}
		if (strcmp(g_sessions[i].host, host) == 0) {
			slot = i;
			break;
		}
		if (g_sessions[i].expires <= now && slot < 0)
			slot = i;
	}
	if (slot < 0) {
		slot = 0;
		for (i = 1; i < XE_TLS_SESSION_MAX; i++) {
			if (g_sessions[i].expires < g_sessions[slot].expires)
				slot = i;
		}
	}
	if (g_sessions[slot].used)
		mbedtls_ssl_session_free(&g_sessions[slot].session);
	g_sessions[slot].session = fresh;
	snprintf(g_sessions[slot].host, sizeof(g_sessions[slot].host), "%s", host);
	g_sessions[slot].expires = now + XE_TLS_SESSION_TTL;
	g_sessions[slot].used = 1;
	return 0;
}

void xe_http_tls_stats(int* full, int* resumed) {
	if (full)
		*full = g_tls_full;
	if (resumed)
		*resumed = g_tls_resumed;
}

static const char* xe_alpn[] = { "http/1.1", NULL };

/* Config and bio only. The overlapped pump calls handshake one step at a time. */
static int tls_prepare(struct xe_reader* r, const char* host, xe_http_response* out) {
	mbedtls_ssl_session* cached;
	int ret;

	r->resumed = 0;
	if (time(NULL) < XE_CLOCK_MIN) {
		set_err(out, "clock not set");
		return -1;
	}
	if (ensure_rng(out) < 0 || ensure_ca(out) < 0)
		return -1;

	r->slot = (struct xe_tls_slot*)malloc(sizeof(struct xe_tls_slot));
	if (!r->slot) {
		set_err(out, "TLS alloc failed");
		return -1;
	}
	memset(r->slot, 0, sizeof(*r->slot));
	mbedtls_ssl_init(&r->slot->ssl);
	mbedtls_ssl_config_init(&r->slot->conf);
	r->ssl = &r->slot->ssl;
	r->tls = 1;
	ret = mbedtls_ssl_config_defaults(&r->slot->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
									 MBEDTLS_SSL_PRESET_DEFAULT);
	if (ret != 0) {
		set_err(out, "TLS config failed");
		goto fail;
	}
	mbedtls_ssl_conf_authmode(&r->slot->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
	mbedtls_ssl_conf_ca_chain(&r->slot->conf, &g_cacert, NULL);
	mbedtls_ssl_conf_rng(&r->slot->conf, tls_rng, &g_drbg);
	mbedtls_ssl_conf_session_tickets(&r->slot->conf, MBEDTLS_SSL_SESSION_TICKETS_ENABLED);
	if (mbedtls_ssl_conf_alpn_protocols(&r->slot->conf, xe_alpn) != 0) {
		set_err(out, "ALPN rejected");
		goto fail;
	}
	if (mbedtls_ssl_setup(&r->slot->ssl, &r->slot->conf) != 0) {
		set_err(out, "TLS setup failed");
		goto fail;
	}
	cached = tls_session_get(host);
	if (cached && mbedtls_ssl_set_session(&r->slot->ssl, cached) == 0)
		r->resumed = 1;
	if (mbedtls_ssl_set_hostname(&r->slot->ssl, host) != 0) {
		set_err(out, "SNI rejected");
		goto fail;
	}
	mbedtls_ssl_set_bio(&r->slot->ssl, r, tls_send, tls_recv, NULL);
	return 0;
fail:
	tls_free(r);
	return -1;
}

static int tls_handshake(struct xe_reader* r, const char* host, xe_http_response* out) {
	int ret;
	uint32_t flags = 0;

	if (tls_prepare(r, host, out) < 0)
		return -1;
	while ((ret = mbedtls_ssl_handshake(r->ssl)) != 0) {
		if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
			continue;
		flags = mbedtls_ssl_get_verify_result(r->ssl);
		tls_error(ret, flags, out);
		goto fail;
	}
	flags = mbedtls_ssl_get_verify_result(r->ssl);
	if (flags != 0) {
		tls_error(ret, flags, out);
		goto fail;
	}
	tls_session_put(host, r->ssl);
	if (r->resumed)
		g_tls_resumed++;
	else
		g_tls_full++;
	return 0;
fail:
	tls_free(r);
	return -1;
}

static int connect_host(const struct xe_url* u, xe_http_response* out) {
	hostent* ent;
	int sock;
	sockaddr_in dest;
	uint32_t ip;
	while (__atomic_exchange_n(&g_dns_lock, 1, __ATOMIC_ACQUIRE))
		;
	ent = gethostbyname(u->host);
	if (ent && ent->h_addr_list && ent->h_addr_list[0] && ent->h_length == 4)
		ip = *(uint32_t*)ent->h_addr_list[0];
	else
		ip = 0;
	__atomic_store_n(&g_dns_lock, 0, __ATOMIC_RELEASE);
	if (!ip) {
		snprintf(out->error, sizeof(out->error), "No IPv4 address for %s", u->host);
		return -1;
	}
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		set_err(out, "socket() failed");
		return -1;
	}
	memset(&dest, 0, sizeof(dest));
	dest.sin_family = AF_INET;
	dest.sin_port = htons(u->port);
	memcpy(&dest.sin_addr, &ip, sizeof(ip));
	if (connect(sock, (sockaddr_*)&dest, sizeof(dest)) < 0) {
		snprintf(out->error, sizeof(out->error), "Connect to %s:%u failed", u->host, u->port);
		_KeCloseFile(sock);
		return -1;
	}
	return sock;
}

/* XEClib EINPROGRESS. The kernel returns this from a non-blocking connect.
 * EISCONN means the handshake finished between polls: another socket's
 * receive drain completed this SYN, so the next connect() no longer sees
 * SYN_SENT. That is success, not a failed connect. */
#define XE_CONNECT_PENDING 119
#define XE_EISCONN 127

/* Start a TCP connection for the overlapped pump. pending is 1 when the
 * SYN is in flight and the caller must connect() again. An older kernel
 * ignores SO_NONBLOCK, connect() blocks, and pending stays 0. */
static int connect_begin(const struct xe_url* u, xe_http_response* out,
						 uint32_t* ip_out, int* pending) {
	hostent* ent;
	int sock;
	sockaddr_in dest;
	uint32_t ip;
	int on = 1;
	int rc;
	*pending = 0;
	*ip_out = 0;
	while (__atomic_exchange_n(&g_dns_lock, 1, __ATOMIC_ACQUIRE))
		;
	ent = gethostbyname(u->host);
	if (ent && ent->h_addr_list && ent->h_addr_list[0] && ent->h_length == 4)
		ip = *(uint32_t*)ent->h_addr_list[0];
	else
		ip = 0;
	__atomic_store_n(&g_dns_lock, 0, __ATOMIC_RELEASE);
	if (!ip) {
		snprintf(out->error, sizeof(out->error), "No IPv4 address for %s", u->host);
		return -1;
	}
	*ip_out = ip;
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		set_err(out, "socket() failed");
		return -1;
	}
	socket_setopt(sock, SOL_SOCKET, SO_NONBLOCK, &on, sizeof(on));
	memset(&dest, 0, sizeof(dest));
	dest.sin_family = AF_INET;
	dest.sin_port = htons(u->port);
	memcpy(&dest.sin_addr, &ip, sizeof(ip));
	rc = connect(sock, (sockaddr_*)&dest, sizeof(dest));
	if (rc == 0)
		return sock;
	if (rc == -XE_CONNECT_PENDING) {
		*pending = 1;
		return sock;
	}
	snprintf(out->error, sizeof(out->error), "Connect to %s:%u failed", u->host, u->port);
	_KeCloseFile(sock);
	return -1;
}

static int read_headers(struct xe_reader* r, char* hdr, size_t hdrsz) {
	size_t n = 0;
	hdr[0] = 0;
	while (n + 1 < hdrsz) {
		size_t one = 0;
		unsigned char c;
		if (rd_copy(r, &c, 1, &one) < 0)
			return -1;
		if (one == 0)
			return -1;
		hdr[n++] = (char)c;
		hdr[n] = 0;
		if (n >= 4 && hdr[n - 4] == '\r' && hdr[n - 3] == '\n' && hdr[n - 2] == '\r' &&
			hdr[n - 1] == '\n')
			return 0;
	}
	return -1;
}

static int status_code(const char* hdr) {
	const char* p;
	int code = 0;
	if (strncmp(hdr, "HTTP/", 5) != 0)
		return -1;
	p = strchr(hdr, ' ');
	if (!p)
		return -1;
	p++;
	while (*p >= '0' && *p <= '9') {
		code = code * 10 + (*p - '0');
		p++;
	}
	return code;
}

/* ---------------------------------------------------------------- */
/* Cookie jar. One process-wide table. Scripts and Set-Cookie share it,
 * so a challenge cookie is sent on the navigation that follows. */
/* ---------------------------------------------------------------- */

#define XE_COOKIE_MAX 32
#define XE_COOKIE_NAME 48
#define XE_COOKIE_VALUE 2048
#define XE_COOKIE_DOMAIN 128
#define XE_COOKIE_PATH 160

struct xe_cookie {
	int used;
	int host_only;
	int secure;
	int http_only;
	int64_t expires; /* 0 = session */
	char name[XE_COOKIE_NAME];
	char value[XE_COOKIE_VALUE];
	char domain[XE_COOKIE_DOMAIN];
	char path[XE_COOKIE_PATH];
};

static struct xe_cookie g_ck[XE_COOKIE_MAX];
static int g_ck_ready;
/* Set for the whole of one xe_http_get, including its redirects. */
static int g_ck_hold;
static int64_t g_ck_hold_now;

static void ck_init(void) {
	if (g_ck_ready)
		return;
	memset(g_ck, 0, sizeof(g_ck));
	g_ck_ready = 1;
}

static int ck_ieq(const char* a, const char* b) {
	if (!a || !b)
		return 0;
	while (*a && *b) {
		unsigned char ca = (unsigned char)*a;
		unsigned char cb = (unsigned char)*b;
		if (ca >= 'A' && ca <= 'Z')
			ca = (unsigned char)(ca + 32);
		if (cb >= 'A' && cb <= 'Z')
			cb = (unsigned char)(cb + 32);
		if (ca != cb)
			return 0;
		a++;
		b++;
	}
	return *a == 0 && *b == 0;
}

static void ck_trim(char* s) {
	char* e;
	size_t n;
	if (!s)
		return;
	while (*s == ' ' || *s == '\t')
		memmove(s, s + 1, strlen(s));
	n = strlen(s);
	e = s + n;
	while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
		*--e = 0;
}

static int ck_domain_ok(const char* host, const char* domain) {
	size_t hl, dl;
	if (!host || !domain || !domain[0])
		return 0;
	/* "com" is a suffix of "google.com" and must not match. */
	if (!strchr(domain, '.') && !ck_ieq(host, domain))
		return 0;
	hl = strlen(host);
	dl = strlen(domain);
	if (dl == 0 || hl < dl)
		return 0;
	if (!ck_ieq(host + (hl - dl), domain))
		return 0;
	if (hl == dl)
		return 1;
	return host[hl - dl - 1] == '.';
}

static int64_t ck_now(void) {
	time_t t = time(NULL);
	if (t <= 0)
		return 0;
	return (int64_t)t;
}

static int ck_month(const char* s) {
	static const char* names = "janfebmaraprmayjunjulaugsepoctnovdec";
	char b[3];
	int i;
	for (i = 0; i < 3; i++) {
		unsigned char c = (unsigned char)s[i];
		if (!c)
			return 0;
		if (c >= 'A' && c <= 'Z')
			c = (unsigned char)(c + 32);
		b[i] = (char)c;
	}
	for (i = 0; i < 12; i++) {
		if (b[0] == names[i * 3] && b[1] == names[i * 3 + 1] && b[2] == names[i * 3 + 2])
			return i + 1;
	}
	return 0;
}

static int ck_num(const char** p, int* out) {
	const char* s = *p;
	int v = 0;
	int any = 0;
	while (*s >= '0' && *s <= '9') {
		v = v * 10 + (*s - '0');
		any = 1;
		s++;
	}
	if (!any)
		return 0;
	*out = v;
	*p = s;
	return 1;
}

/* Days since 1970-01-01 for a civil Y-M-D. Howard Hinnant's algorithm. */
static int64_t ck_days(int y, unsigned m, unsigned d) {
	int era;
	unsigned yoe, doy, doe;
	y -= m <= 2;
	era = (y >= 0 ? y : y - 399) / 400;
	yoe = (unsigned)(y - era * 400);
	doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static int64_t ck_http_date(const char* s) {
	int day = 0, year = 0, hour = 0, min = 0, sec = 0, mon;
	const char* p = s;
	const char* comma;
	if (!s)
		return 0;
	comma = strchr(s, ',');
	if (comma)
		p = comma + 1;
	while (*p == ' ' || *p == '\t')
		p++;
	if (!ck_num(&p, &day))
		return 0;
	if (*p == '-' || *p == ' ')
		p++;
	mon = ck_month(p);
	if (!mon)
		return 0;
	p += 3;
	if (*p == '-' || *p == ' ')
		p++;
	if (!ck_num(&p, &year))
		return 0;
	while (*p == ' ' || *p == '\t')
		p++;
	if (!ck_num(&p, &hour) || *p != ':')
		return 0;
	p++;
	if (!ck_num(&p, &min) || *p != ':')
		return 0;
	p++;
	if (!ck_num(&p, &sec))
		return 0;
	if (year < 100)
		year += (year >= 70) ? 1900 : 2000;
	if (mon < 1 || mon > 12 || day < 1 || day > 31)
		return 0;
	return ck_days(year, (unsigned)mon, (unsigned)day) * 86400 + hour * 3600 + min * 60 + sec;
}

static void ck_req_path(const char* full, char* dst, size_t n) {
	const char* q;
	size_t len;
	if (!dst || n == 0)
		return;
	dst[0] = '/';
	dst[1] = 0;
	if (!full || full[0] != '/')
		return;
	q = strchr(full, '?');
	len = q ? (size_t)(q - full) : strlen(full);
	if (len == 0)
		return;
	if (len >= n)
		len = n - 1;
	memcpy(dst, (void*)full, len);
	dst[len] = 0;
}

static void ck_default_path(const char* full, char* dst, size_t n) {
	char path[XE_COOKIE_PATH];
	size_t i, slash;
	ck_req_path(full, path, sizeof(path));
	slash = 0;
	for (i = 0; path[i]; i++) {
		if (path[i] == '/')
			slash = i;
	}
	if (slash + 1 >= n)
		slash = n > 0 ? n - 2 : 0;
	memcpy(dst, (void*)path, slash + 1);
	dst[slash + 1] = 0;
}

static int ck_path_match(const char* req, const char* cpath) {
	size_t n;
	if (!cpath || !cpath[0])
		return 1;
	n = strlen(cpath);
	if (strncmp(req, cpath, n) != 0)
		return 0;
	if (cpath[n - 1] == '/')
		return 1;
	return req[n] == '/' || req[n] == 0;
}

static void ck_drop(const char* name, const char* domain, const char* path, int host_only) {
	int i;
	for (i = 0; i < XE_COOKIE_MAX; i++) {
		struct xe_cookie* c = &g_ck[i];
		if (!c->used)
			continue;
		if (c->host_only != host_only)
			continue;
		if (!ck_ieq(c->name, name) || !ck_ieq(c->domain, domain) || strcmp(c->path, path) != 0)
			continue;
		c->used = 0;
	}
}

static void ck_store(const struct xe_url* u, const char* line, int from_http) {
	char buf[4096];
	char domain[XE_COOKIE_DOMAIN];
	char path[XE_COOKIE_PATH];
	char* semi;
	char* eq;
	char* name;
	char* value;
	char* attr;
	int secure = 0;
	int http_only = 0;
	int host_only = 1;
	int del = 0;
	int64_t expires = 0;
	int64_t now;
	size_t n = 0;
	int slot;
	struct xe_cookie* c;
	if (!u || !line || !line[0])
		return;
	ck_init();
	while (line[n] && line[n] != '\r' && line[n] != '\n' && n + 1 < sizeof(buf)) {
		buf[n] = line[n];
		n++;
	}
	buf[n] = 0;
	semi = strchr(buf, ';');
	if (semi)
		*semi = 0;
	eq = strchr(buf, '=');
	if (!eq || eq == buf)
		return;
	*eq = 0;
	name = buf;
	value = eq + 1;
	ck_trim(name);
	ck_trim(value);
	if (!name[0] || strlen(name) >= XE_COOKIE_NAME || strlen(value) >= XE_COOKIE_VALUE) {
		_KePrint("[cookie] drop %s\n", name[0] ? name : "?");
		return;
	}
	snprintf(domain, sizeof(domain), "%s", u->host);
	path[0] = 0;
	now = ck_now();
	attr = semi ? semi + 1 : NULL;
	while (attr && *attr) {
		char* next = strchr(attr, ';');
		char* aeq;
		char* aname;
		char* aval;
		if (next)
			*next = 0;
		while (*attr == ' ' || *attr == '\t')
			attr++;
		aname = attr;
		aval = (char*)"";
		aeq = strchr(attr, '=');
		if (aeq) {
			*aeq = 0;
			aval = aeq + 1;
			while (*aval == ' ' || *aval == '\t')
				aval++;
		}
		ck_trim(aname);
		if (ck_ieq(aname, "domain") && aval[0]) {
			if (aval[0] == '.')
				aval++;
			if (!ck_domain_ok(u->host, aval))
				return;
			snprintf(domain, sizeof(domain), "%s", aval);
			host_only = 0;
		} else if (ck_ieq(aname, "path") && aval[0] == '/') {
			snprintf(path, sizeof(path), "%s", aval);
		} else if (ck_ieq(aname, "secure")) {
			secure = 1;
		} else if (ck_ieq(aname, "httponly")) {
			if (from_http)
				http_only = 1;
		} else if (ck_ieq(aname, "max-age")) {
			int mag = 0;
			int neg = aval[0] == '-';
			const char* np = neg ? aval + 1 : aval;
			if (ck_num(&np, &mag) && (neg || mag == 0))
				del = 1;
			else if (!neg && mag > 0 && now >= XE_CLOCK_MIN)
				expires = now + mag;
		} else if (ck_ieq(aname, "expires")) {
			int64_t when = ck_http_date(aval);
			if (when > 0) {
				expires = when;
				if (now >= XE_CLOCK_MIN && when <= now)
					del = 1;
			}
		}
		attr = next ? next + 1 : NULL;
	}
	if (!path[0])
		ck_default_path(u->path, path, sizeof(path));
	if (del) {
		_KePrint("[cookie] clear %s\n", name[0] ? name : "?");
		ck_drop(name, domain, path, host_only);
		return;
	}
	slot = -1;
	for (n = 0; n < XE_COOKIE_MAX; n++) {
		c = &g_ck[n];
		if (c->used && c->host_only == host_only && ck_ieq(c->name, name) &&
			ck_ieq(c->domain, domain) && strcmp(c->path, path) == 0) {
			slot = (int)n;
			break;
		}
		if (!c->used && slot < 0)
			slot = (int)n;
	}
	if (slot < 0) {
		_KePrint("[cookie] jar full, drop %s\n", name);
		return;
	}
	c = &g_ck[slot];
	memset(c, 0, sizeof(*c));
	c->used = 1;
	c->host_only = host_only;
	c->secure = secure;
	c->http_only = http_only;
	c->expires = expires;
	snprintf(c->name, sizeof(c->name), "%s", name);
	snprintf(c->value, sizeof(c->value), "%s", value);
	snprintf(c->domain, sizeof(c->domain), "%s", domain);
	snprintf(c->path, sizeof(c->path), "%s", path);
	_KePrint("[cookie] store %s %d\n", c->name, (int)strlen(c->value));
}

static void ck_from_headers(const struct xe_url* u, const char* hdr) {
	const char* p = hdr;
	if (!hdr)
		return;
	while (*p) {
		if (strncasecmp(p, "Set-Cookie:", 11) == 0) {
			const char* v = p + 11;
			while (*v == ' ' || *v == '\t')
				v++;
			ck_store(u, v, 1);
		}
		p = strstr(p, "\r\n");
		if (!p)
			break;
		p += 2;
	}
}

/* The search wall skips document.cookie when the current string plus the
 * new pair is at least 4093 bytes, then it puts the value on the query.
 * Keep the script view short enough to assign, and still return SG_SS. */
#define XE_JS_COOKIE_BUDGET 2032

static int ck_live(const struct xe_url* u, struct xe_cookie* c, const char* reqpath, int64_t now,
				   int js_only) {
	int match;
	if (!c->used)
		return 0;
	if (js_only && c->http_only)
		return 0;
	if (c->secure && !u->https)
		return 0;
	if (c->expires && now >= XE_CLOCK_MIN && now >= c->expires) {
		/* The search cookie lives 30s. A redirect on this guest can take
		 * longer than that, so a cookie that was valid when this fetch
		 * started is still sent on the later hops. */
		int keep = g_ck_hold && c->expires >= g_ck_hold_now && now < c->expires + 180;
		if (!keep) {
			_KePrint("[cookie] expire %s\n", c->name);
			c->used = 0;
			return 0;
		}
	}
	if (c->host_only)
		match = ck_ieq(u->host, c->domain);
	else
		match = ck_domain_ok(u->host, c->domain);
	if (!match || !ck_path_match(reqpath, c->path))
		return 0;
	return 1;
}

static int ck_put(char* dst, size_t dstsz, size_t* used, const struct xe_cookie* c, size_t budget) {
	size_t cap;
	size_t need;
	size_t extra;
	int n;
	if (!dst || !used || !c || dstsz == 0)
		return 0;
	cap = budget < dstsz ? budget : dstsz;
	need = strlen(c->name) + 1 + strlen(c->value);
	extra = *used ? 2 : 0;
	if (*used + extra + need + 1 > cap)
		return 0;
	if (extra) {
		dst[(*used)++] = ';';
		dst[(*used)++] = ' ';
	}
	n = snprintf(dst + *used, dstsz - *used, "%s=%s", c->name, c->value);
	if (n < 0 || (size_t)n >= dstsz - *used)
		return 0;
	*used += (size_t)n;
	return 1;
}

static int ck_fill(const struct xe_url* u, char* dst, size_t dstsz, int js_only) {
	char reqpath[XE_COOKIE_PATH];
	size_t used = 0;
	int64_t now;
	int pass;
	int i;
	if (!dst || dstsz == 0)
		return 0;
	dst[0] = 0;
	if (!u)
		return 0;
	ck_init();
	ck_req_path(u->path, reqpath, sizeof(reqpath));
	now = ck_now();
	/* Pass 0 is SG_SS. A full jar used to stop before that name, so the
	 * page thought the cookie it had just stored was missing. */
	for (pass = 0; pass < 2; pass++) {
		for (i = 0; i < XE_COOKIE_MAX; i++) {
			struct xe_cookie* c = &g_ck[i];
			int sg;
			size_t budget;
			if (!ck_live(u, c, reqpath, now, js_only))
				continue;
			sg = ck_ieq(c->name, "SG_SS");
			if (pass == 0 && !sg)
				continue;
			if (pass == 1 && sg)
				continue;
			budget = dstsz;
			if (js_only && pass == 1)
				budget = XE_JS_COOKIE_BUDGET;
			ck_put(dst, dstsz, &used, c, budget);
		}
	}
	return (int)used;
}

static void ck_referer_for(const struct xe_url* u, char* dst, size_t n) {
	struct xe_url r;
	if (!dst || n == 0)
		return;
	dst[0] = 0;
	if (!g_referer[0])
		return;
	if (parse_url(g_referer, &r) < 0)
		return;
	if (!ck_ieq(r.host, u->host))
		return;
	if (r.https && !u->https)
		return;
	snprintf(dst, n, "%s", g_referer);
}

void xe_http_set_referer(const char* url) {
	char* hash;
	g_referer[0] = 0;
	if (!url || !url[0])
		return;
	snprintf(g_referer, sizeof(g_referer), "%s", url);
	hash = strchr(g_referer, '#');
	if (hash)
		*hash = 0;
}

void xe_http_set_body(const char* content_type, const void* body, size_t len) {
	free(g_req_body);
	g_req_body = NULL;
	g_req_body_len = 0;
	g_req_ctype[0] = 0;
	if (!body || len == 0)
		return;
	if (len > 48 * 1024)
		len = 48 * 1024;
	g_req_body = (char*)malloc(len);
	if (!g_req_body)
		return;
	memcpy(g_req_body, (void*)body, len);
	g_req_body_len = len;
	snprintf(g_req_ctype, sizeof(g_req_ctype), "%s",
			 content_type && content_type[0] ? content_type
											 : "application/x-www-form-urlencoded");
}

void xe_http_last_url(char* dst, size_t dstsz) {
	if (!dst || dstsz == 0)
		return;
	snprintf(dst, dstsz, "%s", g_last_url);
}

void xe_cookie_set(const char* page_url, const char* set_cookie, int from_http) {
	struct xe_url u;
	if (!page_url || parse_url(page_url, &u) < 0)
		return;
	ck_store(&u, set_cookie, from_http);
}

int xe_cookie_header(const char* url, char* dst, size_t dstsz) {
	struct xe_url u;
	if (!url || parse_url(url, &u) < 0) {
		if (dst && dstsz)
			dst[0] = 0;
		return 0;
	}
	return ck_fill(&u, dst, dstsz, 0);
}

int xe_cookie_document(const char* url, char* dst, size_t dstsz) {
	struct xe_url u;
	if (!url || parse_url(url, &u) < 0) {
		if (dst && dstsz)
			dst[0] = 0;
		return 0;
	}
	return ck_fill(&u, dst, dstsz, 1);
}

static int read_body(struct xe_reader* r, int chunked, long content_len, int want_body,
					 xe_http_response* out, size_t max_body) {
	size_t cap = max_body ? max_body : 1;
	unsigned char* body = (unsigned char*)malloc(cap + 1);
	size_t got = 0;
	if (!body)
		return -1;
	if (!want_body) {
		body[0] = 0;
		out->body = (char*)body;
		out->body_len = 0;
		return 0;
	}
	if (chunked) {
		for (;;) {
			char line[64];
			size_t sz = 0;
			size_t took = 0;
			if (rd_line(r, line, sizeof(line)) < 0)
				break;
			if (hex_len(line, &sz) < 0)
				break;
			if (sz == 0) {
				/* 0\r\n is not the end of the message. Trailer fields
				 * follow, then a blank line. The socket is closed after
				 * this read, but leave the framing consumed. */
				for (;;) {
					if (rd_line(r, line, sizeof(line)) < 0)
						break;
					if (line[0] == 0)
						break;
				}
				break;
			}
			while (sz) {
				size_t room = (got < cap) ? (cap - got) : 0;
				size_t ask = sz;
				size_t n = 0;
				unsigned char* dst;
				if (ask > 2048)
					ask = 2048;
				/* Once the cap is full, keep reading the chunk so the
				 * framing stays aligned, but never write past body. */
				if (!room)
					break;
				if (ask > room)
					ask = room;
				dst = body + got;
				if (rd_copy(r, dst, ask, &n) < 0 || n == 0) {
					sz = 0;
					break;
				}
				got += n;
				sz -= n;
				(void)took;
			}
			rd_line(r, line, sizeof(line));
		}
	} else if (content_len >= 0) {
		size_t left = (size_t)content_len;
		while (left) {
			size_t room = (got < cap) ? (cap - got) : 0;
			size_t ask = left > 2048 ? 2048 : left;
			size_t n = 0;
			unsigned char* dst;
			if (!room)
				break;
			if (ask > room)
				ask = room;
			dst = body + got;
			if (rd_copy(r, dst, ask, &n) < 0 || n == 0)
				break;
			got += n;
			left -= n;
		}
	} else {
		for (;;) {
			size_t room = (got < cap) ? (cap - got) : 0;
			size_t n = 0;
			unsigned char tmp[2048];
			unsigned char* dst = room ? body + got : tmp;
			size_t ask = room ? (room > 2048 ? 2048 : room) : sizeof(tmp);
			if (rd_copy(r, dst, ask, &n) < 0 || n == 0)
				break;
			if (room)
				got += n;
			if (!room && r->eof)
				break;
		}
	}
	body[got] = 0;
	out->body = (char*)body;
	out->body_len = got;
	return 0;
}

static int fetch_once(const struct xe_url* u, const char* method, const char* ua,
					  const unsigned char* req_body, size_t req_body_len, const char* ctype,
					  xe_http_response* out, size_t max_body, char* location, size_t locsz) {
	struct xe_reader r;
	int fd;
	int head_only = method && strcmp(method, "HEAD") == 0;
	char* req = NULL;
	char* cookie = NULL;
	char* referer = NULL;
	char hdr[XE_HDR_RAW];
	const char* te;
	const char* cl;
	const char* loc;
	int chunked = 0;
	int encoding = XE_ENC_NONE;
	long content_len = -1;
	size_t raw_cap;
	char hosthdr[300];

	location[0] = 0;
	memset(&r, 0, sizeof(r));
	fd = connect_host(u, out);
	if (fd < 0)
		return -1;
	r.fd = fd;
	out->tls = u->https;
	if (u->https && tls_handshake(&r, u->host, out) < 0) {
		_KeCloseFile(fd);
		return -1;
	}
	if ((u->https && u->port == 443) || (!u->https && u->port == 80))
		snprintf(hosthdr, sizeof(hosthdr), "%s", u->host);
	else
		snprintf(hosthdr, sizeof(hosthdr), "%s:%u", u->host, u->port);
	cookie = (char*)malloc(4096);
	referer = (char*)malloc(2048);
	if (cookie) {
		cookie[0] = 0;
		ck_fill(u, cookie, 4096, 0);
	}
	if (referer) {
		referer[0] = 0;
		ck_referer_for(u, referer, 2048);
	}
	{
		size_t extra = (cookie && cookie[0] ? strlen(cookie) : 0) +
					   (referer && referer[0] ? strlen(referer) : 0);
		size_t reqcap = strlen(u->path) + extra + 768;
		int n;
		size_t at;
		req = (char*)malloc(reqcap);
		if (!req) {
			set_err(out, "Out of memory");
			goto fail;
		}
		if (req_body && req_body_len) {
			n = snprintf(req, reqcap,
						 "POST %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\nAccept: */*\r\n"
						 "Accept-Encoding: gzip, deflate, br\r\n"
						 "Content-Type: %s\r\nContent-Length: %u\r\n",
						 u->path, hosthdr, ua ? ua : "xeneva",
						 ctype ? ctype : "application/octet-stream", (unsigned)req_body_len);
		} else {
			n = snprintf(req, reqcap,
						 "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\nAccept: */*\r\n"
						 "Accept-Encoding: gzip, deflate, br\r\n",
						 head_only ? "HEAD" : "GET", u->path, hosthdr, ua ? ua : "xeneva");
		}
		if (n < 0 || (size_t)n >= reqcap) {
			set_err(out, "Request too long");
			goto fail;
		}
		at = (size_t)n;
		if (cookie && cookie[0]) {
			n = snprintf(req + at, reqcap - at, "Cookie: %s\r\n", cookie);
			if (n < 0 || (size_t)n >= reqcap - at) {
				set_err(out, "Request too long");
				goto fail;
			}
			at += (size_t)n;
			_KePrint("[cookie] send %s %d\n", u->host, (int)strlen(cookie));
		}
		if (referer && referer[0]) {
			n = snprintf(req + at, reqcap - at, "Referer: %s\r\n", referer);
			if (n < 0 || (size_t)n >= reqcap - at) {
				set_err(out, "Request too long");
				goto fail;
			}
			at += (size_t)n;
		}
		n = snprintf(req + at, reqcap - at, "Connection: close\r\n\r\n");
		if (n < 0 || (size_t)n >= reqcap - at) {
			set_err(out, "Request too long");
			goto fail;
		}
	}
	if (io_write_all(&r, (const unsigned char*)req, strlen(req)) < 0 ||
		(req_body && req_body_len && io_write_all(&r, req_body, req_body_len) < 0)) {
		set_err(out, "Send failed");
		goto fail;
	}
	if (read_headers(&r, hdr, sizeof(hdr)) < 0) {
		set_err(out, "No HTTP response");
		goto fail;
	}
	memcpy(out->headers, hdr, sizeof(out->headers) - 1);
	out->headers[sizeof(out->headers) - 1] = 0;
	out->status = status_code(hdr);
	if (out->status < 0) {
		set_err(out, "Bad HTTP status");
		goto fail;
	}
	te = hdr_value(hdr, "Transfer-Encoding");
	if (te && strncasecmp(te, "chunked", 7) == 0)
		chunked = 1;
	cl = hdr_value(hdr, "Content-Length");
	if (cl) {
		content_len = 0;
		while (*cl >= '0' && *cl <= '9') {
			content_len = content_len * 10 + (*cl - '0');
			cl++;
		}
	}
	loc = hdr_value(hdr, "Location");
	if (loc)
		copy_token(loc, location, locsz);
	ck_from_headers(u, hdr);
	encoding = http_content_encoding(hdr);
	raw_cap = (encoding == XE_ENC_NONE) ? max_body : decode_raw_cap(max_body);
	if (read_body(&r, chunked, content_len, !head_only, out, raw_cap) < 0) {
		set_err(out, "Body read failed");
		goto fail;
	}
	/* TLS 1.3 NewSessionTicket usually follows the handshake and may only be
	 * consumed while the body is read. Export again now that it can be
	 * present; TLS 1.2 simply keeps the entry saved after the handshake. */
	if (r.ssl)
		tls_session_put(u->host, r.ssl);
	if (decode_body(out, max_body, encoding) < 0) {
		set_err(out, "Content decoding failed");
		goto fail;
	}
	if (r.ssl)
		mbedtls_ssl_close_notify(r.ssl);
	tls_free(&r);
	_KeCloseFile(fd);
	free(req);
	free(cookie);
	free(referer);
	return 0;
fail:
	free(req);
	free(cookie);
	free(referer);
	tls_free(&r);
	_KeCloseFile(fd);
	return -1;
}

/* Several GETs, one thread. Connect is non-blocking so the SYNs overlap,
 * but the certificate check is CPU work and does not. Opening eight TLS
 * sessions to one host therefore costs eight full handshakes in a row.
 * Two sockets per host stay up and the rest of that host's GETs are
 * keep-alive on them. A second thread is not used: the allocator is not
 * safe across threads. */
enum {
	XJ_FREE = 0,
	XJ_CONNECT,
	XJ_TLS,
	XJ_SEND,
	XJ_HDR,
	XJ_BODY,
	XJ_DONE,
	XJ_FAIL,
	XJ_WARM
};

enum {
	XB_RAW = 0,
	XB_LINE,
	XB_DATA,
	XB_CRLF,
	XB_TRAIL
};

#define XE_MANY_IDLE_MS 20000
#define XE_MANY_WAITS 750
#define XE_STEP_BUDGET 16384
/* New handshakes per host. The rest of that host's URLs reuse a socket. */
#define XE_COLD_PER_HOST 2
#define XE_WARM_MAX 2

struct xe_many {
	int state;
	int fd;
	int idx;
	int hops;
	int rc;
	int chunked;
	int body_mode;
	int encoding;
	int tls_ready;
	int waits;
	int keepalive;
	/* A keep-alive socket that dies before the response may be opened once more. */
	int may_retry;
	long content_len;
	size_t body_got;
	size_t chunk_left;
	size_t raw_cap;
	size_t hdr_n;
	size_t req_off;
	size_t req_len;
	size_t max_body;
	uint64_t idle_ms;
	struct xe_url u;
	struct xe_reader r;
	uint32_t ip;
	char url[3072];
	char loc[3072];
	char* req;
	char hdr[XE_HDR_RAW];
	/* The header read fills hdr[] in one call, then splits off the body
	 * bytes that arrived with it. Those bytes must all fit here. A 2048-byte
	 * cap dropped every stylesheet and image once virtio RX delivered a full
	 * TLS record in the first read; the document path reads headers a byte
	 * at a time and was unaffected. */
	unsigned char pend[XE_HDR_RAW];
	int pend_len;
	int pend_pos;
	char chunk_line[64];
	int chunk_line_n;
	xe_http_response* out;
	const char* ua;
};

static void job_close(struct xe_many* j) {
	if (!j)
		return;
	if (j->tls_ready && j->r.ssl)
		mbedtls_ssl_close_notify(j->r.ssl);
	j->tls_ready = 0;
	tls_free(&j->r);
	if (j->fd >= 0) {
		_KeCloseFile(j->fd);
		j->fd = -1;
	}
	j->r.fd = -1;
	free(j->req);
	j->req = NULL;
	j->req_off = 0;
	j->req_len = 0;
}

static void job_fail(struct xe_many* j, const char* msg) {
	if (!j)
		return;
	if (j->out) {
		if (msg && !j->out->error[0])
			set_err(j->out, msg);
		free(j->out->body);
		j->out->body = NULL;
		j->out->body_len = 0;
	}
	job_close(j);
	j->rc = -1;
	j->state = XJ_FAIL;
}

static int job_open(struct xe_many* j);

/* A reused socket can already be closed by the server. Open that same
 * URL once on a new connection. A cold failure is not tried again. */
static int job_broken(struct xe_many* j, const char* msg) {
	if (j->may_retry && j->hdr_n == 0 && j->body_got == 0 && j->out && !j->out->body) {
		j->may_retry = 0;
		job_close(j);
		j->out->error[0] = 0;
		j->rc = -1;
		j->content_len = -1;
		j->hdr_n = 0;
		j->pend_len = 0;
		j->pend_pos = 0;
		j->state = XJ_FREE;
		if (job_open(j) == 0) {
			_KePrint("[http] reuse failed, new connection\n");
			return 1;
		}
	}
	job_fail(j, msg);
	return 1;
}

static void job_touch(struct xe_many* j) {
	j->idle_ms = _KeGetCurrentMS();
	j->waits = 0;
}

static int job_stale(struct xe_many* j) {
	uint64_t now = _KeGetCurrentMS();
	if (j->waits > XE_MANY_WAITS ||
		(now > j->idle_ms && now - j->idle_ms > (uint64_t)XE_MANY_IDLE_MS)) {
		_KePrint("[http] stall\n");
		job_fail(j, "Timed out");
		return 1;
	}
	return 0;
}

static int job_send_some(struct xe_reader* r, const unsigned char* buf, size_t len) {
	int n;
	if (!r || !len)
		return 0;
	r->progressed = 0;
	if (r->tls)
		n = mbedtls_ssl_write(r->ssl, buf, len);
	else
		n = (int)sendto(r->fd, buf, len, 0, NULL, 0);
	if (n > 0) {
		r->progressed = 1;
		return n;
	}
	if (r->tls && (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE))
		return 0;
	if (!r->tls && n == 0)
		return 0;
	return -1;
}

static int job_build_req(struct xe_many* j) {
	char* cookie = NULL;
	char* referer = NULL;
	char hosthdr[300];
	char* req;
	size_t extra;
	size_t reqcap;
	size_t at;
	int n;
	const char* ua = j->ua && j->ua[0] ? j->ua : "xeneva";

	free(j->req);
	j->req = NULL;
	j->req_off = 0;
	j->req_len = 0;
	if ((j->u.https && j->u.port == 443) || (!j->u.https && j->u.port == 80))
		snprintf(hosthdr, sizeof(hosthdr), "%s", j->u.host);
	else
		snprintf(hosthdr, sizeof(hosthdr), "%s:%u", j->u.host, j->u.port);
	cookie = (char*)malloc(4096);
	referer = (char*)malloc(2048);
	if (cookie) {
		cookie[0] = 0;
		ck_fill(&j->u, cookie, 4096, 0);
	}
	if (referer) {
		referer[0] = 0;
		ck_referer_for(&j->u, referer, 2048);
	}
	extra = (cookie && cookie[0] ? strlen(cookie) : 0) +
			(referer && referer[0] ? strlen(referer) : 0);
	reqcap = strlen(j->u.path) + extra + 768;
	req = (char*)malloc(reqcap);
	if (!req) {
		free(cookie);
		free(referer);
		set_err(j->out, "Out of memory");
		return -1;
	}
	n = snprintf(req, reqcap,
				 "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\nAccept: */*\r\n"
				 "Accept-Encoding: gzip, deflate, br\r\n",
				 j->u.path, hosthdr, ua);
	if (n < 0 || (size_t)n >= reqcap)
		goto toolong;
	at = (size_t)n;
	if (cookie && cookie[0]) {
		n = snprintf(req + at, reqcap - at, "Cookie: %s\r\n", cookie);
		if (n < 0 || (size_t)n >= reqcap - at)
			goto toolong;
		at += (size_t)n;
		_KePrint("[cookie] send %s %d\n", j->u.host, (int)strlen(cookie));
	}
	if (referer && referer[0]) {
		n = snprintf(req + at, reqcap - at, "Referer: %s\r\n", referer);
		if (n < 0 || (size_t)n >= reqcap - at)
			goto toolong;
		at += (size_t)n;
	}
	n = snprintf(req + at, reqcap - at, "Connection: keep-alive\r\n\r\n");
	if (n < 0 || (size_t)n >= reqcap - at)
		goto toolong;
	at += (size_t)n;
	j->keepalive = 1;
	j->req = req;
	j->req_len = at;
	j->req_off = 0;
	free(cookie);
	free(referer);
	return 0;
toolong:
	free(req);
	free(cookie);
	free(referer);
	set_err(j->out, "Request too long");
	return -1;
}

static int job_start_transfer(struct xe_many* j) {
	if (j->u.https) {
		if (tls_prepare(&j->r, j->u.host, j->out) < 0) {
			job_close(j);
			return -1;
		}
		j->state = XJ_TLS;
		return 0;
	}
	if (job_build_req(j) < 0) {
		job_close(j);
		return -1;
	}
	j->tls_ready = 1;
	j->state = XJ_SEND;
	return 0;
}

static int job_open(struct xe_many* j) {
	int fd;
	int pending = 0;
	int pu = parse_url(j->url, &j->u);
	if (pu == -2) {
		set_err(j->out, "IPv6-only hosts are not reachable yet");
		return -1;
	}
	if (pu < 0) {
		set_err(j->out, "Bad URL");
		return -1;
	}
	fd = connect_begin(&j->u, j->out, &j->ip, &pending);
	if (fd < 0)
		return -1;
	memset(&j->r, 0, sizeof(j->r));
	j->r.fd = fd;
	j->r.yield = 1;
	j->fd = fd;
	j->tls_ready = 0;
	j->out->tls = j->u.https;
	j->idle_ms = _KeGetCurrentMS();
	j->waits = 0;
	j->keepalive = 1;
	j->may_retry = 0;
	if (pending) {
		j->state = XJ_CONNECT;
		return 0;
	}
	return job_start_transfer(j);
}

static int job_step_connect(struct xe_many* j) {
	sockaddr_in dest;
	int rc;
	memset(&dest, 0, sizeof(dest));
	dest.sin_family = AF_INET;
	dest.sin_port = htons(j->u.port);
	memcpy(&dest.sin_addr, &j->ip, sizeof(j->ip));
	rc = connect(j->fd, (sockaddr_*)&dest, sizeof(dest));
	if (rc == -XE_CONNECT_PENDING)
		return 0;
	if (rc < 0 && rc != -XE_EISCONN) {
		job_fail(j, "Connect failed");
		return 1;
	}
	if (job_start_transfer(j) < 0) {
		if (j->state != XJ_FAIL)
			job_fail(j, "Connect failed");
		return 1;
	}
	job_touch(j);
	return 1;
}

static int job_follow(struct xe_many* j) {
	char next[3072];
	if (j->hops >= XE_MAX_REDIRECTS) {
		set_err(j->out, "Too many redirects");
		xe_http_response_free(j->out);
		job_close(j);
		j->rc = -1;
		j->state = XJ_FAIL;
		return 1;
	}
	if (join_location(&j->u, j->loc, next, sizeof(next)) < 0) {
		set_err(j->out, "Bad redirect");
		xe_http_response_free(j->out);
		job_close(j);
		j->rc = -1;
		j->state = XJ_FAIL;
		return 1;
	}
	if (j->u.https && strncmp(next, "http://", 7) == 0) {
		j->out->https_to_http = 1;
		_KePrint("[https] redirect left TLS for %s\r\n", next);
	}
	xe_http_response_free(j->out);
	j->out->error[0] = 0;
	j->out->headers[0] = 0;
	j->out->status = 0;
	job_close(j);
	j->hops++;
	j->hdr_n = 0;
	j->hdr[0] = 0;
	j->pend_len = 0;
	j->pend_pos = 0;
	j->chunk_line_n = 0;
	j->chunk_left = 0;
	j->body_got = 0;
	j->content_len = -1;
	j->chunked = 0;
	j->body_mode = XB_RAW;
	j->encoding = XE_ENC_NONE;
	j->raw_cap = 0;
	j->loc[0] = 0;
	snprintf(j->url, sizeof(j->url), "%s", next);
	if (job_open(j) < 0) {
		j->rc = -1;
		j->state = XJ_FAIL;
		return 1;
	}
	return 1;
}

/* Leave the socket up for another GET on this host. The response already
 * recorded stays with the caller. */
static void job_park(struct xe_many* j) {
	free(j->req);
	j->req = NULL;
	j->req_off = 0;
	j->req_len = 0;
	j->hdr_n = 0;
	j->hdr[0] = 0;
	j->pend_len = 0;
	j->pend_pos = 0;
	j->chunk_left = 0;
	j->chunk_line_n = 0;
	j->body_got = 0;
	j->content_len = -1;
	j->chunked = 0;
	j->body_mode = XB_RAW;
	j->encoding = XE_ENC_NONE;
	j->raw_cap = 0;
	j->loc[0] = 0;
	j->hops = 0;
	j->keepalive = 1;
	j->may_retry = 1;
	j->state = XJ_WARM;
	job_touch(j);
}

static int job_deliver(struct xe_many* j, int reuse_ok) {
	int st = j->out ? j->out->status : 0;
	int park;
	if (j->out && j->out->body) {
		j->out->body[j->body_got] = 0;
		j->out->body_len = j->body_got;
		/* The ticket may have arrived with the last body record. */
		if (j->r.ssl)
			tls_session_put(j->u.host, j->r.ssl);
		if (decode_body(j->out, j->max_body, j->encoding) < 0) {
			job_fail(j, "Content decoding failed");
			return 1;
		}
	}
	if (st >= 400) {
		snprintf(j->out->error, sizeof(j->out->error), "HTTP %d", st);
		j->rc = 22;
	} else {
		if (j->out)
			j->out->error[0] = 0;
		j->rc = 0;
	}
	park = reuse_ok && j->keepalive && j->fd >= 0 && (j->rc == 0 || j->rc == 22);
	if (j->content_len >= 0 && j->body_got < (size_t)j->content_len)
		park = 0;
	if (park) {
		job_park(j);
		return 1;
	}
	job_close(j);
	j->state = XJ_DONE;
	return 1;
}

static int job_headers_done(struct xe_many* j) {
	const char* te;
	const char* cl;
	const char* loc;
	unsigned char* body;
	size_t cap;
	int st;

	memcpy(j->out->headers, j->hdr, sizeof(j->out->headers) - 1);
	j->out->headers[sizeof(j->out->headers) - 1] = 0;
	st = status_code(j->hdr);
	j->out->status = st;
	if (st < 0) {
		job_fail(j, "Bad HTTP status");
		return -1;
	}
	te = hdr_value(j->hdr, "Transfer-Encoding");
	j->chunked = (te && strncasecmp(te, "chunked", 7) == 0) ? 1 : 0;
	j->content_len = -1;
	cl = hdr_value(j->hdr, "Content-Length");
	if (cl) {
		j->content_len = 0;
		while (*cl >= '0' && *cl <= '9') {
			j->content_len = j->content_len * 10 + (*cl - '0');
			cl++;
		}
	}
	j->loc[0] = 0;
	loc = hdr_value(j->hdr, "Location");
	if (loc)
		copy_token(loc, j->loc, sizeof(j->loc));
	ck_from_headers(&j->u, j->hdr);
	if (hdr_has_token(hdr_value(j->hdr, "Connection"), "close"))
		j->keepalive = 0;
	if ((st == 301 || st == 302 || st == 303 || st == 307 || st == 308) && j->loc[0])
		return job_follow(j);
	j->encoding = http_content_encoding(j->hdr);
	j->raw_cap = (j->encoding == XE_ENC_NONE) ? (j->max_body ? j->max_body : 1)
											  : decode_raw_cap(j->max_body);
	cap = j->raw_cap;
	body = (unsigned char*)malloc(cap + 1);
	if (!body) {
		job_fail(j, "Out of memory");
		return -1;
	}
	body[0] = 0;
	j->out->body = (char*)body;
	j->out->body_len = 0;
	j->body_got = 0;
	j->chunk_left = 0;
	j->chunk_line_n = 0;
	j->body_mode = j->chunked ? XB_LINE : XB_RAW;
	return 0;
}

static int job_take(struct xe_many* j, unsigned char* dst, size_t max, size_t* got) {
	int spins = 0;
	*got = 0;
	if (!dst || !max)
		return 0;
	if (j->pend_pos < j->pend_len) {
		size_t have = (size_t)(j->pend_len - j->pend_pos);
		if (have > max)
			have = max;
		memcpy(dst, j->pend + j->pend_pos, have);
		j->pend_pos += (int)have;
		*got = have;
		return 1;
	}
	while (spins < 4) {
		int n = io_read(&j->r, dst, max);
		if (n > 0) {
			*got = (size_t)n;
			return 1;
		}
		if (n == 0 || n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
			return -1;
		if (n == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) {
			spins++;
			continue;
		}
		if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE ||
			n == MBEDTLS_ERR_SSL_TIMEOUT)
			return 0;
		return -2;
	}
	return 0;
}

static int job_body_raw(struct xe_many* j) {
	size_t budget = XE_STEP_BUDGET;
	size_t cap = j->raw_cap ? j->raw_cap : (j->max_body ? j->max_body : 1);
	unsigned char* body = (unsigned char*)j->out->body;
	int any = 0;
	if (!body) {
		job_fail(j, "Body read failed");
		return 1;
	}
	while (budget) {
		size_t room;
		size_t ask;
		size_t got = 0;
		int tr;
		if (j->content_len >= 0 && j->body_got >= (size_t)j->content_len) {
			job_deliver(j, 1);
			return 1;
		}
		room = (j->body_got < cap) ? (cap - j->body_got) : 0;
		if (!room) {
			job_deliver(j, 0);
			return 1;
		}
		ask = room > 2048 ? 2048 : room;
		if (ask > budget)
			ask = budget;
		tr = job_take(j, body + j->body_got, ask, &got);
		if (tr == 0)
			return any;
		if (tr < 0) {
			job_deliver(j, 0);
			return 1;
		}
		j->body_got += got;
		if (budget >= got)
			budget -= got;
		else
			budget = 0;
		any = 1;
		job_touch(j);
	}
	return any ? 1 : 0;
}

static int job_body_chunked(struct xe_many* j) {
	size_t budget = XE_STEP_BUDGET;
	size_t cap = j->raw_cap ? j->raw_cap : (j->max_body ? j->max_body : 1);
	unsigned char* body = (unsigned char*)j->out->body;
	int any = 0;
	if (!body) {
		job_fail(j, "Body read failed");
		return 1;
	}
	while (budget) {
		if (j->body_mode == XB_LINE) {
			unsigned char c;
			size_t got = 0;
			size_t sz = 0;
			int tr = job_take(j, &c, 1, &got);
			if (tr == 0)
				return any;
			if (tr < 0) {
				job_deliver(j, 0);
				return 1;
			}
			any = 1;
			job_touch(j);
			budget--;
			if (c == '\n') {
				j->chunk_line[j->chunk_line_n] = 0;
				if (hex_len(j->chunk_line, &sz) < 0) {
					job_fail(j, "Body read failed");
					return 1;
				}
				j->chunk_line_n = 0;
				if (sz == 0) {
					/* Park only after the trailer blank line. Leaving
					 * that CRLF in the stream makes the next keep-alive
					 * response start with a blank line, so its status
					 * is rejected and the stylesheet or image is dropped. */
					j->body_mode = XB_TRAIL;
					continue;
				}
				j->chunk_left = sz;
				j->body_mode = XB_DATA;
			} else if (c != '\r') {
				if (j->chunk_line_n + 1 >= (int)sizeof(j->chunk_line)) {
					job_fail(j, "Body read failed");
					return 1;
				}
				j->chunk_line[j->chunk_line_n++] = (char)c;
			}
			continue;
		}
		if (j->body_mode == XB_DATA) {
			size_t room = (j->body_got < cap) ? (cap - j->body_got) : 0;
			size_t ask;
			size_t got = 0;
			int tr;
			if (!room) {
				job_deliver(j, 0);
				return 1;
			}
			ask = j->chunk_left;
			if (ask > room)
				ask = room;
			if (ask > 2048)
				ask = 2048;
			if (ask > budget)
				ask = budget;
			if (!ask) {
				j->body_mode = XB_CRLF;
				continue;
			}
			tr = job_take(j, body + j->body_got, ask, &got);
			if (tr == 0)
				return any;
			if (tr < 0) {
				job_deliver(j, 0);
				return 1;
			}
			j->body_got += got;
			if (j->chunk_left >= got)
				j->chunk_left -= got;
			else
				j->chunk_left = 0;
			if (budget >= got)
				budget -= got;
			else
				budget = 0;
			any = 1;
			job_touch(j);
			if (j->chunk_left == 0)
				j->body_mode = XB_CRLF;
			continue;
		}
		if (j->body_mode == XB_CRLF) {
			unsigned char c;
			size_t got = 0;
			int tr = job_take(j, &c, 1, &got);
			if (tr == 0)
				return any;
			if (tr < 0) {
				job_deliver(j, 0);
				return 1;
			}
			any = 1;
			job_touch(j);
			budget--;
			if (c == '\n') {
				j->chunk_line_n = 0;
				j->body_mode = XB_LINE;
			}
			continue;
		}
		if (j->body_mode == XB_TRAIL) {
			unsigned char c;
			size_t got = 0;
			int tr = job_take(j, &c, 1, &got);
			if (tr == 0)
				return any;
			if (tr < 0) {
				job_deliver(j, 0);
				return 1;
			}
			any = 1;
			job_touch(j);
			budget--;
			if (c == '\n') {
				if (j->chunk_line_n == 0) {
					job_deliver(j, 1);
					return 1;
				}
				j->chunk_line_n = 0;
			} else if (c != '\r') {
				if (j->chunk_line_n + 1 >= (int)sizeof(j->chunk_line)) {
					job_deliver(j, 0);
					return 1;
				}
				j->chunk_line[j->chunk_line_n++] = (char)c;
			}
			continue;
		}
		job_fail(j, "Body read failed");
		return 1;
	}
	return any ? 1 : 0;
}

static int job_step_tls(struct xe_many* j) {
	int ret;
	int tries = 0;
	uint32_t flags = 0;
	if (!j->r.ssl) {
		job_broken(j, "TLS handshake failed");
		return 1;
	}
	do {
		j->r.progressed = 0;
		j->r.got_read = 0;
		ret = mbedtls_ssl_handshake(j->r.ssl);
		tries++;
	} while (ret == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET && tries < 4);
	if (ret == 0) {
		flags = mbedtls_ssl_get_verify_result(j->r.ssl);
		if (flags != 0) {
			tls_error(ret, flags, j->out);
			job_fail(j, "certificate rejected");
			return 1;
		}
		if (job_build_req(j) < 0) {
			job_fail(j, "Out of memory");
			return 1;
		}
		tls_session_put(j->u.host, j->r.ssl);
		if (j->r.resumed)
			g_tls_resumed++;
		else
			g_tls_full++;
		j->tls_ready = 1;
		j->state = XJ_SEND;
		job_touch(j);
		return 1;
	}
	if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE ||
		ret == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) {
		/* A flight that was read counts. A write with no read does not,
		 * or a socket that only retransmits never goes idle. */
		if (j->r.got_read || ret == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)
			job_touch(j);
		return 0;
	}
	flags = mbedtls_ssl_get_verify_result(j->r.ssl);
	tls_error(ret, flags, j->out);
	job_broken(j, "TLS handshake failed");
	return 1;
}

static int job_step_send(struct xe_many* j) {
	while (j->req && j->req_off < j->req_len) {
		size_t left = j->req_len - j->req_off;
		int n = job_send_some(&j->r, (const unsigned char*)j->req + j->req_off, left);
		if (n > 0) {
			j->req_off += (size_t)n;
			job_touch(j);
			continue;
		}
		if (n == 0)
			return 0;
		job_broken(j, "Send failed");
		return 1;
	}
	j->hdr_n = 0;
	j->hdr[0] = 0;
	j->state = XJ_HDR;
	return 1;
}

static int job_step_hdr(struct xe_many* j) {
	int any = 0;
	for (;;) {
		unsigned char tmp[2048];
		size_t room;
		size_t got = 0;
		int tr;
		char* mark;
		size_t hdrbytes;
		size_t extra;
		if (j->hdr_n + 1 >= sizeof(j->hdr)) {
			job_broken(j, "No HTTP response");
			return 1;
		}
		room = sizeof(j->hdr) - 1 - j->hdr_n;
		if (room > sizeof(tmp))
			room = sizeof(tmp);
		tr = job_take(j, tmp, room, &got);
		if (tr == 0)
			return any;
		if (tr < 0) {
			job_broken(j, "No HTTP response");
			return 1;
		}
		memcpy(j->hdr + j->hdr_n, tmp, got);
		j->hdr_n += got;
		j->hdr[j->hdr_n] = 0;
		any = 1;
		job_touch(j);
		mark = strstr(j->hdr, "\r\n\r\n");
		if (!mark)
			continue;
		hdrbytes = (size_t)(mark - j->hdr) + 4;
		extra = j->hdr_n - hdrbytes;
		if (extra > sizeof(j->pend)) {
			job_broken(j, "No HTTP response");
			return 1;
		}
		if (extra) {
			memcpy(j->pend, j->hdr + hdrbytes, extra);
			j->pend_len = (int)extra;
			j->pend_pos = 0;
		}
		j->hdr[hdrbytes] = 0;
		j->hdr_n = hdrbytes;
		if (job_headers_done(j) != 0)
			return 1;
		j->state = XJ_BODY;
		return 1;
	}
}

static int job_step_body(struct xe_many* j) {
	if (j->chunked)
		return job_body_chunked(j);
	return job_body_raw(j);
}

static int job_step(struct xe_many* j) {
	int prog = 0;
	int guard = 0;
	if (!j || j->state == XJ_FREE || j->state == XJ_DONE || j->state == XJ_FAIL ||
		j->state == XJ_WARM)
		return 0;
	while (guard++ < 16) {
		int prev = j->state;
		int p = 0;
		if (job_stale(j))
			return 1;
		if (prev == XJ_CONNECT)
			p = job_step_connect(j);
		else if (prev == XJ_TLS)
			p = job_step_tls(j);
		else if (prev == XJ_SEND)
			p = job_step_send(j);
		else if (prev == XJ_HDR)
			p = job_step_hdr(j);
		else if (prev == XJ_BODY)
			p = job_step_body(j);
		else
			break;
		if (!p)
			break;
		prog = 1;
		if (j->state == XJ_DONE || j->state == XJ_FAIL)
			break;
		if (j->state == prev && (prev == XJ_TLS || prev == XJ_BODY))
			break;
	}
	return prog;
}

static int job_arm(struct xe_many* j, int idx, const char* url, xe_http_response* out,
				   size_t cap, const char* ua) {
	memset(j, 0, sizeof(*j));
	j->fd = -1;
	j->idx = idx;
	j->out = out;
	j->max_body = cap ? cap : 1;
	j->ua = ua;
	j->content_len = -1;
	j->rc = -1;
	if (!url)
		url = "";
	snprintf(j->url, sizeof(j->url), "%s", url);
	if (job_open(j) < 0) {
		job_close(j);
		j->rc = -1;
		j->state = XJ_FAIL;
		return -1;
	}
	return 0;
}

static void job_retire(struct xe_many* j, int* rcs, int* active) {
	if (rcs && j->idx >= 0)
		rcs[j->idx] = j->rc;
	j->state = XJ_FREE;
	j->out = NULL;
	j->idx = -1;
	if (active && *active > 0)
		(*active)--;
}

static int job_slot(struct xe_many* jobs) {
	int i;
	for (i = 0; i < XE_HTTP_MANY_MAX; i++) {
		if (jobs[i].state == XJ_FREE)
			return i;
	}
	return -1;
}

static int origin_eq(const struct xe_url* a, const struct xe_url* b) {
	return a && b && a->https == b->https && a->port == b->port && ck_ieq(a->host, b->host);
}

struct xe_warm_conn {
	int used;
	int fd;
	uint32_t ip;
	struct xe_url u;
	struct xe_reader r;
};

/* Kept across xe_http_get_many calls so the stylesheet connection is
 * still there for the images on that host. */
static struct xe_warm_conn g_warm[XE_WARM_MAX];

static void warm_drop(int k) {
	struct xe_warm_conn* w;
	if (k < 0 || k >= XE_WARM_MAX)
		return;
	w = &g_warm[k];
	if (!w->used)
		return;
	if (w->r.ssl)
		mbedtls_ssl_close_notify(w->r.ssl);
	tls_free(&w->r);
	if (w->fd >= 0)
		_KeCloseFile(w->fd);
	memset(w, 0, sizeof(*w));
	w->fd = -1;
}

static void job_note(struct xe_many* j, int* rcs) {
	if (!j)
		return;
	if (rcs && j->idx >= 0)
		rcs[j->idx] = j->rc;
	j->idx = -1;
	j->out = NULL;
}

/* Move a finished socket off this job. The bio context has to follow the
 * reader, because mbedtls calls back into that pointer. */
static void warm_park(struct xe_many* j) {
	int k;
	int slot = -1;
	int same = -1;
	if (!j)
		return;
	if (j->state != XJ_WARM || j->fd < 0) {
		if (j->fd >= 0)
			job_close(j);
		j->state = XJ_DONE;
		return;
	}
	for (k = 0; k < XE_WARM_MAX; k++) {
		if (!g_warm[k].used) {
			if (slot < 0)
				slot = k;
			continue;
		}
		if (origin_eq(&g_warm[k].u, &j->u))
			same = k;
	}
	/* Two live sockets to one host both stay. A third host drops the
	 * oldest only when the table is already full. */
	if (slot < 0 && same >= 0) {
		warm_drop(same);
		slot = same;
	}
	if (slot < 0) {
		warm_drop(0);
		slot = 0;
	}
	g_warm[slot].used = 1;
	g_warm[slot].fd = j->fd;
	g_warm[slot].ip = j->ip;
	g_warm[slot].u = j->u;
	g_warm[slot].r = j->r;
	g_warm[slot].r.fd = j->fd;
	if (g_warm[slot].r.ssl)
		mbedtls_ssl_set_bio(g_warm[slot].r.ssl, &g_warm[slot].r, tls_send, tls_recv, NULL);
	j->fd = -1;
	j->r.fd = -1;
	j->r.ssl = NULL;
	j->r.slot = NULL;
	j->tls_ready = 0;
	j->state = XJ_DONE;
}

static int job_send_next(struct xe_many* j, int idx, const char* url,
						 xe_http_response* out, size_t cap, const char* ua) {
	j->idx = idx;
	j->out = out;
	j->max_body = cap ? cap : 1;
	j->ua = ua;
	j->content_len = -1;
	j->rc = -1;
	j->keepalive = 1;
	j->may_retry = 1;
	j->tls_ready = j->r.ssl ? 1 : 0;
	j->hops = 0;
	j->hdr_n = 0;
	j->hdr[0] = 0;
	j->req_off = 0;
	j->req_len = 0;
	j->pend_len = 0;
	j->pend_pos = 0;
	j->body_got = 0;
	j->chunk_left = 0;
	j->chunk_line_n = 0;
	j->chunked = 0;
	j->body_mode = XB_RAW;
	j->loc[0] = 0;
	j->idle_ms = _KeGetCurrentMS();
	j->waits = 0;
	if (!url)
		url = "";
	snprintf(j->url, sizeof(j->url), "%s", url);
	if (parse_url(j->url, &j->u) != 0) {
		job_fail(j, "Bad URL");
		return -1;
	}
	if (job_build_req(j) < 0) {
		job_fail(j, "Out of memory");
		return -1;
	}
	j->state = XJ_SEND;
	if (j->out)
		j->out->tls = j->u.https;
	_KePrint("[http] reuse %s\n", j->u.host);
	return 0;
}

static int job_rearm(struct xe_many* j, int idx, const char* url,
					 xe_http_response* out, size_t cap, const char* ua) {
	int fd = j->fd;
	uint32_t ip = j->ip;
	struct xe_reader r = j->r;
	memset(j, 0, sizeof(*j));
	j->r = r;
	j->fd = fd;
	j->r.fd = fd;
	j->ip = ip;
	return job_send_next(j, idx, url, out, cap, ua);
}

static int warm_take(struct xe_many* j, const struct xe_url* u, int idx, const char* url,
					 xe_http_response* out, size_t cap, const char* ua) {
	int k;
	for (k = 0; k < XE_WARM_MAX; k++) {
		if (!g_warm[k].used || !origin_eq(&g_warm[k].u, u))
			continue;
		memset(j, 0, sizeof(*j));
		j->r = g_warm[k].r;
		j->fd = g_warm[k].fd;
		j->r.fd = j->fd;
		j->ip = g_warm[k].ip;
		if (j->r.ssl)
			mbedtls_ssl_set_bio(j->r.ssl, &j->r, tls_send, tls_recv, NULL);
		g_warm[k].used = 0;
		g_warm[k].fd = -1;
		g_warm[k].r.ssl = NULL;
		g_warm[k].r.slot = NULL;
		return job_send_next(j, idx, url, out, cap, ua);
	}
	return -1;
}

static int host_busy(struct xe_many* jobs, const struct xe_url* u) {
	int i;
	int n = 0;
	for (i = 0; i < XE_HTTP_MANY_MAX; i++) {
		int st = jobs[i].state;
		if (st == XJ_FREE || st == XJ_DONE || st == XJ_FAIL)
			continue;
		if (origin_eq(&jobs[i].u, u))
			n++;
	}
	return n;
}

static int job_warm_slot(struct xe_many* jobs, const struct xe_url* u) {
	int i;
	for (i = 0; i < XE_HTTP_MANY_MAX; i++) {
		if (jobs[i].state == XJ_WARM && origin_eq(&jobs[i].u, u))
			return i;
	}
	return -1;
}

int xe_http_get_many(const char** urls, int n, const char* user_agent,
					 xe_http_response* outs, const size_t* max_body, int* rcs) {
	struct xe_many* jobs;
	int active = 0;
	int next = 0;
	int spins = 0;
	int i;
	if (n < 0 || !urls || !outs)
		return -1;
	if (n == 0)
		return 0;
	for (i = 0; i < n; i++) {
		memset(&outs[i], 0, sizeof(outs[i]));
		if (rcs)
			rcs[i] = -1;
	}
	jobs = (struct xe_many*)malloc(sizeof(struct xe_many) * XE_HTTP_MANY_MAX);
	if (!jobs) {
		for (i = 0; i < n; i++)
			set_err(&outs[i], "Out of memory");
		return -1;
	}
	memset(jobs, 0, sizeof(struct xe_many) * XE_HTTP_MANY_MAX);
	for (i = 0; i < XE_HTTP_MANY_MAX; i++) {
		jobs[i].fd = -1;
		jobs[i].idx = -1;
		jobs[i].state = XJ_FREE;
	}
	_KePrint("[http] overlap %d\n", n);
	while (next < n || active > 0) {
		int progressed = 0;
		int started = 0;
		int blocked = 0;
		/* Nothing left to send. Park finished sockets for the next call
		 * (the images that follow a stylesheet on the same host). */
		if (next >= n) {
			for (i = 0; i < XE_HTTP_MANY_MAX; i++) {
				if (jobs[i].state != XJ_WARM)
					continue;
				job_note(&jobs[i], rcs);
				warm_park(&jobs[i]);
			}
		}
		for (i = 0; i < XE_HTTP_MANY_MAX; i++) {
			struct xe_many* j = &jobs[i];
			if (j->state == XJ_FREE)
				continue;
			if (j->state == XJ_WARM) {
				if (j->idx >= 0)
					job_note(j, rcs);
				continue;
			}
			if (j->state == XJ_DONE || j->state == XJ_FAIL) {
				job_retire(j, rcs, &active);
				continue;
			}
			if (job_step(j))
				progressed = 1;
			if (j->state == XJ_WARM && j->idx >= 0)
				job_note(j, rcs);
			if (j->state == XJ_DONE || j->state == XJ_FAIL)
				job_retire(j, rcs, &active);
		}
		/* Reuse a live socket before opening another. A host already at
		 * the cold cap waits, so twenty images do not mean twenty
		 * handshakes. A blocking connect (old kernel) still starts only
		 * one of those per pass. */
		while (!blocked && next < n) {
			struct xe_url probe;
			size_t cap = 512 * 1024;
			int slot;
			int w;
			int pp;
			if (max_body)
				cap = max_body[next] ? max_body[next] : 1;
			pp = parse_url(urls[next] ? urls[next] : "", &probe);
			if (pp == 0) {
				w = job_warm_slot(jobs, &probe);
				if (w >= 0) {
					if (jobs[w].idx >= 0)
						job_note(&jobs[w], rcs);
					if (job_rearm(&jobs[w], next, urls[next], &outs[next], cap,
								  user_agent) != 0)
						job_retire(&jobs[w], rcs, NULL);
					next++;
					started = 1;
					continue;
				}
				if (host_busy(jobs, &probe) >= XE_COLD_PER_HOST)
					break;
			}
			if (active >= XE_HTTP_MANY_MAX)
				break;
			slot = job_slot(jobs);
			if (slot < 0) {
				for (i = 0; i < XE_HTTP_MANY_MAX; i++) {
					if (jobs[i].state != XJ_WARM)
						continue;
					job_note(&jobs[i], rcs);
					warm_park(&jobs[i]);
					job_retire(&jobs[i], rcs, &active);
					break;
				}
				slot = job_slot(jobs);
				if (slot < 0)
					break;
			}
			if (pp == 0 &&
				warm_take(&jobs[slot], &probe, next, urls[next], &outs[next], cap,
						  user_agent) == 0) {
				active++;
				next++;
				started = 1;
				continue;
			}
			if (job_arm(&jobs[slot], next, urls[next], &outs[next], cap, user_agent) == 0) {
				active++;
				if (jobs[slot].state != XJ_CONNECT)
					blocked = 1;
			} else {
				job_retire(&jobs[slot], rcs, NULL);
			}
			next++;
			started = 1;
		}
		if (!progressed && !started && active > 0) {
			_KeProcessSleep(20);
			spins = 0;
			for (i = 0; i < XE_HTTP_MANY_MAX; i++) {
				if (jobs[i].state == XJ_FREE || jobs[i].state == XJ_DONE ||
					jobs[i].state == XJ_FAIL || jobs[i].state == XJ_WARM)
					continue;
				jobs[i].waits++;
				if (jobs[i].waits > XE_MANY_WAITS) {
					_KePrint("[http] stall\n");
					job_fail(&jobs[i], "Timed out");
				}
			}
		} else if (++spins > 100000) {
			for (i = 0; i < XE_HTTP_MANY_MAX; i++) {
				if (jobs[i].state == XJ_FREE || jobs[i].state == XJ_WARM)
					continue;
				_KePrint("[http] stall\n");
				job_fail(&jobs[i], "Timed out");
				job_retire(&jobs[i], rcs, &active);
			}
			break;
		}
		active = 0;
		for (i = 0; i < XE_HTTP_MANY_MAX; i++) {
			if (jobs[i].state != XJ_FREE)
				active++;
		}
	}
	for (i = 0; i < XE_HTTP_MANY_MAX; i++) {
		if (jobs[i].state == XJ_WARM) {
			job_note(&jobs[i], rcs);
			warm_park(&jobs[i]);
		} else if (jobs[i].fd >= 0) {
			job_close(&jobs[i]);
		}
	}
	free(jobs);
	return 0;
}

int xe_http_get(const char* url, const char* method, const char* user_agent,
				xe_http_response* out, size_t max_body) {
	char current[3072];
	char* body;
	size_t body_len;
	char ctype[80];
	int hops;
	if (!out)
		return -1;
	memset(out, 0, sizeof(*out));
	body = g_req_body;
	body_len = g_req_body_len;
	snprintf(ctype, sizeof(ctype), "%s", g_req_ctype);
	g_req_body = NULL;
	g_req_body_len = 0;
	g_req_ctype[0] = 0;
	if (!url || !url[0]) {
		free(body);
		set_err(out, "Bad URL");
		return -1;
	}
	snprintf(current, sizeof(current), "%s", url);
	snprintf(g_last_url, sizeof(g_last_url), "%s", current);
	g_ck_hold_now = ck_now();
	g_ck_hold = 1;
	for (hops = 0; hops <= XE_MAX_REDIRECTS; hops++) {
		struct xe_url u;
		char location[3072];
		char next[3072];
		int pu = parse_url(current, &u);
		int rc;
		xe_http_response_free(out);
		out->error[0] = 0;
		out->headers[0] = 0;
		out->status = 0;
		if (pu == -2) {
			free(body);
			g_ck_hold = 0;
			set_err(out, "IPv6-only hosts are not reachable yet");
			return -1;
		}
		if (pu < 0) {
			free(body);
			g_ck_hold = 0;
			set_err(out, "Bad URL");
			return -1;
		}
		/* A POST body is only the first hop. A redirect is fetched as GET. */
		rc = fetch_once(&u, method, user_agent, hops == 0 ? (const unsigned char*)body : NULL,
						hops == 0 ? body_len : 0, hops == 0 && body ? ctype : NULL, out, max_body,
						location, sizeof(location));
		if (rc < 0) {
			free(body);
			g_ck_hold = 0;
			return -1;
		}
		if (out->status != 301 && out->status != 302 && out->status != 303 && out->status != 307 &&
			out->status != 308)
			break;
		if (!location[0])
			break;
		if (hops == XE_MAX_REDIRECTS) {
			free(body);
			g_ck_hold = 0;
			set_err(out, "Too many redirects");
			return -1;
		}
		if (join_location(&u, location, next, sizeof(next)) < 0) {
			free(body);
			g_ck_hold = 0;
			set_err(out, "Bad redirect");
			return -1;
		}
		if (u.https && !strncmp(next, "http://", 7))
			out->https_to_http = 1;
		{
			struct xe_url nu;
			char path[48];
			const char* q;
			size_t nlen = 0;
			path[0] = 0;
			if (parse_url(next, &nu) == 0) {
				q = strchr(nu.path, '?');
				nlen = q ? (size_t)(q - nu.path) : strlen(nu.path);
				if (nlen >= sizeof(path))
					nlen = sizeof(path) - 1;
				memcpy(path, (void*)nu.path, nlen);
				path[nlen] = 0;
				_KePrint("[https] redirect %d %s %s\n", out->status, nu.host, path);
			} else {
				_KePrint("[https] redirect %d\n", out->status);
			}
		}
		snprintf(current, sizeof(current), "%s", next);
		snprintf(g_last_url, sizeof(g_last_url), "%s", current);
		xe_http_response_free(out);
	}
	g_ck_hold = 0;
	free(body);
	snprintf(g_last_url, sizeof(g_last_url), "%s", current);
	if (out->status >= 400) {
		snprintf(out->error, sizeof(out->error), "HTTP %d", out->status);
		return 22;
	}
	return 0;
}
