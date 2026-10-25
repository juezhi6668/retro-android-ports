#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>

/* Creates the zygote listen socket init would create, then execs
 * app_process --zygote with ANDROID_SOCKET_zygote pointing at it. */
int main(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, "/data/local/tmp/zyg.sock");
    unlink(addr.sun_path);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(fd, 50) < 0) { perror("listen"); return 1; }
    char env[64];
    snprintf(env, sizeof(env), "ANDROID_SOCKET_zygote=%d", fd);
    putenv(env);
    char *av[] = { "app_process", "-Xzygote", "/system/bin", "--zygote", NULL };
    execv("/system/bin/app_process", av);
    perror("execv");
    return 1;
}
