#include <unistd.h>
#include <stdio.h>
int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: dstart CMD [ARGS...]\n"); return 1; }
    if (fork() > 0) return 0;
    setsid();
    execvp(argv[1], &argv[1]);
    perror("execvp");
    return 127;
}
