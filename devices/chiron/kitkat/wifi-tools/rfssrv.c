#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <linux/msm_ipc.h>

#define NINST 12
static int fds[NINST];

/* Minimal TFTP server on IPC router service 0x1000:1 (rfs) that actually
 * answers, unlike the A15 tftp_server which sits silent on the modem's
 * RFS health-check (../readwrite/server_check.txt) and stalls the modem's
 * RFS client before it ever reaches the wlan eloader stage.
 *
 * - RRQ for *wlanmdsp* -> serve /system/vendor/firmware/image/wlanmdsp.mbn
 * - RRQ for *bdwlan*   -> serve /system/vendor/firmware/image/bdwlan.bin
 * - anything else (incl. WRQ) -> TFTP ERROR so the modem knows the server
 *   is alive and can move on.
 * TFTP options (RFC 2348): OACK with blksize/timeoutms/wsize echoed. */

static void hex(const char* tag, const void* p, int n) {
    printf("%s", tag);
    for (int i = 0; i < n && i < 80; i++) printf("%c", ((char*)p)[i] >= 32 && ((char*)p)[i] < 127 ? ((char*)p)[i] : '.');
    printf("\n");
}

static int pick_file(const char* fn) {
    if (strstr(fn, "wlanmdsp")) return open("/system/vendor/firmware/image/wlanmdsp.mbn", O_RDONLY);
    if (strstr(fn, "bdwlan"))   return open("/system/vendor/firmware/image/bdwlan.bin", O_RDONLY);
    return -1;
}

static void send_error(int fd, struct sockaddr_msm_ipc* to, int code, const char* msg) {
    char pkt[128];
    int n = snprintf(pkt, sizeof pkt, "%c%c%c%c%s%c", 0, 5, code >> 8, code & 0xff, msg, 0);
    sendto(fd, pkt, n, 0, (struct sockaddr*)to, sizeof *to);
}

int main(int argc, char** argv) {
    uint32_t svc = argc > 1 ? strtoul(argv[1], 0, 16) : 0x1000;
    for (int i = 0; i < NINST; i++) {
        fds[i] = socket(AF_MSM_IPC, SOCK_DGRAM, 0);
        if (fds[i] < 0) { printf("socket: %s\n", strerror(errno)); return 1; }
        struct sockaddr_msm_ipc sa;
        memset(&sa, 0, sizeof sa);
        sa.family = AF_MSM_IPC;
        sa.address.addrtype = MSM_IPC_ADDR_NAME;
        sa.address.addr.port_name.service = svc;
        sa.address.addr.port_name.instance = i + 1;
        if (bind(fds[i], (struct sockaddr*)&sa, sizeof sa) < 0) {
            printf("bind %x:%d: %s\n", svc, i + 1, strerror(errno));
            return 1;
        }
    }
    printf("rfssrv on %x:1..%d\n", svc, NINST);
    static char buf[8192];
    for (;;) {
        struct pollfd pf[NINST];
        for (int i = 0; i < NINST; i++) { pf[i].fd = fds[i]; pf[i].events = POLLIN; }
        int r = poll(pf, NINST, -1);
        if (r <= 0) continue;
        int fd = -1;
        for (int i = 0; i < NINST; i++) if (pf[i].revents & POLLIN) { fd = fds[i]; break; }
        if (fd < 0) continue;
        struct sockaddr_msm_ipc from;
        socklen_t flen = sizeof from;
        ssize_t n = recvfrom(fd, buf, sizeof buf, 0, (struct sockaddr*)&from, &flen);
        if (n < 6) continue;
        int op = ((unsigned char)buf[0] << 8) | (unsigned char)buf[1];
        if (op != 1 && op != 2) continue;
        const char* fn = buf + 2;
        hex(op == 1 ? "RRQ " : "WRQ ", fn, n - 2);
        fflush(stdout);
        if (op == 2) {
            /* The modem's RFS health probe WRITES readwrite/server_check.txt;
             * an ERROR reply makes it retry forever and the RFS client never
             * advances to the eloader stage. Accept the write: OACK, then
             * ACK every DATA block, discarding content. */
            int blksize = 512;
            const char* p = fn + strlen(fn) + 1;
            p += strlen(p) + 1;
            char oack[256];
            int oacklen = 0, had_opt = 0;
            oack[oacklen++] = 0; oack[oacklen++] = 6;
            while (p < buf + n) {
                const char* opt = p; p += strlen(p) + 1;
                if (p >= buf + n) break;
                const char* val = p; p += strlen(p) + 1;
                if (!strcasecmp(opt, "blksize")) blksize = atoi(val);
                int l = snprintf(oack + oacklen, sizeof oack - oacklen, "%s%c%s%c", opt, 0, val, 0);
                oacklen += l; had_opt = 1;
            }
            if (blksize < 8 || blksize > 6944) blksize = 6944;
            if (had_opt) sendto(fd, oack, oacklen, 0, (struct sockaddr*)&from, sizeof from);
            else { char a[4] = {0,4,0,0}; sendto(fd, a, 4, 0, (struct sockaddr*)&from, sizeof from); }
            for (;;) {
                ssize_t r = recvfrom(fd, buf, sizeof buf, 0, (struct sockaddr*)&from, &flen);
                if (r < 4) break;
                int rop = ((unsigned char)buf[0] << 8) | (unsigned char)buf[1];
                if (rop != 3) break; /* not DATA */
                unsigned blk = ((unsigned char)buf[2] << 8) | (unsigned char)buf[3];
                char a[4] = {0, 4, blk >> 8, blk & 0xff};
                sendto(fd, a, 4, 0, (struct sockaddr*)&from, sizeof from);
                if (r - 4 < blksize) { printf("  WRQ done (%u blocks)\n", blk); break; }
            }
            fflush(stdout);
            continue;
        }
        int file = pick_file(fn);
        if (file < 0) {
            printf("  -> ERROR file-not-found\n");
            send_error(fd, &from, 1, "File not found");
            continue;
        }
        off_t fsize = lseek(file, 0, SEEK_END);
        lseek(file, 0, SEEK_SET);
        /* parse options: filename\0 mode\0 then opt\0 val\0 ... */
        int blksize = 512, wsize = 1;
        const char* p = fn + strlen(fn) + 1;      /* mode */
        p += strlen(p) + 1;
        char oack[256];
        int oacklen = 0;
        oack[oacklen++] = 0; oack[oacklen++] = 6;
        while (p < buf + n) {
            const char* opt = p; p += strlen(p) + 1;
            if (p >= buf + n) break;
            const char* val = p; p += strlen(p) + 1;
            char valbuf[32];
            if (!strcasecmp(opt, "blksize")) { blksize = atoi(val); snprintf(valbuf, sizeof valbuf, "%d", blksize); val = valbuf; }
            else if (!strcasecmp(opt, "wsize")) { wsize = atoi(val); snprintf(valbuf, sizeof valbuf, "%d", wsize); val = valbuf; }
            else if (!strcasecmp(opt, "tsize")) { snprintf(valbuf, sizeof valbuf, "%lld", (long long)fsize); val = valbuf; }
            int l = snprintf(oack + oacklen, sizeof oack - oacklen, "%s%c%s%c", opt, 0, val, 0);
            oacklen += l;
        }
        if (oacklen > 2) sendto(fd, oack, oacklen, 0, (struct sockaddr*)&from, sizeof from);
        if (wsize < 1) wsize = 1;
        if (blksize < 8 || blksize > 8100) blksize = 8100;
        printf("  -> serving blksize=%d wsize=%d\n", blksize, wsize);
        static char datablk[8192];
        unsigned next = 1;
        int winflight = 0, eof = 0, done = 0;
        while (!done) {
            while (!eof && winflight < wsize) {
                ssize_t r = read(file, datablk + 4, blksize);
                if (r <= 0) { eof = 1; break; }
                datablk[0] = 0; datablk[1] = 3;
                datablk[2] = next >> 8; datablk[3] = next & 0xff;
                sendto(fd, datablk, r + 4, 0, (struct sockaddr*)&from, sizeof from);
                next++; winflight++;
            }
            if (eof && winflight == 0) break;
            ssize_t r = recvfrom(fd, buf, sizeof buf, 0, (struct sockaddr*)&from, &flen);
            if (r < 4) continue;
            int rop = ((unsigned char)buf[0] << 8) | (unsigned char)buf[1];
            if (rop == 4) {
                unsigned ack = ((unsigned char)buf[2] << 8) | (unsigned char)buf[3];
                winflight = (next - 1) - ack;
                if (eof && ack == next - 1) done = 1;
            } else if (rop == 5) {
                printf("  client ERROR, abort\n");
                done = 1;
            }
        }
        close(file);
        printf("  transfer done (%u blocks)\n", next - 1);
    }
}
