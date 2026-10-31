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

#ifndef XENEVA_HTTPS_H
#define XENEVA_HTTPS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XE_HTTP_ERR_MAX 160
#define XE_HTTP_HDR_MAX 1024

typedef struct xe_http_response {
	int status;
	int tls;
	int https_to_http;
	char* body;
	size_t body_len;
	char error[XE_HTTP_ERR_MAX];
	char headers[XE_HTTP_HDR_MAX];
} xe_http_response;

/* GET or HEAD. Returns 0 on a completed 1xx-3xx response, 22 on HTTP
 * status >= 400 (body still filled), and -1 when the transfer failed. */
int xe_http_get(const char* url,
				const char* method,
				const char* user_agent,
				xe_http_response* out,
				size_t max_body);

/* How many GETs xe_http_get_many has in flight at once. Same-host
 * requests share a TLS connection. A host gets at most two new
 * handshakes; further GETs are keep-alive on those sockets. */
#define XE_HTTP_MANY_MAX 8

/* Fetch n independent GETs on this thread, XE_HTTP_MANY_MAX at a time.
 * outs[i] matches urls[i]. max_body is one cap per URL, or NULL for
 * 512 KiB each. rcs may be NULL; otherwise rcs[i] is 0, 22, or -1,
 * the same as xe_http_get. No request body. Does not change the last URL.
 * Returns 0 when the pump ran, or -1 if the arguments are unusable. */
int xe_http_get_many(const char** urls, int n, const char* user_agent,
					 xe_http_response* outs, const size_t* max_body, int* rcs);

void xe_http_response_free(xe_http_response* out);

/* Page the next request was started from. A request to that same host
 * sends it as Referer. NULL or "" clears it. */
void xe_http_set_referer(const char* url);

/* One request body for the next xe_http_get. The call consumes it.
 * NULL or a zero length clears a pending body. Used for form POST. */
void xe_http_set_body(const char* content_type, const void* body, size_t len);

/* URL of the last completed xe_http_get, after redirects. */
void xe_http_last_url(char* dst, size_t dstsz);

/* Where the trust anchors came from: "file:/etc/ssl/certs/cacert.pem",
 * "file:/cacert.pem", "embedded", or "uninitialized" before the first TLS
 * connection. */
const char* xe_http_ca_source(void);

/* Successful TLS handshakes since process start. resumed counts handshakes
 * that loaded a cached session; the peer may still choose a full handshake. */
void xe_http_tls_stats(int* full, int* resumed);

/* Store one Set-Cookie line, or one document.cookie assignment.
 * from_http is 1 for a response header and 0 for a script. */
void xe_cookie_set(const char* page_url, const char* set_cookie, int from_http);

/* "n=v; n2=v2" for a Cookie request header, including HttpOnly. */
int xe_cookie_header(const char* url, char* dst, size_t dstsz);

/* document.cookie. HttpOnly names are left out. */
int xe_cookie_document(const char* url, char* dst, size_t dstsz);

#ifdef __cplusplus
}
#endif

#endif
