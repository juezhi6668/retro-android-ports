/* lights HAL for Xiaomi msm8998 boards (Mi 6 = sagit, Mi MIX 2 = chiron) - KitKat.
 *
 * Backlight: msm8998 kernels expose the panel backlight as
 * /sys/class/leds/lcd-backlight (what AOSP HALs use). The PMIC WLED is also
 * exported as /sys/class/leds/wled, so the same scaled value is written to
 * both — whichever is wired to the panel takes effect, the other is inert
 * (maple verified the two are independent).
 *
 * Notification: both boards wire the PMI8998 RGB red channel, renamed "white"
 * in the DTS, with qcom,use-blink and an 8-step / 50 ms-ramp LUT — the
 * breathing pattern. The kernel's `blink` attribute is a 1/0 flag (1 = run the
 * DT LUT, 0 = back to steady PWM), not an on/off millisecond pair. Brightness
 * must be written first: qpnp_rgb_set() clears pwm_cfg->blinking, so a
 * brightness write after `blink 1` cancels the pattern. The channel registers
 * with the battery-charging trigger, which owns brightness_set, so it is
 * dropped to "none" before the framework drives the LED.
 *
 * The LED class attributes are root:root 0644; init.<board>.rc chowns
 * white/{brightness,blink,trigger} and the backlight nodes to system.
 * Without that LightsService (system_server, uid system) gets EACCES and the
 * notification light never turns on.
 */
#define LOG_TAG "lights"

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
static const char *const WHITE_TRIGGER    = "/sys/class/leds/white/trigger";

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

static int write_str(const char *path, const char *value)
{
    int fd = open(path, O_WRONLY);
    int n = strlen(value);
    if (fd < 0) return -errno;
    if (write(fd, value, n) != n) { close(fd); return -EIO; }
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
    int blink = v > 0 && (state->flashMode == LIGHT_FLASH_TIMED ||
                          state->flashMode == LIGHT_FLASH_HARDWARE);
    (void)dev;
    ALOGI("notification v=%d flash=%d on=%d off=%d", v, state->flashMode,
          state->flashOnMS, state->flashOffMS);
    pthread_mutex_lock(&g_lock);
    write_str(WHITE_TRIGGER, "none");
    if (v == 0) {
        write_int(WHITE_BLINK, 0);
        write_int(WHITE_BRIGHTNESS, 0);
    } else if (blink) {
        write_int(WHITE_BRIGHTNESS, scale_to(WHITE_MAX, 255));
        write_int(WHITE_BLINK, 1);
    } else {
        write_int(WHITE_BLINK, 0);
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
