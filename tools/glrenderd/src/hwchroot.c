// ported from pdx203-jellybean by @chardidathing, https://github.com/chardidathing/pdx203-jellybean
// static helper, kitkat's toolbox has no chroot and its mount can't bind.
// usage: hwchroot <root> <program> [args...]
// mounts live in a private namespace so nothing leaks back into kitkat.
//
// the chroot view is assembled on /dev's tmpfs, NOT inside <root>: <root> ships
// on read-only /system, and aosp's PRODUCT_COPY_FILES cannot create the empty
// dirs stage.py stages (dev, proc, sys, data/...) — every "find -type f" copy
// drops them, mkdir'ing them at runtime fails EROFS, the binds below get
// ENOENT, glrenderd restart-loops and surfaceflinger's 30s probe lapses to the
// software renderer (single-digit fps until SF/device restarts, maple
// 2026-10-17). tmpfs-backed mountpoints can't regress that way: <root>'s real
// entries are bind-mounted onto a writable skeleton built fresh each run.
//
// a restarted daemon strands a running surfaceflinger, too: the fresh daemon
// has no server-side state for SF's window objects, so no frame ever reaches
// fb0 again until SF reprobes the socket (it latches hw-vs-software once per
// process; verified on maple 2026-10-17: posts=0 while SF kept flipping). the
// first hwchroot run of a boot is init's own from post-fs-data and SF cannot
// have probed yet — class_start core forks SF milliseconds later, its egl
// init is seconds after that — so only a *second* run is a restart: bounce SF
// through init then. the marker lives on /dev's tmpfs, so it exists exactly
// when a previous run happened this boot, and reboots clear it.
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define LIB_PATH "/vendor/lib/egl:/vendor/lib:/system/lib:/vendor/lib64/egl:/vendor/lib64:/system/lib64"

// the writable skeleton: /dev is tmpfs in kitkat, everything else either
// isn't mounted yet or isn't writable (system is ro, proc/sys aren't dirs)
#define SKEL "/dev/.glr"
// assembled by hand, never bind-mounted from <root>: dev/proc/sys are live
// kernel mounts, data/ is scratch the adreno allocator writes
static const char* kSynthetic[] = { "dev", "proc", "sys", "data" };

static int make_dir(const char *path) {
    if (!mkdir(path, 0755) || errno == EEXIST) return 0;
    fprintf(stderr, "hwchroot: mkdir %s: %s\n", path, strerror(errno));
    return -1;
}

static int touch_file(const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT, 0755);
    if (fd < 0) {
        fprintf(stderr, "hwchroot: create %s: %s\n", path, strerror(errno));
        return -1;
    }
    close(fd);
    return 0;
}

// kitkat init only honours ctl.<verb> property writes; SF redoes its gl
// hw-vs-software probe while starting, which is the whole point
static void restart_surfaceflinger(void) {
    pid_t p = fork();
    if (p < 0) { perror("hwchroot: fork"); return; }
    if (!p) {
        execl("/system/bin/setprop", "setprop", "ctl.restart", "surfaceflinger", (char *)NULL);
        _exit(127);
    }
    int status = 0;
    if (waitpid(p, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status))
        fprintf(stderr, "hwchroot: setprop ctl.restart surfaceflinger failed\n");
}

static int bind_onto(const char *src, const char *dst, unsigned long flags) {
    if (mount(src, dst, NULL, flags, NULL)) {
        fprintf(stderr, "hwchroot: bind %s -> %s: %s\n", src, dst, strerror(errno));
        return -1;
    }
    return 0;
}

// mirror one entry of <root> under the skeleton, preserving its type
static int expose(const char *root, const char *name) {
    char src[512], dst[512];
    snprintf(src, sizeof(src), "%s/%s", root, name);
    snprintf(dst, sizeof(dst), "%s/%s", SKEL, name);
    struct stat st;
    if (stat(src, &st)) {
        fprintf(stderr, "hwchroot: stat %s: %s\n", src, strerror(errno));
        return -1;
    }
    if (S_ISDIR(st.st_mode))
        return make_dir(dst) || bind_onto(src, dst, MS_BIND | MS_REC);
    if (S_ISREG(st.st_mode))
        return touch_file(dst) || bind_onto(src, dst, MS_BIND);
    fprintf(stderr, "hwchroot: skipping %s (unsupported file type)\n", src);
    return 0;
}

static int synthetic(const char *name) {
    for (unsigned i = 0; i < sizeof(kSynthetic)/sizeof(kSynthetic[0]); i++)
        if (!strcmp(name, kSynthetic[i])) return 1;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <root> <program> [args...]\n", argv[0]);
        return 1;
    }
    const char *root = argv[1];
    if (unshare(CLONE_NEWNS)) { perror("hwchroot: unshare"); return 1; }
    // kitkat init leaves / shared-less, but be explicit so our binds never propagate
    mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL);

    if (make_dir(SKEL)) return 1;
    // did a previous run happen this boot? /dev is tmpfs, so reboot wipes it
    struct stat ran;
    int prev_ran = !stat(SKEL "/ran", &ran);
    // /dev whole first (it has kgsl-3d0, ion and deliberately no hwbinder):
    // the recursive bind snapshots /dev as it is right now, so it must happen
    // before the binds below attach under the same tmpfs, or they'd be
    // cloned into the guest's /dev/.glr too
    if (make_dir(SKEL "/dev") || bind_onto("/dev", SKEL "/dev", MS_BIND | MS_REC)) return 1;

    DIR *d = opendir(root);
    if (!d) { perror("hwchroot: opendir"); return 1; }
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (synthetic(e->d_name)) continue;
        if (expose(root, e->d_name)) { closedir(d); return 1; }
    }
    closedir(d);

    if (make_dir(SKEL "/proc") || bind_onto("/proc", SKEL "/proc", MS_BIND | MS_REC)) return 1;
    if (make_dir(SKEL "/sys") || bind_onto("/sys", SKEL "/sys", MS_BIND | MS_REC)) return 1;
    // writable scratch the adreno allocator uses; tmpfs, fresh every run
    if (make_dir(SKEL "/data") || make_dir(SKEL "/data/vendor") ||
        make_dir(SKEL "/data/vendor/gpu") || make_dir(SKEL "/data/misc") ||
        make_dir(SKEL "/data/misc/gpu")) return 1;

    fprintf(stderr, "hwchroot: assembled %s from %s\n", SKEL, root);

    // mark this run first (its failure only costs one skipped heal), then heal
    // if the daemon has started here before this boot
    touch_file(SKEL "/ran");
    char daemon[512];
    snprintf(daemon, sizeof(daemon), "%s%s", root, argv[2]);
    if (!access(daemon, X_OK) && prev_ran) {
        fprintf(stderr, "hwchroot: daemon restart, restarting surfaceflinger\n");
        restart_surfaceflinger();
    }

    if (chroot(SKEL) || chdir("/")) { perror("hwchroot: chroot"); return 1; }

    // kitkat's ANDROID_PROPERTY_WORKSPACE means nothing to modern bionic, don't pass it on
    char *envp[] = {"PATH=/system/bin", "LD_LIBRARY_PATH=" LIB_PATH, "HOME=/", NULL};
    execve(argv[2], &argv[2], envp);
    perror("hwchroot: execve");
    return 1;
}
