#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <linux/msm_ipc.h>

/* rfssrv — TFTP/RFS server on IPC router service 0x1000:1..12.
 * Unlike the A15 tftp_server (silent on the modem's RFS write probe), this
 * implements full file semantics: WRQs are written under RFS_ROOT, RRQs are
 * served from there, with the wlan firmware files served from the image dir.
 * The mcfg.tmp round-trip (modem writes then reads back its config) needs
 * real storage — discarding writes stalled the modem's config engine. */

#define NINST 12
#define RFS_ROOT "/system/vendor/rfs/msm/mpss"
#define FW_DIR   "/system/vendor/firmware/image"
#define MDM_DIR  "/data/local/tmp/mdm2/image"
static int fds[NINST];

static void hex(const char* tag, const void* p, int n) {
    printf("%s", tag);
    for (int i = 0; i < n && i < 90; i++) printf("%c", ((char*)p)[i] >= 32 && ((char*)p)[i] < 127 ? ((char*)p)[i] : '.');
    printf("\n");
}

/* map a client path like "/readwrite/mcfg.tmp" or "../readwrite/x" or
 * "/readonly/firmware/image/wlanmdsp.mbn" into a host path, or NULL */
static char* map_path(const char* fn) {
    static char out[512];
    const char* p = fn;
    while (!strncmp(p, "../", 3)) p += 3;
    while (*p == '/') p++;
    if (!strncmp(p, "readonly/firmware/image/", 25)) {
        snprintf(out, sizeof out, "%s/%s", FW_DIR, p + 25);
        return out;
    }
    if (!strncmp(p, "readonly/firmware/", 18)) {
        snprintf(out, sizeof out, "/system/vendor/firmware/%s", p + 18);
        return out;
    }
    snprintf(out, sizeof out, "%s/%s", RFS_ROOT, p);
    return out;
}

static void send_error(int fd, struct sockaddr_msm_ipc* to, int code, const char* msg) {
    char pkt[128];
    int n = snprintf(pkt, sizeof pkt, "%c%c%c%c%s%c", 0, 5, code >> 8, code & 0xff, msg, 0);
    sendto(fd, pkt, n, 0, (struct sockaddr*)to, sizeof *to);
}

struct xfer { int fd; int blksize; int wsize; unsigned next; int winflight; int eof; };

static int open_fw_fallback(const char* host) {
    int f = open(host, O_RDONLY);
    if (f >= 0) return f;
    const char* img = strstr(host, "/image/");
    if (img) {
        static char alt[512];
        snprintf(alt, sizeof alt, "%s/%s", MDM_DIR, img + 7);
        f = open(alt, O_RDONLY);
        if (f >= 0) printf("  (served from modem FAT: %s)\n", alt);
    }
    return f;
}

static void serve_rrq(int fd, struct sockaddr_msm_ipc* from, const char* fn, const unsigned char* buf, ssize_t n, int is_wrq) {
    char* host = map_path(fn);
    int file = is_wrq ? -2 : (strncmp(host, FW_DIR, strlen(FW_DIR)) ? open(host, O_RDONLY) : open_fw_fallback(host));
    if (!is_wrq && file < 0) {
        printf("  -> ERROR cannot open %s (%s)\n", host, strerror(errno));
        send_error(fd, from, 1, "File not found");
        return;
    }
    if (is_wrq) {
        file = open(host, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (file < 0) {
            printf("  -> ERROR cannot create %s (%s)\n", host, strerror(errno));
            send_error(fd, from, 2, "Access violation");
            return;
        }
    }
    /* parse options */
    int blksize = 512, wsize = 1;
    const char* p = fn + strlen(fn) + 1;
    p += strlen(p) + 1;
    char oack[256];
    int oacklen = 0, had_opt = 0;
    oack[oacklen++] = 0; oack[oacklen++] = 6;
    off_t fsize = is_wrq ? 0 : lseek(file, 0, SEEK_END);
    if (!is_wrq) lseek(file, 0, SEEK_SET);
    char valbuf[32];
    while (p < (const char*)buf + n) {
        const char* opt = p; p += strlen(p) + 1;
        if (p >= (const char*)buf + n) break;
        const char* val = p; p += strlen(p) + 1;
        if (!strcasecmp(opt, "blksize")) { blksize = atoi(val); snprintf(valbuf, sizeof valbuf, "%d", blksize); val = valbuf; }
        else if (!strcasecmp(opt, "wsize")) { wsize = atoi(val); snprintf(valbuf, sizeof valbuf, "%d", wsize); val = valbuf; }
        else if (!strcasecmp(opt, "tsize")) { snprintf(valbuf, sizeof valbuf, "%lld", (long long)fsize); val = valbuf; }
        int l = snprintf(oack + oacklen, sizeof oack - oacklen, "%s%c%s%c", opt, 0, val, 0);
        oacklen += l; had_opt = 1;
    }
    if (had_opt) sendto(fd, oack, oacklen, 0, (struct sockaddr*)from, sizeof *from);
    else { char a[4] = {0,4,0,0}; sendto(fd, a, 4, 0, (struct sockaddr*)from, sizeof *from); }
    if (wsize < 1) wsize = 1;
    if (blksize < 8 || blksize > 8100) blksize = 8100;
    printf("  -> %s %s blksize=%d wsize=%d\n", is_wrq ? "write" : "read", host, blksize, wsize);
    fflush(stdout);

    static char datablk[8192];
    static char rbuf[8192];
    unsigned next = 1;
    int winflight = 0, eof = 0, done = 0;
    struct sockaddr_msm_ipc from2;
    socklen_t flen2;
    while (!done) {
        if (is_wrq) {
            ssize_t r = recvfrom(fd, rbuf, sizeof rbuf, 0, (struct sockaddr*)&from2, &flen2);
            if (r < 4) break;
            int rop = ((unsigned char)rbuf[0] << 8) | (unsigned char)rbuf[1];
            if (rop != 3) break;
            unsigned blk = ((unsigned char)rbuf[2] << 8) | (unsigned char)rbuf[3];
            write(file, rbuf + 4, r - 4);
            char a[4] = {0, 4, blk >> 8, blk & 0xff};
            sendto(fd, a, 4, 0, (struct sockaddr*)&from2, sizeof from2);
            if (r - 4 < blksize) { printf("  WRQ done (%u blocks)\n", blk); break; }
        } else {
            while (!eof && winflight < wsize) {
                ssize_t r = read(file, datablk + 4, blksize);
                if (r <= 0) { eof = 1; break; }
                datablk[0] = 0; datablk[1] = 3;
                datablk[2] = next >> 8; datablk[3] = next & 0xff;
                sendto(fd, datablk, r + 4, 0, (struct sockaddr*)from, sizeof *from);
                next++; winflight++;
            }
            if (eof && winflight == 0) break;
            ssize_t r = recvfrom(fd, rbuf, sizeof rbuf, 0, (struct sockaddr*)&from2, &flen2);
            if (r < 4) continue;
            int rop = ((unsigned char)rbuf[0] << 8) | (unsigned char)rbuf[1];
            if (rop == 4) {
                unsigned ack = ((unsigned char)rbuf[2] << 8) | (unsigned char)rbuf[3];
                winflight = (next - 1) - ack;
                if (eof && ack == next - 1) done = 1;
            } else if (rop == 5) {
                printf("  client ERROR, abort\n");
                done = 1;
            }
        }
    }
    close(file);
    if (!is_wrq) printf("  read done (%u blocks)\n", next - 1);
    fflush(stdout);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    for (int i = 0; i < NINST; i++) {
        fds[i] = socket(AF_MSM_IPC, SOCK_DGRAM, 0);
        if (fds[i] < 0) { printf("socket: %s\n", strerror(errno)); return 1; }
        struct sockaddr_msm_ipc sa;
        memset(&sa, 0, sizeof sa);
        sa.family = AF_MSM_IPC;
        sa.address.addrtype = MSM_IPC_ADDR_NAME;
        sa.address.addr.port_name.service = 0x1000;
        sa.address.addr.port_name.instance = i + 1;
        if (bind(fds[i], (struct sockaddr*)&sa, sizeof sa) < 0) {
            printf("bind %d: %s\n", i + 1, strerror(errno));
            return 1;
        }
    }
    printf("rfssrv2 on 0x1000:1..%d root=%s\n", NINST, RFS_ROOT);
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
        serve_rrq(fd, &from, fn, (unsigned char*)buf, n, op == 2);
    }
}
