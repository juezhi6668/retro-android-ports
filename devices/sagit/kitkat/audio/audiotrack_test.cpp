/* audiotrack_test — play a tone through AudioFlinger and the sagit HAL.
 *
 * tone_test talks to tinyalsa directly, which bypasses everything above the
 * kernel. This goes AudioTrack -> AudioFlinger -> audio.primary.msm8998, so it
 * exercises the path apps use: the HAL opens the PCM, sends the ACDB
 * calibration and writes the speaker route. Speaker output is the check that
 * matters; pair it with tone_test's mic capture for an acoustic loopback.
 *
 * usage: audiotrack_test [seconds] [rate] [volume] [routing]
 *        audiotrack_test 0            (silence / route only)
 *        audiotrack_test 3 48000 1 8  (route the output to the headphones)
 *
 * routing is an audio_devices_t value sent through
 * AudioSystem::setParameters(0, "routing=N") exactly like
 * AudioPolicyManager does when a wired headset is plugged in:
 * 2 = speaker, 8 = wired headphone, 4 = wired headset. With no h2w
 * switch in this kernel nothing sends it on its own.
 */
#include <media/AudioSystem.h>
#include <media/AudioTrack.h>
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
    int seconds = argc > 1 ? atoi(argv[1]) : 3;
    uint32_t rate = argc > 2 ? (uint32_t)atoi(argv[2]) : 48000;
    float volume = argc > 3 ? (float)atof(argv[3]) : 1.0f;
    uint32_t routing = argc > 4 ? (uint32_t)strtoul(argv[4], NULL, 0) : 0;
    sp<AudioTrack> track = new AudioTrack(AUDIO_STREAM_MUSIC, rate,
                     AUDIO_FORMAT_PCM_16_BIT, AUDIO_CHANNEL_OUT_STEREO, 0,
                     AUDIO_OUTPUT_FLAG_NONE);
    status_t st = track->initCheck();
    if (st != NO_ERROR) {
        fprintf(stderr, "AudioTrack init failed: %d\n", st);
        return 1;
    }
    /* After construction: openOutput hands the HAL the framework's device
     * set, so a routing set before this would be overwritten. The HAL
     * applies the route when the PCM opens on the first write. */
    if (routing) {
        status_t rs = AudioSystem::setParameters(0, String8::format("routing=%d", (int)routing));
        printf("AudioSystem::setParameters(routing=0x%x) -> %d\n", routing, rs);
    }
    track->setVolume(volume);
    track->start();
    printf("AudioTrack: %d s, %u Hz, volume %.2f\n", seconds, rate, volume);
    if (seconds > 0) {
        const int frames = 480; /* 10 ms at 48 kHz */
        int16_t buf[frames * 2];
        const double step = 2.0 * 3.141592653589793 * 440.0 / rate;
        double phase = 0.0;
        long total = (long)seconds * rate / frames;
        long n;
        for (n = 0; n < total; n++) {
            int i;
            for (i = 0; i < frames; i++) {
                int16_t v = (int16_t)(10000.0 * sin(phase));
                buf[2 * i] = v;
                buf[2 * i + 1] = v;
                phase += step;
                if (phase > 2.0 * 3.141592653589793)
                    phase -= 2.0 * 3.141592653589793;
            }
            track->write(buf, sizeof(buf));
        }
    }
    track->stop();
    printf("done\n");
    return 0;
}
