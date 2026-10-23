# usbudc.sh — bind the configfs gadget once a UDC exists.
# ported from pdx203-jellybean, Mi 6 adaptation: the UDC device name is read
# from /sys/class/udc instead of hardcoding a800000.dwc3 (sagit's controller
# may register as a800000.ssusb or a800000.dwc3 depending on the DT).
# kitkat adbd closes ep0 on every disconnect, and closing an ffs instance
# unbinds the whole gadget from the udc. there's no sys.usb.ffs.ready either,
# so keep binding until it sticks.
UDC=/config/usb_gadget/g1/UDC
while true; do
    set -- $(ls /sys/class/udc 2>/dev/null)
    if [ -n "$1" ] && [ -z "$(cat $UDC 2>/dev/null)" ]; then
        echo "$1" > $UDC 2>/dev/null
        echo "usbudc: bind $1 -> $(cat $UDC 2>/dev/null)" > /dev/kmsg
    fi
    sleep 2
done
