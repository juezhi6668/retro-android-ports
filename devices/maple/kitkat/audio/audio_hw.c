/* audio.primary.maple — tinyalsa HAL for the Sony Xperia XZ Premium.
 *
 * Ported pattern from CHARQUIRKS audio (audio.primary.pdx203): playback,
 * capture and a usb-card fallback through the adsp, decimator +20 dB so
 * recording is usable. Card is msm8998-tavil-snd-card
 * (WCD9340 + WSA881x amps); speaker route taken from lineage's
 * mixer_paths_tasha.xml ("speaker" + "deep-buffer-playback" =
 * SLIMBUS_0_RX <- MultiMedia1, pcm 0), mic route from "handset-mic" /
 * "dmic1" (SLIM_0_TX -> MultiMedia1, pcm 0). The adsp resamples capture
 * itself, so every rate opens on pcm 0.
 *
 * Silent-safe: with no soundcard (card never probes yet — see
 * docs/devices/maple/quirks.md TEST 36) open succeeds and write discards / read
 * returns silence, so mediaserver stays up and audio starts working the
 * moment the card appears.
 *
 * ACDB calibration (2026-10-19): without it q6asm rejects every stream
 * ("send_afe_cal_type cal_block not found", "q6asm_send_cal: cal_block is
 * NULL") and the dsp drains the buffer faster than realtime with no
 * audible output. The loader that parses /vendor/etc/acdbdata is an
 * Android 15 library (libacdbloader.so), so it is brought in through
 * libhyb exactly like the Adreno driver. The HAL keeps the handle open
 * for the life of mediaserver: the cal blocks live in the kernel and are
 * dropped when the loader's /dev/msm_audio_cal fd closes. Before every
 * stream start the HAL declares the device acdb id in the kernel's
 * "Audio Stream 0 App Type Cfg" control and asks the loader for the
 * matching blocks (speaker 15 for now; headset routing is future work).
 *
 * Ops-table discipline (learned the hard way 2026-10-16: mediaserver
 * SIGSEGV at AudioFlinger.cpp:1349 calling dev->init_check through NULL):
 * every function pointer AudioFlinger/AudioPolicyManager can reach is
 * assigned — device (close/init_check/volumes/mute/mode/mic/params/
 * input_buffer_size/streams/dump) and both stream commons
 * (rate/buffer/channels/format/standby/dump/params/effects) plus
 * out (latency/volume/write/render_position/write_timestamp) and in
 * (gain/read/frames_lost). Unused knobs return -ENOSYS or neutral
 * values, never NULL.
 *
 * verify-me live: speaker playback (ringtone), mic capture (sound
 * recorder needs libsrec_jni — in full_maple.mk), BT SCO is known-dead
 * (slimbus into the adsp, nothing drives it: CHARQUIRKS).
 */

#define LOG_TAG "audio.primary.maple"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cutils/log.h>
#include <cutils/str_parms.h>
#include <hardware/audio.h>
#include <hardware/hardware.h>
#include <system/audio.h>
#include <tinyalsa/asoundlib.h>

#include "hyb.h"

#define CARD_NAME_TASHA "msm8998-tasha-snd-card"
#define CARD_NAME_TAVIL "msm8998-tavil-snd-card"
/* Live 2026-10-16 the tasha card (sound-9335) wins the probe race and
 * registers as card 0 once the adsp is booted (`echo 1 >
 * /sys/kernel/boot_adsp/boot` with adsp.* staged); tavil fails its
 * slimbus logical-address assignment (-6) and never appears. Prefer
 * tasha, fall back to tavil. */
#define PCM_PLAYBACK_DEVICE 0
#define PCM_CAPTURE_DEVICE 0
#define PLAYBACK_RATE 48000
#define PLAYBACK_CHANNELS 2
#define PLAYBACK_PERIOD_SIZE 1024
#define PLAYBACK_PERIODS 4

/* qcom HAL's DEFAULT_APP_TYPE_RX_PATH (hal/msm8974/platform.c) and
 * ACDB_DEV_TYPE_OUT (linux/msm_audio_calibration.h). Speaker acdb id 15
 * comes from the vendor audio_platform_info.xml acdb_ids table. */
#define ACDB_APP_TYPE_RX 69936
#define ACDB_DEV_TYPE_OUT 1
#define ACDB_ID_SPEAKER 15

typedef int (*acdb_init_fn)(char *card, char *cvd, int flag);
typedef int (*acdb_send_fn)(int acdb_dev_id, int dev_type, int app_type,
                            int sample_rate, int idx);

struct maple_audio_device {
    struct audio_hw_device device;
    pthread_mutex_t lock;
    struct mixer *mixer;
    unsigned int card;
    int out_open;
    int in_open;
    const char *card_name;
    void *acdb;
    acdb_init_fn acdb_init;
    acdb_send_fn acdb_send;
};

struct maple_stream_out {
    struct audio_stream_out stream;
    pthread_mutex_t lock;
    struct maple_audio_device *adev;
    struct pcm *pcm;
    int standby;
};

struct maple_stream_in {
    struct audio_stream_in stream;
    pthread_mutex_t lock;
    struct maple_audio_device *adev;
    struct pcm *pcm;
    int standby;
    unsigned int rate;
    unsigned int channels;
};

/* name/value pairs; enum values go through set_enum_by_string when the
 * value is non-numeric. Missing controls are skipped, never fatal. */
struct ctl_setting {
    const char *name;
    const char *str; /* enum/string value, else NULL */
    int value;       /* numeric value, used when str == NULL */
};

static void set_ctl(struct mixer *mixer, const char *name,
                    const char *str, int value)
{
    struct mixer_ctl *ctl;
    if (!mixer)
        return;
    ctl = mixer_get_ctl_by_name(mixer, name);
    if (!ctl) {
        ALOGV("ctl '%s' not present, skipping", name);
        return;
    }
    if (str)
        mixer_ctl_set_enum_by_string(ctl, str);
    else
        mixer_ctl_set_value(ctl, 0, value);
}

/* "speaker" path from mixer_paths_tasha.xml plus the deep-buffer
 * playback frontend (SLIMBUS_0_RX <- MultiMedia1). on=1 enables. */
static void speaker_path(struct mixer *mixer, int on)
{
    static const struct ctl_setting spk[] = {
        { "COMP7 Switch", NULL, 1 },
        { "COMP8 Switch", NULL, 1 },
        { "AIF4_VI Mixer SPKR_VI_1", NULL, 1 },
        { "AIF4_VI Mixer SPKR_VI_2", NULL, 1 },
        { "SLIM_4_TX Format", "PACKED_16B", 0 },
        { "SLIM RX0 MUX", "AIF_MIX1_PB", 0 },
        { "SLIM RX1 MUX", "AIF_MIX1_PB", 0 },
        { "SLIM_0_RX Channels", "Two", 0 },
        { "RX INT7_1 MIX1 INP0", "RX0", 0 },
        { "RX INT8_1 MIX1 INP0", "RX1", 0 },
        { "RX7 Digital Volume", NULL, 82 },
        { "RX8 Digital Volume", NULL, 82 },
        { "SpkrLeft COMP Switch", NULL, 1 },
        { "SpkrRight COMP Switch", NULL, 1 },
        { "SpkrLeft BOOST Switch", NULL, 1 },
        { "SpkrRight BOOST Switch", NULL, 1 },
        { "SpkrLeft VISENSE Switch", NULL, 1 },
        { "SpkrRight VISENSE Switch", NULL, 1 },
        { "SpkrLeft SWR DAC_Port Switch", NULL, 1 },
        { "SpkrRight SWR DAC_Port Switch", NULL, 1 },
        { "VI_FEED_TX Channels", "Two", 0 },
        { "SLIM0_RX_VI_FB_LCH_MUX", "SLIM4_TX", 0 },
        { "SLIM0_RX_VI_FB_RCH_MUX", "SLIM4_TX", 0 },
        /* lineage ships amp gain with speaker protection on; without it
         * the safe value is lower (CHARQUIRKS speaker: 10 not 18).
         * RX7/8 above carry the level; keep protection mixers at unity. */
        { NULL, NULL, 0 },
    };
    unsigned i;
    set_ctl(mixer, "SLIMBUS_0_RX Audio Mixer MultiMedia1", NULL, on);
    if (!on)
        return;
    for (i = 0; spk[i].name; i++)
        set_ctl(mixer, spk[i].name, spk[i].str, spk[i].value);
}

/* "handset-mic"/"dmic1" from mixer_paths_tasha.xml plus the capture
 * frontend (MultiMedia1 <- SLIM_0_TX). No acdb, so the decimator sits at
 * 84 (0 dB) + 20 for a usable level (CHARQUIRKS mics). */
static void mic_path(struct mixer *mixer, int on)
{
    set_ctl(mixer, "MultiMedia1 Mixer SLIM_0_TX", NULL, on);
    if (!on)
        return;
    set_ctl(mixer, "AIF1_CAP Mixer SLIM TX7", NULL, 1);
    set_ctl(mixer, "SLIM_0_TX Channels", "One", 0);
    set_ctl(mixer, "SLIM TX7 MUX", "DEC7", 0);
    set_ctl(mixer, "ADC MUX7", "DMIC", 0);
    set_ctl(mixer, "DMIC MUX7", "DMIC0", 0);
    set_ctl(mixer, "IIR0 INP0 MUX", "DEC7", 0);
    set_ctl(mixer, "DEC7 Volume", NULL, 104);
}

static unsigned int find_card(const char *name)
{
    FILE *f;
    char line[128];
    unsigned int card = 0;
    f = fopen("/proc/asound/cards", "r");
    if (!f)
        return 0;
    while (fgets(line, sizeof(line), f)) {
        /* lines look like " 0 [tavil          ]: msm8998-tavil-snd-card ..." */
        if (strstr(line, name)) {
            card = (unsigned int)atoi(line);
            break;
        }
    }
    fclose(f);
    return card;
}

/* ---- ACDB calibration ------------------------------------------------ */

/* mediaserver opens the HAL around 5 s but the tasha card registers at
 * ~8 s (the adsp has to boot first), so adev_open can run before
 * /proc/asound/cards lists the card. Opening the mixer there failed and
 * the NULL was cached forever: the route was never written, pcm_open
 * still succeeded (card 0 exists by then) and framework playback was
 * silent while tone_test (own mixer) worked. Re-detect on first use. */
static int ensure_card(struct maple_audio_device *adev)
{
    if (adev->mixer)
        return 0;
    adev->card = find_card(CARD_NAME_TASHA);
    adev->card_name = CARD_NAME_TASHA;
    adev->mixer = mixer_open(adev->card);
    if (!adev->mixer) {
        adev->card_name = CARD_NAME_TAVIL;
        adev->mixer = mixer_open(find_card(CARD_NAME_TAVIL));
    }
    if (!adev->mixer)
        return -ENODEV;
    ALOGI("audio.primary.maple: late card %u (%s) mixer %p", adev->card,
          adev->card_name, (void *)adev->mixer);
    return 0;
}

/* Load libacdbloader (Android 15, via libhyb) and initialize it for the
 * card that is now present. The cal blocks live in the kernel and stay
 * until the loader's /dev/msm_audio_cal fd closes, so the handle is kept
 * for the life of mediaserver. Only call with the card up: initializing
 * before the codec exists fails in ADIE RTAC init with -ENOMEM. */
static int acdb_ensure(struct maple_audio_device *adev)
{
    int r;
    if (adev->acdb_send)
        return 0;
    if (ensure_card(adev))
        return -ENODEV;
    if (!adev->acdb) {
        adev->acdb = hyb_dlopen("libacdbloader.so", 2 /* RTLD_NOW */);
        if (!adev->acdb) {
            ALOGE("acdb: libacdbloader not loadable: %s", hyb_dlerror());
            return -ENOENT;
        }
        adev->acdb_init = (acdb_init_fn)hyb_dlsym(adev->acdb,
                                                  "acdb_loader_init_v2");
        adev->acdb_send = (acdb_send_fn)hyb_dlsym(adev->acdb,
                                        "acdb_loader_send_audio_cal_v3");
        if (!adev->acdb_init || !adev->acdb_send) {
            ALOGE("acdb: loader symbols missing: %s", hyb_dlerror());
            adev->acdb_send = NULL;
            return -ENOENT;
        }
    }
    r = adev->acdb_init((char *)adev->card_name, NULL, 0);
    ALOGI("acdb: loader init(%s) = %d", adev->card_name, r);
    if (r) {
        adev->acdb_send = NULL;
        return r;
    }
    return 0;
}

/* Declare the stream's device acdb id to the kernel and load the matching
 * calibration blocks. Must run before the backend route opens the AFE
 * port: afe_get_cal_topology_id()/send_afe_cal_type() look the blocks up
 * when the port is enabled, and q6asm_send_cal() when the PCM opens. */
static void acdb_stream_cal(struct maple_audio_device *adev, int acdb_id)
{
    struct mixer_ctl *ctl;
    int cfg[3];
    int r;

    if (acdb_ensure(adev))
        ALOGW("acdb: no calibration for this stream");
    ctl = mixer_get_ctl_by_name(adev->mixer, "Audio Stream 0 App Type Cfg");
    if (!ctl) {
        ALOGE("acdb: no 'Audio Stream 0 App Type Cfg' control");
    } else {
        cfg[0] = ACDB_APP_TYPE_RX;
        cfg[1] = acdb_id;
        cfg[2] = PLAYBACK_RATE;
        if (mixer_ctl_set_array(ctl, cfg, 3) < 0)
            ALOGE("acdb: app type cfg write failed");
    }
    if (!adev->acdb_send) {
        ALOGW("acdb: loader unavailable, stream runs uncalibrated");
        return;
    }
    r = adev->acdb_send(acdb_id, ACDB_DEV_TYPE_OUT, ACDB_APP_TYPE_RX,
                        PLAYBACK_RATE, 0);
    ALOGV("acdb: send cal id %d -> %d", acdb_id, r);
}

static struct pcm_config pcm_config_out = {
    .channels = PLAYBACK_CHANNELS,
    .rate = PLAYBACK_RATE,
    .period_size = PLAYBACK_PERIOD_SIZE,
    .period_count = PLAYBACK_PERIODS,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = PLAYBACK_PERIOD_SIZE,
    /* the adsp acks ASM buffers far faster than realtime (adm buffers
     * internally), so hw_ptr keeps catching up with appl_ptr. with
     * tinyalsa's default stop_threshold (= buffer frames) that reads as
     * an underrun: SNDRV_PCM_TRIGGER_STOP + CMD_EOS ~30 ms into a fresh
     * stream, after which the dsp stops acking and pcm_write dies with
     * ETIMEDOUT (live kernel trace 2026-10-17). qcom's own hal uses
     * INT_MAX here; verified against q6asm/q6adm dyn-debug. */
    .stop_threshold = INT_MAX,
};

/* ---- output stream ---- */

static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
    (void)stream;
    return PLAYBACK_RATE;
}

static int out_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    (void)stream;
    (void)rate;
    return 0;
}

static size_t out_get_buffer_size(const struct audio_stream *stream)
{
    (void)stream;
    return PLAYBACK_PERIOD_SIZE * PLAYBACK_CHANNELS * sizeof(int16_t);
}

static audio_channel_mask_t out_get_channels(const struct audio_stream *stream)
{
    (void)stream;
    return AUDIO_CHANNEL_OUT_STEREO;
}

static audio_format_t out_get_format(const struct audio_stream *stream)
{
    (void)stream;
    return AUDIO_FORMAT_PCM_16_BIT;
}

static int out_set_format(struct audio_stream *stream, audio_format_t format)
{
    (void)stream;
    (void)format;
    return 0;
}

static int out_dump(const struct audio_stream *stream, int fd)
{
    (void)stream;
    (void)fd;
    return 0;
}

static int out_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    (void)stream;
    (void)kvpairs;
    return 0;
}

static char *out_get_parameters(const struct audio_stream *stream,
                                const char *keys)
{
    (void)stream;
    (void)keys;
    return strdup("");
}

static int out_add_audio_effect(const struct audio_stream *stream,
                                effect_handle_t effect)
{
    (void)stream;
    (void)effect;
    return 0;
}

static int out_remove_audio_effect(const struct audio_stream *stream,
                                   effect_handle_t effect)
{
    (void)stream;
    (void)effect;
    return 0;
}

static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
    (void)stream;
    return (PLAYBACK_PERIOD_SIZE * PLAYBACK_PERIODS * 1000) / PLAYBACK_RATE;
}

static int out_set_volume(struct audio_stream_out *stream, float left,
                          float right)
{
    (void)stream;
    (void)left;
    (void)right;
    return 0;
}

static ssize_t out_write(struct audio_stream_out *stream, const void *buffer,
                         size_t bytes)
{
    struct maple_stream_out *out = (struct maple_stream_out *)stream;
    struct maple_audio_device *adev = out->adev;
    int rc;
    pthread_mutex_lock(&out->lock);
    if (out->standby) {
        out->standby = 0;
        pthread_mutex_lock(&adev->lock);
        acdb_stream_cal(adev, ACDB_ID_SPEAKER);
        speaker_path(adev->mixer, 1);
        pthread_mutex_unlock(&adev->lock);
        out->pcm = pcm_open(adev->card, PCM_PLAYBACK_DEVICE,
                            PCM_OUT, &pcm_config_out);
        if (!pcm_is_ready(out->pcm)) {
            ALOGV("pcm_open not ready (no card yet?), discarding %zu bytes",
                  bytes);
            pcm_close(out->pcm);
            out->pcm = NULL;
            pthread_mutex_unlock(&out->lock);
            return bytes;
        }
    }
    if (!out->pcm) {
        pthread_mutex_unlock(&out->lock);
        return bytes;
    }
    /* kitkat tinyalsa pcm_write() takes BYTES (it divides by the frame
     * size itself), not frames: passing `frames` wrote a quarter of the
     * mixer buffer and dropped the rest, which is why the dsp got a
     * chopped signal and the speaker stayed silent while tone_test
     * (same buffer math in frames) seemed fine. */
    rc = pcm_write(out->pcm, buffer, bytes);
    if (rc != 0) {
        /* ADSP restart (modem RELATED restart) can invalidate the PCM
         * handle; drop it so the next write re-opens on the new card. */
        pcm_close(out->pcm);
        out->pcm = NULL;
        out->standby = 1;
    }
    pthread_mutex_unlock(&out->lock);
    if (rc != 0)
        return -EIO;
    return bytes;
}

static int out_get_render_position(const struct audio_stream_out *stream,
                                   uint32_t *dsp_frames)
{
    (void)stream;
    (void)dsp_frames;
    return -EINVAL;
}

static int out_get_next_write_timestamp(const struct audio_stream_out *stream,
                                        int64_t *timestamp)
{
    (void)stream;
    (void)timestamp;
    return -EINVAL;
}

static int out_standby(struct audio_stream *stream)
{
    struct maple_stream_out *out = (struct maple_stream_out *)stream;
    struct maple_audio_device *adev = out->adev;
    pthread_mutex_lock(&out->lock);
    if (!out->standby) {
        out->standby = 1;
        if (out->pcm) {
            pcm_close(out->pcm);
            out->pcm = NULL;
        }
        pthread_mutex_lock(&adev->lock);
        adev->out_open--;
        if (adev->out_open == 0)
            speaker_path(adev->mixer, 0);
        pthread_mutex_unlock(&adev->lock);
    }
    pthread_mutex_unlock(&out->lock);
    return 0;
}

/* ---- input stream ---- */

static uint32_t in_get_sample_rate(const struct audio_stream *stream)
{
    struct maple_stream_in *in = (struct maple_stream_in *)stream;
    return in->rate;
}

static int in_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    (void)stream;
    (void)rate;
    return 0;
}

static size_t in_get_buffer_size(const struct audio_stream *stream)
{
    (void)stream;
    return 1024 * sizeof(int16_t);
}

static audio_channel_mask_t in_get_channels(const struct audio_stream *stream)
{
    struct maple_stream_in *in = (struct maple_stream_in *)stream;
    return in->channels == 2 ? AUDIO_CHANNEL_IN_STEREO : AUDIO_CHANNEL_IN_MONO;
}

static audio_format_t in_get_format(const struct audio_stream *stream)
{
    (void)stream;
    return AUDIO_FORMAT_PCM_16_BIT;
}

static int in_set_format(struct audio_stream *stream, audio_format_t format)
{
    (void)stream;
    (void)format;
    return 0;
}

static int in_dump(const struct audio_stream *stream, int fd)
{
    (void)stream;
    (void)fd;
    return 0;
}

static int in_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    (void)stream;
    (void)kvpairs;
    return 0;
}

static char *in_get_parameters(const struct audio_stream *stream,
                               const char *keys)
{
    (void)stream;
    (void)keys;
    return strdup("");
}

static int in_add_audio_effect(const struct audio_stream *stream,
                               effect_handle_t effect)
{
    (void)stream;
    (void)effect;
    return 0;
}

static int in_remove_audio_effect(const struct audio_stream *stream,
                                  effect_handle_t effect)
{
    (void)stream;
    (void)effect;
    return 0;
}

static int in_set_gain(struct audio_stream_in *stream, float gain)
{
    (void)stream;
    (void)gain;
    return 0;
}

static ssize_t in_read(struct audio_stream_in *stream, void *buffer,
                       size_t bytes)
{
    struct maple_stream_in *in = (struct maple_stream_in *)stream;
    struct maple_audio_device *adev = in->adev;
    struct pcm_config cfg;
    int rc;
    pthread_mutex_lock(&in->lock);
    if (in->standby) {
        in->standby = 0;
        pthread_mutex_lock(&adev->lock);
        ensure_card(adev);
        mic_path(adev->mixer, 1);
        pthread_mutex_unlock(&adev->lock);
        memset(&cfg, 0, sizeof(cfg));
        cfg.channels = in->channels;
        cfg.rate = in->rate;
        cfg.period_size = 1024;
        cfg.period_count = 4;
        cfg.format = PCM_FORMAT_S16_LE;
        in->pcm = pcm_open(adev->card, PCM_CAPTURE_DEVICE, PCM_IN, &cfg);
        if (!pcm_is_ready(in->pcm)) {
            ALOGV("capture pcm not ready, returning silence");
            pcm_close(in->pcm);
            in->pcm = NULL;
            memset(buffer, 0, bytes);
            pthread_mutex_unlock(&in->lock);
            return bytes;
        }
    }
    if (!in->pcm) {
        memset(buffer, 0, bytes);
        pthread_mutex_unlock(&in->lock);
        return bytes;
    }
    /* pcm_read() also takes bytes on kitkat tinyalsa */
    rc = pcm_read(in->pcm, buffer, bytes);
    pthread_mutex_unlock(&in->lock);
    if (rc != 0)
        return -EIO;
    return bytes;
}

static uint32_t in_get_input_frames_lost(struct audio_stream_in *stream)
{
    (void)stream;
    return 0;
}

static int in_standby(struct audio_stream *stream)
{
    struct maple_stream_in *in = (struct maple_stream_in *)stream;
    struct maple_audio_device *adev = in->adev;
    pthread_mutex_lock(&in->lock);
    if (!in->standby) {
        in->standby = 1;
        if (in->pcm) {
            pcm_close(in->pcm);
            in->pcm = NULL;
        }
        pthread_mutex_lock(&adev->lock);
        adev->in_open--;
        if (adev->in_open == 0)
            mic_path(adev->mixer, 0);
        pthread_mutex_unlock(&adev->lock);
    }
    pthread_mutex_unlock(&in->lock);
    return 0;
}

/* ---- device ---- */

static int adev_open_output_stream(struct audio_hw_device *dev,
                                   audio_io_handle_t handle,
                                   audio_devices_t devices,
                                   audio_output_flags_t flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out)
{
    struct maple_audio_device *adev = (struct maple_audio_device *)dev;
    struct maple_stream_out *out;
    (void)handle;
    (void)devices;
    (void)flags;
    out = calloc(1, sizeof(*out));
    if (!out)
        return -ENOMEM;
    pthread_mutex_init(&out->lock, NULL);
    out->adev = adev;
    out->standby = 1;
    out->stream.common.get_sample_rate = out_get_sample_rate;
    out->stream.common.set_sample_rate = out_set_sample_rate;
    out->stream.common.get_buffer_size = out_get_buffer_size;
    out->stream.common.get_channels = out_get_channels;
    out->stream.common.get_format = out_get_format;
    out->stream.common.set_format = out_set_format;
    out->stream.common.standby = out_standby;
    out->stream.common.dump = out_dump;
    out->stream.common.set_parameters = out_set_parameters;
    out->stream.common.get_parameters = out_get_parameters;
    out->stream.common.add_audio_effect = out_add_audio_effect;
    out->stream.common.remove_audio_effect = out_remove_audio_effect;
    out->stream.get_latency = out_get_latency;
    out->stream.set_volume = out_set_volume;
    out->stream.write = out_write;
    out->stream.get_render_position = out_get_render_position;
    out->stream.get_next_write_timestamp = out_get_next_write_timestamp;
    config->format = AUDIO_FORMAT_PCM_16_BIT;
    config->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
    config->sample_rate = PLAYBACK_RATE;
    *stream_out = &out->stream;
    pthread_mutex_lock(&adev->lock);
    adev->out_open++;
    pthread_mutex_unlock(&adev->lock);
    return 0;
}

static void adev_close_output_stream(struct audio_hw_device *dev,
                                     struct audio_stream_out *stream)
{
    struct maple_stream_out *out = (struct maple_stream_out *)stream;
    out_standby(&stream->common);
    free(out);
    (void)dev;
}

static int adev_open_input_stream(struct audio_hw_device *dev,
                                  audio_io_handle_t handle,
                                  audio_devices_t devices,
                                  struct audio_config *config,
                                  struct audio_stream_in **stream_in)
{
    struct maple_audio_device *adev = (struct maple_audio_device *)dev;
    struct maple_stream_in *in;
    (void)handle;
    (void)devices;
    in = calloc(1, sizeof(*in));
    if (!in)
        return -ENOMEM;
    pthread_mutex_init(&in->lock, NULL);
    in->adev = adev;
    in->standby = 1;
    in->rate = config->sample_rate ? config->sample_rate : 8000;
    in->channels = (config->channel_mask == AUDIO_CHANNEL_IN_STEREO) ? 2 : 1;
    in->stream.common.get_sample_rate = in_get_sample_rate;
    in->stream.common.set_sample_rate = in_set_sample_rate;
    in->stream.common.get_buffer_size = in_get_buffer_size;
    in->stream.common.get_channels = in_get_channels;
    in->stream.common.get_format = in_get_format;
    in->stream.common.set_format = in_set_format;
    in->stream.common.standby = in_standby;
    in->stream.common.dump = in_dump;
    in->stream.common.set_parameters = in_set_parameters;
    in->stream.common.get_parameters = in_get_parameters;
    in->stream.common.add_audio_effect = in_add_audio_effect;
    in->stream.common.remove_audio_effect = in_remove_audio_effect;
    in->stream.set_gain = in_set_gain;
    in->stream.read = in_read;
    in->stream.get_input_frames_lost = in_get_input_frames_lost;
    config->format = AUDIO_FORMAT_PCM_16_BIT;
    *stream_in = &in->stream;
    pthread_mutex_lock(&adev->lock);
    adev->in_open++;
    pthread_mutex_unlock(&adev->lock);
    return 0;
}

static void adev_close_input_stream(struct audio_hw_device *dev,
                                    struct audio_stream_in *stream)
{
    struct maple_stream_in *in = (struct maple_stream_in *)stream;
    in_standby(&stream->common);
    free(in);
    (void)dev;
}

static int adev_set_parameters(struct audio_hw_device *dev, const char *kvpairs)
{
    (void)dev;
    (void)kvpairs;
    return 0;
}

static char *adev_get_parameters(const struct audio_hw_device *dev,
                                 const char *keys)
{
    (void)dev;
    (void)keys;
    return strdup("");
}

static int adev_init_check(const struct audio_hw_device *dev)
{
    (void)dev;
    return 0;
}

static int adev_set_voice_volume(struct audio_hw_device *dev, float volume)
{
    (void)dev;
    (void)volume;
    return 0;
}

static int adev_set_master_volume(struct audio_hw_device *dev, float volume)
{
    (void)dev;
    (void)volume;
    return 0;
}

static int adev_get_master_volume(struct audio_hw_device *dev, float *volume)
{
    (void)dev;
    (void)volume;
    return -ENOSYS;
}

static int adev_set_master_mute(struct audio_hw_device *dev, bool muted)
{
    (void)dev;
    (void)muted;
    return -ENOSYS;
}

static int adev_get_master_mute(struct audio_hw_device *dev, bool *muted)
{
    (void)dev;
    (void)muted;
    return -ENOSYS;
}

static int adev_set_mode(struct audio_hw_device *dev, audio_mode_t mode)
{
    (void)dev;
    (void)mode;
    return 0;
}

static int adev_set_mic_mute(struct audio_hw_device *dev, bool state)
{
    (void)dev;
    (void)state;
    return 0;
}

static int adev_get_mic_mute(const struct audio_hw_device *dev, bool *state)
{
    (void)dev;
    if (state)
        *state = false;
    return 0;
}

static size_t adev_get_input_buffer_size(const struct audio_hw_device *dev,
                                         const struct audio_config *config)
{
    (void)dev;
    (void)config;
    return 1024 * sizeof(int16_t);
}

static int adev_dump(const audio_hw_device_t *device, int fd)
{
    (void)device;
    (void)fd;
    return 0;
}

static int adev_close(hw_device_t *device)
{
    struct maple_audio_device *adev = (struct maple_audio_device *)device;
    if (adev->mixer)
        mixer_close(adev->mixer);
    free(adev);
    return 0;
}

static int adev_open(const hw_module_t *module, const char *name,
                     hw_device_t **device)
{
    struct maple_audio_device *adev;
    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0)
        return -EINVAL;
    adev = calloc(1, sizeof(*adev));
    if (!adev)
        return -ENOMEM;
    pthread_mutex_init(&adev->lock, NULL);
    adev->card = find_card(CARD_NAME_TASHA);
    adev->card_name = CARD_NAME_TASHA;
    {
        /* find_card returns 0 both for "card 0" and "not found"; with a
         * single tasha card 0 is the right answer either way. Only look
         * for tavil if tasha left no trace in /proc/asound/cards. */
        FILE *f = fopen("/proc/asound/cards", "r");
        char line[128];
        int seen = 0;
        if (f) {
            while (fgets(line, sizeof(line), f)) {
                if (strstr(line, CARD_NAME_TASHA)) {
                    seen = 1;
                    break;
                }
            }
            fclose(f);
        }
        if (!seen) {
            adev->card = find_card(CARD_NAME_TAVIL);
            adev->card_name = CARD_NAME_TAVIL;
        }
    }
    adev->mixer = mixer_open(adev->card);
    ALOGI("audio.primary.maple: card %u mixer %p", adev->card,
          (void *)adev->mixer);
    adev->device.common.tag = HARDWARE_DEVICE_TAG;
    adev->device.common.version = AUDIO_DEVICE_API_VERSION_2_0;
    adev->device.common.module = (struct hw_module_t *)module;
    adev->device.common.close = adev_close;
    adev->device.init_check = adev_init_check;
    adev->device.set_voice_volume = adev_set_voice_volume;
    adev->device.set_master_volume = adev_set_master_volume;
    adev->device.get_master_volume = adev_get_master_volume;
    adev->device.set_master_mute = adev_set_master_mute;
    adev->device.get_master_mute = adev_get_master_mute;
    adev->device.set_mode = adev_set_mode;
    adev->device.set_mic_mute = adev_set_mic_mute;
    adev->device.get_mic_mute = adev_get_mic_mute;
    adev->device.set_parameters = adev_set_parameters;
    adev->device.get_parameters = adev_get_parameters;
    adev->device.get_input_buffer_size = adev_get_input_buffer_size;
    adev->device.open_output_stream = adev_open_output_stream;
    adev->device.close_output_stream = adev_close_output_stream;
    adev->device.open_input_stream = adev_open_input_stream;
    adev->device.close_input_stream = adev_close_input_stream;
    adev->device.dump = adev_dump;
    *device = &adev->device.common;
    return 0;
}

static struct hw_module_methods_t hal_module_methods = {
    .open = adev_open,
};

struct audio_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .version_major = 1,
        .version_minor = 0,
        .id = AUDIO_HARDWARE_MODULE_ID,
        .name = "maple tinyalsa audio HAL",
        .author = "retro-android-ports",
        .methods = &hal_module_methods,
    },
};
