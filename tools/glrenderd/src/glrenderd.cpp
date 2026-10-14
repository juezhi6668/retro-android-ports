// ported from pdx203-jellybean by @chardidathing, https://github.com/chardidathing/pdx203-jellybean
// glrenderd, the emulator's gl renderer turned into a daemon that draws on adreno.
// kitkat's emulation egl/gles libs stream gl calls over the "glrenderd" abstract socket,
// this replays them on the vendor driver inside the lineage chroot.
//
// usage: glrenderd <width> <height> [-fb]
//   -fb  copy posted frames onto /dev/graphics/fb0 and flip, otherwise just count them
#include <fcntl.h>
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
        // a plain pan, FBIOPUT_VSCREENINFO can take drm's set_par path and redo the whole mode
        p->var.yoffset = p->back * p->var.yres;
        p->var.activate = FB_ACTIVATE_VBL;
        if (ioctl(p->fd, FBIOPAN_DISPLAY, &p->var) < 0)
            perror("glrenderd: flip");
        p->back ^= 1;
    }
    long long flip = GetCurrentTimeMS() - t1;
    s_flipMs += flip;
    if (flip > s_flipMax)
        s_flipMax = flip;

    if (++s_frames % 60 == 0) {
        long long now = GetCurrentTimeMS();
        fprintf(stderr, "glrenderd: %.1f fps shown, per frame readback %.1fms copy %.1fms flip %.1fms (max %lldms)\n",
                60000.0 / (now - s_lastReport), s_readbackMs / 60.0, s_copyMs / 60.0, s_flipMs / 60.0, s_flipMax);
        s_lastReport = now;
        s_readbackMs = s_copyMs = s_flipMs = s_flipMax = 0;
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

static void on_post(void* ctx, int width, int height, int, int, int, unsigned char* pixels)
{
    Presenter* pr = (Presenter*)ctx;
    if (!pr) {
        if (++s_frames % 60 == 1)
            fprintf(stderr, "glrenderd: frame %lu %dx%d\n", s_frames, width, height);
        return;
    }
    pthread_mutex_lock(&pr->lock);
    // any slot that isn't on screen and isn't the newest pending frame is free
    int slot = 0;
    while (slot == pr->showing || slot == pr->ready)
        slot++;
    pthread_mutex_unlock(&pr->lock);
    memcpy(pr->slots[slot], pixels, width * height * 4);
    pthread_mutex_lock(&pr->lock);
    pr->ready = slot;
    pthread_cond_signal(&pr->wake);
    pthread_mutex_unlock(&pr->lock);
}

static bool start_presenter(Presenter* pr, Panel* panel, int width, int height)
{
    pr->panel = panel;
    pr->width = width;
    pr->height = height;
    pr->ready = -1;
    pr->showing = -1;
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
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = on_crash;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);

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

    if (!FrameBuffer::initialize(width, height)) {
        fprintf(stderr, "glrenderd: FrameBuffer::initialize failed\n");
        return 1;
    }

    static Panel panel;
    if (to_fb && (!open_panel(&panel) || !start_presenter(&s_pres, &panel, width, height)))
        return 1;
    FrameBuffer::getFB()->setPostCallback(on_post, to_fb ? &s_pres : nullptr);

    char addr[256];
    RenderServer* server = RenderServer::create(addr, sizeof(addr));
    if (!server) {
        fprintf(stderr, "glrenderd: can't listen\n");
        return 1;
    }
    fprintf(stderr, "glrenderd: %dx%d listening on %s\n", width, height, addr);
    return server->Main();
}
