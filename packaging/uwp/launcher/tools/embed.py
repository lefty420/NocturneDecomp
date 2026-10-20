#!/usr/bin/env python3
"""Turn font files into a C++ source of byte arrays: embed.py out.cpp name=file ..."""
import sys

out = open(sys.argv[1], "w")
out.write('extern "C" {\n')
for spec in sys.argv[2:]:
    name, path = spec.split("=", 1)
    data = open(path, "rb").read()
    out.write(f"extern const unsigned char {name}[] = {{\n")
    for i in range(0, len(data), 24):
        out.write(",".join(str(b) for b in data[i:i + 24]) + ",\n")
    out.write("0};\n")
    out.write(f"extern const unsigned long {name}_size = {len(data)};\n")
out.write("}\n")
