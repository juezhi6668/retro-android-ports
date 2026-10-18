# Poplar KitKat GPU fixes — `glrenderd` reference (maple / poplar)

Sibling-port reference for the yoshino MSM8998 `glrenderd` work — developed on
`poplar`, adopted by `maple`. **Historical for maple**: on 2026-10-18 the
glrenderd sources, patches and tools were removed from this repo
([libhyb](software/libhyb.md) replaced them, see the
[porting notes](porting-notes/kitkat.md#libhyb-lessons-maple-2026-10-18-built-on-device-verification-pending));
poplar still builds that design from its own device tree, and these fixes
are what a poplar-to-libhyb port must carry over (or consciously drop).
Also: [maple quirks](../devices/maple/quirks.md),
[poplar quirks](../devices/poplar/quirks.md).

What makes hardware GL work under KitKat 4.4.4 on the yoshino MSM8998 devices —
[maple](../devices/maple/README.md) (XZ Premium) and [poplar](../devices/poplar/README.md) (XZ1). Same SoC,
same 4.4 kernel, same Adreno 540, so **every fix is shared**; they were developed on
poplar.

KitKat has no Adreno 540 driver and `libagl` is ES1-only, so the UI runs on the goldfish
emulator GL libs pointed at a daemon instead of a qemu pipe:

```
guest libEGL/libGLESv1_CM/libGLESv2  ->  @glrenderd abstract socket
  ->  glrenderd (Lineage userspace chroot, 32-bit)  ->  Adreno 540
  ->  readback  ->  fb0 pan
```

**One flipper owns `fb0`** — a second presenter wedges mdss overlay start (`EBUSY`).

| symptom | cause | fix |
| --- | --- | --- |
| Tearing / artifacts, varies by app | emugl `HostConnection` is thread-local, so every zygote-forked app kept **zygote's** socket — two processes, one GL stream | `HostConnection::get()` tracks a static `s_ownerPid`, rebuilds on pid change |
| UI freeze, compositor stuck | unbounded `while (glGetError() != GL_NO_ERROR)` — one blocking socket round-trip per iteration, on the compositor thread | bound to 8 iterations |
| UI freeze, input dead (`DispatchFrozen: 1`) | frozen dispatch branch returned without setting `*nextWakeupTime` | 10 s self-thaw |
| `SIGSEGV` in `GLClientState::setActiveTextureUnit` (fault `0x1a0`) on reconnect | freeing/re-creating `HostConnection` + encoders is a use-after-free | reconnect **in place** |
| SurfaceFlinger killed outright | `::send(..., 0)` raises `SIGPIPE` | `MSG_NOSIGNAL` |
| Frames stall mid-push | `net.core.rmem_max` (208 KB) clamps the 8 MB `SO_RCVBUF`; an AF_UNIX sender blocks when the queue fills, so every 8.3 MB frame push stalls the guest | `raise_socket_limits()` in `glrenderd` |

A write on a **dead stream must return success (0)** — `alloc()` treats failure as NULL and
the prebuilt encoders then `memcpy` into it. Mark the stream dead and drop; never block.

## Invariants

- Never add members to `HostConnection` — its inline `flush()`/`alloc()` hard-code member
  offsets. Never move `IOStream` members either.
- Do not rebuild `glrenderd` casually: its build regenerates the codec tables, and a daemon
  that no longer matches the guest libs garbles the panel. Keep the known-good binary.
- `persist.glr.replytimeout` (default 20 s) = how long a guest waits before declaring the
  daemon dead.

## Status — glrenderd-era; the patch set was retired 2026-10-18

The fixes below existed only in the poplar working tree (the four
scaffolding patches were deleted from `patches/kitkat/` together with the
rest of the glrenderd design — all in git history). **The glrenderd GPU
behaviour cannot be reproduced from the repo as it stands**; poplar's
device tree plus git history hold the old design, or port libhyb
([checklist](software/libhyb.md#porting-it-to-another-device)) and these
fixes become moot:

| fix | file |
| --- | --- |
| fork-safe connection | `device/generic/goldfish/opengl/system/OpenglSystemCommon/HostConnection.cpp` |
| dead stream, `MSG_NOSIGNAL` | `device/generic/goldfish/opengl/shared/OpenglCodecCommon/SocketStream.cpp` |
| reconnect in place | `device/generic/goldfish/opengl/system/OpenglSystemCommon/RenderdStream.cpp` (`.h`) |
| bounded `glGetError` | `frameworks/native/libs/gui/GLConsumer.cpp` |
| dispatch self-thaw | `frameworks/base/services/input/InputDispatcher.cpp` (`.h`) — **still in the repo**: `patches/kitkat/frameworks-base-input-dispatch-freeze-thaw.patch` |
| socket caps | `tools/glrenderd/src/glrenderd.cpp` (`raise_socket_limits`) — deleted 2026-10-18, git history |

The first two extended `device-generic-goldfish-glrenderd.patch`; the
GLConsumer one bounded the socket round-trip (moot without the socket);
the InputDispatcher fix is transport-independent and still applied.
