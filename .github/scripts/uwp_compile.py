#!/usr/bin/env python3
"""Syntax-compile a sample of the decompiled game sources and report what fails.

Used by the xbox-uwp workflow (see docs/xbox-uwp.md). Informational: always exits 0.

  --mode msvc   clang-cl under WINAPI_FAMILY_APP, with the MSVC x64 environment
                imported from vcvars64.bat (what CI runs on Windows)
  --mode gnu    clang++ -fsyntax-only, so the script's own logic can be tested on Linux

Results are printed as GitHub Actions notice annotations (readable on the run page
without signing in) and written in full under --out.
"""
import argparse
import collections
import concurrent.futures as cf
import glob
import os
import re
import subprocess
import sys

ERR = re.compile(r"^(?P<loc>.*?): (?:fatal )?error: (?P<msg>.*)$")


def annotate(title, text, limit=1800):
    t = text.strip()
    if len(t) > limit:
        t = t[:limit] + " ...(truncated)"
    t = t.replace("%", "%25").replace("\r", "").replace("\n", "%0A")
    print(f"::notice title={title}::{t}", flush=True)


def msvc_env():
    """The environment vcvars64.bat sets up, as a dict."""
    vswhere = os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                           "Microsoft Visual Studio", "Installer", "vswhere.exe")
    vs = subprocess.run([vswhere, "-latest", "-products", "*", "-property", "installationPath"],
                        capture_output=True, text=True).stdout.strip()
    vcvars = os.path.join(vs, "VC", "Auxiliary", "Build", "vcvars64.bat")
    out = subprocess.run(f'cmd /c ""{vcvars}" >nul && set"', capture_output=True, text=True,
                         shell=True).stdout
    env = dict(os.environ)
    for line in out.splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            env[k] = v
    return env


def build_cmd(mode, includes):
    if mode == "msvc":
        return (["clang-cl", "/nologo", "/TP", "/std:c++17", "/Zs", "/clang:-Wno-everything",
                 "/DWINAPI_FAMILY=WINAPI_FAMILY_APP", "/D_WIN32_WINNT=0x0A00", "/DNOCTURNE_GLES=1"]
                + ["/I" + i for i in includes])
    return (["clang++", "-fsyntax-only", "-std=gnu++17", "-x", "c++", "-Wno-everything"]
            + ["-I" + i for i in includes])


def compile_one(cmd, env, path):
    p = subprocess.run(cmd + [path], capture_output=True, text=True, env=env, errors="replace")
    errs = [l for l in (p.stdout + "\n" + p.stderr).splitlines() if ERR.match(l)]
    return path, errs


def short(line, root):
    return line.replace(root, "").replace("\\", "/") if root else line


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mode", choices=["msvc", "gnu"], required=True)
    ap.add_argument("--sources-dir", required=True, help="dir holding sources_*.txt lists")
    ap.add_argument("--stride", type=int, default=20, help="compile every Nth file")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--include", action="append", default=[])
    ap.add_argument("--root", default="", help="path prefix to strip from messages")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    files = []
    for lst in sorted(glob.glob(os.path.join(a.sources_dir, "sources_*.txt"))):
        with open(lst) as f:
            files += [l.strip() for l in f if l.strip()]
    files = sorted(set(files))
    sample = files[::max(1, a.stride)]

    env = msvc_env() if a.mode == "msvc" else dict(os.environ)
    cmd = build_cmd(a.mode, a.include)

    results = []
    with cf.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        for r in ex.map(lambda p: compile_one(cmd, env, p), sample):
            results.append(r)

    clean = [p for p, e in results if not e]
    bad = sorted(((p, e) for p, e in results if e), key=lambda t: -len(t[1]))
    total = sum(len(e) for _, e in results)

    fam_count = collections.Counter()
    fam_files = collections.defaultdict(set)
    fam_example = {}
    for p, errs in results:
        for e in errs:
            m = ERR.match(e)
            fam = re.sub(r"\d+", "N", re.sub(r"'[^']*'", "'_'", m.group("msg")))
            fam_count[fam] += 1
            fam_files[fam].add(p)
            fam_example.setdefault(fam, e)

    os.makedirs(a.out, exist_ok=True)
    with open(os.path.join(a.out, "game-compile.log"), "w") as f:
        for p, errs in bad:
            f.write(f"##### {short(p, a.root)} ({len(errs)} errors)\n")
            f.write("\n".join(short(e, a.root) for e in errs) + "\n")

    annotate("Game compile totals",
             f"population: {len(files)} files, sampled every {a.stride}th: {len(sample)}\n"
             f"clean: {len(clean)}, with errors: {len(bad)}, total errors: {total}\n"
             f"error families: {len(fam_count)}")
    top = fam_count.most_common(24)
    lines = [f"{n}x in {len(fam_files[f])} files: {f}" for f, n in top]
    for i in range(0, len(lines), 8):
        annotate(f"Error families {i + 1}-{min(i + 8, len(lines))}", "\n".join(lines[i:i + 8]))
    ex = [short(fam_example[f], a.root)[:260] for f, _ in top[:12]]
    for i in range(0, len(ex), 4):
        annotate(f"Examples {i + 1}-{min(i + 4, len(ex))}", "\n".join(ex[i:i + 4]))
    worst = [f"{len(e)}  {short(p, a.root)}" for p, e in bad[:12]]
    annotate("Worst files", "\n".join(worst) if worst else "(none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
