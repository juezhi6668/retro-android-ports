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
