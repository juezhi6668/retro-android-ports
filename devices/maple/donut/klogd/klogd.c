/*
 * klogd - bringup-only kernel log capture for maple/Donut.
 *
 * This kernel has no Android logger (no /dev/log*), and when a boot never
 * reaches adb there is no way to see init's own klog output or the kernel
 * log at all (pstore is wiped by power cycles, no UART access). klogd reads
 * /dev/kmsg from the start of the ring buffer and mirrors it to
 * /cache/kmsg.txt (fsync per record), where TWRP can read it after a failed
 * boot. Rotates to kmsg.txt.1 at 4 MB. Static, runs from the ramdisk as
 * root; /cache is mounted (with the `sync` option) before services start.
 * Delete this service once the port is stable.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#define OUT      "/cache/kmsg.txt"
#define MAXSZ    (4 * 1024 * 1024)

static void copy_from(const char *path, const char *out)
{
    int kfd, ofd;
    ssize_t n;
    char buf[4096];
    unsigned long total = 0;
    int i;

    /* /cache is mounted by init before services start; wait a little in
     * case of ordering surprises. */
    for (i = 0; i < 300; i++) {
        if (access("/cache", F_OK) == 0)
            break;
        usleep(100000);
    }

    kfd = open(path, O_RDONLY | O_NONBLOCK);
    if (kfd < 0)
        return;

    ofd = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (ofd < 0) {
        close(kfd);
        return;
    }

    for (;;) {
        n = read(kfd, buf, sizeof(buf) - 1);
        if (n > 0) {
            if (total + (unsigned long)n > MAXSZ) {
                char rot[256];
                close(ofd);
                snprintf(rot, sizeof(rot), "%s.1", out);
                rename(out, rot);
                ofd = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (ofd < 0)
                    break;
                total = 0;
            }
            if (write(ofd, buf, n) == n) {
                fsync(ofd);
                total += n;
            }
        } else if (n < 0 && errno != EAGAIN && errno != EINTR) {
            break;
        } else {
            usleep(200000);
        }
    }
    close(kfd);
    close(ofd);
}

int main(void)
{
    copy_from("/dev/kmsg", OUT);
    return 0;
}
