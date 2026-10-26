#!/usr/bin/env python3
"""Patch the KitKat 4.4 static servicemanager copy so it manages vndbinder.

The A15 (LOS 22.2) cnss-daemon's libbinder waits forever for a context
manager on /dev/vndbinder. This kernel has vndbinder (misc 10:54) but no
/vndservicemanager. The KitKat servicemanager is a 9.6 KB static binary that
hardcodes "/dev/binder"; patching that string to the shorter "/dev/vb" and
symlinking /dev/vb -> /dev/vndbinder gives the vndbinder domain a working
context manager that the A15 libbinder accepts (binder protocol v8 both
sides; verified with vpoke PING -> BR_REPLY).
"""
import sys

src, dst = sys.argv[1], sys.argv[2]
d = open(src, 'rb').read()
old = b'/dev/binder\x00'
i = d.find(old)
assert i >= 0 and d.count(old) == 1, 'unexpected binary layout'
d = d[:i] + b'/dev/vb\x00' + b'\x00' * 4 + d[i + 12:]
open(dst, 'wb').write(d)
print('patched', dst)
