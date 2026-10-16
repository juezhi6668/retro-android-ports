/* tone_test: tinyalsa diagnostic + direct playback for maple tasha card.
 *
 * Usage: tone_test list                 — dump all mixer control names
 *        tone_test route <name-file>    — apply NAME=VALUE lines from file
 *        tone_test play <wav>           — play 16-bit PCM wav on card0 pcm0
 *        tone_test hp <wav>             — apply headphone route, then play
 *        tone_test spk <wav>            — apply speaker route, then play
 *
 * Routes come from lineage mixer_paths_tasha.xml (headphones + speaker
 * paths). Missing controls are reported and skipped, never fatal.
 * Build on the server with the NDK armv7a clang against
 * aosp-kitkat/external/tinyalsa/include, dynamic link to the on-device
 * /system/lib/libtinyalsa.so.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tinyalsa/asoundlib.h>

static struct mixer *open_mixer(void)
{
    struct mixer *m = mixer_open(0);
    if (!m) {
        printf("mixer_open(0) failed\n");
        exit(1);
    }
    return m;
}

static void do_list(void)
{
    struct mixer *m = open_mixer();
    unsigned n = mixer_get_num_ctls(m);
    unsigned i;
    printf("card0 controls: %u\n", n);
    for (i = 0; i < n; i++) {
        struct mixer_ctl *c = mixer_get_ctl(m, i);
        printf("%u: %s\n", i, mixer_ctl_get_name(c));
    }
    mixer_close(m);
}

static int apply_one(struct mixer *m, const char *name, const char *val)
{
    struct mixer_ctl *c = mixer_get_ctl_by_name(m, name);
    char *end = NULL;
    long v;
    if (!c) {
        printf("SKIP missing: %s\n", name);
        return -1;
    }
    v = strtol(val, &end, 0);
    if (end && *end == '\0')
        mixer_ctl_set_value(c, 0, (int)v);
    else
        mixer_ctl_set_enum_by_string(c, val);
    printf("SET %s = %s\n", name, val);
    return 0;
}

/* NAME=VALUE per line, # comments */
static void do_route(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[512];
    struct mixer *m;
    if (!f) {
        printf("cannot open %s\n", path);
        exit(1);
    }
    m = open_mixer();
    while (fgets(line, sizeof(line), f)) {
        char *eq, *nl;
        if (line[0] == '#' || line[0] == '\n')
            continue;
        nl = strchr(line, '\n');
        if (nl)
            *nl = '\0';
        eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        apply_one(m, line, eq + 1);
    }
    fclose(f);
    mixer_close(m);
}

static void do_play(const char *wavpath)
{
    FILE *f = fopen(wavpath, "rb");
    struct pcm_config cfg;
    struct pcm *pcm;
    char buf[4096];
    size_t n;
    if (!f) {
        printf("cannot open %s\n", wavpath);
        exit(1);
    }
    /* skip 44-byte wav header */
    if (fread(buf, 1, 44, f) != 44) {
        printf("short wav header\n");
        exit(1);
    }
    memset(&cfg, 0, sizeof(cfg));
    cfg.channels = 2;
    cfg.rate = 48000;
    cfg.period_size = 1024;
    cfg.period_count = 4;
    cfg.format = PCM_FORMAT_S16_LE;
    pcm = pcm_open(0, 0, PCM_OUT, &cfg);
    if (!pcm_is_ready(pcm)) {
        printf("pcm not ready: %s\n", pcm_get_error(pcm));
        exit(1);
    }
    printf("pcm ready, writing...\n");
    /* upstream wav is mono: duplicate to stereo on the fly */
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        short *in = (short *)buf;
        unsigned frames = n / 2;
        unsigned i;
        static short stereo[4096];
        if (frames > 2048)
            frames = 2048;
        for (i = 0; i < frames; i++) {
            stereo[2 * i] = in[i];
            stereo[2 * i + 1] = in[i];
        }
        if (pcm_write(pcm, stereo, frames) != 0) {
            printf("pcm_write err: %s\n", pcm_get_error(pcm));
            break;
        }
    }
    printf("done, closing (standby)\n");
    pcm_close(pcm);
    fclose(f);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: tone_test list|route <file>|play <wav>|hp <wav>|spk <wav>\n");
        return 1;
    }
    if (!strcmp(argv[1], "list"))
        do_list();
    else if (!strcmp(argv[1], "route") && argc > 2)
        do_route(argv[2]);
    else if (!strcmp(argv[1], "play") && argc > 2)
        do_play(argv[2]);
    else if (!strcmp(argv[1], "hp") && argc > 2) {
        do_route("/data/local/tmp/route-hp.txt");
        do_play(argv[2]);
    } else if (!strcmp(argv[1], "spk") && argc > 2) {
        do_route("/data/local/tmp/route-spk.txt");
        do_play(argv[2]);
    } else {
        printf("bad args\n");
        return 1;
    }
    return 0;
}
