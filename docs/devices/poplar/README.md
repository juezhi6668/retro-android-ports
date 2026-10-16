# Sony Xperia XZ1 G8341/G8342 (poplar)

*Photo: TODO — no Commons photo committed yet (`device.jpg` missing).*

The XZ1 is Sony's 2017 compact flagship on Qualcomm's **Snapdragon 835
(MSM8998)**: Kryo (ARM64) + Adreno 540, 4 GB RAM, 64 GB UFS, microSD, a
5.2" 1080x1920 IPS LCD (command-mode panel, density 430 in our port),
USB-C. It shipped with **Oreo 8.0**; `poplar` is the G8341,
`poplar_dsds` the dual-SIM G8342 (plus `poplar-kddi` SOV36). Our port
puts **KitKat 4.4.4** on it — same yoshino/MSM8998 platform as
[maple](../maple/README.md), where the display and GPU work originated.

## Status

### KitKat 4.4.4 — AOSP `android-4.4.4_r2` (KTU84Q) — boots to UI

| Area | State |
| --- | --- |
| Boot | Boots to launcher + nav bar (`sys.boot_completed=1`) at 1080x1920 |
| Launcher / UI | Working |
| Touch | Working (Clearpad, full-range coords, nav bar + gpio keys) |
| Display | Working, command-mode panel, fb0 stride 4352 B (1088 px) |
| GPU | **Hardware GL via [glrenderd](../../advanced/software/glrenderd.md)** (Adreno 540) |
| USB | adb working (configfs gadget) |
| Lights | Brightness + sleep via `lights.poplar` (`lcd-backlight` node only — maple has since gained dual-node; see quirks) |
| WiFi / Audio | Not yet — this tree predates maple TEST 36-37 (no supplicant services, no adsp boot); needs forward-port |
| Cellular / Camera / GPS / BT | Not started |

Details, block map (`sda38`/`sda65`/`sda66`/`sda53`/`sda33`), and verified
fixes: [quirks.md](quirks.md).

**Releases:** poplar images are built locally — no published zips yet; see
[Building KitKat](../../advanced/build/kitkat.md) (maple steps + poplar
deltas in quirks).

## Documentation and links

- [**Device quirks**](quirks.md) — hardware snapshot, flash layout, poplar-vs-maple deltas, init wiring, verified fixes.
- [KitKat 4.4.4 porting notes](../../advanced/porting-notes/kitkat.md) —
  generic playbook.
- [Building KitKat](../../advanced/build/kitkat.md).
- [Device tree](../../../devices/poplar/kitkat/) (`devices/poplar/kitkat/`)
  and [AOSP patches](../../../patches/kitkat/README.md) (`patches/kitkat/`).
- Sibling device: [maple](../maple/README.md). Reference: [poplar GPU fixes](../../advanced/poplar-kitkat-gpu-fixes.md),
  tool: [glrenderd](../../advanced/software/glrenderd.md).
