/* NTRIP client for the LC29H(DA) GNSS HAT.
 *
 * Streams RTCM3 corrections from an NTRIP caster into the receiver's UART,
 * sends the receiver's GGA back to the caster, and re-serves the receiver's
 * NMEA output to TCP clients (default port 10110).
 *
 * Config: environment (/etc/default/ntrip-rtk). IPv6/NAT64 aware.
 */
#include "common.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_CLIENTS 8

static struct {
    const char *host, *port, *mount, *user, *pass, *dev;
    int nmea_port;
    double gga_interval;
} cfg;

#define log(...) (printf(__VA_ARGS__), putchar('\n'))

/* ---------- caster ---------- */

static void base64(const char *in, char *out)
{
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const unsigned char *s = (const unsigned char *)in;
    size_t n = strlen(in);
    for (size_t i = 0; i < n; i += 3) {
        unsigned v = s[i] << 16 | (i + 1 < n ? s[i + 1] << 8 : 0) | (i + 2 < n ? s[i + 2] : 0);
        *out++ = t[v >> 18 & 63];
        *out++ = t[v >> 12 & 63];
        *out++ = i + 1 < n ? t[v >> 6 & 63] : '=';
        *out++ = i + 2 < n ? t[v & 63] : '=';
    }
    *out = 0;
}

/* Connect, send the request, check for "200". Leftover RTCM goes to pend. */
static int try_caster(const struct sockaddr *sa, socklen_t len, const char *req,
                      char *pend, size_t *npend)
{
    char ip[INET6_ADDRSTRLEN];
    const void *a = sa->sa_family == AF_INET6 ? (const void *)&((const struct sockaddr_in6 *)sa)->sin6_addr
                                              : (const void *)&((const struct sockaddr_in *)sa)->sin_addr;
    inet_ntop(sa->sa_family, a, ip, sizeof ip);

    int s = socket(sa->sa_family, SOCK_STREAM, 0);
    struct timeval tv = {8, 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    if (connect(s, sa, len) < 0 || send(s, req, strlen(req), MSG_NOSIGNAL) < 0) {
        log("caster %s failed: %s", ip, errno == EINPROGRESS ? "timed out" : strerror(errno));
        close(s);
        return -1;
    }

    char hdr[1024];
    size_t n = 0;
    while (n < sizeof hdr && !memmem(hdr, n, "\r\n", 2)) {
        ssize_t r = recv(s, hdr + n, sizeof hdr - n, 0);
        if (r <= 0)
            break;
        n += (size_t)r;
    }
    char *eol = memmem(hdr, n, "\r\n", 2);
    size_t l1 = eol ? (size_t)(eol - hdr) : n;
    if (!eol || !memmem(hdr, l1, "200", 3)) {
        log("caster %s refused: %.*s", ip, (int)(l1 > 80 ? 80 : l1), hdr);
        close(s);
        return -1;
    }

    /* Drop the rest of the response header; keep any RTCM after it. */
    char *body = memmem(hdr, n, "\r\n\r\n", 4);
    body = body ? body + 4 : eol + 2;
    *npend = (size_t)(hdr + n - body);
    memcpy(pend, body, *npend);

    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof on);
    fcntl(s, F_SETFL, O_NONBLOCK);
    log("connected to caster %s:%s/%s", ip, cfg.port, cfg.mount);
    return s;
}

/* Direct address first, then NAT64 (64:ff9b::/96) for IPv4 literals. */
static int connect_caster(char *pend, size_t *npend)
{
    char req[1024], cred[256], tok[352];
    int k = snprintf(req, sizeof req,
                     "GET /%s HTTP/1.0\r\nUser-Agent: NTRIP ntrip-rtk/1.0\r\nAccept: */*\r\n", cfg.mount);
    if (*cfg.user) {
        snprintf(cred, sizeof cred, "%s:%s", cfg.user, cfg.pass);
        base64(cred, tok);
        k += snprintf(req + k, sizeof req - k, "Authorization: Basic %s\r\n", tok);
    }
    snprintf(req + k, sizeof req - k, "\r\n");

    int s = -1;
    struct addrinfo hints = {.ai_socktype = SOCK_STREAM}, *res, *ai;
    if (getaddrinfo(cfg.host, cfg.port, &hints, &res) == 0) {
        for (ai = res; ai && s < 0; ai = ai->ai_next)
            s = try_caster(ai->ai_addr, ai->ai_addrlen, req, pend, npend);
        freeaddrinfo(res);
    }

    struct in_addr v4;
    if (s < 0 && inet_pton(AF_INET, cfg.host, &v4) == 1) {
        struct sockaddr_in6 a = {.sin6_family = AF_INET6, .sin6_port = htons(atoi(cfg.port))};
        a.sin6_addr.s6_addr[1] = 0x64;
        a.sin6_addr.s6_addr[2] = 0xff;
        a.sin6_addr.s6_addr[3] = 0x9b;
        memcpy(&a.sin6_addr.s6_addr[12], &v4, 4);
        s = try_caster((struct sockaddr *)&a, sizeof a, req, pend, npend);
    }
    return s;
}

/* ---------- NMEA ---------- */

static char gga[256]; /* latest GGA sentence, CRLF-terminated */

static void track_gga(const char *buf, size_t n)
{
    static char line[512];
    static size_t ln;
    for (size_t i = 0; i < n; i++) {
        if (buf[i] != '\n') {
            if (ln < sizeof line - 1)
                line[ln++] = buf[i];
            continue;
        }
        while (ln && line[ln - 1] == '\r')
            ln--;
        line[ln] = 0;
        if (ln > 6 && !memcmp(line + 3, "GGA", 3))
            snprintf(gga, sizeof gga, "%s\r\n", line);
        ln = 0;
    }
}

static void gga_field(int k, char *out, size_t n)
{
    const char *s = gga;
    while (k-- && (s = strchr(s, ',')))
        s++;
    if (!*gga || !s) {
        snprintf(out, n, "?");
        return;
    }
    snprintf(out, n, "%.*s", (int)strcspn(s, ",\r\n"), s);
}

static void log_status(unsigned long rtcm, int caster_up, int nclients)
{
    static const char *fix[] = {"none", "GPS", "DGPS", "3", "RTK-FIXED", "RTK-FLOAT", "DR"};
    char q[16], sats[16], hdop[16];
    gga_field(6, q, sizeof q);
    gga_field(7, sats, sizeof sats);
    gga_field(8, hdop, sizeof hdop);
    int qi = *q == '?' ? -1 : atoi(q);
    log("fix=%s sats=%s hdop=%s rtcm_rx=%luB caster=%s nmea_clients=%d",
        qi >= 0 && qi <= 6 ? fix[qi] : q, sats, hdop, rtcm, caster_up ? "up" : "down", nclients);
}

/* ---------- main loop ---------- */

static int nmea_server(int port)
{
    int s = socket(AF_INET6, SOCK_STREAM, 0), on = 1, off = 0;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof off);
    struct sockaddr_in6 a = {.sin6_family = AF_INET6, .sin6_port = htons(port), .sin6_addr = in6addr_any};
    if (bind(s, (struct sockaddr *)&a, sizeof a) < 0 || listen(s, 8) < 0) {
        perror("nmea server");
        exit(1);
    }
    return s;
}

int main(void)
{
    cfg.host = env_str("NTRIP_HOST", "142.41.245.88");
    cfg.port = env_str("NTRIP_PORT", "2107");
    cfg.mount = env_str("NTRIP_MOUNT", "Gatineau_RTCM3x");
    cfg.user = env_str("NTRIP_USER", "");
    cfg.pass = env_str("NTRIP_PASS", "");
    cfg.dev = env_str("GPS_DEV", "/dev/ttyS0");
    cfg.nmea_port = atoi(env_str("NMEA_TCP_PORT", "10110"));
    cfg.gga_interval = atof(env_str("GGA_INTERVAL", "10"));

    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGPIPE, SIG_IGN);

    int ser = serial_open(cfg.dev);
    if (ser < 0)
        return 1;
    int srv = nmea_server(cfg.nmea_port);

    int cl[MAX_CLIENTS], ncl = 0, caster = -1;
    char buf[8192], pend[1024];
    size_t npend;
    unsigned long rtcm = 0;
    double next_conn = 0, last_rtcm = 0, last_gga_sent = 0, last_status = 0;

    for (;;) {
        double t = now();
        if (caster < 0 && t >= next_conn) {
            caster = connect_caster(pend, &npend);
            if (caster < 0) {
                next_conn = t + 15;
            } else {
                last_rtcm = t;
                write_all(ser, pend, npend);
                rtcm += npend;
            }
        }

        struct pollfd p[3 + MAX_CLIENTS] = {{ser, POLLIN, 0}, {srv, POLLIN, 0}, {caster, POLLIN, 0}};
        for (int i = 0; i < ncl; i++)
            p[3 + i] = (struct pollfd){cl[i], POLLIN, 0};
        poll(p, 3 + ncl, 1000);
        t = now();

        /* Client input is ignored; EOF/error means it left. */
        for (int i = 0; i < ncl; i++)
            if (p[3 + i].revents && recv(cl[i], buf, sizeof buf, MSG_DONTWAIT) <= 0) {
                close(cl[i]);
                cl[i] = -1;
            }

        if (p[0].revents & POLLIN) {
            ssize_t r = read(ser, buf, sizeof buf);
            if (r > 0) {
                for (int i = 0; i < ncl; i++)
                    if (cl[i] >= 0 && send(cl[i], buf, (size_t)r, MSG_NOSIGNAL | MSG_DONTWAIT) != r) {
                        close(cl[i]);
                        cl[i] = -1;
                    }
                track_gga(buf, (size_t)r);
            }
        }

        int n = 0; /* compact client list */
        for (int i = 0; i < ncl; i++)
            if (cl[i] >= 0)
                cl[n++] = cl[i];
        ncl = n;

        if (p[1].revents & POLLIN) {
            struct sockaddr_in6 a;
            socklen_t al = sizeof a;
            int c = accept(srv, (struct sockaddr *)&a, &al);
            if (c >= 0 && ncl < MAX_CLIENTS) {
                char ip[INET6_ADDRSTRLEN];
                inet_ntop(AF_INET6, &a.sin6_addr, ip, sizeof ip);
                cl[ncl++] = c;
                log("NMEA client connected: %s", ip);
            } else if (c >= 0) {
                close(c);
            }
        }

        if (caster >= 0 && p[2].revents) {
            ssize_t r = recv(caster, buf, sizeof buf, 0);
            if (r > 0) {
                write_all(ser, buf, (size_t)r);
                rtcm += (size_t)r;
                last_rtcm = t;
            } else if (r == 0 || (errno != EAGAIN && errno != EINTR)) {
                if (r == 0)
                    log("caster closed connection");
                else
                    log("caster read error: %s", strerror(errno));
                close(caster);
                caster = -1;
                next_conn = t + 5;
            }
        }

        if (caster >= 0 && t - last_rtcm > 30) {
            log("no RTCM for 30s, reconnecting");
            close(caster);
            caster = -1;
            next_conn = t + 2;
        }

        if (caster >= 0 && *gga && t - last_gga_sent >= cfg.gga_interval) {
            send(caster, gga, strlen(gga), MSG_NOSIGNAL);
            last_gga_sent = t;
        }

        if (t - last_status >= 30) {
            last_status = t;
            log_status(rtcm, caster >= 0, ncl);
        }
    }
}
