/*
 * logshim - bringup-only /dev/log FIFO shim for maple/Donut.
 *
 * This kernel has no Android logger (no /dev/log*). Donut's liblog
 * (system/core/liblog/logd_write.c) opens /dev/log/main, /dev/log/radio
 * and /dev/log/events all at once on the first log call from a process,
 * and disables logging entirely in that process if ANY open fails - so
 * all three must exist and all three need a reader. This shim creates
 * three FIFOs, keeps non-blocking readers on them for its whole life
 * (a standing reader means liblog's blocking O_WRONLY opens never wait,
 * and never deadlock on radio/events while main is already open), and
 * mirrors each buffer to /cache/logcat-{main,radio,events}.txt where
 * TWRP can read them after a failed boot.
 *
 * liblog writes the payloads raw - for main, [priority][tag\0][msg\0]
 * concatenated, no logger_entry header (the kernel logger used to add
 * it) - so the output files are raw streams; parse-logcat.py untangles
 * the main one on the host.
 *
 * Modes:
 *   --init : create /dev/log + the FIFOs, exit 0 (run from `on init`).
 *   (none) : create the FIFOs (idempotent), then run the reader loop.
 *
 * Delete this service once the port is stable.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#define NFIFO 3

static const char *fifo_path[NFIFO] = {
    "/dev/log/main",
    "/dev/log/radio",
    "/dev/log/events",
};

static const char *out_path[NFIFO] = {
    "/data/logcat-main.txt",
    "/data/logcat-radio.txt",
    "/data/logcat-events.txt",
};

static void make_fifos(void)
{
    int i;
    mkdir("/dev/log", 0755);
    for (i = 0; i < NFIFO; i++) {
        if (access(fifo_path[i], F_OK) != 0)
            mknod(fifo_path[i], S_IFIFO | 0666, 0);
    }
}

static void wait_cache(void)
{
    int i;
    for (i = 0; i < 300; i++) {
        if (access("/cache", F_OK) == 0)
            return;
        usleep(100 * 1000);
    }
}

int main(int argc, char **argv)
{
    int rfd[NFIFO];
    int ofd[NFIFO];
    int i;
    char buf[8192];

    make_fifos();
    if (argc > 1 && strcmp(argv[1], "--init") == 0)
        return 0;

    /* Readers first, /cache after: writers must never wait on us. */
    for (i = 0; i < NFIFO; i++) {
        rfd[i] = open(fifo_path[i], O_RDONLY | O_NONBLOCK);
        ofd[i] = -1;
    }

    wait_cache();

    for (;;) {
        int got = 0;

        for (i = 0; i < NFIFO; i++) {
            ssize_t n;

            if (rfd[i] < 0) {
                rfd[i] = open(fifo_path[i], O_RDONLY | O_NONBLOCK);
                if (rfd[i] < 0)
                    continue;
            }

            n = read(rfd[i], buf, sizeof(buf));
            if (n <= 0)
                continue;       /* EAGAIN / no writer yet */

            got = 1;
            if (ofd[i] < 0)
                ofd[i] = open(out_path[i],
                              O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (ofd[i] >= 0) {
                if (write(ofd[i], buf, n) != n) {
                    close(ofd[i]);
                    ofd[i] = -1;
                } else {
                    fsync(ofd[i]);
                }
            }
        }

        if (!got)
            usleep(20 * 1000);
    }
}
