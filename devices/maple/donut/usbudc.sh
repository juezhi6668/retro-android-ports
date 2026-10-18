#!/system/bin/sh
# usbudc.sh - bind / keep the configfs USB gadget (Donut adbd over FunctionFS).
#
# Two hard-learned constraints (TEST 42, 2026-10-18):
#
# 1. DONUT'S /system/bin/sh HAS NO [ / test: no builtin and no /system/bin/[
#    binary, so every `[ ... ]` is "[: not found" and every conditional
#    written with it silently takes the else branch. A previous revision of
#    this file gated its bind on `[ -z "$(cat $UDC)" ]`, never bound the
#    gadget, and that whole boot came up with no USB at all. Working
#    instead: while/if/until/case, &&, ||, !, $( ), $(( )) and command exit
#    statuses; toolbox `cmp` exists (but not -s); there is no grep/sed/tr/
#    wc/head, and there is no /tmp directory.
#
# 2. Gadget rules: never write "" to the UDC - that unbinds the gadget and
#    the host sees the cable as pulled; an unbind/rebind loop flapped the
#    link every 3-6 s until the dwc3 PHY wedged. Bind only while the UDC is
#    unbound, and keep re-binding for the whole boot: closing the ffs
#    instance (adbd restart / host disconnect) unbinds the gadget.
#
# The unbound UDC attribute reads exactly "\n" (verified with the Donut
# shell + cmp against a live unbound gadget), so a one-newline reference
# file is the emptiness test.
#
# adbd has written its descriptors + strings by ~6 s into the boot; wait
# 12 s so the first write cannot race it into a -ENODEV ("failed to start
# g1: -19"), then poll every 3 s. One kmsg tick per attempt so a boot
# without adb can be diagnosed from /cache/kmsg.txt.
UDC=/config/usb_gadget/g1/UDC
echo "usbudc: start" > /dev/kmsg
sleep 12

i=0
while true; do
    i=$((i + 1))
    # Bind unconditionally: writing the UDC name to an already-bound UDC
    # returns -EBUSY (harmless, no unbind), and to an unbound one binds it.
    # Never write "" (that unbinds and flaps the host link). No reference
    # file needed - /cache/usbudc-unbound was unreliable (missing on some
    # boots, which silently disabled binding for the whole boot).
    echo a800000.dwc3 > $UDC 2>/dev/null
    echo "usbudc: tick $i" > /dev/kmsg
    sleep 3
done
