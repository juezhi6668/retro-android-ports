# Copyright (C) 2010 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


LOCAL_PATH := $(call my-dir)

# vtouch - multi-touch to single-touch input proxy daemon, adapted for
# maple (Xperia XZ Premium) on Donut. Based on cooper's Donut port of
# MLXProjects' froyo vtouch 1.2.0; maple changes: virtual device renamed
# maple_vtouch, explicit source path (/dev/input/event7 = clearpad) forces
# MT mode, type-A groups commit on TRACKING_ID (clearpad sends one per
# contact; cooper had none and used touch_major), PRESSURE as a secondary
# down signal, 1080x1920 fallback ranges. Static executable in the ramdisk's
# /sbin, started by init before the framework parses /dev/input devices.
include $(CLEAR_VARS)

LOCAL_SRC_FILES := vtouch.c
LOCAL_CFLAGS := -O2 -Wall -Wno-unused-parameter

# Module name must be unique across the whole checkout: cooper's Donut tree
# (vendor/samsung/cooper/vtouch) also defines a module named `vtouch`, and
# Donut's build picks up every vendor/*/*/Android.mk in one pass.
LOCAL_MODULE := maple_vtouch
LOCAL_MODULE_TAGS := eng

LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_STATIC_LIBRARIES := libc

LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT_SBIN)
LOCAL_UNSTRIPPED_PATH := $(TARGET_ROOT_OUT_SBIN_UNSTRIPPED)

include $(BUILD_EXECUTABLE)
