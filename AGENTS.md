# AGENTS.md

## Local environment overrides (untracked, optional)

- If `AGENTS.local.md` exists alongside this file, read it — it holds per-instance private details (build-server host/user/password, local paths) and overrides the generic docs. It is gitignored and absent in fresh clones, so never require it and never commit it.

## Docs are part of the port

Update relevant documentation in the same change as any porting work.
If you hit a quirk (build failure, boot stall, silent misbehavior, flash/packaging gotcha, tool lies to you), document it — do not leave it as chat-only knowledge.

Flush pending doc updates before any context compaction: a compacted session must never be the only place a quirk is recorded. If compaction is imminent and docs are stale, update them first.

Where it goes:

- Generic lesson, reusable across devices of that release: `docs/advanced/porting-notes/<release>.md` (Donut: `donut.md`, KitKat: `kitkat.md`). A new release gets a new file, registered in `docs/advanced/README.md`.
- Build recipe (source fetch, patch order, packaging, flashing), per release: `docs/advanced/build/<release>.md`. Build-host/container recipe change: `docs/advanced/build-server.md` or `containers/<release>/`.
- Project-made software/driver write-up: `docs/advanced/software/<tool>.md` (vtouch, libhyb, mkbootimg-cooper). New tool ⇒ new page linked from `docs/advanced/README.md`.
- Hardware-specific fact, address, partition layout, verified fix, open gap: `docs/devices/<codename>/quirks.md`.
- Behavior/status change (new boot state, working/broken feature): the device main page `docs/devices/<codename>/README.md`, the README Ports table, and the Roadmap in `README.md`.
- Patch-set change: `patches/<release>/README.md` alongside the diff under `patches/<release>/` (+ `files/` overlays).
- Adoption notes that map another port's document/solutions onto ours (poplar/CHARQUIRKS-class bundles): reference pages under `docs/advanced/` (e.g. `charquirks-mapping.md`).

New device ⇒ `docs/devices/<codename>/README.md` (photo, description, per-release status matrix, links) + `docs/devices/<codename>/quirks.md`, one row in the README device table, and the "Ported to" column of the release's porting-notes table in `docs/advanced/README.md`.

Unsure whether generic or device-specific? Put it in the device quirks page and link it from the playbook, not the reverse.

## Match existing doc format

Follow the shape of the file you are editing:

- Titles: device main page `# <Full device name> (<codename>)`; device quirks `# <codename> quirks (<full device name>)`; playbooks `# Porting notes: <release> on newer hardware`; build/software pages `# <what it is>`. Then 2-4 lines of scope: what release/device/tool this covers and links to the sibling docs (device pages → quirks + playbook + build guide; playbooks → build guide + device quirks + advanced index). Hub pages are `README.md` so a directory browse on GitHub renders them: `docs/advanced/README.md`, `docs/devices/<codename>/README.md`.
- `##` sections by subsystem (`Source & toolchain`, `Platform specifics`, `Field lessons`, etc.). Add a new section only if nothing fits.
- Bullets with a **bold lead** (`- **Sparse images bite twice.** ...`), then symptom → root cause → fix, with exact file paths (`system/core/init/builtins.c`, `devices/maple/kitkat/...`), exact error strings / ioctl numbers / log lines, and the command or patch that fixed it.
- Verification, not assertion: state how it was confirmed (on-device date like `verified 2026-10-15`, `TEST N` sequence for maple, hash readback after flash, `dmesg`/fb0-decode evidence). Mark untested predictions as such (`TODO`, `verify-me`, `to fill in once built`) — see `docs/advanced/porting-notes/kitkat.md` Container gotchas and `docs/devices/maple/quirks.md` blockers.
- Record sibling-device divergences explicitly when they conflict (e.g. `FBIOPAN_DISPLAY` vs `FBIOPUT_VSCREENINFO` on msm8998 ports): which device each was verified on, and re-verify on the target — do not generalize from one device.
- Keep cross-links relative (`[playbook](../porting-notes/donut.md)`, `[quirks](quirks.md)`) and keep the README Ports table, `docs/advanced/README.md` indexes, and each device page's links/status consistent if you add a doc, device, or release.

No drive-by reformatting. Keep line length, bullet style, and section order of the file you touch.
