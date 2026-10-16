# KitKat GPU fixes — `glrenderd` (maple / poplar)

What makes hardware GL work under KitKat 4.4.4 on the yoshino MSM8998 devices —
[maple](devices/maple.md) (XZ Premium) and [poplar](devices/poplar.md) (XZ1). Same SoC,
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

## Status — not in `patches/kitkat/` yet

Only the earlier scaffolding is there (`device-generic-goldfish-glrenderd`,
`frameworks-native-egl-glrenderd`, `sdk-glrenderd`, `system-core-glrenderd-fs-config`).
The fixes below exist only in the poplar working tree, so **the GPU behaviour cannot be
reproduced from the repo until they are formalised**:

| fix | file |
| --- | --- |
| fork-safe connection | `device/generic/goldfish/opengl/system/OpenglSystemCommon/HostConnection.cpp` |
| dead stream, `MSG_NOSIGNAL` | `device/generic/goldfish/opengl/shared/OpenglCodecCommon/SocketStream.cpp` |
| reconnect in place | `device/generic/goldfish/opengl/system/OpenglSystemCommon/RenderdStream.cpp` (`.h`) |
| bounded `glGetError` | `frameworks/native/libs/gui/GLConsumer.cpp` |
| dispatch self-thaw | `frameworks/base/services/input/InputDispatcher.cpp` (`.h`) |
| socket caps | `tools/glrenderd/src/glrenderd.cpp` (`raise_socket_limits`) |

The first two extend `device-generic-goldfish-glrenderd.patch`; the GLConsumer and
InputDispatcher ones are new `frameworks` patches.
