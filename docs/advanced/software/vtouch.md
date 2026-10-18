# vtouch — multi-touch to single-touch input proxy

`vtouch` makes a **multi-touch panel work on Android releases that predate
multi-touch input** (we ported it for Donut 1.6). It reads the real touch
event node, creates a single-touch `uinput` device, and relays touches to
it as type-B (slot-based) single-touch events — zero framework changes.

Source: [`devices/cooper/donut/vtouch/`](../../../devices/cooper/donut/vtouch/)
(vtouch.c + Android.mk, static ramdisk binary). Ported from MLXProjects'
Froyo `vtouch` 1.2.0, cooper-adapted.

## Why it is needed

- Cooper's Synaptics RMI4 panel **streams protocol type-A**
  (`SYN_MT_REPORT` groups, X/Y/pressure/width) **but declares no MT axes**
  (`ABS=650000`: HAT bits only). Donut classifies the node as
  keyboard-only and drops every touch event — the screen simply never
  responds. (Verified by pulling `/dev/input/event1` during taps and
  decoding `MT_POSITION_X/Y` 53/54.)
- Donut predates the multi-touch input protocol entirely, so no keylayout
  or framework-side trick recovers the events.

## How it works

- Opens a source node — explicitly, or the first `/dev/input/event*` that
  looks like a multi-touch screen (with single-touch absolute fallback).
- Creates a `uinput` device (`cooper_vtouch`), then relays touches as
  single-touch events: type-A groups collapse to slot 0; ABS ranges fall
  back to the panel's 320×480 HVGA when the driver reports nothing usable;
  `/dev/uinput` (c 10 223) is created if missing.
- Signals: `SIGUSR1` restarts the relay / recreates the virtual device,
  `SIGINT`/`SIGTERM` restores the original node and exits. CLI:
  `vtouch [-r|-q|-h|-v] [source-node]`.

## Cooper integration (working)

- Built as a static binary and shipped in the ramdisk:
  `service vtouch /sbin/vtouch /dev/input/event1` in
  `init.gt-s5830.rc`; the explicit path forces MT mode (auto-detect cannot
  see the missing axes).
- `cooper_vtouch.kl` maps MENU/BACK/HOME/SEARCH (BTN_TOUCH needs no entry —
  Donut consumes it by scancode). Note Donut **aborts a whole `.kl` file**
  on unknown flags: no `VIRTUAL` flag on 1.6, use `WAKE`.
- Broken symlink handling matters: fixes the documented Home-button
  provisioning issue but not keymap loading (see
  [cooper quirks](../../devices/cooper/quirks.md#touchscreen-working)).
- Debug: `/dev/.vtouch.log`, plus EventHub `New device` lines in the main
  log buffer (`classes=0x5` = keyboard + touchscreen).

## Porting to another device

1. Find the source node and confirm the failure mode (raw event decode:
   type-A groups with no MT axes declared, or a panel Donut can't classify).
2. Add the `/dev/uinput` node to your ueventd rules if the kernel has
   `CONFIG_INPUT_UINPUT=y` but no device node (cooper does).
3. Add the service to your init rc with an **explicit** source path if
   auto-detect can't see the caps.
4. Adapt the device name (`cooper_vtouch`), coordinate fallbacks (panel
   resolution) and type-A vs type-B handling in `vtouch.c` — the cooper
   comments mark each adaptation.
5. Ship a `.kl` for the virtual device with a Donut-legal flag set.

## Maple (Sony Xperia XZ Premium) adaptation

- Source: `devices/maple/donut/vtouch/vtouch.c` (built as `maple_vtouch`,
  shipped in the ramdisk at `/sbin/maple_vtouch`, started by `init.maple.rc`
  `service vtouch /sbin/maple_vtouch /dev/input/event7`, disabled + started
  in `init.rc on boot` before `class_start default` so the virtual node
  replaces event7 before system_server's EventHub opens the input devices).
- Version in this port: v1.2.0-maple3. Maple-specific behaviour on top of
  the cooper base:
  - MT axes: clearpad declares only MT axes (ABS_MT_POSITION_X/Y, TOOL_TYPE,
    TRACKING_ID, PRESSURE) with no BTN_TOUCH/ABS_X/ABS_Y/ABS_MT_SLOT and
    streams protocol type-A; groups commit on TRACKING_ID >= 0 (pressure/
    major as fallbacks), lift is an empty group. Source ranges 0..2159 x
    0..3839 (2x the 1080x1920 panel) are copied to the virtual device and
    Donut's framework scales them.
  - The panel needs a userspace kick: `clearpad_probe` parks after i2c probe
    with `post_probe_start sysfs is required to start post probe`; vtouch
    writes `1` to `/sys/devices/virtual/input/clearpad/post_probe_start`
    (via the stable input-class symlink, never `inputN`) after the virtual-
    node substitution and before the relay loop, retrying 1x/s for up to
    60 s (the node appears at ~2.9 s kernel time, after init's `on boot`).
  - Firmware-load stall (v1.2.0-maple3): the post-probe "ensure firmware"
    step runs `request_firmware(touch_module_id_0xd1.img)`; Donut has no
    userspace firmware loader, so each attempt blocks the firmware class'
    60 s fallback timeout and the driver retries 3x — touch only comes
    alive ~184 s after boot. vtouch sets `/sys/class/firmware/timeout` to
    1 first thing in `start_post_probe()`, so the retries fail fast and the
    post probe completes ~4 s in. Verified knob writable (readback 1);
    end-to-end timing verified only after the next flash+reboot (the driver
    refuses to re-run: `already post probed, need reboot`).
  - Verified boot evidence: dmesg `start post probewith post_probe_start
    sysfs` -> `begin 'post probe' session` -> `ensure firmware` -> device id
    `PLG1163-C1` (family 0xd1, fw rev 0x03.02) -> `end post probe ...
    (rc=0)`; `/dev/.vtouch.log` shows `touch panel post probe started
    (try 0)` + `relaying on /dev/input/event7`; `/proc/bus/input/devices`
    shows `maple_vtouch` (input8, Handlers event8->event7) carrying
    BTN_TOUCH + ABS_X/Y and the real `clearpad` (input7) preserved as
    `/dev/input/vtouch_saved`.
