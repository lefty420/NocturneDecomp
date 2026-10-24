"""Print "SYM <rva> <name>" for every code symbol in an lld-link /MAP file, so the log can be searched later."""
import re
import sys

lines = open(sys.argv[1], errors="replace").read().splitlines()
print("map head:")
for l in lines[:12]:
    print("  | " + l[:140])
pat = re.compile(r"^\s*([0-9a-fA-F]{4}):([0-9a-fA-F]{8})\s+(\S+)\s+([0-9a-fA-F]{8,16})\b")
syms = []
for l in lines:
    m = pat.match(l)
    if m and m.group(1) == "0001":
        rva = int(m.group(4), 16)
        if rva >= 0x140000000:
            rva -= 0x140000000
        syms.append((rva, m.group(3)))
syms.sort()
print("code symbols: %d" % len(syms))
for rva, name in syms:
    print("SYM %x %s" % (rva, name))
