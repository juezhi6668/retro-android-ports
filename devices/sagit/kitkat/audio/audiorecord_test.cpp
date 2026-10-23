/* audiorecord_test — record through AudioFlinger and the sagit HAL.
 *
 * The capture sibling of audiotrack_test: tone_test's "cap" opens tinyalsa
 * itself, so the only way to know the HAL's input stream works (route
 * applied, pcm opened with the framework's config) is to record the way an
 * app does. Raw S16_LE mono is written to a file for the host to analyse
 * (scripts/analyze-rec.py: peak/rms, dominant frequency, stutter
 * sidebands), and a per-second peak/rms is printed so a quiet mic is
 * visible without pulling anything.
 *
 * usage: audiorecord_test [seconds] [rate] [file] [routing]
 *        audiorecord_test 5 48000 /data/local/tmp/rec.raw
 *        audiorecord_test 5 48000 /data/local/tmp/rec.raw 0x80000010
 *   seconds 0 = open/start the stream, then stop (route-only check)
 *   routing is an input audio_devices_t sent through
 *   AudioSystem::setParameters(0, "routing=N") the way AudioPolicyManager
 *   does it: 0x80000004 builtin mic, 0x80000010 wired headset mic.
 */
#include <media/AudioRecord.h>
#include <media/AudioSystem.h>
#include <system/audio.h>
#include <utils/Log.h>
#include <utils/String8.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace android;

int main(int argc, char **argv)
{
    int seconds = argc > 1 ? atoi(argv[1]) : 5;
    uint32_t rate = argc > 2 ? (uint32_t)atoi(argv[2]) : 48000;
    const char *path = argc > 3 ? argv[3] : "/data/local/tmp/rec.raw";
    uint32_t routing = argc > 4 ? (uint32_t)strtoul(argv[4], NULL, 0) : 0;
    size_t minFrames = 0;
    sp<AudioRecord> rec;
    FILE *f = NULL;

    AudioRecord::getMinFrameCount(&minFrames, rate, AUDIO_FORMAT_PCM_16_BIT,
                                  AUDIO_CHANNEL_IN_MONO);
    if ((int)minFrames <= 0)
        minFrames = rate / 10;
    rec = new AudioRecord(AUDIO_SOURCE_MIC, rate, AUDIO_FORMAT_PCM_16_BIT,
                          AUDIO_CHANNEL_IN_MONO, minFrames * 2);
    status_t st = rec->initCheck();
    if (st != NO_ERROR) {
        fprintf(stderr, "AudioRecord init failed: %d\n", st);
        return 1;
    }
    /* After construction: AudioFlinger opens the input with the framework's
     * own routing right then, which would overwrite a device-level routing
     * set before this point. The HAL opens the PCM lazily on the first
     * read, so this still decides the route. */
    if (routing) {
        status_t rs = AudioSystem::setParameters(0, String8::format("routing=%d", (int)routing));
        printf("AudioSystem::setParameters(routing=0x%x) -> %d\n", routing, rs);
    }
    printf("AudioRecord: %d s, %u Hz mono, %d frames/call\n", seconds, rate,
           (int)minFrames);

    if (seconds > 0) {
        f = fopen(path, "wb");
        if (!f) {
            fprintf(stderr, "cannot write %s\n", path);
            return 1;
        }
    }
    rec->start();
    if (seconds > 0) {
        const size_t frames = minFrames;
        int16_t *buf = (int16_t *)malloc(frames * sizeof(int16_t));
        long total = (long)seconds * rate / (long)frames;
        long n;
        long written = 0;
        long sec_frames = 0;
        long sec = 0;
        int peak = 0;
        double sum = 0;
        for (n = 0; n < total; n++) {
            ssize_t got = rec->read(buf, frames * sizeof(int16_t));
            size_t i;
            if (got <= 0) {
                fprintf(stderr, "read failed: %zd\n", got);
                break;
            }
            if (fwrite(buf, 1, got, f) != (size_t)got) {
                fprintf(stderr, "short write\n");
                break;
            }
            written += got;
            /* one level line per second of audio, on the fly */
            for (i = 0; i < (size_t)got / sizeof(int16_t); i++) {
                int v = buf[i];
                int a = v < 0 ? -v : v;
                if (a > peak)
                    peak = a;
                sum += (double)v * v;
                if (++sec_frames >= (long)rate) {
                    printf("t=%lds peak=%d rms=%.1f\n", sec, peak,
                           sqrt(sum / sec_frames));
                    peak = 0;
                    sum = 0;
                    sec_frames = 0;
                    sec++;
                }
            }
        }
        if (sec_frames)
            printf("t=%lds peak=%d rms=%.1f (partial)\n", sec, peak,
                   sqrt(sum / sec_frames));
        free(buf);
        printf("recorded %ld bytes to %s\n", written, path);
    }
    rec->stop();
    if (f)
        fclose(f);
    return 0;
}
