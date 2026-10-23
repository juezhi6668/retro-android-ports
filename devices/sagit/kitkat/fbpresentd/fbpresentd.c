/* fbpresentd: shows SurfaceFlinger's buffers on the main display through an MDSS atomic commit
 * (MSMFB_ATOMIC_COMMIT, DMA0 pipe), without any copy. 64-bit process: the kernel's 32-bit
 * compat path for this ioctl crashes the system. Sagit: the kernel is the dumped 4.4.302
 * (yoshino), the header is its include/uapi/linux/msm_mdp_ext.h. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <linux/fb.h>
#define __user
#include "../gralloc/mdss/msm_mdp_ext.h"
#include "fbpresent.h"

/* init gives services /dev/null as stderr and this port has no /dev/log, so log
 * through /dev/kmsg: that is the only way a brought-up-without-a-terminal run shows
 * what the presenter did (dmesg is the port's log; the fallback keeps a hand run
 * readable). */
static int s_klog = -1;

static void klog(const char *fmt, ...)
{
    char msg[224], buf[256];
    va_list ap;
    int n;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    if (s_klog < 0)
        s_klog = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    if (s_klog >= 0) {
        n = snprintf(buf, sizeof(buf), "<4>fbpresentd: %s\n", msg);
        if (n > 0 && n < (int)sizeof(buf))
            write(s_klog, buf, n);
    } else {
        fprintf(stderr, "fbpresentd: %s\n", msg);
    }
}
#define LOG(...) klog(__VA_ARGS__)
#define MAXBUF 8

static struct { uint64_t id; int fd; } s_bufs[MAXBUF];
static int s_fb, s_last_release = -1;
static unsigned long long s_commits;
static struct timespec s_last_ts;
static unsigned long long s_active_frames;
static long long s_active_ns;   /* summed gaps < 100 ms: SF posts nothing while idle */

static void reset_cache(void)
{
    for (int i = 0; i < MAXBUF; i++) {
        if (s_bufs[i].fd > 0)
            close(s_bufs[i].fd);
        s_bufs[i].fd = -1;
        s_bufs[i].id = 0;
    }
    if (s_last_release >= 0)
        close(s_last_release);
    s_last_release = -1;
}

static int buf_fd(uint64_t id, int newfd)
{
    int slot = -1;
    for (int i = 0; i < MAXBUF; i++)
        if (s_bufs[i].id == id && s_bufs[i].fd >= 0)
            slot = i;
    if (newfd >= 0) {
        if (slot < 0)
            for (int i = 0; i < MAXBUF && slot < 0; i++)
                if (s_bufs[i].fd < 0)
                    slot = i;
        if (slot < 0) {  /* no room left: recycle the oldest entry */
            slot = 0;
        }
        if (s_bufs[slot].fd >= 0)
            close(s_bufs[slot].fd);
        s_bufs[slot].id = id;
        s_bufs[slot].fd = newfd;
    }
    return slot >= 0 ? s_bufs[slot].fd : -1;
}

static int commit(const struct fbp_req *r, int fd)
{
    struct mdp_input_layer l;
    memset(&l, 0, sizeof(l));
    l.pipe_ndx = 64;  /* DMA0, attached to the main display */
    l.alpha = 0xff;
    l.blend_op = BLEND_OP_OPAQUE;
    l.src_rect.w = l.dst_rect.w = r->xres;
    l.src_rect.h = l.dst_rect.h = r->yres;
    l.buffer.width = r->width;
    l.buffer.height = r->height;
    l.buffer.format = r->ubwc ? MDP_RGBA_8888_UBWC : MDP_RGBA_8888;
    l.buffer.planes[0].fd = fd;
    l.buffer.planes[0].offset = r->offset;
    l.buffer.planes[0].stride = r->width * 4;
    l.buffer.plane_count = 1;
    l.buffer.fence = -1;
    struct mdp_layer_commit c;
    memset(&c, 0, sizeof(c));
    c.version = MDP_COMMIT_VERSION_1_0;
    c.commit_v1.input_layers = &l;
    c.commit_v1.input_layer_cnt = 1;
    c.commit_v1.release_fence = c.commit_v1.retire_fence = -1;
    if (ioctl(s_fb, MSMFB_ATOMIC_COMMIT, &c) < 0) {
        int e = errno;
        LOG("commit: %s (layer %d)", strerror(e), l.error_code);
        return -e;
    }
    if (c.commit_v1.retire_fence >= 0)
        close(c.commit_v1.retire_fence);
    /* the buffer of the previous commit must not be rewritten before it is released */
    int scanned = -1;   /* 0/1: the previous frame's release fence, >0 = scanned out */
    if (s_last_release >= 0) {
        struct pollfd p = { s_last_release, POLLIN, 0 };
        scanned = poll(&p, 1, 100) > 0;
        close(s_last_release);
    }
    s_last_release = c.commit_v1.release_fence;
    /* Frame rate: SurfaceFlinger posts nothing while the screen is static, so only
     * gaps under 100 ms count as "active" time — the average is otherwise dominated
     * by idle and reads as a false slowdown (measured 60.0 fps with
     * `dumpsys SurfaceFlinger --latency` while this line said 2.3 fps). */
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (s_commits > 0) {
        long long dt = (long long)(now.tv_sec - s_last_ts.tv_sec) * 1000000000LL
                     + (now.tv_nsec - s_last_ts.tv_nsec);
        if (dt > 0 && dt < 100000000LL) {
            s_active_ns += dt;
            s_active_frames++;
        }
    }
    s_last_ts = now;
    s_commits++;
    /* A release fence signals only once DMA0 has read the buffer for scanout, so its
     * state separates "the panel is driven" from "the MDP merely accepts commits".
     * First three frames, then one line per 600 with the active-time rate. */
    if (s_commits <= 3 || s_commits % 600 == 0) {
        char rate[24] = "";
        if (s_commits % 600 == 0) {
            if (s_active_ns > 0)
                snprintf(rate, sizeof(rate), ", %.1f fps while active",
                         s_active_frames * 1e9 / (double)s_active_ns);
            s_active_frames = 0;
            s_active_ns = 0;
        }
        if (scanned < 0)
            LOG("commit #%llu %ux%u (first frame)%s", s_commits, r->xres, r->yres, rate);
        else
            LOG("commit #%llu %ux%u: previous frame %s%s", s_commits, r->xres, r->yres,
                scanned ? "scanned out" : "NOT scanned out in 100 ms", rate);
    }
    return 0;
}

/* client gone: empty commit to release DMA0. Without it, a SurfaceFlinger restarted on the copy
 * path (persist.gralloc.fb_commit=0) mixes FBIOPUT_VSCREENINFO flips with an atomic layer still
 * attached, and the kernel panics. */
static void release_display(void)
{
    struct mdp_layer_commit c;
    memset(&c, 0, sizeof(c));
    c.version = MDP_COMMIT_VERSION_1_0;
    c.commit_v1.release_fence = c.commit_v1.retire_fence = -1;
    if (ioctl(s_fb, MSMFB_ATOMIC_COMMIT, &c) < 0) {
        LOG("empty commit: %s", strerror(errno));
        return;
    }
    if (c.commit_v1.retire_fence >= 0)
        close(c.commit_v1.retire_fence);
    if (c.commit_v1.release_fence >= 0)
        close(c.commit_v1.release_fence);
}

static void serve(int cl)
{
    for (;;) {
        struct fbp_req r;
        char ctl[CMSG_SPACE(sizeof(int))];
        struct iovec iov = { &r, sizeof(r) };
        struct msghdr m;
        memset(&m, 0, sizeof(m));
        m.msg_iov = &iov;
        m.msg_iovlen = 1;
        m.msg_control = ctl;
        m.msg_controllen = sizeof(ctl);
        ssize_t n = recvmsg(cl, &m, 0);
        if (n <= 0)
            return;
        int newfd = -1;
        struct cmsghdr *cm = CMSG_FIRSTHDR(&m);
        if (cm && cm->cmsg_level == SOL_SOCKET && cm->cmsg_type == SCM_RIGHTS)
            memcpy(&newfd, CMSG_DATA(cm), sizeof(int));
        int32_t ret;
        if (n != sizeof(r)) {
            ret = -EINVAL;
        } else {
            int fd = buf_fd(r.id, newfd);
            ret = fd < 0 ? -ENOENT : commit(&r, fd);
        }
        send(cl, &ret, sizeof(ret), MSG_NOSIGNAL);
    }
}

int main(void)
{
    setvbuf(stderr, NULL, _IOLBF, 0);
    s_fb = open("/dev/graphics/fb0", O_RDWR | O_CLOEXEC);
    if (s_fb < 0) {
        LOG("fb0: %s", strerror(errno));
        return 1;
    }
    reset_cache();
    int srv = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    struct sockaddr_un a;
    memset(&a, 0, sizeof(a));
    a.sun_family = AF_UNIX;
    strcpy(a.sun_path + 1, FBP_SOCKET);
    socklen_t len = offsetof(struct sockaddr_un, sun_path) + 1 + strlen(FBP_SOCKET);
    if (bind(srv, (struct sockaddr *)&a, len) || listen(srv, 2)) {
        LOG("socket: %s", strerror(errno));
        return 1;
    }
    LOG("ready");
    for (;;) {
        int cl = accept4(srv, NULL, NULL, SOCK_CLOEXEC);
        if (cl < 0)
            continue;
        LOG("client connected");
        s_commits = 0;   /* the per-client rate line counts this client's frames only */
        s_active_frames = 0;
        s_active_ns = 0;
        serve(cl);
        close(cl);
        release_display();
        reset_cache();
        LOG("client disconnected");
    }
}
