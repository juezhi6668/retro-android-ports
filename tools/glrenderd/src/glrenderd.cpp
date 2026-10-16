// ported from pdx203-jellybean by @chardidathing, https://github.com/chardidathing/pdx203-jellybean
// glrenderd, the emulator's gl renderer turned into a daemon that draws on adreno.
// kitkat's emulation egl/gles libs stream gl calls over the "glrenderd" abstract socket,
// this replays them on the vendor driver inside the lineage chroot.
//
// usage: glrenderd <width> <height> [-fb]
//   -fb  copy posted frames onto /dev/graphics/fb0 and flip, otherwise just count them
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <pthread.h>
#include <linux/fb.h>
#include <dlfcn.h>
#include <signal.h>
#include <unwind.h>

#include "EGLDispatch.h"
#include "FrameBuffer.h"
#include "GL2Dispatch.h"
#include "GLDispatch.h"
#include "RenderServer.h"
#include "codec_defs.h"
#include "TimeUtils.h"

// lives in render_api.cpp in the emulator, which we don't build
int gRendererStreamMode = STREAM_MODE_UNIX;

struct Panel {
    int fd;
    uint8_t* base;
    uint32_t stride;
    uint32_t back;
    bool flips;
    bool swapRB;
    struct fb_var_screeninfo var;
};

static unsigned long s_frames;
extern long long s_readbackMs;
static long long s_copyMs, s_flipMs, s_lastReport, s_flipMax;
// frame-path diagnostics (TEST 34): who limits the presented rate?
static unsigned long s_posts, s_drops;
static long long s_lastPostMs, s_postGapSumMs, s_stageMs;

// net.core.rmem_max is 208 KB by default, so any SO_RCVBUF/SO_SNDBUF larger
// than that is silently clamped - and for an AF_UNIX stream the sender blocks
// as soon as the receiver's queue is full. Every full-frame gralloc push
// (8.3 MB) can then stall a guest mid-write against a busy renderer. Raise the
// caps before accepting anything; the buffers are demand-allocated, so this
// costs nothing until a frame is actually in flight.
#define GLR_SOCK_MAX "16777216"
static void raise_socket_limits(void)
{
    static const char* kPaths[] = {
        "/proc/sys/net/core/rmem_max",
        "/proc/sys/net/core/wmem_max",
    };
    for (unsigned i = 0; i < sizeof(kPaths)/sizeof(kPaths[0]); i++) {
        int fd = open(kPaths[i], O_WRONLY);
        if (fd < 0) {
            fprintf(stderr, "glrenderd: open %s: %s\n", kPaths[i], strerror(errno));
            continue;
        }
        if (write(fd, GLR_SOCK_MAX, strlen(GLR_SOCK_MAX)) < 0)
            fprintf(stderr, "glrenderd: write %s: %s\n", kPaths[i], strerror(errno));
        close(fd);
    }
    for (unsigned i = 0; i < sizeof(kPaths)/sizeof(kPaths[0]); i++) {
        char buf[32] = {0};
        int fd = open(kPaths[i], O_RDONLY);
        if (fd >= 0) {
            int n = read(fd, buf, sizeof(buf) - 1);
            close(fd);
            if (n > 0) {
                while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\0')) buf[--n] = 0;
                fprintf(stderr, "glrenderd: %s = %s\n", kPaths[i], buf);
            }
        }
    }
    fflush(stderr);
}

static bool open_panel(Panel* p)
{
    p->fd = open("/dev/graphics/fb0", O_RDWR);
    if (p->fd < 0) {
        perror("glrenderd: fb0");
        return false;
    }
    struct fb_fix_screeninfo fix;
    if (ioctl(p->fd, FBIOGET_FSCREENINFO, &fix) || ioctl(p->fd, FBIOGET_VSCREENINFO, &p->var)) {
        perror("glrenderd: fb0 info");
        return false;
    }
    // glrenderd owns the panel: surfaceflinger in glrenderd mode posts via rcFBPost over
    // the socket and never FBIOPANs itself, so this daemon is the single flipper. that only
    // holds if flippers agree on double-buffering. the fb HAL would double yres_virtual when
    // it opens fb0, but nobody opens the fb HAL in glrenderd mode, so do it here exactly like
    // the fb HAL would. without this we sample flips=0, memcpy without ever panning, and the
    // panel freezes on the xboot splash (maple TEST 28). do it immediately, not wait-then-do:
    // the wait only delays binding the socket the egl loader is polling for.
    if (p->var.yres_virtual < p->var.yres * 2) {
        p->var.yres_virtual = p->var.yres * 2;
        p->var.activate = FB_ACTIVATE_NOW;
        if (ioctl(p->fd, FBIOPUT_VSCREENINFO, &p->var))
            perror("glrenderd: double-buffer fb0");
        else if (ioctl(p->fd, FBIOGET_VSCREENINFO, &p->var))
            perror("glrenderd: fb0 re-read");
        else {
            fprintf(stderr, "glrenderd: doubled fb0 virtual to %ux%u\n",
                    p->var.xres_virtual, p->var.yres_virtual);
            // the mode change can resize smem, re-read fix before mmap below
            if (ioctl(p->fd, FBIOGET_FSCREENINFO, &fix))
                perror("glrenderd: fb0 fix re-read");
        }
    }
    p->stride = fix.line_length;
    // readback is r,g,b,a in memory, mdss can come up bgra instead
    p->swapRB = p->var.red.offset == 16;
    p->flips = p->var.yres_virtual >= p->var.yres * 2;
    p->back = p->flips ? 1 : 0;
    p->base = (uint8_t*)mmap(nullptr, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, p->fd, 0);
    if (p->base == MAP_FAILED) {
        perror("glrenderd: fb0 mmap");
        return false;
    }
    fprintf(stderr, "glrenderd: panel %ux%u stride %u flips %d swap %d\n",
            p->var.xres, p->var.yres, p->stride, p->flips, p->swapRB);
    return true;
}

// readback comes out top-down as r,g,b,a bytes, which fb0 takes as is unless it's bgra.
// the guest screen can be an integer fraction of the panel, pixels get repeated to fill it.
//
// the render thread only copies the readback into a staging slot, a presenter thread does the
// upscale and the flip (which blocks for a vblank or two). if it falls behind, stale frames get
// dropped and the newest one wins
struct Presenter {
    Panel* panel;
    pthread_mutex_t lock;
    pthread_cond_t wake;
    uint8_t* slots[3];
    int width, height;
    int ready;     // newest finished slot, -1 if nothing new
    int showing;   // slot the presenter is reading, -1 if idle
    int writing;   // slot the readback is filling, -1 if none (A6)
    uint32_t* row; // one upscaled row in cached memory, fb0 is never read back
    uint32_t* swapped; // one source row with red and blue swapped
};

static inline uint32_t swap_rb(uint32_t px)
{
    return (px & 0xff00ff00) | ((px & 0xff) << 16) | ((px >> 16) & 0xff);
}

static Presenter s_pres;

static void present(Presenter* pr, const uint8_t* pixels)
{
    Panel* p = pr->panel;
    int width = pr->width, height = pr->height;
    long long t0 = GetCurrentTimeMS();
    uint32_t scale = p->var.xres / width;
    if (scale < 1)
        scale = 1;
    uint32_t w = width * scale > p->var.xres ? p->var.xres / scale : width;
    uint32_t h = height * scale > p->var.yres ? p->var.yres / scale : height;
    uint8_t* dst = p->base + p->back * p->var.yres * p->stride;
    for (uint32_t y = 0; y < h; y++) {
        const uint32_t* src = (const uint32_t*)(pixels + y * width * 4);
        if (p->swapRB) {
            for (uint32_t x = 0; x < w; x++)
                pr->swapped[x] = swap_rb(src[x]);
            src = pr->swapped;
        }
        if (scale == 1) {
            memcpy(dst + y * p->stride, src, w * 4);
            continue;
        }
        for (uint32_t x = 0; x < w; x++)
            for (uint32_t i = 0; i < scale; i++)
                pr->row[x * scale + i] = src[x];
        for (uint32_t i = 0; i < scale; i++)
            memcpy(dst + (y * scale + i) * p->stride, pr->row, w * scale * 4);
    }
    long long t1 = GetCurrentTimeMS();
    s_copyMs += t1 - t0;
    if (p->flips) {
        // flip the same way the stock gralloc's fb_post does (FBIOPUT_VSCREENINFO with a new
        // yoffset). on maple's mdss_mdp_overlay the FBIOPAN_DISPLAY path logs "unable to
        // allocate base pipe" and returns success WITHOUT moving the offset, so the panel
        // stays frozen while fb0 holds correct frames. FBIOPUT goes through set_par, which
        // is what actually re-arms the overlay here. (the ported comment about set_par
        // "redoing the whole mode" was from pdx203's drm path, not this fbdev one.)
        p->var.yoffset = p->back * p->var.yres;
        p->var.activate = FB_ACTIVATE_VBL;
        if (ioctl(p->fd, FBIOPUT_VSCREENINFO, &p->var) < 0)
            perror("glrenderd: flip");
        p->back ^= 1;
    }
    long long flip = GetCurrentTimeMS() - t1;
    s_flipMs += flip;
    if (flip > s_flipMax)
        s_flipMax = flip;

    if (++s_frames % 60 == 0) {
        long long now = GetCurrentTimeMS();
        fprintf(stderr, "glrenderd: %.1f fps shown, per frame readback %.1fms copy %.1fms flip %.1fms"
                        " (max %lldms) posts=%lu drops=%lu postgap %.1fms stage %.1fms\n",
                60000.0 / (now - s_lastReport), s_readbackMs / 60.0, s_copyMs / 60.0, s_flipMs / 60.0, s_flipMax,
                s_posts, s_drops,
                s_posts ? (double)s_postGapSumMs / s_posts : 0.0,
                s_posts ? (double)s_stageMs / s_posts : 0.0);
        s_lastReport = now;
        s_readbackMs = s_copyMs = s_flipMs = s_flipMax = 0;
        s_posts = s_drops = 0;
        s_postGapSumMs = s_stageMs = 0;
    }
}

static void* presenter_main(void* arg)
{
    Presenter* pr = (Presenter*)arg;
    pthread_mutex_lock(&pr->lock);
    for (;;) {
        while (pr->ready < 0)
            pthread_cond_wait(&pr->wake, &pr->lock);
        pr->showing = pr->ready;
        pr->ready = -1;
        pthread_mutex_unlock(&pr->lock);
        present(pr, pr->slots[pr->showing]);
        pthread_mutex_lock(&pr->lock);
        pr->showing = -1;
    }
    return nullptr;
}

// MAPLE (poplar A6): called on the posting thread with a GL context current,
// right before the readback. Choosing the destination here is what removes the
// full-frame copy that used to sit between the readback and the presenter.
static unsigned char* present_acquire(void* ctx, int* w, int* h)
{
    Presenter* pr = (Presenter*)ctx;
    if (!pr)
        return nullptr;
    pthread_mutex_lock(&pr->lock);
    // any slot that isn't on screen and isn't the newest pending frame is free
    int slot = 0;
    while (slot == pr->showing || slot == pr->ready)
        slot++;
    pr->writing = slot;
    *w = pr->width;
    *h = pr->height;
    pthread_mutex_unlock(&pr->lock);
    return pr->slots[slot];
}

static void on_post(void* ctx, int width, int height, int, int, int, unsigned char* pixels)
{
    Presenter* pr = (Presenter*)ctx;
    if (!pr) {
        if (++s_frames % 60 == 1)
            fprintf(stderr, "glrenderd: frame %lu %dx%d\n", s_frames, width, height);
        return;
    }
    long long t0 = GetCurrentTimeMS();
    pthread_mutex_lock(&pr->lock);
    if (pr->writing >= 0) {
        // MAPLE (poplar A6): the readback already landed in the slot that
        // present_acquire picked - nothing to copy, just publish it
        if (pr->ready >= 0)
            s_drops++;   // previous frame never made it to the panel (superseded)
        pr->ready = pr->writing;
        pr->writing = -1;
    } else {
        // fallback (no readback hook): copy the FrameBuffer's buffer to a slot
        int slot = 0;
        while (slot == pr->showing || slot == pr->ready)
            slot++;
        if (pr->ready >= 0)
            s_drops++;   // previous frame never made it to the panel (superseded)
        pthread_mutex_unlock(&pr->lock);
        memcpy(pr->slots[slot], pixels, width * height * 4);
        s_stageMs += GetCurrentTimeMS() - t0;
        pthread_mutex_lock(&pr->lock);
        pr->ready = slot;
    }
    pthread_cond_signal(&pr->wake);
    pthread_mutex_unlock(&pr->lock);
    long long now = GetCurrentTimeMS();
    if (s_lastPostMs)
        s_postGapSumMs += now - s_lastPostMs;
    s_lastPostMs = now;
    s_posts++;
}

static bool start_presenter(Presenter* pr, Panel* panel, int width, int height)
{
    pr->panel = panel;
    pr->width = width;
    pr->height = height;
    pr->ready = -1;
    pr->showing = -1;
    pr->writing = -1;
    pthread_mutex_init(&pr->lock, nullptr);
    pthread_cond_init(&pr->wake, nullptr);
    for (int i = 0; i < 3; i++)
        if (!(pr->slots[i] = (uint8_t*)malloc(width * height * 4)))
            return false;
    if (!(pr->row = (uint32_t*)malloc(panel->var.xres * 4)))
        return false;
    if (!(pr->swapped = (uint32_t*)malloc(width * 4)))
        return false;
    pthread_t t;
    return pthread_create(&t, nullptr, presenter_main, pr) == 0;
}

// no debuggerd in the chroot, so print our own backtrace on a crash
static _Unwind_Reason_Code print_frame(struct _Unwind_Context* ctx, void* arg)
{
    int* depth = (int*)arg;
    uintptr_t pc = _Unwind_GetIP(ctx);
    Dl_info info;
    if (pc && dladdr((void*)pc, &info) && info.dli_fname)
        fprintf(stderr, "  #%02d pc %08lx %s (%s+%lu)\n", *depth, (unsigned long)(pc - (uintptr_t)info.dli_fbase),
                info.dli_fname, info.dli_sname ? info.dli_sname : "?",
                info.dli_saddr ? (unsigned long)(pc - (uintptr_t)info.dli_saddr) : 0UL);
    else
        fprintf(stderr, "  #%02d pc %08lx\n", *depth, (unsigned long)pc);
    return ++*depth < 32 ? _URC_NO_REASON : _URC_END_OF_STACK;
}

static void on_crash(int sig, siginfo_t* si, void*)
{
    fprintf(stderr, "glrenderd: signal %d at %p\n", sig, si->si_addr);
    int depth = 0;
    _Unwind_Backtrace(print_frame, &depth);
    signal(sig, SIG_DFL);
    raise(sig);
}

int main(int argc, char** argv)
{
    // first line: lets a cold-boot kmsg log show exec time vs "listening" time, so the
    // surfaceflinger probe window (Loader.cpp) can be sized against the real startup latency
    fprintf(stderr, "glrenderd: starting (pid %d)\n", getpid());

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = on_crash;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);
    // a guest that dies mid-reply must never take the daemon down: the guest
    // side already sends with MSG_NOSIGNAL, this is the belt-and-braces for
    // every other write path
    signal(SIGPIPE, SIG_IGN);

    if (argc < 3) {
        fprintf(stderr, "usage: %s <width> <height> [-fb]\n", argv[0]);
        return 1;
    }
    int width = atoi(argv[1]);
    int height = atoi(argv[2]);
    bool to_fb = argc > 3 && !strcmp(argv[3], "-fb");

    // the 32-bit adreno closure, wire format pointers are 32-bit so this daemon is too
    setenv("ANDROID_EGL_LIB", "/vendor/lib/egl/libEGL_adreno.so", 0);
    setenv("ANDROID_GLESv1_LIB", "/vendor/lib/egl/libGLESv1_CM_adreno.so", 0);
    setenv("ANDROID_GLESv2_LIB", "/vendor/lib/egl/libGLESv2_adreno.so", 0);

    // render_api.cpp's initLibrary in the emulator, the dispatch tables are empty until this
    if (!init_egl_dispatch() || !init_gl_dispatch() || !init_gl2_dispatch()) {
        fprintf(stderr, "glrenderd: couldn't load the adreno egl/gles libs\n");
        return 1;
    }

    raise_socket_limits();

    if (!FrameBuffer::initialize(width, height)) {
        fprintf(stderr, "glrenderd: FrameBuffer::initialize failed\n");
        return 1;
    }

    static Panel panel;
    if (to_fb && (!open_panel(&panel) || !start_presenter(&s_pres, &panel, width, height)))
        return 1;
    FrameBuffer::getFB()->setPostCallback(on_post, to_fb ? &s_pres : nullptr);
    if (to_fb)
        FrameBuffer::getFB()->setPostReadback(present_acquire, &s_pres);

    char addr[256];
    RenderServer* server = RenderServer::create(addr, sizeof(addr));
    if (!server) {
        fprintf(stderr, "glrenderd: can't listen\n");
        return 1;
    }
    fprintf(stderr, "glrenderd: %dx%d listening on %s\n", width, height, addr);
    return server->Main();
}
