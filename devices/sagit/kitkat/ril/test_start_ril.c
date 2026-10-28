#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

extern char **environ;
static int test_execv(const char *path, char *const argv[]);

#define main ril_launcher_main
#define execv test_execv
#include "start-ril.c"
#undef execv
#undef main

/* Runs in the forked child with the launcher's environment and descriptors. */
static int test_execv(const char *path, char *const argv[])
{
    int count = 0;
    char **entry;
    char link[64] = "";
    struct stat st;
    const char *libs = getenv("LD_LIBRARY_PATH");
    const char *radio = getenv("ANDROID_SOCKET_rild");
    const char *pid = getenv("TEST_LAUNCHER_PID");
    const char *console = getenv("RIL_CONSOLE_LOG");

    assert(!strcmp(path, "/system/bin/rild-msm8998"));
    assert(!strcmp(argv[0], path));
    assert(!strcmp(argv[1], "-l"));
    assert(!strcmp(argv[2], "/system/vendor/msm8998-ril/lib/libril-qc-qmi-1.so"));
    assert(!strcmp(argv[3], "--"));
    assert(argv[4] == NULL);
    assert(libs && !strcmp(libs, "/system/vendor/msm8998-ril/lib:/vendor/lib:/system/lib"));
    for (entry = environ; *entry; ++entry)
        if (!strncmp(*entry, "LD_LIBRARY_PATH=", 16))
            ++count;
    assert(count == 1);
    assert(radio && getenv("ANDROID_SOCKET_rild-debug") && pid && console);
    assert(getppid() == atoi(pid));
    assert(fstat(STDERR_FILENO, &st) == 0 && S_ISREG(st.st_mode));
    assert(fstat(STDOUT_FILENO, &st) == 0 && S_ISREG(st.st_mode));
    assert(readlink("/proc/self/fd/2", link, sizeof(link) - 1) > 0);
    assert(!strcmp(link, console));
    memset(link, 0, sizeof link);
    assert(readlink("/proc/self/fd/1", link, sizeof(link) - 1) > 0);
    assert(!strcmp(link, console));
    assert(send(atoi(radio), "ok", 2, 0) == 2);
    _exit(0);
}

int main(void)
{
    int fd[2];
    int out = dup(STDOUT_FILENO);
    char value[32];
    char reply[2];

    assert(out >= 0);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == 0);
    snprintf(value, sizeof(value), "%d", fd[0]);
    assert(setenv("ANDROID_SOCKET_rild", value, 1) == 0);
    snprintf(value, sizeof(value), "%d", fd[1]);
    assert(setenv("ANDROID_SOCKET_rild-debug", value, 1) == 0);
    snprintf(value, sizeof(value), "%ld", (long)getpid());
    assert(setenv("TEST_LAUNCHER_PID", value, 1) == 0);
    assert(setenv("LD_LIBRARY_PATH", "/vendor/lib:/system/lib", 1) == 0);
    snprintf(value, sizeof(value), "/tmp/rild-console.%ld", (long)getpid());
    assert(setenv("RIL_CONSOLE_LOG", value, 1) == 0);
    assert(ril_launcher_main() == 0);
    assert(recv(fd[1], reply, sizeof(reply), MSG_DONTWAIT) == 2);
    assert(!memcmp(reply, "ok", 2));
    unlink(value);
    /* The launcher redirected our stdout to that log for the child's sake. */
    dprintf(out, "PASS: launcher overrides library path, keeps both socket variables and descriptors, redirects stdout/stderr\n");
    close(out);
    return 0;
}
