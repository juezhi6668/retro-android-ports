/* lights HAL for Xiaomi Mi 6 (sagit) - KitKat.
 *
 * Backlight: msm8998 kernels expose the panel backlight as
 * /sys/class/leds/lcd-backlight (what AOSP HALs use). Mi 6 drives it through
 * the PMIC WLED; some Xiaomi kernels also export /sys/class/leds/wled, so the
 * same scaled value is written to both — whichever is wired to the panel
 * takes effect, the other is inert (maple verified the two are independent).
 *
 * Notification: single white LED at /sys/class/leds/white (qpnp). KitKat's
 * LightsService sends notifications + battery through LIGHT_ID_NOTIFICATIONS /
 * LIGHT_ID_BATTERY; both map to the same LED. Blink is used when the caller
 * asks for it; otherwise a steady brightness is written.
 * verify-me: `ls /sys/class/leds` on the phone; rename WHITE_* if the node is
 * named differently.
 */
#define LOG_TAG "lights.sagit"

#include <cutils/log.h>
#include <cutils/properties.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <hardware/lights.h>

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static const char *const LCD_BRIGHTNESS   = "/sys/class/leds/lcd-backlight/brightness";
static const char *const LCD_MAX          = "/sys/class/leds/lcd-backlight/max_brightness";
static const char *const WLED_BRIGHTNESS  = "/sys/class/leds/wled/brightness";
static const char *const WLED_MAX         = "/sys/class/leds/wled/max_brightness";
static const char *const WHITE_BRIGHTNESS = "/sys/class/leds/white/brightness";
static const char *const WHITE_MAX        = "/sys/class/leds/white/max_brightness";
static const char *const WHITE_BLINK      = "/sys/class/leds/white/blink";

static int read_int(const char *path)
{
    int fd = open(path, O_RDONLY);
    char buf[16];
    int n;
    if (fd < 0) return -1;
    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';
    return atoi(buf);
}

static int write_int(const char *path, int value)
{
    int fd = open(path, O_WRONLY);
    char buf[16];
    int n;
    if (fd < 0) return -errno;
    n = snprintf(buf, sizeof(buf), "%d\n", value);
    if (write(fd, buf, n) != n) { close(fd); return -EIO; }
    close(fd);
    return 0;
}

static int rgb_to_brightness(const struct light_state_t *state)
{
    unsigned int color = state->color & 0x00ffffff;
    return ((77 * ((color >> 16) & 0xff)) +
            (150 * ((color >> 8) & 0xff)) +
            (29  * (color & 0xff))) >> 8;
}

static int scale_to(const char *max_path, int v)
{
    int max = read_int(max_path);
    int scaled;
    if (max <= 0) max = 4095;
    scaled = (v * max) / 255;
    if (v > 0 && scaled < 1) scaled = 1;
    return scaled;
}

static int set_light_backlight(struct light_device_t *dev,
                               const struct light_state_t *state)
{
    int v = rgb_to_brightness(state);
    (void)dev;
    pthread_mutex_lock(&g_lock);
    write_int(LCD_BRIGHTNESS, scale_to(LCD_MAX, v));
    write_int(WLED_BRIGHTNESS, scale_to(WLED_MAX, v));
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static int set_light_notification(struct light_device_t *dev,
                                  const struct light_state_t *state)
{
    int v = rgb_to_brightness(state);
    (void)dev;
    pthread_mutex_lock(&g_lock);
    if (v == 0) {
        write_int(WHITE_BRIGHTNESS, 0);
    } else if (state->flashMode != LIGHT_FLASH_NONE &&
               (state->flashOnMS > 0 || state->flashOffMS > 0)) {
        /* qpnp blink takes (on,off) in ms through one node */
        char buf[32];
        int fd = open(WHITE_BLINK, O_WRONLY);
        int n = snprintf(buf, sizeof(buf), "%d %d\n",
                         state->flashOnMS, state->flashOffMS);
        if (fd >= 0) {
            if (write(fd, buf, n) != n) { /* fall through to steady */ }
            close(fd);
        }
        write_int(WHITE_BRIGHTNESS, scale_to(WHITE_MAX, 255));
    } else {
        write_int(WHITE_BRIGHTNESS, scale_to(WHITE_MAX, v));
    }
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static int set_light_buttons(struct light_device_t *dev,
                             const struct light_state_t *state)
{
    (void)dev; (void)state;
    return 0;
}

static int close_lights(struct light_device_t *dev)
{
    if (dev) free(dev);
    return 0;
}

static int open_lights(const struct hw_module_t *module, const char *name,
                       struct hw_device_t **device)
{
    int (*set_light)(struct light_device_t *, const struct light_state_t *);
    struct light_device_t *dev;

    if (!strcmp(LIGHT_ID_BACKLIGHT, name))
        set_light = set_light_backlight;
    else if (!strcmp(LIGHT_ID_NOTIFICATIONS, name) ||
             !strcmp(LIGHT_ID_BATTERY, name))
        set_light = set_light_notification;
    else if (!strcmp(LIGHT_ID_BUTTONS, name))
        set_light = set_light_buttons;
    else
        return -EINVAL;

    dev = malloc(sizeof(*dev));
    if (!dev) return -ENOMEM;
    memset(dev, 0, sizeof(*dev));
    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = 0;
    dev->common.module = (struct hw_module_t *)module;
    dev->common.close = (int (*)(struct hw_device_t *))close_lights;
    dev->set_light = set_light;
    *device = &dev->common;
    return 0;
}

static struct hw_module_methods_t lights_module_methods = {
    .open = open_lights,
};

struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .version_major = 1,
    .version_minor = 0,
    .id = LIGHTS_HARDWARE_MODULE_ID,
    .name = "sagit lights",
    .author = "kitkat-sagit",
    .methods = &lights_module_methods,
};
