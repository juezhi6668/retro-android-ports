#!/system/bin/sh
# Poplar USB gadget bring-up + keep-alive (KitKat).
# Belt-and-braces: also (re)creates the configfs gadget + FunctionFS and prods
# adbd, in case the early `on fs` setup missed (a failing init command aborts
# its whole action silently). Then keeps the gadget bound to the UDC: KitKat
# adbd closes ep0 on every disconnect, which unbinds the udc, so re-bind.
CFG=/config
UDC=a800000.dwc3
G=$CFG/usb_gadget/g1

mkdir -p $CFG /dev/usb-ffs/adb 2>/dev/null
mount configfs none $CFG 2>/dev/null
mount functionfs adb /dev/usb-ffs/adb uid=2000,gid=2000 2>/dev/null

mkdir -p $G/strings/0x409 $G/configs/b.1/strings/0x409 $G/functions/ffs.adb 2>/dev/null
echo 0x18d1 > $G/idVendor 2>/dev/null
echo 0x4ee7 > $G/idProduct 2>/dev/null
echo 0x0200 > $G/bcdUSB 2>/dev/null
echo poplarkk > $G/strings/0x409/serialnumber 2>/dev/null
echo Sony > $G/strings/0x409/manufacturer 2>/dev/null
echo poplar-kitkat > $G/strings/0x409/product 2>/dev/null
echo adb > $G/configs/b.1/strings/0x409/configuration 2>/dev/null
[ -e $G/configs/b.1/f1 ] || ln -s $G/functions/ffs.adb $G/configs/b.1/f1 2>/dev/null

# if adbd started before the ffs endpoint existed it fell back to the (absent)
# legacy gadget -- restart it once so it picks FunctionFS up.
setprop ctl.stop adbd 2>/dev/null
sleep 1
setprop ctl.start adbd 2>/dev/null

# let adbd write the ffs descriptors before we bind, otherwise the udc comes up
# with an empty config (host: "device descriptor request failed", code 43)
sleep 4

while true; do
    cur=$(cat $G/UDC 2>/dev/null)
    if [ -z "$cur" ]; then
        echo $UDC > $G/UDC 2>/dev/null
        echo "usbudc: bind -> $(cat $G/UDC 2>/dev/null)" > /dev/kmsg
    fi
    sleep 2
done
