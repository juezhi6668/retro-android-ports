# Pure-AOSP Donut build stub for the proprietary GoogleLoginService API.
#
# Only packages/providers/CalendarProvider/tests references
# com.google.android.googlelogin (GoogleLoginServiceBlockingHelper /
# GoogleLoginServiceNotFoundException), which ships in the proprietary
# GoogleLoginService and is absent from AOSP. Without this stub, a pure-AOSP
# Donut eng build fails in CalendarProviderTests with "package
# com.google.android.googlelogin does not exist".
#
# This provides source-compatible stub classes so the test apk compiles.
# Nothing in the shipped system image calls them at runtime.
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(call all-java-files-under, src)

LOCAL_MODULE := googlelogin

include $(BUILD_JAVA_LIBRARY)
