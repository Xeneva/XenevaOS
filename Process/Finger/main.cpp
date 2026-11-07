/**
 * Finger client for XenevaOS (terminal app, RFC 1288).
 *
 * Usage: finger [user@]host
 *   finger @host        -- remote user list
 *   finger root@host    -- query one user
 *
 * One-shot TCP/79 query: sends "user\r\n", prints everything until close.
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

static void usage(void) {
    printf("Usage: finger [user@]host[:port]\n");
    printf("  finger @localhost\n");
    printf("  finger root@example.com\n");
}

int main(int argc, char* argv[]) {
    const char* host = NULL;
    const char* user = "";
    uint16_t port = 79;
    char hostbuf[128];
    char userbuf[128];
    memset(hostbuf, 0, sizeof(hostbuf));
    memset(userbuf, 0, sizeof(userbuf));

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
            printf("finger: unknown option %s\n", a);
            return 2;
        }
        const char* at = strchr(a, '@');
        if (at) {
            size_t ulen = (size_t)(at - a);
            if (ulen >= sizeof(userbuf)) {
                printf("finger: user too long\n");
                return 2;
            }
            memcpy(userbuf, (void*)a, ulen);
            userbuf[ulen] = 0;
            user = userbuf;
            const char* hp = at + 1;
            const char* colon = strchr(hp, ':');
            if (colon && colon != hp) {
                size_t hlen = (size_t)(colon - hp);
                if (hlen >= sizeof(hostbuf)) {
                    printf("finger: hostname too long\n");
                    return 2;
                }
                memcpy(hostbuf, (void*)hp, hlen);
                hostbuf[hlen] = 0;
                host = hostbuf;
                int prt = 0;
                for (const char* d = colon + 1; *d >= '0' && *d <= '9'; d++)
                    prt = prt * 10 + (*d - '0');
                if (prt > 0 && prt < 65536)
                    port = (uint16_t)prt;
            } else {
                if (strlen(hp) >= sizeof(hostbuf)) {
                    printf("finger: hostname too long\n");
                    return 2;
                }
                strcpy(hostbuf, hp);
                host = hostbuf;
            }
        } else if (!host) {
            if (strlen(a) >= sizeof(hostbuf)) {
                printf("finger: hostname too long\n");
                return 2;
            }
            strcpy(hostbuf, a);
            host = hostbuf;
        }
    }

    if (!host || !host[0]) {
        usage();
        return 2;
    }

    hostent* ent = gethostbyname(host);
    if (!ent) {
        printf("finger: could not resolve %s\n", host);
        return 6;
    }
    if (ent->h_addrtype != AF_INET || ent->h_length != 4 || !ent->h_addr_list[0]) {
        printf("finger: no IPv4 address for %s\n", host);
        return 6;
    }
    uint32_t ipaddr = *(uint32_t*)ent->h_addr_list[0];

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("finger: socket failed\n");
        return 7;
    }

    sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);
    memcpy(&dest.sin_addr, &ipaddr, sizeof(uint32_t));

    if (connect(sock, (sockaddr_*)&dest, sizeof(dest)) < 0) {
        printf("finger: connect to %s:%u failed\n", host, port);
        _KeCloseFile(sock);
        return 7;
    }

    char req[160];
    memset(req, 0, sizeof(req));
    snprintf(req, sizeof(req), "%s\r\n", user);
    if (sendto(sock, req, strlen(req), 0, (sockaddr*)&dest, sizeof(dest)) < 0) {
        printf("finger: send failed\n");
        _KeCloseFile(sock);
        return 7;
    }

    char buf[2048];
    int idle = 0;
    int got_any = 0;
    while (idle < 80) {
        memset(buf, 0, sizeof(buf));
        int n = recvfrom(sock, buf, sizeof(buf) - 1, 0, NULL, NULL);
        if (n > 0) {
            got_any = 1;
            idle = 0;
            fwrite(buf, 1, (size_t)n, stdout);
            fflush(stdout);
            continue;
        }
        if (n == 0 && got_any)
            break;
        _KeProcessSleep(100);
        idle++;
    }
    if (!got_any)
        printf("finger: no reply (timeout)\n");

    _KeCloseFile(sock);
    return got_any ? 0 : 28;
}
