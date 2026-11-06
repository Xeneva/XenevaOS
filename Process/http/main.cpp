/**
* curl: HTTP/1.1 GET/HEAD. https:// uses TLS 1.2 and 1.3 with a checked chain.
**/

#include <stdint.h>
#include <_xeneva.h>
#include <stdio.h>
#include <sys/_keproc.h>
#include <sys/_kefile.h>
#include <sys/socket.h>
#include <sys/netdb.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <https.h>

static int is_prog(const char* s) {
	if (!s || !s[0])
		return 1;
	if (s[0] == '/')
		return 1;
	if (strstr(s, ".exe"))
		return 1;
	return 0;
}

static void usage(void) {
	printf("Usage: curl [options...] <url>\n");
	printf("  curl example.com\n");
	printf("  curl http://example.com/\n");
	printf("  curl -v http://example.com\n");
	printf("  curl -I example.com\n");
	printf("  curl -o out.html example.com\n");
	printf("\nOptions:\n");
	printf("  -v        Verbose\n");
	printf("  -I        HEAD request (headers only)\n");
	printf("  -i        Include response headers\n");
	printf("  -o <file> Write body to file\n");
	printf("  -h        This help\n");
	printf("\nhttp:// and https:// (TLS 1.2 and 1.3, checked certificates).\n");
}

int main(int argc, char* argv[]) {
	int verbose = 0;
	int head_only = 0;
	int include_hdr = 0;
	const char* outfile = NULL;
	const char* url = NULL;
	int i;

	for (i = 0; i < argc; i++) {
		const char* a = argv[i];
		if (is_prog(a))
			continue;
		if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
			usage();
			return 0;
		}
		if (!strcmp(a, "-v") || !strcmp(a, "--verbose")) {
			verbose = 1;
			continue;
		}
		if (!strcmp(a, "-I") || !strcmp(a, "--head")) {
			head_only = 1;
			include_hdr = 1;
			continue;
		}
		if (!strcmp(a, "-i") || !strcmp(a, "--include")) {
			include_hdr = 1;
			continue;
		}
		if (!strcmp(a, "-o") || !strcmp(a, "--output")) {
			if (i + 1 < argc && !is_prog(argv[i + 1])) {
				i++;
				outfile = argv[i];
			}
			continue;
		}
		if (a[0] == '-') {
			fprintf(stderr, "curl: option %s: is unknown\n", a);
			return 2;
		}
		if (!url)
			url = a;
	}

	if (!url) {
		usage();
		return 2;
	}

	xe_http_response resp;
	memset(&resp, 0, sizeof(resp));
	int rc = xe_http_get(url, head_only ? "HEAD" : "GET", "curl/xeneva", &resp, 1024 * 1024);
	if (resp.https_to_http) {
		fprintf(stderr, "curl: redirect left HTTPS for HTTP\n");
		_KePrint("curl: redirect left HTTPS for HTTP\r\n");
	}
	if (verbose && resp.headers[0])
		fprintf(stderr, "%s", resp.headers);

	FILE* out = stdout;
	if (outfile) {
		out = fopen(outfile, "w+");
		if (!out) {
			fprintf(stderr, "curl: (23) Failed writing body\n");
			xe_http_response_free(&resp);
			return 23;
		}
	}
	if (include_hdr && resp.headers[0])
		fwrite(resp.headers, 1, strlen(resp.headers), out);
	if (!head_only && resp.body && resp.body_len)
		fwrite(resp.body, 1, resp.body_len, out);
	fflush(out);
	if (outfile)
		fclose(out);

	if (rc != 0) {
		const char* msg = resp.error[0] ? resp.error : "transfer failed";
		fprintf(stderr, "curl: %s\n", msg);
		_KePrint("curl: %s\r\n", msg);
		xe_http_response_free(&resp);
		return rc < 0 ? 1 : rc;
	}
	xe_http_response_free(&resp);
	return 0;
}
