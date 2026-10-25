"""Resolve profiler addresses against an lld-link /MAP file and print the result as GitHub annotations.

  map_symbols.py nocturne.map [rvas.txt]

rvas.txt holds the `prof-rva:` lines copied from stderr.log ("<rva-hex>:<samples>" pairs). Without it only the
map's head and symbol count are reported (a format check).
"""
import bisect
import re
import sys
from collections import defaultdict


def notice(title, text):
    t = text.strip().replace("%", "%25").replace("\r", "").replace("\n", "%0A")
    print(f"::notice title={title}::{t}", flush=True)


lines = open(sys.argv[1], errors="replace").read().splitlines()
if len(sys.argv) < 4:
    notice("map head", "\n".join(l[:120] for l in lines[:14]))
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
if len(sys.argv) < 4:
    notice("map symbols", "code symbols: %d; first %s" % (len(syms), syms[:3]))
label = sys.argv[3] if len(sys.argv) > 3 else "hot functions"
if len(sys.argv) < 3 or not syms:
    sys.exit(0)

addrs = [a for a, _ in syms]
total = 0
by_fn = defaultdict(int)
for tok in re.findall(r"([0-9a-fA-F]+):(\d+)", open(sys.argv[2], errors="replace").read()):
    rva, n = int(tok[0], 16), int(tok[1])
    i = bisect.bisect_right(addrs, rva) - 1
    name = syms[i][1] if i >= 0 else "?"
    by_fn[name] += n
    total += n
rows = sorted(by_fn.items(), key=lambda kv: -kv[1])
out, chunk = [], []
for name, n in rows[:60]:
    chunk.append("%4.1f%% %s" % (100.0 * n / max(total, 1), name[:90]))
    if sum(len(c) + 1 for c in chunk) > 1500:
        out.append("\n".join(chunk)); chunk = []
if chunk:
    out.append("\n".join(chunk))
for k, c in enumerate(out[:6]):
    notice("%s %d (of %d samples)" % (label, k + 1, total), c)
