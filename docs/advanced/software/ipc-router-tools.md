# IPC-router tools: irsc-load and pdstart

Two small static-armv7 tools for maple/KitKat's legacy IPC router
(`AF_MSM_IPC`) layer: `irsc-load` arms the IPC Router Security
Configuration so that *any* userspace QMI client can send at all, and
`pdstart` is a raw SERVREG_NOTIF client for the modem's servreg notifier
(`0x42:0xb401`) used to interrogate the `msm/modem/wlan_pd` domain.
Sources: `devices/maple/kitkat/pdstart/{irsc-load,pdstart}.c`. Context:
[maple quirks](../../devices/maple/quirks.md) (TEST 53 modem stall, the
IRSC bullet), [KitKat porting notes](../porting-notes/kitkat.md),
[KitKat build guide](../build/kitkat.md).

## irsc-load

- **Why.** `net/ipc_router/ipc_router_socket.c:351` calls
  `wait_for_irsc_completion()` on *every* send from a `CLIENT_PORT`, and
  that completion is only signaled when an **IRSC_PORT is destroyed**
  (`ipc_router_core.c:3567` → `signal_irsc_completion()` →
  `complete_all()`). Stock arms this with `irsc_util`, which binds a
  socket, loads every `sec_config` rule via
  `IPC_ROUTER_IOCTL_CONFIG_SEC_RULES` (`_IOR(0xC3, 5)`, root-only; the
  ioctl arms the port as IRSC_PORT unless it returns `-EPERM`), then
  exits. Without that daemon the completion never fires: userspace QMI
  senders hang in `D` state (`wait_for_irsc_completion+0x54 ->
  msm_ipc_router_sendmsg+0x2ac -> SyS_sendto`), SIGKILL-proof, with a
  `wait_for_irsc_completion: waiting for IPC Security Conf.` line in
  `dmesg` every 30 s. Kernel QMI handles are unaffected (server-side
  rmt_storage/tftp/pdserve traffic works), so the failure is invisible
  until a userspace *client* tries to send.
- **Usage.** `irsc-load [sec_config]` (default `/vendor/etc/sec_config`,
  format `service:instance:gid[:gid...]`, decimal, `4294967295` = all
  instances, `/`- or `#`-comment lines). It reports
  `rules ok=300 fail=0` on maple and closes the socket, which signals the
  completion. Verified 2026-10-19: two probes stuck in the wait for 8+
  minutes exited immediately and later `pdstart` traffic flowed.
- **Semantics.** Rules are appended per service; lookup falls back
  specific `<service,instance>` → service-wide → `ALL_SERVICE` → the
  built-in default (AID_NET_RAW 3004). `msm_ipc_check_send_permissions`
  always allows `euid == 0`, so root probes are unaffected either way.
  The A15 `irsc64` binary staged on the device fails here (rc=255, no
  rules loaded); use this tool. It belongs in the port's boot sequence
  ahead of any non-root QMI client.

## pdstart

- **What it speaks.** The legacy router userspace API exactly like
  `pdserve` does server-side: `AF_MSM_IPC` (27) `SOCK_DGRAM`,
  `IPC_ROUTER_IOCTL_LOOKUP_SERVER` (`_IOWR(0xC3, 2)`) to resolve
  `{service 0x42, instance 0xb401}` (= `BUILD_INSTANCE_ID(vers 1, ins
  180)`, see `qmi_interface.c:38`), `connect()` by `MSM_IPC_ADDR_ID`,
  then the 7-byte QMI header + TLVs (`u8 cntl_flag | u16 txn | u16 msgid
  | u16 msglen`; top-level strings are bare bytes, no NUL).
- **Messages** (from `drivers/soc/qcom/service-notifier-private.h`):
  `0x20 REGISTER_LISTENER` (TLV 0x01 `u8 enable`, TLV 0x02 name; resp
  TLV 0x02 `{result,error}` + TLV 0x10 `curr_state`), `0x21 QUERY_STATE`
  (TLV 0x01 name), `0x24 RESTART_PD` (TLV 0x01 name). States:
  `0x7fffffff` UNINIT, `0x1fffffff` UP, `0x0fffffff` DOWN.
- **Field result (2026-10-19).** Against the dying maple modem:
  register → `result=0 error=0`, `curr_state=0x7fffffff` (UNINIT);
  query → `result=0 error=0`, UNINIT; **restart → `result=1 error=9`
  (refused)**; no `STATE_UPDATED_IND` in 10 s; modem behavior unchanged.
  So the TMS knows the wlan_pd domain but will not start it on AP
  request — the domain's UNINIT state is internal to the FW. Details and
  the surrounding investigation: [maple quirks](../../devices/maple/quirks.md).
- **Build.** Static armv7 musl:
  `~/toolchains/armv7-eabihf--musl--stable-2026.08-1/bin/arm-buildroot-linux-musleabihf-gcc -static -O2 -o <tool> <tool>.c`.
  Both tools are HUP-proof when run from `adb shell` and leave no state
  behind except the IRSC completion (per-boot).
