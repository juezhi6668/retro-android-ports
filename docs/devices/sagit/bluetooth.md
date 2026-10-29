# Bluetooth on sagit/chiron (WCN3990 "cherokee", msm8998)

Shared implementation: `devices/sagit/kitkat/bt/` builds module
`libbt-vendor` and is in `PRODUCT_PACKAGES` for both sagit and chiron. There is
no per-device Bluetooth variant — the two phones use the same chip, the same
UART and the same firmware.

## Who does what

The booted kernel (`~/kernel-22.2-msm8998`) only supplies **power**.
`drivers/bluetooth/bluetooth-power.c` registers `/dev/btpower` and an rfkill
device; the ioctl powers the rails and toggles the clock. Everything else —
H4/UART transport, EDL, baud change, rampatch + NVM TLV download, HCI reset —
is userspace, in the ported `libbt-vendor` (`bt_vendor_msm8998.c` →
`hci_uart.c` → `hw_rome.c`), which runs inside bluedroid's `bluetooth` process.

- HCI transport: `/dev/ttyHS0` = `uart@c171000`, `qcom,msm-hsuart-v14` (BLSP1
  high-speed UART). Pinctrl `blsp_uart3_a`: GPIO 45 TX, 46 RX (+wakeup), 47
  CTS, 48 RTS — **48 is BT_WAKE**, so `USERIAL_OP_FLOW_ON/OFF` in this HAL is a
  BT_WAKE pulse, not a flow-control change (it only pokes `TIOCM_RTS`; the
  `CRTSCTS` termios flag is never cleared).
- WLAN is the PCIe side of the same chip (`18800000.qcom,icnss`), see
  [wifi-progress](../chiron/wifi-progress.md).

Chip identity as read live: `compatible = qca,wcn3990`, ROM build `0x0201`, SOC
`0x40010214`, Chip ID `0x30214`, patch `crbtfw21.tlv` (122 456 B, 503 segments),
NVM `crnv21.bin`, and after a successful download the controller reports
`BTFM.CHE.2.1.1-00283-QCACHROM-1`.

## Two kernel-side facts that shape every test

**1. Nothing but a real power-off puts the chip back into ROM mode.**
`/sys/firmware/devicetree/base/bt_wcn3990` has `clock-names = rf_clk2`, the six
`qca,bt-vdd-*`/`qca,bt-chip-pwd` supplies and **no `qca,bt-reset-gpio`** (no
msm8998 DTS entry defines one either). `bt_power_populate_dt_pinfo()` therefore
leaves `bt_gpio_sys_rst < 0`, `bluetooth_power()` skips `bt_configure_gpios()`,
and no code path ever pulses the chip's reset — only rails. Measured
(2026-10-28): a warm reboot, `adb reboot bootloader` + `fastboot reboot`, and an
explicit six-rail off/on cycle driven through rfkill all leave the controller
silent at every baud; only a PMIC power-down restores EDL. **A bring-up test
must be a cold boot** — hold the power button to power off (or `reboot -p`),
then power on.

**2. `/dev/btpower` power-ON returns `-EINVAL` once the clock is flagged
enabled, and the failure tears the rails back down.** `bt_clk_enable()` returns
`-EINVAL` when `!clk->clk || clk->is_enabled`, and `bluetooth_power()`'s
`clk_fail:` label sits *inside* the power-off disable chain, so a failed clock
enable switches off all six supplies and the ioctl returns `-EINVAL` — with the
misleading log `BT_PWR_ERR("bt_power gpio config failed")`. The clock gets
desynchronised by the rfkill `.set_block` path (which bypasses `bt_ioctl`'s
`pwr_state` cache) and by a cache-skipped power-off. Consequences:
any *post-boot* enable attempt dies before the first EDL command, and
`bt/tools/btprobe.c`'s `do_matrix()` — which calls `bt_power(0)` then
`bt_power(1)` before every combination — measures an unpowered chip. Treat its
matrix output as void; `do_single ... 0` (cycle disabled) is the only honest
form once power is already up.

Also measured: on a cold boot the *first* enable attempt times out (at ~30 s)
and the stack's retry ~6 s later gets an EDL answer within 6 ms. The chip needs
seconds after POR; bluedroid's ~3 s retry loop already covers that, so no fix is
wanted there.

## Bring-up state

Sequence reached, from the cold-boot traces of 2026-10-28
(`C:\tmp\qmx\bt_cold.txt`, `bt_c4.txt`, `bt_c5.txt`):

```
EDL version req → baud change to 3 M → rampatch+NVM TLV (503 seg) → build info
→ HCI Reset          <-- the only failing step
```

Everything through the build-info read is answered at 3 Mbaud, in ~0.9 s total.
The reset is not, and two runs of measurement say why the obvious explanations
are wrong:

**Run 1 — rate sweep (`bt_cold.txt`).** One `HCI_RESET` write at `t=37.823`,
then a window per rate, each preceded by a resend:

| listen rate | bytes | content |
|---|---|---|
| 3 M | 4 | `c0 20 f8 fe` (170 ms after the write) |
| 115200, 921600 | 0 | |
| 1 M | 31 | `00 a6 c0 80 01 92 01 48 00 d2 59 92 92 b2 b2 b2 b2 92 92 b2 b2 b2 b2 b2 b2 0b af 06 84 e4 ff` |
| 2 M, 3 M | 0 | |

That looked like "a real reply sampled three times too slowly" — 31 bytes at 1 M
≈ 10 bytes at 3 M, a `92/b2` run is what a periodic pattern becomes under clock
drift, and the trailing `ff` is the line returning to idle. It was not: the
sweep resends the command at every rate and flushed before each attempt, so
nothing in this table is attributable.

**Run 2 — one rate, three windows, with a control (`bt_c4/c5.txt`).** All at 3
Mbaud, `HCI RESET` written at `t=37.947`, no flushes:

```
39.716  3M after wake+reset : rx 4 byte(s) [c0 20 f8 fe]
41.319  reset, no wake      : rx 0 byte(s)
42.120  nothing sent        : rx 0 byte(s)     <-- control
42.120  EDL_GET_BUILD_INFO  sent, no answer
45.524  bt_powerup: off                        <-- framework teardown
45.810  uart_read: read failed: Bad file number(9)
```

Which establishes:

- **The control window receives nothing**, so the bytes in run 1 were not wake
  -edge artefacts on the RX line — they are a response to the command.
- **After one reset the controller stops transmitting entirely.** A second HCI
  Reset gets nothing, and the EDL build-info command — answered in ~1 ms six
  hundred milliseconds earlier — gets nothing for the remaining 3.4 s. The reset
  does take effect (EDL falls silent) and HCI does not take its place.
- **The write path is fine**: the build-info command left without blocking, so
  the chip is not holding us off with flow control; the silence is receive-only.
- **`/dev/btpower` teardown lands at 45.5 s**: the framework gives the tail
  ~7.5 s (measured 37.9 → 45.5) before it closes the UART, after which every
  read is `EBADF` and every log line about "no answer" is meaningless. Any
  bring-up retry has to fit in that.

So the surviving hypothesis was that the patched firmware comes back on the **ROM
default rate** after the reset. Run 3 tested exactly that and nothing else: one
reset written at 3 Mbaud, a 1.2 s window there, then a move to 115200 and 4.2 s of
pure listening with no further command.

**Run 3 — one reset, then only listen (`bt_c7.txt`, `781c6cd…`'s predecessor).**

```
37.7758  TLV (patch + NVM) done, 3M
37.7758  EDL_GET_BUILD_INFO sent          → 37.7769 answered (1.1 ms)
37.7770  rome_hci_reset: HCI RESET, baud 14, Flow Off 37.7771 → Flow On 37.7974
38.7442  3M after reset       : rx 4 byte(s) [c0 20 f8 fe]
42.9550  115200 after reset   : rx 0 byte(s) []
42.9551  rome_hci_reset: HCI Reset got no Command Complete
42.9560  bt_powerup: off
```

That kills the rate hypothesis too: 4.2 s of listening at 115200 with the UART
otherwise idle received **nothing**, so the chip is not answering at the ROM
default rate either.

What is left is in the four bytes. Bit 0 is clear in each of them and the low run
shortens across the sequence (`c0` 6 lows, `20` 5, `f8` 3, `fe` 1) — that is what a
receiver reports while the far end drops its TX line and re-acquires it, i.e. the
chip *did* take the reset and restarted its own UART. The Command Complete that
should follow never appears, at any rate. Untested therefore, and now what the
next run measures:

- A **second** reset, written only after the line has settled. Every attempt so
  far pulsed RTS 100 ms before writing and then wrote exactly once, so a command
  landing during the chip's UART restart has never been followed up.
- Whether the ROM is still there afterwards: the tail now re-sends
  `EDL_GET_BUILD_INFO` (raw write + the same collector, not the vendor helper with
  its extra 3 s Command-Complete wait) and the answer distinguishes "never left
  ROM mode, reset did not land" from "running the patch, deaf to everything".
- `c_cflag` straight off the port, because `userial_vendor_open()` sets
  `CLOCAL | CRTSCTS` and `CRTSCTS` makes the kernel drive RTS — the chip's wake
  input — from its own RX-buffer state, overriding the manual `TIOCMSET` the flow
  calls do. `TIOCMGET` on this driver is a constant, so only the `c_cflag` half of
  that line is evidence.

Budget: 2500 + 250 + 1000 ms of reset windows + a 600 ms probe = 4.35 s of the
7.5 s the framework gives before it closes the UART. Note that `0 bytes` at a
*slower* rate than the chip is not evidence of silence — a 3 Mbaud start bit is
0.33 µs, well inside the noise floor of a receiver sampling at 115200 — which is
why run 3 only rules the default rate out and not a slow answer at 3 M.

## Solved — the tail was three separate causes (2026-10-29)

Bluetooth now turns on and stays on through repeated off/on cycles. It took
three independent fixes, each of which was visible only after the previous one
was in:

**1. The controller was putting the *host* to sleep.** `crnv21.bin` ships NVM tag
17 (`EDL_TAG_ID_HCI`) with byte 0 bit 7 set and tag 27
(`EDL_TAG_ID_DEEP_SLEEP`) with byte 0 bit 0 set — controller-side in-band HCI_IBS
and deep sleep. Both need a partner that speaks the `0xfd`/`0xfc`/`0xfe` protocol
of `drivers/bluetooth/hci_qca.c` or a `bt_wake` GPIO; KitKat's H4 read thread has
neither. The chip then parked its UART right after coming up, and the port saw
2445 wake-indication bytes and `0x200b` command timeouts. `rome_get_tlv_file()`
now clears both bits before the NVM is sent (masks `HCI_IBS_ENABLE_MASK`,
`HCI_DEEP_SLEEP_ENABLE_MASK` in `hw_rome.h`). Effect: 2445 lines → 0.

**2. A warm enable spent exactly all of bluedroid's patience.** `bt_powerup(off)`
does not reliably cut the rail (fact 1 above), so from the second enable onwards
the chip still *ran its patch* and answered no EDL command at all.
`rome_patch_ver_req()` waited out its full 3 s `HCI_RESPONSE_TIMEOUT_MS` — and
3 s is precisely `PRELOAD_START_TIMEOUT_MS` with `PRELOAD_MAX_RETRY_ATTEMPTS 0`
(`external/bluetooth/bluedroid/main/bte_main.c:338`), so `preload_wait_timeout`
fired `BT_EVT_HARDWARE_INIT_FAIL` *before* the HAL finished and the enable was
torn down even though the chip was healthy. The teardown then took a SIGSEGV in
`userial_read`, the crash closed the UART while `msm_serial_hs` still held a
client vote (`msm_hs_shutdown: Client vote on, forcing to 0`) and every later
enable in that boot read silence at both rates. That is the whole of "it turns on
once or twice, then never again until a reboot".

`rome_soc_init()` therefore now asks the question only a *patched* controller can
answer, and asks it first: `rome_chip_already_patched()` writes an HCI Reset at
3 Mbps, which a patched chip completes in 7.5 ms, and hands over without touching
the download. Only when nothing answers does it drop to 115200 and talk to the
boot ROM.

**3. The controller's stale queue masked the reply.** The first version of that
probe read back 96 bytes for its own reset — the Command Complete followed by all
the ACL and event traffic the chip had accumulated while no host was reading —
and 96 more bytes for the reset that was supposed to confirm it. The real answer
was behind the buffer, the probe concluded "no patch", and the EDL download it
fell through to can never succeed on a patched chip. `rome_drain_backlog()` now
discards input until the line has been quiet for 20 ms, and the reset that is
judged is written after that, so its reply is the only thing that can be in the
window.

Measured with all three in place (chiron, `C:\tmp\qmx\warm3.txt`..`cold1.txt`):

| case | what the HAL logged | result |
|---|---|---|
| enable on a patched chip | `warm reset: rx 7 byte(s) [04 0e 04 01 03 0c 00]` | ON 36–52 ms after the port opens, 5/5 across 1–6 s gaps and one reboot |
| enable on a patched chip with backlog | same 7 bytes, no 96-byte dump | ON |
| cold download (pre-fix build) | `open 39.591 → HCI Reset is done 41.014` | 1.42 s of the 3 s budget |

The warm path is the *normal* path: the patch survived a 5.9 s power-off and a
4 s rfkill-driven rail drop, so only a real power cycle restores ROM mode and
exercises the download. That cold case is untested on the final build — it only
differs by ≤220 ms of pre-check against a silent line.

`CONFIG_RT_GROUP_SCHED=y` in the booted kernel was checked and is **not**
involved: every thread of `com.android.bluetooth` is `policy=0` (`SCHED_OTHER`)
while the adapter is on, the process sits in `/dev/cpuctl/apps` whose
`cpu.rt_runtime_us` is 800000 (root 950000), and bluedroid's one RT request
(`gki/ulinux/gki_ulinux.c:742`, the GKI timer) never appears in the log at all.

## Ruled out

- `disable_internal_ldo` (`01 0c fc 01 32`) — guarded by `q.enable_extldo`,
  which is false on sagit (no "external LDO configured" in the log). The command
  is never sent, so it cannot explain the tail.
- Modem-line readings as liveness: `msm_serial_hs` reports CTS/DSR/RI/CD as
  driver constants (`0x166`), not wire state.
- `/proc/tty/driver/msm_serial_hs` `rx:` as evidence of reception — it is not
  incremented on the SPS/BAM path.
- CAF-vs-port gaps: the port's `rome_soc_init()` matches the upstream
  `ROME_VER_2_1` path (baud → TLV → build info → LDO → reset). There is no
  SW-flow-control or event-config step in this HAL generation to have dropped.
- A sleeping chip: `USERIAL_OP_FLOW_OFF/ON` here only drives `TIOCM_RTS` (the
  wake line) — `CRTSCTS` is never cleared — and pulsing wake before every window
  changed nothing except the first `c0 20 f8 fe`.
- The patched firmware answering at the **ROM default rate**: 4.2 s of listening
  at 115200 after a single 3 Mbaud reset received 0 bytes (run 3).
- A missing step in the port. The booted Lineage 22.2 kernel has no `rome.c`; its
  ROME bring-up is `drivers/bluetooth/btqca.c:373 qca_uart_setup_rome()` — version
  request, patch TLV, `msleep(10)`, NVM TLV, `rome_reset()` — which is the same
  sequence the HAL runs, and `hci_qca.c:905 qca_setup()` does nothing before it
  except the baud change (`qca_set_baudrate()`, then a 300 ms settle, then the
  local `hci_uart_set_baudrate()`). Nothing there is absent from the port.


## KitKat traps on this device

- No logd: the HAL's `ALOG*` output lands in **kmsg**, and the ring wraps in
  ~30 s under RILQ spam. Capture `dmesg` promptly; the newest lines survive.
- Toolbox lacks `head`, `tail`, `sed`, `wc`, `tr`, `md5sum`, `hexdump`. Dump to
  `/data/local/tmp` and strip on the host; `adb` never runs from WSL.
- `/system` is re-mounted read-only at every boot — `mount -o remount,rw /system`
  immediately before each `adb push`.
- BT auto-enables at `t≈30 s` and the stack retries the enable every ~3-6 s.
