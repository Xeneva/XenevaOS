/**
 * Gopher client for XenevaOS (terminal app, RFC 1436).
 *
 * Usage: gopher <host> [port] [selector]   (default port 70)
 *
 * Renders menus with numbered links; type a number to follow it.
 * Type 7 (search) prompts for a query. 'u <host>' switches servers,
 * 'q' quits. Text (0) and directory (1) listings render inline.
 */

#include <stdint.h>
#include <_xeneva.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/_keproc.h>
#include <sys/_kefile.h>
#include <sys/socket.h>
#include <sys/netdb.h>
#include <arpa/inet.h>

#define GOPHER_MAX_PAGE (32 * 1024)
#define GOPHER_MAX_LINKS 64

typedef struct {
    char type;
    char host[128];
    uint16_t port;
    char selector[256];
} GopherLink;

static GopherLink s_links[GOPHER_MAX_LINKS];
static int s_nlinks = 0;

static char s_host[128] = {0};
static uint16_t s_port = 70;
static char s_selector[256] = {0};

/* Raw line input with local echo + backspace (tty is raw, like xesh). */
static int NsReadLine(char* out, size_t outsz) {
    size_t len = 0;
    memset(out, 0, outsz);
    while (1) {
        char c = 0;
        int n = _KeReadFile(XENEVA_STDIN, &c, 1);
        if (n <= 0) {
            _KeProcessSleep(10);
            continue;
        }
        if (c == '\r' || c == '\n') {
            _KeWriteFile(XENEVA_STDOUT, (void*)"\r\n", 2);
            return (int)len;
        }
        if (c == 127 || c == 8) { /* DEL / backspace */
            if (len > 0) {
                len--;
                out[len] = 0;
                _KeWriteFile(XENEVA_STDOUT, (void*)"\b \b", 3);
            }
            continue;
        }
        if (c == 3) /* Ctrl+C */
            return -1;
        if ((unsigned char)c < 32)
            continue;
        if (len + 1 < outsz) {
            out[len++] = c;
            out[len] = 0;
            _KeWriteFile(XENEVA_STDOUT, &c, 1);
        }
    }
}

static int NsResolve(const char* host, uint32_t* ipaddr) {
    hostent* ent = gethostbyname(host);
    if (!ent)
        return -1;
    if (ent->h_addrtype != AF_INET || ent->h_length != 4 || !ent->h_addr_list[0])
        return -2;
    memcpy(ipaddr, ent->h_addr_list[0], sizeof(uint32_t));
    return 0;
}

/* Fetch one selector. Returns malloc'd NUL-terminated body (caller frees),
 * or NULL on failure. */
static char* NsGopherFetch(const char* host, uint16_t port, const char* selector) {
    uint32_t ipaddr = 0;
    if (NsResolve(host, &ipaddr) != 0) {
        printf("gopher: cannot resolve %s\n", host);
        return NULL;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("gopher: socket failed\n");
        return NULL;
    }

    sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);
    memcpy(&dest.sin_addr, &ipaddr, sizeof(uint32_t));

    if (connect(sock, (sockaddr_*)&dest, sizeof(dest)) < 0) {
        printf("gopher: connect to %s:%u failed\n", host, port);
        _KeCloseFile(sock);
        return NULL;
    }

    char req[300];
    memset(req, 0, sizeof(req));
    snprintf(req, sizeof(req), "%s\r\n", selector ? selector : "");
    if (sendto(sock, req, strlen(req), 0, (sockaddr*)&dest, sizeof(dest)) < 0) {
        printf("gopher: send failed\n");
        _KeCloseFile(sock);
        return NULL;
    }

    char* page = (char*)malloc(GOPHER_MAX_PAGE + 1);
    if (!page) {
        _KeCloseFile(sock);
        return NULL;
    }
    memset(page, 0, GOPHER_MAX_PAGE + 1);
    size_t got = 0;
    int idle = 0;
    char buf[2048];
    while (idle < 80 && got < GOPHER_MAX_PAGE) {
        memset(buf, 0, sizeof(buf));
        size_t want = sizeof(buf) - 1;
        if (want > GOPHER_MAX_PAGE - got)
            want = GOPHER_MAX_PAGE - got;
        int n = recvfrom(sock, buf, want, 0, NULL, NULL);
        if (n > 0) {
            idle = 0;
            memcpy(page + got, buf, (size_t)n);
            got += (size_t)n;
            continue;
        }
        if (n == 0 && got > 0)
            break;
        _KeProcessSleep(100);
        idle++;
    }
    _KeCloseFile(sock);
    page[got] = 0;
    if (got == 0) {
        free(page);
        return NULL;
    }
    return page;
}

static bool NsFollowable(char t) {
    return t == '0' || t == '1' || t == '7';
}

static void NsRenderPage(const char* page) {
    s_nlinks = 0;
    const char* p = page;
    while (*p && s_nlinks < GOPHER_MAX_LINKS) {
        const char* eol = strchr(p, '\n');
        size_t llen = eol ? (size_t)(eol - p) : strlen(p);
        while (llen > 0 && (p[llen - 1] == '\r' || p[llen - 1] == '\n'))
            llen--;
        if (llen == 0 || (llen == 1 && p[0] == '.')) {
            /* blank line, or lone "." (end-of-listing in menus) */
        } else if (llen < 2) {
            /* Single character: plain text. */
            char line[512];
            size_t cp = llen < sizeof(line) - 1 ? llen : sizeof(line) - 1;
            memcpy(line, (void*)p, cp);
            line[cp] = 0;
            printf("%s\n", line);
        } else {
            /* Menu line: <type><display>\t<selector>\t<host>\t<port>.
             * typeless lines (no tab at all) are plain file text. */
            char type = p[0];
            const char* f1 = p + 1;
            const char* t1 = strchr(f1, '\t');
            if (!t1) {
                char line[512];
                size_t cp = llen < sizeof(line) - 1 ? llen : sizeof(line) - 1;
                memcpy(line, (void*)p, cp);
                line[cp] = 0;
                printf("%s\n", line);
            } else {
                size_t dlen = (size_t)(t1 - f1);
            char disp[256];
            size_t dc = dlen < sizeof(disp) - 1 ? dlen : sizeof(disp) - 1;
            memcpy(disp, (void*)f1, dc);
            disp[dc] = 0;

            if (type == 'i') {
                printf("%s\n", disp);
            } else if (type == '3') {
                printf("!! %s\n", disp);
            } else if (NsFollowable(type) && s_nlinks < GOPHER_MAX_LINKS) {
                GopherLink* L = &s_links[s_nlinks];
                L->type = type;
                /* selector */
                const char* f2 = t1 ? t1 + 1 : "";
                const char* t2 = strchr(f2, '\t');
                size_t slen = t2 ? (size_t)(t2 - f2) : strlen(f2);
                if (slen > sizeof(L->selector) - 1)
                    slen = sizeof(L->selector) - 1;
                memcpy(L->selector, (void*)f2, slen);
                L->selector[slen] = 0;
                /* host */
                const char* f3 = t2 ? t2 + 1 : "";
                const char* t3 = strchr(f3, '\t');
                size_t hlen = t3 ? (size_t)(t3 - f3) : strlen(f3);
                if (hlen == 0 || (hlen == 1 && f3[0] == '+')) {
                    strcpy(L->host, s_host);
                } else {
                    if (hlen > sizeof(L->host) - 1)
                        hlen = sizeof(L->host) - 1;
                    memcpy(L->host, (void*)f3, hlen);
                    L->host[hlen] = 0;
                }
                /* port */
                L->port = s_port;
                if (t3 && t3[1] >= '0' && t3[1] <= '9') {
                    int prt = 0;
                    for (const char* d = t3 + 1; *d >= '0' && *d <= '9'; d++)
                        prt = prt * 10 + (*d - '0');
                    if (prt > 0 && prt < 65536)
                        L->port = (uint16_t)prt;
                }
                const char* kind = type == '1' ? "/" : (type == '7' ? " (search)" : "");
                printf("[%d] %s%s\n", s_nlinks + 1, disp, kind);
                s_nlinks++;
            } else {
                printf("(%c) %s\n", type, disp);
            }
            }
        }
        if (!eol)
            break;
        p = eol + 1;
    }
}

static void usage(void) {
    printf("Usage: gopher <host> [port] [selector]\n");
    printf("  gopher gopher.floodgap.com\n");
    printf("  gopher 10.0.2.2 7000 /path\n");
    printf("\nAt the gopher> prompt: number follows a link,\n"
           "'u <host>' switches servers, 'q' quits.\n");
}

int main(int argc, char* argv[]) {
    memset(s_host, 0, sizeof(s_host));
    memset(s_selector, 0, sizeof(s_selector));

    bool have_host = false;
    bool have_port = false;
    for (int i = 0; i < argc; i++) {
        const char* a = argv[i];
        if (!a || !a[0])
            continue;
        if (a[0] == '/')
            continue;
        if (strstr(a, ".exe"))
            continue;
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            usage();
            return 0;
        }
        if (a[0] == '-') {
            printf("gopher: unknown option %s\n", a);
            return 2;
        }
        if (!have_host) {
            const char* colon = strchr(a, ':');
            if (colon && colon != a) {
                size_t hlen = (size_t)(colon - a);
                if (hlen >= sizeof(s_host)) {
                    printf("gopher: hostname too long\n");
                    return 2;
                }
                memcpy(s_host, (void*)a, hlen);
                s_host[hlen] = 0;
                int prt = 0;
                for (const char* d = colon + 1; *d >= '0' && *d <= '9'; d++)
                    prt = prt * 10 + (*d - '0');
                if (prt > 0 && prt < 65536) {
                    s_port = (uint16_t)prt;
                    have_port = true;
                }
            } else {
                if (strlen(a) >= sizeof(s_host)) {
                    printf("gopher: hostname too long\n");
                    return 2;
                }
                strcpy(s_host, a);
            }
            have_host = true;
            continue;
        }
        if (!have_port) {
            const char* d = a;
            int prt = 0;
            while (*d >= '0' && *d <= '9') {
                prt = prt * 10 + (*d - '0');
                d++;
            }
            if (*d == 0 && prt > 0 && prt < 65536) {
                s_port = (uint16_t)prt;
                have_port = true;
                continue;
            }
        }
        /* remainder: selector words joined with space */
        if (strlen(s_selector) + strlen(a) + 2 < sizeof(s_selector)) {
            if (s_selector[0])
                strcat(s_selector, " ");
            strcat(s_selector, a);
        }
    }

    if (!have_host) {
        usage();
        return 2;
    }

    while (1) {
        printf("\n--- gopher://%s:%u/%s ---\n", s_host, s_port, s_selector);
        char* page = NsGopherFetch(s_host, s_port, s_selector);
        if (!page) {
            printf("gopher: fetch failed\n");
        } else {
            NsRenderPage(page);
            free(page);
        }

        printf("\ngopher> ");
        fflush(stdout);
        char line[300];
        if (NsReadLine(line, sizeof(line)) < 0) {
            printf("\n");
            return 0;
        }
        while (*line == ' ' || *line == '\t')
            memmove(line, line + 1, strlen(line));
        if (line[0] == 0 || !strcmp(line, "q") || !strcmp(line, "quit"))
            return 0;
        if (line[0] == 'u' && (line[1] == ' ' || line[1] == 0)) {
            const char* nh = line + 1;
            while (*nh == ' ')
                nh++;
            if (*nh) {
                strncpy(s_host, nh, sizeof(s_host) - 1);
                s_host[sizeof(s_host) - 1] = 0;
                s_selector[0] = 0;
                continue;
            }
            return 0;
        }
        /* number? follow link */
        {
            const char* d = line;
            int num = 0;
            while (*d >= '0' && *d <= '9') {
                num = num * 10 + (*d - '0');
                d++;
            }
            if (*d == 0 && num >= 1 && num <= s_nlinks) {
                GopherLink* L = &s_links[num - 1];
                if (L->type == '7') {
                    printf("search: ");
                    fflush(stdout);
                    char query[200];
                    if (NsReadLine(query, sizeof(query)) < 0)
                        return 0;
                    snprintf(s_selector, sizeof(s_selector), "%s\t%s", L->selector, query);
                } else {
                    strncpy(s_selector, L->selector, sizeof(s_selector) - 1);
                    s_selector[sizeof(s_selector) - 1] = 0;
                }
                strncpy(s_host, L->host, sizeof(s_host) - 1);
                s_host[sizeof(s_host) - 1] = 0;
                s_port = L->port;
                continue;
            }
        }
        /* anything else: new selector on the same host */
        strncpy(s_selector, line, sizeof(s_selector) - 1);
        s_selector[sizeof(s_selector) - 1] = 0;
    }
}
