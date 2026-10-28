/* acdbtest: drive the Lineage 22.1 (Android 15) ACDB loader through libhyb.
 *
 * This is the diagnostic for that path: the HAL (audio.primary.msm8998) owns
 * calibration now, and this tool is how the loader, the acdbdata directory and
 * the fd lifetime were proven before wiring anything into mediaserver.
 * libacdbloader parses the ROM's *.acdb files (the Mi 6 ROM ships its own
 * set under /vendor/etc/acdbdata) and hands the blocks to
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
 *   acdbtest --scan                 one init, then report which RX/TX
 *                                   acdb ids this loader accepts (the TX
 *                                   sends poison capture until reboot —
 *                                   run it last)
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
    int scan = 0;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--init-only")) init_only = 1;
        else if (!strcmp(argv[i], "--hold")) hold = 1;
        else if (!strcmp(argv[i], "--scan")) scan = 1;
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

    /* --scan: which acdb ids does this loader actually accept? The check
     * inside libacdbloader is a hardcoded RX/TX table ("Device is not RX
     * or TX!acdb_id = %d" is logged for anything else), and older qcom
     * HALs pass ids this Android 15 build no longer knows. One loader
     * init, then every id in turn; send_audio_cal_v3's return value is
     * the answer (0 = the loader mapped it and pushed blocks).
     * RX (dev_type 1, app type 0x11130): speaker/headset editions of the
     * routes this port uses; TX (dev_type 2 — ACDB_DEV_TYPE_IN in
     * hardware/qcom/audio/hal/audio_hw.h, 0 is rejected, 0x11131): the
     * mic family. Careful: the TX sends that come back 0 push real
     * blocks; on msm8998 that kills capture prepare until reboot (see
     * docs/devices/sagit/quirks.md TEST 51), so run a scan last. */
    if (scan) {
        static const int rx[] = { 15, 10, 799 };
        static const int tx[] = { 4, 8, 9, 6, 35, 131, 143, 152 };
        acdb_send_cal_v3_t send3 = (acdb_send_cal_v3_t)hyb_dlsym(h, "acdb_loader_send_audio_cal_v3");
        size_t k;
        if (!send3) { printf("no send_audio_cal_v3\n"); return 1; }
        for (k = 0; k < sizeof(rx) / sizeof(rx[0]); k++) {
            r = send3(rx[k], 1, 0x11130, rate, 0);
            printf("RX acdb_id=%-4d dev_type=1 app=0x11130 -> %d %s\n", rx[k], r,
                   r ? "(rejected)" : "(accepted)");
        }
        for (k = 0; k < sizeof(tx) / sizeof(tx[0]); k++) {
            r = send3(tx[k], 2, 0x11131, rate, 0);
            printf("TX acdb_id=%-4d dev_type=2 app=0x11131 -> %d %s\n", tx[k], r,
                   r ? "(rejected)" : "(accepted)");
        }
        /* what the pre-fix HAL sent, for the record */
        r = send3(4, 0, 0x11131, rate, 0);
        printf("TX acdb_id=4    dev_type=0 app=0x11131 -> %d (the old bug)\n", r);
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
