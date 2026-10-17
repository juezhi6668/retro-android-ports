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
#include <limits.h>
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

static void do_play(const char *wavpath, unsigned period)
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
    if (!period)
        period = 1024;
    memset(&cfg, 0, sizeof(cfg));
    cfg.channels = 2;
    cfg.rate = 48000;
    cfg.period_size = period;
    cfg.period_count = 4;
    cfg.format = PCM_FORMAT_S16_LE;
    cfg.start_threshold = period;
    /* see audio_hw.c: the adsp acks buffers faster than realtime, hw_ptr
     * catches appl_ptr; the tinyalsa default stop threshold turns that
     * into an XRUN ~30 ms in (TRIGGER_STOP + CMD_EOS), then every later
     * write times out. INT_MAX = no underrun stop, qcom-hal style. */
    cfg.stop_threshold = INT_MAX;
    pcm = pcm_open(0, 0, PCM_OUT, &cfg);
    if (!pcm_is_ready(pcm)) {
        printf("pcm not ready: %s\n", pcm_get_error(pcm));
        exit(1);
    }
    printf("pcm ready (period %u), writing...\n", period);
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

static void do_cap(const char *wavpath, int seconds, unsigned period)
{
    FILE *f;
    struct pcm_config cfg;
    struct pcm *pcm;
    short buf[4096];
    int written = 0, total;
    if (!seconds || seconds > 60)
        seconds = 5;
    /* mic route from audio_hw.c mic_path() */
    {
        struct mixer *m = open_mixer();
        struct { const char *n, *v; } c[] = {
            { "MultiMedia1 Mixer SLIM_0_TX", "1" },
            { "AIF1_CAP Mixer SLIM TX7", "1" },
            { "SLIM_0_TX Channels", "One" },
            { "SLIM TX7 MUX", "DEC7" },
            { "ADC MUX7", "DMIC" },
            { "DMIC MUX7", "DMIC0" },
            { "IIR0 INP0 MUX", "DEC7" },
            { "DEC7 Volume", "104" },
            { NULL, NULL }
        };
        unsigned i;
        for (i = 0; c[i].n; i++)
            apply_one(m, c[i].n, c[i].v);
        mixer_close(m);
    }
    f = fopen("/data/local/tmp/.cap.raw", "wb");
    if (!f) {
        printf("cannot open raw file\n");
        exit(1);
    }
    if (!period)
        period = 1024;
    memset(&cfg, 0, sizeof(cfg));
    cfg.channels = 1;
    cfg.rate = 48000;
    cfg.period_size = period;
    cfg.period_count = 4;
    cfg.format = PCM_FORMAT_S16_LE;
    pcm = pcm_open(0, 0, PCM_IN, &cfg);
    if (!pcm_is_ready(pcm)) {
        printf("capture pcm not ready: %s\n", pcm_get_error(pcm));
        exit(1);
    }
    printf("capturing %d s...\n", seconds);
    total = seconds * (48000 / period);
    while (written < total) {
        if (pcm_read(pcm, buf, period) != 0) {
            printf("pcm_read err: %s\n", pcm_get_error(pcm));
            break;
        }
        fwrite(buf, 1, period * 2, f);
        written++;
    }
    printf("captured %d frames*%u\n", written, period);
    fclose(f);
    pcm_close(pcm);
    /* second pass: raw data + a computed header -> wav (mono 16-bit) */
    {
        unsigned datasize = written * period * 2;
        unsigned char h[44] = {
            'R','I','F','F', 0,0,0,0, 'W','A','V','E',
            'f','m','t',' ', 16,0,0,0, 1,0, 1,0, 0x80,0xbb,0,0,
            0,0x77,0x01,0, 2,0, 16,0,
            'd','a','t','a', 0,0,0,0
        };
        unsigned riff = datasize + 36;
        FILE *in, *out;
        char cbuf[8192];
        size_t n;
        h[4] = riff & 0xff; h[5] = (riff >> 8) & 0xff;
        h[6] = (riff >> 16) & 0xff; h[7] = (riff >> 24) & 0xff;
        h[40] = datasize & 0xff; h[41] = (datasize >> 8) & 0xff;
        h[42] = (datasize >> 16) & 0xff; h[43] = (datasize >> 24) & 0xff;
        in = fopen("/data/local/tmp/.cap.raw", "rb");
        out = fopen(wavpath, "wb");
        if (!in || !out) {
            printf("cannot reopen for wav write\n");
            exit(1);
        }
        fwrite(h, 1, sizeof(h), out);
        while ((n = fread(cbuf, 1, sizeof(cbuf), in)) > 0)
            fwrite(cbuf, 1, n, out);
        fclose(in);
        fclose(out);
        unlink("/data/local/tmp/.cap.raw");
    }
}

int main(int argc, char **argv)
{
    unsigned period = 0;
    if (argc < 2) {
        printf("usage: tone_test list|route <file>|play <wav> [period]|hp <wav>|"
               "spk <wav>|cap <wav> [sec] [period]\n");
        return 1;
    }
    if ((!strcmp(argv[1], "play")) && argc > 3)
        period = atoi(argv[3]);
    if (!strcmp(argv[1], "list"))
        do_list();
    else if (!strcmp(argv[1], "route") && argc > 2)
        do_route(argv[2]);
    else if (!strcmp(argv[1], "play") && argc > 2)
        do_play(argv[2], period);
    else if (!strcmp(argv[1], "cap") && argc > 2)
        do_cap(argv[2], argc > 3 ? atoi(argv[3]) : 5,
               (argc > 4) ? (unsigned)atoi(argv[4]) : 0);
    else if (!strcmp(argv[1], "hp") && argc > 2) {
        do_route("/data/local/tmp/route-hp.txt");
        do_play(argv[2], 0);
    } else if (!strcmp(argv[1], "spk") && argc > 2) {
        do_route("/data/local/tmp/route-spk.txt");
        do_play(argv[2], 0);
    } else {
        printf("bad args\n");
        return 1;
    }
    return 0;
}
