# chiron KitKat WiFi 修复进度（交接文档）

日期：2026-10-26（更新）。状态：**WiFi 已经修好并做到开机自启，framework 开关可用**，
冷启动无需任何手工操作（详见第 9 节）。
真正的唯一硬件根因是 **msm8998 legacy ipc-router 的 IRSC completion 从未被触发**（第 8 节），
与之前推测的 PeripheralManager/DMS、group 1000 都无关。修复方式是用自研静态二进制 `wlq irsc`
在 post-fs-data 加载一次 sec_config 并关掉那个 fd，从而 fire `signal_irsc_completion()`；
之后 vendor cnss-daemon 自己就走完 BDF 下载。
剩下的最后一层是 framework 的开机竞争（supplicant ~29s 起、wlan0 ~39s 才注册），
用"启动期锁住 supplicant 配置文件 + 驱动就绪后补一次真实 off→on 开关"解决（第 9 节）。

冷启动实测证据（真实 reboot，全程零手工命令，uptime 记时）：
- `/data/local/tmp/wlq-irsc.log`：`irsc: 295 rules loaded, closing IRSC port -> signal_irsc_completion`
- `dmesg | grep -c "waiting for IPC Security Conf"` = **0**（修复前每个用户态 QMI 发送都卡在这句）
- `/sys/kernel/debug/icnss/stats`：`FW_READY 1/1`、`State: 0xd8d(QMI CONN | FW READY | DRIVER PROBED |
  SSR REGISTERED | PDR REGISTERED | MSA0 ASSIGNED | WLAN FW EXISTS)`、`root_pd_crash: 0`
- `/sys/class/net` 有 **wlan0/wlan1/p2p0**，MAC **00:0a:f5:6c:c9:98**（来自固件，非全零）
- 29.03s `WifiStateMachine: Failed to start supplicant!`（预期的干净失败，无 wedge）；
  41.1s `/sbin/wf-wifi-gate.sh` 解锁 conf，43.5s 做 disable→enable，62.6s 记录
  `framework reports Wi-Fi enabled`
- `dumpsys wifi`：**Wi-Fi is enabled**，`Supplicant state: COMPLETED`，连上 "Xiaomi 15"
  （Link speed 866），`wlan0` 拿到 **10.237.226.204/24**，`ping 8.8.8.8` 3/3 通
- 之后 `svc wifi disable` → supplicant stopped + wifi_on=0；`svc wifi enable` → 重新
  COMPLETED + 保持原 IP，即用户侧开关双向可用

## 0. 一句话现状

三层根因都已解决：(1) modem 41 秒自崩循环 = rmt_storage + tftp_server 每次 SSR 后死掉无人重启，
现由 boot ramdisk 里的 wf-efs-keeper.sh 看护；(2) WLAN 不起来 = ipc-router 的 IRSC completion
从未 fire，导致 cnss-daemon 的 `wlfw_bdf_download_req` 永久挂起，现由 ramdisk `/sbin/wf-setup.sh`
里的 `wlq irsc` 每次开机 fire 一次；(3) 用户侧 WiFi 开关打不开 = framework 开机起 supplicant
时 wlan0 还不存在，而这条失败路径会把 WifiStateMachine 永久锁死，现由 wf-setup.sh 的
"conf 上锁"+ wf-wifi-gate.sh 的"驱动就绪后解锁并补一次真实开关"解决（第 9 节）。
整条链路（驱动 → FW_READY → wlan0 → supplicant → framework ENABLED → 扫描/连接/DHCP）
冷启动自动可用，之后的用户开关走 framework 正常路径。

## 1. 已固化到镜像的东西（boot-wf.img，基于 boot-good.img，4.4.247-perf+ #3）

**唯一来源是设备树** `devices/chiron/kitkat/wifi-tools/`（rdg/ 下那份是历史草稿，已作废）：
`wf-block-append.rc`、`wf-setup.sh`、`wf-efs-keeper.sh`、`wf-wifi-gate.sh`、`repack-wf.sh`。
modem-hold.sh 与 sagit 共用一份（`devices/sagit/kitkat/modem-hold.sh`，靠 `/etc/rfs_gate`
是否存在来开关 RFS 等待，sagit 不受影响）。

- /init.chiron.rc 追加（wf-block-append.rc）：
  on post-fs-data: start wf-setup / start wf-efs / start wf-wifi-gate；服务 wf-setup
  (oneshot,/sbin/wf-setup.sh)、wf-efs(respawn,/sbin/wf-efs-keeper.sh)、
  wf-wifi-gate(oneshot,/sbin/wf-wifi-gate.sh)、wf-sm(sm-vndk)。
- ramdisk /sbin/wf-efs-keeper.sh：轮询 /proc/*/cmdline，缺 tftp_server / rmt_storage /
  cnss-daemon 就用 ( LD_LIBRARY_PATH=/system/lib64:/vendor/lib64 /x >/dev/null 2>&1 & ) 拉起。
- ramdisk /sbin/wf-setup.sh：/dev/vb 软链、/mnt/vendor/persist、by-name 里 modemst1/2、fsc/fsg
  软链、挂 debugfs、rfs-dirs.sh、( rfssrv2 & ) 后台起 RFS/TFTP 服务并等它 listen 后 touch
  /data/local/tmp/rfssrv2.ready（modem-hold.sh 等这个文件才放 modem 起来）、`wlq irsc` fire
  IRSC completion、start wf-efs / wf-sm，最后把 `/data/misc/wifi/wpa_supplicant.conf`
  锁成 root:root 0600（第 9 节）。
- 重建脚本：`devices/chiron/kitkat/wifi-tools/repack-wf.sh`（WSL 上跑，临时树在 ext4 的 /tmp/rdf，
  产物写到 Windows 工作区 `boot-wf.img`；`WS=` 可覆盖工作区路径；带 ramdisk 体积与文件完整性
  双重 guard，避免"cpio 静默跳过 → 生成 55 字节 ramdisk → 变砖"这类事故）。
- KitKat 坑：**没有 nohup / pidof / sed / tail / head / wc / awk / setsid / tr**；
  export LD_LIBRARY_PATH 会污染 toolbox（libcutils.so 32/64-bit），只能给单个二进制前缀赋值。
- **这三个 vendor 二进制不能直接做 init service**：被 init 直接 exec 会在 ~3s 内静默退出，
  同一命令行在 shell 里能长期存活；所以用 keeper 脚本代管。
- 所以 init 里那个 `service wf-irsc /system/vendor/bin/irsc_util ...`（即使带 setenv）
  **从来没生效过**，这也正是 IRSC completion 一直没人 fire 的原因。现在由 /system 钩子里的
  静态 `wlq irsc` 承担（第 6/8 节），不依赖 LD_LIBRARY_PATH，也不依赖 init 能 exec vendor 二进制。

## 2. 现场观测到的关键事实（2026-10-26，修复**前**）

这些现象现在都能由第 8 节解释：只要 IRSC completion 没 fire，所有用户态 QMI sendmsg
一律挂在 do_sys_poll，所以守护进程"活着但不响应"、RRQ=0、FW_READY=0 是同一个病根的不同表现。

- 稳定态（三个守护都在，rfssrv2 在听）：root_pd_crash 冻结、modem ONLINE，但 RRQ=0、
  SERVER_ARRIVE=0、FW_READY=0、State 只有 0x180/0x4180(SSR|PDR|FW DOWN)。
- 手工 ctl.stop/ctl.start modem_hold 循环：曾经出现过完整一轮
  ind_register/msa_info/msa_ready/cap 全部 resp、rfssrv2 成功服务 441 块 wlanmdsp.mbn、
  service-notifier 报 msm/modem/wlan_pd state 0x1fffffff（PD Up），但 **FW_READY_ind 始终为 0**，
  随后 WLFW SERVER_EXIT，icnss 落入 FW_DOWN|MODEM_CRASHED 只能重启恢复。
- "client ERROR, abort / read done (10 blocks)" 是常见失败形态（RRQ 被中断），
  只有最后一次 rsize.3384 那轮读满 441 块。
- mpss_IPCRTR 里能看到 modem 侧注册 SVC<0x42:0xb401>（RFSA），AP 侧 kworker 发 QCCI TX/MI:20。

## 3. 剩余工作（按优先级）

1. 冷启动稳定性再观察：目前只有连续几次 reboot 的记录（第 8.1、9.4 节），modem/wifi 的
   SSR 恢复路径还没做过"长时间跑 + 断网重连"的压力验证。
2. 同一套竞争在 sagit 上也存在（同一颗 WCN3990 + 同一份 framework）。sagit 的 ramdisk 现在
   只有 `dump-boot-log.sh` 里的 `wlq irsc` 钩子，没有 wf-setup/wf-wifi-gate，所以它的
   framework 开关仍会踩第 9 节的坑——需要把 wf-block-append.rc + 三个脚本折进 sagit 的 boot 镜像。
3. 可选：`/system/bin/dump-boot-log.sh` 里的 IRSC 钩子现在是冗余的（ramdisk 的 wf-setup.sh
   在 post-fs-data 就 fire 了，比它早 ~23s）。留着只对没带 ramdisk  wf 栈的镜像有意义；
   若保留，注意 completion 一次性，重复 fire 无害但要确保不会两处都失败。
4. 可选：把 Settings 里 WiFi 开关的 UI 反馈也测一遍（本轮验证走的是 `svc wifi`，
   即同一条 IWifi.setWifiEnabled 路径）。

## 5. 定位过程（保留，但结论以第 8 节为准）

**根因 A — BDF 文件从未安装（已修，进 full_chiron.mk）**
WCN3990 的 WLFW(QMI 0x45) 序列被劈成两半：内核 icnss 只做 ind_register/msa_info/msa_ready/cap，
**只有 vendor 的 cnss-daemon 会做 wlfw_bdf_download_req（板级数据文件）+ mac_addr + cal_report**。
modem 只有在 BDF 下载成功后才发 FW_READY_ind。而 full_chiron.mk 旧版只把 sagit 的 bdwlan* 拷进
/system/etc/wifi/，cnss-daemon 根本不在那找。它在 /vendor/firmware/ 和 /vendor/firmware_mnt/image/
里按固定名搜：bdf_c2.bin、bdf_d5.bin、bdf_d5s.bin、bdwlan.bin 以及 .%03x(chip)/.b%02x(board) 变体；
chiron 是 D5（chip_id 0x30214），要的就是 **bdf_d5.bin**。缺它 → "Failed to Download the BDF File" →
永远没有 FW_READY。现已在 full_chiron.mk 里把 sagit 的通用 bdwlan.bin 复制成这几个名字装进
system/vendor/firmware/ 和 .../firmware/image/。

**曾经的根因 B 推测 — PeripheralManager/DMS（已被第 8 节推翻）**
用 wifi-tools/liblog-shim.so 把 cnss-daemon 的 ALOG 打到 stderr，再用 dstart(fork+setsid)
把它从 adb shell 里彻底脱离（否则 shell 一关就收 SIGHUP，日志会假死在 "Caught Signal: 1"），
拿到一条不被打断的真实开机序列：
```
WLFW service connected
ro.baseband : [msm]
Failed to init DMS client, ret -17
wlfw_read_file: No such file /data/vendor/wifi/wlfw_cal_0N.bin  (00..04, 缺cal, 非致命)
<然后就挂住：/proc/<pid>/wchan = do_sys_poll>
```
当时把挂住归因于 KitKat 上没有 vndservicemanager / vendor.qcom.PeripheralManager。
**这是错的**：`pm_init ret=-1` 与 `DMS ret -17` 都是非致命的，cnss 照样继续往下走；
真正卡住它的是那次 sendmsg 永远等不到 IRSC completion（见第 8 节）。completion 一 fire，
同一条 cnss-daemon 就顺利完成 BDF 下载并拿到 FW_READY。

**KitKat 观测与工具坑（仍然有效）**
- 设备无 /dev/log、无 logd，/proc/misc 无 log/*：所有 ALOG（vendor 守护、wpa_supplicant）静默丢弃。
  解法：wifi-tools/liblog-shim.c — 纯 raw-syscall、零 DT_NEEDED/零未定义符号的 liblog 替身，
  build-shim.sh 用 linaro aarch64/arm 交叉编出 32/64 两个 .so，LD_PRELOAD 即用。
- adb shell 里跑 daemon 会被 SIGHUP：无 setsid/nohup，用 wifi-tools/dstart.c 静态编译后 fork+setsid 拉起。
- wpa_supplicant v2.1-devel-4.4.4 只编了 nl80211（-Dwext 直接打 usage），/system/lib 无 libnl 但
  它静态链了 nl80211 不需要外部 libnl；conf 必须写 `ctrl_interface=DIR=<可写目录>`，
  模板里那个 `ctrl_interface=wlan0` 会让它去 mkdir 只读根（Read-only file system）。
- **conf 路径权限**：/data/misc/wifi 是 drwxrwx--- wifi wifi，root 从 adb shell 起 supplicant 会报
  `Failed to open config file ... Permission denied` → `Failed to add interface wlan0`（错误只进 dmesg，
  stderr 是空的）。改在 /data/local/tmp 放 777 的 conf + sock 目录就完全正常（见 wpascan2.sh）。
- toolbox 的 ifconfig 对 wlan0 报假错（Cannot assign requested address），用 `ip link set wlan0 up`。
- **64 位 vendor 二进制的链接坑**：irsc_util / cnss-daemon 是 aarch64，直接跑报
  `liblog.so is 32-bit instead of 64-bit`（4.4 没有 /linkerconfig/ld.config.txt）。
  只要 `export LD_LIBRARY_PATH=/system/lib64:/vendor/lib64` 就能正常跑（cnss-run.sh 就是这么做的）。
- **这些 vendor 二进制不能由 init 直接 exec**（会 ~3s 静默退出），必须从 shell 派生
  `( LD_LIBRARY_PATH=$K $BIN args >/dev/null 2>&1 & )`；所以 init service `wf-irsc` 其实从来没生效过。

## 6. 落地的修复（本轮固化进仓库）

1. `devices/chiron/kitkat/wifi-tools/wlq.c` 新增 **`irsc` 模式**（`irsc_load()`）：开一个
   AF_MSM_IPC SOCK_DGRAM 端口 → `IPC_ROUTER_IOCTL_CONFIG_SEC_RULES` 逐条加载 sec_config
   （本设备 295 条）→ **close(fd)**。close 让内核在 IRSC_PORT 的释放分支调用
   `signal_irsc_completion()`，此后本轮开机所有用户态 QMI sendmsg 立即通过。
   静态交叉编译，无 liblog / LD_LIBRARY_PATH 依赖，32/64 位都能跑。
   构建：`wifi-tools/build-wlq.sh`（产物同时 stage 进 out-of-git 的
   `vendor/xiaomi/sagit-bin/bin/wlq`）。
2. `devices/chiron/kitkat/wifi-tools/bootlog-wifi-hook.sh` → 设备上
   `/system/bin/dump-boot-log.sh`：**每次开机**（不再是一次性 guard）后台执行
   `$WLQ irsc /system/vendor/etc/sec_config >/data/local/tmp/wlq-irsc.log 2>&1 &`，
   其中 `WLQ=/system/bin/wlq`，取不到再退 `/data/local/tmp/wlq`。这个钩子由 init 的
   bootlog 服务在 boot 早期跑起来，正好赶在 cnss-daemon 发 BDF 之前。
3. `full_chiron.mk`：BDF 变体安装（根因 A）+ `$(BIN_DIR)/bin/wlq → system/bin/wlq` 条件安装，
   并写全 IRSC 根因注释。

## 7. （历史）直发 BDF 的 QMI 编码器规格

保留备查：最终修复**没有**走"自研客户端直发 BDF"这条路——cnss-daemon 在 IRSC completion
fire 之后自己就把 BDF 做完了，所以 wlq 的 bdf 模式只是调试工具，不是生产路径。

- 消息号：**QMI_WLFW_BDF_DOWNLOAD_REQ_V01 = 0x0025**（resp 同 0x0025；单请求最大 6178B），
  不是 0x22。QMI 头沿用 wlq.c 的 7 字节 legacy 框架（cntl|txn|msg_id|msg_len）。
- 分块：一次 data TLV 最多 **6144B**（QMI_WLFW_MAX_DATA_SIZE_V01），19152B 的 bdf_d5.bin
  要 4 段（0..3），每段 seg_id 递增，末段 end=1。
- TLV 布局（可选字段先发 1 字节 OPT_FLAG=0x01，与其对应值 TLV 同号、相邻两发）：
  0x01 u8 image_type（BDF 取 1）/ 0x10 u32 file_id（BDF 用 0）/ 0x11 u32 total_size /
  0x12 u32 seg_id / 0x13 u16 len + 裸数据 / 0x14 u8 end。
- 后续步骤：MAC 是 0x0033，host_cap 是 0x0034；内核 icnss 已发过 ind_register/msa_info/
  msa_ready/cap。

## 8. 最终根因：ipc-router 的 IRSC completion 从未触发（已证实）

msm8998 用的是 legacy ipc-router v1。在这套代码里，**每一个用户态 CLIENT_PORT 的 sendmsg
都先等一个全局 completion**：
- `net/ipc_router/ipc_router_socket.c:350-351` → `wait_for_irsc_completion(r_port_ptr)`
  （日志就是 dmesg 里那句 "waiting for IPC Security Conf."）
- 该 completion 只在 `net/ipc_router/ipc_router_core.c:3558-3562` 被满足，且**仅当被释放的
  那个 port 的 type == IRSC_PORT** 时才调 `signal_irsc_completion()`。
- port 变成 IRSC_PORT 的唯一途径是
  `ioctl(fd, IPC_ROUTER_IOCTL_CONFIG_SEC_RULES, ...)`：`ipc_router_socket.c:509-513` 只要返回值
  不是 -EPERM 就把 `port->type = IRSC_PORT`。

所以冷启动时序是这样的：没人（或没成功地）加载过 sec_config → 没有任何 IRSC_PORT 被 close →
completion 永不 fire → **所有**用户态 QMI 请求（cnss 的 bdf_download_req、rfssrv2、rmt_storage
的多余请求…）全部挂在 do_sys_poll。modem 因此收不到 BDF，从不发 FW_READY_ind，icnss 停在
state 0xd83（QMI CONN | POWER ON | WLAN FW EXISTS，没有 FW READY）。

几个曾经的误判，一并纠正：
- **不是** PeripheralManager/DMS：那些报错非致命，cnss 照走。
- **不是** 权限/group：`msm_ipc_check_send_permissions()` 对 `current_euid == GLOBAL_ROOT_UID`
  直接返回 1，默认规则是 ALL_SERVICE→AID_NET_RAW。root 下的 cnss/wlq 从不被规则拒绝，
  所以把 sec_config 的 group 改掉对 root 完全无效（inert）。
- completion 是**全局且一次性**的：一个进程 close 掉 IRSC_PORT 之后，本轮开机所有后续发送都放行；
  因此 `wlq irsc` 每次开机跑一次就够，重复跑没有意义。

`struct config_sec_rules_args`（include/uapi/linux/msm_ipc.h:48-54）：
`int num_group_info; uint32_t service_id; uint32_t instance_id; unsigned reserved; gid_t group_id[0];`
= 16 字节头 + gid 数组紧随其后；ioctl 号 `_IOR(IPC_ROUTER_IOCTL_MAGIC, 5, struct config_sec_rules_args)`。
sec_config 每行 `service_dec:instance_dec:gid[:gid...]`，`/*` 开头是注释；WLFW 那行是 `69:4294967295:1000`。

## 8.1 冷启动验证记录（2026-10-26，真实 reboot，零手工触发）

| 观测点 | 结果 |
| --- | --- |
| wlq-irsc.log | `irsc: 295 rules loaded, closing IRSC port -> signal_irsc_completion` |
| dmesg "waiting for IPC Security Conf." | 0 次 |
| icnss stats | FW_READY 1/1、REGISTER_DRIVER 1/1、root_pd_crash 0 |
| icnss State | 0xd8d（QMI CONN \| FW READY \| DRIVER PROBED \| SSR REG \| PDR REG \| MSA0 ASSIGNED \| WLAN FW EXISTS）|
| 固件 | Chip ID 0x30214 / WLAN.HL.1.0.c5-00462（2019-07-07 build）|
| 接口 | wlan0 / wlan1 / p2p0，MAC 00:0a:f5:6c:c9:98 |
| wpa_cli scan | `OK`；scan_results 11 个真实 AP（baiyun，2.412–2.472G + 5.180/5.300/5.785G）|

## 9. 最后一层：framework 的开机竞争，以及用户侧开关为什么"打不开"

### 9.1 时序

| 时刻 | 事件 |
| --- | --- |
| ~4 s | init post-fs-data：wf-setup.sh（锁 conf、起 rfssrv2、fire IRSC、起 keeper） |
| ~27 s | modem 起来，icnss 开始和 WLFW 说话 |
| ~28.9 s | WifiService 看到 `Settings.Global.wifi_on=1`，自动开 WiFi → `wifi_start_supplicant()` |
| ~29.0 s | wpa_supplicant 起来就死：`Could not read interface wlan0 flags: No such device` |
| ~39.2 s | `icnss: WLAN FW is ready` → `wlan: probing driver` → **wlan0 这时才出现** |

驱动不可能更快（modem 自身启动就要 ~27s），所以 28.9s 那次尝试注定失败；问题只在于
**失败之后 framework 还剩不剩得救**。

### 9.2 为什么随手 `svc wifi enable` 没用（三条 framework 事实）

1. `frameworks/base/services/java/com/android/server/wifi/WifiService.java:591-616`：
   `setWifiEnabled()` 先问 `mSettingsStore.handleWifiToggled(enable)`，它**只在请求状态和
   自己记的状态不同**时才通知 WifiController；相同就 `return true` 什么都不做。开机那次
   尝试已经把 `wifi_on` 落成 1，所以之后的 `svc wifi enable`（以及 Settings 里再点一次
   "开"）全是空操作 —— **必须做真实的 off→on 两条边**。
2. `frameworks/base/wifi/java/android/net/wifi/WifiStateMachine.java:2703-2710`
   （InitialState 的 `CMD_START_SUPPLICANT`）：`startSupplicant()` 成功才会
   `startMonitoring()` 并进 SupplicantStartingState；失败只打 `Failed to start supplicant!`
   并且**留在 InitialState**，是完全可恢复的状态。
3. `frameworks/base/wifi/java/android/net/wifi/WifiMonitor.java:407-441`
   `startMonitoring()`：连不上 supplicant 就重试 6 次（每次 sleep 1s），全部失败后
   **`mIfaceMap.remove(iface)`** 再发 SUP_DISCONNECTION_EVENT。而重新注册只在构造时的
   `registerInterfaceMonitor()`（同文件 452-456）发生一次。也就是说：一旦 supplicant 起来过
   但又没有 ctrl socket（正是"wlan0 还不存在 → supplicant 启动即退出"这个场景），
   framework 会烧掉这 6 次重试并把 wlan0 从 map 里永久删掉，此后任何 enable 只会打
   `startMonitor called with unknown iface=wlan0`（同文件 410）然后什么都不做 ——
   **本轮开机 WiFi 彻底救不回来，只能重启 system_server**。

实测两种开机结局（同一次代码，纯运气）：一次是 `40.02→44.03 WifiHW: Supplicant not running,
cannot connect` ×5 → `startMonitoring(wlan0) failed!` → `Failed to setup control channel,
restart supplicant` → `44.237 startMonitor called with unknown iface=wlan0`（wedge，之后
dumpsys 永远停在 "Wi-Fi is enabling"）；另一次是 `28.969 WifiStateMachine: Failed to start
supplicant!`（干净，dumpsys 显示 "Wi-Fi is disabled"，之后可救）。差别只是 init 那个短暂的
`init.svc.wpa_supplicant=running` 有没有被 `wait_for_property` 撞上。

### 9.3 修复

思路：让开机那次尝试**必然走干净失败分支**（第 2 条），然后在驱动就绪后补一次真实的
off→on（第 1 条）。两者都不动 framework 代码，因此不需要重编 framework.jar/处理 odex。

- `wifi-tools/wf-setup.sh` 末尾（post-fs-data，~4s）把 `/data/misc/wifi/wpa_supplicant.conf`
  锁成 `root:root 0600`。`wifi_start_supplicant()` 在 **碰 init 之前** 先调
  `ensure_config_file_exists()`（`hardware/libhardware_legacy/wifi/wifi.c:502-505`），
  该函数发现文件存在但 uid system 不可读写时会尝试 `chmod` 并由 EPERM 失败返回 -1
  （同文件 407-413），于是 wifi.c 打 `Wi-Fi will not be enabled`、Java 打
  `Failed to start supplicant!`，**根本不会启动 supplicant，也不会进 startMonitoring**，
  状态机停在 InitialState、`mIfaceMap` 完好。
- `wifi-tools/wf-wifi-gate.sh`（init oneshot 服务）等 `/sys/class/net/wlan0` 出现 →
  把 conf 恢复成 `1000:1010 0660`（正是 wifi.c 自己会设的属主/权限）→ sleep 2 让接口稳定 →
  读 `settings get global wifi_on`（≠1 就退出，绝不和用户抢开关）→
  `dumpsys wifi | grep 'Wi-Fi is enabled'` 判定成败 → `svc wifi disable; sleep 4; svc wifi enable`
  → 最多 5 轮。之后框架完全接管，脚本退出（所以是 oneshot，不是 respawn）。

### 9.4 验证记录（2026-10-26，boot-wf.img = 设备树 repack-wf.sh 产物，md5 3a3bb932d4738c1ea4406b3e0b58a153）

| uptime | 观测 |
| --- | --- |
| 29.03 s | `WifiStateMachine: Failed to start supplicant!`（预期；dmesg 里没有 supplicant 进程输出、没有 `unknown iface`）|
| 41.07 s | gate：`wlan0 present, unlocking /data/misc/wifi/wpa_supplicant.conf` |
| 43.46 s | gate：`cycle #1: svc wifi disable + enable` |
| 48.11 s | `WifiMonitor: startMonitoring(wlan0) with mConnected = false` → 一次成功，无 failed |
| 62.64 s | gate：`framework reports Wi-Fi enabled` 后退出（`init.svc.wf-wifi-gate=stopped`）|
| — | `dumpsys wifi`：Wi-Fi is enabled / `Supplicant state: COMPLETED` / SSID "Xiaomi 15" / Link speed 866 |
| — | `wlan0` = 10.237.226.204/24，`ping -c 3 8.8.8.8` 3/3（rtt ~66ms）|
| 之后 | `svc wifi disable` → supplicant stopped、`wifi_on=0`；`svc wifi enable` → COMPLETED、IP 保持，用户开关双向可用 |

## 4. 红线（用户规矩，必须遵守）

- 抓完日志立即 adb reboot recovery，别让手机一直亮屏耗电。
- vendor blobs 不进 git；AGENTS.local.md 不外传；不 force push、不开 PR；只在被要求时 commit，永不 push。
- 修复落在 /home/juezhi/retro-android-ports 设备树，不动 aosp 树。
- 给用户的命令一律单行、可直接粘贴。
