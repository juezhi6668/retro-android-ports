#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef HAVE_ANDROID_LOG
#include <android/log.h>
#define LOG(...) __android_log_print(ANDROID_LOG_ERROR, "RILL", __VA_ARGS__)
#else
#include <stdio.h>
#define LOG(...) fprintf(stderr, __VA_ARGS__)
#endif

#define RILD "/system/bin/rild-msm8998"
#define RIL_LIBRARY "/system/vendor/msm8998-ril/lib/libril-qc-qmi-1.so"
#define LIBRARY_PATH "/system/vendor/msm8998-ril/lib:/vendor/lib:/system/lib"
#define CONSOLE_PATH "/data/misc/radio/rild-console.log"

static void log_open(const char *what, const char *path, int flags)
{
    int fd = open(path, flags);
    int err = errno;

    LOG("%s %s: fd=%d errno=%d", what, path, fd, fd < 0 ? err : 0);
    if (fd >= 0)
        close(fd);
}

int main(void)
{
    /* Without "--" rild takes the library arguments from rild.libargs, which
     * MIUI V5 still sets to "-d /dev/smd0" while the AOSP build has no such
     * property. Passing "--" gives QCRIL the same single-arg argv on both. */
    char *const argv[] = { RILD, "-l", RIL_LIBRARY, "--", NULL };
    const char *console = getenv("RIL_CONSOLE_LOG");
    struct stat st;
    pid_t pid;
    int fd, status;

    if (!console)
        console = CONSOLE_PATH;

    /* Init appends duplicate keys; a shell drops ANDROID_SOCKET_rild-debug. */
    if (setenv("LD_LIBRARY_PATH", LIBRARY_PATH, 1)) {
        LOG("setenv LD_LIBRARY_PATH: errno=%d", errno);
        return 1;
    }

    /* Whatever kills rild here is silent, so record the environment it opens. */
    log_open("diag", "/dev/diag", O_RDWR);
    LOG("qmux dir=%d connect=%d",
        stat("/dev/socket/qmux_radio", &st) == 0,
        stat("/dev/socket/qmux_radio/qmux_connect_socket", &st) == 0);

    /* Init points stdout/stderr at /dev/null; keep rild's own diagnostics. */
    fd = open(console, O_WRONLY | O_CREAT | O_APPEND, 0640);
    if (fd >= 0) {
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO)
            close(fd);
    } else {
        LOG("console %s: errno=%d", console, errno);
    }

    /* The parent outlives rild only long enough to report how it died. */
    pid = fork();
    if (pid < 0) {
        LOG("fork: errno=%d", errno);
        return 1;
    }
    if (pid == 0) {
        execv(argv[0], argv);
        LOG("exec %s: errno=%d", argv[0], errno);
        _exit(127);
    }
    if (waitpid(pid, &status, 0) < 0) {
        LOG("waitpid %d: errno=%d", pid, errno);
        return 1;
    }
    if (WIFEXITED(status)) {
        LOG("rild pid=%d exit=%d", pid, WEXITSTATUS(status));
        return WEXITSTATUS(status);
    }
    LOG("rild pid=%d signal=%d", pid, WTERMSIG(status));
    return 1;
}
