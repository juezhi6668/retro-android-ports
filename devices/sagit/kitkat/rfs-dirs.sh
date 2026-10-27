#!/system/bin/sh
# rfs-dirs.sh — build the RFS tree the AP-side RFS/TFTP server serves the modem.
#
# The modem (msm/mpss) reads and writes files through RFS: rfssrv2 answers those
# requests on the qsocket transport and maps them under its root
# (/data/rfs/<arch>/<subsys> by default, see wifi-tools/rfssrv2.c). Stock keeps
# this tree in /vendor/rfs; this port cannot, because /vendor is /system/vendor
# and /system is read-only, so neither the skeleton nor the modem's WRQs
# (mcfg.tmp round-trip) would ever land. It is built at boot instead:
# PRODUCT_COPY_FILES cannot carry symlinks, and /data is mounted by the time
# wf-setup.sh runs.
#
# The layout mirrors lineage-22.1's vendor/rfs exactly, except for the targets
# of the data links: stock puts them on /mnt/vendor/persist (the persist
# partition's stock mountpoint) and on /vendor/firmware_mnt (the firmware
# partition). This port mounts persist at /persist and stages the firmware
# partition's images under /vendor/firmware, and /mnt itself is a read-only
# ramdisk dir, so the links point at this port's paths.
#
# readonly/firmware is the path the modem's wlan_pd side reads the WLAN image
# (wlanmdsp.mbn) from; readwrite/shared/hlos are its log and hive areas;
# ramdumps land in /data.
set -e
R=/data/rfs
P=/persist

mkdir -p $P/rfs/shared
mkdir -p $P/hlos_rfs/shared
mkdir -p /data/vendor/tombstones/rfs
mkdir -p /data/vendor/pddump

# KitKat's toolbox ln only takes a bare -s: "ln -sfn TARGET LINK" fails with
# "link failed No such file or directory", so remove-then-link by hand
mklink() {
	rm -f "$2"
	ln -s "$1" "$2"
}

for sub in msm/adsp msm/cdsp msm/mpss msm/slpi \
	   mdm/adsp mdm/cdsp mdm/mpss mdm/slpi mdm/tn apq/gnss; do
	case $sub in
	*/adsp) rd=lpass ;;
	*/cdsp) rd=cdsp ;;
	*/mpss | apq/*) rd=modem ;;
	*/slpi) rd=slpi ;;
	*/tn) rd=tn ;;
	esac

	d=$R/$sub
	# One operand per mkdir: KitKat's toolbox stops at the first operand that
	# fails, which with set -e would drop the rest of this subsystem tree.
	mkdir -p $d/readonly/vendor
	mkdir -p $P/rfs/$sub
	mkdir -p /data/vendor/tombstones/rfs/$rd
	mklink $P/rfs/$sub $d/readwrite
	mklink $P/rfs/shared $d/shared
	mklink $P/hlos_rfs/shared $d/hlos
	mklink /vendor/firmware $d/readonly/firmware
	mklink /vendor/firmware $d/readonly/vendor/firmware
	mklink /data/vendor/tombstones/rfs/$rd $d/ramdumps
done
