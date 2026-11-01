/* Send NMEA/PAIR commands to the GPS UART and print replies.
 * Usage: gpscmd [secs] 'PQTMVERNO' ...   (no commands: dump raw output) */
#include "common.h"

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void read_for(int fd, double secs, int replies_only)
{
    char buf[4096], line[512];
    size_t n = 0;
    double end = now() + secs;

    while (now() < end) {
        struct pollfd p = {fd, POLLIN, 0};
        if (poll(&p, 1, 100) <= 0)
            continue;
        ssize_t r = read(fd, buf, sizeof buf);
        if (r <= 0)
            continue;
        if (!replies_only) {
            fwrite(buf, 1, (size_t)r, stdout);
            continue;
        }
        for (ssize_t i = 0; i < r; i++) {
            if (buf[i] == '\n') {
                line[n] = 0;
                if (!strncmp(line, "$PQTM", 5) || !strncmp(line, "$PAIR", 5))
                    printf("   %s\n", line);
                n = 0;
            } else if (buf[i] != '\r' && n < sizeof line - 1) {
                line[n++] = buf[i];
            }
        }
    }
}

int main(int argc, char **argv)
{
    int fd = serial_open(env_str("GPS_DEV", "/dev/ttyS0"));
    if (fd < 0)
        return 1;

    int i = 1;
    double secs = 2.0;
    char *end;
    if (i < argc) {
        double v = strtod(argv[i], &end);
        if (*end == 0 && end != argv[i]) {
            secs = v;
            i++;
        }
    }

    if (i == argc) {
        read_for(fd, secs, 0);
        return 0;
    }

    for (; i < argc; i++) {
        unsigned char cs = 0;
        for (const char *c = argv[i]; *c; c++)
            cs ^= (unsigned char)*c;
        char msg[512];
        int len = snprintf(msg, sizeof msg, "$%s*%02X\r\n", argv[i], cs);
        write_all(fd, msg, (size_t)len);
        printf(">> $%s*%02X\n", argv[i], cs);
        read_for(fd, secs, 1);
    }
    return 0;
}
