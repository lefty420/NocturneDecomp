#!/usr/bin/env python3
"""Compile the whole game (decompiled sources + shims) for UWP with clang-cl, then link it with
MSVC's link.exe and report what is unresolved. Used by the uwp-app workflow.

Objects are cached by content hash so a rerun only recompiles what changed.
Results are printed as GitHub Actions notice annotations and written under --out.
"""
import argparse
import collections
import concurrent.futures as cf
import glob
import hashlib
import os
import re
import shutil
import subprocess
import sys
import time


def annotate(title, text, limit=1800):
    t = text.strip() or "(empty)"
    if len(t) > limit:
        t = t[:limit] + " ...(truncated)"
    t = t.replace("%", "%25").replace("\r", "").replace("\n", "%0A")
    print(f"::notice title={title}::{t}", flush=True)


def sha1_files(paths):
    h = hashlib.sha1()
    for p in sorted(paths):
        h.update(p.encode())
        with open(p, "rb") as f:
            h.update(f.read())
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pseudocode", required=True)
    ap.add_argument("--sources-dir", required=True, help="dir with sources_*.txt from collect_sources.py")
    ap.add_argument("--gen-dir", required=True, help="dir holding nocturne_version.h")
    ap.add_argument("--include", action="append", default=[])
    ap.add_argument("--lib", action="append", default=[], help="libraries to link (paths or names)")
    ap.add_argument("--libpath", action="append", default=[])
    ap.add_argument("--obj-dir", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--limit", type=int, default=0, help="compile only the first N files (debugging)")
    a = ap.parse_args()

    R = a.pseudocode
    os.makedirs(a.obj_dir, exist_ok=True)
    os.makedirs(a.out, exist_ok=True)

    game = []
    for lst in sorted(glob.glob(os.path.join(a.sources_dir, "sources_*.txt"))):
        with open(lst) as f:
            game += [l.strip() for l in f if l.strip()]
    game = sorted(set(game))
    shims = [p for p in glob.glob(os.path.join(R, "shims", "**", "*"), recursive=True)
             if p.endswith((".cpp", ".c")) and not p.endswith(("winsock_posix.c",))]
    shims.sort()
    sources = game + shims
    if a.limit:
        sources = sources[: a.limit]

    flags = ["/nologo", "/TP", "/std:c++17", "/EHsc", "/MD", "/Od", "/utf-8", "/c",
             "/clang:-Wno-everything", "/clang:-fwrapv", "/clang:-fno-strict-aliasing",
             "/DWINAPI_FAMILY=WINAPI_FAMILY_APP", "/D_WIN32_WINNT=0x0A00", "/DNOCTURNE_GLES=1",
             "/DNOCTURNE_VERSION_GENERATED=1", "/D_CRT_SECURE_NO_WARNINGS", "/D_CRT_NONSTDC_NO_WARNINGS"]
    incs = [os.path.join(R, "include"), os.path.join(R, "shims"), a.gen_dir] + a.include
    flags += ["/I" + i for i in incs]

    headers = [p for root in (os.path.join(R, "include"), os.path.join(R, "shims"), a.gen_dir)
               for p in glob.glob(os.path.join(root, "**", "*.h"), recursive=True)]
    digest = sha1_files(headers) + hashlib.sha1(" ".join(flags).encode()).hexdigest()

    def obj_for(src):
        rel = os.path.relpath(src, R).replace("\\", "/")
        return os.path.join(a.obj_dir, hashlib.sha1(rel.encode()).hexdigest()[:10] + "_" + os.path.basename(src) + ".obj"), rel

    def compile_one(src):
        obj, rel = obj_for(src)
        stamp = obj + ".stamp"
        with open(src, "rb") as f:
            key = hashlib.sha1(f.read() + digest.encode()).hexdigest()
        if os.path.exists(obj) and os.path.exists(stamp) and open(stamp).read() == key:
            return rel, "cached", []
        p = subprocess.run(["clang-cl"] + flags + ["/Fo" + obj, src], capture_output=True, text=True, errors="replace")
        errs = [l for l in (p.stdout + "\n" + p.stderr).splitlines() if re.search(r": (fatal )?error", l)]
        if p.returncode != 0 or not os.path.exists(obj):
            if os.path.exists(obj):
                os.remove(obj)
            return rel, "failed", errs or [(p.stdout + p.stderr)[-300:]]
        with open(stamp, "w") as f:
            f.write(key)
        return rel, "compiled", []

    t0 = time.time()
    counts = collections.Counter()
    failures = []
    done = 0
    with cf.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        for rel, state, errs in ex.map(compile_one, sources):
            counts[state] += 1
            done += 1
            if state == "failed":
                failures.append((rel, errs))
            if done % 500 == 0:
                print(f"  {done}/{len(sources)} ({time.time() - t0:.0f}s) {dict(counts)}", flush=True)
    annotate("Compile",
             f"{len(sources)} files ({len(game)} game, {len(shims)} shims) in {time.time() - t0:.0f}s\n"
             f"compiled {counts['compiled']}, cached {counts['cached']}, failed {counts['failed']}")
    with open(os.path.join(a.out, "compile-failures.log"), "w") as f:
        for rel, errs in failures:
            f.write(f"##### {rel}\n" + "\n".join(errs) + "\n")
    if failures:
        lines = [f"{rel}: {errs[0][:200] if errs else ''}" for rel, errs in failures[:14]]
        annotate("Compile failures", "\n".join(lines))
        return 0   # cannot link without every object

    # ---- link
    objs = [obj_for(s)[0] for s in sources]
    rsp = os.path.join(a.out, "link.rsp")
    libdir = os.path.join(a.out, "libs")
    os.makedirs(libdir, exist_ok=True)
    libs = []
    for l in a.lib:
        if os.path.isfile(l) and l.endswith(".a"):       # FFmpeg's MSVC archives are named .a
            dst = os.path.join(libdir, os.path.basename(l)[:-2] + ".lib")
            shutil.copy(l, dst)
            libs.append(dst)
        else:
            libs.append(l)
    with open(rsp, "w") as f:
        for o in objs:
            f.write('"' + o + '"\n')
    exe = os.path.join(a.out, "nocturne_link.exe")
    cmd = ["link", "@" + rsp, "/nologo", "/OUT:" + exe, "/MACHINE:X64", "/APPCONTAINER", "/SUBSYSTEM:WINDOWS",
           "/ENTRY:mainCRTStartup", "/DEBUG:NONE", "/INCREMENTAL:NO", "/OPT:NOREF"] + \
          ["/LIBPATH:" + p for p in a.libpath] + libs
    t1 = time.time()
    p = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
    log = p.stdout + "\n" + p.stderr
    with open(os.path.join(a.out, "link.log"), "w") as f:
        f.write(log)
    annotate("Link", f"exit {p.returncode} in {time.time() - t1:.0f}s; exe exists: {os.path.exists(exe)}")

    unresolved = collections.Counter()
    for m in re.finditer(r"LNK2(?:019|001): unresolved external symbol (.+?)(?: referenced in function| in function|$)", log, re.M):
        unresolved[m.group(1).strip()] += 1
    imports = {s: n for s, n in unresolved.items() if s.startswith("__imp_")}
    others = {s: n for s, n in unresolved.items() if not s.startswith("__imp_")}
    other_lines = [l for l in log.splitlines() if "error LNK" in l and "unresolved external" not in l]
    with open(os.path.join(a.out, "unresolved.txt"), "w") as f:
        for s, n in sorted(unresolved.items(), key=lambda t: -t[1]):
            f.write(f"{n}\t{s}\n")
    annotate("Unresolved symbols",
             f"{len(unresolved)} distinct: {len(imports)} Windows imports (__imp_*), {len(others)} others\n"
             f"other link errors: {len(other_lines)}")
    top_imp = sorted(imports.items(), key=lambda t: -t[1])
    for i in range(0, min(len(top_imp), 60), 20):
        annotate(f"Missing Windows imports {i + 1}-{min(i + 20, len(top_imp))}",
                 "\n".join(f"{n}x {s[6:]}" for s, n in top_imp[i:i + 20]))
    top_oth = sorted(others.items(), key=lambda t: -t[1])
    for i in range(0, min(len(top_oth), 40), 20):
        annotate(f"Other unresolved {i + 1}-{min(i + 20, len(top_oth))}",
                 "\n".join(f"{n}x {s[:150]}" for s, n in top_oth[i:i + 20]))
    if other_lines:
        annotate("Other link errors", "\n".join(l.strip()[:220] for l in other_lines[:10]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
