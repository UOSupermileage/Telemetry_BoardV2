#include "common.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

int serial_open(const char *dev)
{
    int fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        perror(dev);
        return -1;
    }
    /* Raw 8N1, no echo: echo would bounce NMEA back into the receiver. */
    struct termios t = {0};
    t.c_cflag = CS8 | CREAD | CLOCAL;
    cfsetispeed(&t, B115200);
    cfsetospeed(&t, B115200);
    tcsetattr(fd, TCSANOW, &t);
    tcflush(fd, TCIOFLUSH);
    fcntl(fd, F_SETFL, 0); /* reads are gated by poll(); writes must not be partial */
    return fd;
}

int write_all(int fd, const void *buf, size_t n)
{
    const char *p = buf;
    while (n) {
        ssize_t w = write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

const char *env_str(const char *name, const char *def)
{
    const char *v = getenv(name);
    return v && *v ? v : def;
}

double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}
