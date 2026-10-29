# Product definition: full_sagit (Xiaomi Mi 6, KitKat 4.4.4)
# Ported from full_maple; every vendor path moved to vendor/xiaomi/sagit.

PRODUCT_PACKAGES := \
    AlarmClock \
    Calendar \
    Camera \
    LatinIME \
    Music \
    Settings

$(call inherit-product, $(SRC_TARGET_DIR)/product/core.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/generic.mk)

# Dalvik heap: 4/6 GB RAM, 480 dpi 1080p panel → the 2048 bucket
# (heapgrowthlimit 192m, heapsize 512m). Without this the build ships Dalvik's
# tiny defaults and system_server OOMs verifying the 11.8 MB framework-res.apk
# on first boot (maple 2026-10-15: OutOfMemoryError in JarUtils.verifySignature,
# dalvik-cache never populates).
$(call inherit-product, frameworks/native/build/phone-xhdpi-2048-dalvik-heap.mk)
# stock AOSP ringtones/alarms/ui effects; without it /system/media/audio is
# empty and there is nothing for AudioFlinger to play.
$(call inherit-product, frameworks/base/data/sounds/AllAudio.mk)

DEVICE_PACKAGE_OVERLAYS := vendor/xiaomi/sagit/overlay

PRODUCT_NAME := full_sagit
PRODUCT_DEVICE := sagit
PRODUCT_MODEL := MI 6
PRODUCT_BRAND := Xiaomi
PRODUCT_MANUFACTURER := Xiaomi

$(call inherit-product, vendor/xiaomi/sagit/ril/ril.mk)

PRODUCT_COPY_FILES += \
    vendor/xiaomi/sagit/system.prop:system/build.prop \
    vendor/xiaomi/sagit/fstab.msm8998:root/fstab.msm8998 \
    vendor/xiaomi/sagit/init.rc:root/init.rc \
    vendor/xiaomi/sagit/init.msm8998.rc:root/init.msm8998.rc \
    vendor/xiaomi/sagit/init.msm8998.usb.rc:root/init.msm8998.usb.rc \
    vendor/xiaomi/sagit/usbudc.sh:root/sbin/usbudc.sh \
    vendor/xiaomi/sagit/ueventd.msm8998.rc:root/ueventd.msm8998.rc \
    vendor/xiaomi/sagit/autowinch.sh:root/sbin/autowinch.sh \
    vendor/xiaomi/sagit/wf-setup.sh:root/sbin/wf-setup.sh \
    vendor/xiaomi/sagit/wf-efs-keeper.sh:root/sbin/wf-efs-keeper.sh \
    vendor/xiaomi/sagit/wf-wifi-gate.sh:root/sbin/wf-wifi-gate.sh \
    vendor/xiaomi/sagit/rfs-dirs.sh:root/sbin/rfs-dirs.sh \
    vendor/xiaomi/sagit/time-sync.sh:root/sbin/time-sync.sh \
    vendor/xiaomi/sagit/wifi/sec_config:system/vendor/etc/sec_config \
    vendor/xiaomi/sagit/dump-boot-log.sh:system/bin/dump-boot-log.sh \
    vendor/xiaomi/sagit/modem-hold.sh:root/sbin/modem-hold.sh \
    vendor/xiaomi/sagit/adsp-boot.sh:root/sbin/adsp-boot.sh \
    vendor/xiaomi/sagit/gpio-keys.kl:system/usr/keylayout/gpio-keys.kl \
    vendor/xiaomi/sagit/synaptics_dsx.idc:system/usr/idc/synaptics_dsx.idc \
    vendor/xiaomi/sagit/wifi/wpa_supplicant_overlay.conf:system/etc/wifi/wpa_supplicant_overlay.conf \
    vendor/xiaomi/sagit/wifi/p2p_supplicant_overlay.conf:system/etc/wifi/p2p_supplicant_overlay.conf \
    vendor/xiaomi/sagit/wifi/WCNSS_qcom_cfg.ini:system/vendor/firmware/wlan/qca_cld/WCNSS_qcom_cfg.ini \
    vendor/xiaomi/sagit/audio/audio_policy.conf:system/etc/audio_policy.conf \
    vendor/xiaomi/sagit/audio/mixer_paths_tasha.xml:system/etc/mixer_paths_tasha.xml \
    vendor/xiaomi/sagit/media/media_codecs.xml:system/etc/media_codecs.xml \
    frameworks/native/data/etc/android.hardware.wifi.xml:system/etc/permissions/android.hardware.wifi.xml \
    frameworks/native/data/etc/android.hardware.bluetooth.xml:system/etc/permissions/android.hardware.bluetooth.xml \
    frameworks/native/data/etc/android.hardware.bluetooth_le.xml:system/etc/permissions/android.hardware.bluetooth_le.xml

# audio.primary.msm8998 (tinyalsa HAL) + sound recorder jni (CHARQUIRKS:
# "sound recorder needs libsrec_jni in PRODUCT_PACKAGES or it crashes").
# media_codecs.xml is product-supplied: KitKat's MediaCodecList reads
# /etc/media_codecs.xml and AOSP only ships one for goldfish/sdk — without it
# every compressed format fails with UNKNOWN_ERROR (maple TEST 52).
PRODUCT_PACKAGES += \
    audio.primary.msm8998 \
    libsrec_jni \
    libbt-vendor

# Firmware blobs live out-of-git at vendor/xiaomi/sagit-fw/{adsp,wlan,modem,audio}/
# (pulled from the modem partition + persist + stock ROM; mirrors the kernel-blob
# and sagit-hyb precedent so sync-device-tree.sh --delete leaves them alone).
# They land where firmware_class.path points (/vendor/firmware, /vendor ->
# /system/vendor):
#  - adsp.* + cpe_* (audio DSP + codec engine)
#  - bdwlan.* (WLAN board-data) + wlan_mac.bin (from persist; without it the
#    MAC falls back per CHARQUIRKS)
#  - modem/ (mba.mbn, modem.mdt, modem.b00-*, wlanmdsp.mbn — the PIL boot
#    modem-hold.sh triggers)
#  - audio/ (tfa98xx_aac.cnt — the TFA98xx speaker-amp container; the codec
#    loads it while registering the card and the whole card fails (-11) when
#    it is missing, so /proc/asound/cards stays empty: no sound at all)
FW_DIR := vendor/xiaomi/sagit-fw
ifneq ($(wildcard $(FW_DIR)/adsp/adsp.mdt),)
PRODUCT_COPY_FILES += \
    $(foreach f,$(shell cd $(FW_DIR)/adsp && ls adsp.* cpe_* 2>/dev/null),$(FW_DIR)/adsp/$(f):system/vendor/firmware/$(f)) \
    $(foreach f,$(shell cd $(FW_DIR)/wlan && ls bdwlan* 2>/dev/null),$(FW_DIR)/wlan/$(f):system/etc/wifi/$(f)) \
    $(FW_DIR)/wlan/wlan_mac.bin:system/vendor/firmware/wlan/qca_cld/wlan_mac.bin
endif
ifneq ($(wildcard $(FW_DIR)/modem/modem.mdt),)
PRODUCT_COPY_FILES += \
    $(foreach f,$(shell cd $(FW_DIR)/modem && ls mba.mbn modem.mdt modem.b* wlanmdsp.mbn 2>/dev/null),$(FW_DIR)/modem/$(f):system/vendor/firmware/$(f))
endif

# Bluetooth (WCN3990 / cherokee): the patchram pair, named after the ROM build
# the controller reports -- crbtfw<NN>.tlv + crnv<NN>.bin where NN is the two
# low nibbles of the 16-bit build version (0x0201 -> 21). libbt-vendor picks
# the pair by trying the prefixes the stock QTI HAL searches
# (rome_set_cherokee_fw_paths), so the whole set ships rather than a guess;
# sagit and chiron carry the same WCN3990 files. The path mirrors the stock
# /bt_firmware/image layout, which KitKat has no vendor partition for.
ifneq ($(wildcard $(FW_DIR)/bt/image),)
PRODUCT_COPY_FILES += \
    $(foreach f,$(shell cd $(FW_DIR)/bt/image && ls *.tlv *.bin 2>/dev/null),$(FW_DIR)/bt/image/$(f):system/etc/firmware/bt/image/$(f))
endif

# WLAN board data where cnss-daemon actually looks (chiron bring-up proved
# this is not optional — same WCN3990, same driver, docs/devices/chiron/
# wifi-progress.md §5 root cause A). The WLFW (QMI 0x45) sequence is split:
# the kernel icnss only does ind_register/msa_info/msa_ready/cap, and the
# vendor cnss-daemon is the only thing that performs wlfw_bdf_download_req;
# the modem does not post FW_READY until that BDF lands. cnss searches a
# fixed name list (bdf_c2.bin, bdf_d5.bin, bdf_d5s.bin, bdwlan.bin plus
# .%03x chip-id / .b%02x board-id variants) in /vendor/firmware/ and
# /vendor/firmware_mnt/image/, NOT in /system/etc/wifi where the kernel's
# firmware loader puts it — so one source feeds every name. The wlan PD
# fetches wlanmdsp.mbn over RFS with the "readonly/firmware/image/<name>"
# path, which rfssrv2 maps to /system/vendor/firmware/image/, hence that
# copy too.
BDF_NAMES := bdf_c2.bin bdf_d5.bin bdf_d5s.bin bdwlan.bin
ifneq ($(wildcard $(FW_DIR)/wlan/bdwlan.bin),)
PRODUCT_COPY_FILES += \
    $(foreach n,$(BDF_NAMES),$(FW_DIR)/wlan/bdwlan.bin:system/vendor/firmware/$(n)) \
    $(foreach n,$(BDF_NAMES),$(FW_DIR)/wlan/bdwlan.bin:system/vendor/firmware/image/$(n))
endif
ifneq ($(wildcard $(FW_DIR)/modem/wlanmdsp.mbn),)
PRODUCT_COPY_FILES += $(FW_DIR)/modem/wlanmdsp.mbn:system/vendor/firmware/image/wlanmdsp.mbn
endif

# The WLAN vendor stack, staged out-of-git next to sagit-fw/sagit-hyb/sagit-bin
# (same reason: sync-device-tree.sh --delete only touches vendor/xiaomi/sagit,
# and these are device dumps, not source). KitKat is a 32-bit tree with no
# /vendor partition, so the 64-bit QC daemons and every library they resolve
# have to be laid out by hand:
#  - bin/  cnss-daemon (does the BDF download + MAC + cal report),
#          rmt_storage (modem EFS; every SSR kills it, which is what produced
#          the 41 s crash loop), irsc_util / pd-mapper / tftp_server kept for
#          debugging only — init cannot exec them and rfssrv2 replaces
#          tftp_server on purpose (they race for QMI service 0x1000).
#  - lib64/     their QC dependencies (libqmi_*, libqrtr, libqsocket,
#               libperipheral_client, libnl, libcld80211, libbinder, ...).
#  - system-lib64/ the 64-bit bionic they need (libc/libm/libdl/liblog/
#               ld-android/libvndksupport/libapexsupport/libdl_android);
#               this tree builds arm-only, so nothing else puts a 64-bit
#               /system/lib64 on the phone.
# The keepers launch them with LD_LIBRARY_PATH=/system/lib64:/vendor/lib64.
# Currently staged from the chiron phone's working set (OS3 stock vendor blobs:
# generic Qualcomm code, not board-specific); for a sagit-native set use
# tools/libhyb/extract-lineage.sh on the Lineage 22.1 sagit zip.
WIFI_VND := vendor/xiaomi/sagit-wifi
ifneq ($(wildcard $(WIFI_VND)/bin/cnss-daemon),)
PRODUCT_COPY_FILES += \
    $(foreach f,$(shell cd $(WIFI_VND)/bin && ls),$(WIFI_VND)/bin/$(f):system/vendor/bin/$(f)) \
    $(foreach f,$(shell cd $(WIFI_VND)/lib64 && ls),$(WIFI_VND)/lib64/$(f):system/vendor/lib64/$(f)) \
    $(foreach f,$(shell cd $(WIFI_VND)/system-lib64 && ls),$(WIFI_VND)/system-lib64/$(f):system/lib64/$(f))
endif

# Every one of those daemons is aarch64 with PT_INTERP /system/bin/linker64, and
# this tree builds arm-only, so the 64-bit bionic linker has to be carried in
# too. Without it exec() fails with ENOENT -- not "cannot exec format", just
# "No such file or directory" -- so the keeper's children die instantly and the
# modem never gets its RFS/BDF services. Staged from the same Android 10 system
# image as the libs above (build-wf-bins.sh / stage-wifi-vnd.sh).
ifneq ($(wildcard $(WIFI_VND)/linker64),)
PRODUCT_COPY_FILES += \
    $(WIFI_VND)/linker64:system/bin/linker64
endif
ifneq ($(wildcard $(FW_DIR)/audio/tfa98xx_aac.cnt),)
PRODUCT_COPY_FILES += \
    $(foreach f,$(shell cd $(FW_DIR)/audio && ls tfa98xx*.cnt 2>/dev/null),$(FW_DIR)/audio/$(f):system/vendor/firmware/$(f))
endif

# Graphics: the real Adreno 540 driver (Android 15 vendor closure) and the QTI
# gralloc1 are loaded into every KitKat process by libhyb (hybtest probes the
# driver end to end); gralloc.default and libGLES_android stay as fallbacks.
PRODUCT_PACKAGES += \
    libhyb \
    libEGL_adreno \
    libGLESv1_CM_adreno \
    libGLESv2_adreno \
    gralloc.msm8998 \
    gralloc.default \
    libGLES_android \
    hybtest \
    acdbtest \
    audiotrack_test \
    audiorecord_test \
    rtc-restore \
    time-sync \
    lights.msm8998 \
    pdserve

# Android 15 vendor closure staged by tools/libhyb/stage.py: Adreno 540 driver
# (vendor/lib/egl), QTI gralloc1, and their dependencies. Outside
# vendor/xiaomi/sagit so rsync --delete leaves it alone; lands at /system/a15
# (HYB_ROOT in hyb.h). The a540 firmware goes to /vendor/firmware too.
HYB_DIR := vendor/xiaomi/sagit-hyb
ifneq ($(wildcard $(HYB_DIR)/root/vendor/lib/egl/libEGL_adreno.so),)
PRODUCT_COPY_FILES += \
    $(foreach f,$(shell cd $(HYB_DIR)/root && find . -type f -printf '%P\n'),$(HYB_DIR)/root/$(f):system/a15/$(f)) \
    $(foreach f,$(shell cd $(HYB_DIR)/root/vendor/firmware && ls),$(HYB_DIR)/root/vendor/firmware/$(f):system/vendor/firmware/$(f))
endif

# ACDB calibration: the vendor libacdbloader hardcodes "Forte" as its data
# directory (hw_platform reads SAGIT on this device and is not what the loader
# uses), so the Mi 6 blocks are staged as acdbdata/Forte and installed under
# that name. Without it the AFE/ASM cal lookups fail and playback drains
# silently (maple TEST 50). adsp_avs_config.acdb sits beside the platform dir.
ifneq ($(wildcard $(HYB_DIR)/acdbdata/Forte),)
PRODUCT_COPY_FILES += \
    $(foreach f,$(shell cd $(HYB_DIR)/acdbdata/Forte && ls),$(HYB_DIR)/acdbdata/Forte/$(f):system/vendor/etc/acdbdata/Forte/$(f))
endif
ifneq ($(wildcard $(HYB_DIR)/acdbdata/adsp_avs_config.acdb),)
PRODUCT_COPY_FILES += $(HYB_DIR)/acdbdata/adsp_avs_config.acdb:system/vendor/etc/acdbdata/adsp_avs_config.acdb
endif

# fbpresentd: zero-copy MDSS presentation (sources in fbpresentd/). 64-bit, so
# the NDK builds it (tools/libhyb/build-native.sh) into vendor/xiaomi/sagit-bin.
BIN_DIR := vendor/xiaomi/sagit-bin
ifneq ($(wildcard $(BIN_DIR)/bin/fbpresentd),)
PRODUCT_COPY_FILES += $(BIN_DIR)/bin/fbpresentd:system/bin/fbpresentd
endif

# wlq / rfssrv2 / sm-vndk: the three small binaries the WLAN stack runs as.
# Built by devices/chiron/kitkat/wifi-tools/build-wf-bins.sh (static, so no
# LD_LIBRARY_PATH and no 32/64-bit mixing), staged here next to fbpresentd.
#  - wlq fires the ipc-router IRSC completion. On msm8998 legacy ipc-router v1
#    every user-space QMI sendmsg waits on a global completion that only fires
#    when a port configured via IPC_ROUTER_IOCTL_CONFIG_SEC_RULES is closed;
#    without it cnss-daemon's wlfw_bdf_download_req hangs forever, the modem
#    never posts FW_READY and there is no wlan0. `wlq irsc <sec_config>` loads
#    the rules and closes the socket.
#  - rfssrv2 is the AP-side RFS/TFTP server on QMI 0x1000 that serves the
#    modem's wlanmdsp.mbn fetch (and lets it write its mcfg.tmp round-trip).
#    It replaces the vendor tftp_server, which must NOT run: it registers on the
#    same service and wins the race half the time, then reads a
#    /vendor/firmware_mnt/image that this port does not have.
#  - sm-vndk is KitKat's own static servicemanager with "/dev/binder" patched to
#    "/dev/vb" so the vendor cnss-daemon's libbinder has a vndbinder context
#    manager (wifi-tools/patch-sm-vndk.py).
# wf-setup.sh looks for each of them in /sbin, then /system/bin, then
# /data/local/tmp, so a repacked stock boot image can carry its own copies.
WF_BINS := wlq rfssrv2 sm-vndk
PRODUCT_COPY_FILES += \
    $(foreach b,$(WF_BINS),$(if $(wildcard $(BIN_DIR)/bin/$(b)),$(BIN_DIR)/bin/$(b):system/bin/$(b)))
