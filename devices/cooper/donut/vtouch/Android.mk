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
# cooper (GT-S5830) on Donut. Based on MLXProjects' froyo vtouch; cooper
# changes: virtual device renamed, explicit source path forces MT mode
# (the sec_touchscreen declares no MT caps), type-A (SYN_MT_REPORT, no
# slots/IDs) group handling, HVGA fallback ranges, /dev/uinput autocreate.
# Static executable in the ramdisk's /sbin, started by init before the
# framework parses /dev/input devices.
include $(CLEAR_VARS)

LOCAL_SRC_FILES := vtouch.c
LOCAL_CFLAGS := -O2 -Wall -Wno-unused-parameter

LOCAL_MODULE := vtouch
LOCAL_MODULE_TAGS := eng

LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_STATIC_LIBRARIES := libc

LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT_SBIN)
LOCAL_UNSTRIPPED_PATH := $(TARGET_ROOT_OUT_SBIN_UNSTRIPPED)

include $(BUILD_EXECUTABLE)