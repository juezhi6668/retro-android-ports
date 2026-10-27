# Product definition: full_chiron (Xiaomi Mi MIX 2, KitKat 4.4.4)
# Thin sibling of full_sagit: the shared sources (audio/lights/gralloc HALs,
# libhyb, EGL shims, fbpresentd, pdserve, rtc-restore, init.rc, scripts,
# keylayout/idc, wifi glue) are referenced straight out of
# vendor/xiaomi/sagit, and only the chiron configs live here.

PRODUCT_PACKAGES := \
    AlarmClock \
    Calendar \
    Camera \
    LatinIME \
    Music \
    Settings

$(call inherit-product, $(SRC_TARGET_DIR)/product/core.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/generic.mk)

# Same 2048-dalvik-heap bucket as sagit (6/8 GB RAM, 1080p panel); the
# defaults OOM system_server verifying framework-res.apk on first boot.
$(call inherit-product, frameworks/native/build/phone-xhdpi-2048-dalvik-heap.mk)
$(call inherit-product, frameworks/base/data/sounds/AllAudio.mk)

DEVICE_PACKAGE_OVERLAYS := vendor/xiaomi/sagit/overlay

PRODUCT_NAME := full_chiron
PRODUCT_DEVICE := chiron
PRODUCT_MODEL := MI MIX 2
PRODUCT_BRAND := Xiaomi
PRODUCT_MANUFACTURER := Xiaomi

PRODUCT_COPY_FILES += \
    vendor/xiaomi/chiron/system.prop:system/build.prop \
    vendor/xiaomi/chiron/fstab.chiron:root/fstab.chiron \
    vendor/xiaomi/sagit/init.rc:root/init.rc \
    vendor/xiaomi/chiron/init.chiron.rc:root/init.chiron.rc \
    vendor/xiaomi/chiron/init.chiron.usb.rc:root/init.chiron.usb.rc \
    vendor/xiaomi/sagit/usbudc.sh:root/sbin/usbudc.sh \
    vendor/xiaomi/chiron/ueventd.chiron.rc:root/ueventd.chiron.rc \
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
    vendor/xiaomi/chiron/wifi/wpa_supplicant_overlay.conf:system/etc/wifi/wpa_supplicant_overlay.conf \
    vendor/xiaomi/chiron/wifi/p2p_supplicant_overlay.conf:system/etc/wifi/p2p_supplicant_overlay.conf \
    vendor/xiaomi/chiron/wifi/WCNSS_qcom_cfg.ini:system/vendor/etc/wifi/WCNSS_qcom_cfg.ini \
    vendor/xiaomi/chiron/wifi/WCNSS_qcom_cfg.ini:system/vendor/firmware/wlan/qca_cld/WCNSS_qcom_cfg.ini \
    vendor/xiaomi/sagit/audio/audio_policy.conf:system/etc/audio_policy.conf \
    vendor/xiaomi/chiron/mixer_paths_tasha.xml:system/etc/mixer_paths_tasha.xml \
    vendor/xiaomi/sagit/media/media_codecs.xml:system/etc/media_codecs.xml \
    frameworks/native/data/etc/android.hardware.wifi.xml:system/etc/permissions/android.hardware.wifi.xml

# audio.primary.chiron (shared tinyalsa HAL, -DAMP_TAS2559) + sound recorder
# jni (same CHARQUIRKS reason as sagit).
PRODUCT_PACKAGES += \
    audio.primary.$(PRODUCT_DEVICE) \
    libsrec_jni

# Firmware. adsp/modem are the same msm8998 blobs as sagit (same baseband,
# user-confirmed) and are staged out-of-git in vendor/xiaomi/sagit-fw; the
# speaker amp is board-specific: chiron's TAS2559 needs its own uCDSP
# containers (vendor/xiaomi/chiron-fw/audio). No wlan_mac.bin: chiron keeps
# it in /persist, and the supplied persist.img is empty — the driver falls
# back to the address in bdwlan until the phone's own persist is restored.
SAGIT_FW := vendor/xiaomi/sagit-fw
CHIRON_FW := vendor/xiaomi/chiron-fw
# Prebuilt binaries built by this repo (sagit-bin: fbpresentd + the WLAN stack's
# wlq/rfssrv2/sm-vndk) and the 64-bit WLAN vendor blobs (sagit-wifi). Both are
# out-of-git staging inside the AOSP tree. Defined here, before their first use.
BIN_DIR := vendor/xiaomi/sagit-bin
WIFI_VND := vendor/xiaomi/sagit-wifi
WF_BINS := wlq rfssrv2 sm-vndk
ifneq ($(wildcard $(SAGIT_FW)/adsp/adsp.mdt),)
PRODUCT_COPY_FILES += \
    $(foreach f,$(shell cd $(SAGIT_FW)/adsp && ls adsp.* cpe_* 2>/dev/null),$(SAGIT_FW)/adsp/$(f):system/vendor/firmware/$(f))
endif
ifneq ($(wildcard $(SAGIT_FW)/modem/modem.mdt),)
PRODUCT_COPY_FILES += \
    $(foreach f,$(shell cd $(SAGIT_FW)/modem && ls mba.mbn modem.mdt modem.b* wlanmdsp.mbn 2>/dev/null),$(SAGIT_FW)/modem/$(f):system/vendor/firmware/$(f))
endif
# The modem fetches the WLAN firmware image over RFS with the path
# "readonly/firmware/image/wlanmdsp.mbn", which rfssrv2 maps to
# /system/vendor/firmware/image/ — a second copy of the file, in the directory
# layout stock keeps on the firmware_mnt partition. Verified on-device: the
# 441-block read only succeeds when this exists.
ifneq ($(wildcard $(SAGIT_FW)/modem/wlanmdsp.mbn),)
PRODUCT_COPY_FILES += $(SAGIT_FW)/modem/wlanmdsp.mbn:system/vendor/firmware/image/wlanmdsp.mbn
endif
ifneq ($(wildcard $(CHIRON_FW)/audio/tas2559_uCDSP.bin),)
PRODUCT_COPY_FILES += \
    $(CHIRON_FW)/audio/tas2559_uCDSP.bin:system/vendor/firmware/tas2559_uCDSP.bin \
    $(CHIRON_FW)/audio/tas2559_s_uCDSP.bin:system/vendor/firmware/tas2559_s_uCDSP.bin
endif
# bdwlan: the OS3 dump has none (it lives in the modem partition we were not
# given), so the WCN3990 board data from sagit is the bringup fallback.
ifneq ($(wildcard $(SAGIT_FW)/wlan/bdwlan.bin),)
PRODUCT_COPY_FILES += \
    $(foreach f,$(shell cd $(SAGIT_FW)/wlan && ls bdwlan* 2>/dev/null),$(SAGIT_FW)/wlan/$(f):system/etc/wifi/$(f))

# The board data alone in /system/etc/wifi is NOT enough for WCN3990. On this
# platform the WLFW (QMI 0x45) sequence is split across two clients: the
# kernel icnss driver only does ind_register/msa_info/msa_ready/cap, and the
# vendor cnss-daemon is the only thing that performs wlfw_bdf_download_req.
# The modem does not post FW_READY until that BDF lands, so without it icnss
# never loads the FW and wlan0 never appears. cnss-daemon searches, in
# /vendor/firmware/ and /vendor/firmware_mnt/image/, for a fixed name list
# (bdf_c2.bin, bdf_d5.bin, bdf_d5s.bin, bdwlan.bin plus .%03x chip-id and
# .b%02x board-id variants). chiron is a D5 (chip_id 0x30214), so bdf_d5.bin
# is the one it wants; the others are installed so any variant probe hits.
# All sagit blobs are the same generic 19152-byte image, so one source feeds
# every name. Proven on-device 2026-10-26: with these present cnss-daemon
# gets past "Failed to Download the BDF File", icnss reaches FW_READY +
# DRIVER PROBED, and `wpa_supplicant -Dnl80211` scans real 2.4/5 GHz APs.
define _bdf_variants
  bdf_c2.bin bdf_d5.bin bdf_d5s.bin bdwlan.bin
endef
PRODUCT_COPY_FILES += \
    $(foreach n,$(_bdf_variants),$(SAGIT_FW)/wlan/bdwlan.bin:system/vendor/firmware/$(n)) \
    $(foreach n,$(_bdf_variants),$(SAGIT_FW)/wlan/bdwlan.bin:system/vendor/firmware/image/$(n))
endif

# IRSC completion (the real cold-boot blocker, found 2026-10-26). The BDF files
# above let cnss-daemon *find* the board data, but on msm8998 legacy ipc-router
# v1 every user-space CLIENT_PORT send first calls wait_for_irsc_completion(),
# and that completion is signalled only when an IRSC_PORT closes -- normally done
# by the vendor irsc_util. Here irsc_util is a 64-bit blob that must be run from
# a shell with LD_LIBRARY_PATH=/system/lib64:/vendor/lib64 (init's exec of it
# dies silently in ~3 s, which is why the old `service wf-irsc` never did
# anything), so the completion never fired and cnss's wlfw_bdf_download_req
# stalled forever: no BDF, no FW_READY, no wlan0. /sbin/wf-setup.sh fires it at
# post-fs-data with our static wlq (`wlq irsc <sec_config>`), which loads
# sec_config and closes the IRSC port. It has to be that early: the /system
# boot-log hook is a class-main service and only runs at ~27 s, which pushes
# FW_READY past the framework's first WiFi attempt.
#   rfssrv2 (AP-side RFS/TFTP on QMI 0x1000, serves the wlanmdsp.mbn fetch) and
# sm-vndk (vndbinder context manager for cnss's libbinder; KitKat's own
# servicemanager with "/dev/binder" patched to "/dev/vb") complete the stack.
# All three are built by wifi-tools/build-wf-bins.sh and staged in sagit-bin.
# wf-setup.sh resolves them from /sbin, then /system/bin, then /data/local/tmp.
PRODUCT_COPY_FILES += \
    $(foreach b,$(WF_BINS),$(if $(wildcard $(BIN_DIR)/bin/$(b)),$(BIN_DIR)/bin/$(b):system/bin/$(b)))

# The 64-bit WLAN vendor blobs: cnss-daemon + rmt_storage and every library they
# resolve, including a 64-bit /system/lib64 (this tree builds arm-only, so
# nothing else puts one on the phone). Staged out-of-git, shared with sagit --
# these are generic Qualcomm binaries, not board-specific, and the set staged
# here is the one this phone boots with today. Without cnss-daemon there is no
# BDF download, and without rmt_storage held alive across SSR the modem
# re-crashes every 41 s; both are run by /sbin/wf-efs-keeper.sh because init
# cannot exec them.
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

# Graphics: the real Adreno 540 driver (Android 15 vendor closure) + QTI
# gralloc1 through libhyb, shared with sagit. gralloc.default and
# libGLES_android stay as fallbacks.
PRODUCT_PACKAGES += \
    libhyb \
    libEGL_adreno \
    libGLESv1_CM_adreno \
    libGLESv2_adreno \
    gralloc.$(PRODUCT_DEVICE) \
    gralloc.default \
    libGLES_android \
    hybtest \
    acdbtest \
    audiotrack_test \
    audiorecord_test \
    rtc-restore \
    time-sync \
    lights.$(PRODUCT_DEVICE) \
    pdserve

# Android 15 vendor closure: same staged root at /system/a15 + a540 firmware.
HYB_DIR := vendor/xiaomi/sagit-hyb
ifneq ($(wildcard $(HYB_DIR)/root/vendor/lib/egl/libEGL_adreno.so),)
PRODUCT_COPY_FILES += \
    $(foreach f,$(shell cd $(HYB_DIR)/root && find . -type f -printf '%P\n'),$(HYB_DIR)/root/$(f):system/a15/$(f)) \
    $(foreach f,$(shell cd $(HYB_DIR)/root/vendor/firmware && ls),$(HYB_DIR)/root/vendor/firmware/$(f):system/vendor/firmware/$(f))
endif

# ACDB calibration for chiron: the loader picks its directory by platform
# name; the chiron dump ships both ChironS/<blocks> and the generic Forte
# copies, so both are installed verbatim (libacdbloader on this device knows
# both names). Without them AFE/ASM cal lookups fail and playback drains
# silently.
CHIRON_ACDB := vendor/xiaomi/chiron-hyb/acdbdata
ifneq ($(wildcard $(CHIRON_ACDB)),)
PRODUCT_COPY_FILES += \
    $(foreach f,$(shell cd $(CHIRON_ACDB) && find . -type f -printf '%P\n'),$(CHIRON_ACDB)/$(f):system/vendor/etc/acdbdata/$(f))
endif

# fbpresentd: shared 64-bit MDSS presenter (sources in
# vendor/xiaomi/sagit/fbpresentd, prebuilt in sagit-bin — BIN_DIR defined above).
ifneq ($(wildcard $(BIN_DIR)/bin/fbpresentd),)
PRODUCT_COPY_FILES += $(BIN_DIR)/bin/fbpresentd:system/bin/fbpresentd
endif
