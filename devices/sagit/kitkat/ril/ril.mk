MSM8998_RIL_DIR := vendor/xiaomi/sagit-ril
MSM8998_RIL_MANIFEST := vendor/xiaomi/sagit/ril/blobs.sha256
MSM8998_RIL_FILES := $(shell cut -c67- $(MSM8998_RIL_MANIFEST))
MSM8998_RIL_VERIFIED := $(shell cd $(MSM8998_RIL_DIR) && sha256sum -c --status $(abspath $(MSM8998_RIL_MANIFEST)) && printf verified)
ifneq ($(MSM8998_RIL_VERIFIED),verified)
$(error Stage the matched RIL with vendor/xiaomi/sagit/ril/stage-ril.py before building)
endif

PRODUCT_PACKAGES += qmxd start-msm8998-ril
PRODUCT_COPY_FILES += \
    vendor/xiaomi/sagit/ril/init.msm8998.ril.rc:root/init.msm8998.ril.rc \
    $(MSM8998_RIL_DIR)/bin/rild:system/bin/rild-msm8998 \
    $(foreach f,$(filter lib/%,$(MSM8998_RIL_FILES)),$(MSM8998_RIL_DIR)/$(f):system/vendor/msm8998-ril/$(f))
