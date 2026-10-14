#!/usr/bin/env python3
# ported from pdx203-jellybean by @chardidathing, https://github.com/chardidathing/pdx203-jellybean
# rewrites emugen's *(T *)(ptr + n) reads into Unpack<T>(ptr + n). the wire format packs
# args back to back, so a float after a GLboolean is unaligned, and arm32 vfp loads fault on that
import re
import sys

head = re.compile(r'\*\((?P<type>[A-Za-z_][A-Za-z0-9_ ]*?)\s*\*\)\s*\(ptr')

def rewrite(src):
    out, i = [], 0
    while True:
        m = head.search(src, i)
        if not m:
            out.append(src[i:])
            return ''.join(out)
        out.append(src[i:m.start()])
        # m ends just after "(ptr", walk to the matching close paren of that group
        j, depth = m.end(), 1
        while depth:
            c = src[j]
            depth += c == '('
            depth -= c == ')'
            j += 1
        inner = rewrite(src[m.end() - 3:j - 1])
        out.append('Unpack<%s>(%s)' % (m.group('type').strip(), inner))
        i = j

for path in sys.argv[1:]:
    s = open(path).read()
    open(path, 'w').write(rewrite(s))
