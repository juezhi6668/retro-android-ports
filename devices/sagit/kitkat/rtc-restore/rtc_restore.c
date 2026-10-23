/* rtc-restore: set the wall clock at boot from the PM8998 RTC (read-only, it counts seconds
 * from 1970 and refuses RTC_SET_TIME — the sagit DT has no allow-set-time) plus the offset
 * system_server saved in /data/system/time/rtc_offset when it last set the time (see
 * patches/kitkat/frameworks-base-time-without-dev-alarm.patch). This kernel has no /dev/alarm
 * either, so nothing else restores the clock across reboots.
 * Runs once from init at post-fs-data (service rtc-restore in init.sagit.rc); it is a no-op
 * until the offset file exists, and logs what it did. */
#define LOG_TAG "rtc-restore"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <cutils/log.h>

static long long read_ll(const char *path)
{
    char buf[32];
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0)
        return -1;
    buf[n] = 0;
    char *end;
    long long v = strtoll(buf, &end, 10);
    return end == buf ? -1 : v;
}

int main(void)
{
    long long offset = read_ll("/data/system/time/rtc_offset");
    if (offset < 0) {
        ALOGI("no saved rtc offset yet, leaving the clock to the kernel/RTC");
        return 0;
    }
    long long rtc = read_ll("/sys/class/rtc/rtc0/since_epoch");
    if (rtc < 0) {
        ALOGE("no rtc0 since_epoch: %s", strerror(errno));
        return 1;
    }
    struct timeval tv = { (time_t)(rtc + offset), 0 };
    if (settimeofday(&tv, NULL) < 0) {
        ALOGE("settimeofday(%lld): %s", (long long)tv.tv_sec, strerror(errno));
        return 1;
    }
    ALOGI("clock set to %lld (rtc %lld + offset %lld)", (long long)tv.tv_sec, rtc, offset);
    return 0;
}
