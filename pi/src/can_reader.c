/* MCP2222 + MCP2518FD CAN reader over USB HID (Linux hidraw, no libraries).
 * IO4 on the MCP2222 drives the MCP2518FD's SPI chip select.
 *
 * NOTE: the MCP2222 report codes below are placeholders carried over from
 * the original Python prototype; check them against the MCP2222 datasheet. */
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define USB_VID 0x04D8
#define USB_PID 0x00DD
#define REPORT  64

#define CMD_PIN_CONFIG 0x60
#define CMD_GPIO_SET   0x50
#define CMD_SPI_XFER   0x40

#define MCP2518FD_READ 0x03

static volatile sig_atomic_t running = 1;
static void on_sigint(int s) { (void)s; running = 0; }

/* Find the /dev/hidrawN whose USB VID:PID matches. */
static int hid_open(void)
{
    char want[64], path[300], line[256];
    snprintf(want, sizeof want, "HID_ID=0003:%08X:%08X", USB_VID, USB_PID);
    DIR *d = opendir("/sys/class/hidraw");
    struct dirent *e;
    int fd = -1;

    while (d && fd < 0 && (e = readdir(d))) {
        if (strncmp(e->d_name, "hidraw", 6))
            continue;
        snprintf(path, sizeof path, "/sys/class/hidraw/%s/device/uevent", e->d_name);
        FILE *f = fopen(path, "r");
        if (!f)
            continue;
        while (fgets(line, sizeof line, f))
            if (!strncmp(line, want, strlen(want))) {
                snprintf(path, sizeof path, "/dev/%s", e->d_name);
                fd = open(path, O_RDWR);
                break;
            }
        fclose(f);
    }
    if (d)
        closedir(d);
    return fd;
}

static int hid_write(int fd, const uint8_t *r) { return write(fd, r, REPORT) == REPORT ? 0 : -1; }

static void set_cs(int fd, int level)
{
    uint8_t r[REPORT] = {CMD_GPIO_SET, 0x01, 0x00, level ? 0x04 : 0x00, 0x04};
    hid_write(fd, r);
}

/* Assert CS, exchange tx over SPI, deassert CS. Returns bytes received. */
static int spi_xfer(int fd, const uint8_t *tx, int n, uint8_t *rx)
{
    uint8_t r[REPORT] = {CMD_SPI_XFER, (uint8_t)n};
    memcpy(r + 2, tx, (size_t)n);
    int got = 0;

    set_cs(fd, 0);
    struct pollfd p = {fd, POLLIN, 0};
    if (hid_write(fd, r) == 0 && poll(&p, 1, 100) > 0 && read(fd, r, REPORT) > 2) {
        memcpy(rx, r + 2, (size_t)n);
        got = n;
    }
    set_cs(fd, 1);
    return got;
}

int main(void)
{
    int fd = hid_open();
    if (fd < 0) {
        fprintf(stderr, "MCP2222 %04X:%04X not found (check lsusb and /dev/hidraw* permissions)\n",
                USB_VID, USB_PID);
        return 1;
    }
    signal(SIGINT, on_sigint);

    uint8_t cfg[REPORT] = {CMD_PIN_CONFIG}; /* IO4 -> GPIO output */
    hid_write(fd, cfg);
    usleep(50000);
    set_cs(fd, 1);

    printf("Listening for CAN messages from MCP2518FD... Ctrl+C to exit.\n");
    const uint8_t cmd[19] = {MCP2518FD_READ, 0x0E, 0x00}; /* + 16 clock bytes */
    uint8_t rx[19];

    while (running) {
        if (spi_xfer(fd, cmd, sizeof cmd, rx) == sizeof cmd) {
            const uint8_t *f = rx + 3; /* skip instruction/address echo */
            uint32_t id = (f[0] | f[1] << 8 | f[2] << 16 | (uint32_t)f[3] << 24) & 0x1FFFFFFF;
            int dlc = f[4] & 0x0F, len = dlc > 8 ? 8 : dlc;
            if (id) {
                printf("ID: 0x%X | DLC: %d | Data:", id, dlc);
                for (int i = 0; i < len; i++)
                    printf(" %02X", f[8 + i]);
                putchar('\n');
            }
        }
        usleep(10000);
    }
    close(fd);
    return 0;
}
