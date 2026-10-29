/*
 * btprobe -- measure the WCN3990 HCI channel on sagit/chiron directly.
 *
 * The KitKat libbt-vendor port opens /dev/ttyHS0 at 115200 with RTS/CTS, sends
 * the EDL version request and gets nothing back (rx stays 0 in
 * /proc/tty/driver/msm_serial_hs). A dead RX line with zero garbage means the
 * controller never drives its TX, so this tool exists to separate "wrong UART
 * settings" from "chip is not powered / held in reset": every mode reports the
 * UART byte counters and the modem status lines around each write.
 *
 * Run as root:
 *   btprobe stats                 UART/port identity and counters
 *   btprobe on | off              hold the chip powered (rails, gpio, clk,
 *                                 interrupts can then be read undisturbed)
 *   btprobe single [baud] [flow] [rts] [settle] [cycle]
 *                                 one EDL version request
 *   btprobe quick | full          baud x flow x rts x settle matrix
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define BT_PWR_DEVICE    "/dev/btpower"
#define BT_CMD_PWR_CTRL  0xbfad
#define BT_UART_DEVICE   "/dev/ttyHS0"
#define HS_STAT          "/proc/tty/driver/msm_serial_hs"
#define PINMUX_PINS      "/sys/kernel/debug/pinctrl/3400000.pinctrl/pinmux-pins"
#define HS_UART_PM       "/sys/devices/soc/c171000.uart/power/runtime_status"

/* msm_serial_hs power-management ioctl -- not in glibc's headers. */
#ifndef TIOCPMGET
#define TIOCPMGET 0x5441
#endif
#ifndef TIOCMSET
#define TIOCMSET 0x5418
#endif
#ifndef TIOCMGET
#define TIOCMGET 0x5415
#endif
#ifndef TIOCM_RTS
#define TIOCM_RTS 0x004
#endif
#ifndef TIOCM_DTR
#define TIOCM_DTR 0x002
#endif
#ifndef TIOCM_CTS
#define TIOCM_CTS 0x020
#endif
#ifndef TIOCM_DSR
#define TIOCM_DSR 0x040
#endif
#ifndef TIOCM_RNG
#define TIOCM_RNG 0x080
#endif
#ifndef TIOCM_CD
#define TIOCM_CD 0x010
#endif

/* EDL commands ride on vendor opcode 0xFC00 with the sub-command as the first
 * parameter byte (cmd_opcode_pack(HCI_VENDOR_CMD_OGF=0x3F, OCF=0)). */
static const uint8_t edl_ver_req[]   = {0x01, 0x00, 0xfc, 0x01, 0x19};
static const uint8_t edl_build_req[] = {0x01, 0x00, 0xfc, 0x01, 0x20};
static const uint8_t hci_reset[]     = {0x01, 0x03, 0x0c, 0x00};

static void msleep(int ms)
{
    struct timespec t = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&t, NULL);
}

/* tx/rx byte counters the HS driver keeps for port 0. */
static void uart_counters(long *tx, long *rx)
{
    char buf[512];
    FILE *f = fopen(HS_STAT, "r");

    *tx = -1;
    *rx = -1;
    if (!f)
        return;
    if (fgets(buf, sizeof(buf), f) &&
        fgets(buf, sizeof(buf), f)) {
        char *p = strstr(buf, "tx:");
        if (p)
            *tx = strtol(p + 3, NULL, 10);
        p = strstr(buf, "rx:");
        if (p)
            *rx = strtol(p + 3, NULL, 10);
    }
    fclose(f);
}

static void dump_matching(const char *path, const char *needle)
{
    char buf[1024];
    FILE *f = fopen(path, "r");

    printf("  %s:\n", path);
    if (!f) {
        printf("    (open failed: %s)\n", strerror(errno));
        return;
    }
    while (fgets(buf, sizeof(buf), f)) {
        buf[strcspn(buf, "\n")] = 0;
        if (needle && !strstr(buf, needle))
            continue;
        printf("    %s\n", buf);
    }
    fclose(f);
}

static void dump_line(const char *path)
{
    dump_matching(path, NULL);
}

static int bt_power(int on)
{
    int fd = open(BT_PWR_DEVICE, O_RDWR);
    int ret;

    if (fd < 0) {
        printf("  btpower open failed: %s\n", strerror(errno));
        return -1;
    }
    ret = ioctl(fd, BT_CMD_PWR_CTRL, (unsigned long)on);
    if (ret < 0)
        printf("  power %s failed: %s\n", on ? "on" : "off", strerror(errno));
    else
        printf("  BT_CMD_PWR_CTRL %d -> %d\n", on, ret);
    close(fd);
    return ret;
}

static int uart_open(int baud, int flow, int rts)
{
    struct termios t;
    int fd = open(BT_UART_DEVICE, O_RDWR | O_NOCTTY);

    if (fd < 0) {
        printf("  %s open failed: %s\n", BT_UART_DEVICE, strerror(errno));
        return -1;
    }
    if (tcgetattr(fd, &t) < 0) {
        printf("  tcgetattr failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    cfmakeraw(&t);
    t.c_cflag = CLOCAL | CREAD | CS8;
    if (flow)
        t.c_cflag |= CRTSCTS;
    t.c_iflag = 0;
    t.c_oflag = 0;
    t.c_lflag = 0;
    /* Numeric speeds: the aarch64 termios carries c_ispeed/c_ospeed directly,
     * which is what msm_serial_hs uses, and covers the 1 M..4 M range the
     * controller can be driven at. */
    t.c_ispeed = baud;
    t.c_ospeed = baud;
    if (tcsetattr(fd, TCSANOW, &t) < 0)
        printf("  tcsetattr(%d) failed: %s\n", baud, strerror(errno));
    if (rts) {
        /* BT_WAKE is the AP's RTS: hold the chip out of sleep. */
        int m = TIOCM_RTS | TIOCM_DTR;
        if (ioctl(fd, TIOCMSET, &m) < 0)
            printf("  TIOCMSET(RTS|DTR) failed: %s\n", strerror(errno));
    }
    tcflush(fd, TCIOFLUSH);
    return fd;
}

static void modem_lines(int fd, const char *when)
{
    int m = 0;

    if (ioctl(fd, TIOCMGET, &m) < 0) {
        printf("    %s TIOCMGET failed: %s\n", when, strerror(errno));
        return;
    }
    printf("    %s modem lines 0x%03x CTS=%d DSR=%d RI=%d CD=%d\n", when, m,
           !!(m & TIOCM_CTS), !!(m & TIOCM_DSR), !!(m & TIOCM_RNG),
           !!(m & TIOCM_CD));
}

/* Write one HCI command and wait for any bytes back. */
static int probe_cmd(int fd, const uint8_t *cmd, int len, uint8_t *rsp, int timeout_ms)
{
    ssize_t n;
    struct pollfd pfd;
    long tx0, tx1, rx0, rx1;

    uart_counters(&tx0, &rx0);
    n = write(fd, cmd, len);
    if (n != len) {
        printf("    write failed after %zd/%d bytes: %s\n", n, len, strerror(errno));
        return -1;
    }
    pfd.fd = fd;
    pfd.events = POLLIN;
    if (poll(&pfd, 1, timeout_ms) <= 0) {
        uart_counters(&tx1, &rx1);
        printf("    no answer in %d ms (tx +%ld, rx +%ld)\n",
               timeout_ms, tx1 - tx0, rx1 - rx0);
        return 0;
    }
    n = read(fd, rsp, 256);
    uart_counters(&tx1, &rx1);
    printf("    rx %zd bytes (tx +%ld, rx +%ld):", n, tx1 - tx0, rx1 - rx0);
    for (ssize_t i = 0; i < n && i < 24; i++)
        printf(" %02x", rsp[i]);
    printf("\n");
    return n > 0 ? 1 : 0;
}

static void run_probe(int fd)
{
    uint8_t rsp[256];
    int answer;

    /* Wake the HS port the same way the HAL does (TIOCPMGET). */
    ioctl(fd, TIOCPMGET, 0);
    /* msm_hs_startup() is what muxes GPIO 45-48 to blsp_uart3_a; if the port
     * is still in its "sleep" state the bytes go nowhere at all, so read the
     * pin function while the port is held open. */
    dump_matching(PINMUX_PINS, "pin 4");
    dump_line(HS_UART_PM);
    modem_lines(fd, "pre");
    answer = probe_cmd(fd, edl_ver_req, sizeof(edl_ver_req), rsp, 1500);
    if (answer <= 0)
        answer = probe_cmd(fd, edl_build_req, sizeof(edl_build_req), rsp, 1000);
    if (answer <= 0)
        probe_cmd(fd, hci_reset, sizeof(hci_reset), rsp, 1000);
    modem_lines(fd, "post");
}

static int do_single(int argc, char **argv)
{
    int baud = argc > 2 ? atoi(argv[2]) : 115200;
    int flow = argc > 3 ? atoi(argv[3]) : 1;
    int rts  = argc > 4 ? atoi(argv[4]) : 1;
    int settle = argc > 5 ? atoi(argv[5]) : 300;
    int cycle = argc > 6 ? atoi(argv[6]) : 0;
    int fd;

    printf("== single: baud %d flow %d rts %d settle %d cycle %d\n",
           baud, flow, rts, settle, cycle);
    if (cycle) {
        bt_power(0);
        msleep(200);
        bt_power(1);
        msleep(settle);
    }
    fd = uart_open(baud, flow, rts);
    if (fd < 0)
        return 1;
    run_probe(fd);
    close(fd);
    return 0;
}

static int do_stats(void)
{
    dump_line(HS_STAT);
    dump_line("/sys/class/tty/ttyHS0/device/of_node/name");
    dump_line(HS_UART_PM);
    dump_matching(PINMUX_PINS, "pin 4");
    return 0;
}

static int do_matrix(int full)
{
    static const int bauds_quick[] = {115200, 3000000};
    static const int bauds_full[] = {115200, 921600, 1500000, 3000000};
    static const int settles[] = {100, 600};
    const int *bauds = full ? bauds_full : bauds_quick;
    int nbaud = full ? 4 : 2;
    int flow_on, rts_on, bi, si;
    long tx0, rx0, tx1, rx1;

    uart_counters(&tx0, &rx0);
    printf("HS uart before: tx=%ld rx=%ld\n", tx0, rx0);

    for (flow_on = 0; flow_on <= 1; flow_on++) {
        for (rts_on = 0; rts_on <= 1; rts_on++) {
            for (bi = 0; bi < nbaud; bi++) {
                for (si = 0; si < 2; si++) {
                    int fd;

                    printf("\n== baud %-8d flow %-3d rts %-3d settle %-3d ms\n",
                           bauds[bi], flow_on, rts_on, settles[si]);
                    bt_power(0);
                    msleep(200);
                    bt_power(1);
                    msleep(settles[si]);

                    fd = uart_open(bauds[bi], flow_on, rts_on);
                    if (fd < 0)
                        continue;
                    run_probe(fd);
                    close(fd);
                }
            }
        }
    }

    bt_power(0);
    uart_counters(&tx1, &rx1);
    printf("\nHS uart after: tx=%ld rx=%ld\n", tx1, rx1);
    return 0;
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "stats";

    if (!strcmp(mode, "on"))
        return bt_power(1) < 0 ? 1 : 0;
    if (!strcmp(mode, "off"))
        return bt_power(0) < 0 ? 1 : 0;
    if (!strcmp(mode, "single"))
        return do_single(argc, argv);
    if (!strcmp(mode, "quick"))
        return do_matrix(0);
    if (!strcmp(mode, "full"))
        return do_matrix(1);
    return do_stats();
}
