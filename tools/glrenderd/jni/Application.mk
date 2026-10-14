# ported from pdx203-jellybean by @chardidathing, https://github.com/chardidathing/pdx203-jellybean
APP_ABI := armeabi-v7a
# runs against lineage's bionic in the chroot, not kitkat's, so a modern api level is fine
APP_PLATFORM := android-29
APP_STL := c++_static
APP_CPPFLAGS := -fexceptions -frtti
