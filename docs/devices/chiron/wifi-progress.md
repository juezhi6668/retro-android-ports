# chiron KitKat WiFi 修复进度（交接文档）

日期：2026-10-26（更新）。状态：**WiFi 已经修好并做到开机自启，framework 开关可用**，
冷启动无需任何手工操作（详见第 9 节）。2026-10-26 晚把整套栈抽象成设备无关的一份，
同步给 sagit（同一颗 WCN3990、同一套 icnss/内核路径），并改成由 AOSP 构建安装，
不再依赖手工 push（第 10 节）。
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
2026-10-27 又补了两个只在"干净镜像"上才暴露的前提（`/system/bin/linker64` 由构建安装、
`/dev/block/bootdevice` 别名改指向内核自己建好的 by-name），现在这张镜像是新机器刷完
就能用 WiFi 的状态，见第 11 节。
第 12 节是同一套栈往**预构建 ROM**（sagit 上的 MIUI V5）搬的做法和两个成品镜像；
那边还有一个本 port 完全没有的坑：ROM 自带的 `libhardware_legacy.so` 会去 insmod
`wlan.ko`，而这套内核的 qca_cld3 是编进去的，所以开关必然打不开，必须换成 AOSP 那份。

第 13 节是 V5 那边最终打通的三条根因（HAL 走错控制口、`wifi.interface` 没设所以 fallback 成
`sta`、`WCNSS_qcom_cfg.ini` 是 20 行残档导致 Rx 全死），产物是 2026-10-27 那两个镜像
（boot md5 `ee2234588c8b53c5bfe9ec55b6662b97`、system md5 `2014196202b6bd13258cae04ab73d503`），
已用两次真实冷启动验证：刷完开机即连接，用户侧开关双向可用。

第 14 节是刷完之后剩下的"时间不对"：内核没有 `/dev/alarm`、PM8998 RTC 又没有
`allow-set-time`，所以框架拿到 NTP 结果也写不进钟；补救是 root 侧的 `/system/bin/time-sync`
（SNTP，退路 HTTP `Date:`）自己 `settimeofday()` 并写 `/data/system/time/rtc_offset`，
再由 ramdisk 里早就存在、但一直没被 shipped 的 `rtc-restore` 在下次开机应用。

第 15 节是同一套做法往**通知呼吸灯**上搬：V5 只带 cancro 的 `lights.msm8974.so`，驱动的是这个
DTS 不注册的 red/green/blue 节点，而真实节点 `/sys/class/leds/white` 的属性是 `root:root 0644`，
LightsService（uid system）每笔写都 EACCES —— 两半分别由 `inject-v5-system.sh`（同一份 AOSP
`lights.sagit.so` 装成两个查找名）和 `repack-v5-boot.sh`（把 chown 段单独切进 V5 ramdisk）固化。



## 1. 已固化到镜像的东西（由 `make bootimage` / `make systemimage` 安装）

**唯一来源是设备树**，两个设备共用同一份运行时脚本，放在 `devices/sagit/kitkat/`
（chiron 早就在用 sagit 的 init.rc/modem-hold.sh 等，sagit 是这份树的老家）：
`wf-setup.sh`、`wf-efs-keeper.sh`、`wf-wifi-gate.sh`、`rfs-dirs.sh`、`modem-hold.sh`、
`wifi/sec_config`。开发/构建工具留在 `devices/chiron/kitkat/wifi-tools/`
（`build-wf-bins.sh`、`repack-wf.sh`、`stage-wifi-vnd.sh`、`wlq.c`、`rfssrv2.c`、
`patch-sm-vndk.py`、观测用的 `dstart.c`/`liblog-shim.c`/`qmic.c`/`vpoke.c`）。
早先的 `wf-block-append.rc`、`build-wlq.sh`、`bootlog-wifi-hook.sh`、`rfssrv.c` 已删：
服务段进了两份 init rc，IRSC 钩子被 ramdisk 里的 wf-setup.sh 取代（早 ~23s）。

- `init.sagit.rc` 与 `init.chiron.rc` 追加同一段（已 diff 校验一致）：
  on post-fs-data: start wf-setup / start wf-efs / start wf-wifi-gate；服务 wf-setup
  (oneshot,/system/bin/sh /sbin/wf-setup.sh)、wf-efs(respawn,/sbin/wf-efs-keeper.sh)、
  wf-sm(sm-vndk)、wf-wifi-gate(oneshot,/sbin/wf-wifi-gate.sh)。四个服务都带 `disabled`，
  与 post-fs-data 里的显式 `start` 并存 —— KitKat 的 `SVC_DISABLED` 只是"不按 class 自启"
  （init.h:66、builtins.c:197），action 里的 `start` 照样会起它。
- `full_sagit.mk` / `full_chiron.mk` 把这些装进镜像：四个脚本 → `root/sbin/`
  （boot ramdisk），`wifi/sec_config` → `system/vendor/etc/sec_config`，
  `wlq`/`rfssrv2`/`sm-vndk`/`linker64` → `system/bin/`，A15 vendor 守护 + 两套 64 位库 →
  `system/vendor/bin`、`system/vendor/lib64`、`system/lib64`，BDF 名称变体 +
  `firmware/image/wlanmdsp.mbn`（第 10 节列出这些 blob 从哪来）。
- /sbin/wf-efs-keeper.sh：轮询 /proc/*/cmdline，缺 rmt_storage / cnss-daemon 就用
  ( LD_LIBRARY_PATH=/system/lib64:/vendor/lib64 $V/x >$LOG.$1 2>&1 & ) 拉起；
  **故意不起 tftp_server**（它和 rfssrv2 抢同一个 QMI 0x1000，且读不存在的
  /vendor/firmware_mnt/image）。
- /sbin/wf-setup.sh：`/dev/block/bootdevice` → 内核 ueventd 的
  `/dev/block/platform/soc` 别名 + vndbinder 软链（存在才不覆盖，设备无关，见第 11 节）、挂
  debugfs、跑 rfs-dirs.sh、后台起 rfssrv2 并等它 listen 后 touch
  /data/local/tmp/rfssrv2.ready（modem-hold.sh 等这个文件才放 modem 起来）、`wlq irsc`
  fire IRSC completion、start wf-efs / wf-sm，最后把 `/data/misc/wifi/wpa_supplicant.conf`
  锁成 root:root 0600（第 9 节）。三个二进制都按 /sbin → /system/bin → /data/local/tmp
  顺序找，所以既能在完整镜像里跑，也能在临时 repack 的镜像里跑。
- /sbin/rfs-dirs.sh 的可写半边挪到 **/data/rfs**：本端口 /vendor 就是 /system/vendor，而
  /system 只读，modem 的 WRQ（mcfg.tmp 回写）在干净镜像上永远写不进去；rfssrv2 的 root
  同步改成默认 `/data/rfs/msm/mpss` 并可用 argv[1] 覆盖。
- modem-hold.sh 的 RFS 等待开关从 `/etc/rfs_gate` 改成"有没有 `/sbin/wf-setup.sh`"，
  少一个只有开关作用的文件；sagit 装上这套栈后同样生效。
- 快速迭代脚本用的重建脚本：`wifi-tools/repack-wf.sh <sagit|chiron> <boot.img>`
  （WSL 上跑，临时树在 ext4 的 /tmp/rdf，把设备树当前的 init rc + 5 个脚本 + 3 个静态
  二进制替换进已构建镜像的 ramdisk；带 ramdisk 体积与文件完整性双重 guard，避免
  "cpio 静默跳过 → 生成 55 字节 ramdisk → 变砖"这类事故）。它只是快捷方式，
  构建路径仍是第 10 节的 docker + make bootimage。
- KitKat 坑：**没有 nohup / pidof / sed / tail / head / wc / awk / setsid / tr**；
  export LD_LIBRARY_PATH 会污染 toolbox（libcutils.so 32/64-bit），只能给单个二进制前缀赋值。
- **这三个 vendor 二进制不能直接做 init service**：被 init 直接 exec 会在 ~3s 内静默退出，
  同一命令行在 shell 里能长期存活；所以用 keeper 脚本代管。
- 所以 init 里那个 `service wf-irsc /system/vendor/bin/irsc_util ...`（即使带 setenv）
  **从来没生效过**，这也正是 IRSC completion 一直没人 fire 的原因。现在由 ramdisk
  `/sbin/wf-setup.sh` 里的静态 `wlq irsc` 承担（第 6/8 节），不依赖 LD_LIBRARY_PATH，
  也不依赖 init 能 exec vendor 二进制。

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
2. sagit 实测：栈已经同步、两个产品都走同一份构建（第 10 节），但 **sagit 手机没连上，
   还没在它上面跑过一次冷启动**。sagit 与 chiron 的已知差异只有 blob 层面：
   by-name 分区号相同（modemst1=sdf4、modemst2=sdf5、fsc=sdf1、fsg=sde34），
   WCN3990/icnss/内核路径相同，vendor 守护与 64 位库是通用 QC 代码；唯一可能板级相关的是
   board data，现在用一份 bdwlan.bin 复制成 bdf_c2/bdf_d5/bdf_d5s/bdwlan 四个名字铺满
   cnss 的搜索列表，所以 sagit 不需要另一份 BDF 也能命中（真机上要看 cnss 选中的 chip id）。
3. 可选：把 Settings 里 WiFi 开关的 UI 反馈也测一遍（本轮验证走的是 `svc wifi`，
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
   构建：`wifi-tools/build-wf-bins.sh`（一次编出 wlq/rfssrv2/sm-vndk 三个，产物 stage 进
   out-of-git 的 `vendor/xiaomi/sagit-bin/bin/`，由两份 mk 装进 /system/bin）。
2. `full_chiron.mk` / `full_sagit.mk`：BDF 变体安装（根因 A）+ `$(BIN_DIR)/bin/{wlq,rfssrv2,sm-vndk}`
   条件安装 + `sec_config`、vendor 守护与 64 位库闭包，并写全 IRSC 根因注释
   （第 1 节和第 10 节）。

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

- `devices/sagit/kitkat/wf-setup.sh` 末尾（post-fs-data，~4s）把 `/data/misc/wifi/wpa_supplicant.conf`
  锁成 `root:root 0600`。`wifi_start_supplicant()` 在 **碰 init 之前** 先调
  `ensure_config_file_exists()`（`hardware/libhardware_legacy/wifi/wifi.c:502-505`），
  该函数发现文件存在但 uid system 不可读写时会尝试 `chmod` 并由 EPERM 失败返回 -1
  （同文件 407-413），于是 wifi.c 打 `Wi-Fi will not be enabled`、Java 打
  `Failed to start supplicant!`，**根本不会启动 supplicant，也不会进 startMonitoring**，
  状态机停在 InitialState、`mIfaceMap` 完好。
- `devices/sagit/kitkat/wf-wifi-gate.sh`（init oneshot 服务）等 `/sys/class/net/wlan0` 出现 →
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

## 10. 构建路径与不进 git 的 blob（sagit/chiron 共用，2026-10-27 起）

chiron 之前能跑，靠的是手工 push 上去的一堆文件（vendor 守护、lib64、sec_config、
/data/local/tmp 里的脚本与二进制、只读 /system 上手工搭的 rfs 树、image/wlanmdsp.mbn），
换一台机器或刷一张干净镜像就全没了。现在这些都由 `full_sagit.mk` / `full_chiron.mk`
安装，构建流程：

```
# WSL 上，kitkat-build 容器里（JDK6 只在容器里有，host 上 make 会死在 config.mk:365）
MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu-20.04 -u root -- docker exec kitkat-build bash -c 'cd /srv/aosp-kitkat && . build/envsetup.sh && lunch full_chiron-eng && make -j$(nproc) bootimage systemimage'
```

同步设备树（容器挂载的就是 `/home/juezhi/aosp-kitkat` → `/srv/aosp-kitkat`）：
`AOSP_DIR=/home/juezhi/aosp-kitkat scripts/sync-device-tree.sh chiron`。

产物：`out/target/product/<dev>/boot.img`（ramdisk 里是 init.<dev>.rc + /sbin 那五个脚本）
和 `system.img`（需 `out/host/linux-x86/bin/simg2img` 转成 raw 再 dd）。
2026-10-27 00:08 的 chiron 构建已核对：`root/sbin/{wf-setup,wf-efs-keeper,wf-wifi-gate,rfs-dirs,modem-hold}.sh`、
`system/bin/{wlq,rfssrv2,sm-vndk}`、`system/vendor/etc/sec_config`、
`system/vendor/bin/` 5 个守护、`system/vendor/lib64/` 31 个库、`system/lib64/` 8 个 64 位 bionic、
`system/vendor/firmware/image/{bdf_c2,bdf_d5,bdf_d5s,bdwlan,wlanmdsp.mbn}` 全部到位。

git 里没有、必须自己准备的（`scripts/sync-device-tree.sh` 的 `--delete` 不会碰它们，
因为都在 `vendor/xiaomi/` 的兄弟目录里）：

| 目录 | 内容 | 怎么来 |
| --- | --- | --- |
| `vendor/xiaomi/sagit-bin/bin/` | `wlq` `rfssrv2` `sm-vndk`（自研静态二进制）+ `fbpresentd` | `wifi-tools/build-wf-bins.sh`（sm-vndk 需要先有一次 AOSP 构建，用 patch-sm-vndk.py 改 servicemanager） |
| `vendor/xiaomi/sagit-wifi/` | `bin/` 5 个 QC 守护、`lib64/` 31 个库、`system-lib64/` 8 个 bionic、`linker64` | `wifi-tools/stage-wifi-vnd.sh`（在 Windows 上跑，从一台还跑着原厂固件的同类手机 adb pull；KitKat 机上取不到，所以这份没法自举） |
| `vendor/xiaomi/sagit-fw/` | `adsp/` `modem/` `wlan/` `audio/` 固件 |  modem 分区 + persist + 原厂 ROM dump（沿用 sagit 那批） |
| `vendor/xiaomi/sagit-hyb/` | A15 vendor 闭源包 + ACDB | `tools/libhyb/stage.py` |
| `vendor/xiaomi/{sagit,chiron}/kernel` | 4.4 内核 `Image.gz-dtb` | 见本节末尾：**两台都换成了 LOS 17.1 KernelSU 的 Image.gz-dtb**，不再是原厂 boot.img 里那份 |

两个 mk 里这些都包在 `ifneq ($(wildcard ...))` 里：blob 没 staged 时构建照过，只是镜像里没有
WiFi 栈——所以刷完不起 WiFi 时，先确认上表五个目录都在，再怀疑代码。

**核对产物用 `wifi-tools/check-bootimg.py`，别信 boot header 的三段切分。**
msm8998 上不管是原厂 boot.img 还是 `make bootimage` 出来的，header 里的 ramdisk
尺寸字段都是 32768（一页对齐的占位），真正的 gzip ramdisk 流一直写进下一个槽位
（chiron 340165、sagit 340956）。照 AOSP 的 kernel/ramdisk/second 去切，gunzip 只
会得到 49603 字节然后 `premature end of file`，看着就像"ramdisk 被截断、构建坏了"；
从 ramdisk 偏移一路解到文件尾才是完整 cpio：33 条目，
`sbin/{wf-setup,wf-efs-keeper,wf-wifi-gate,rfs-dirs,modem-hold}.sh` + `init.<dev>.rc`
全在（2026-10-27 两个产品各自核对）。

**内核：两台都用 LOS 17.1 KernelSU 的 `Image.gz-dtb`，不用原厂那份。**
chiron 用树里原厂 stock 内核做出来的 boot.img 一直重启，换成
`C:\mCloudDownload\los17.1 -KernelSU.zip`（`anykernel.sh: device.name1=chiron`，
14594254 B，md5 `db252ba31495dceda3e6c6ebadd4ed9b`）才能开机，这就是现在
`vendor/xiaomi/chiron/kernel`。sagit 同日同处理：
`C:\mCloudDownload\lineage17.1_ksu.zip`（`device.name1=sagit`，15774877 B，
md5 `f32b9017cfa4b8a441bcae49a6758453`），换完**仍要**跑
`scripts/patch-sagit-dtb.py` —— 它在 6 个 DTB 里都命中并删掉了
`qcom,mdss-idle-power-collapse-enabled` 和 `qcom,wcn-btfm`（15774877 → 15774349 B）。
chiron 没有对应的 patch 脚本，直接拿来用。换下来的 sagit 原厂内核留在
`/home/juezhi/imgs/sagit-kernel-ba7ca7ab188114335ef58b217285ae61.bin`。

## 11. 干净镜像冷启动实测（2026-10-27）与两个"只有全新镜像才会暴露"的坑

第 10 节把 WiFi 栈交给构建之后，第一次刷干净镜像 WiFi 完全不起来：没有
`wlan0`，`/sys/kernel/debug/icnss/stats` 里 WLFW 计数全 0、`SERVER_ARRIVE 0`、
`root_pd_crash` 一直往上加。挖出来是两个独立的坑，都属于"手工 push 时代被顺手补
上、但从来没进设备树"的那类。

**坑 1：`/system/bin/linker64` 没人装。** 那 5 个 QC 守护是 aarch64 ELF，
`PT_INTERP` 写死 `/system/bin/linker64`，而这棵树只编 arm（`TARGET_ARCH=arm`），
所以镜像里根本没有这个文件。bionic 在找不到 interpreter 时报的是
`No such file or directory`，看上去像"二进制没刷进去"，很容易查错方向：

```
/sbin/wf-efs-keeper.sh[40]: /system/vendor/bin/rmt_storage: No such file or directory
```

修：blob 目录里补一个 `sagit-wifi/linker64`（和那 8 个 64 位 bionic 同一代），
`full_sagit.mk` / `full_chiron.mk` 装到 `system/bin/linker64`，
`stage-wifi-vnd.sh` 一起 `adb pull /system/bin/linker64`。

**坑 2：`/dev/block/bootdevice/by-name` 从来没建出来。** 老代码是
`mkdir -p /mnt/vendor /dev/block/bootdevice/by-name`，而 KitKat 的 toolbox mkdir
**遇到第一个失败的操作数就整体退出**（`/mnt` 是只读 ramdir，`mkdir failed for
/mnt/vendor/, Read-only file system`，返回 255），第二个操作数根本没被执行 ——
手工 push 那阵子是 TWRP/ueventd 已经把 by-name 建好了，所以掩盖了这个问题。
后果在 `rmt_storage`：它把错误写进 `/dev/kmsg`（stderr 一个字都没有，只有
linker 那两行 warning），所以只在 dmesg 里看得到：

```
rmt_storage:ERR:parse_gpt_partition: Failed to open partition /dev/block/bootdevice/by-name/modemst1 err 2 (No such file or directory)
```

它退出码 1 → 没有 EFS/TFTP 服务 → modem 回到 ~41 s 自杀循环 → root PD 一直Down，
`icnss` 连一个 WLFW server 都等不到（`SERVER_ARRIVE 0`），wlan0 不可能出现。

修：这个内核的 ueventd 其实已经把整套 by-name 建在上一层
（实测 `/dev/block/platform/soc/by-name` 里 modemst1=sdf4、modemst2=sdf5、
fsc=sdf1、fsg=sde34、persist=sda14，和 sagit 原厂 dump
`dumps/sagit-stock-20261024-014254/by-name-bootdevice.txt` 完全一致），缺的只是
`androidboot.bootdevice` 正常情况下会创建的那个别名，所以
`wf-setup.sh` 现在只做 `ln -s /dev/block/platform/soc /dev/block/bootdevice`，
不再自己拼目录；`rfs-dirs.sh` 里的多操作数 `mkdir -p` 也一律拆成一行一个。

**顺带记一条排查纪律**：`rfssrv2` 不能在开机后手工杀掉再重启。modem 的 RFSA client
只在一次 life 里注册一次、不重试，杀掉之后它就再也找不到 AP 侧的 RFS 服务；实测这样
折腾一次，`crash_count` 从 9 直接冲到 646，那次 boot 就废了，只能 reboot 重来。

最终这一版（`boot.img` ksize 14594254 + `simg2img` 转出来的 3 GiB raw
`system-raw.img`）真实冷启动、零手工命令：

| 项 | 结果 |
| --- | --- |
| `/dev/block/bootdevice` | -> /dev/block/platform/soc（由 /sbin/wf-setup.sh 建） |
| 守护 | `rfssrv2`、`rmt_storage`（drop 到 nobody）、`cnss-daemon` 三个都在 |
| icnss | `FW_READY 1/1`、`SERVER_ARRIVE 1/1`、`ind_register_req 1`、`root_pd_crash: 0` |
| State | `0xd8f`（QMI CONN \| POWER ON \| FW READY \| DRIVER PROBED \| SSR \| PDR \| MSA0 ASSIGNED \| WLAN FW EXISTS） |
| 网卡 | `wlan0` / `wlan1` / `p2p0`，MAC 00:0a:f5:6c:c9:98 |
| framework | `dumpsys wifi`：Wi-Fi is enabled、Supplicant state COMPLETED、SSID "Xiaomi 15"、RSSI -41、Link speed 866 |

一个踩过的操作坑：`make systemimage` 出来的 `out/target/product/<dev>/system.img`
是 **sparse**（文件头 magic `0x3aff26ed`），必须先
`out/host/linux-x86/bin/simg2img` 转 raw 再 `dd`；直接 dd sparse 文件的结果是
`/system` 挂不上、卡在 Mi logo、USB 完全不枚举（看着像又碰上了 kernel 的 panic 循环）。

## 12. 把这套栈搬到预构建 ROM 上：sagit 的 MIUI V5（2026-10-27）

sagit 刷本 port 的干净 AOSP 镜像后 rec 读不到 /sdcard，改跑能用的 MIUI V5
（cancro 4.4.2，`androidboot.hardware=sagit`，ramdisk 就是本仓库那份
`init.sagit.rc` + MIUI 自己的行）。V5 的镜像是成品 ext4 / 成品 boot.img，没有
构建可走，所以第 10 节那套"由 mk 安装"的东西全部改成手工安装。脚本在
`/home/juezhi/mi5-wifi/`（和 `miui5-chiron-pack.sh` 一样放仓库外，仓库只跟 AOSP 设备树）：

| 脚本 | 干什么 |
| --- | --- |
| `inject-v5-system.sh <mnt>` | 把 `full_sagit.mk` 的 62 个 WiFi 文件按同样的目标路径装进挂载好的 system（**只在 WSL ext4 上改**，别动 C: 那份） |
| `verify-v5-wifi.sh <mnt>` | 逐个 md5 对比 `out/target/product/sagit/system`，2026-10-27 结果 62 files / 0 mismatches |
| `repack-v5-boot.sh [base] [out]` | 换 ramdisk、保留 kernel，写新镜像 |
| `hwl-*.sh` / `hwl-wifi-addr.py` | 下面那个 `libhardware_legacy` 坑的取证脚本 |

两条和 `repack-wf.sh` 不一样的规矩：

1. **只追加，不整份替换 `init.sagit.rc`。** V5 那份里有 MIUI 开机必需的行
   （`/data/misc/location*` 一组 mkdir，缺了 QUIPC/ULP 直接 abort system_server；
   `service checkpath ... miuioneshots` 建 miuisdk.jar 软链；`miui-shell`），
   `repack-wf.sh` 的 wholesale 替换会把它们抹掉。所以从 `# ---- WLAN bring-up stack`
   那行起截到 EOF 追加，脚本还会 assert 这四段都还在。
2. **新 ramdisk 长度写 header 偏移 16**（就是第 10 节说的"下一个槽位"），
   偏移 12 那个 32768 占位不动；kernel 段原样 `src[page:page+kn]`，尾部补齐到页。
   `repack-v5-boot.sh` 动手前先 assert 文件尾到 ramdisk 结束正好是 0 字节，
   有 second image / DTBO 的镜像会被它拒绝，免得静默丢掉一段。
   `/sbin` 只放 4 个脚本：`wlq rfssrv2 sm-vndk` 走 `/system/bin`，
   `wf-setup.sh` 的 `find_bin()` 本来就是 /sbin → /system/bin → /data/local/tmp。

**最大的坑：V5 自带的 `libhardware_legacy.so` 让开关根本打不开。** 它是编译时
定义了 `WIFI_DRIVER_MODULE_PATH` 的 cancro 版本，`wifi_load_driver()` 的顺序是
`is_wifi_driver_loaded()`（要 `wlan.driver.status=ok` **且** `/proc/modules` 里有
`wlan ` 开头的行）→ 不满足就 `insmod /system/lib/modules/wlan.ko`。这台机器上
qca_cld3 是**编进内核**的（V5 boot.img 里那份 Image.gz-dtb 解开能 grep 到
`wlan_hdd_cfg80211_start_ap` 这类符号，`/proc/modules` 永远不会出现 wlan），
msm8974 的 `pronto_wlan.ko` 更不可能加载 —— 三条路全断，
`wifi_load_driver()` 返回 -1，framework 直接拒绝 enable。本 port 自己编的那份没定义
这个宏，走 `property_set("wlan.driver.status","ok"); return 0`，正是 chiron 上验证过的行为。
换上它同时把 V5 的震动从 Mi 3 的 Immersion 引擎（`ImmVibe*`）切到
`/sys/class/timed_output/vibrator/enable`，和 `miui5-chiron-pack.sh` 1f 节是同一个替换；
换之前扫过 V5 的 `/system/{lib,lib/hw,bin,xbin}`：111 个"只有 MIUI 版才导出"的符号
没有任何组件引用（`ImmVibe*`、`qmi_nv_read_wlan_mac` 都是死代码），所以链接面是安全的。
原版备份 `/home/juezhi/mi5-wifi/backup/libhardware_legacy.so.miui`。

顺带核对过、不需要动的：V5 的 `wpa_supplicant` 自带 nl80211（318 处字符串），
DHCP 用镜像里的 `dhcpcd`；`/system/etc/wifi/wpa_supplicant.conf` 写的是
`ctrl_interface=wlan0`，被 init 服务行的 `-O/data/misc/wifi/sockets` 覆盖，所以
**每接口** socket 落在 `/data/misc/wifi/sockets/wlan0` —— 但 framework 走的不是它，
见 §13 第 1 条（global 控制口 `/dev/socket/wpa_wlan0`）。

**产物（当时的中间版，最终版见 §13；`boot-mi5-wifi3.img` 与下面那个
md5 `82a4e600…` 的镜像逐字节相同，就是去掉 `-O` 改写之后的结果）**：
`C:\download\chiron\system_miui5_wifi.img`
md5 `e7b5d4e9758d6ccb680e211b4d77c343`（1 GiB raw，e2fsck clean，20% 用量）、
`C:\download\chiron\boot-miui7-ksu2-v5fix-wifi.img` md5 `82a4e600a294fd7e7d5c436990ea4682`
（16125952 B，ramdisk 333639 → 341710，33 条目含 4 个 `wf-*` 脚本）。
sagit 现在 adb 不枚举（Windows 认到 `VID_2717&PID_FF48`，`adb devices` 空），
只能 TWRP 刷 + 屏上 dmesg 验。另注：这张 V5 boot 用的还是**没跑过
`scripts/patch-sagit-dtb.py`** 的内核（15774877 B），两个 DTB 补丁跟 WiFi 无关，
但 V5 如果出现"静止画面要摸一下才刷新"或声卡不注册，就是这个原因。

## 13. sagit MIUI V5 的 WiFi 真正打通：三条根因（2026-10-27 晚）

现象是"开关打不开 / 能搜到但连不上"。三条根因互相独立、互相掩盖，全部固化进
`boot.img` + `system.img`，真实冷启动零手工命令即可连接。

**1. HAL 连的是 supplicant 的每接口控制口，而 MIUI 的命令带 `IFNAME=` 前缀。**
只有 `wpa_supplicant_global_ctrl_iface_process()`（
`external/wpa_supplicant_8/wpa_supplicant/ctrl_iface.c:6194`，`os_strncmp(buf,"IFNAME=",7)`
在 :6202）会解析 `IFNAME=<iface> <cmd>`；每接口那份
`wpa_supplicant_ctrl_iface_process()` 没有这段，一律回 `UNKNOWN COMMAND`。
把命令改投 global 口之前那份 HAL 探针日志（`C:\tmp\ss-probe.txt`，403 行）里
supplicant 回了 45 次 `UNKNOWN COMMAND`。
V5 的 supplicant 由 init 服务行的 `socket wpa_wlan0 dgram 660 wifi wifi` 拿到 fd，
`-g@android:wpa_wlan0` 在这个版本里绑的是**文件系统**名 `/dev/socket/wpa_wlan0`
（`/proc/net/unix` 只有这个，abstract 名从未出现），所以把
`hardware/libhardware_legacy/wifi/wifi.c` 的 `IFACE_DIR` 指到 `/dev/socket` 并按
`wpa_<iface>` 拼路径，就是 stock 4.4 本来的行为；`-O`（每接口 socket 目录）保持
MIUI 原样，不用改。

**2. `wifi.interface` 在 V5 上根本没设。** `wifi.c:530` 是
`property_get("wifi.interface", primary_iface, WIFI_TEST_INTERFACE)`，而
`WIFI_TEST_INTERFACE` 是 `"sta"`（模拟器默认值）。本 port 的两份 AOSP 镜像靠
`devices/<dev>/kitkat/system.prop` 提供 `wifi.interface=wlan0`，MIUI V5 的 build.prop
没有这一条 → `primary_iface=sta` → 打开 `/dev/socket/wpa_sta` → 5 次重试全失败 →
`WifiMonitor: startMonitoring(wlan0) failed!` → `mIfaceMap.remove(wlan0)` → 此后任何
enable 只会打 `startMonitor called with unknown iface=wlan0`，正是第 9.2 节那条永久
wedge。修：ramdisk 的 WLAN 块在 `on post-fs-data` 里 `setprop wifi.interface wlan0`
（sagit 与 chiron 两份 init rc 同步加，两块 diff 一致）。这条修完，V5 的 framework
开机自己就连上了 —— 第 9.3 节那套"补一次 off→on"在 V5 上不再需要，gate 只在
`wifi_on=1` 且框架没连上时才会动作。

**3. `WCNSS_qcom_cfg.ini` 是个 20 行的残档，Rx 路径整个是死的。** 症状精确对应
"能扫到、能关联、能 4 次握手，但一个数据帧都收不到"：
`/sys/class/net/wlan0/statistics/rx_bytes` 停在几百字节，`Supplicant state: COMPLETED`
却拿不到 DHCP，框架超时后自己断开（`reason=3 locally_generated=1`）。qca_cld3 的 Rx
处理方式由 INI 决定（`rx_mode`：1=Rx_thread、2=RPS、4=NAPI），chiron 跑通那份是
`rx_mode=4` + `gIPAConfig=0x7d` + `gEnableFlowSteering=1` + `ce_service_max_yield_time=500`
+ `maxMSDUsPerRxInd=8` 一整套，而 sagit 只写了 `gIPAConfig=0`。现在 sagit 的 INI 换成
chiron 同款（606 行，md5 `771c3e398d0f98da3f5710b66ec9b007`），设备树与 out-of-git 的
`vendor/xiaomi/sagit/wifi/` 两份同步；AOSP 侧也重跑了一次 `make systemimage`，所以
`out/target/product/sagit/system` 与 V5 树里那份逐字节相同。

**只在干净镜像上才看得见的三条**：手工 push 那阵子 `/system` 是脏的，
`wifi.interface` 被临时 `setprop` 掩盖、INI 是旧的、HAL 是 trace 版 —— 任何一条都能
单独让人误判另外两条。另外 **判断 V5 的 WiFi 之前一定要先点亮屏幕**：屏灭时 MIUI 不发
扫描请求，`Supplicant state` 停在 INACTIVE，看着就像 HAL 又坏了（本轮我据此误判过一次）。

V5 上取证的两条手段：
- 原生 `libhardware_legacy`/`WifiHW` 的 ALOG 到不了 kmsg（没有 logd），改用 per-pid
  文件探针 `/data/misc/wifi/wfdbg.txt-<pid>`（0666，system 与 root 都写得动）。
  java 侧不同：MIUI V5 的 framework 走 liblog 的 kmsg fallback，`adb shell dmesg`
  里带 `<15>`/`<14>` 前缀就能看到 `WifiService`/`WifiMonitor`/`WifiStateMachine`。
- 判定 attach：`startMonitoring(wlan0) with mConnected = false` 之后**没有**
  `failed!` 就是连上了；`init.svc.wpa_supplicant` 属性可以完全不存在 —— V5 走的是
  p2p（`hasSystemFeature(FEATURE_WIFI_DIRECT)` → `startSupplicant(1)`），跑的是
  `p2p_supplicant`，`wlan0` 和 `p2p0` 在同一个进程里。

**最终产物（上机两次冷启动验证，均为刷镜像而非 push 文件）**

| 镜像 | 字节 | md5 |
| --- | --- | --- |
| `boot.img`（ramdisk 341990） | 16125952 | `ee2234588c8b53c5bfe9ec55b6662b97` |
| `system.img`（1 GiB raw ext4，e2fsck clean） | 1073741824 | `2014196202b6bd13258cae04ab73d503` |

打包：Windows `C:\download\chiron\sagit-v5-wifi-20261027\{boot.img,system.img,FLASH.md}`，
WSL `~/mi5-wifi/out/{boot.img,system.img,MD5SUMS}`。刷的是裸块：
boot=`/dev/block/sde40`、system=`/dev/block/sda18`（这个 recovery 不建 by-name）。
冷启动实测：`wifi.interface=wlan0`、`init.svc.p2p_supplicant=running`、51.9s
`startMonitoring(wlan0)` 一次成功、`Wi-Fi is enabled` + `Supplicant state: COMPLETED` +
SSID "Xiaomi 15" + 866 Mbps、`wlan0=10.237.226.237/24`（DHCP）、rx 计数器持续上涨、
`ping 8.8.8.8` 通；`svc wifi disable` → `Wi-Fi is disabled`，`svc wifi enable` →
重新 COMPLETED 且保持原租约，用户侧开关双向可用。

## 14. 时钟：预构建 ROM 为什么永远同步不上时间（2026-10-27 深夜）

现象：刷好的 V5 里时间停在 1971 年，NTP 同步点了没反应，日期一改就跳回去。

三条根因，前两条在**内核**、第三条在**镜像内容**：

1. **这个内核没有 android_alarm，所以没有 `/dev/alarm`。** KitKat 原版
   `SystemClock.setCurrentTimeMillis()`（`frameworks/base/core/jni/android_os_SystemClock.cpp`）
   只认这一条路 —— 打开 `/dev/alarm` 写 alarm struct 再 settimeofday；打不开就直接放弃。
   所以 system_server 里能看到 `NetworkTimeUpda` 线程在 184 s 真的跑了一轮 NTP，
   最后那一步写钟却被丢掉，dmesg 里配一句 `Unable to open alarm driver: No such file or directory`。
   我们自己的 AOSP 树是靠 `patches/kitkat/frameworks-base-time-without-dev-alarm.patch`
   （ZygoteInit 里补 `CAP_WAKE_ALARM`、SystemClock 直接 settimeofday 并写
   `/data/system/time/rtc_offset`、AlarmManagerService 换 timerfd/epoll）绕开的，
   预构建 ROM 没法打这个补丁，只能换一个能写钟的人：**root 侧自己写**。
2. **PM8998 的 `qpnp_rtc` 在 sagit 的 DT 里没有 `allow-set-time`。** RTC 是只读的，
   它只是个从上次掉电点继续数的裸秒计数器 —— 实测
   `/sys/class/rtc/rtc0/since_epoch = 35337894`，也就是 1971-02-14。开机时框架拿到的
   "现在"就是这个值，写不进 RTC 意味着下一次冷启动还是它。
3. **V5 镜像里根本没有 `rtc-restore`。** 这个 port 的开机恢复路径（`on post-fs-data` 里
   `start rtc-restore`，读 `/data/system/time/rtc_offset`）在 ramdisk 里一直在，
   但那个二进制从来没进过任何预构建 system 分区 —— 属性在、服务定义在、文件不在。

落地的东西（全部在 `devices/sagit/kitkat/`，chiron 共用同一条 init block）：

- `time-sync/time_sync.c` + `Android.mk` → `/system/bin/time-sync`。SNTP 按 RFC 4330 自己拼
  48 字节包（`pkt[0]=0x1b`，回复取 offset 40 的 transmit timestamp，减 2208988800），
  依次试 `ntp.aliyun.com`、`cn.pool.ntp.org`、`2.cn.pool.ntp.org`、`time.android.com`、
  `pool.ntp.org`、`time.windows.com`；全失败退到 HTTP `Date:`（TCP/80，`HEAD /`，
  自己用 days_from_civil 解析，因为 KitKat bionic 里没有 `timegm`），主机
  `www.baidu.com`/`www.taobao.com`/`www.qq.com`/`www.apple.com`。成功后 `settimeofday()`，
  再把 `wall - since_epoch` 写 `/data/system/time/rtc_offset`（目录 0770 root:system，
  文件先 `.tmp` 再 rename，0660 root:system）—— **路径与格式和 AOSP patch 那份完全一致**，
  所以 `rtc-restore` 不用改就能复用。
- `time-sync.sh` → ramdisk `/sbin/time-sync.sh`，只管"什么时候跑"：轮询 DHCP 租约，
  拿到就跑，成功睡 12 h，失败 2 min 后重试。
- 两个 init rc 在共享的 `# ---- WLAN bring-up stack` 段里加 `start time-sync`，EOF 加
  `service time-sync`（class main / user root / group root / disabled）。必须写在这段**之后**：
  `repack-v5-boot.sh` 只把该段到 EOF 追加进 V5 ramdisk，写在外面的服务到不了预构建 ROM。
- `inject-v5-system.sh` 顺带把 AOSP 造好的 `rtc-restore` 和 `time-sync` 拷进 V5
  `/system/bin`（缺一个就 FATAL）；`full_sagit.mk`/`full_chiron.mk` 补
  `PRODUCT_PACKAGES += time-sync rtc-restore` 和 `root/sbin/time-sync.sh` 的 copy 规则。

V5 上取证的两条坑（跟 WiFi 那轮一样）：
- **`getprop dhcp.wlan0.ip` 永远是空的。** 这个属性真名是 `dhcp.wlan0.ipaddress`，
  `getprop` 只做精确匹配、不做前缀匹配，所以脚本写错就永远等不到租约（我第一版就写错了）。
- **我们的原生二进制 ALOG 到不了 kmsg**（没有 logd），所以 `time-sync` 同时 `printf` 到
  stdout，由 `time-sync.sh` 把输出重定向进 `/data/local/tmp/time-sync.log`；
  框架 java 侧不同，`NetworkTimeUpdateService` 那类日志在 `dmesg` 里带 `<14>`/`<15>` 前缀能看到。
- UDP/123 从 WSL 发不出去（超时），从手机却一次成功 —— 不要把 PC 上的失败当成手机上的失败。

实测（都是刷镜像、不是 push 文件）：

- 手工三种：`/system/bin/time-sync` →
  `clock set to 2026-10-27 15:08:56 UTC from ntp.aliyun.com, rtc offset 1757773423 saved`，
  `exit=0`；`--http` 走 `www.baidu.com` 得到**同一个** offset；`--set` 故意把钟打到
  2020 之后 `rtc-restore` 能按新 offset 拉回（注意：`--set` 自己也会重写 offset，
  所以"打完再 restore"这种测法是自证的，要测就测 `--set` → 再 sync → offset 回到原值）。
- 冷启动 25.1 s、网络还没起来时 `date` = `Tue Oct 27 23:01:30 CST 2026`，
  `init.svc.rtc-restore=stopped`（oneshot 已跑完）、`init.svc.time-sync=running`、
  `wifi.interface=wlan0` —— 开机恢复这条路是通的。
- 这一轮冷启动 WiFi 在 238 s 还停在 `Supplicant state: INACTIVE`（屏是亮的），
  `svc wifi disable` → `enable` 一次才 `COMPLETED`；和 §13 那次"全自动 attach"不一样，
  说明 V5 的开机自动重连有随机性。time-sync 不依赖它：没租约就一直每分钟重问，
  网络补上后那一轮自己就跑起来了。

V5 成品：`boot-mi5-wifi5.img`（md5 `c3f314881a7322b436ce0cf596c7e321`，ramdisk 343132）+
`system_miui5_wifi3.img`（md5 `585504392664bcfa7f1a6de4df25696a`）是取到上面那组数据的那一对；
`boot-mi5-wifi6.img`（md5 `0ef956599e35a985ccc83b8131e3555c`，ramdisk 343159，sha256
`278428bfbc06c880439fab65086a814b9220d75d5d74e1d29790d0415031b4d8`）只改 `/sbin/time-sync.sh`
的 `ipaddress` 与重试逻辑，已刷进 `/dev/block/sde40` 并按字节回读核对（md5 一致），
但它的**第一次网络同步还没在机上跑完** —— 刷完重启后 Windows 侧 `adb devices` 就再也列不出
设备（PnP 里 `USB\VID_18D1&PID_4EE7\SAGITKK` 一直是 OK），这一条要等下次连上补。
打包在 `C:\download\chiron\sagit-v5-time-20261027\{boot.img,system.img,FLASH.md}`，
WSL 副本在 `~/mi5-wifi/out/`。

AOSP 侧同一轮也重建了（`PRODUCT_PACKAGES` + `root/sbin/time-sync.sh` 那条 copy 规则生效）：
sagit `boot.img` md5 `416450f4def1dcc1a417e214a7bf823c` / `system.img`
`9b212ea55b02faade1d934cf6b39e2c4`；chiron `boot.img` `41f052d888abb84df33e43b6a790ff4b` /
`system.img` `a298c8faf62d882f75c491f3e25d9c89`；两边 `out/target/product/<dev>/system/bin/`
里 `time-sync` md5 `8f13eeb9fdc4b95443a3bbfdc912d529`、`rtc-restore` md5
`53904e5aaeab9e6eebb689a9f984044b`，`root/init.<dev>.rc` 各含 4 处 time-sync。这两个还没上机。

**构建侧的一个坑（本轮踩到）**：`scripts/sync-device-tree.sh` 用的是 `rsync -a`，会保留源文件
mtime。改完 `devices/sagit/kitkat/time-sync.sh` 再 sync 进 `vendor/xiaomi/sagit/`，那个文件的
mtime 仍是改动时刻，比 `out/.../root/sbin/time-sync.sh` 里上一次拷进去的那份**更旧**，于是 make
认为目标已是最新、直接跳过 —— sagit 的 boot.img 里就还是旧脚本（1958 B），而没建过的 chiron
拿到的是新的（1978 B）。判断依据只能是 `stat -c '%s %Y'` 两边的字节数与 md5，不能信"build 成功
且镜像里确实有这个文件"。修法：`touch vendor/xiaomi/sagit/time-sync.sh` 再
`rm out/target/product/sagit/root/sbin/time-sync.sh`，然后 `make bootimage`，日志里要能看到
`Copy: out/.../root/sbin/time-sync.sh` 这一行。

## 15. 呼吸灯：把 chiron 的那套修复搬到 sagit 的 V5 上（2026-10-28）

现象：V5 里来通知、充电闪灯，LED 一直是灭的。chiron 的 V5 早就正常（仓库 commit
`0ec68b5` "fix notification LED on msm8998 (sagit + chiron)"，当时在 chiron 上实测会呼吸），
sagit 这边缺的只是"预构建 ROM 也要吃到这两半"。

两条根因，和 §14 一样是**两块都要修**：

1. **V5 自带的 lights 模块驱动的是根本不存在的节点。** 这份 ROM 只带 cancro（Mi 3）那套
   `lights.msm8974.so`，写 `/sys/class/leds/{red,green,blue}/brightness`。msm8998 上
   PMI8998 的 RGB **只有红通道**接了通知灯，而且 DTS 把它改名成 `white` —— 真实节点是
   `/sys/class/leds/white/{brightness,trigger,blink}`（`max_brightness` 255，recovery 里量的），
   red/green/blue 三个目录这个内核不会建出来。
2. **LED 的 sysfs 属性是 `root:root 0644`。** LightsService 跑在 system_server 里、uid 是
   system，所以每一笔写都是 EACCES —— 属主没修之前，HAL 写得再对也不会亮。MIUI 自己的
   `init.rc` 只 chown red/green/blue（同样是不存在的节点），等于没做。

两半各自落在打包脚本里：

- **system 半边**（`inject-v5-system.sh`）：把 AOSP 造好的 `lights.sagit.so` 同时装成
  **两个名字** —— `lights.sagit.so`（`ro.hardware=sagit` 先解析到的）和 `lights.msm8974.so`
  （V5 里本来就在、而且内容是错的那一份）。只加新名字不换旧名字，HAL 管理器仍可能拿到
  cancro 的。装之前两道 FATAL：文件存在，且 `grep -a 'leds/white/trigger'` 命中（防止装到
  `0ec68b5` 之前的旧 build）；`verify-v5-wifi.sh` 再断言两个文件 md5 相同。
  chiron 的 `miui5-chiron-pack.sh` §1g 一直是这个做法（`lights.chiron.so` +
  `lights.msm8974.so`），sagit 只是把同一对写死成自己的两个名字。
- **boot 半边**（`repack-v5-boot.sh`）：chown 那一段在 `init.sagit.rc` 里位于
  `# ---- WLAN bring-up stack` 标记**之前**，而该脚本只把标记到 EOF 追加进 V5 ramdisk，
  所以必须单独切：`sed -n '/^# LightsService writes these/,/leds\/wled\/brightness/p'`。
  幂等（V5 rc 里已经有 `leds/white/trigger` 就跳过），切不出来就 FATAL 让打包失败。
  五条 chown：`lcd-backlight/brightness`、`white/trigger`、`white/brightness`、`white/blink`、
  `wled/brightness`。**它们必须自己占一个 `on boot` 段** —— init 里一条 chown 打到缺失节点会
  中断该 action 剩下的全部语句。

HAL 里那条最容易写反的次序（`0ec68b5` 的实质内容，AOSP 和 V5 共用一个 `lights.c`）：

- `blink` **不是**"亮 ms / 灭 ms"，qpnp 驱动把它当 1/0 开关：1 = 跑 DTS 里
  `qcom,use-blink` 那条 8 步 / 50 ms 斜坡 LUT（呼吸），0 = 回到普通 PWM 常亮。
- **brightness 必须在 blink 之前写**：`qpnp_rgb_set()` 会清掉 `pwm_cfg->blinking`，
  先 `blink 1` 再写 brightness 等于亲手把图案取消。
- 写之前先把 `trigger` 置 `none`：这个通道注册在 `battery-charging` trigger 上，
  trigger 占着 `brightness_set`，框架的写会被吞。

KitKat 框架侧（`frameworks/base/services/java/com/android/server/LightsService.java`）：
`LIGHT_ID_NOTIFICATIONS = 4`，`flashMode` 0/1/2 = NONE/TIMED/HARDWARE。
**V5 不走硬件呼吸** —— 它是软件计时：同一条通知每隔两三秒把 `v` 在 149 和 0 之间调一次
（实测 157.9 / 159.1 / 162.1 / 163.4 s），所以真正跑的是 HAL 里"常亮 / 常灭"那一支；
`dumpsys notification` 里每条记录都是 `led=0x00000000 onMs=0 offMs=0`，也就是这个 ROM 上
没有 app 提供 LED 参数。LUT 那一支留着是为了手测和需要 HARDWARE 闪烁的 ROM。

实测（刷下面那对镜像，不是 push 文件）：

- 属主改过来了：`ls -l /sys/class/leds/white/{brightness,trigger,blink}` → 三个都是
  `system system`（`blink` 是 `0660`，另外两个 `0644`）。
- 载入的确实是我们的模块：`dmesg` 里 `<14>[51.783203] SystemServer: Lights Service` 之后紧跟
  `<14>[51.803574] lights: notification v=149 flash=0 on=0 off=0`，pid 4764 = system_server；
  再往后 `v=0` 与 `v=149` 交替出现，就是 MIUI 的软件脉冲。
- 写落到硬件：`cat /sys/class/leds/white/brightness` = `149`。
- 呼吸图案能起来（手测）：`echo none > trigger; echo 255 > brightness; echo 1 > blink` → 按 LUT
  呼吸；`echo 0 > blink; echo 0 > brightness` → 停。
- **`cat /sys/class/leds/white/blink` 返回 I/O error 是正常的** —— 这个属性在该驱动里只写不读，
  别当成失败；判断 blink 只能看灯或看 HAL 日志。

成品（**取代** §13、§14 的两对，同一个 system 树重打，WiFi + 时钟 + 呼吸灯全在里面）：
`boot-mi5-led1.img` md5 `e398455c52f157b8e245d6ba6b29bfe5`（ramdisk 343285，sha256
`a9557f35e8f638e9ed76abd882546f8a2867a0ec9752750d73ec0fc31502ebc9`）+ `system_miui5_led.img`
md5 `396e31bb43b5baf53d8a966ce0db00e9`（sha256 `2b0338e574f4bf07f5221502c6dcb28e004cae2d9fa45ea8a6833d5f19fc3c7d`），
两个都已刷进本机并按字节回读核对；打包在
`C:\download\chiron\sagit-v5-led-20261028\{boot.img,system.img,FLASH.md}`，WSL 副本 `~/mi5-wifi/out/`。
sagit 的 V5 至此有：WiFi、墙钟、通知灯（震动/导航栏那两条是 chiron pack 的事，sagit 这边不涉及）。

## 4. 红线（用户规矩，必须遵守）

- 抓完日志立即 adb reboot recovery，别让手机一直亮屏耗电。
- vendor blobs 不进 git；AGENTS.local.md 不外传；不 force push、不开 PR；只在被要求时 commit，永不 push。
- 修复落在 /home/juezhi/retro-android-ports 设备树，不动 aosp 树。
- 给用户的命令一律单行、可直接粘贴。
