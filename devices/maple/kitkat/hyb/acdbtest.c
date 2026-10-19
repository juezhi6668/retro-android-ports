/* acdbtest: drive the Lineage 22.1 (Android 15) ACDB loader through libhyb.
 *
 * This is the diagnostic for that path: the HAL (audio.primary.maple) owns
 * calibration now, and this tool is how the loader, the acdbdata directory and
 * the fd lifetime were proven before wiring anything into mediaserver.
 * libacdbloader parses the Sony_yoshino_*.acdb files and hands the blocks to
 * /dev/msm_audio_cal, where q6asm/afe pick them up by acdb id; the blocks are
 * dropped when this process exits, which is what --hold demonstrates.
 *
 * usage: acdbtest [acdb-id] [dev-type]
 *   defaults: msm8998-tasha-snd-card, none, 15 (speaker), 1 (out)
 *   acdbtest --init-only            init the loader, send nothing
 *   acdbtest --send ID TYPE         one block only
 *   acdbtest --v3 --hold            v3 call (app-type/rate/idx path), stay
 *                                   resident so the cal blocks survive
 *   acdbtest --app-type N --rate N  app-type/rate for --v3
 *   acdbtest --card NAME --cvd VER  override card / cvd version
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "hyb.h"

typedef int (*acdb_init_v2_t)(char *, char *, int);
typedef int (*acdb_send_cal_t)(int, int);
typedef int (*acdb_send_cal_v3_t)(int, int, int, int, int);
typedef int (*acdb_get_cal_t)(void *, void *, unsigned int *);

int main(int argc, char **argv)
{
    const char *card = "msm8998-tasha-snd-card";
    char *cvd = NULL;
    int id = 15, type = 1, init_only = 0;
    void *h;
    acdb_init_v2_t init;
    acdb_send_cal_t send;
    int r, i, use_v3 = 0, app_type = 69936, rate = 48000, idx = 0, hold = 0;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--init-only")) init_only = 1;
        else if (!strcmp(argv[i], "--hold")) hold = 1;
        else if (!strcmp(argv[i], "--send") && i + 2 < argc) { id = atoi(argv[++i]); type = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--v3")) use_v3 = 1;
        else if (!strcmp(argv[i], "--app-type") && i + 1 < argc) app_type = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--rate") && i + 1 < argc) rate = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--card") && i + 1 < argc) card = argv[++i];
        else if (!strcmp(argv[i], "--cvd") && i + 1 < argc) cvd = argv[++i];
        else { id = atoi(argv[i]); if (i + 1 < argc) type = atoi(argv[++i]); }
    }

    h = hyb_dlopen("libacdbloader.so", 2 /* RTLD_NOW */);
    if (!h) {
        printf("dlopen(libacdbloader.so): %s\n", hyb_dlerror());
        return 1;
    }
    init = (acdb_init_v2_t)hyb_dlsym(h, "acdb_loader_init_v2");
    send = (acdb_send_cal_t)hyb_dlsym(h, "acdb_loader_send_audio_cal");
    if (!init || !send) {
        printf("dlsym: init=%p send=%p (%s)\n", (void *)init, (void *)send, hyb_dlerror());
        return 1;
    }

    r = init((char *)card, cvd, 0);
    printf("acdb_loader_init_v2(%s, %s, 0) = %d\n", card, cvd ? cvd : "NULL", r);
    if (r)
        return 1;
    if (init_only) {
        if (hold) {
            printf("holding ACDB open (pid %d)\n", getpid());
            for (;;) pause();
        }
        return 0;
    }

    if (use_v3) {
        acdb_send_cal_v3_t send3 = (acdb_send_cal_v3_t)hyb_dlsym(h, "acdb_loader_send_audio_cal_v3");
        if (!send3) { printf("no send_audio_cal_v3\n"); return 1; }
        r = send3(id, type, app_type, rate, idx);
        printf("acdb_loader_send_audio_cal_v3(acdb_id=%d, dev_type=%d, app_type=%d, rate=%d, idx=%d) = %d\n",
               id, type, app_type, rate, idx, r);
    } else {
        r = send(id, type);
        printf("acdb_loader_send_audio_cal(acdb_id=%d, dev_type=%d) = %d\n", id, type, r);
    }
    if (!r && hold) {
        printf("holding ACDB open (pid %d)\n", getpid());
        for (;;) pause();
    }
    return r ? 1 : 0;
}
