#!/system/bin/sh
# wf-efs-keeper.sh — keeps rmt_storage + cnss-daemon alive across modem SSRs.
#
# Why a keeper script instead of init `respawn` on the binaries: launched
# directly by KitKat init all of them exit within ~3s with no output at all,
# while the identical command line run from a shell stays up forever. The
# daemons are fine; init's exec path is not. This script runs as an ordinary
# init service, daemonises its children with the ( ... & ) subshell form, and
# the children — which re-parent to init when this script is killed — outlive it.
#
# tftp_server is deliberately NOT started. It advertises itself on the same
# QMI service (0x1000) as /sbin/rfssrv2, and the modem binds the most recently
# registered server. tftp_server reads from /vendor/firmware_mnt/image, which
# does not exist on this KitKat tree, so whenever it wins the race the modem's
# wlanmdsp.mbn fetch dies silently and WLFW (QMI 0x45) never appears.
LOG=/data/local/tmp/wf-keeper.log
K=/system/lib64:/vendor/lib64
V=/system/vendor/bin

alive() {
    for d in /proc/[0-9]*; do
        c=$(cat $d/cmdline 2>/dev/null)
        case "$c" in *$1*) return 0 ;; esac
    done
    return 1
}

spawn() {
    echo "$(toolbox date 2>/dev/null) start $1" >> $LOG
    ( LD_LIBRARY_PATH=$K $V/$1 >$LOG.$1 2>&1 & )
}

while true; do
    alive rmt_storage || spawn rmt_storage
    if ! alive cnss-daemon; then
        sleep 5
        spawn cnss-daemon
    fi
    sleep 3
done
