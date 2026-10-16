# ported from pdx203-jellybean by @chardidathing, https://github.com/chardidathing/pdx203-jellybean
# adreno renders the whole ui from lineage's chroot, see tools/glrenderd. 1080x1920 is the panel's
# native mode, glrenderd repeats pixels by a whole multiple if fb0 comes up bigger
exec /system/glr/hwchroot /system/glr/root /glrenderd 1080 1920 -fb 2>/dev/kmsg
