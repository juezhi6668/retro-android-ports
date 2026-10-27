/* time-sync — set the wall clock from the network, then save the offset to the PMIC
 * RTC so init's rtc-restore rebuilds it on the next boot.
 *
 * Why it is needed: the 4.4 kernel here has no android_alarm driver, and the sagit DT
 * gives the PM8998 RTC no allow-set-time, so the RTC only ever counts seconds from
 * whatever it last lost power at (measured 2026-10-27: 1971-02-14). Stock 4.4's
 * SystemClock.setCurrentTimeMillis() sets the clock *through /dev/alarm* only, so on a
 * prebuilt ROM like MIUI V5 -- which carries that stock framework JNI, not this port's
 * patched one -- every attempt dies with "Unable to open alarm driver: No such file or
 * directory" from system_server and the clock never moves.
 *
 * So this runs as root from init, takes a plausible time from the network, calls
 * settimeofday() itself, and writes (wall time - /sys/class/rtc/rtc0/since_epoch) to
 * /data/system/time/rtc_offset -- the same file
 * patches/kitkat/frameworks-base-time-without-dev-alarm.patch writes on the built
 * images, and the file /system/bin/rtc-restore applies at post-fs-data.
 *
 * UDP/123 does not get through this network (measured: time.android.com,
 * ntp.aliyun.com, cn/2.cn/pool/debian.pool.ntp.org and time.windows.com all time out),
 * so an HTTP Date header over TCP/80 is the fallback. One-second granularity is enough
 * for a wall clock.
 */
#define LOG_TAG "time-sync"

#include <cutils/log.h>

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define OFFSET_DIR "/data/system/time"
#define OFFSET_FILE OFFSET_DIR "/rtc_offset"
#define OFFSET_TMP OFFSET_FILE ".tmp"
#define RTC_EPOCH "/sys/class/rtc/rtc0/since_epoch"

/* A server answer outside this window is a broken reply, not a broken clock. */
#define EPOCH_MIN 1577836800LL   /* 2020-01-01 */
#define EPOCH_MAX 4102444800LL   /* 2100-01-01 */

#define NTP_TO_UNIX 2208988800LL /* 1900-01-01 -> 1970-01-01 in seconds */

static const char *const kNtp[] = {
    "ntp.aliyun.com", "cn.pool.ntp.org", "2.cn.pool.ntp.org",
    "time.android.com", "pool.ntp.org", "time.windows.com", NULL
};

static const char *const kHttp[] = {
    "www.baidu.com", "www.taobao.com", "www.qq.com", "www.apple.com", NULL
};

static const char *const kMonths[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static long long read_ll(const char *path)
{
    char buf[32];
    int fd = open(path, O_RDONLY);
    ssize_t n;
    char *end;
    long long v;

    if (fd < 0)
        return -1;
    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0)
        return -1;
    buf[n] = 0;
    v = strtoll(buf, &end, 10);
    return end == buf ? -1 : v;
}

static int plausible(long long epoch)
{
    return epoch >= EPOCH_MIN && epoch < EPOCH_MAX;
}

/* Connected socket to host:service, with both poll timeouts set, so a filtered
 * server costs timeout seconds rather than an uninterruptible wait. */
static int open_socket(const char *host, const char *service, int type, int timeout_s)
{
    struct addrinfo hints, *res, *ai;
    struct timeval tv;
    int fd = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = type;
    if (getaddrinfo(host, service, &hints, &res) != 0)
        return -1;

    tv.tv_sec = timeout_s;
    tv.tv_usec = 0;
    for (ai = res; ai != NULL; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, 0);
        if (fd < 0)
            continue;
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

/* RFC 4330: 48-byte packet, server answer carries its transmit timestamp at 40. */
static long long sntp_query(const char *host)
{
    unsigned char pkt[48];
    unsigned tx_sec;
    ssize_t n;
    int fd;

    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0x1b; /* LI=0, VN=3, mode=3 (client) */

    fd = open_socket(host, "123", SOCK_DGRAM, 4);
    if (fd < 0)
        return -1;

    if (send(fd, pkt, sizeof(pkt), 0) != (ssize_t)sizeof(pkt)) {
        close(fd);
        return -1;
    }
    n = recv(fd, pkt, sizeof(pkt), 0);
    close(fd);
    if (n != (ssize_t)sizeof(pkt))
        return -1;

    /* LI=3 in the reply means the server is unsynchronised (or smearing). */
    if ((pkt[0] >> 6) == 3 || pkt[1] < 1 || pkt[1] > 15)
        return -1;
    memcpy(&tx_sec, &pkt[40], sizeof(tx_sec));
    tx_sec = ntohl(tx_sec);
    if (tx_sec == 0)
        return -1;
    return (long long)tx_sec - NTP_TO_UNIX;
}

/* Days since the Unix epoch from a proleptic-Gregorian civil date (Hinnant's
 * algorithm, so no leap-year table and no dependency on timegm, which KitKat's
 * bionic does not export). */
static long long days_from_civil(long long y, unsigned m, unsigned d)
{
    long long era;
    unsigned yoe, doy, doe;

    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = (unsigned)(y - era * 400);
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long long)doe - 719468;
}

/* HTTP-date is the one thing every proxy and load balancer answers with, and it
 * rides on TCP/80, which is reachable where SNTP is not. */
static long long http_date_query(const char *host)
{
    char req[256];
    char buf[2048];
    char wd[8], mon[8], zone[8];
    char *date = NULL;
    char *p;
    size_t got, room;
    ssize_t r;
    int dm, yy, hh, mm, ss, mo;
    long long epoch = -1;
    int fd;

    snprintf(req, sizeof(req),
             "HEAD / HTTP/1.0\r\nHost: %s\r\nUser-Agent: time-sync\r\nConnection: close\r\n\r\n",
             host);

    fd = open_socket(host, "80", SOCK_STREAM, 5);
    if (fd < 0)
        return -1;

    if (send(fd, req, strlen(req), 0) <= 0) {
        close(fd);
        return -1;
    }

    got = 0;
    for (;;) {
        room = sizeof(buf) - got - 1;
        if (room == 0)
            break;
        r = recv(fd, buf + got, room, 0);
        if (r <= 0)
            break;
        got += (size_t)r;
        buf[got] = 0;
        if (strstr(buf, "\r\n\r\n") != NULL)
            break;
    }
    close(fd);

    for (p = buf; p + 5 <= buf + got; p++) {
        if (strncasecmp(p, "Date:", 5) == 0) {
            date = p + 5;
            break;
        }
    }
    if (date == NULL)
        return -1;

    if (sscanf(date, " %3[A-Za-z], %d %3[A-Za-z] %d %d:%d:%d %3[A-Za-z]",
               wd, &dm, mon, &yy, &hh, &mm, &ss, zone) < 7)
        return -1;
    for (mo = 0; mo < 12; mo++) {
        if (strcasecmp(mon, kMonths[mo]) == 0)
            break;
    }
    if (mo == 12)
        return -1;

    epoch = days_from_civil(yy, (unsigned)mo + 1, (unsigned)dm) * 86400LL
          + hh * 3600 + mm * 60 + ss;
    return plausible(epoch) ? epoch : -1;
}

/* The offset survives because the RTC itself keeps counting; wall time minus
 * since_epoch is the correction rtc-restore re-applies before system_server is up. */
static int persist_offset(long long epoch, long long *offset_out)
{
    long long rtc = read_ll(RTC_EPOCH);
    FILE *f;

    if (rtc < 0) {
        ALOGE("cannot read %s: %s", RTC_EPOCH, strerror(errno));
        return -1;
    }
    /* system:system_server writes this too on the patched images; root:system 0660
     * keeps both paths working. */
    mkdir(OFFSET_DIR, 0770);
    chmod(OFFSET_DIR, 0770);
    chown(OFFSET_DIR, 0, 1000);

    unlink(OFFSET_TMP);
    f = fopen(OFFSET_TMP, "w");
    if (f == NULL) {
        ALOGE("%s: %s", OFFSET_TMP, strerror(errno));
        return -1;
    }
    fprintf(f, "%lld\n", epoch - rtc);
    if (fclose(f) != 0) {
        ALOGE("closing %s: %s", OFFSET_TMP, strerror(errno));
        return -1;
    }
    chmod(OFFSET_TMP, 0660);
    chown(OFFSET_TMP, 0, 1000);
    if (rename(OFFSET_TMP, OFFSET_FILE) < 0) {
        ALOGE("rename %s: %s", OFFSET_FILE, strerror(errno));
        return -1;
    }
    *offset_out = epoch - rtc;
    return 0;
}

static int apply_time(long long epoch, const char *source)
{
    struct timeval tv;
    time_t sec = (time_t)epoch;
    char stamp[32];
    long long offset = 0;

    tv.tv_sec = sec;
    tv.tv_usec = 0;
    if (settimeofday(&tv, NULL) < 0) {
        ALOGE("settimeofday(%lld): %s", epoch, strerror(errno));
        return 1;
    }
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S UTC", gmtime(&sec));
    /* Repeat it on stdout: a prebuilt ROM like MIUI V5 has no logd, and this binary's
     * own ALOG never reaches kmsg either, so /sbin/time-sync.sh is what records the
     * answer (it appends the tool's stdout to its log file). */
    if (persist_offset(epoch, &offset) == 0) {
        ALOGI("clock set to %s from %s, rtc offset %lld saved", stamp, source, offset);
        printf("clock set to %s from %s, rtc offset %lld saved\n", stamp, source, offset);
    } else {
        ALOGI("clock set to %s from %s, but the rtc offset could not be saved", stamp, source);
        printf("clock set to %s from %s, but the rtc offset could not be saved\n",
               stamp, source);
    }
    return 0;
}

int main(int argc, char **argv)
{
    long long epoch;
    const char *const *server;
    int http_only = 0;

    if (argc > 1 && strcmp(argv[1], "--set") == 0) {
        if (argc < 3) {
            fprintf(stderr, "usage: time-sync [--set <epoch-seconds>] [--http]\n");
            return 1;
        }
        epoch = strtoll(argv[2], NULL, 10);
        if (!plausible(epoch)) {
            fprintf(stderr, "time-sync: %lld is outside the 2020..2100 window\n", epoch);
            return 1;
        }
        return apply_time(epoch, "the command line");
    }
    /* --http skips SNTP, so the TCP/80 fallback can be checked on a network where
     * UDP/123 works and would otherwise always win. */
    if (argc > 1 && strcmp(argv[1], "--http") == 0)
        http_only = 1;
    else if (argc > 1 && strcmp(argv[1], "sync") != 0) {
        fprintf(stderr, "usage: time-sync [--set <epoch-seconds>] [--http]\n");
        return 1;
    }

    if (!http_only) {
        for (server = kNtp; *server != NULL; server++) {
            epoch = sntp_query(*server);
            if (plausible(epoch))
                return apply_time(epoch, *server);
        }
    }
    for (server = kHttp; *server != NULL; server++) {
        epoch = http_date_query(*server);
        if (plausible(epoch))
            return apply_time(epoch, *server);
    }

    ALOGE("no time server answered (SNTP then HTTP over TCP/80), clock left alone");
    fprintf(stderr, "no time server answered (SNTP then HTTP over TCP/80)\n");
    return 1;
}
